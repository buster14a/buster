extern int printf(const char *, ...);
static volatile int input;
static volatile int hits;
static int mark(void) { hits += 1; return input; }

int main(void) {
int actual = 0;
actual = mark() && 0;
printf("actual=%d expected=%d hits=%d expected_hits=%d\n", actual, 0, hits, 1);
return (actual != (0)) | ((hits != (1)) << 1);
}
