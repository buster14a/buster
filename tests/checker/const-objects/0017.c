int f(void){const int a[2]={2,5};enum{E=a[1]};_Static_assert(E==5,"local array");return E;}
