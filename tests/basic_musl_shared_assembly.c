// The architecture assembly a shared musl cannot leave undefined.
//
// A static link only pulls the archive members a program actually reaches, so
// the seven assembly-only translation units -- the ones whose .c file is empty
// because x86-64 supplies the implementation in assembly -- cost nothing until
// something calls one.  A shared object is the opposite: every object handed
// to `ld -shared` is in the result, every relocation in it is resolved when the
// library is loaded, and musl's own loader stops the process at exit 127 for
// the first name it cannot find.  `--no-undefined` is therefore on the shared
// link line, and this file is what makes that pass.
//
// It defines the six names the object set below leaves undefined, and nothing
// else.  Each one is written as C the two compilers both build, and it is
// compiled by each side with the compiler that side is testing, exactly the
// way tests/basic_musl_thread_pointer.c is.  What each substitution costs is
// written down beside it: none of them is musl's own answer, and none of them
// is reached by the freestanding probe or by a startup that never opens a
// shared library.
//
//   __syscall_cp_asm   a cancellable system call, without the cancellation
//                      window.  musl's assembly publishes the three labels
//                      below around the `syscall` instruction so a cancel
//                      signal that lands inside the window can be turned into
//                      a cancellation; this performs the call and returns.
//                      That is what musl itself does whenever cancellation is
//                      disabled, which it is for every thread this
//                      configuration can create -- `__clone` is architecture
//                      assembly too.
//   __cp_begin,        the window's bounds and its cancel target.  Only
//   __cp_end,          cancel_handler reads them, and only to compare the
//   __cp_cancel        interrupted program counter against the range; with no
//                      window to be inside, the comparison never matches.
//   setjmp, longjmp    musl's dynamic loader saves a jump buffer around
//                      dlopen so a failed load can unwind out of the middle of
//                      relocation processing.  Neither is expressible in C --
//                      saving the callee-saved registers and the return
//                      address needs the register names an inline template is
//                      not allowed to write -- so both trap.  Nothing at
//                      startup calls either: a program that never opens a
//                      library never reaches the path that does.

#if defined(__x86_64__) || defined(_M_X64)

// The six-argument Linux system call, spelled the way musl spells it in
// arch/x86_64/syscall_arch.h: the fourth argument travels in r10 rather than
// rcx because `syscall` overwrites rcx with the return address.
long __syscall_cp_asm(volatile void* cancel, long number, long argument1, long argument2, long argument3, long argument4, long argument5,
                      long argument6)
{
    (void)cancel;
    unsigned long result;
    register long r10 __asm__("r10") = argument4;
    register long r8 __asm__("r8") = argument5;
    register long r9 __asm__("r9") = argument6;
    __asm__ __volatile__("syscall"
                         : "=a"(result)
                         : "a"(number), "D"(argument1), "S"(argument2), "d"(argument3), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
    return (long)result;
}

// musl declares these as `const char[1]` and only ever takes their addresses.
const char __cp_begin[1] = {0};
const char __cp_end[1] = {0};
const char __cp_cancel[1] = {0};

int setjmp(void* environment)
{
    (void)environment;
    __builtin_trap();
}

void longjmp(void* environment, int value)
{
    (void)environment;
    (void)value;
    __builtin_trap();
}

#else

// Every other architecture keeps whatever musl's own build supplies; this file
// is the x86-64 shared link's substitution and nothing more.
long __syscall_cp_asm(volatile void* cancel, long number, long argument1, long argument2, long argument3, long argument4, long argument5,
                      long argument6);

#endif
