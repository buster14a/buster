void f(int x[const 5]); void f(int *const x); void g(int x[static restrict 5]); void g(int *restrict x); void h(int x[const *]); void h(int *const x); void grouped(int (x[const 3]));
