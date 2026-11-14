const volatile float x; int f(void){ return __atomic_fetch_fminimum(&x, 1, 5); }
