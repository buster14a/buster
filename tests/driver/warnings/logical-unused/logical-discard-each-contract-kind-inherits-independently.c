int trace(const char*,...)__attribute__((pure));int trace(const char*,...)__attribute__((format(printf,1,2)));int required(int)__attribute__((warn_unused_result));int required(int)__attribute__((const));int required(int);
void f(int a){a&&trace("%d",a);a&&required(a);}
