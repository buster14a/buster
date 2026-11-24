enum E { A=0x80000000 }; enum F { B=_Generic(A,unsigned:7,default:9) }; _Static_assert(B==7 && _Generic(B,int:1,default:0),"source order");
