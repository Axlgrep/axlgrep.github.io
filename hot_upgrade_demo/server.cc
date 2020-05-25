#include "netinet/in.h"

#include "unistd.h"
#include "error.h"
#include "stdio.h"
#include "string.h"
#include "map"
#include "iostream"

#include "epoll.h"
#include "inet_conn.h"
#include "unix_conn.h"
#include "unix_client.h"
#include "inet_socket.h"
#include "unix_socket.h"

#define MAX_EVENTS 10000

#define UNIX_SERVER_TRIGGER   "./unix_server_trigger"
#define UNIX_CLIENT_TRIGGER   "./unix_client_trigger"
#define UNIX_SOCKET_LOCK_FILE "./unix_socket_lock_file"

Epoll *epoll = new Epoll();
UnixConn *unix_conn = NULL;
UnixClient *unix_client = NULL;
std::map<int, InetConn*> inet_conn_map;

bool FileExists(const std::string& path) {
  return access(path.c_str(), F_OK) == 0;
}

int handle_unix_event(FiredEvent *fe) {
  if (fe->mask & EPOLLERR || fe->mask & EPOLLHUP) {
    return -1;
  }

  if (!unix_conn) {
    return -1;
  }

  if (fe->mask & EPOLLIN) {
    RecvMsgStatus rms = unix_conn->RecvFD();
    if (rms == kRecvMsgOk) {
      int new_fd = unix_conn->NewFd();
      InetConn *conn = new InetConn(new_fd, "received conn tmp no ip_port");
      conn->SetNonblock();
      inet_conn_map[new_fd] = conn;
      epoll->AddEvent(new_fd, EPOLLIN | EPOLLET);
      printf("receive inet conn success\n");
    } else {
      printf("receive inet conn failed\n");
    }
  }
}

int handle_net_event(FiredEvent *fe) {
  if (fe->mask & EPOLLERR || fe->mask & EPOLLHUP) {
    return -1;
  }

  if (inet_conn_map.find(fe->fd) == inet_conn_map.end()) {
   return -1;
  }

  int ret = 0;
  InetConn *conn = inet_conn_map[fe->fd];
  if (fe->mask & EPOLLIN && conn->EmptyReadBuf() && unix_client) {
    SendMsgStatus sms = unix_client->SendFD(fe->fd);
    if (sms == kSendMsgOk) {
      printf("send conn(%s) tcp connection success\n", conn->IpPort().data());
      return -1;
    } else {
      printf("send conn(%s) tcp connection failed\n", conn->IpPort().data());
    }
  }

  if (fe->mask & EPOLLIN) {
    ReadStatus rs = conn->GetRequest();
    if (rs == kReadAll) {
      epoll->ModEvent(fe->fd, EPOLLOUT);
    } else if (rs != kReadHalf) {
      ret = -1;
    }
  } else if (fe->mask & EPOLLOUT) {
    WriteStatus ws = conn->SendReply();
    if (ws == kWriteAll) {
      epoll->ModEvent(fe->fd, EPOLLIN);
    } else if (ws != kWriteHalf) {
      ret = -1;
    }
  }
  return ret;
}

int main(int argc, char** argv) {
  int port = 9221;
  int inet_listen_fd = -1;
  int unix_listen_fd = -1;
  int unix_conn_fd = -1;
  int unix_client_fd = -1;
  bool unix_socket_init = false;

  if (argc < 2) {
    fprintf(stderr, "should input detect file, exit...\n");
    exit(-1);
  }
  std::string path = std::string(argv[1]);

  inet_listen_fd = inet_server_listen(port);
  if (inet_listen_fd == -1) {
    perror("open inet listen fd error");
    exit(-1);
  }
  epoll->AddEvent(inet_listen_fd, EPOLLIN);

  for (;;) {

    if (FileExists(path) && !unix_socket_init) {
      if (path == UNIX_SERVER_TRIGGER) {
        unix_listen_fd = unix_server_listen(UNIX_SOCKET_LOCK_FILE);
        if (unix_listen_fd < 0) {
          perror("open unix listen fd error");
          exit(-1);
        }
        epoll->AddEvent(unix_listen_fd, EPOLLIN);
        printf("open unix listenfd success\n");
        unix_socket_init = true;
      } else if (path == UNIX_CLIENT_TRIGGER) {
        unix_client_fd = unix_client_conn(UNIX_SOCKET_LOCK_FILE);
        if (unix_client_fd < 0) {
          perror("open client fd error");
          exit(-1);
        }
        unix_client = new UnixClient(unix_client_fd);
        printf("unix client connection success\n");
        unix_socket_init = true;
      }
    }

    int nfds = epoll->Poll(10);

    if (nfds == -1) {
      perror("poll error");
      exit(-1);
    }

    for (int n = 0; n < nfds; n++) {
      FiredEvent *fe = (epoll->firedevent()  + n);
      if (fe->fd == inet_listen_fd) {
        std::string ip_port;
        int inet_conn_fd = inet_server_accept(inet_listen_fd, &ip_port);
        if (inet_conn_fd < 0) {
          perror("inet server accept error");
          exit(-1);
        }
        InetConn *conn = new InetConn(inet_conn_fd, ip_port);
        conn->SetNonblock();
        inet_conn_map[inet_conn_fd] = conn;
        epoll->AddEvent(inet_conn_fd, EPOLLIN | EPOLLET);
        printf("accept inet conn(%s): %d\n", ip_port.data(), inet_conn_map.size());
      } else if (fe->fd == unix_listen_fd) {
        unix_conn_fd = unix_server_accept(unix_listen_fd, NULL);
        if (unix_listen_fd < 0) {
          perror("unix server accept error");
          exit(-1);
        }
        unix_conn = new UnixConn(unix_conn_fd);
        epoll->AddEvent(unix_conn_fd, EPOLLIN);
        printf("accept unix conn\n");
      } else if (fe->fd == unix_conn_fd) {
        int ret = handle_unix_event(fe);
        if (ret == -1) {
          epoll->DelEvent(fe->fd);
          close(fe->fd);
          delete unix_conn;
          unix_conn = NULL;
        }
      } else {
        int ret = handle_net_event(fe);
        if (ret == -1) {
          epoll->DelEvent(fe->fd);
          close(fe->fd);
          if (inet_conn_map.find(fe->fd) != inet_conn_map.end()) {
            InetConn *conn = inet_conn_map[fe->fd];
            inet_conn_map.erase(fe->fd);
            delete conn;
          }
        }
      }
    }
  }

  return 0;
}

