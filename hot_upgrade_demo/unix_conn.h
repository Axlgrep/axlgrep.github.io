#ifndef UNIX_CONN_H_
#define UNIX_CONN_H_

enum RecvMsgStatus {
  kRecvMsgOk = 0,
  kRecvMSGParseError = 1,
  kRecvMsgError = 2,
  kRecvMsgClose = 3,
};

class UnixConn {
 public:
  UnixConn(const int fd);
  ~UnixConn();

  int NewFd();
  RecvMsgStatus RecvFD();
 private:
  int fd_;
  int new_fd_;
  struct cmsghdr *cmptr;

  UnixConn(const UnixConn&);
  void operator=(const UnixConn&);
};

#endif
