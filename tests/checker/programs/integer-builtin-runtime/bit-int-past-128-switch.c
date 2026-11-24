typedef _BitInt(256) W;typedef unsigned _BitInt(200) U;typedef _BitInt(129) N;
static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
static int f(W x){switch(x){case (W)1<<130:return 1;case -((W)1<<200):return 2;case 7:return 3;case -1:return 4;
case ((W)5<<180)...((W)9<<180):return 5;case -((W)9<<100)...-((W)5<<100):return 6;case ((W)1<<130)+7:return 7;default:return 0;}}
static int g(U x){switch(x){case (U)1<<199:return 1;case 0 ... 3:return 2;case (U)-1:return 3;case ((U)3<<64)...((U)3<<64)+5:return 4;}return 0;}
static int k(N x){switch(x){case -((N)1<<127)*2:return 1;case ((N)1<<127)+(((N)1<<127)-1):return 2;case 0:return 3;case -5 ... -1:return 4;}return 9;}
int tested(int n){h=(unsigned)n;W base[]={(W)1<<130,-((W)1<<200),7,-1,(W)5<<180,(W)9<<180,-((W)9<<100),-((W)5<<100),((W)1<<130)+7,0,(W)3<<64};
for(unsigned i=0;i<sizeof base/sizeof base[0];i++)for(int d=-2;d<=2;d++){W v=base[i]+d+(n&1);mix(f(v));mix(g((U)v));mix(k((N)v));
mix(f(-v));mix(g((U)-v));mix(k((N)-v));}
return (int)(h^(h>>32));}