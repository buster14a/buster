int a[2];
unsigned u;
int s;
unsigned long f(int x) {
    return sizeof(({ x / 0; 1; })) + sizeof(({ x % 0; 1; })) + sizeof(({ a[5]; 1; })) + sizeof(({ char c = 300; c; })) +
           sizeof(({ u < s; 1; })) + sizeof(({ int b[2]; b[2]; })) + __alignof__(({ a[2]; x / 0; 1; }));
}
