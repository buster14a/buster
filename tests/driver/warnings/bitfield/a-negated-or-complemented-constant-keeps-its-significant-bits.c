struct S { int b : 3; unsigned u : 3; } s;
void g(void) {
    s.b = ~8;
    s.b = -(9);
    s.b = -4;
    s.b = -5;
    s.u = -9;
    s.u = ~0u;
    s.u = -1;
    s.b = 0 - 5;
}
