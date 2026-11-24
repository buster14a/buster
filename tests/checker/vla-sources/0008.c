int x;
int f(void){__typeof__(int[x]) a;return sizeof(a);}
extern int x asm("b");
