#include "epoll.h"
#include "unistd.h"

#include "inet_socket.h"
#include "unix_socket.h"

Epoll* Epoll::s_instance = NULL;

Epoll::Epoll()
    : inet_listen_fd(-1),
      unix_listen_fd(-1),
      unix_conn_fd(-1),
      unix_client_fd(-1) {
  epfd_ = epoll_create(1024);
}

Epoll::~Epoll() {
  close(epfd_);
}

int Epoll::Poll(const int timeout) {
 int retval, numevents = 0;
 retval = epoll_wait(epfd_, events_, MAX_EVENTS, timeout);
  if (retval > 0) {
    numevents = retval;
    for (int i = 0; i < numevents; i++) {
      int mask = 0;
      firedevent_[i].fd = (events_ + i)->data.fd;

      if ((events_ + i)->events & EPOLLIN) {
        mask |= EPOLLIN;
      }
      if ((events_ + i)->events & EPOLLOUT) {
        mask |= EPOLLOUT;
      }
      if ((events_ + i)->events & EPOLLERR) {
        mask |= EPOLLERR;
      }
      if ((events_ + i)->events & EPOLLHUP) {
        mask |= EPOLLHUP;
      }
      firedevent_[i].mask = mask;
    }
  }
  return numevents;
}

int Epoll::AddEvent(const int fd, const int mask) {
  struct epoll_event ee;
  ee.data.fd = fd;
  ee.events = mask;
  return epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ee);
}

int Epoll::ModEvent(const int fd, const int mask) {
  struct epoll_event ee;
  ee.data.fd = fd;
  ee.events = mask;
  return epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ee);
}

int Epoll::DelEvent(const int fd) {
  struct epoll_event ee;
  ee.data.fd = fd;
  return epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, &ee);
}

bool Epoll::detachInetListenFd() {
  DelEvent(inet_listen_fd);
  close(inet_listen_fd);
  inet_listen_fd = -1;
}

bool Epoll::attachInetListenFd(int il_fd) {
  inet_listen_fd = il_fd;
  AddEvent(inet_listen_fd, EPOLLIN);
  return true;
}

bool Epoll::startInetServer(int port) {
  inet_listen_fd = inet_server_listen(port);
  if (inet_listen_fd == -1) {
    return false;
  }
  AddEvent(inet_listen_fd, EPOLLIN);
  return true;
}

bool Epoll::startUnixServer(const std::string& path) {
  unix_listen_fd = unix_server_listen(path.data());
  if (unix_listen_fd < 0) {
    return false;
  }
  AddEvent(unix_listen_fd, EPOLLIN);
  return true;
}

Epoll* Epoll::getInstance() {
  if (!s_instance) {
    s_instance = new Epoll();
  }
  return s_instance;
}
