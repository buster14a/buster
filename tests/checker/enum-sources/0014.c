enum E { A=2147483648LL, B=_Generic(A,long long:1,default:9) }; _Static_assert(B==1,"provisional"); _Static_assert(_Generic(A,unsigned:1,default:0),"completed");
