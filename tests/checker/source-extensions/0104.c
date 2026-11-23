int f(void *p){return ((__attribute__((noinline)) int(*)(void))p)();}
