union U{int i;unsigned u;};void take(union U);typedef union U T __attribute__((transparent_union));void after(void){take(1);}
