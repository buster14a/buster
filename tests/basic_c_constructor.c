// __attribute__((constructor)) and ((destructor)), and the order they run in
// (issue 771).  Every function here is `static` and none is called from an
// expression, which is the shape that used to leave the translation unit with
// an empty .text: the attribute made nothing reachable, so unused-static
// elimination dropped the only functions in the file.
//
// Each case exits with its own status so a failure names itself.  The fixture
// deliberately touches no library function: it is linked and run for the
// hosted ELF, PE and, where the host has an emulator for it, cross targets,
// and a libc call would make it about the import machinery instead.

// Filled by the constructors, in the order they ran.
static int order[8];
static int order_count;
// A constructor may write a global that main then reads; `nonzero` proves the
// write reached the same object main looks at rather than a copy.
static int nonzero;
// Incremented by the destructor.  main must see zero: a destructor runs after
// main returns, not before it starts.
static int destroyed;

static void record(int identifier)
{
    if (order_count < (int)(sizeof(order) / sizeof(order[0])))
    {
        order[order_count] = identifier;
    }
    order_count += 1;
}

// GNU runs every prioritized constructor before every unprioritized one, and
// prioritized ones in ascending order, so the three below must run 1, 2, 3
// whatever order they are written in.  They are written out of that order on
// purpose.
__attribute__((constructor)) static void without_priority(void)
{
    record(3);
    nonzero = 7;
}

__attribute__((constructor(150))) static void with_later_priority(void)
{
    record(2);
}

__attribute__((constructor(101))) static void with_earlier_priority(void)
{
    record(1);
}

// `__destructor__` is the reserved spelling a header may use where a macro
// could capture the plain one; both have to be recognized.
__attribute__((__destructor__)) static void at_exit(void)
{
    destroyed += 1;
}

int main(void)
{
    if (order_count != 3)
    {
        return 1;
    }
    if (order[0] != 1)
    {
        return 2;
    }
    if (order[1] != 2)
    {
        return 3;
    }
    if (order[2] != 3)
    {
        return 4;
    }
    if (nonzero != 7)
    {
        return 5;
    }
    if (destroyed != 0)
    {
        return 6;
    }
    return 0;
}
