#ifndef __INET_SOCKET_H__
#define __INET_SOCKET_H__

int inet_server_listen(int port);
int inet_server_accept(int listenfd, std::string *const ip_port);

int inet_client_conn(const char *hostname, int port);

#endif
