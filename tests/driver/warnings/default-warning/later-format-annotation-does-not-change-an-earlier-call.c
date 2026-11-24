int report(const char*,...);
void before(void){report("%d","str");}
int report(const char*,...)__attribute__((format(printf,1,2)));
int report(const char*,...);
void after(void){report("%d","str");}
