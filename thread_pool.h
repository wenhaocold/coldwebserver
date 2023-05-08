#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "locker.h"
#include <pthread.h>
#include <queue>
#include <stdio.h>
#include <unistd.h>

template <typename T> class thread_pool {

private:
  static const int thread_count = 10;
  static thread_pool<T> *_instance;

  static void *springboard(void *);

private:
  thread_pool<T>(){};
  void run();

public:
  static thread_pool<T> *get_instance();

  bool init_pool();
  void enqueue_action(T *action);

private:
  // pthread_t *m_thread_ids;
  sem m_sem;
  locker m_locker;
  std::queue<T *> m_mutex_queue; // access this queue need a lock
};

template <typename T> thread_pool<T> *thread_pool<T>::_instance = nullptr;

template <typename T> thread_pool<T> *thread_pool<T>::get_instance() {

  if (_instance == nullptr) {
    _instance = new thread_pool<T>;

    if (_instance->init_pool() == false) {
      delete _instance;
      _instance = nullptr;
    }
  }

  return _instance;
}

template <typename T> bool thread_pool<T>::init_pool() {
  pthread_t tid;
  // m_thread_ids = new pthread_t[thread_count];
  for (int i = 0; i < thread_count; i++) {
    if (pthread_create(&tid, NULL, springboard, this) != 0) {
      // delete m_thread_ids;
      return false;
    }
    printf("create thread: %d\n", gettid());
    // m_thread_ids[i] = tid;
  }

  return true;
}

template <typename T> void *thread_pool<T>::springboard(void *args) {
  thread_pool<T> *pool = (thread_pool<T> *)args;

  pool->run();
  return nullptr;
}

template <typename T> void thread_pool<T>::enqueue_action(T *action) {

  m_locker.lock();
  m_mutex_queue.push(action);
  m_locker.unlock();
  m_sem.post();
}

// THINK: there is no state in the thread
// thread just provide a place for action to take place
template <typename T> void thread_pool<T>::run() {
  while (1) {
    m_sem.wait();
    m_locker.lock();
    T *action = m_mutex_queue.front();
    m_mutex_queue.pop();

    action->process();

    m_locker.unlock();
  }
}

#endif // !THREAD_POOL_H
