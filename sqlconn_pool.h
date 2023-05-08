#ifndef SQLCONN_POOL_H
#define SQLCONN_POOL_H

#include "locker.h"
#include <mysql/mysql.h>
#include <queue>
#include <stdio.h>

class sqlconn_pool {

  static const size_t pool_size = 8;

public:
  MYSQL *get_conn();
  void return_conn(MYSQL *conn);
  static sqlconn_pool *get_instance();
  ;

private:
private:
  sqlconn_pool(){};
  bool init();

private:
  sem m_sem;
  locker m_lock;
  std::queue<MYSQL *> m_conn_q;
  static sqlconn_pool *_instance;
};

#endif // !SQLCONN_POOL_H
