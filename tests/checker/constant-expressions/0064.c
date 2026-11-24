int x; void f(void) { char x; enum { N=sizeof x }; _Static_assert(N==1, "inner"); } enum { N=sizeof x }; _Static_assert(N==4, "outer");
