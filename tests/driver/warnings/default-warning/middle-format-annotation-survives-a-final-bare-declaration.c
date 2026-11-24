int printf(const char*,...);
int printf(const char*,...)__attribute__((format(printf,1,2)));
int printf(const char*,...);
void f(void){printf("%d","str");}
