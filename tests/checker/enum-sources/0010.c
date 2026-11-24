enum E { A=0x80000000, B }; _Static_assert(A==2147483648U && B==2147483649U,"values"); _Static_assert(_Generic(A,unsigned:1,default:0),"common type"); unsigned f(void){return B;}
