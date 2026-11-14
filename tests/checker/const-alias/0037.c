const int a[2]={0,1};
const int* const p=(const int*)(const void*)a;
const int* const q=p+1;
enum{E=*q};
