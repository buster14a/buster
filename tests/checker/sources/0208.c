int f(void) { int a[({ 1; }) + sizeof(struct S { int x; })]; struct S s = { 0 }; return s.x + (int)sizeof a; }
