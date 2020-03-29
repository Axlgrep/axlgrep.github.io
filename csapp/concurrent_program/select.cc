#include "../csapp.h"

void echo(int connfd) {
  size_t n;
  char buf[MAXLINE];
  rio_t rio;

  rio_readinitb(&rio, connfd);
  while ((n = rio_readlineb(&rio, buf, MAXLINE)) != 0) {
    printf("server received %lu bytes\n", n);
    rio_writen(connfd, buf, n);
  }
}

void command(void) {
  char buf[MAXLINE];
  if (!fgets(buf, MAXLINE, stdin)) {
    exit(0);
  }
  printf("%s", buf);
}

int open_listenfd(int port) {
  int listenfd, optval = 1;
  struct sockaddr_in serveraddr;

  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }

  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int)) < 0) {
    return -1;
  }

  bzero((char*)&serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned short)port);

  if (bind(listenfd, (SA*)&serveraddr, sizeof(serveraddr)) < 0) {
    return -1;
  }

  if (listen(listenfd, 1024) < 0) {
    return -1;
  }
  return listenfd;
}

int main(int argc, char **argv) {

  int listenfd, connfd, port;
  socklen_t clientlen = sizeof(struct sockaddr_in);
  struct sockaddr_in clientaddr;
  fd_set read_set, ready_set;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  port = atoi(argv[1]);
  listenfd = open_listenfd(port);

  FD_ZERO(&ready_set);
  FD_SET(STDIN_FILENO, &read_set);
  FD_SET(listenfd, &read_set);

  while (true) {
    ready_set = read_set;
    int n = select(listenfd + 1, &read_set, NULL, NULL, NULL);
    printf("num: %d fd ready\n", n);
    if (FD_ISSET(STDIN_FILENO, &ready_set)) {
      printf("STDIN_FILENO ready");
      command();
    }
    if (FD_ISSET(listenfd, &ready_set)) {
      printf("listenfd ready");
      connfd = accept(listenfd, (SA*)&clientaddr, &clientlen);
      echo(connfd);
      close(connfd);
    }
  }
}
