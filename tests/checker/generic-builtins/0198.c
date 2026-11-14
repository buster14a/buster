union U { int n; int *p __attribute__((counted_by(n))); };
