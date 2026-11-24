typedef int V __attribute__((vector_size(32)));typedef float W __attribute__((vector_size(64)));
static void* buf[5];static volatile long double lsink;static volatile __int128 wsink;static V vsink;static W wvsink;
static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}__attribute__((noinline)) static int side(int x){return x+5;}
__attribute__((noinline)) static long double lside(long double x){return x+0.25L;}__attribute__((noinline)) static __int128 wside(__int128 x){return x*3;}
__attribute__((noinline)) static V vside(V x){return x+1;}
__attribute__((noinline)) static long double lrun(int k){long double r=(long double)k*1.5L+__builtin_setjmp(buf);if(r==k*1.5L){lsink=7.25L+lside(9.5L)*(lside(2.0L)+side(1));jump();}return r;}
__attribute__((noinline)) static __int128 wrun(int k){__int128 r=((__int128)k<<70)+__builtin_setjmp(buf);if(r==((__int128)k<<70)){wsink=((__int128)99<<80)+wside((__int128)5<<66)*(wside(3)+side(1));jump();}return r;}
__attribute__((noinline)) static V vrun(int k){V b={k,k+1,k+2,k+3,k+4,k+5,k+6,k+7};V r=b*3+__builtin_setjmp(buf);if(r[0]==k*3){vsink=(V){9,9,9,9,9,9,9,9}+vside(b*7)*(vside(b)+side(2));jump();}return r;}
__attribute__((noinline)) static W wvrun(int k){W b={k,1,2,3,4,5,6,7,8,9,10,11,12,13,14,k};W r=b*2.5f+(float)__builtin_setjmp(buf);if(r[0]==k*2.5f){wvsink=(W){1}+b*(b+(float)side(3))*(b+(float)side(4));jump();}return r;}
__attribute__((noinline)) static long double mixed(int k){long double a=k;__int128 w=k;int r=(int)(a*2+(w*3+(side(k)+__builtin_setjmp(buf))));if(r==k*5+k+5){lsink=lside(1.0L)+(long double)(wside(7)+side(1));jump();}return r+a;}
int tested(int n){h=(unsigned)n;int k=(n&15)+1;mix((unsigned long long)(lrun(k)*4));mix((unsigned long long)(lsink*16));__int128 w=wrun(k);mix((unsigned long long)(w>>64));mix((unsigned long long)w);
mix((unsigned long long)(wsink>>64));V v=vrun(k);for(int i=0;i<8;i++)mix((unsigned)v[i]);mix((unsigned)vsink[3]);W q=wvrun(k);for(int i=0;i<16;i++)mix((unsigned long long)(q[i]*2));
mix((unsigned long long)wvsink[5]);mix((unsigned long long)(mixed(k)*2));return (int)(h^(h>>32));}
