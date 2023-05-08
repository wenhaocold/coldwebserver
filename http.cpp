#include "http.h"
#include "server.h"
#include "tools.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

// just read from actual a I/O
int http::read() {
  int sum_len = 0;
  int len;

  while (1) {
    len = ::read(m_http_fd, m_read_buf + m_read_len, read_buf_capacity);
    if (len > 0) {
      sum_len += len;
      m_read_len += len;
    } else if (len == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        // TODO: something wrong when read from socket
        return -1;
      }
    } else {
      // TODO: connection is disconnected
      return 0;
    }
  }

  return sum_len;
}

// just write to a actual I/O
int http::write() {
  int len;
  int sum_len = 0;
  while (1) {
    if (m_to_sent_len == 0) {
      if (m_keep_alive) {
        init();
        modfdevent(m_epfd, m_http_fd, EPOLLIN);
      } else {
        // delay the close action to alarm signal handler
        close_conn();
      }
      break;
    }
    len = ::writev(m_http_fd, m_iov, 2);
    if (len == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        return -1;
      }
    }
    m_to_sent_len -= len;
    m_has_sent_len += len;

    if (m_iov[0].iov_len > len) {
      m_iov[0].iov_len -= len;
      m_iov[0].iov_base = static_cast<char *>(m_iov[0].iov_base) + len;
    } else {
      int tmp_len = len - m_iov[0].iov_len;
      m_iov[1].iov_len -= tmp_len;
      m_iov[1].iov_base = static_cast<char *>(m_iov[1].iov_base) + tmp_len;
      m_iov[0].iov_len = 0;
    }
    sum_len += len;
  }
  return sum_len;
}

void http::process() {
  update_http_timer();

  HTTP_CODE read_ret = parse_request();
  if (read_ret == NO_REQUEST) {
    modfdevent(m_epfd, m_http_fd, EPOLLIN);
    return;
  }

  if (create_response(read_ret) == false) {
    close_conn();
    return;
  }
  modfdevent(m_epfd, m_http_fd, EPOLLOUT);
}

void http::close_conn() {
  // time_t cur_time = time(nullptr);
  // printf("http closed because the time expired, duration: %ld seconds\n",
  //        cur_time - m_creat_time);
  close(m_http_fd);
  munmap(m_mapped_file, m_mapped_file_len);
}

// ============ private ==============

void http::init() {

  m_old_extime = time(nullptr) + HTTP_DURATION_SECONDS;
  m_new_extime = time(nullptr) + HTTP_DURATION_SECONDS;
  m_write_len = 0;    // length of write buffer
  m_read_len = 0;     // length of read buffer
  m_has_sent_len = 0; // length of message has been sent
  m_to_sent_len = 0;  // length of message wait to sent
  m_proc_idx = 0;     // processed index
  m_cur_line_start_pointer = 0;

  m_check_state = CHECK_STATE_REQUESTLINE;

  m_method = GET;
  m_url = nullptr;
  m_version = nullptr;
  // m_flie_path = nullptr;
  m_mapped_file = nullptr;
  m_mapped_file_len = 0;
  m_iov[0].iov_len = 0;
  m_iov[0].iov_base = nullptr;
  m_iov[1].iov_len = 0;
  m_iov[1].iov_base = nullptr;

  m_host = nullptr;
  m_content_len = 0;
  m_content_str = nullptr;
  m_keep_alive = false;
}

http::HTTP_CODE http::parse_request() {

  HTTP_CODE ret;
  char *text;
  while ((m_check_state == CHECK_STATE_CONTENT) || (parse_line() == LINE_OK)) {

    text = get_cur_line();
    m_cur_line_start_pointer = m_proc_idx;
    switch (m_check_state) {
    case CHECK_STATE_REQUESTLINE:
      ret = parse_request_line(text);
      if (ret == BAD_REQUEST) {
        return ret;
      }
      break;
    case CHECK_STATE_HEADER:
      ret = parse_request_header(text);
      if (ret == GET_REQUEST) {
        return process_request();
      }
      break;
    case CHECK_STATE_CONTENT:
      ret = parse_request_content(text);
      if (ret == GET_REQUEST) {
        return process_request();
      }
      return process_request();
      break;
    default:
      break;
    }
  }

  return NO_REQUEST;
}

