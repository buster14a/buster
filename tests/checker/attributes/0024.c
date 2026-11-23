struct S { char c; int i; } __attribute__((packed)); _Static_assert(sizeof(struct S)==5,"packed");
