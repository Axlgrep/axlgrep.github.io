#include "stddef.h"
#include "stdint.h"
#include "string.h"
#include "malloc.h"
#include "sys/uio.h"
#include "sys/socket.h"

#include "iostream"

#include "epoll.h"
#include "assert.h"
#include "unix_conn.h"

/* size of control buffer to send/recv one file descriptor */
#define MAXLINE 1024

UnixConn::UnixConn(const int fd): fd_(fd) {
  cmptr = (struct cmsghdr*)malloc(CMSG_LEN(sizeof(int)));
  msgDataBuf = (char*)malloc(MSG_DATA_BUF_LEN);
}

UnixConn::~UnixConn() {
  free(cmptr);
  free(msgDataBuf);
}

RecvMsgStatus UnixConn::processRecv() {
  struct iovec  iov[1];
  struct msghdr msg;

  iov[0].iov_base = msgDataBuf;
  iov[0].iov_len = MSG_DATA_BUF_LEN;

  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_control = cmptr;
  msg.msg_controllen = CMSG_LEN(sizeof(int));

  int64_t read = 0;
  std::string requestStr;
  while (read < MSG_DATA_BUF_LEN) {
    int64_t readlen = recvmsg(fd_, &msg, 0);
    if (readlen == 0) {
      return kRecvMsgClose;
    }

    if (readlen == -1) {
      if (errno == EAGAIN || errno == EINTR) {
        continue;
      }
      perror("recvmsg");
      return kRecvMsgClose;
    }
    requestStr.append(msgDataBuf, readlen);
    read += readlen;
    //printf("readlen: %ld\n", read);
  }

  int fd = *(int*)CMSG_DATA(msg.msg_control);

  uint32_t msgType = 0;
  memcpy(&msgType, requestStr.data(), sizeof(uint32_t));
  requestStr = requestStr.substr(sizeof(uint32_t));

  if (msgType == MsgRequestType::kTransferListenFd) {
    HandleListenFdMsg(fd, requestStr);
  } else if (msgType == MsgRequestType::kTransferSessionFd) {
    HandleSessionFdMsg(fd, requestStr);
  } else {
    return kRecvMSGParseError;
  }
  return kRecvMsgOk;
}

bool UnixConn::makeCommonReply() {
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

  memcpy(msgReplyBuf, reply_ok.data(), reply_ok.size());
  msgReplyBuf[reply_ok.size()] = 0;

  int sendlen = sendmsg(fd_, &msg, 0);
  //printf("common reply sendlen: %d\n", sendlen);
  if (sendlen != sizeof(msgReplyBuf)) {
    return false;
  }
  return true;
}

bool UnixConn::HandleListenFdMsg(int fd, const std::string& buffer) {

  uint32_t dataLen = 0;
  memcpy(&dataLen, buffer.data(), sizeof(uint32_t));
  assert(buffer.size() > sizeof(uint32_t));
  std::string data = buffer.substr(sizeof(uint32_t));

  printf("receive listenFD success, dataLen: %d, data: %s\n", dataLen, data.data());
  Epoll::getInstance()->attachInetListenFd(fd);
  makeCommonReply();
  return true;
}

bool UnixConn::HandleSessionFdMsg(int fd, const std::string& buffer) {

  uint32_t dataLen = 0;
  memcpy(&dataLen, buffer.data(), sizeof(uint32_t));
  assert(buffer.size() > sizeof(uint32_t));
  std::string data = buffer.substr(sizeof(uint32_t));

  printf("receive tcp connection success, dataLen: %d, data: %s\n", dataLen, data.data());
  InetConn *conn = new InetConn(fd, "received conn tmp no ip_port");
  conn->SetNonblock();
  Epoll::getInstance()->inet_conn_map[fd] = conn;
  Epoll::getInstance()->AddEvent(fd, EPOLLIN | EPOLLET);
  makeCommonReply();
  return true;
}

SendMsgStatus UnixConn::processSend() {
}
