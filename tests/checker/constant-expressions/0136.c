struct S; union U{struct S*p;void*q;} __attribute__((transparent_union)); int f(void){return _Generic(0,int(*)(union U):1,int(*)(struct S*):2,default:0);}
