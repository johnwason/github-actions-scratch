// expect: GCC-ONLY
int f(int c) { switch (c) { case 1 ... 5: return 1; default: return 0; } }
