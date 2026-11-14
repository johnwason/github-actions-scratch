// expect: GCC-ONLY
bool f(bool c) { return __builtin_expect(c, 1); }
