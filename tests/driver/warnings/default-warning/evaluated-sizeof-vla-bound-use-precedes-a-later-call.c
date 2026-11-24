static int m(void);void f(void){int a[sizeof(int[m()])];a[0]=m();(void)a;}
