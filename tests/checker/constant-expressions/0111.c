enum { A=sizeof(int (*)(int x,int y))+sizeof(int (*)(int x,int y)) }; _Static_assert(A==16,"separate prototypes");
