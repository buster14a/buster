struct S{union{struct{int a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r;};};int z;unsigned:0;};_Static_assert(sizeof(struct S)==76,"layout");_Static_assert(__builtin_offsetof(struct S,r)==68,"member");
