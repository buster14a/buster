int work(int);void before(int a){a&&work(a);}
int work(int)__attribute__((pure));int work(int);void after(int a){a&&work(a);}
