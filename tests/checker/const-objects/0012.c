int f(void){const int a[2]={2,5};const int* const p=a;enum{E=p[1]};_Static_assert(E==5,"array pointer");return E;}
