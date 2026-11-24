enum { N=sizeof(struct __attribute__((aligned(sizeof(struct S*)))) S*) }; _Static_assert(N==8, "nested tag identity");
