int g; void f1(int x) __attribute__((diagnose_if(g, "glob", "warning")));
