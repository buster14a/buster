// An unprototyped declaration followed by its prototyped definition declares
// one function (C11 6.2.7p3): older C and musl's `long __syscall_cp_asm();`
// rely on it. Each shape below used to reach the LLVM writer as two function
// records for one symbol. The program exits 0 when every call returns the
// expected value.
static int twice(int x) { return 2 * x; }

long add();
long add(long a, long b) { return a + b; }

// A call written before the definition resolves through the later prototype.
long scale();
static long use_scale(void) { return scale(6L, 7L); }
long scale(long a, long b) { return a * b; }

// With a prototype in between, the first one spelled is the one calls use.
long difference();
long difference(long a, long b);
long difference(long a, long b) { return a - b; }

// Internal linkage, and a static initializer that names the symbol.
static long negated();
static long negated(long a) { return -a; }
static long (*negate)(long) = negated;

// A function-pointer return: each declarator builds its own pointee type.
int (*pick())(int);
int (*pick(int w))(int)
{
    (void)w;
    return twice;
}

int main(void)
{
    int failures = 0;
    failures += add(2, 3) != 5;
    failures += use_scale() != 42;
    failures += difference(9L, 4L) != 5;
    failures += negate(8L) != -8;
    failures += negated(3L) != -3;
    failures += pick(1)(4) != 8;
    return failures;
}
