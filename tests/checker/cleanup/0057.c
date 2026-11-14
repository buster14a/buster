void f(int *p);
void g(int n) { void *t[] = { &&l };
{ int a __attribute__((cleanup(f)));
goto *t[0]; }
l:;
}
