int report(const char*,...)__attribute__((format(printf,1,2)));
int report(const char*,...)__attribute__((noinline));
void f(void){report("%d","str");}
