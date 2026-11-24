enum { A=8, N=sizeof(struct __attribute__((aligned(A))) S*) }; _Static_assert(N==8, "tag activation after attribute use");
