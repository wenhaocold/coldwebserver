#include "server.h"
#include "http.h"
#include "sqlconn_pool.h"
#include "tools.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

Server *Server::_instance = nullptr;
std::unordered_map<std::string, std::string> Server::user_cache;
int Server::spipe[2];

Server *Server::get_instance() {

  if (_instance == nullptr) {
    _instance = new Server();
    if (!_instance || _instance->init_server() == false) {
      fprintf(stderr, "create sever failed\n");
      delete _instance;
      _instance = nullptr;
      return nullptr;
    }

    if (!_instance || _instance->init_epoll() == false) {
      fprintf(stderr, "init epoll failed\n");
      delete _instance;
      _instance = nullptr;
      return nullptr;
    }

    if (!_instance || _instance->init_thread_pool() == false) {
      fprintf(stderr, "init thread pool failed\n");
      delete _instance;
      _instance = nullptr;
      return nullptr;
    }
    if (!_instance || _instance->init_signal() == false) {
      fprintf(stderr, "init signal failed\n");
      delete _instance;
      _instance = nullptr;
      return nullptr;
    }
    if (!_instance || _instance->init_sql_conn_pool() == false) {
      fprintf(stderr, "init sql connection failed\n");
      delete _instance;
      _instance = nullptr;
      return nullptr;
    }
  }

  return _instance;
}

void Server::sig_handler(int signum) {
  int saved_e = errno;

  char msg = signum;
  write(spipe[1], &signum, 1);
  errno = saved_e;
}

bool Server::init_server() {

  int fd;
  struct sockaddr_in address;
  int optval;
  // 1. create socket

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    perror("create socket failed");
    return false;
  }
  set_nonblock(fd);

  optval = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  // 2. bind address
  address.sin_family = AF_INET;
  address.sin_port = htons(10307);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
    perror("bind socket failed");
    return false;
  }

  // 3. listen
  if (listen(fd, 10) == -1) {
    perror("listen socket failed");
    return false;
  }
  m_server_fd = fd;

  return true;
}

bool Server::init_epoll() {
  int fd;
  struct epoll_event e;
  fd = epoll_create(5);

  if (fd == -1) {
    perror("epoll_create");
    return false;
  }

  e.events = EPOLLIN;
  e.data.fd = m_server_fd;
  if (epoll_ctl(fd, EPOLL_CTL_ADD, m_server_fd, &e) == -1) {
    perror("epoll_ctl");
    return false;
  }

  m_epfd = fd;
  return true;
}

bool Server::init_thread_pool() {

  auto *_thread_pool = thread_pool<http>::get_instance();
  if (_thread_pool == nullptr) {
    return false;
  }
  m_thread_pool = _thread_pool;
  return true;
}

// NOTE: init_signal should be placed after init_epoll;
bool Server::init_signal() {
  struct sigaction act;
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, spipe) == -1) {
    perror("socketpair");
    return false;
  }

  set_nonblock(spipe[0]);
  addfdtoep(m_epfd, spipe[0], EPOLLIN, true);

  act.sa_flags = SA_RESTART;
  act.sa_handler = sig_handler;
  sigaction(SIGINT, &act, nullptr);
  sigaction(SIGALRM, &act, nullptr);
  act.sa_flags = 0;
  sigaction(SIGQUIT, &act, nullptr);

  return true;
}

bool Server::init_sql_conn_pool() {
  sqlconn_pool *sql_pool = sqlconn_pool::get_instance();
  MYSQL *conn;

  conn = sql_pool->get_conn();
  if (!conn) {
    return false;
  }

  if (mysql_query(conn, "select username, passwd from user") != 0) {
    fprintf(stderr, "mysql_query\n");
    return false;
  }
  MYSQL_RES *result = mysql_store_result(conn);

  mysql_fetch_row(result); // skip head
  while (MYSQL_ROW row = mysql_fetch_row(result)) {
    std::string name = row[0];
    std::string passwd = row[1];
    user_cache[name] = passwd;
  }

  return true;
}

int Server::epoll_wait_wrapper(int, struct epoll_event *, int) {
  int ret;

  while (1) {
    ret = epoll_wait(m_epfd, m_events, event_count, -1);
    if (ret == -1) {
      // interrupted by signal handler
      if (errno == EINTR) {
        continue;
      } else {
        perror("epoll_wait");
        return -1;
      }
    }
    break;
  }

  return ret;
}

http *http_user[65536];
void Server::run() {
  int ready_fd;
  int wait_ret;
  struct epoll_event *pe;
  bool r;

  m_timer.start();
  while (1) {

    wait_ret = epoll_wait_wrapper(m_epfd, m_events, event_count);

    if (wait_ret == -1) {
      exit(1);
    }

    for (int i = 0; i < wait_ret; i++) {
      ready_fd = m_events[i].data.fd;
      // accept is ready
      if (ready_fd == m_server_fd && m_events[i].events & EPOLLIN) {
        if (handle_connection_event() == false) {
          fprintf(stderr, "register client fd to epoll failed\n");
          continue;
        }
      } else if (ready_fd == spipe[0] && m_events[i].events & EPOLLIN) {
        handle_signal_event(ready_fd);
      }
      // receive data from client
      else if (m_events[i].events & EPOLLIN) {
        handle_read_event(ready_fd);
      }
      // send data to client
      else if (m_events[i].events & EPOLLOUT) {
        handle_write_event(ready_fd);
      }
    }
  }
}

bool Server::handle_connection_event() {

  bool ret;
  int client_fd;
  struct sockaddr_in address;
  socklen_t add_len;

  // TODO: signal should set to SA_RESTART
  add_len = sizeof(address);
  while (1) {
    client_fd = accept(m_server_fd, (struct sockaddr *)&address, &add_len);

    if (client_fd == -1) {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        perror("accept");
        return false;
      }
    } else {
      ret = set_nonblock(client_fd);

      if (ret && (ret = addfdtoep(m_epfd, client_fd, EPOLLIN, true)))
        ;
      if (ret == false) {
        fprintf(stderr, "some thing wrong, close client");
        close(client_fd);
        continue;
      }
      http_user[client_fd] = new http(client_fd, m_epfd);
      m_timer.add_http_timer(http_user[client_fd]);
      // printf("fd: %d client connected: %s\n", client_fd,
      //        inet_ntoa(address.sin_addr));
    }
  }

  return true;
}

bool Server::handle_read_event(int ready_fd) {

  if (http_user[ready_fd]->read() > 0) {

    m_thread_pool->enqueue_action(http_user[ready_fd]);
  }
  return true;
}

bool Server::handle_write_event(int ready_fd) {

  if (http_user[ready_fd]->write() > 0) {
  }
  return true;
}

bool Server::handle_signal_event(int ready_fd) {

  int signum;
  char msg;
  int len;

  while (1) {
    len = read(ready_fd, &msg, 1);
    if (len == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else if (errno == EINTR) {
        continue;
      } else {
        return false;
      }
    }
    signum = msg;
    switch (signum) {
    case SIGINT:
      break;

    case SIGALRM:
      m_timer.tick();
      break;
    case SIGQUIT:
      close(m_server_fd);
      close(m_epfd);
      m_timer.close_all();

      exit(0);

      break;
    default:

      break;
    }
  }

  return true;
}
