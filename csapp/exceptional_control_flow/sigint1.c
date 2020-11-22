#include "../csapp.h"

void handler(int sig) {  /* SIGINT handler */
  printf("Caught SIGINT\n");
  exit(0);
}

int main() {
  /* Install the SIGINT handler */
  if (signal(SIGINT, handler) == SIG_ERR) {
    unix_error("signal error");
  }
  pause();
  exit(0);
}
