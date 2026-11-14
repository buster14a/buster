extern const int a[];
const int* const p=a;
const int a[2]={3,4};
enum{E=p[1]};
_Static_assert(E==4,"completed target");
