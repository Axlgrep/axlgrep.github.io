#ifndef UNIX_CONN_H_
#define UNIX_CONN_H_

#include "unix_socket.h"

class UnixConn {
 public:
  UnixConn(const int fd);
  ~UnixConn();

  int NewFd();
  RecvMsgStatus processRecv();
  SendMsgStatus processSend();

  bool makeCommonReply();
  bool HandleListenFdMsg(const msghdr& msg);
  bool HandleSessionFdMsg(const msghdr& msg);

 private:
  int fd_;
  int new_fd_;
  struct cmsghdr *cmptr;

  UnixConn(const UnixConn&);
  void operator=(const UnixConn&);
};

#endif
