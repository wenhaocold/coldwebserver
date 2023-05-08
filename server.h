#ifndef SERVER_H
#define SERVER_H

#include "http.h"
#include "thread_pool.h"
#include "timer.h"
#include <string>
#include <sys/epoll.h>
#include <unordered_map>

class Server {

public:
  static Server *get_instance();
  void run();

private:
  Server(){};
  bool init_server();
  bool init_epoll();
  bool init_thread_pool();
  bool init_signal();
  bool init_sql_conn_pool();
  bool handle_connection_event();
  bool handle_read_event(int);
  bool handle_write_event(int);
  bool handle_signal_event(int);
  int epoll_wait_wrapper(int, struct epoll_event *, int);
  static void sig_handler(int signum);

private:
  static Server *_instance;
  int m_server_fd;
  int m_epfd;
  static const int event_count = 10;
  struct epoll_event m_events[event_count];
  thread_pool<http> *m_thread_pool;
  static int spipe[2];
  Timer m_timer;
public:
  static std::unordered_map<std::string, std::string> user_cache;
};

#endif // !SERVER_H
