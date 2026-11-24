void f(int i, int n) {
    for (; i < n;) { (void)__builtin_constant_p(({ i = 8; 1; })); }
    for (; i < n + __builtin_constant_p(({ i = 6; 1; })); ) { }
    for (; i < n;) { (void)sizeof(({ i = 9; 1; })); }
    for (; i < n;) { (void)_Alignof(({ i = 1; 1; })); }
    for (; i < n;) { typeof(({ i = 1; 1; })) t = 0; (void)t; }
    for (; i < n;) { (void)({ i = 1; 1; }); }
    for (; i < n;) { (void)__builtin_constant_p(({ ({ i = 2; 1; }); })); }
    for (; i < n;) { (void)sizeof(({ (void)({ i = 2; 1; }); 1; })); }
    for (; i < n;) { (void)_Generic(1, int: ({ i = 4; 1; }), long: 2); }
    for (; i < n;) { (void)_Generic(1, long: ({ i = 4; 1; }), int: 2); }
    for (; i < n;) { (void)__builtin_choose_expr(1, ({ i = 4; 1; }), 2); }
    for (; i < n;) { (void)__builtin_choose_expr(0, ({ i = 4; 1; }), 2); }
    for (; i < n;) { (void)__builtin_object_size(({ i = 5; &i; }), 0); }
    for (; i < n;) { (void)_Generic(1, long: ({ (void)({ i = 4; 1; }); 1; }), int: 2); }
    for (; i < n;) { (void)_Generic(1, int: ({ (void)({ i = 4; 1; }); 1; }), long: 2); }
    for (; i < n;) { (void)_Generic(1, int: ({ (void)sizeof(({ i = 4; 1; })); 1; }), long: 2); }
}
void g(int i, int n) {
    for (; i < n;) { (void)sizeof(({ return; 1; })); }
    for (; i < n;) { (void)sizeof(({ break; 1; })); }
    for (; i < n;) { (void)({ break; 1; }); }
    for (; i < n;) { (void)_Generic(1, long: ({ goto out; 1; }), int: 2); }
    for (; i < n + sizeof(({ i = 2; 1; }));) { }
    for (; i < n; (void)sizeof(({ i = 2; 1; }))) { }
    for (; i < n; (void)__builtin_constant_p(({ (void)({ i++; 1; }); 1; }))) { }
    for (; i < n;) { (void)sizeof(int[({ i = 1; n; })]); }
    for (; i < n;) { (void)sizeof(int[i++]); }
    for (; i < n;) { (void)sizeof(int (*)[i++]); }
    for (; i < n;) { int a[n]; (void)sizeof(a[({ i = 3; 1; })]); }
    for (; i < n;) { int a[({ i = 3; n; })]; (void)a; }
    for (; i < n;) { int a = ({ i = 3; 8; }); (void)a; }
    (void)sizeof(({ for (; i < n;) { i++; } 1; }));
out:;
}
