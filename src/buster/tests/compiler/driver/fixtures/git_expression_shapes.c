typedef __INTPTR_TYPE__ intptr;
struct Alignment { char unalign; union { unsigned long integer; void *pointer; } aligned; };
struct Option { intptr value; };
static struct Option option = {(intptr)"all"};
static intptr address = (intptr)"x";
_Alignas(32) static char standard_alignment;
static char gnu_alignment __attribute__((unused, aligned(64)));
static int hits;

#define BUILD_ASSERT_OR_ZERO(cond) (sizeof(char[1 - 2*!(cond)]) - 1)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]) + BUILD_ASSERT_OR_ZERO(!__builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0]))))

static int tick(void)
{
    hits += 1;
    return hits;
}

static int enter_body(int c)
{
    goto inside;
    for (;;)
    {
        if (c == 1)
        {
inside:
            c = 0;
        }
        else if (c == 2)
        {
            c = 3;
        }
        return c;
    }
}

int main(void)
{
    int failed = 0;
    int a = 0, b = 0;
    if ((a = b = tick()))
    {
        failed |= a != 1 || b != 1 || hits != 1;
    }
    else
    {
        failed = 1;
    }
    int nested = ({ int value = 7; __extension__ ({ hits += 1; value; }); });
    failed |= nested != 7 || hits != 2;
    failed |= enter_body(2) != 0;
    failed |= __builtin_offsetof(struct Alignment, aligned) != sizeof(void *);
    failed |= __alignof__(standard_alignment) != 32 || __alignof__(gnu_alignment) != 64;
    failed |= *(char *)address != 'x' || ((char *)option.value)[2] != 'l';
    failed |= ARRAY_SIZE(((int[]){tick(), tick()})) != 2 || hits != 2;
    failed |= sizeof((((double[]){1.0, 2.0}))[0]) != sizeof(double);
    return failed;
}
