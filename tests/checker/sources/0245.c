struct S { float f; } s; int f(void){ __atomic_fetch_fmaximum(&s, s, 5); return 0; }
