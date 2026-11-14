// Mentions socket(), connect(), <unistd.h> and __attribute__ in comments only.
/* getaddrinfo("x") in a block comment
   #include <pthread.h> */
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

struct Signal { void connect(int) {} };

int f(int a, Signal & s, Signal * p)
{
  auto g = std::bind([](int x) { return x; }, a);
  s.connect(1);
  p->connect(2);
  std::vector<int> v({1, 2});
  const char * str = "socket(AF_INET) ?: __builtin_trap() unistd.h";
  const char * raw = R"x(connect( ::bind( )x";
  (void)str; (void)raw;
  long n = 1'000'000;
  return a ? g() : static_cast<int>(n + v.size());
}
