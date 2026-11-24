static int unused(int x){return x*3;}
__attribute__((target("bmi2"))) static unsigned deposit(unsigned x,unsigned m){return __builtin_ia32_pdep_si(x,m);}
static int also_unused(int x){return x+1;}
int main(void){volatile unsigned x=0x5u,m=0xf0f0u;return deposit(x,m)==0x50u?0:1;}
