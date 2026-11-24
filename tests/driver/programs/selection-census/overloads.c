static int __attribute__((overloadable)) pick(int x) { return x; }
static int __attribute__((overloadable)) pick(double x) { return (int)x; }
int main(void) { return pick(1); }
