// Inline assembly that names a symbol, which is the one thing the templates in
// tests/basic_c_atomic_asm.c never do. Two shapes from musl's own
// arch/x86_64/reloc.h are here because they are what a libc's startup and its
// dynamic loader are made of, and neither could be compiled before the inline
// path learned to report a relocation.
//
// The first is GETFUNCSYM: a `.hidden` directive on the symbol followed by
// `lea sym(%rip),%0` into an output operand. That is how a program that has
// not been relocated yet takes the address of a function -- there is no
// absolute address to load, so the reference has to be PC-relative, and the
// directive is what keeps the linker from routing it through a PLT. It appears
// here against a function, against an object, and against a weak symbol
// nothing defines, whose reference is owed zero: that is exactly what musl's
// startup reads to learn that it is running in a static image.
//
// The second is CRTJMP: `mov %1,%%rsp ; jmp *%0`, which moves onto a stack the
// caller chose and leaves through an address it computed. musl's loader
// enters the program it has just relocated this way, and a thread jumps onto a
// borrowed stack this way before unmapping the one it was running on. Nothing
// after it in this function is reached, which is what licenses the template to
// write the stack pointer at all, so the jumped-to code has to end the process
// itself -- it exits with the failure count the checks above accumulated.
//
// Every answer is checked rather than only assembled: a PC-relative reference
// that lands one instruction off still assembles, and hands back an address
// that is merely wrong.

#if defined(__x86_64__) || defined(_M_X64)

typedef unsigned long word;

static int failures;

static void check(long actual, long expected)
{
    if (actual != expected)
    {
        failures += 1;
    }
}

__attribute__((__visibility__("hidden"))) int inline_asm_symbol_answer(int value);
__attribute__((__visibility__("hidden"))) int inline_asm_symbol_answer(int value)
{
    return value * 3 + 1;
}

__attribute__((__visibility__("hidden"))) int inline_asm_symbol_cell = 0x5a5a;

typedef int (*inline_asm_symbol_function)(int);

// musl's GETFUNCSYM verbatim, with its symbol spelled rather than pasted in.
static inline_asm_symbol_function inline_asm_symbol_load_function(void)
{
    inline_asm_symbol_function target;
    __asm__(".hidden inline_asm_symbol_answer\n"
            "	lea inline_asm_symbol_answer(%%rip),%0\n"
            : "=r"(target)
            :
            : "memory");
    return target;
}

// The same reference against an object rather than a function, and without a
// directive: the relocation is the whole content of this one.
static int* inline_asm_symbol_load_cell(void)
{
    int* cell;
    __asm__("lea inline_asm_symbol_cell(%%rip),%0" : "=r"(cell) : : "memory");
    return cell;
}

// A weak hidden symbol nothing in the program defines. A static link owes the
// reference zero; a link that turned it into a dynamic import would hand back
// the address of a stub instead, which is how a startup object would conclude
// that a static program is dynamic.
static word* inline_asm_symbol_load_absent(void)
{
    word* absent;
    __asm__(".weak inline_asm_symbol_absent\n"
            ".hidden inline_asm_symbol_absent\n"
            "	lea inline_asm_symbol_absent(%%rip),%0\n"
            : "=r"(absent)
            :
            : "memory");
    return absent;
}

// The stack CRTJMP hands over. musl gives its own 256 bytes; this one is wider
// because the frame the canonical emitter builds for the function below is,
// and the point of the fixture is the hand-off rather than the size.
static char inline_asm_symbol_stack[8192];
static int inline_asm_symbol_status;

// Where CRTJMP lands. It is entered by a jump rather than a call, so there is
// no return address under it and it must not return; it ends the process with
// the status the checks produced.
__attribute__((__visibility__("hidden"))) void inline_asm_symbol_hand_off_target(void);
__attribute__((__visibility__("hidden"))) void inline_asm_symbol_hand_off_target(void)
{
    long status = inline_asm_symbol_status;
    long number = 231;
    __asm__ __volatile__("syscall" : : "a"(number), "D"(status) : "memory");
    for (;;)
    {
    }
}

// musl's CRTJMP verbatim. It is compiled everywhere this file is and called
// only where the system call above exists, which is why it has external
// linkage: a static function nothing calls is one a compiler may never emit,
// and emitting it is half of what this fixture is for.
void inline_asm_symbol_hand_off(void);
void inline_asm_symbol_hand_off(void)
{
    char* stack = inline_asm_symbol_stack + sizeof inline_asm_symbol_stack;
    void (*target)(void) = inline_asm_symbol_hand_off_target;
    stack -= (word)stack % 16;
    __asm__ __volatile__("mov %1,%%rsp ; jmp *%0" : : "r"(target), "r"(stack) : "memory");
}

int main(void)
{
    int guard = 0x2b2b;
    inline_asm_symbol_function answer = inline_asm_symbol_load_function();
    int* cell = inline_asm_symbol_load_cell();
    word* absent = inline_asm_symbol_load_absent();

    check(answer(13), 40);
    check((long)(word)cell, (long)(word)&inline_asm_symbol_cell);
    check(*cell, 0x5a5a);
    check((long)(word)absent, 0);
    check(guard, 0x2b2b);

    inline_asm_symbol_status = failures;
#if defined(__linux__)
    inline_asm_symbol_hand_off();
#endif
    return failures;
}

#else

int main(void)
{
    return 0;
}

#endif
