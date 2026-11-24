enum { A=0x80000000 }; int f(void){ { enum { A=7 }; _Static_assert(A==7,"inner"); } _Static_assert(A==2147483648U,"outer"); return 0;}
