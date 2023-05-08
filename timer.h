#ifndef TIMER_H
#define TIMER_H

#include "http.h"
#include <queue>
#include <sys/time.h>
#include <vector>

class Timer {
  struct cmp {
    bool operator()(const http *p, const http *c) {
      return p->m_old_extime > c->m_old_extime;
    }
  };

public:
  void start();
  void tick();
  void add_http_timer(http *http);
	void update_http_timer(http *http);
	void close_all();
  Timer();

private:
  bool init();

private:
  struct itimerval m_time_val;
  std::priority_queue<http *, std::vector<http *>, cmp> m_http_pq;
};

#endif // !TIMER_H
