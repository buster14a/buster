// A program whose entry point is defined by a module-level `__asm__` block,
// which is the shape a libc's startup object has: the kernel enters the
// assembly directly, the assembly establishes the ABI the C world expects and
// calls into it, and nothing before it has run.
//
// The block is written the way musl's arch/x86_64/crt_arch.h is, so what the
// emitter has to cover is what a crt actually asks for: a `.globl`/`.type`
// pair and a label for a symbol this file never declares in C, a stack
// alignment, a RIP-relative address of a C object, and a call to a C
// function. Each of the three is checked rather than merely executed --
// `global_asm_entry_c` receives the entry stack pointer, the address the
// assembly computed and the aligned stack pointer, and returns a status the
// assembly hands to the exit system call, so a mis-encoded `and` or a
// relocation applied to the wrong place fails the run instead of passing it.
//
// The file is freestanding: it has no include of any kind and calls nothing.
// Link it with `-e global_asm_entry`, which is what makes the assembly label
// the image's entry point rather than a function that startup code reaches.

typedef unsigned long word;

// The object the assembly takes the address of. It is not static because the
// assembly names it, and its value is what proves the RIP-relative
// displacement landed on it rather than near it.
word global_asm_entry_token = 0x5ad0c0ffee5ad0UL;

int global_asm_entry_c(word* stack_on_entry, word* token, word aligned_stack);

#if defined(__x86_64__) || defined(_M_X64)

__asm__(".text\n"
        ".globl global_asm_entry\n"
        ".type global_asm_entry, @function\n"
        "global_asm_entry:\n"
        "xorl %ebp, %ebp\n"
        "movq %rsp, %rdi\n"
        "andq $-16, %rsp\n"
        "movq %rsp, %rdx\n"
        "lea global_asm_entry_token(%rip), %rsi\n"
        "call global_asm_entry_c\n"
        "movl %eax, %edi\n"
        "movl $60, %eax\n"
        "syscall\n"
        "hlt\n");

#elif defined(__aarch64__) || defined(_M_ARM64)

// The AArch64 arm is compiled and linked but not run. The textual assembler's
// AArch64 vocabulary is the bootstrap control-flow set: it has the `bl` whose
// relocation this exercises, but neither the stack-aligning `and` nor the
// `svc` an entry point would need to leave, so the block calls into C and
// traps. What it covers is the call relocation on the other architecture.
__asm__(".text\n"
        ".globl global_asm_entry\n"
        ".type global_asm_entry, %function\n"
        "global_asm_entry:\n"
        "bl global_asm_entry_c\n"
        "brk #0\n");

#endif

int global_asm_entry_c(word* stack_on_entry, word* token, word aligned_stack)
{
    int status = 0;
    // The kernel leaves the stack pointer holding argc; a null one would mean
    // the assembly never ran or clobbered it before the call.
    status = stack_on_entry ? status : 1;
    // What the stack-aligning `and` produced. A wrong immediate, or an `and`
    // encoded against the wrong operand, shows up here and nowhere else.
    status = (aligned_stack & 15) == 0 ? status : 2;
    // The PC-relative displacement. `token` is null on the AArch64 arm, which
    // does not compute it.
    status = !token || *token == 0x5ad0c0ffee5ad0UL ? status : 3;
    return status;
}
