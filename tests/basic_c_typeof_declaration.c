// Three shapes musl's `weak_alias` needs, each of which failed on its own.
//
//   #define weak_alias(old, new) \
//           extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))
//
// 1. The spelling is `__typeof`. GCC and Clang accept `typeof`, `__typeof`
//    and `__typeof__`; only the first (in GNU/C23 dialects) and the last were
//    recognized here, so every `weak_alias` in a translation unit compiled
//    under -std=c99 declared nothing at all and the name failed as
//    "use of undeclared identifier" at its first use.
// 2. A declarator with no parameter list still declares a function when the
//    specifiers give it a function type (C11 6.7.6.3): `extern __typeof(f) g;`
//    declares the function `g`. It was classified as an object with a
//    function type, which has no storage and no call target.
// 3. C11 6.2.7p3 makes an unprototyped `long f();` compatible with a
//    non-variadic prototype for the same function, which is how
//    `src/thread/pthread_cancel.c` declares `__syscall_cp_asm` twice.
//
// The results are read at run time: a declaration that resolves to the wrong
// entity still links, and only calling it says which function ran.

static int base(int x)
{
    return x * 3;
}

// The typeof-derived function declaration, in all three spellings.
extern __typeof(base) via_underscore_typeof;
extern __typeof__(base) via_underscore_typeof_underscore;

int via_underscore_typeof(int x)
{
    return x + 1;
}

int via_underscore_typeof_underscore(int x)
{
    return x + 2;
}

// The same specifier declaring an object rather than a function.
static long counter = 4;
extern __typeof(counter) counter;

// A typedef'd function type used as the specifier of a declarator with no
// parameter list, which is the shape musl's __libc_start_main.c writes as
// `static lsm2_fn libc_start_main_stage2;`.
typedef int stage_fn(int, int);
static stage_fn staged;

static int staged(int a, int b)
{
    return a * 10 + b;
}

// An unprototyped declaration followed by the prototype for the same
// function. Both name one entity, and the call must use the prototype.
static long dispatch();

static long dispatch(volatile void *token, long a, long b);

static long dispatch(volatile void *token, long a, long b)
{
    return token ? a : b;
}

// The reverse order, and a definition that completes the unprototyped form.
static int summed();

static int summed(int a, int b)
{
    return a + b;
}

// A typeof over an expression rather than a name still yields the type.
static int widths[3];

int main(void)
{
    if (base(2) != 6)
    {
        return 1;
    }
    if (via_underscore_typeof(2) != 3 || via_underscore_typeof_underscore(2) != 4)
    {
        return 2;
    }
    if (counter != 4)
    {
        return 3;
    }
    // The function's address is taken through the typeof-derived declaration
    // as well, so it has a symbol and not merely a call target.
    int (*taken)(int) = via_underscore_typeof;
    if (taken(5) != 6)
    {
        return 4;
    }
    if (staged(3, 4) != 34)
    {
        return 5;
    }
    stage_fn *staged_pointer = staged;
    if (staged_pointer(1, 2) != 12)
    {
        return 6;
    }
    if (dispatch(widths, 7, 8) != 7 || dispatch(0, 7, 8) != 8)
    {
        return 7;
    }
    if (summed(20, 3) != 23)
    {
        return 8;
    }
    // `__typeof` of a local, of an expression, and of an array member.
    int seed = 11;
    __typeof(seed) copied = seed;
    __typeof(seed + 1L) widened = seed;
    __typeof(widths[0]) element = seed;
    if (copied != 11 || widened != 11 || element != 11 || sizeof widened != sizeof(long))
    {
        return 9;
    }
    __typeof(widths) block = {1, 2, 3};
    if (sizeof block != sizeof(int) * 3 || block[2] != 3)
    {
        return 10;
    }
    return 0;
}
