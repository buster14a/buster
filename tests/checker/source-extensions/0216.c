struct S { int x; }; struct S f(struct S x) { struct S y=x; y=x; return y; }
