#include "../csapp.h"

#define N 10

int main() {
  int status, i;
  pid_t pid[N], retpid;

  /* Parent creates N children */
  for (i = 0; i < N; i++) {
    if ((pid[i] = Fork()) == 0) {

    }
  }
}

