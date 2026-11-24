int printf(const char *, ...);
int f(int x)
{
    if (x = ({ printf("%s", 1); x; })) return 1;
    if (x = printf("%s", 2)) return 1;
    while (x = ({ printf("%s", 3); x; })) {}
    for (; x = ({ printf("%d", "v"); 1; }); ) {}
    do {} while (x = ({ printf("%d", "w"); 1; }));
    printf("%s", ({ printf("%d", "s"); 1; }));
    printf("%s", printf("%d", "t"));
    int a = x & ({ printf("%d", "u"); 1; }) == 2;
    int b = !x == ({ printf("%d", "y"); 1; });
    (void)a; (void)b;
    return (x = 1) ? printf("%s", 4) : 0;
}
