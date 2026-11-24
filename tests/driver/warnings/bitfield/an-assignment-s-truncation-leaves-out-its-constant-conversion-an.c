struct S { signed char c : 2; unsigned char d : 4; int b : 3; unsigned u : 3; } s;
int f(int);
void g(int x) {
    s.c = 200;
    s.d = 300;
    s.b = 9, s.b = 10;
    f(s.b = 12);
    s.b = s.u = 9;
    s.b = 1 ? 9 : 2;
    s.b = 1 << 32;
    x ? (s.b = 9) : (s.u = 9);
    0 && (s.b = 10);
    return;
    s.b = 9;
}
