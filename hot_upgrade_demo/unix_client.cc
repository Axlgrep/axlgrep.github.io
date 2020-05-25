#include "stddef.h"
#include "malloc.h"
#include "sys/uio.h"
#include "sys/socket.h"

#include "unix_client.h"

UnixClient::UnixClient(const int fd): fd_(fd) {
  cmptr = (struct cmsghdr*)malloc(CMSG_LEN(sizeof(int)));
}

UnixClient::~UnixClient() {
  free(cmptr);
}

SendMsgStatus UnixClient::SendFD(const int fd_to_send) {
  struct iovec  iov[1];
  struct msghdr msg;
  char          buf[2];

  iov[0].iov_base = buf;
  iov[0].iov_len  = 2;

  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;

  cmptr->cmsg_level = SOL_SOCKET;
  cmptr->cmsg_type = SCM_RIGHTS;
  cmptr->cmsg_len = CMSG_LEN(sizeof(int));

  msg.msg_control = cmptr;
  msg.msg_controllen = CMSG_LEN(sizeof(int));
  *(int*)CMSG_DATA(cmptr) = fd_to_send;
  buf[0] = buf[1] = 0;
  if (sendmsg(fd_, &msg, 0) != 2) {
    return kSendMsgError;
  }
  return kSendMsgOk;
}
