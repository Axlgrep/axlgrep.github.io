#ifndef __UNIX_SOCKET_H__
#define __UNIX_SOCKET_H__

#include "string.h"
#include "iostream"

const std::string reply_ok = "ok";

enum MsgRequestType {
  kAuth = 0,
  kTransferListenFd = 1,
  kTransferAdminListenFd = 2,
  kTransferSessionFd = 3
};

enum SendMsgStatus {
  kSendMsgOk = 0,
  kSendMsgError = 1
};

enum RecvMsgStatus {
  kRecvMsgOk = 0,
  kRecvMSGParseError = 1,
  kRecvMsgError = 2,
  kRecvMsgClose = 3
};


int unix_server_listen(const char *name);
int unix_server_accept(int listenfd, uid_t *uidptr);

int unix_client_conn(const char *name);

int send_fd(int fd, int fd_to_send);
int recv_fd(int fd, ssize_t (*userfunc)(int, const void*, size_t));
#endif
