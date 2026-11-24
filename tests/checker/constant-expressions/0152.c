int a[2]; int f(void); _Static_assert(_Generic(a,int*:1,default:0), "array"); _Static_assert(_Generic(f,int(*)(void):1,default:0), "function");
