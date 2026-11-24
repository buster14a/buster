struct S { int a : 1; signed char c : 2; unsigned u : 3; int b : 3; enum E { X, Y = 9 } e : 2; _Bool z : 1; } s;
void g(void) {
    s.a = 1;
    s.c = 200;
    s.u = 9u;
    s.b = 'a';
    s.b = (9);
    s.b = 3;
    s.b = 4;
    s.b = -4;
    s.u = -1;
    s.z = 2;
    s.e = Y;
    s.e = 9;
    s.b = 9L;
    s.b = 9.0;
    s.b += 9;
    s.b++;
}
