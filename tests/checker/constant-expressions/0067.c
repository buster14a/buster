struct S { int a[3]; }; struct S s; int f(int); enum { A=sizeof s.a[1], B=sizeof f(3), C=sizeof(1/0) }; _Static_assert(A+B+C==12, "unevaluated");
