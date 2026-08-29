// musl's arch/x86_64/crt_arch.h, reduced to what it asks of the compiler and
// of the linker. This is the shape of every startup object a libc ships: the
// entry point is a module-level `__asm__` block, the block declares a symbol
// weak and hidden that nothing in the program defines, and it takes that
// symbol's address PC-relatively before calling into C. The reference has to
// resolve to zero -- musl's startup reads `_DYNAMIC` exactly this way, and
// zero is how a static program learns that it is static.
//
// Two things are checked and they are different. Compiling the file checks
// what the object records for the symbol: weak binding, hidden visibility,
// and a PC-relative relocation into the instruction that names it. Linking it
// with `-e global_asm_weak_start` and running it checks what the linker did
// with that record -- the C function returns a nonzero status for whichever
// of the two facts it was handed is wrong, and the assembly hands that status
// to the exit system call. Nothing else in this file is undefined, so the
// image is the static one a startup object belongs to; the hidden weak
// reference must not be what drags a dynamic loader into it.
// tests/basic_c_weak_undefined.c is the hosted counterpart, where a shared
// library is present and a weak reference it can define has to reach it.
//
// The AArch64 arm is absent on purpose. The textual assembler's AArch64
// vocabulary is the bootstrap control-flow set, which has no way to spell the
// ADRP/ADD page pair this needs; tests/basic_c_global_asm_entry.c carries the
// AArch64 coverage that does exist.

typedef unsigned long word;

int global_asm_weak_start_c(word* stack_on_entry, word* dynamic);

#if defined(__x86_64__) || defined(_M_X64)

__asm__(".text\n"
        ".global global_asm_weak_start\n"
        ".type global_asm_weak_start,%function\n"
        "global_asm_weak_start:\n"
        "	xor %rbp,%rbp\n"
        "	mov %rsp,%rdi\n"
        ".weak global_asm_weak_dynamic\n"
        ".hidden global_asm_weak_dynamic\n"
        "	lea global_asm_weak_dynamic(%rip),%rsi\n"
        "	andq $-16,%rsp\n"
        "	call global_asm_weak_start_c\n"
        "	movl %eax,%edi\n"
        "	movl $60,%eax\n"
        "	syscall\n"
        "	hlt\n");

#endif

int global_asm_weak_start_c(word* stack_on_entry, word* dynamic)
{
    int status = 0;
    // The kernel leaves the stack pointer holding argc; a null one would mean
    // the assembly never ran or clobbered it before the call.
    status = stack_on_entry ? status : 1;
    // The weak hidden symbol nothing defines. A linker that turned it into a
    // dynamic import hands back the address of a stub or of a copy slot here,
    // and a startup object reading it would conclude the program is dynamic.
    status = dynamic == 0 ? status : 2;
    return status;
}
