struct S; int f(void) { return _Generic(1,int:1,struct S:2,int[]:3); } struct S { int x; };
