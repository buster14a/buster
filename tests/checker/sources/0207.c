int f(void) { int a[sizeof(struct S { int x; }) + ({ 1; })]; struct S s = { 0 }; return s.x + (int)sizeof a; }
