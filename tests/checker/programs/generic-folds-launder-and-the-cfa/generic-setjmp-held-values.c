static void* buf[5];static volatile int sink;static volatile double dsink;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
__attribute__((noinline)) static int side(int x){return x+5;}__attribute__((noinline)) static double dside(double x){return x+0.5;}
__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}
__attribute__((noinline)) static int run(int n){int result=n+100+__builtin_setjmp(buf);if(result==n+100){sink=12345+side(8);jump();}return result;}
__attribute__((noinline)) static double drun(int k){double result=(double)k*1.5+__builtin_setjmp(buf);if(result==k*1.5){dsink=7.25+dside(1.0)+side(1);jump();}return result;}
__attribute__((noinline)) static int nested(int a){int r=a+(a*3+(side(a)+__builtin_setjmp(buf)));if(r==a+a*3+side(a)){sink=1+(2+(3+side(4)));jump();}return r;}
__attribute__((noinline)) static int control(void){if(__builtin_setjmp(buf)==0){sink=99+side(1);jump();}return sink;}
int tested(int n){h=(unsigned)n;mix((unsigned)run(n));mix((unsigned)sink);mix((unsigned long long)(drun(n&7)*4));mix((unsigned long long)(dsink*4));
mix((unsigned)nested(n&15));mix((unsigned)control());return (int)(h^(h>>32));}
