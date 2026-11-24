void f1(int n) { int i = 0; for (; i < n; ) { (void)__builtin_constant_p(i = 6); } }
void f2(int n) { int i = 0; for (; i < n; ) { (void)__builtin_object_size(&i, 0); } }
void f5(int n) { int i = 0; for (; i < n; ) { (void)__builtin_dynamic_object_size(&i, 0); } }
void f6(int n) { int i = 0; for (; i < n; ) { (void)__builtin_classify_type(i = 6); } }
void f7(int n) { int i = 0; for (; i < n; __builtin_constant_p(i = 6)) { } }
void f8(int n) { int i = 0; for (; i < n; ) { (void)__builtin_constant_p(0 ? 0 : (i = 6)); } }
void g(int n) {
    int i = 0;
    for (; ({ for (int x = 0; x < 2; ) { } 1; }); ) { }
    for (; ({ do { } while (i < 2); 1; }); ) { }
    for (; ({ int y = 0; for (; y < n; ) { } 1; }) && i < n; ) { }
    for (; i < ({ for (int z = 0; z < 2; ) { } 1; }); ) { }
    for (; (struct { int a; }){ i }.a < n; ) { }
}
