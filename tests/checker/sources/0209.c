int f(int m) { int a[({ m; }) + sizeof(struct S { int x; }) + ({ m + 1; })]; struct S s = { 0 }; return s.x + (int)sizeof a; }
