void declare(void){extern int printf(const char*,...)__attribute__((format(printf,1,2)));}
int printf(const char*,...);
void call(void){printf("%d","str");}
