#include "netinet/in.h"

#include "unistd.h"
#include "error.h"
#include "stdio.h"
#include "string.h"
#include "signal.h"
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

UnixConn *unix_conn = NULL;
UnixClient *unix_client = NULL;

void InitSignal() {
  signal(SIGPIPE, SIG_IGN);
}

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
    RecvMsgStatus rms = unix_conn->processRecv();
    if (rms != kRecvMsgOk) {
      return -1;
    }
  }
  return 0;
}

int handle_net_event(FiredEvent *fe) {
  if (fe->mask & EPOLLERR || fe->mask & EPOLLHUP) {
    return -1;
  }

  if (Epoll::getInstance()->inet_conn_map.find(fe->fd) == Epoll::getInstance()->inet_conn_map.end()) {
   return -1;
  }

  int ret = 0;
  InetConn *conn = Epoll::getInstance()->inet_conn_map[fe->fd];
  if (fe->mask & EPOLLIN && conn->EmptyReadBuf() && unix_client) {
    SendMsgStatus sms = unix_client->TransferSessionFd(fe->fd);
    if (sms == kSendMsgOk) {
      printf("send conn(%s) tcp connection success\n", conn->IpPort().data());
    } else {
      printf("send conn(%s) tcp connection failed\n", conn->IpPort().data());
    }
    return -1;
  }

  if (fe->mask & EPOLLIN) {
    ReadStatus rs = conn->GetRequest();
    if (rs == kReadAll) {
        Epoll::getInstance()->ModEvent(fe->fd, EPOLLOUT);
    } else if (rs != kReadHalf) {
      ret = -1;
    }
  } else if (fe->mask & EPOLLOUT) {
    WriteStatus ws = conn->SendReply();
    if (ws == kWriteAll) {
        Epoll::getInstance()->ModEvent(fe->fd, EPOLLIN);
    } else if (ws != kWriteHalf) {
      ret = -1;
    }
  }
  return ret;
}

int main(int argc, char** argv) {
  int port = 9221;
  bool ret;
  int unix_conn_fd = -1;
  int unix_client_fd = -1;
  bool unix_socket_init = false;

  InitSignal();

  if (argc < 2) {
    fprintf(stderr, "should input detect file, exit...\n");
    exit(-1);
  }
  std::string path = std::string(argv[1]);

  if (path != UNIX_SERVER_TRIGGER) {
    ret = Epoll::getInstance()->startInetServer(port);
    if (!ret) {
      perror("open inet listen fd error");
      exit(-1);
    }
  }

  for (;;) {

    if (FileExists(path) && !unix_socket_init) {
      if (path == UNIX_SERVER_TRIGGER) {
        ret = Epoll::getInstance()->startUnixServer(UNIX_SOCKET_LOCK_FILE);
        if (!ret) {
          perror("open unix listen fd error");
          exit(-1);
        }
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
        unix_client->TransferListenFd(Epoll::getInstance()->inet_listen_fd);
        Epoll::getInstance()->detachInetListenFd();
      }
    }

    int nfds = Epoll::getInstance()->Poll(10);

    if (nfds == -1) {
      perror("poll error");
      exit(-1);
    }

    for (int n = 0; n < nfds; n++) {
      FiredEvent *fe = (Epoll::getInstance()->firedevent()  + n);
      if (fe->fd == Epoll::getInstance()->inet_listen_fd) {
        std::string ip_port;
        int inet_conn_fd = inet_server_accept(Epoll::getInstance()->inet_listen_fd, &ip_port);
        if (inet_conn_fd < 0) {
          perror("inet server accept error");
          exit(-1);
        }
        InetConn *conn = new InetConn(inet_conn_fd, ip_port);
        conn->SetNonblock();
        Epoll::getInstance()->inet_conn_map[inet_conn_fd] = conn;
        Epoll::getInstance()->AddEvent(inet_conn_fd, EPOLLIN | EPOLLET);
        printf("accept inet conn(%s): %d\n", ip_port.data(), Epoll::getInstance()->inet_conn_map.size());
      } else if (fe->fd == Epoll::getInstance()->unix_listen_fd) {
        unix_conn_fd = unix_server_accept(Epoll::getInstance()->unix_listen_fd, NULL);
        if (unix_conn_fd < 0) {
          perror("unix server accept error");
          exit(-1);
        }
        unix_conn = new UnixConn(unix_conn_fd);
        Epoll::getInstance()->AddEvent(unix_conn_fd, EPOLLIN);
      } else if (fe->fd == unix_conn_fd) {
        int ret = handle_unix_event(fe);
        if (ret == -1) {
          Epoll::getInstance()->DelEvent(fe->fd);
          close(fe->fd);
          delete unix_conn;
          unix_conn = NULL;
        }
      } else {
        int ret = handle_net_event(fe);
        if (ret == -1) {
          Epoll::getInstance()->DelEvent(fe->fd);
          close(fe->fd);
          if (Epoll::getInstance()->inet_conn_map.find(fe->fd) != Epoll::getInstance()->inet_conn_map.end()) {
            InetConn *conn = Epoll::getInstance()->inet_conn_map[fe->fd];
            Epoll::getInstance()->inet_conn_map.erase(fe->fd);
            delete conn;
          }
        }
      }
    }
  }

  return 0;
}

