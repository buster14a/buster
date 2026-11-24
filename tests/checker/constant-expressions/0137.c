struct S; union U{struct S*p;void*q;}; enum{N=_Generic(0,int(*)(union U):1,int(*)(struct S*):2,default:7)}; typedef union U TU __attribute__((transparent_union)); _Static_assert(N==7,"source time");
