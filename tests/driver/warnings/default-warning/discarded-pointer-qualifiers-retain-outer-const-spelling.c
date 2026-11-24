const int b=1;int *const c=&b;
void f(const int *const a){int *const b=a;(void)b;}
int *g(const int *const a){return a;}
