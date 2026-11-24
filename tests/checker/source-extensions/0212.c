struct S; struct S { int x; }; struct S x; _Static_assert(sizeof(x)==4, "record");
