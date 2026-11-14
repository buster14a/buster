const int a[2]={7,9};
int* const p=(int*)a;
int* const q=p+1;
enum{E=*q};
_Static_assert(E==9,"qualification");
