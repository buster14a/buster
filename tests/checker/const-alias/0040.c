const int a[2]={7,9};
const int* const p=(const int*)a;
const int* const q=p+1;
enum{E=*q};
_Static_assert(E==9,"same type");
