#ifndef TOOLS_H
#define TOOLS_H

bool addfdtoep(int epfd, int fd, int op, bool isET = true);
bool modfdevent(int epfd, int fd, int events, bool isET = true);
bool set_nonblock(int fd);

#endif // !TOOLS_H
