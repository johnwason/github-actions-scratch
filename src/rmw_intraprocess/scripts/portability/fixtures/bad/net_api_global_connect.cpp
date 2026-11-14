// expect: NET-API
int f(int fd) { return ::connect(fd, nullptr, 0); }
