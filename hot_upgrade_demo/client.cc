#include "sys/types.h"
#include "sys/socket.h"
#include "netinet/in.h"

#include "unistd.h"
#include "fcntl.h"
#include "error.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "iostream"

#include "inet_socket.h"

#define CLIENT_NUM  100
#define BUFFER_SIZE 1024

int client_echo_request(int client_fd) {
  char write_buf[BUFFER_SIZE];
  std::string message = "message";
  int request_len = message.size();
  memcpy(write_buf, &request_len, sizeof(int));
  memcpy(write_buf + sizeof(int), message.data(), message.size());

  int write_pos = 0, target_write_pos = message.size() + sizeof(int);
  while (write_pos <  target_write_pos) {
    ssize_t nwrite = write(client_fd, write_buf + write_pos, target_write_pos - write_pos);
    if (nwrite == -1) {
      if (errno == EAGAIN) {
        continue;
      } else {
        perror("write request error");
        exit(-1);
      }
    } else {
     write_pos += nwrite;
    }
  }

  char read_buf[BUFFER_SIZE];
  int read_pos = 0, target_read_pos = message.size();
  while (read_pos < target_read_pos) {
    ssize_t nread = read(client_fd, read_buf + read_pos, BUFFER_SIZE - read_pos);
    if (nread == -1) {
      if (errno == EAGAIN) {
        continue;
      } else {
        perror("read reply error");
        exit(-1);
      }
    } else {
      read_pos += nread;
    }
  }
  printf("client %d, receive %s, timestamp: %lld\n", client_fd, read_buf, time(NULL));
}

int main() {
  int client_fds[CLIENT_NUM], fd, port = 9221;
  std::string host = "127.0.0.1";

  for (int i = 0; i < CLIENT_NUM; i++) {
    fd = inet_client_conn(host.data(), port);
    if (fd < 0) {
      perror("open client fd error");
      exit(-1);
    } else {
      client_fds[i] = fd;
    }
  }

  while (true) {
    for (int i = 0; i < CLIENT_NUM; i++) {
      client_echo_request(client_fds[i]);
    }
  }
  return 0;
}
