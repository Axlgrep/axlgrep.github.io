#ifndef EPOLL_H_
#define EPOLL_H_

#include "sys/epoll.h"
#include "string"
#include "map"

#include "inet_conn.h"

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

  bool detachInetListenFd();
  bool attachInetListenFd(int il_fd);

  bool startInetServer(int port);
  bool startUnixServer(const std::string& pat);

  FiredEvent *firedevent() { return firedevent_; }

  int epfd_;
  int inet_listen_fd;
  int unix_listen_fd;
  int unix_conn_fd;
  int unix_client_fd;
  std::map<int, InetConn*> inet_conn_map;

  static Epoll* getInstance();

 private:
  static Epoll* s_instance;

  FiredEvent firedevent_[MAX_EVENTS];
  struct epoll_event events_[MAX_EVENTS];
};

#endif
