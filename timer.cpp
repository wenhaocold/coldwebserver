#include "timer.h"
#include <cstdio>
#include <time.h>

void Timer::start() { setitimer(ITIMER_REAL, &m_time_val, nullptr); }

bool Timer::init() {

  m_time_val.it_value.tv_sec = 1;
  m_time_val.it_value.tv_usec = 0;
  m_time_val.it_interval.tv_sec = 1;
  m_time_val.it_interval.tv_usec = 0;
  return true;
}

Timer::Timer() { init(); }

void Timer::tick() {
  time_t cur_time = time(nullptr);
  http *tmp_http;

  while (!m_http_pq.empty()) {
    tmp_http = m_http_pq.top();
    if (tmp_http->m_old_extime > cur_time) {
      break;
    }

    m_http_pq.pop();
    if (tmp_http->m_new_extime > cur_time) {
      tmp_http->m_old_extime = tmp_http->m_new_extime;
      m_http_pq.push(tmp_http);
    } else {
      delete tmp_http;
    }
  }
}

void Timer::add_http_timer(http *http) { m_http_pq.push(http); }

void Timer::update_http_timer(http *http) {

  time_t cur_time = time(nullptr);

  http->m_new_extime = cur_time;
}

void Timer::close_all() {
  http *tmp_http;
  while (!m_http_pq.empty()) {
    tmp_http = m_http_pq.top();
    m_http_pq.pop();
    delete tmp_http;
  }
}
