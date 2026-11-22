#define OV __attribute__((overloadable))
int zero(void) OV;
int zero(int) OV;
int variadic(void) OV;
int variadic(int, ...) OV;
int ellipsis(...) OV;
int ellipsis(int) OV;
int both(void) OV __attribute__((enable_if(1, "always")));
int both(void) OV;
int both(int) OV;
int none(int) OV;
int none(long) OV;
int t(void) {
    int sum = zero + 1;
    sum += variadic + 1;
    sum += ellipsis + 1;
    sum += both + 1;
    sum += none + 1;
    return sum;
}
