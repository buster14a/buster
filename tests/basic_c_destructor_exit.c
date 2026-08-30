// A destructor has to run when the program terminates by calling `exit`, not
// only when `main` returns (issue 781).  An image this linker writes has no
// libc startup object -- the entry stub it synthesizes is the startup -- so
// the stub used to call the destructors where `main` came back, and a program
// that reached `exit` from inside `main` never came back to it.  GNU registers
// the walk as an exit handler instead, which runs it whichever way the program
// ends and runs it after every handler the program registered itself, because
// the walk was registered first.  Both halves are checked here.
//
// Nothing in the program is observable once the last destructor has run, so
// the process status is the whole report: `_exit` from that destructor is what
// puts this fixture's own verdict where the test can read it, and it is the
// terminating call that runs no further handler and so cannot recurse.
//
// The prototypes are written out instead of included because the fixture is
// linked for the PE target as well, where a cross host has no C runtime
// headers to read.  `atexit` is only named where the program can reach it:
// ucrtbase.dll exports `_crt_atexit` and not `atexit`, so a PE image reaches
// that table through the name the entry stub uses and a C program cannot.

extern void exit(int status);
extern void _exit(int status);
#if !defined(_WIN32)
extern int atexit(void (*handler)(void));
#endif

// What ran, in the order it ran.  Every step records one identifier, and the
// last destructor compares the whole sequence against the one GNU produces.
static int order[8];
static int order_count;

static void record(int identifier)
{
    if (order_count < (int)(sizeof(order) / sizeof(order[0])))
    {
        order[order_count] = identifier;
    }
    order_count += 1;
}

#if !defined(_WIN32)
// Registered by the constructor, so it runs after the one `main` registers
// and before either destructor.
static void handler_from_constructor(void)
{
    record(3);
}

// Registered last and therefore run first: exit handlers run in reverse
// registration order.
static void handler_from_main(void)
{
    record(2);
}
#endif

__attribute__((constructor)) static void construct(void)
{
    record(1);
#if !defined(_WIN32)
    atexit(handler_from_constructor);
#endif
}

// Written before the destructor below and therefore run after it: `.fini_array`
// is walked backwards.  This is the last thing the program does, which is why
// it is the one that reports.
__attribute__((destructor)) static void report(void)
{
    static int const expected[] = {
#if defined(_WIN32)
        1, 4, 5,
#else
        1, 2, 3, 4, 5,
#endif
    };
    int expected_count = (int)(sizeof(expected) / sizeof(expected[0]));
    record(5);
    if (order_count != expected_count)
    {
        _exit(2);
    }
    for (int index = 0; index < expected_count; index += 1)
    {
        if (order[index] != expected[index])
        {
            _exit(3 + index);
        }
    }
    _exit(0);
}

__attribute__((destructor)) static void destruct(void)
{
    record(4);
}

int main(void)
{
#if !defined(_WIN32)
    atexit(handler_from_main);
#endif
    // Not `return`: returning is the case that already worked, and the whole
    // point of the fixture is the one that did not.  A status nothing
    // overwrote would be this 1, which is why 1 is not a passing status.
    exit(1);
    // `exit` does not return; reaching this would be the compiler's fault.
    return 1;
}
