struct S { enum E { A=0x80000000, B } x; enum F { C=-1, D } y; }; _Static_assert(B==2147483649U && D==0 && sizeof(struct S)==8,"nested chains");
