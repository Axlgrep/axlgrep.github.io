#include "stddef.h"
#include "malloc.h"
#include "sys/uio.h"
#include "sys/socket.h"

#include "unix_conn.h"

/* size of control buffer to send/recv one file descriptor */
#define MAXLINE 1024

UnixConn::UnixConn(const int fd): fd_(fd) {
  cmptr = (struct cmsghdr*)malloc(CMSG_LEN(sizeof(int)));
}

UnixConn::~UnixConn() {
  free(cmptr);
}

int UnixConn::NewFd() {
  return new_fd_;
}

RecvMsgStatus UnixConn::RecvFD() {
  int           newfd, nr, status = -1;
  char          *ptr = NULL;
  char          buf[MAXLINE];
  struct iovec  iov[1];
  struct msghdr msg;

  iov[0].iov_base = buf;
  iov[0].iov_len = sizeof(buf);

  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_control = cmptr;
  msg.msg_controllen = CMSG_LEN(sizeof(int));

  if ((nr = recvmsg(fd_, &msg, 0)) < 0) {
    return kRecvMsgError;
  } else if (nr == 0) {
    return kRecvMsgClose;
  }

  if (nr == 2 && buf[0] == 0 && buf[1] == 0) {
    if (msg.msg_controllen != CMSG_LEN(sizeof(int))) {
      printf("kRecvMSGParseError\n");
      return kRecvMSGParseError;
    }
    new_fd_ = *(int*)CMSG_DATA(cmptr);
    return kRecvMsgOk;
  }
  return kRecvMSGParseError;
}
