union U { int n; char data[]; }; _Static_assert(sizeof(union U)==4 && _Alignof(union U)==4, "after");
union V { double data[]; char c; }; _Static_assert(sizeof(union V)==8 && _Alignof(union V)==8, "first");
union W { char data[]; }; _Static_assert(sizeof(union W)==0, "alone");
struct T { int k; union U u; }; _Static_assert(sizeof(struct T)==8, "nested");
