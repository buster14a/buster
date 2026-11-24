void g(int n) {
    char vv[n][n];
    (void)sizeof(({ 3; vv[0]; }));
    (void)sizeof(({ 4; vv; }));
}
