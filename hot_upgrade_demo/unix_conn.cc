#include "stddef.h"
#include "stdint.h"
#include "string.h"
#include "malloc.h"
#include "sys/uio.h"
#include "sys/socket.h"

#include "iostream"

#include "epoll.h"
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

RecvMsgStatus UnixConn::processRecv() {
  char          msgTypeBuf[4];
  char          msgDataBuf[128];
  struct iovec  iov[2];
  struct msghdr msg;

  iov[0].iov_base = msgTypeBuf;
  iov[0].iov_len = sizeof(msgTypeBuf);
  iov[1].iov_base = msgDataBuf;
  iov[1].iov_len = sizeof(msgDataBuf);

  msg.msg_iov = iov;
  msg.msg_iovlen = 2;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_control = cmptr;
  msg.msg_controllen = CMSG_LEN(sizeof(int));
  int readlen = recvmsg(fd_, &msg, 0);
  printf("recvmsg len: %d\n", readlen);
  if (readlen == -1) {
    return kRecvMsgError;
  }
  if (readlen == 0) {
    return kRecvMsgClose;
  }

  uint32_t msgType = 0;
  memcpy(&msgType, msgTypeBuf, sizeof(msgType));
  if (msgType == MsgRequestType::kTransferListenFd) {
    HandleListenFdMsg(msg);
  } else if (msgType == MsgRequestType::kTransferSessionFd) {
    HandleSessionFdMsg(msg);
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
  printf("common reply sendlen: %d\n", sendlen);
  if (sendlen != sizeof(msgReplyBuf)) {
    return false;
  }
  return true;
}

bool UnixConn::HandleListenFdMsg(const msghdr& msg) {
  if (msg.msg_controllen != CMSG_LEN(sizeof(int))) {
    return false;
  }
  int listen_fd = *(int*)CMSG_DATA(msg.msg_control);
  std::string msgDataStr = std::string((char*)msg.msg_iov[1].iov_base);
  printf("handle listenfd msg: %s\n", msgDataStr.data());
  Epoll::getInstance()->attachInetListenFd(listen_fd);
  makeCommonReply();
  return true;
}

bool UnixConn::HandleSessionFdMsg(const msghdr& msg) {
  if (msg.msg_controllen != CMSG_LEN(sizeof(int))) {
    return false;
  }

  int session_fd = *(int*)CMSG_DATA(msg.msg_control);
  std::string msgDataStr = std::string((char*)msg.msg_iov[1].iov_base);
  printf("handle sessionfd msg: %s\n", msgDataStr.data());
  InetConn *conn = new InetConn(session_fd, "received conn tmp no ip_port");
  conn->SetNonblock();
  Epoll::getInstance()->inet_conn_map[session_fd] = conn;
  Epoll::getInstance()->AddEvent(session_fd, EPOLLIN | EPOLLET);
  makeCommonReply();
  return true;
}

SendMsgStatus UnixConn::processSend() {
}
