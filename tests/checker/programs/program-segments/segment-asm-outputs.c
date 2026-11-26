// Register output stores and read/write inputs keep their segments in rows, inline bodies and assembled helpers.
#define SS __attribute__((address_space(258)))
typedef int Vector __attribute__((vector_size(16)));
typedef long long Mmx __attribute__((vector_size(8)));

static _Thread_local int integer;
static _Thread_local long wide;
static _Thread_local short narrow;
static _Thread_local unsigned char byte;
static _Thread_local double floating;
static _Thread_local long double extended;
static _Thread_local Vector vector;
static _Thread_local Mmx mmx;
static _Thread_local _Atomic int atomic_flag;
static int evaluations;

static int __seg_fs* once(int __seg_fs* p)
{
    evaluations += 1;
    return p;
}

int tested(int n)
{
    char* thread = __builtin_thread_pointer();
    int __seg_fs* i = (int __seg_fs*)((char*)&integer - thread);
    long __seg_fs* l = (long __seg_fs*)((char*)&wide - thread);
    short __seg_fs* s = (short __seg_fs*)((char*)&narrow - thread);
    unsigned char __seg_fs* b = (unsigned char __seg_fs*)((char*)&byte - thread);
    double __seg_fs* d = (double __seg_fs*)((char*)&floating - thread);
    long double __seg_fs* q = (long double __seg_fs*)((char*)&extended - thread);
    Vector __seg_fs* v = (Vector __seg_fs*)((char*)&vector - thread);
    Mmx __seg_fs* m = (Mmx __seg_fs*)((char*)&mmx - thread);
    _Atomic int __seg_fs* a = (_Atomic int __seg_fs*)((char*)&atomic_flag - thread);
    const int seed = (unsigned)n & 127;
    integer = seed;
    wide = seed + 5;
    narrow = 13;
    byte = 17;
    floating = 1.25;
    extended = 1.5L;
    vector = (Vector){ 1, 2, 3, 4 };
    mmx = (Mmx){ 0x0101010101010101LL };
    evaluations = 0;
    unsigned long h = 0;

    __asm__("movl $23,%0" : "=r"(*i));
    __asm__("addl $7,%0" : "+r"(*once(i)));
    __asm__("addq $9,%0" : "+r"(*l));
    __asm__("addw $3,%0" : "+r"(*s));
    __asm__("addb $5,%0" : "+q"(*b));
    h += integer + wide + narrow + byte + evaluations;

    // An explicit clobber and a directive select the other statement representations.
    __asm__("addl $11,%0" : "+r"(*i) : : "r10");
    __asm__(".p2align 4\n\taddl $13,%0" : "+r"(*i));
    __asm__("" : "=r"(*i) : "0"(seed + 31));
    __asm__("" : "+r"(*l));
    h = h * 33 + integer + wide;

    __asm__("testl %1,%1" : "=@ccz"(*i) : "r"(seed & 1));
    h = h * 33 + integer;
    __asm__("testl %1,%1" : "=@ccnz"(*a) : "r"(seed & 1));
    h = h * 33 + atomic_flag;
    __asm__("addsd %1,%0" : "+x"(*d) : "x"(2.5));
    __asm__("fld1\n\tfaddp" : "+t"(*q) : : "st(1)");
    h = h * 33 + (unsigned long)(floating * 4) + (unsigned long)(extended * 4);

    Vector add = { 5, 7, 11, 13 };
    __asm__("paddd %1,%0" : "+x"(*v) : "x"(add));
    __asm__("addl $19,%0" : "+r"((*v)[2]));
    Mmx more = { 0x0202020202020202LL };
    __asm__("paddb %1,%0" : "+y"(*m) : "y"(more));
    __asm__("emms");
    h = h * 33 + vector[0] + vector[1] + vector[2] + vector[3] + (unsigned long)mmx[0];

    int ordinary = seed;
    __asm__("incl %0" : "+r"(*(int SS*)&ordinary));
    // A memory constraint passes its offset to the template, which clang deliberately leaves without FS.
    __asm__("incl %0" : "+m"(*(int __seg_fs*)&ordinary));
    h = h * 33 + ordinary;
    __asm__ goto ("addl $2,%0\n\tjmp %l[done]" : "+r"(*i) : : : done);
    integer += 1000;
done:
    return (int)(h + integer);
}
