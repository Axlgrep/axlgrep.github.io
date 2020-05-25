#ifndef INET_CONN_H_
#define INET_CONN_H_

#include "string.h"
#include "iostream"

#define READ_BUFFER_SIZE 1024 * 1024

enum ReadStatus {
  kReadHalf = 0,
  kReadAll = 1,
  kReadError = 2,
  kReadClose = 3,
  kFullError = 4,
  kParseError = 5,
  kDealError = 6,
};

enum WriteStatus {
  kWriteHalf = 0,
  kWriteAll = 1,
  kWriteError = 2,
};

class InetConn {
 public:
  InetConn(const int fd, const std::string& ip_port);
  ~InetConn() {}

  bool SetNonblock();
  bool EmptyReadBuf();
  std::string IpPort();

  ReadStatus GetRequest();
  WriteStatus SendReply();

 private:
  int fd_;
  std::string ip_port_;
  size_t last_read_pos_;
  char read_buf_[1024 * 1024];
  size_t last_write_pos_;
  std::string reply;

  InetConn(const InetConn&);
  void operator=(const InetConn&);
};

#endif
