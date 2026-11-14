void f(int *p);
void g(int n) {
goto l;
{ int a __attribute__((cleanup(f))); l:; }
}
