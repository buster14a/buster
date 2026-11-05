__attribute__((selectany)) int x=1;
__attribute__((selectany)) int z;
__attribute__((selectany)) _Thread_local int t=2;
__attribute__((selectany)) _Thread_local int tz;
__attribute__((selectany)) const int c=3;
__attribute__((selectany,section("chosen"))) int e=4;
__attribute__((selectany)) int *p=&x;