http::HTTP_CODE http::parse_request_line(char *text) {

  if (strncasecmp(text, "GET", 3) == 0) {
    m_method = GET;
  } else if (strncasecmp(text, "POST", 4) == 0) {
    m_method = POST;
  }

  m_url = strpbrk(text, " \t");
  if (!m_url) {
    return BAD_REQUEST;
  }
  *m_url++ = '\0';

  m_url += strspn(m_url, " \t");

  m_version = strpbrk(m_url, " \t");
  if (!m_version) {
    return BAD_REQUEST;
  }
  *m_version++ = '\0';
  m_version += strspn(m_version, " \t");

  m_check_state = CHECK_STATE_HEADER;

  return NO_REQUEST;
}

#define HEADER_TITLE_HOST "Host:"
#define HEADER_TITLE_Content_Len "Content-Length:"
#define HEADER_TITLE_Connection "Connection:"
http::HTTP_CODE http::parse_request_header(char *text) {

  char *tmp_cp;
  if (text == nullptr) {
    return BAD_REQUEST;
  }

  // request header is over
  if (text[0] == '\0') {
    // the request header is over
    if (m_content_len != 0) {
      m_check_state = CHECK_STATE_CONTENT;
      return NO_REQUEST;
    } else {
      return GET_REQUEST;
    }
  }

  if (strncasecmp(text, HEADER_TITLE_HOST, strlen(HEADER_TITLE_HOST)) == 0) {
    tmp_cp = text + strlen(HEADER_TITLE_HOST);
    tmp_cp += strspn(tmp_cp, " \t");
    m_host = tmp_cp;
  } else if (strncasecmp(text, HEADER_TITLE_Content_Len,
                         strlen(HEADER_TITLE_Content_Len)) == 0) {
    tmp_cp = text + strlen(HEADER_TITLE_Content_Len);
    tmp_cp += strspn(tmp_cp, " \t");
    m_content_len = atoi(tmp_cp);
  } else if (strncasecmp(text, HEADER_TITLE_Connection,
                         strlen(HEADER_TITLE_Connection)) == 0) {
    tmp_cp = text + strlen(HEADER_TITLE_Connection);
    tmp_cp += strspn(tmp_cp, " \t");
    m_keep_alive = strncasecmp(tmp_cp, "keep-alive", strlen("keep-alive")) == 0;
  }
  return NO_REQUEST;
}

http::HTTP_CODE http::parse_request_content(char *text) {

  text[m_content_len] = 0;
  m_content_str = text;
  return GET_REQUEST;
}

http::HTTP_CODE http::process_request() {

  if (m_content_len == 0) {

    // return welcome html
    if (strcasecmp(m_url, "/") == 0) {
      strcpy(m_flie_path, "./res/index.html");
      return FILE_REQUEST;
    }
    // return register html
    else if (strcasecmp(m_url, "/0") == 0) {
      strcpy(m_flie_path, "./res/register.html");
      return FILE_REQUEST;
    }
    // return login html
    else if (strcasecmp(m_url, "/1") == 0) {
      strcpy(m_flie_path, "./res/login.html");
      return FILE_REQUEST;
    }
    //
    else if (strcasecmp(m_url, "/tinyweb") == 0) {
      return REDIRECTION;
    }
  } else {
    // judge username and passwd
    if (strcasecmp(m_url, "/2CGISQL.cgi") == 0) {
      std::string name;
      std::string passwd;
      parse_name_passwd_from_content(name, passwd);
      if (Server::user_cache.find(name) != Server::user_cache.end()) {
        if (Server::user_cache[name] == passwd) {
          strcpy(m_flie_path, "./res/welcome.html");
          return FILE_REQUEST;
        }
      }
      strcpy(m_flie_path, "./res/login_error.html");
      return FILE_REQUEST;
    } else if (strcasecmp(m_url, "/3CGISQL.cgi") == 0) {
      std::string name;
      std::string passwd;
      parse_name_passwd_from_content(name, passwd);

      if (Server::user_cache.find(name) != Server::user_cache.end()) {
        strcpy(m_flie_path, "./res/register_errno.html");
        return FILE_REQUEST;
      } else {
        Server::user_cache[name] = passwd;
        strcpy(m_flie_path, "./res/login.html");
        return FILE_REQUEST;
      }
    }
  }

  return BAD_REQUEST;
}

