int g(int);
void f(int n, int *p) {
    int i = 0, j = 0;
    for ((i, j); i < n; ++i) {}
    for (; ; (i++, j)) { break; }
    for (((i, j)); ; ) { break; }
    for ((j, i++); ; ((i, 2))) { break; }
    for (; (i, j); ) { break; }
    for ((i == 1, j == 2); ; (i < 2)) { break; }
    for ((i == 1); ; ((i < 2))) { break; }
    for ((g(1), j); ; (j, (void)0)) { break; }
    for ((i, 0); ; (j, 0)) { break; }
    for ((void)(i, j); ; ) { break; }
    for ((p[0], (j)); ; ) { break; }
}
