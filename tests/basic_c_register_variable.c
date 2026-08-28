// Local register variables and the fixed-register asm constraints a libc's
// system-call layer is written against.  The shapes here are musl's
// arch/x86_64/syscall_arch.h: arguments one to three ride 'D', 'S' and 'd',
// and arguments four to six have no constraint letter at all, so they are
// bound with `register long r10 __asm__("r10")` and passed as plain "r".
//
// The check has to prove the binding rather than the round trip, because a
// value that reaches the wrong register still reaches *a* register.  The
// kernel is the witness: it reads argument four from R10, five from R8 and six
// from R9, so a sequence that only succeeds when all three land where the ABI
// says they do is a real test of the binding.  A memfd is the file, which
// keeps the fixture free of the filesystem and of the SDK.
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))

typedef unsigned long u64_type;

static long syscall2(long number, long a1, long a2)
{
    unsigned long result;
    __asm__ __volatile__("syscall" : "=a"(result) : "a"(number), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return (long)result;
}

static long syscall4(long number, long a1, long a2, long a3, long a4)
{
    unsigned long result;
    register long r10 __asm__("r10") = a4;
    __asm__ __volatile__("syscall" : "=a"(result) : "a"(number), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory");
    return (long)result;
}

static long syscall6(long number, long a1, long a2, long a3, long a4, long a5, long a6)
{
    unsigned long result;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ __volatile__("syscall"
                         : "=a"(result)
                         : "a"(number), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
    return (long)result;
}

#define SYSTEM_CALL_CLOSE 3
#define SYSTEM_CALL_MMAP 9
#define SYSTEM_CALL_MUNMAP 11
#define SYSTEM_CALL_PWRITE64 18
#define SYSTEM_CALL_FTRUNCATE 77
#define SYSTEM_CALL_MEMFD_CREATE 319

#define PAGE_BYTES 4096
#define PROTECTION_READ 1
#define MAP_SHARED 1

// An assembler label renames the object it is attached to.  On a file-scope
// object it is the symbol name, which is the other half of the declarator
// shape the register binding rides on.
static u64_type marker_storage __asm__("buster_register_variable_marker") = 0x0123456789abcdefUL;

static int probe(void)
{
    static const char name[] = "buster-register-variable";
    long descriptor = syscall2(SYSTEM_CALL_MEMFD_CREATE, (long)name, 0);
    if (descriptor < 0)
    {
        return 1;
    }
    if (syscall2(SYSTEM_CALL_FTRUNCATE, descriptor, 2 * PAGE_BYTES) < 0)
    {
        return 2;
    }
    // Argument four is the file offset and rides R10: written to offset zero
    // instead, the mapping below reads back zero.
    if (syscall4(SYSTEM_CALL_PWRITE64, descriptor, (long)&marker_storage, (long)sizeof(marker_storage), PAGE_BYTES) != (long)sizeof(marker_storage))
    {
        return 3;
    }
    // Argument four is the mapping flags (R10), five the descriptor (R8) and
    // six the offset (R9).  Wrong flags fail outright, and a wrong descriptor
    // or offset maps the wrong bytes.
    long mapping = syscall6(SYSTEM_CALL_MMAP, 0, PAGE_BYTES, PROTECTION_READ, MAP_SHARED, descriptor, PAGE_BYTES);
    if (mapping < 0 && mapping > -4096)
    {
        return 4;
    }
    u64_type observed = *(u64_type const*)(unsigned long)mapping;
    int mismatch = observed != marker_storage;
    syscall2(SYSTEM_CALL_MUNMAP, mapping, PAGE_BYTES);
    syscall2(SYSTEM_CALL_CLOSE, descriptor, 0);
    return mismatch ? 5 : 0;
}

int main(void)
{
    return probe();
}

#else

int main(void)
{
    return 0;
}

#endif
