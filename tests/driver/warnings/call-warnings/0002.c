void work(int*);void before(void){work(0);}
void work(int*)__attribute__((nonnull(1)));void work(int*);void after(int*p){work(p);work(0);}
