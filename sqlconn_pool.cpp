#include "sqlconn_pool.h"

sqlconn_pool *sqlconn_pool::_instance = nullptr;

sqlconn_pool *sqlconn_pool::get_instance() {

  if (_instance == nullptr) {
    _instance = new sqlconn_pool;
    if (!_instance) {
      return nullptr;
    }
    if (!_instance || _instance->init() == false) {
      delete _instance;
      _instance = nullptr;
      return nullptr;
    }
  }

  return _instance;
}

MYSQL *sqlconn_pool::get_conn() {
  MYSQL *ret;
  m_sem.wait();
  m_lock.lock();

  ret = m_conn_q.front();
  m_conn_q.pop();

  m_lock.unlock();
  return ret;
}

void sqlconn_pool::return_conn(MYSQL *conn) {

  if (conn == nullptr) {
    return;
  }

  m_lock.lock();

  m_conn_q.push(conn);

  m_lock.unlock();
  m_sem.post();
}

bool sqlconn_pool::init() {
  m_sem = sem(pool_size);

  MYSQL *con = nullptr;

  for (int i = 0; i < pool_size; i++) {

    con = mysql_init(con);
    if (con == nullptr) {
      fprintf(stderr, "mysql_init\n");
    }
    con = mysql_real_connect(con, "localhost", "whcold", "0307", "tiny_web_sql",
                             3306, nullptr, 0);
    m_conn_q.push(con);
  }

  return true;
}
