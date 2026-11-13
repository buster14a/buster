__float128 q; int f(void){ __atomic_fetch_fmaximum_num(&q, 1.0, 5); return 0; }
