enum __attribute__((packed)) E { A=0, B=255 }; _Static_assert(sizeof(enum E)==1 && _Alignof(enum E)==1 && _Generic(A,int:1,default:0),"packed");
