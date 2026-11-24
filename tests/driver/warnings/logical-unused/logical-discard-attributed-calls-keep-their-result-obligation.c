int pure_fn(int)__attribute__((pure));int const_fn(int)__attribute__((const));int required(int)__attribute__((warn_unused_result));
void f(int a){a&&pure_fn(a);const_fn(a)||a;a&&required(a);a&&pure_fn(a++);}
