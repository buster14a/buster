enum E { A __attribute__((deprecated))=0x80000000, B=A+1 }; _Static_assert(B==2147483649U,"attribute");
