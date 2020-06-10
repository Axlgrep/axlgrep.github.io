#ifndef UNIX_CONN_H_
#define UNIX_CONN_H_

#include "unix_socket.h"

class UnixConn {
 public:
  UnixConn(const int fd);
  ~UnixConn();

  RecvMsgStatus processRecv();
  SendMsgStatus processSend();

  bool makeCommonReply();
  bool HandleListenFdMsg(int fd, const std::string& buffer);
  bool HandleSessionFdMsg(int fd, const std::string& buffer);

 private:
  int fd_;
  struct cmsghdr *cmptr;
  char *msgDataBuf;

  UnixConn(const UnixConn&);
  void operator=(const UnixConn&);
};

#endif
