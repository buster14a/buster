int *q;
unsigned long f(int x) {
    ({ *q; 1; });
    sizeof(({ 1; 2; }));
    unsigned long n = sizeof(int[({ x == 1; 2; })]) + sizeof(({ int w[({ 3; 4; })]; 5; }));
    return n + ({ x < 1; 6; }) + sizeof(int[sizeof(({ 7; 8; }))]);
}
