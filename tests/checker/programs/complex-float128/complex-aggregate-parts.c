typedef _Complex __float128 CQ;typedef __float128 Q;typedef __typeof__((_Complex char)0+(_BitInt(200))0) CB;typedef _BitInt(200) B;
static unsigned long qb(Q q){unsigned long w[2];__builtin_memcpy(w,&q,16);return w[0]^w[1]*3;}
static unsigned long bb(B b){return (unsigned long)b^(unsigned long)(b>>64)*5^(unsigned long)(b>>128)*7;}
static CQ mkq(int n){CQ z;__real__ z=n+0.5;__imag__ z=n-2.25;return z;}
static CB mkb(int n){CB z;__real__ z=(B)n*((B)1<<130);__imag__ z=-(B)n*3;return z;}
int tested(int n){CQ z=mkq(n);CB c=mkb(n);unsigned long h=0;
h+=qb(__real__ z)*3+qb(__imag__ z)*5+(__real__ z>1)*7+(__imag__ z<0)*11;Q s=__real__ z+__imag__ z;h+=qb(s)*13;
h+=qb(__real__ mkq(n+1))*17+qb(__imag__ mkq(n+2))*19;
h+=bb(__real__ c)*23+bb(__imag__ c)*29+(__real__ c>1)*31+(__imag__ c<0)*37;B t=__real__ c+__imag__ c;h+=bb(t)*41;
h+=bb(__real__ mkb(n+1))*43+bb(__imag__ mkb(n+2))*47;
return (int)(h%1000003);}
