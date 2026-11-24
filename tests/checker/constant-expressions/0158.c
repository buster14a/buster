int a[3]; _Static_assert(sizeof(__builtin_choose_expr(1,a,0))==12, "array");
