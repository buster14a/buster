_Complex double z; int f(void){ __atomic_fetch_fminimum_num(&z, 1.0, 5); return 0; }
