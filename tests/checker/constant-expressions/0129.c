enum { N=_Generic(1,int:7,default:1/0) }; _Static_assert(N==7, "generic");
