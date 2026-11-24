typedef __SIZE_TYPE__ size_t;void*malloc(size_t)__attribute__((warn_unused_result));int printf(const char*,...)__attribute__((pure));
void f(int a,size_t n){a&&malloc(n);a&&printf("%d",a);}