bool http::create_response(HTTP_CODE state) {

  if (state == FILE_REQUEST) {
    int fd;
    struct stat statbuf;

    fd = open(m_flie_path, O_RDONLY);
    fstat(fd, &statbuf);

    push_str_to_write_buf("HTTP/1.1 200 OK\r\n");
    push_str_to_write_buf("Content-Length: %d\r\n", statbuf.st_size);
    push_str_to_write_buf("\r\n");

    m_iov[0].iov_base = m_write_buf;
    m_iov[0].iov_len = m_write_len;

    m_mapped_file_len = statbuf.st_size;
    m_mapped_file = static_cast<char *>(
        mmap(0, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    m_iov[1].iov_base = m_mapped_file;
    m_iov[1].iov_len = statbuf.st_size;

    m_to_sent_len = m_write_len + statbuf.st_size;
    return true;
  } else if (state == REDIRECTION) {
    push_str_to_write_buf("HTTP/1.1 302 Found\r\n");
    push_str_to_write_buf("Location: http://192.168.31.90:9006\r\n");
    push_str_to_write_buf("\r\n");

    m_iov[0].iov_base = m_write_buf;
    m_iov[0].iov_len = m_write_len;
    m_iov[1].iov_base = 0;
    m_iov[1].iov_len = 0;
    m_to_sent_len = m_write_len;
    return true;

  } else if (state == BAD_REQUEST) {
    return false;
  }
  return false;
}

http::LINE_STATUS http::parse_line() {
  for (int i = m_proc_idx; i < m_read_len; i++) {
    if (m_read_buf[i] == '\r') {
      if (i + 1 < m_read_len && m_read_buf[i + 1] == '\n') {
        m_read_buf[i] = '\0';
        m_read_buf[i + 1] = '\0';
        m_proc_idx = i + 2;
        return LINE_OK;
      }
    } else if (m_read_buf[i] == '\n') {
      if (i > 0 && m_read_buf[i - 1] == '\r') {
        m_read_buf[i] = '\0';
        m_read_buf[i - 1] = '\0';
        m_proc_idx = i + 1;
        return LINE_OK;
      }
    } else {
    }
  }

  m_proc_idx = m_read_len;
  return LINE_OPEN;
}

void http::update_http_timer() {
  m_new_extime = time(nullptr) + HTTP_DURATION_SECONDS;
}

void http::parse_name_passwd_from_content(std::string &name,
                                          std::string &passwd) {
  char *p1 = strchr(m_content_str, '=');
  char *p2;
  char *q = strchr(m_content_str, '&');
  p1++;
  p2 = p1;
  p2 = strchr(p2, '=');
  p2++;
  *q = 0;
  name = p1;
  passwd = p2;
}

inline char *http::get_cur_line() {
  return m_read_buf + m_cur_line_start_pointer;
}
bool http::push_str_to_write_buf(const char *str, ...) {
  int len;
  va_list ap;
  va_start(ap, str);
  len = vsnprintf(m_write_buf + m_write_len, write_buf_capacity - m_write_len,
                  str, ap);
  va_end(ap);

  if (len < 0) {
    // error
    return false;
  }
  m_write_len += len;
  return true;
}
