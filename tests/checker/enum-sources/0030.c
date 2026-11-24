enum E { A __attribute__((packed,noreturn,vector_size(16)))=0x80000000 }; _Static_assert(_Generic(A,unsigned:1,default:0),"ignored attributes");
