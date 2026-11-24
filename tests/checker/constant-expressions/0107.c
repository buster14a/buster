enum { A=sizeof(int (*)(int x,int (*cb)(int x,int y),int y)) }; _Static_assert(A==8,"nested scopes");
