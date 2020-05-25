#ifndef __UNIX_SOCKET_H__
#define __UNIX_SOCKET_H__

int unix_server_listen(const char *name);
int unix_server_accept(int listenfd, uid_t *uidptr);

int unix_client_conn(const char *name);

int send_fd(int fd, int fd_to_send);
int recv_fd(int fd, ssize_t (*userfunc)(int, const void*, size_t));
#endif
