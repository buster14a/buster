static void* buf[5];static volatile int sink;static volatile double dsink;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}__attribute__((noinline)) static int side(int x){return x+5;}
__attribute__((noinline)) static int deep(int a){int b=a+1,c=a+2,d=a+3,e=a+4,f=a+5,g=a+6,i=a+7,j=a+8,k=a+9,l=a+10,m=a+11,o=a+12,p=a+13,q=a+14,s=a+15,t=a+16,u=a+17;
int r=a+(b*(c-(d+(e*(f-(g+(i*(j-(k+(l*(m-(o+(p*(q-(s+(t*(u-__builtin_setjmp(buf))))))))))))))))));
if(r==a+(b*(c-(d+(e*(f-(g+(i*(j-(k+(l*(m-(o+(p*(q-(s+(t*u))))))))))))))))){sink=u+(t*(s-(q+(p*(o-(m+(l*(k-(j+(i*(g-(f+(e*(d-(c+(b*side(a)))))))))))))))));jump();}return r;}
__attribute__((noinline)) static double fdeep(int a){double b=a+0.5,c=a+1.5,d=a+2.5,e=a+3.5,f=a+4.5,g=a+5.5,i=a+6.5,j=a+7.5,k=a+8.5,l=a+9.5,m=a+10.5,o=a+11.5,p=a+12.5,q=a+13.5,s=a+14.5,t=a+15.5,u=a+16.5,v=a+17.5;
double r=b+(c*(d-(e+(f*(g-(i+(j*(k-(l+(m*(o-(p+(q*(s-(t+(u*(v-(double)__builtin_setjmp(buf))))))))))))))))));
if(r==b+(c*(d-(e+(f*(g-(i+(j*(k-(l+(m*(o-(p+(q*(s-(t+(u*v))))))))))))))))){dsink=v+(u*(t-(s+(q*(p-(o+(m*(l-(k+(j*(i-(g+(f*(e-(d+(c*(double)side(a)))))))))))))))));jump();}return r;}
int tested(int n){h=(unsigned)n;int k=(n&15)+1;mix((unsigned)deep(k));mix((unsigned)sink);mix((unsigned long long)(fdeep(k)*4));mix((unsigned long long)(dsink*4));return (int)(h^(h>>32));}
