void nn(int*)__attribute__((nonnull(1)));void all(int*,int*)__attribute__((nonnull));void repeated(int*,int*)__attribute__((nonnull(1)));void repeated(int*,int*)__attribute__((nonnull(2)));void normal(int*);
void f(void){nn(0);nn((void*)0);all(0,0);repeated(0,0);normal(0);if(0)nn(0);(void)sizeof(nn(0));}
void g(void){{extern void normal(int*)__attribute__((nonnull(1)));normal(0);}normal(0);}
