int printf(const char*, ...);
static _Atomic int a = 5;
static _Atomic long l = 6;
static _Atomic double d = 6;
static _Atomic short s = 6;
static _Atomic char c = 6;
int main(void) { printf("%d %ld %f %hd %c\n", a, l, d, s, c); return 0; }
