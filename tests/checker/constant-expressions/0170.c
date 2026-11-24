int x; enum { A=sizeof(__builtin_add_overflow(1,2,&x)) }; _Static_assert(A==1, "overflow type");
