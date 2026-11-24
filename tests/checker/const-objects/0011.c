int f(void){const int a[2]={2,5};enum{E=(&a[0])[1]};_Static_assert(E==5,"array address");return E;}
