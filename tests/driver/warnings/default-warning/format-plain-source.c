typedef struct _IO_FILE FILE;
int printf(const char*,...);
int fprintf(FILE*,const char*,...);
int sprintf(char*,const char*,...);
int snprintf(char*,unsigned long,const char*,...);
void f(FILE*p,char*b){printf("%d","str");fprintf(p,"%d","str");sprintf(b,"%d","str");snprintf(b,16,"%d","str");snprintf(b,16,"%s.mo\0","x");}
