enum E { A=0xffffffffffffffffULL, B=-1 }; _Static_assert(A==-1 && B==-1 && _Generic(A,long long:1,default:0),"common signed representation");
