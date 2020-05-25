#ifndef EPOLL_H_
#define EPOLL_H_

#include "sys/epoll.h"

#define MAX_EVENTS 10000

struct FiredEvent {
  int mask;
  int fd;
};

class Epoll {
 public:
  Epoll();
  ~Epoll();

  int Poll(const int timeout);
  int AddEvent(const int fd, const int mask);
  int ModEvent(const int fd, const int mask);
  int DelEvent(const int fd);

  FiredEvent *firedevent() { return firedevent_; }

 private:
  int epfd_;
  FiredEvent firedevent_[MAX_EVENTS];
  struct epoll_event events_[MAX_EVENTS];
};

#endif
