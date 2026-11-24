const int x=1; _Atomic(int) a; _Static_assert(_Generic(x,int:1,default:0), "const"); _Static_assert(_Generic(a,int:1,default:0), "atomic");
