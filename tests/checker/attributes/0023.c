struct __attribute__((packed)) S { char c; int i; }; _Static_assert(sizeof(struct S)==5 && _Alignof(struct S)==1,"packed");
