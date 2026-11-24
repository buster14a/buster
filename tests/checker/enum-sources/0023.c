enum E { A=-((__int128)1<<100), B=(A>>100)==-1, C=_Generic(A,__int128:7,default:9) }; _Static_assert(A==0 && B==1 && C==7 && _Generic(A,long long:1,default:0),"negative exact");
