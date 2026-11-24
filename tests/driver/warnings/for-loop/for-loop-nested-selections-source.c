void f1(int i, int n) {
    for (; i < n;) {
        (void)({ (void)_Generic(1, long: ({ i++; 1; }), int: 2); 1; });
    }
}
void f2(int i, int n) {
    for (; i < n;) {
        (void)({ (void)_Generic(1, int: ({ i++; 1; }), long: 2); 1; });
    }
}
void f3(int i, int n) {
    for (; i < n;) {
        (void)({ (void)_Generic(1, long: ({ break; 1; }), int: 2); 1; });
    }
}
void f4(int i, int n) {
    for (; i < n;) {
        (void)({ (void)_Generic(1, int: ({ break; 1; }), long: 2); 1; });
    }
}
void f5(int i, int n) {
    for (; i < n;) {
        (void)_Generic(1, int: ({ (void)_Generic(1, long: ({ i++; 1; }), int: 2); 1; }), long: 2);
    }
}
void f6(int i, int n) {
    for (; i < n;) {
        (void)_Generic(1, int: ({ (void)__builtin_choose_expr(1, ({ i++; 1; }), 2); 1; }), long: 2);
    }
}
void f7(int i, int n) {
    for (; i < n;) {
        (void)({ (void)__builtin_choose_expr(0, ({ i++; 1; }), 2); 1; });
    }
}
void f8(int i, int n) {
    for (; i < n;) {
        (void)({ (void)sizeof(_Generic(1, int: ({ i++; 1; }), long: 2)); 1; });
    }
}
void f9(int i, int n) {
    for (; i < n;) {
        (void)({ (void)_Generic(1, long: ({ goto out; 1; }), int: 2); 1; });
    }
out:;
}
void f11(int i, int n) {
    for (; i < n;) {
        (void)_Generic(1, long: 0, int: ({ (void)_Generic(1, int: ({ (void)({ i++; 1; }); 1; }), long: 2); 1; }));
    }
}
void f12(int i, int n) {
    for (; i < n;) {
        (void)_Generic(1, long: 0, int: ({ (void)_Generic(1, long: ({ (void)({ return; 1; }); 1; }), int: 2); 1; }));
    }
}
