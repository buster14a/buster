static int arm(void) { return 1; }
static int association(void) { return 2; }
static int operand(void) { return 3; }
static void release(int* p) { (void)p; }
static char buffer[8];
int main(int argc, char** argv)
{
    int r = 0;
    if (0)
    {
        r = arm();
    }
    r += _Generic(r, long: association, default: operand)() == 3;
    r += 1 ? 0 : operand();
    r += (int)sizeof "literal";
    __builtin_memcpy(buffer, argv[0], 4);
    {
        __attribute__((cleanup(release))) int held = argc;
        r += held;
    }
    return r;
}
