// musl's arch/x86_64/crt_arch.h, reduced to what it asks of the compiler.
// This is the shape of every startup object a libc ships: the entry point is
// a module-level `__asm__` block, the block declares a symbol weak and hidden
// that nothing in the program defines, and it takes that symbol's address
// PC-relatively before calling into C. In a static image the weak reference
// resolves to zero, which is how the startup code tells a static program from
// a dynamic one.
//
// This fixture is compiled, not linked: the reference to an undefined weak
// symbol is the point of it, and what the object records for that symbol --
// weak binding, hidden visibility, and a PC-relative relocation into the
// instruction that names it -- is what is checked.
//
// The AArch64 arm is absent on purpose. The textual assembler's AArch64
// vocabulary is the bootstrap control-flow set, which has no way to spell the
// ADRP/ADD page pair this needs; tests/basic_c_global_asm_entry.c carries the
// AArch64 coverage that does exist.

void global_asm_weak_start_c(long* stack);

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
        "	call global_asm_weak_start_c\n");

#endif

void global_asm_weak_start_c(long* stack)
{
    (void)stack;
}
