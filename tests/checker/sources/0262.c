enum E { A } e; int f(void){ return __atomic_fetch_fmaximum(&e, 1, 5); }
