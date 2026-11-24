int printf(const char*,...);
int snprintf(char*,unsigned long,const char*,...);
void f(char*b){printf("%d",7);snprintf(b,16,"%s.mo","x");}
