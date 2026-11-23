int pick(int x){return x+5;} double pick(double) __attribute__((overloadable)); int run(void){return pick(7);}
