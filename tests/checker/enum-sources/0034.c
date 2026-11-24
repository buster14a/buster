enum E { A=1ULL<<63, B=3 }; _Static_assert(_Generic(B,unsigned long:1,default:0) && B==3,"whole set");
