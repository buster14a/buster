struct S { int a, b; };
void f(int i, int n, int j, int *p) {
    for (; ({ i < n; 1; }); ) { }
    for (; (int)({ i < n; 0; }); ) { }
    for (; _Generic(i, int: j < n, default: 0); ) { }
    for (; _Generic(0, int: i < n, default: 0); ) { }
    for (; i < __builtin_offsetof(struct S, b); ) { }
    for (; i < n + __builtin_constant_p(j); ) { }
    for (; i < __builtin_object_size(p, 0); ) { }
    for (; i < __builtin_classify_type(j); ) { }
    for (; __builtin_choose_expr(1, i < n, j); ) { }
    for (; i < n; ) { (void)__builtin_choose_expr(0, i++, 0); }
    for (; i < n; ) { (void)_Generic(0, int: 0, default: i++); }
    for (; i < n; ) { (void)_Generic(0, int: i++, default: 0); }
}
