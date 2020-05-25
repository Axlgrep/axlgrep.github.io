#include "unistd.h"

#include "fcntl.h"
#include "inet_conn.h"

#define HEADER_LEN sizeof(int)

InetConn::InetConn(const int fd, const std::string& ip_port): fd_(fd), ip_port_(ip_port) {

 last_read_pos_ = 0;
 last_write_pos_ = 0;
}


bool InetConn::SetNonblock() {
  if (fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFD, 0) | O_NONBLOCK) == -1) {
    return -1;
  }
  return 0;
}

bool InetConn::EmptyReadBuf() {
  return last_read_pos_ == 0;
}

std::string InetConn::IpPort() {
  return ip_port_;
}

ReadStatus InetConn::GetRequest() {
  ssize_t nread = 0;
  if (last_read_pos_ < HEADER_LEN) {
    nread = read(fd_, read_buf_ + last_read_pos_, HEADER_LEN - last_read_pos_);
    if (nread == -1) {
      if (errno == EAGAIN) {
        return kReadHalf;
      } else {
        return kReadError;
      }
    } else if (nread == 0) {
      return kReadClose;
    } else {
      last_read_pos_ += nread;
    }

    if (last_read_pos_ < HEADER_LEN) {
      return kReadHalf;
    }
  }

  int request_len;
  memcpy(&request_len, read_buf_, HEADER_LEN); 

  if (request_len + HEADER_LEN > READ_BUFFER_SIZE) {
    return kFullError;
  }

  nread = read(fd_, read_buf_ + last_read_pos_, (request_len + HEADER_LEN - last_read_pos_));
  if (nread == -1) {
    if (errno == EAGAIN) {
      return kReadHalf;
    } else {
      return kReadError;
    }
  } else if (nread == 0) {
    return kReadClose;
  } else {
    last_read_pos_ += nread;
  }

  if (last_read_pos_ < HEADER_LEN + request_len) {
    return kReadHalf;
  }
  reply = std::string(read_buf_ + HEADER_LEN, request_len);
  last_read_pos_ = 0;
  return kReadAll;
}

WriteStatus InetConn::SendReply() {
  ssize_t nwrite = write(fd_, reply.data() + last_write_pos_, reply.size() - last_write_pos_);
  if (nwrite == -1) {
    if (errno == EAGAIN) {
      return kWriteHalf;
    } else {
      return kWriteError;
    }
  }

  last_write_pos_ += nwrite;
  if (last_write_pos_ < reply.size()) {
    return kWriteHalf;
  }
  reply.clear();
  last_write_pos_ = 0;
  return kWriteAll;
}
