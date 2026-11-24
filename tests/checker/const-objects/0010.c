int f(void){const int k=3;enum{E=*(&k)};_Static_assert(E==3,"direct address");return E;}
