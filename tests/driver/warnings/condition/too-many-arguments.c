void f2(int x) __attribute__((diagnose_if(x, "m", "warning", "foo", "bar")));
