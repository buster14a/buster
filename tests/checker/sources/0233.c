_Complex float z; int f(void){ __atomic_fetch_fmaximum(&z, 1.0f, 5); return 0; }
