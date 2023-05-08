#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

bool addfdtoep(int epfd, int fd, int events, bool isET) {
  struct epoll_event e;

  if (isET) {
    events |= EPOLLET;
  }
  e.events = events;
  e.data.fd = fd;

  if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &e) == -1) {
    fprintf(stderr, "addfdto:epoll_ctl, fd = %d", fd);
    perror(" ");
    return false;
  }

  return true;
}

bool modfdevent(int epfd, int fd, int events, bool isET) {
  struct epoll_event e;

  if (isET) {
    events |= EPOLLET;
  }
  e.events = events;
  e.data.fd = fd;

  if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &e) == -1) {
    fprintf(stderr, "modfdevent:epoll_ctl, fd = %d", fd);
    perror(" ");
    return false;
  }

  return true;
}

bool set_nonblock(int fd) {

  int flags;
  flags = fcntl(fd, F_GETFL);
  if (flags == -1) {
    return false;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return false;
  }

  return true;
}
