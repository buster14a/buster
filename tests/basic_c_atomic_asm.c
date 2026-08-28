// The inline-assembly vocabulary a libc's atomics and thread pointer are
// written in, in the shapes musl's arch/x86_64/atomic_arch.h and
// pthread_arch.h use: a memory operand naming the storage rather than its
// value, the LOCK prefix separated from its instruction by a semicolon, a
// literal immediate, the stack pointer as a memory base, and a segment
// override.
//
// Every operation is checked against the answer it is supposed to produce
// rather than only assembled, because a memory operand that reaches the wrong
// address still assembles.

#if defined(__x86_64__) || defined(_M_X64)

typedef unsigned long long atomic_u64;

static int a_cas(volatile int* p, int t, int s)
{
    __asm__ __volatile__("lock ; cmpxchg %3, %1" : "=a"(t), "=m"(*p) : "a"(t), "r"(s) : "memory");
    return t;
}

static void* a_cas_p(volatile void* p, void* t, void* s)
{
    __asm__("lock ; cmpxchg %3, %1" : "=a"(t), "=m"(*(void* volatile*)p) : "a"(t), "r"(s) : "memory");
    return t;
}

static int a_swap(volatile int* p, int v)
{
    __asm__ __volatile__("xchg %0, %1" : "=r"(v), "=m"(*p) : "0"(v) : "memory");
    return v;
}

static int a_fetch_add(volatile int* p, int v)
{
    __asm__ __volatile__("lock ; xadd %0, %1" : "=r"(v), "=m"(*p) : "0"(v) : "memory");
    return v;
}

static void a_and(volatile int* p, int v)
{
    __asm__ __volatile__("lock ; and %1, %0" : "=m"(*p) : "r"(v) : "memory");
}

static void a_or(volatile int* p, int v)
{
    __asm__ __volatile__("lock ; or %1, %0" : "=m"(*p) : "r"(v) : "memory");
}

// musl spells the volatile qualifier without its trailing underscores here.
static void a_and_64(volatile atomic_u64* p, atomic_u64 v)
{
    __asm__ __volatile("lock ; and %1, %0" : "=m"(*p) : "r"(v) : "memory");
}

static void a_or_64(volatile atomic_u64* p, atomic_u64 v)
{
    __asm__ __volatile__("lock ; or %1, %0" : "=m"(*p) : "r"(v) : "memory");
}

// The only operand is both read and written in memory, which is the one shape
// where the same place arrives twice under two constraints.
static void a_inc(volatile int* p)
{
    __asm__ __volatile__("lock ; incl %0" : "=m"(*p) : "m"(*p) : "memory");
}

static void a_dec(volatile int* p)
{
    __asm__ __volatile__("lock ; decl %0" : "=m"(*p) : "m"(*p) : "memory");
}

// A literal immediate and the stack pointer as a memory base: the fence idiom.
static void a_store(volatile int* p, int x)
{
    __asm__ __volatile__("mov %1, %0 ; lock ; orl $0,(%%rsp)" : "=m"(*p) : "r"(x) : "memory");
}

static void a_barrier(void)
{
    __asm__ __volatile__("" : : : "memory");
}

static void a_spin(void)
{
    __asm__ __volatile__("pause" : : : "memory");
}

static int a_ctz_64(atomic_u64 x)
{
    __asm__("bsf %1,%0" : "=r"(x) : "r"(x));
    return (int)x;
}

static int a_clz_64(atomic_u64 x)
{
    __asm__("bsr %1,%0 ; xor $63,%0" : "=r"(x) : "r"(x));
    return (int)x;
}

// A segment override reading the thread pointer, which is how a libc finds its
// own thread-local storage. It is compiled but deliberately never called: a
// program linked by this driver has no thread area, so the read would fault on
// an address that exists only once a libc has installed one. Its encoding is
// checked in the driver test instead, by looking for the FS-prefixed load in
// the object; running it is what the musl harness does, after musl's own
// startup has set the segment base.
unsigned long buster_read_thread_pointer(void);
unsigned long buster_read_thread_pointer(void)
{
    unsigned long tp;
    __asm__("mov %%fs:0,%0" : "=r"(tp));
    return tp;
}

// A local written either side of the assembly, so an operand that escaped its
// declared registers and landed on this slot instead shows up as a wrong
// answer rather than as nothing.
static int failures;

static void check(long actual, long expected)
{
    if (actual != expected)
    {
        failures += 1;
    }
}

int main(void)
{
    volatile int cell = 5;
    volatile atomic_u64 wide = 0;
    void* volatile pointer = 0;
    int target = 0;
    int guard = 0x5a5a;

    check(a_cas((volatile int*)&cell, 5, 9), 5);
    check(cell, 9);
    check(a_cas((volatile int*)&cell, 5, 11), 9);
    check(cell, 9);
    check(a_swap((volatile int*)&cell, 20), 9);
    check(cell, 20);
    check(a_fetch_add((volatile int*)&cell, 3), 20);
    check(cell, 23);

    a_and((volatile int*)&cell, 7);
    check(cell, 23 & 7);
    a_or((volatile int*)&cell, 8);
    check(cell, (23 & 7) | 8);
    a_inc((volatile int*)&cell);
    check(cell, ((23 & 7) | 8) + 1);
    a_dec((volatile int*)&cell);
    check(cell, (23 & 7) | 8);
    a_store((volatile int*)&cell, 41);
    check(cell, 41);

    wide = 0xff00ff00ff00ff00ull;
    a_and_64(&wide, 0x0f0f0f0f0f0f0f0full);
    check((long)(wide == 0x0f000f000f000f00ull), 1);
    a_or_64(&wide, 0x00000000000000ffull);
    check((long)(wide == 0x0f000f000f000fffull), 1);

    check((long)(a_cas_p(&pointer, 0, &target) == 0), 1);
    check((long)(pointer == (void*)&target), 1);

    a_barrier();
    a_spin();

    check(a_ctz_64(40), 3);
    check(a_clz_64(40), 63 ^ 5);

    check(guard, 0x5a5a);
    return failures;
}

#else

int main(void)
{
    return 0;
}

#endif
