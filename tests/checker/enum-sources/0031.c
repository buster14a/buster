enum E { A __attribute__((aligned(8)))=0x80000000 }; _Static_assert(__alignof__(A)==8 && _Generic(A,unsigned:1,default:0),"alignment");
