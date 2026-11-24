int f(void){const int k=3;const int* const p=&k;enum{E=*p};_Static_assert(E==3,"local pointer");return E;}
