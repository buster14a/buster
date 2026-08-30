// The function-pointer conversions that stay legal, beside the incompatible
// ones tests/basic_c_function_pointer_conflict.c pins as refused (issue #830).
// Assigning a prototype to an incompatible one used to compile in silence,
// which flipped autoconf probes that read the diagnostic as their answer:
// CPython's readline check assigns `int(const char*, int)` to an `int(*)(void)`
// and concludes from the refusal that the hook takes no arguments.
//
// Everything here is what the refusal must not reach. An unprototyped `()` is
// compatible with a non-variadic prototype (C11 6.2.7p3) in both directions --
// musl's `long __syscall_cp_asm();` beside its eight-parameter prototype is one
// function -- a `void*` crossing is the idiom every dispatch table is built
// from, and a matching prototype is a matching prototype however it is spelled.

typedef int Hook(void);
typedef int (*HookPointer)(void);

static int no_arguments(void)
{
    return 1;
}

static int two_arguments(int first, char* second)
{
    (void)second;
    return first;
}

static int variadic(int first, ...)
{
    return first;
}

static int unprototyped();
static int unprototyped(void)
{
    return 2;
}

static int through_typedef(void)
{
    return 4;
}

static void* generic;
static int (*plain)(void);
static int (*pair)(int, char*);
static int (*varying)(int, ...);

static int call_through(int (*callee)(void))
{
    return callee();
}

static int (*answer_pointer(void))(void)
{
    return no_arguments;
}

int main(void)
{
    Hook* named = through_typedef;
    HookPointer alias = no_arguments;
    int (*from_unprototyped)() = unprototyped;
    int (*to_prototype)(void) = unprototyped;
    int total = 0;
    plain = no_arguments;
    pair = two_arguments;
    varying = variadic;
    generic = (void*)no_arguments;
    total += named();
    total += alias();
    total += plain();
    total += pair(2, 0);
    total += varying(8);
    total += from_unprototyped();
    total += to_prototype();
    total += call_through(no_arguments);
    {
        int (*returned)(void) = answer_pointer();
        total += returned();
    }
    if (!generic)
    {
        return 1;
    }
    // 4 + 1 + 1 + 2 + 8 + 2 + 2 + 1 + 1
    return total == 22 ? 0 : 2;
}
