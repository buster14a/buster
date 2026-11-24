enum { A=sizeof(int (*)(int (*a)(int x,int y),int (*b)(int x,int y))) }; _Static_assert(A==8,"sibling scopes");
