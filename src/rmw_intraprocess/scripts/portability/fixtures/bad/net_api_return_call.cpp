// expect: NET-API
int f(int fd) { if (fd) { return bind(fd, nullptr, 0); } return 0; }
