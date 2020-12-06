#include "../csapp.h"

handler_t *Signal(int signum, handler_t *handler) {
  struct sigaction action, old_action;

  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);      /* Block sigs of type being handled */
  action.sa_flags = SA_RESTART;      /* Restart syscalls if possible */

  if (sigaction(signum, &action, &old_action) < 0) {
    unix_error("Signal error");
  }
  return (old_action.sa_handler);
}

void handler2(int sig) {
  pid_t pid;
  while ((pid = waitpid(-1, NULL, 0)) > 0) {
    printf("Handler reaped child %d\n", (int)pid);
  }
  if (errno != ECHILD) {
    unix_error("waitpid error");
  }
  sleep(2);
  return;
}

int main() {
  int i, n;
  char buf[MAXBUF];
  pid_t pid;

  Signal(SIGCHLD, handler2);  /* sigaction error-hanling warpper */

  /* Parent creates children */
  for (i = 0; i < 3; i++) {
    pid = fork();
    if (pid == 0) {
      printf("Hello from child %d\n", (int)getpid());
      sleep(1);
      exit(0);
    }
  }

  /* Parent waits for terminal input and then processes it */
  if ((n = read(STDIN_FILENO, buf, sizeof(buf))) < 0) {
    unix_error("read error");
  }

  printf("Parent processing input\n");
  while(1);
  exit(0);
}
