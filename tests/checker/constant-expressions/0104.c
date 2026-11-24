enum { A=sizeof(int (*)(register int x,int y)) }; _Static_assert(A==8, "callback");
