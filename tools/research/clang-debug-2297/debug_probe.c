/* Small source-level debugger fixture; the host C runtime supplies printf. */
int printf(const char *format, ...);

static volatile int debug_seed = 5;
static volatile int debug_sink;

__attribute__((noinline)) static int debug_add(int parameter)
{
    int local = parameter + 7;
    debug_sink = local; /* STOP_CALLEE: parameter=7, local=14; caller input=5, saved=7 */
    return local * 3;
}

int main(void)
{
    int input = debug_seed;
    int saved = input + 2;
    debug_sink = saved; /* STOP_MAIN: input=5, saved=7 */
    int result = debug_add(saved);
    debug_sink = result; /* STOP_RESULT: input=5, saved=7, result=42 */
    int checksum = result + saved;
    printf("checksum=%d\n", checksum);
    int exit_status = checksum == 49 && debug_sink == 42 ? 0 : 1;
    return exit_status;
}
