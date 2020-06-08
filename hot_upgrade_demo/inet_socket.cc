#include "netinet/in.h"
#include "arpa/inet.h"

#include "string.h"
#include "netdb.h"

#include "iostream"

int inet_server_listen(int port) {
  int listenfd, optval = 1;
  struct sockaddr_in serveraddr;

  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }

  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int)) < 0) {
    return -1;
  }

  /*
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (const void*)&optval, sizeof(int)) < 0) {
    return -1;
  }
  */

  bzero((char*)&serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned short)port);

  if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
    return -1;
  }

  if (listen(listenfd, 1024) < 0) {
    return -1;
  }
  return listenfd;
}

int inet_server_accept(int listenfd, std::string *const ip_port) {
  struct sockaddr_in clientaddr;
  socklen_t clientlen = sizeof(clientaddr);
  int conn_fd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
  if (conn_fd == -1) {
    return -1;
  }
  *ip_port = std::string(inet_ntoa(clientaddr.sin_addr)) + ":" + std::to_string(ntohs(clientaddr.sin_port));
  return conn_fd;
}

int inet_client_conn(const char *hostname, int port) {
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

  if (connect(clientfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
    return -1;
  }
  return clientfd;
}

