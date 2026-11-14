extern const int a[];
const int a[2]={3,4};
enum{E=a[1]};
_Static_assert(E==4,"completed array");
