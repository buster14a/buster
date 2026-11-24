static void* buf[5];static int depth;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
static void deep(int n){volatile int pad[16];pad[n&15]=n;depth+=pad[n&15]+1;if(n>0)deep(n-1);else __builtin_longjmp(buf,1);}
static int jumps(int n){int kept=n*7+1;volatile int changed=0;if(__builtin_setjmp(buf)==0){changed=5;deep(n&7);return -1;}return kept+changed*100;}
static int frames(void){void* f[5];int r=__builtin_setjmp(f);return r==0&&f[0]==__builtin_frame_address(0);}
int tested(int n){h=(unsigned)n;depth=0;for(int i=0;i<4;i++)mix((unsigned)jumps(n+i));mix((unsigned)depth);mix((unsigned)frames());return (int)(h^(h>>32));}
