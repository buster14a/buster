enum { A=sizeof(int (*)(int,int,int,int)) }; _Static_assert(A==8,"unnamed parameters");
