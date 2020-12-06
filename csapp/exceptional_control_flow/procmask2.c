#include "../csapp.h"

void handler(int sig) {
  pid_t pid;
  while ((pid = waitpid(-1, NULL, 0)) > 0) {  /* Reap a zombie child */
      deletejob(pid);  /* Delete the child from the job list */
  }
  if (errno != ECHILD) {
    unix_error("waitpid error");
  }
}

int main(int argc, char **argv) {
  int pid;
  sigset_t mask;

  signal(SIGCHLD, handler);
  initjobs();  /* Initialize the job list */

  while (1) {
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL); /* Block SIGCHLD */

    /* Child process */
    if (pid = fork() == 0) {
      sigprocmask(SIG_UNBLOCK, &mask, NULL); 
      Execve("/bin/date", argv, NULL);
    }
    
    /* Parent process */
    addjob(pid);  /* Add the child to the job list */
    sigprocmask(SIG_UNBLOCK, &mask, NULL);  /* Unblock SIGCHLD */
  }
}
