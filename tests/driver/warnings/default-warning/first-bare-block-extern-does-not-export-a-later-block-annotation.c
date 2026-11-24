void declare(void){extern int printf(const char*,...);extern int printf(const char*,...)__attribute__((format(printf,1,2)));extern int printf(const char*,...);}
int printf(const char*,...);
void call(void){printf("%d","str");}
