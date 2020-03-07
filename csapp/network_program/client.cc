#include "../csapp.h"

int open_clientfd(char *hostname, int port) {
  int clientfd;
  struct hostent *hp;
  struct sockaddr_in serveraddr;

  if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }

  if ((hp = gethostbyname(hostname)) == NULL) {
    return -2;
  }
  bzero((char*)&serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  bcopy((char*)hp->h_addr_list[0], (char*)&serveraddr.sin_addr.s_addr, hp->h_length);
  serveraddr.sin_port = htons(port);

  if (connect(clientfd, (SA*)&serveraddr, sizeof(serveraddr)) < 0) {
    return -1;
  }
  return clientfd;
}

int main(int argc, char **argv) {
  int clientfd, port;
  char *host, buf[MAXLINE];

  rio_t rio;

  if (argc != 3) {
    fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
    exit(0);
  }

  host = argv[1];
  port = atoi(argv[2]);

  clientfd = open_clientfd(host, port);
  rio_readinitb(&rio, clientfd);

  while (fgets(buf, MAXLINE, stdin) != NULL) {
    rio_writen(clientfd, buf, strlen(buf));
    rio_readlineb(&rio, buf, MAXLINE);
    fputs(buf, stdout);
  }
  close(clientfd);
  exit(0);
}
