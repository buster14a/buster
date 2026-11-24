struct S; enum { N=_Generic(1,int:1,struct S:2,void:3) }; _Static_assert(N==1, "int"); struct S { int x; };
