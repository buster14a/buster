extern int printf(const char *, ...);
volatile double seed=0.25; int main(void){double x=seed;for(unsigned int i=0;i<50000000;i++)x=x*0.999999+0.000001;printf("%.17g\n",x);return 0;}
