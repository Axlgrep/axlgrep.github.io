#include "../csapp.h"

void handler1(int sig) {
  pid_t pid;
  if ((pid = waitpid(-1, NULL, 0)) < 0) {
    unix_error("waitpid error");
  }
  printf("Handler reaped child %d\n", (int)pid);
  sleep(2);
  return;
}

int main() {
  int i, n;
  char buf[MAXBUF];

  if (signal(SIGCHLD, handler1) == SIG_ERR) {
    unix_error("signal error");
  }

  /* Parent creates children */
  for (i = 0; i < 3; i++) {
    if (fork() == 0) {
      printf("Hello from child %d\n", (int)getpid());
      sleep(1);
      exit(0);
    }
  }

  /* parent waits for terminal input and then processes it */
  if ((n = read(STDIN_FILENO, buf, sizeof(buf))) < 0) {
    unix_error("read");
  }

  printf("parent processing input\n");
  while(1);
  exit(0);
}
