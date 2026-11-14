// expect: GCC-ONLY
int f() { int y = ({ int t = 1; t; }); return y; }
