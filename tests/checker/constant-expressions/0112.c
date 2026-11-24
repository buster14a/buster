typedef int T; enum { A=sizeof(int (*)(int (*cb)(int T),T x)) }; _Static_assert(A==8,"typedef restored");
