int pure_fn(int)__attribute__((pure));int pure_fn(int);int const_fn(int)__attribute__((const));int const_fn(int);int required(int)__attribute__((warn_unused_result));int required(int);
void f(int a){a&&pure_fn(a);a&&const_fn(a);a&&required(a);}
