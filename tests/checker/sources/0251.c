volatile double x; double f(void){ return __atomic_fetch_fminimum(&x, 2.0f, 2); }
