int f1(void) { int u; return _Generic(1, int: ({ u; }), default: 0); }
int f2(void) { int u; return __builtin_choose_expr(1, ({ u; }), 0); }
int f3(void) { return _Generic(1, int: ({ char c = 302; c; }), default: 0); }
int f4(void) { int u; return _Generic(1, int: _Generic(2, long: 0, default: ({ u; })), default: 0); }
int f5(void) { int u; return __builtin_choose_expr(0, _Generic(1, int: ({ u; })), 0); }
int f6(int x) { return _Generic(1, int: ({ x == 1; 0; }), default: 0); }
int f7(void) { int u; return _Generic(1, long: ({ u; }), int: ({ int v = 1; v; })); }
