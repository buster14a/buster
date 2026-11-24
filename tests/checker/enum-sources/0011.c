enum E { A=0x80000000, B, C=-1 }; _Static_assert(A==2147483648L && B==2147483649L && C==-1,"values"); _Static_assert(_Generic(A,long:1,default:0) && _Generic(C,long:1,default:0),"common type");
