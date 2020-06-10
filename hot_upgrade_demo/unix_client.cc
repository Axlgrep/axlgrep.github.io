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
  msgDataBuf = (char*)malloc(MSG_DATA_BUF_LEN);
}

UnixClient::~UnixClient() {
  free(cmptr);
  free(msgDataBuf);
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
  struct iovec  iov[1];
  struct msghdr msg;

  uint32_t msgType = MsgRequestType::kTransferListenFd;
  std::string data = "listen fd data";
  uint32_t dataLen = data.size();

  memset(msgDataBuf, 0, MSG_DATA_BUF_LEN);
  memcpy(msgDataBuf, &msgType, sizeof(uint32_t));
  memcpy(msgDataBuf + sizeof(uint32_t), &dataLen, sizeof(uint32_t));
  memcpy(msgDataBuf + 2 * sizeof(uint32_t), data.data(), data.size());

  iov[0].iov_base = msgDataBuf;
  iov[0].iov_len = MSG_DATA_BUF_LEN;

  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;

  cmptr->cmsg_level = SOL_SOCKET;
  cmptr->cmsg_type = SCM_RIGHTS;
  cmptr->cmsg_len = CMSG_LEN(sizeof(int));
  *(int*)CMSG_DATA(cmptr) = listen_fd;

  msg.msg_control = cmptr;
  msg.msg_controllen = CMSG_LEN(sizeof(int));

  int64_t send = 0;
  while (send < MSG_DATA_BUF_LEN) {
    int64_t sendlen = sendmsg(fd_, &msg, 0);
    if (sendlen == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      return kSendMsgError;
    }
    send += sendlen;
    //printf("sendlen: %ld\n", send);
  }
  if (send != MSG_DATA_BUF_LEN) {
    return kSendMsgError;
  }
  GetReply();
  return kSendMsgOk;
}

SendMsgStatus UnixClient::TransferSessionFd(const int session_fd) {
  struct iovec  iov[1];
  struct msghdr msg;

  uint32_t msgType = MsgRequestType::kTransferSessionFd;
  std::string data = "session fd data";
  uint32_t dataLen = data.size();

  memset(msgDataBuf, 0, MSG_DATA_BUF_LEN);
  memcpy(msgDataBuf, &msgType, sizeof(uint32_t));
  memcpy(msgDataBuf + sizeof(uint32_t), &dataLen, sizeof(uint32_t));
  memcpy(msgDataBuf + 2 * sizeof(uint32_t), data.data(), data.size());

  iov[0].iov_base = msgDataBuf;
  iov[0].iov_len = MSG_DATA_BUF_LEN;

  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;

  cmptr->cmsg_level = SOL_SOCKET;
  cmptr->cmsg_type = SCM_RIGHTS;
  cmptr->cmsg_len = CMSG_LEN(sizeof(int));
  *(int*)CMSG_DATA(cmptr) = session_fd;

  msg.msg_control = cmptr;
  msg.msg_controllen = CMSG_LEN(sizeof(int));

  int64_t send = 0;
  while (send < MSG_DATA_BUF_LEN) {
    int64_t sendlen = sendmsg(fd_, &msg, 0);
    if (sendlen == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      perror("sendmsg");
      return kSendMsgError;
    }
    send += sendlen;
  }
  if (send != MSG_DATA_BUF_LEN) {
    return kSendMsgError;
  }
  GetReply();
  return kSendMsgOk;
}

