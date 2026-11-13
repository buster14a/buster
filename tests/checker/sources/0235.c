__bf16 b; int f(void){ __atomic_fetch_fminimum(&b, 1.0f, 5); return 0; }
