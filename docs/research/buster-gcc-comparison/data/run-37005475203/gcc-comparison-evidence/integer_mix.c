extern int printf(const char *, ...);
int main(void) {unsigned int x=1; for (unsigned int i=0;i<50000000u;i++) { x^=x<<13; x^=x>>17; x^=x<<5; }printf("%u\n",x); return 0;}
