extern int printf(const char *, ...);
static volatile int input;
static volatile int hits;
static int mark(void) { hits += 1; return input; }
#define PICK(e) (__builtin_constant_p(e) ? 0 : ((void)(e), 1))

int main(void) {
int actual = 0;
actual = PICK(mark() && 0);
printf("actual=%d expected=%d hits=%d expected_hits=%d\n", actual, 1, hits, 1);
return (actual != (1)) | ((hits != (1)) << 1);
}
