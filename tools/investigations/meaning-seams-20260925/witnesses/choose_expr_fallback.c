extern int printf(const char *, ...);
static volatile int input;
static volatile int hits;
static int mark(void) { hits += 1; return input; }

int main(void) {
int actual = 0;
actual = __builtin_choose_expr(__builtin_constant_p(mark() && 0), 0, ((void)(mark() && 0), 1));
printf("actual=%d expected=%d hits=%d expected_hits=%d\n", actual, 1, hits, 1);
return (actual != (1)) | ((hits != (1)) << 1);
}
