#include "../csapp.h"

int main() {
  int fd = open("axl.txt", O_CREAT | O_APPEND | O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
  int ret = dup2(fd, STDOUT_FILENO);  // 将标准输出重定向到axl.txt
  printf("ret = %d, fd = %d, STDOUT = %d\n", ret, fd, STDOUT_FILENO);
  close(fd);
  exit(0);
}
