void f1(int x) __attribute__((diagnose_if(x, "m", "warning", "foo")));
