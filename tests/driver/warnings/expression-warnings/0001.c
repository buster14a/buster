int __attribute__((warn_unused_result)) wu(void);
[[nodiscard]] int nd(void);
void nn(int*) __attribute__((nonnull(1)));
typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
char *strcpy(char *, const char *);
void *memset(void *, int, size_t);
int abs(int);
void w(void){wu();nd();nn(0);}
void buffers(void){char a[4],b[8];memcpy(a,"123456789",9);strcpy(b,"123456789");}
int literals(void){int i=1.5; char c=1000.0; return i+c;}
int shifts(int x,long y){return (x<<40)+(x>>32)+(y<<64)+(x<<-1);}
int self(int x){return (x<x)+(x!=x);}
int range(unsigned char c){return c>300;}
int pointer(int *p){return &p==0;}
int function(void){if(w)return 1;return 0;}
int equality(int x){if ((x==1))return 1;return 0;}
int logical(int x){return !x&1;}
int string(const char *s){return s=="x";}
int sw(int x){switch(x==1){default:return 0;}}
int arr(int a[10]){return sizeof(a);}
void mem(int *p){memset(p,0,sizeof(p));}
int absolute(double d){return abs(d);}
char *cast1(int x){return (char*)x;}
int cast2(void *p){return (int)p;}
int extras(int*a,char c){label:return a[c];}
