// The thread-pointer setter the musl harness supplies in place of musl's own.
//
// musl reads and writes the thread pointer in architecture assembly, and the
// Buster driver takes no assembly input, so the harness compiles the portable
// C sibling of each of those units instead and reports the assembly as an
// excluded component.  For __set_thread_area that substitution is not neutral:
// the portable sibling is written for architectures that have a
// SYS_set_thread_area system call, x86-64 has none, and musl's own x86-64
// assembly is the arch_prctl(ARCH_SET_FS) that installs the thread pointer.
// The portable path therefore returns -ENOSYS, __init_tp fails, and __init_tls
// crashes the process before main -- so no program with a libc runtime can
// start against either archive, Clang-built or Buster-built.
//
// This file is that one instruction sequence, written as C with an inline
// system call so that both compilers can build it, and it is compiled by each
// side with the compiler that side is testing.  It is placed ahead of the
// archive on the link line, which is what keeps musl's own portable
// __set_thread_area member from being pulled in beside it: an archive member
// is only extracted for a symbol that is still undefined.
//
// It defines nothing else.  Everything a test program resolves apart from this
// and musl's startup object comes out of the libc archive under test.

#if defined(__x86_64__) || defined(_M_X64)

// arch_prctl(ARCH_SET_FS, p), which is what musl's
// src/thread/x86_64/__set_thread_area.s does and all it does.
#define SYSTEM_CALL_ARCH_PRCTL 158
#define ARCH_SET_FS 0x1002

int __set_thread_area(void* pointer)
{
    unsigned long result;
    __asm__ __volatile__("syscall"
                         : "=a"(result)
                         : "a"((long)SYSTEM_CALL_ARCH_PRCTL), "D"((long)ARCH_SET_FS), "S"(pointer)
                         : "rcx", "r11", "memory");
    return (int)(long)result;
}

#else

// Every other architecture keeps musl's portable unit, which is correct
// wherever the system call it is written against exists.
int __set_thread_area(void* pointer);

#endif
