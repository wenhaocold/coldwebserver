#include "http.h"
#include "server.h"
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/socket.h>

int main(int argc, char *argv[]) {

  struct rlimit limit;

  // 获取当前的资源限制
  if (getrlimit(RLIMIT_NOFILE, &limit) == -1) {
    perror("getrlimit");
    exit(EXIT_FAILURE);
  }
  printf("file descriptor: %ld, %ld\n", limit.rlim_cur, limit.rlim_max);

  // 设置文件描述符数量的软限制和硬限制为 5000
  limit.rlim_cur = 5000;
  limit.rlim_max = 5000;

  // 设置新的资源限制
  if (setrlimit(RLIMIT_NOFILE, &limit) == -1) {
    perror("setrlimit");
    exit(EXIT_FAILURE);
  }

  Server *server = Server::get_instance();

  if (server == nullptr) {
    exit(1);
  }

  server->run();
  return 0;
}
