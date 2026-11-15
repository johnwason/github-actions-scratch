// expect: NET-HEADER, NET-API
#include <string>
#include "netdb.h"
int g() { int s = socket(2, 1, 0); return s; }
