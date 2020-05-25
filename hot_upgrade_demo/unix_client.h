#ifndef UNIX_CLIENT_H_
#define UNIX_CLIENT_H_

enum SendMsgStatus {
  kSendMsgOk = 0,
  kSendMsgError = 1,
};

class UnixClient {
 public:
  UnixClient(const int fd);
  ~UnixClient();

  SendMsgStatus SendFD(const int fd_to_send);
 private:
  int fd_;
  struct cmsghdr *cmptr;

  UnixClient(const UnixClient&);
  void operator=(const UnixClient&);
};

#endif
