struct __attribute__((capability("mutex"))) M { int x; }; struct M g; int v __attribute__((guarded_by(g)));
