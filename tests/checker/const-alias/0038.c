const int a[2]={3,4};
const void* const v=a;
const int* const q=v;
const int* const r=q+1;
enum{E=*r};
