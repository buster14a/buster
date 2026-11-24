int f1(void) { int u; (void)sizeof(({ u = 1; })); return u; }
int f2(void) { int u; (void)sizeof(({ u; })); u = 2; return u; }
int f3(int n) { int u; (void)(n ? sizeof(({ u; })) : 0); return 0; }
int f4(void) { int u; (void)({ u = 1; }); return u; }
int f5(void) { int u; return (int)sizeof ({ u; }); }
int f6(void) { int u; return (int)sizeof ({ int v = u; v; }) + (int)__alignof__(({ u; })); }
int f8(void) { int u; return _Generic(1, long: ({ u; }), default: 0); }
int f9(int n) { return sizeof ({ n; }) + sizeof({ (char)n; }) + sizeof ({ int k = n; k; }) * 2; }
int f10(int n) { int u; return (int)sizeof(({ ({ u; }); })) + (int)sizeof(({ (void)sizeof(({ u = 1; })); 1; })) + u; }
