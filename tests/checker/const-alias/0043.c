const int a[2]={3,4};
enum{E=a[((const void*)&a,1)]};
_Static_assert(E==4,"index path");
