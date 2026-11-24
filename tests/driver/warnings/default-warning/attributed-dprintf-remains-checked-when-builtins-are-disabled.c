int dprintf(int,const char*,...)__attribute__((format(printf,2,3)));
void f(void){dprintf(1,"%d","str");}
