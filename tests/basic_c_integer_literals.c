/* Shared integer admission must preserve valid values and runtime effects. */
#define JOIN(a, b) a ## b
#define STR(x) #x
#define UNUSED_BAD_INTEGER 09
#if 0
static int inactive = 09;
#endif
#if 077 != 63 || 0xffffffffffffffffULL != 18446744073709551615ULL
#error integer preprocessing value mismatch
#endif

static unsigned long long maximum = JOIN(18446744073709551615, ULL);
static unsigned long long octal_maximum = 01777777777777777777777ULL;
static volatile int calls;
static const char *spelling = STR(09);

static unsigned long long value(void)
{
    calls += 1;
    return maximum;
}

int main(void)
{
    unsigned long long local = 0xFFFFFFFFFFFFFFFFuLL;
    int failures = maximum != local || octal_maximum != local;
    failures += (maximum + 1ULL) != 0;
    failures += 077uL != 63 || 0xFFll != 255 || 42LLU != 42;
    failures += sizeof(value()) != sizeof(unsigned long long);
    failures += calls != 0;
    failures += (0 && value()) != 0;
    failures += calls != 0;
    failures += (1 ? 42ULL : value()) != 42;
    failures += calls != 0;
    local = value();
    failures += calls != 1 || local != maximum;
    failures += (0 ? value() : 7ULL) != 7;
    failures += calls != 1;
    failures += (1 && value()) != 1;
    failures += calls != 2;
    failures += spelling[0] != '0' || spelling[1] != '9' || spelling[2] != 0;
    failures += sizeof(09.0) != sizeof(double);
    return failures;
}
