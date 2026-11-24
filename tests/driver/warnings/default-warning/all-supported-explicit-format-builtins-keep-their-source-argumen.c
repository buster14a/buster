typedef struct _IO_FILE FILE;
void f(FILE*p,char*b){__builtin_printf("%d","str");__builtin_fprintf(p,"%d","str");__builtin_sprintf(b,"%d","str");__builtin_snprintf(b,16,"%d","str");__builtin_snprintf(b,16,"%s.mo\0","x");}
