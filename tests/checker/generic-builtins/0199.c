struct S { int n; char fam[] __attribute__((sized_by(n))); };
