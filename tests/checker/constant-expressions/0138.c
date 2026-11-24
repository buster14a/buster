typedef short T; enum { N=_Generic((T)0,T:_Generic(1.0,double:3,default:8),default:9) }; _Static_assert(N==3, "nested generic");
