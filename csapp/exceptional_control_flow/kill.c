#include "../csapp.h"

int main() {
  pid_t pid;
  /* Child sleeps until SIGKILL signal received, then dies */
  if ((pid = fork()) == 0) {
    pause(); /* Wait for a signal to arrive */
    printf("control should never reach here!\n");
    exit(0);
  }

  /* parent sends a SIGKILL signal to a child */
  kill(pid, SIGKILL);
  exit(0);
}
