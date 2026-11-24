enum E { A=(unsigned __int128)1<<100, B=A>>100, C=_Generic(A,unsigned __int128:7,default:9) }; _Static_assert(A==0 && B==1 && C==7 && _Generic(A,unsigned long long:1,default:0),"wide exact");
