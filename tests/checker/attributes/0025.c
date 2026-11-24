struct S { char c; int i __attribute__((packed,aligned(2))); }; _Static_assert(sizeof(struct S)==6,"member");
