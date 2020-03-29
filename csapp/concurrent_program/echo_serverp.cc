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

void sigchld_handler(int sig) {
  while (waitpid(-1, 0, WNOHANG) > 0);
  return;
}

int main(int argc, char **argv) {
  int listenfd, connfd, port;
  socklen_t clientlen = sizeof(struct sockaddr_in);
  struct sockaddr_in clientaddr;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  port = atoi(argv[1]);

  signal(SIGCHLD, sigchld_handler);
  listenfd = open_listenfd(port);
  while (true) {
    connfd = accept(listenfd, (SA*)&clientaddr, &clientlen);
    printf("server accept client(%s:%d), fd = %d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), connfd);

    if (fork() == 0) {
      close(listenfd);
      echo(connfd);
      close(connfd);
      printf("server close client, fd = %d\n", connfd);
      exit(0);
    }
    close(connfd);
  }
}
