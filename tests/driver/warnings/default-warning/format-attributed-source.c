int printf(const char*,...)__attribute__((format(printf,1,2)));
int printf(const char*,...);
int snprintf(char*,unsigned long,const char*,...);
int snprintf(char*,unsigned long,const char*,...)__attribute__((format(printf,3,4)));
int trace(int,const char*,...)__attribute__((format(printf,2,3)));
void f(char*b){printf("%d","str");snprintf(b,16,"%d","str");trace(0,"%d","str");}
