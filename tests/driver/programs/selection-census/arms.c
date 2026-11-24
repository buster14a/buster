static int dead_case(void) { return 7; }
static int live_case(void) { return 8; }
static int else_arm(void) { return 9; }
static int entered(void) { return 10; }
int main(int argc, char** argv)
{
    int r = 0;
    (void)argv;
    switch (2)
    {
    case 1:
        r = dead_case();
        break;
    case 2:
        r = live_case();
        break;
    }
    if (1)
    {
        r += 1;
    }
    else
    {
        r += else_arm();
    }
    if (argc > 5)
    {
        goto inside;
    }
    if (0)
    {
    inside:
        r += entered();
    }
    return r;
}
