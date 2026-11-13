int f(union U { struct S { int x; } s; enum E { A, B } e; } u) { struct S t = u.s; enum E k = B; return t.x + (k == u.e) + A; }
