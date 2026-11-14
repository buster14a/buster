void f(int *p);
void g(int n) { switch (n) {
int a __attribute__((cleanup(f)));
case 1:;
} }
