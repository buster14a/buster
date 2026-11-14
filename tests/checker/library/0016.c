typedef struct F FILE;
int f(FILE* p, __builtin_va_list a){return __builtin_fprintf(p, "%d", 1) + __builtin_vfscanf(p, "x", a) + __builtin___fprintf_chk(p, 1, "x") + __builtin___vfprintf_chk(p, 1, "x", a);}
