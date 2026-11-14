int *p; int f(void){ __atomic_fetch_fminimum(&p, 0, 5); return 0; }
