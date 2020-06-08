#include "stddef.h"
#include "stdint.h"
#include "malloc.h"
#include "memory.h"
#include "unistd.h"
#include "sys/uio.h"
#include "sys/socket.h"

#include "epoll.h"
#include "unix_client.h"

UnixClient::UnixClient(const int fd): fd_(fd) {
  cmptr = (struct cmsghdr*)malloc(CMSG_LEN(sizeof(int)));
}

UnixClient::~UnixClient() {
  free(cmptr);
}

bool UnixClient::GetReply() {
  char          msgReplyBuf[8];
  struct iovec  iov[1];
  struct msghdr msg;

  iov[0].iov_base = msgReplyBuf;
  iov[0].iov_len = sizeof(msgReplyBuf);

  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;

  int readlen = recvmsg(fd_, &msg, 0);
  return true;
}

SendMsgStatus UnixClient::TransferListenFd(const int listen_fd) {
  char          msgTypeBuf[4];
  char          msgDataBuf[128];
  struct iovec  iov[2];
  struct msghdr msg;

  memset(msgTypeBuf, 0, sizeof(msgTypeBuf));
  uint32_t msgType = MsgRequestType::kTransferListenFd;
  memcpy(msgTypeBuf, &msgType, sizeof(msgType));

  memset(msgDataBuf, 0, sizeof(msgDataBuf));
  memcpy(msgDataBuf, "listen fd data", 15);

  iov[0].iov_base = msgTypeBuf;
  iov[0].iov_len = sizeof(msgTypeBuf);
  iov[1].iov_base = msgDataBuf;
  iov[1].iov_len = sizeof(msgDataBuf);

  msg.msg_iov = iov;
  msg.msg_iovlen = 2;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;

  cmptr->cmsg_level = SOL_SOCKET;
  cmptr->cmsg_type = SCM_RIGHTS;
  cmptr->cmsg_len = CMSG_LEN(sizeof(int));
  *(int*)CMSG_DATA(cmptr) = listen_fd;

  msg.msg_control = cmptr;
  msg.msg_controllen = CMSG_LEN(sizeof(int));

  int sendlen = sendmsg(fd_, &msg, 0);
  if (sendlen != sizeof(msgTypeBuf) + sizeof(msgDataBuf)) {
    return kSendMsgError;
  }
  GetReply();
  return kSendMsgOk;
}

SendMsgStatus UnixClient::TransferSessionFd(const int session_fd) {
  char          msgTypeBuf[4];
  char          msgDataBuf[128];
  struct iovec  iov[2];
  struct msghdr msg;

  memset(msgTypeBuf, 0, sizeof(msgTypeBuf));
  uint32_t msgType = MsgRequestType::kTransferSessionFd;
  memcpy(msgTypeBuf, &msgType, sizeof(msgType));

  memset(msgDataBuf, 0, sizeof(msgDataBuf));
  memcpy(msgDataBuf, "session fd data", 16);

  iov[0].iov_base = msgTypeBuf;
  iov[0].iov_len = sizeof(msgTypeBuf);
  iov[1].iov_base = msgDataBuf;
  iov[1].iov_len = sizeof(msgDataBuf);

  msg.msg_iov = iov;
  msg.msg_iovlen = 2;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;

  cmptr->cmsg_level = SOL_SOCKET;
  cmptr->cmsg_type = SCM_RIGHTS;
  cmptr->cmsg_len = CMSG_LEN(sizeof(int));
  *(int*)CMSG_DATA(cmptr) = session_fd;

  msg.msg_control = cmptr;
  msg.msg_controllen = CMSG_LEN(sizeof(int));

  int sendlen = sendmsg(fd_, &msg, 0);
  if (sendlen != sizeof(msgTypeBuf) + sizeof(msgDataBuf)) {
    return kSendMsgError;
  }
  return kSendMsgOk;
}

