#ifndef HTTP_H
#define HTTP_H

#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <sys/uio.h>
#include <time.h>

class http {

  enum METHOD {
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT,
    PATH
  };
  enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
  };
  enum HTTP_CODE {
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION,
    REDIRECTION,

  };
  enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
  int m_old_extime;
  int m_new_extime;
  int m_creat_time;

private:
#define HTTP_DURATION_SECONDS 60
#define FILE_NAME_MAX 128
  static const int read_buf_capacity = 1024;
  static const int write_buf_capacity = 1024;

  int m_http_fd;
  int m_epfd;

  char m_read_buf[read_buf_capacity];
  char m_write_buf[write_buf_capacity];

  int m_write_len = 0;    // length of write buffer
  int m_read_len = 0;     // length of read buffer
  int m_has_sent_len = 0; // length of message has been sent
  int m_to_sent_len = 0;  // length of message wait to sent
  int m_proc_idx = 0;     // processed index
  int m_cur_line_start_pointer = 0;

  CHECK_STATE m_check_state;
  METHOD m_method;

  char *m_url;
  char *m_version;
  char m_flie_path[FILE_NAME_MAX];
  char *m_mapped_file;
  size_t m_mapped_file_len;
  struct iovec m_iov[2];

  char *m_host;
  size_t m_content_len;
  char *m_content_str;
  bool m_keep_alive;

public:
  http(int fd, int epfd) : m_http_fd(fd), m_epfd(epfd) {
    m_creat_time = time(nullptr);
    init();
  }
  ~http() { close_conn(); }

  // I/O process
  int read();
  int write();

  // bussiness logic process
  void process();
  void close_conn();

private:
  void init();

  HTTP_CODE parse_request();
  HTTP_CODE parse_request_line(char *text);
  HTTP_CODE parse_request_header(char *);
  HTTP_CODE parse_request_content(char *);
  HTTP_CODE process_request();
  bool create_response(HTTP_CODE);

  LINE_STATUS parse_line();

  void update_http_timer();

  void parse_name_passwd_from_content(std::string &name, std::string &passwd);

  char *get_cur_line();
  bool push_str_to_write_buf(const char *str, ...);
};

#endif // !HTTP_H
