typedef __float128 Q;static int calls;
static Q next(Q q){calls++;return q+1;}
static unsigned long bits(Q q){unsigned long w[2];__builtin_memcpy(w,&q,16);return w[0]^w[1]*3;}
static unsigned long pass(Q a,Q b){return bits(a)*5+bits(b);}
static Q imag_of(Q q){return __imag__ q;}
static Q imag_effect(Q q){return __imag__ next(q);}
int tested(int n){Q q=n-0.5;Q z=__imag__ q,e=__imag__ next(q);
unsigned long h=bits(imag_of(q))+bits(imag_effect(q))*7+pass(__imag__ q,__imag__ next(q))*11+bits(z)*13+bits(e)*17+(unsigned long)calls;
return (int)(h%1000003)+(z==0)*2+(e==0)*4+(__imag__ next(q)<1)*8;}
