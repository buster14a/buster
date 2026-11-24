enum { N=sizeof(struct { enum { A=3 } e; int x[A]; })+A }; _Static_assert(N==19 && A==3,"nested ordinary declaration");
