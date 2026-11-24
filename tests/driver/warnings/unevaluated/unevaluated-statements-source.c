int g(int);
void f(int i) {
    (void)sizeof(({ int x = 1; x; })); (void)sizeof(({ int x; x; })); (void)sizeof(({ g(i); 1; }));
    (void)sizeof(({ __builtin_constant_p(i++); })); (void)sizeof(({ (void)__builtin_constant_p(i++); 1; }));
    (void)sizeof(({ int y = i++; y; })); (void)sizeof(({ int y[i++]; 1; })); (void)sizeof(({ if (i) i = 2; 1; }));
    (void)sizeof(({ sizeof(i++); })); (void)sizeof(({ return; 1; })); (void)sizeof(({ i++; }) + 1);
    (void)sizeof(({ (void)({ i = 2; 1; }); 1; })); (void)_Alignof(({ i = 1; 1; }));
    (void)sizeof(({ _Generic(0, int: 1, long: i++); })); (void)sizeof(({ _Generic(0, long: 1, int: i++); }));
    (void)sizeof(({ struct { int m; } t = { i++ }; t.m; }));
    (void)sizeof(({ if (__builtin_constant_p(i++)) {} 1; })); (void)sizeof(({ (__builtin_constant_p(i++)); 1; }));
    typeof(i++) t1 = 0; (void)t1;
}
