#include "sys/un.h"
#include "sys/socket.h"
#include "sys/stat.h"
#include "netinet/in.h"

#include "stdio.h"
#include "stddef.h"
#include "unistd.h"
#include "stdlib.h"
#include "iostream"

#define QLEN 10
#define STALE 30
#define MAXLINE 1024

#define CLI_PATH "/var/tmp/"
#define CLI_PERM S_IRWXU

#define CONTROLLEN CMSG_LEN(sizeof(int))

static struct cmsghdr *cmptr = NULL; /* malloc'ed first time */

int unix_server_listen(const char *name) {
  int fd, len, err, rval;
  struct sockaddr_un un;

  /* create a UNIX domain stream socket */
  if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    perror("create unix socker error");
    rval = -1;
    goto errout;
  }
  unlink(name);      /* in case it already exists */

  /* fill in socker address structure */
  memset(&un, 0, sizeof(un));
  un.sun_family = AF_UNIX;
  strcpy(un.sun_path, name);
  len = offsetof(struct sockaddr_un, sun_path) + strlen(name);

  /* bind the name to the descriptor */
  if (bind(fd, (struct sockaddr*)&un, len) < 0) {
    perror("bind sockaddr error");
    rval = -2;
    goto errout;
  }

  if (listen(fd, QLEN) < 0) {
    perror("listen unix socket error");
    rval = -3;
    goto errout;
  }
  return fd;

errout:
  err = errno;
  close(fd);
  errno = err;
  return rval;
}

int unix_server_accept(int listenfd, uid_t *uidptr) {
  int                clifd, err, rval;
  time_t             staletime;
  struct sockaddr_un un;
  struct stat        statbuf;
  socklen_t len;

  len = sizeof(un);

  if ((clifd = accept(listenfd, (struct sockaddr*)&un, &len)) < 0) {
    perror("accept unix socket error");
    exit(-1);
  }

  /* obtain the client's uid from its calling address */
  len -= offsetof(struct sockaddr_un, sun_path); /* len of pathname */
  un.sun_path[len] = 0;


  if (stat(un.sun_path, &statbuf) < 0) {
    rval = -2;
    goto errout;
  }

  if ((statbuf.st_mode & (S_IRWXG | S_IRWXO)) || (statbuf.st_mode & S_IRWXU) != S_IRWXU) {
    rval = -4;
    goto errout;
  }

  staletime = time(NULL) - STALE;
  if (statbuf.st_atime < staletime || statbuf.st_ctime < staletime || statbuf.st_mtime < staletime) {
    rval = -5;
    goto errout;
  }

  if (uidptr != NULL) {
    *uidptr = statbuf.st_uid;
  }
  unlink(un.sun_path);
  return clifd;

errout:
  err = errno;
  close(clifd);
  errno = err;
  return rval;
}

int unix_client_conn(const char *name) {
  int                fd, err, rval;
  struct sockaddr_un un;
  socklen_t len;

  /* create a UNIX domain stream socket */
  if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    perror("create client unix socket error");
    exit(-1);
  }

  /* fill socket address structure with our address */
  memset(&un, 0, sizeof(un));
  un.sun_family = AF_UNIX;
  sprintf(un.sun_path, "%s%05d", CLI_PATH, getpid());
  len = offsetof(struct sockaddr_un, sun_path) + strlen(un.sun_path);
  unlink(un.sun_path);      /* in case it already exists */

  if (bind(fd, (struct sockaddr*)&un, len) < 0) {
    rval = -2;
    goto errout;
  }

  if (chmod(un.sun_path, CLI_PERM) < 0) {
    rval = -3;
    goto errout;
  }

  /* fill socket address structure with server's address */
  memset(&un, 0, sizeof(un));
  un.sun_family = AF_UNIX;
  strcpy(un.sun_path, name);
  len = offsetof(struct sockaddr_un, sun_path) + strlen(name);

  if (connect(fd, (struct sockaddr*)&un, len) < 0) {
    rval = -4;
    goto errout;
  }
  return fd;

errout:
  err = errno;
  close(fd);
  errno = err;
  return rval;
}

int send_fd(int fd, int fd_to_send) {
  struct iovec iov[1];
  struct msghdr msg;
  char buf[2];

  iov[0].iov_base = buf;
  iov[0].iov_len = 2;

  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;

  if (fd_to_send < 0) {
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    buf[1] = -fd_to_send;
    if (buf[1] == 0)
      buf[1] = 1;
  } else {
    if (cmptr == NULL && (cmptr = (struct cmsghdr*)malloc(CONTROLLEN)) == NULL) {
      perror("malloc cmptr error");
      exit(-1);
    }
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type  = SCM_RIGHTS;
    cmptr->cmsg_len = CONTROLLEN;
    msg.msg_control = cmptr;
    msg.msg_controllen = CONTROLLEN;
    *(int*)CMSG_DATA(cmptr) = fd_to_send;  /* the fd to pass */
    buf[1] = 0;  /* zero status means OK */
  }
  buf[0] = 0;
  if (sendmsg(fd, &msg, 0) != 2) {
    perror("sendmsg error");
    exit(-1);
  }
  return 0;
}

int recv_fd(int fd, ssize_t (*userfunc)(int, const void*, size_t)) {
  int newfd, nr, status;
  char *ptr;
  char buf[MAXLINE];
  struct iovec iov[1];
  struct msghdr msg;

  status = -1;

  iov[0].iov_base = buf;
  iov[0].iov_len = sizeof(buf);
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  if (cmptr == NULL && (cmptr = (struct cmsghdr*)malloc(CONTROLLEN)) == NULL) {
    perror("malloc cmptr error");
    exit(-1);
  }
  msg.msg_control = cmptr;
  msg.msg_controllen = CONTROLLEN;

  if ((nr = recvmsg(fd, &msg, 0)) < 0) {
    perror("recv msg error");
    exit(-1);
  } else if (nr == 0) {
    perror("connection closed by server");
    exit(-1);
  }

  for (ptr = buf; ptr < &buf[nr]; ) {
    if (*ptr++ == 0) {
      if (ptr != &buf[nr - 1]) {
        perror("message format error");
        exit(-1);
      }
      status = *ptr & 0xFF;
      if (status == 0) {
        if (msg.msg_controllen != CONTROLLEN) {
          perror("status = 0 but no fd");
          exit(-1);
        }
        newfd = *(int*)CMSG_DATA(cmptr);
      } else {
        newfd = -status;
      }
      nr -= 2;
    }
  }
  if (nr > 0 && (*userfunc)(STDERR_FILENO, buf, nr) != nr) {
    perror("nr > 0");
    exit(-1);
  }
  if (status >= 0) {
    return newfd;
  }
}


