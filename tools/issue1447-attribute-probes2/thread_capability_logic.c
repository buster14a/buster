struct __attribute__((capability("mutex"))) M { int x; }; struct M a, b; int v __attribute__((guarded_by(a && b)));
