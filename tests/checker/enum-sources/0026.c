enum __attribute__((aligned(16))) E { A=0x80000000 }; _Static_assert(sizeof(enum E)==4 && _Alignof(enum E)==16 && _Generic(A,unsigned:1,default:0),"aligned");
