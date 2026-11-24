int a(void){return (1<<2)?1:0;}int b(int x){return !(0<<x);}int c(void){return !(2<<32);}int d(void){return !(3<<32);}int f(void){return !(1<<-1);}int g(int x){return !(-0<<x);}
