// The expression and statement shapes sbase's sources put in front of IR
// lowering. Every one of these produced a plausible-looking refusal or, worse,
// a silently wrong value, and none of them is visible from the diagnostic
// alone -- so the fixture runs and each check returns its own status.

struct rules
{
    const char* name;
    int (*check)(int);
};

struct command;
struct handler
{
    int (*argument)(struct command*, int);
};

struct command
{
    struct handler* info;
};

static int odd(int value)
{
    return value & 1;
}

static int doubled(struct command* command, int value)
{
    (void)command;
    return value * 2;
}

// 1. A call through a function pointer reached by subscripting. sbase's tr
//    walks its class table this way (`classes[i].check(rune)`), and the
//    callee's place starts at the array rather than at the token before the
//    dot.
static struct rules classes[] = {{"odd", odd}, {"odd", odd}};

// 2. A call through a chain rooted in a parenthesized expression, which is how
//    sbase's sed reaches the argument parser of the previous command
//    (`(pc - 1)->fninfo->getarg(...)`). That chain is not a place the place
//    machine can form, and its first token is also the call's own first token.
static struct handler handler = {doubled};

// 3. A static initializer that points into another object. sbase's dc builds
//    its constant one this way: `{.buf = onestr, .wp = onestr + 1}`. An array
//    lvalue decays to a pointer before the addition, and without that decay
//    the addend was silently dropped -- the pointer aimed at element zero and
//    dc printed zero for every exponentiation.
static signed char digits[] = {1, 2, 3, 0};
static signed char* first = digits;
static signed char* second = digits + 1;

struct span
{
    signed char* begin;
    signed char* end;
};

static struct span span = {.begin = digits, .end = digits + 3};

// 4. A function designator named by a block-scope extern declaration, stored
//    into a function pointer. sbase's make declares its signal handler inside
//    the function that installs it.
void sbase_signal_handler(int signal);
void sbase_signal_handler(int signal)
{
    (void)signal;
}

struct action
{
    void (*handler)(int);
};

// 5. Control reaching the end of a non-void function is undefined only if the
//    caller uses the value (C 6.9.1p12). sbase's dc ends a function with a
//    call to its own error reporter, which is not declared noreturn; the body
//    lowers and its tail is unreachable.
static int classify(int value)
{
    if (value > 0)
    {
        return 1;
    }
    if (value <= 0)
    {
        return -1;
    }
}

// 6. `*d++ = *s++` in a value position: the copy loop of strlcpy, which tests
//    the assigned character while advancing both pointers.
static unsigned long copy(char* destination, const char* source, unsigned long size)
{
    char* out = destination;
    const char* in = source;
    unsigned long left = size;
    if (left != 0)
    {
        while (--left != 0)
        {
            if ((*out++ = *in++) == '\0')
            {
                break;
            }
        }
    }
    if (left == 0)
    {
        if (size != 0)
        {
            *out = '\0';
        }
        while (*in++)
        {
        }
    }
    return (unsigned long)(in - source - 1);
}

int main(void)
{
    if (classes[1].check(3) != 1 || classes[0].check(4) != 0)
    {
        return 1;
    }

    struct command commands[2];
    commands[0].info = &handler;
    struct command* cursor = commands + 1;
    if ((cursor - 1)->info->argument(cursor - 1, 21) != 42)
    {
        return 2;
    }

    if (first != digits || second != digits + 1 || *second != 2)
    {
        return 3;
    }
    if (span.begin != digits || span.end != digits + 3 || *span.end != 0)
    {
        return 4;
    }

    {
        extern void sbase_signal_handler(int signal);
        struct action action = {.handler = sbase_signal_handler};
        if (action.handler != sbase_signal_handler)
        {
            return 5;
        }
        action.handler(0);
    }

    if (classify(7) != 1 || classify(-7) != -1)
    {
        return 6;
    }

    char buffer[8];
    if (copy(buffer, "abc", sizeof buffer) != 3 || buffer[0] != 'a' || buffer[3] != '\0')
    {
        return 7;
    }

    // 7. An assignment inside a conditional arm, as a statement. sbase's tar
    //    reorders its own arguments this way. The statement is split at the
    //    first top-level assignment operator, and the one in an arm is not
    //    the statement's.
    int count = 2;
    char mode = ' ';
    int stepped = 0;
    count ? mode = '-' : (stepped = 1, count -= 1);
    if (mode != '-' || stepped != 0 || count != 2)
    {
        return 8;
    }
    count = 0;
    count ? mode = '+' : (stepped = 1, count -= 1);
    if (mode != '-' || stepped != 1 || count != -1)
    {
        return 9;
    }

    return 0;
}
