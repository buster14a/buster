const int k=3;
const int* const p=((const void*)&k,&k);
enum{E=*p};
_Static_assert(E==3,"pointer path");
