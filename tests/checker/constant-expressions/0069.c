struct S { int a[3]; }; enum { A=sizeof(((struct S*)0)->a) }; _Static_assert(A==12, "member");
