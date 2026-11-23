int f(int) __attribute__((overloadable,unavailable));
_Static_assert(_Generic(0,int:1,default:f),"type");
