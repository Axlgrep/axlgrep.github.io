#ifndef UNIX_CLIENT_H_
#define UNIX_CLIENT_H_

#include "unix_socket.h"

class UnixClient {
 public:
  UnixClient(const int fd);
  ~UnixClient();

  bool GetReply();
  SendMsgStatus TransferListenFd(const int listen_fd);
  SendMsgStatus TransferSessionFd(const int session_fd);
 private:
  int fd_;
  struct cmsghdr *cmptr;

  UnixClient(const UnixClient&);
  void operator=(const UnixClient&);
};

#endif
