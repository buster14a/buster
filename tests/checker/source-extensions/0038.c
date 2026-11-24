union U{int i;unsigned u;};void take(union U);
void before(void){take(1);}
typedef union U T __attribute__((transparent_union));
void after(void){take(1);}
