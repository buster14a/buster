// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Raw link-foreign command tests. foreign_test.c includes this after the dynamic test helpers. Every link starts a
// fresh /proc/self/exe; only the guarded selection-kernel checks call driver internals. Original assembly supplies
// its own entry and syscall exit, except the main-only fixture that checks explicitly supplied system CRT inputs.
// The output starts as a sentinel, so both successful replacement and failure preservation are observable. Archive
// winners, scoped input options, GC, string pooling and dynamic dependencies are checked in the resulting programs.

#define BUSTER_FOREIGN_DRIVER_TEST_ARGUMENTS 64

typedef enum ForeignDriverTestFixture
{
    FOREIGN_DRIVER_TEST_START,
    FOREIGN_DRIVER_TEST_ENTRY,
    FOREIGN_DRIVER_TEST_DEAD,
    FOREIGN_DRIVER_TEST_CALL,
    FOREIGN_DRIVER_TEST_SCOPED_CALL,
    FOREIGN_DRIVER_TEST_FIRST,
    FOREIGN_DRIVER_TEST_SECOND,
    FOREIGN_DRIVER_TEST_POISON,
    FOREIGN_DRIVER_TEST_UNUSED,
    FOREIGN_DRIVER_TEST_STRINGS,
    FOREIGN_DRIVER_TEST_STRINGS_PC,
    FOREIGN_DRIVER_TEST_MAIN,
    FOREIGN_DRIVER_TEST_OPTION_NAMES,
    FOREIGN_DRIVER_TEST_ABSOLUTE,
    FOREIGN_DRIVER_TEST_IFUNC,
    FOREIGN_DRIVER_TEST_IFUNC_INERT,
    FOREIGN_DRIVER_TEST_IFUNC_INERT_DYNAMIC,
    FOREIGN_DRIVER_TEST_IFUNC_APPLIED,
    FOREIGN_DRIVER_TEST_IFUNC_PIE_APPLIED,
    FOREIGN_DRIVER_TEST_IFUNC_MAIN,
    FOREIGN_DRIVER_TEST_INERT_START,
    FOREIGN_DRIVER_TEST_INERT_START_PIE,
    FOREIGN_DRIVER_TEST_OWN_LIBC_START,
    FOREIGN_DRIVER_TEST_OWN_RELOCATOR,
    FOREIGN_DRIVER_TEST_LTO,
    FOREIGN_DRIVER_TEST_LTO_LLVM,
    FOREIGN_DRIVER_TEST_LTO_FUNCTION,
    FOREIGN_DRIVER_TEST_LTO_CALL,
    FOREIGN_DRIVER_TEST_FIXTURES,
} ForeignDriverTestFixture;

typedef struct ForeignDriverTestSource ForeignDriverTestSource;
struct ForeignDriverTestSource
{
    const char* name;
    const char* text;
};

static const ForeignDriverTestSource foreign_driver_test_sources[FOREIGN_DRIVER_TEST_FIXTURES] = {
    [FOREIGN_DRIVER_TEST_START] = {
        .name = "fdriver_start",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                ".cfi_startproc\nmov $42,%edi\nmov $60,%eax\nsyscall\n.cfi_endproc\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_ENTRY] = {
        .name = "fdriver_entry",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "mov $19,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .text.alternate,\"ax\",@progbits\n.globl driver_alternate\n.type driver_alternate,@function\ndriver_alternate:\n"
                "mov $42,%edi\nmov $60,%eax\nsyscall\n.size driver_alternate,.-driver_alternate\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_DEAD] = {
        .name = "fdriver_dead",
        .text = ".section .text.dead,\"ax\",@progbits\n.globl driver_dead\n.type driver_dead,@function\ndriver_dead:\n"
                ".cfi_startproc\ncall driver_missing\nret\n.cfi_endproc\n.size driver_dead,.-driver_dead\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_CALL] = {
        .name = "fdriver_call",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                ".cfi_startproc\ncall driver_pick@PLT\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.cfi_endproc\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    // Both calls must resolve: pick comes from the scoped archive, unused from its DSO with no archive substitute.
    [FOREIGN_DRIVER_TEST_SCOPED_CALL] = {
        .name = "fdriver_scoped_call",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "call driver_pick@PLT\nmov %eax,%ebx\ncall driver_unused@PLT\nadd %ebx,%eax\n"
                "mov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_FIRST] = {
        .name = "fdriver_first",
        .text = ".text\n.globl driver_pick\n.type driver_pick,@function\ndriver_pick:\nmov $42,%eax\nret\n.size driver_pick,.-driver_pick\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_SECOND] = {
        .name = "fdriver_second",
        .text = ".text\n.globl driver_pick\n.type driver_pick,@function\ndriver_pick:\nmov $19,%eax\nret\n.size driver_pick,.-driver_pick\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_POISON] = {
        .name = "fdriver_poison",
        .text = ".text\n.globl driver_poison\n.type driver_poison,@function\ndriver_poison:\ncall driver_missing\nret\n.size driver_poison,.-driver_poison\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_UNUSED] = {
        .name = "fdriver_unused",
        .text = ".text\n.globl driver_unused\n.type driver_unused,@function\ndriver_unused:\nmov $7,%eax\nret\n.size driver_unused,.-driver_unused\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_STRINGS] = {
        .name = "fdriver_strings",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "mov $1,%edi\nmov driver_string_a@GOTPCREL(%rip),%rax\nmov driver_string_b@GOTPCREL(%rip),%rcx\n"
                "lea driver_string_pointers(%rip),%r8\ncmp (%r8),%rax\njne 1f\ncmp 8(%r8),%rcx\njne 1f\n"
                "cmpb $100,(%rax)\njne 1f\ncmpb $100,(%rcx)\njne 1f\nmov $42,%edi\n1: mov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .rodata.driver,\"aMS\",@progbits,1\n.globl driver_string_a\n.type driver_string_a,@object\ndriver_string_a:\n"
                ".asciz \"driver duplicate string\"\n.size driver_string_a,.-driver_string_a\n"
                ".globl driver_string_b\n.type driver_string_b,@object\ndriver_string_b:\n"
                ".asciz \"driver duplicate string\"\n.size driver_string_b,.-driver_string_b\n"
                ".data\n.balign 8\ndriver_string_pointers:\n.quad driver_string_a,driver_string_b\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_STRINGS_PC] = {
        .name = "fdriver_strings_pc",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "mov $1,%edi\nlea driver_string_a(%rip),%rax\nlea driver_string_b(%rip),%rcx\n"
                "lea driver_string_pointers(%rip),%r8\ncmp (%r8),%rax\njne 1f\ncmp 8(%r8),%rcx\njne 1f\n"
                "cmpb $100,(%rax)\njne 1f\ncmpb $100,(%rcx)\njne 1f\nmov $42,%edi\n1: mov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .rodata.driver,\"aMS\",@progbits,1\n.globl driver_string_a\n.type driver_string_a,@object\ndriver_string_a:\n"
                ".asciz \"driver duplicate string\"\n.size driver_string_a,.-driver_string_a\n"
                ".globl driver_string_b\n.type driver_string_b,@object\ndriver_string_b:\n"
                ".asciz \"driver duplicate string\"\n.size driver_string_b,.-driver_string_b\n"
                ".data\n.balign 8\ndriver_string_pointers:\n.quad driver_string_a,driver_string_b\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_MAIN] = {
        .name = "fdriver_main",
        .text = ".text\n.globl main\n.type main,@function\nmain:\nmov $42,%eax\nret\n.size main,.-main\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_OPTION_NAMES] = {
        .name = "fdriver_option_names",
        .text = ".section .text.option_a,\"ax\",@progbits\n.globl \"--gc-sections\"\n.type \"--gc-sections\",@function\n\"--gc-sections\":\nmov $0x13ab47e1,%eax\nret\n"
                ".section .text.option_b,\"ax\",@progbits\n.globl \"--scalar\"\n.type \"--scalar\",@function\n\"--scalar\":\nmov $0x35cd68f2,%eax\nret\n"
                ".section .text.option_c,\"ax\",@progbits\n.globl \"-s\"\n.type \"-s\",@function\n\"-s\":\nmov $11,%eax\nret\n"
                ".section .text.option_d,\"ax\",@progbits\n.globl \"--strip-all\"\n.type \"--strip-all\",@function\n\"--strip-all\":\nmov $13,%eax\nret\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_ABSOLUTE] = {
        .name = "fdriver_absolute",
        .text = ".globl driver_pick\n.set driver_pick,0x7fff000000000000\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // A program without libc whose _start calls an IFUNC, which returns 42, through its stub: nothing applies the
    // stub's IRELATIVE relocation, and a static link refuses it (the next five too, but where they say otherwise).
    [FOREIGN_DRIVER_TEST_IFUNC] = {
        .name = "fdriver_ifunc",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "call driver_ifunc\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".text\n.type driver_ifunc_one,@function\ndriver_ifunc_one:\nmov $42,%eax\nret\n.size driver_ifunc_one,.-driver_ifunc_one\n"
                ".globl driver_ifunc\n.type driver_ifunc,@gnu_indirect_function\ndriver_ifunc:\nlea driver_ifunc_one(%rip),%rax\nret\n"
                ".size driver_ifunc,.-driver_ifunc\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // The same with a pointer to __rela_iplt_start that nothing reads (#405's review): the name's mention isn't startup.
    [FOREIGN_DRIVER_TEST_IFUNC_INERT] = {
        .name = "fdriver_ifunc_inert",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "call driver_ifunc\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".data\n.quad __rela_iplt_start\n"
                ".text\n.type driver_ifunc_one,@function\ndriver_ifunc_one:\nmov $42,%eax\nret\n.size driver_ifunc_one,.-driver_ifunc_one\n"
                ".globl driver_ifunc\n.type driver_ifunc,@gnu_indirect_function\ndriver_ifunc:\nlea driver_ifunc_one(%rip),%rax\nret\n"
                ".size driver_ifunc,.-driver_ifunc\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // ... and with one to _DYNAMIC, for a static PIE.
    [FOREIGN_DRIVER_TEST_IFUNC_INERT_DYNAMIC] = {
        .name = "fdriver_ifunc_inert_dynamic",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "call driver_ifunc\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".data\n.quad _DYNAMIC\n"
                ".text\n.type driver_ifunc_one,@function\ndriver_ifunc_one:\nmov $42,%eax\nret\n.size driver_ifunc_one,.-driver_ifunc_one\n"
                ".globl driver_ifunc\n.type driver_ifunc,@gnu_indirect_function\ndriver_ifunc:\nlea driver_ifunc_one(%rip),%rax\nret\n"
                ".size driver_ifunc,.-driver_ifunc\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // The same, whose _start first applies the IRELATIVE relocations between __rela_iplt_start and __rela_iplt_end
    // (each slot at its r_offset gets what the resolver at its r_addend returns), as libc.a's startup applies them: a
    // static executable takes it with --startup-applies-irelative, and refuses it without.
    [FOREIGN_DRIVER_TEST_IFUNC_APPLIED] = {
        .name = "fdriver_ifunc_applied",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "lea __rela_iplt_start(%rip),%rbx\nlea __rela_iplt_end(%rip),%r12\n1: cmp %r12,%rbx\njae 2f\n"
                "mov 16(%rbx),%rax\ncall *%rax\nmov (%rbx),%rcx\nmov %rax,(%rcx)\nadd $24,%rbx\njmp 1b\n"
                "2: call driver_ifunc\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".text\n.type driver_ifunc_one,@function\ndriver_ifunc_one:\nmov $42,%eax\nret\n.size driver_ifunc_one,.-driver_ifunc_one\n"
                ".globl driver_ifunc\n.type driver_ifunc,@gnu_indirect_function\ndriver_ifunc:\nlea driver_ifunc_one(%rip),%rax\nret\n"
                ".size driver_ifunc,.-driver_ifunc\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // A static PIE's: its _start finds .rela.dyn through _DYNAMIC (DT_RELA and DT_RELASZ) and applies each RELATIVE
    // relocation (the place gets the base plus the addend) and each IRELATIVE one (what the resolver at the base plus
    // the addend returns), the base being the ELF header's address, as libc.a's _dl_relocate_static_pie does; a static
    // PIE takes it with --startup-applies-irelative.
    [FOREIGN_DRIVER_TEST_IFUNC_PIE_APPLIED] = {
        .name = "fdriver_ifunc_pie_applied",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "lea __ehdr_start(%rip),%r15\nlea _DYNAMIC(%rip),%rbx\nxor %r12d,%r12d\nxor %r13d,%r13d\n"
                "1: mov (%rbx),%rax\ntest %rax,%rax\nje 3f\ncmp $7,%rax\njne 2f\nmov 8(%rbx),%r12\n"
                "2: cmp $8,%rax\njne 4f\nmov 8(%rbx),%r13\n4: add $16,%rbx\njmp 1b\n"
                "3: add %r15,%r12\nlea (%r12,%r13),%r13\n"
                "5: cmp %r13,%r12\njae 7f\nmov 8(%r12),%eax\nmov (%r12),%rdi\nadd %r15,%rdi\nmov 16(%r12),%rsi\nadd %r15,%rsi\n"
                "cmp $8,%eax\njne 6f\nmov %rsi,(%rdi)\njmp 8f\n"
                "6: cmp $37,%eax\njne 8f\nmov %rdi,%r14\ncall *%rsi\nmov %rax,(%r14)\n"
                "8: add $24,%r12\njmp 5b\n"
                "7: call driver_ifunc\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".text\n.type driver_ifunc_one,@function\ndriver_ifunc_one:\nmov $42,%eax\nret\n.size driver_ifunc_one,.-driver_ifunc_one\n"
                ".globl driver_ifunc\n.type driver_ifunc,@gnu_indirect_function\ndriver_ifunc:\nlea driver_ifunc_one(%rip),%rax\nret\n"
                ".size driver_ifunc,.-driver_ifunc\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // main, which glibc's startup calls, returning what an IFUNC returns (42) through its stub
    // (foreign_driver_test_startup).
    [FOREIGN_DRIVER_TEST_IFUNC_MAIN] = {
        .name = "fdriver_ifunc_main",
        .text = ".text\n.globl main\n.type main,@function\nmain:\nsub $8,%rsp\ncall driver_ifunc\nadd $8,%rsp\nret\n.size main,.-main\n"
                ".text\n.type driver_ifunc_one,@function\ndriver_ifunc_one:\nmov $42,%eax\nret\n.size driver_ifunc_one,.-driver_ifunc_one\n"
                ".globl driver_ifunc\n.type driver_ifunc,@gnu_indirect_function\ndriver_ifunc:\nlea driver_ifunc_one(%rip),%rax\nret\n"
                ".size driver_ifunc,.-driver_ifunc\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // A _start of its own in crt1.o's place, which calls an IFUNC with nothing applying its relocation and mentions
    // __libc_start_main only in data (#405's second review), with crt1.o's empty _dl_relocate_static_pie and
    // _dl_relocate_static_pie_ifunc: libc.a's __libc_start_main joins and refers to __rela_iplt_start, but never runs.
    [FOREIGN_DRIVER_TEST_INERT_START] = {
        .name = "fdriver_inert_start",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "call driver_ifunc\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".globl _dl_relocate_static_pie\n.type _dl_relocate_static_pie,@function\n_dl_relocate_static_pie:\nret\n"
                ".size _dl_relocate_static_pie,.-_dl_relocate_static_pie\n"
                ".globl _dl_relocate_static_pie_ifunc\n.type _dl_relocate_static_pie_ifunc,@function\n_dl_relocate_static_pie_ifunc:\nret\n"
                ".size _dl_relocate_static_pie_ifunc,.-_dl_relocate_static_pie_ifunc\n"
                ".data\n.balign 8\n.quad __libc_start_main\n"
                ".text\n.type driver_ifunc_one,@function\ndriver_ifunc_one:\nmov $42,%eax\nret\n.size driver_ifunc_one,.-driver_ifunc_one\n"
                ".globl driver_ifunc\n.type driver_ifunc,@gnu_indirect_function\ndriver_ifunc:\nlea driver_ifunc_one(%rip),%rax\nret\n"
                ".size driver_ifunc,.-driver_ifunc\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // ... and in rcrt1.o's place, without them, as rcrt1.o has none: libc.a's _dl_relocate_static_pie joins too, and
    // refers to _DYNAMIC.
    [FOREIGN_DRIVER_TEST_INERT_START_PIE] = {
        .name = "fdriver_inert_start_pie",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "call driver_ifunc\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".data\n.balign 8\n.quad __libc_start_main\n"
                ".text\n.type driver_ifunc_one,@function\ndriver_ifunc_one:\nmov $42,%eax\nret\n.size driver_ifunc_one,.-driver_ifunc_one\n"
                ".globl driver_ifunc\n.type driver_ifunc,@gnu_indirect_function\ndriver_ifunc:\nlea driver_ifunc_one(%rip),%rax\nret\n"
                ".size driver_ifunc,.-driver_ifunc\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // A __libc_start_main of its own, which calls main and exits with its result, applying nothing, and mentions the
    // names libc.a's refers to.
    [FOREIGN_DRIVER_TEST_OWN_LIBC_START] = {
        .name = "fdriver_own_libc_start",
        .text = ".text\n.globl __libc_start_main\n.type __libc_start_main,@function\n__libc_start_main:\n"
                "call *%rdi\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size __libc_start_main,.-__libc_start_main\n"
                ".weak __rela_iplt_start\n.weak __rela_iplt_end\n"
                ".data\n.balign 8\n.quad __rela_iplt_start,__rela_iplt_end,_dl_relocate_static_pie\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    // A static PIE's _dl_relocate_static_pie and _dl_relocate_static_pie_ifunc of its own, which mention _DYNAMIC and
    // apply nothing: with them, a static PIE needs nothing of libc.a's (whose libc-start.o, which libc.a's relocator
    // needs, defines __libc_start_main too).
    [FOREIGN_DRIVER_TEST_OWN_RELOCATOR] = {
        .name = "fdriver_own_relocator",
        .text = ".text\n.globl _dl_relocate_static_pie\n.type _dl_relocate_static_pie,@function\n_dl_relocate_static_pie:\nret\n"
                ".size _dl_relocate_static_pie,.-_dl_relocate_static_pie\n"
                ".globl _dl_relocate_static_pie_ifunc\n.type _dl_relocate_static_pie_ifunc,@function\n_dl_relocate_static_pie_ifunc:\nret\n"
                ".size _dl_relocate_static_pie_ifunc,.-_dl_relocate_static_pie_ifunc\n"
                ".data\n.balign 8\n.quad _DYNAMIC\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    // GCC's LTO object: a .gnu.lto_* section that only its plugin can link, beside ordinary code.
    [FOREIGN_DRIVER_TEST_LTO] = {
        .name = "fdriver_lto",
        .text = ".section .gnu.lto_.opts,\"\",@progbits\n.byte 1\n"
                ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "mov $42,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    // clang's fat LTO object: .llvm.lto holds the bitcode.
    [FOREIGN_DRIVER_TEST_LTO_LLVM] = {
        .name = "fdriver_lto_llvm",
        .text = ".section .llvm.lto,\"\",@progbits\n.byte 1\n"
                ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "mov $42,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    // A .gnu.lto_* member of an archive, which defines the function the call below needs.
    [FOREIGN_DRIVER_TEST_LTO_FUNCTION] = {
        .name = "fdriver_lto_function",
        .text = ".section .gnu.lto_.opts,\"\",@progbits\n.byte 1\n"
                ".text\n.globl driver_lto\n.type driver_lto,@function\ndriver_lto:\nmov $42,%eax\nret\n.size driver_lto,.-driver_lto\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    // A caller that makes the archive load that member.
    [FOREIGN_DRIVER_TEST_LTO_CALL] = {
        .name = "fdriver_lto_call",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "call driver_lto\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
};

typedef struct ForeignDriverTestFiles ForeignDriverTestFiles;
struct ForeignDriverTestFiles
{
    ForeignTestPath objects[FOREIGN_DRIVER_TEST_FIXTURES];
    char* first;
    char* second;
    char* poison;
    char* entry;
    char* alternate;
    char* options;
    char* choice;
    ForeignTestPath needed;
    ForeignTestPath unused;
    ForeignTestPath choice_library;
    // An object of _start and twice BUSTER_FOREIGN_DRIVER_LANE_BYTES of data, whose bytes pay for lanes by default (#594).
    ForeignTestPath large;
};

static bool foreign_driver_test_compile(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena)
{
    bool ready = true;
    for (uint32_t f = 0; f < FOREIGN_DRIVER_TEST_FIXTURES; f += 1)
    {
        const ForeignDriverTestSource* fixture = &foreign_driver_test_sources[f];
        ForeignTestPath source;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(source, sizeof(source), "%s/%s.S", directory, fixture->name);
        snprintf(files->objects[f], sizeof(files->objects[f]), "%s/%s.o", directory, fixture->name);
        snprintf(output, sizeof(output), "%s/%s_compile.out", directory, fixture->name);
        snprintf(errors, sizeof(errors), "%s/%s_compile.err", directory, fixture->name);
        char* arguments[] = {
            (char*)"cc",
            (char*)"-c",
            (char*)"-fPIC",
            source,
            (char*)"-o",
            files->objects[f],
            0,
        };
        const bool made = foreign_test_write(source, fixture->text, strlen(fixture->text)) && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, made, "assemble original driver fixture", fixture->name);
        ready = ready && made;
    }
    if (ready)
    {
        const char* first[] = {
            "fdriver_first.o",
        };
        const char* second[] = {
            "fdriver_second.o",
        };
        const char* poison[] = {
            "fdriver_poison.o",
        };
        const char* entry[] = {
            "fdriver_start.o",
        };
        const char* alternate[] = {
            "fdriver_entry.o",
        };
        const char* options[] = {
            "fdriver_option_names.o",
        };
        files->first = foreign_test_archive(directory, "libfdriver_pick.a", first, BUSTER_ARRAY_LENGTH(first), false, arena);
        files->second = foreign_test_archive(directory, "libfdriver_second.a", second, BUSTER_ARRAY_LENGTH(second), false, arena);
        files->poison = foreign_test_archive(directory, "libfdriver_poison.a", poison, BUSTER_ARRAY_LENGTH(poison), false, arena);
        files->entry = foreign_test_archive(directory, "libfdriver_entry.a", entry, BUSTER_ARRAY_LENGTH(entry), false, arena);
        files->alternate = foreign_test_archive(directory, "libfdriver_alternate.a", alternate, BUSTER_ARRAY_LENGTH(alternate), false, arena);
        files->options = foreign_test_archive(directory, "libfdriver_options.a", options, BUSTER_ARRAY_LENGTH(options), false, arena);
        files->choice = foreign_test_archive(directory, "libfdriver_choice.a", second, BUSTER_ARRAY_LENGTH(second), false, arena);
        ready = files->first != 0 && files->second != 0 && files->poison != 0 && files->entry != 0 && files->alternate != 0 && files->options != 0 && files->choice != 0;
        foreign_test_expect(tally, ready, "write original driver archives", 0);
    }
    for (uint32_t d = 0; ready && d < 3; d += 1)
    {
        ForeignTestPath output;
        ForeignTestPath errors;
        char* library = d == 0 ? files->needed : d == 1 ? files->unused : files->choice_library;
        const char* basename = d == 0 ? "pick" : d == 1 ? "unused" : "choice";
        char soname[64];
        snprintf(library, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/libfdriver_%s.so", directory, basename);
        snprintf(soname, sizeof(soname), "-Wl,-soname,libfdriver_%s.so", basename);
        snprintf(output, sizeof(output), "%s/fdriver_dso_%u.out", directory, d);
        snprintf(errors, sizeof(errors), "%s/fdriver_dso_%u.err", directory, d);
        char* arguments[] = {
            (char*)"cc",
            (char*)"-shared",
            (char*)"-nostdlib",
            soname,
            files->objects[d == 1 ? FOREIGN_DRIVER_TEST_UNUSED : FOREIGN_DRIVER_TEST_FIRST],
            (char*)"-o",
            library,
            0,
        };
        ready = object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, ready, "link original driver DSO", library);
    }
    return ready;
}

static bool foreign_driver_test_same(String8 left, String8 right)
{
    return left.pointer != 0 && right.pointer != 0 && left.length == right.length && memcmp(left.pointer, right.pointer, left.length) == 0;
}

// Execute the output path directly: env would parse a path containing '=' as another assignment. Borrow every
// inherited entry except LD_LIBRARY_PATH, replacing all its occurrences without changing the process environment.
static int foreign_driver_test_run(const char* directory, const char* executable, const char* output, const char* errors, Arena* arena)
{
    const uint64_t saved = arena->length;
    char library_path[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    const int length = snprintf(library_path, sizeof(library_path), "LD_LIBRARY_PATH=%s", directory);
    uint64_t count = 0;
    for (char** entry = environ; entry != 0 && *entry != 0; entry += 1)
    {
        count += 1;
    }
    char** environment = length >= 0 && (uint64_t)length < sizeof(library_path) ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, count + 2) : 0;
    int result = -1;
    if (environment != 0)
    {
        uint64_t used = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            if (strncmp(environ[i], "LD_LIBRARY_PATH=", sizeof("LD_LIBRARY_PATH=") - 1) != 0)
            {
                environment[used] = environ[i];
                used += 1;
            }
        }
        environment[used] = library_path;
        environment[used + 1] = 0;
        char* arguments[] = { (char*)executable, 0 };
        result = object_test_run_environment(arguments, output, errors, environment);
    }
    arena_pop_to(arena, saved);
    return result;
}

// A failed invocation must exit normally, issue a diagnostic, and preserve the old output byte for byte. A signal,
// timeout or spawn failure is reported as -1 by object_test_run and must never pass as an expected refusal.
// output_option uses the literal placeholders -oFILE and --output=FILE for the two joined spellings.
static String8 foreign_driver_test_invoke(ForeignTestTally* tally, const char* directory, const char* name, const char* output_option, char* const* inputs, uint32_t count, bool success, int exit_status, Arena* arena)
{
    static const char sentinel[] = "existing driver output\000must survive failed link\377";
    ForeignTestPath executable;
    ForeignTestPath output;
    ForeignTestPath errors;
    char output_argument[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
    snprintf(executable, sizeof(executable), "%s/fdriver_%s", directory, name);
    snprintf(output, sizeof(output), "%s/fdriver_%s.out", directory, name);
    snprintf(errors, sizeof(errors), "%s/fdriver_%s.err", directory, name);
    char* arguments[BUSTER_FOREIGN_DRIVER_TEST_ARGUMENTS];
    const bool prepared = count + 5 <= BUSTER_ARRAY_LENGTH(arguments) && foreign_test_write(executable, sentinel, sizeof(sentinel));
    foreign_test_expect(tally, prepared, "prepare driver output sentinel", name);
    String8 result = { 0 };
    if (prepared)
    {
        arguments[0] = (char*)"/proc/self/exe";
        arguments[1] = (char*)"link-foreign";
        uint32_t first = 2;
        if (strcmp(output_option, "-oFILE") == 0 || strcmp(output_option, "--output=FILE") == 0)
        {
            snprintf(output_argument, sizeof(output_argument), "%s%s", output_option[1] == 'o' ? "-o" : "--output=", executable);
            arguments[first++] = output_argument;
        }
        else
        {
            arguments[first++] = (char*)output_option;
            arguments[first++] = executable;
        }
        memcpy(arguments + first, inputs, count * sizeof(char*));
        arguments[count + first] = 0;
        const int status = object_test_run(arguments, output, errors);
        foreign_test_expect(tally, success ? status == 0 : status > 0, success ? "driver links fixture" : "driver refuses fixture normally", name);
        const String8 bytes = foreign_test_read(arena, executable);
        if (success && status == 0)
        {
            const int ran = foreign_driver_test_run(directory, executable, output, errors, arena);
            foreign_test_expect(tally, ran == exit_status, "driver executable returns expected status", name);
            const String8 printed = foreign_test_read(arena, output);
            foreign_test_expect(tally, printed.pointer != 0 && printed.length == 0, "driver executable has no unexpected output", name);
            result = bytes;
        }
        else if (!success)
        {
            const String8 expected = {
                .pointer = (char*)sentinel,
                .length = sizeof(sentinel),
            };
            const String8 diagnostic = foreign_test_read(arena, errors);
            foreign_test_expect(tally, foreign_driver_test_same(bytes, expected), "failed driver leaves existing output unchanged", name);
            foreign_test_expect(tally, diagnostic.pointer != 0 && diagnostic.length != 0, "failed driver explains refusal", name);
        }
    }
    return result;
}

static void foreign_driver_test_structure(ForeignTestTally* tally, String8 bytes, uint16_t type, String8 interpreter, const char* name)
{
    Elf64_Ehdr header = { 0 };
    const bool header_inside = bytes.pointer != 0 && bytes.length >= sizeof(header);
    if (header_inside)
    {
        memcpy(&header, bytes.pointer, sizeof(header));
    }
    const bool valid = header_inside && memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_ident[EI_CLASS] == ELFCLASS64 && header.e_type == type &&
                       header.e_machine == EM_X86_64 && header.e_entry != 0 && header.e_phoff <= bytes.length &&
                       (uint64_t)header.e_phnum * sizeof(Elf64_Phdr) <= bytes.length - header.e_phoff;
    foreign_test_expect(tally, valid, "driver output has expected ELF type and entry", name);
    uint32_t interpreters = 0;
    for (uint32_t p = 0; valid && p < header.e_phnum; p += 1)
    {
        Elf64_Phdr segment;
        memcpy(&segment, bytes.pointer + header.e_phoff + p * sizeof(segment), sizeof(segment));
        if (segment.p_type == PT_INTERP)
        {
            interpreters += 1;
            const bool inside = segment.p_offset <= bytes.length && segment.p_filesz <= bytes.length - segment.p_offset;
            const bool same = inside && interpreter.pointer != 0 && segment.p_filesz == interpreter.length + 1 &&
                              memcmp(bytes.pointer + segment.p_offset, interpreter.pointer, interpreter.length) == 0 && bytes.pointer[segment.p_offset + interpreter.length] == 0;
            foreign_test_expect(tally, same, "driver preserves explicit interpreter", name);
        }
    }
    foreign_test_expect(tally, interpreters == (interpreter.length != 0 ? 1u : 0u), "driver emits expected interpreter count", name);
}

#include "buster/compiler/foreign_driver_symbols_test.c"

static void foreign_driver_test_paths(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, String8 interpreter)
{
    uint64_t baseline_text_size = UINT64_MAX;
    for (uint32_t fixture = 0; fixture < 3; fixture += 1)
    {
        String8 reference = { 0 };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
            {
                char name[64];
                char lanes[16];
                char* arguments[16];
                uint32_t count = 0;
                snprintf(name, sizeof(name), "paths_%u_%u_%u", fixture, path, foreign_test_lanes[l]);
                snprintf(lanes, sizeof(lanes), "%u", foreign_test_lanes[l]);
                arguments[count++] = files->objects[fixture == 2 ? FOREIGN_DRIVER_TEST_CALL : FOREIGN_DRIVER_TEST_START];
                arguments[count++] = (char*)"--lanes";
                arguments[count++] = lanes;
                if (path == 0)
                {
                    arguments[count++] = (char*)"--scalar";
                }
                if (fixture == 1)
                {
                    arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_DEAD];
                    arguments[count++] = (char*)"--gc-sections";
                }
                else if (fixture == 2)
                {
                    arguments[count++] = (char*)"-pie";
                    arguments[count++] = (char*)(l % 2 == 0 ? "-dynamic-linker" : "--dynamic-linker");
                    arguments[count++] = interpreter.pointer;
                    arguments[count++] = files->needed;
                }
                const String8 bytes = foreign_driver_test_invoke(tally, directory, name, l % 2 == 0 ? "-o" : "--output", arguments, count, true, 42, arena);
                foreign_driver_test_structure(tally, bytes, fixture == 2 ? ET_DYN : ET_EXEC, fixture == 2 ? interpreter : (String8){ 0 }, name);
                if (path == 0 && l == 0)
                {
                    reference = bytes;
                }
                else
                {
                    foreign_test_expect(tally, foreign_driver_test_same(reference, bytes), "driver output is identical across paths and lane counts", name);
                }
                if (fixture == 1)
                {
                    const ExecutableTestImage image = executable_test_image(bytes);
                    const Elf64_Shdr* text = executable_test_section(&image, ".text");
                    foreign_test_expect(tally, text != 0 && text->sh_size == baseline_text_size, "GC output has only the baseline entry's code", name);
                    const ForeignDriverSymbolsTable table = foreign_driver_symbols_table(bytes);
                    Elf64_Sym symbol;
                    foreign_test_expect(tally, table.valid && foreign_driver_symbols_find(&table, "driver_dead", &symbol) == 0 &&
                                              foreign_driver_symbols_find(&table, "driver_missing", &symbol) == 0,
                                        "GC dead definitions and references are omitted, never fake undefined rows", name);
                }
                else if (fixture == 0 && path == 0 && l == 0)
                {
                    const ExecutableTestImage image = executable_test_image(bytes);
                    const Elf64_Shdr* text = executable_test_section(&image, ".text");
                    baseline_text_size = text != 0 ? text->sh_size : UINT64_MAX;
                }
            }
        }
    }
}

typedef struct ForeignDriverTestCase ForeignDriverTestCase;
struct ForeignDriverTestCase
{
    const char* name;
    char* arguments[16];
    bool success;
    int status;
};

static void foreign_driver_test_cases(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena)
{
    ForeignDriverTestCase cases[] = {
        {
            .name = "default_static",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "short_entry",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_ENTRY],
                (char*)"-e",
                (char*)"driver_alternate",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "long_entry",
            .arguments = {
                (char*)"--entry",
                (char*)"driver_alternate",
                files->objects[FOREIGN_DRIVER_TEST_ENTRY],
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "archive_entry",
            .arguments = {
                files->entry,
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "joined_entry_and_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_ENTRY],
                (char*)"--entry=driver_alternate",
                (char*)"--lanes=2",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "archive_alternate_entry",
            .arguments = {
                files->alternate,
                (char*)"--entry",
                (char*)"driver_alternate",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "archive_first_wins",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                files->first,
                (char*)"--scalar",
                files->second,
                (char*)"--lanes",
                (char*)"2",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "archive_second_wins",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                files->second,
                (char*)"--lanes",
                (char*)"3",
                files->first,
            },
            .success = true,
            .status = 19,
        },
        {
            .name = "whole_scope",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"--whole-archive",
                files->first,
                (char*)"--scalar",
                (char*)"--no-whole-archive",
                files->poison,
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "whole_poison",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--whole-archive",
                (char*)"--lanes",
                (char*)"2",
                files->poison,
                (char*)"--no-whole-archive",
            },
        },
        {
            .name = "library_search",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"-L",
                (char*)directory,
                (char*)"-Bstatic",
                (char*)"--lanes",
                (char*)"3",
                (char*)"-l",
                (char*)"fdriver_pick",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "option_named_symbols",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--gc-sections",
                (char*)"-u",
                (char*)"--gc-sections",
                (char*)"--undefined",
                (char*)"--scalar",
                files->options,
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "strip_option_named_symbols",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--gc-sections",
                (char*)"-u",
                (char*)"-s",
                (char*)"--undefined",
                (char*)"--strip-all",
                files->options,
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "strip_reject_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--strip-all=false",
            },
        },
        {
            .name = "strip_reject_joined",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"-sfoo",
            },
        },
        {
            .name = "stripped_undefined",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"-s",
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
            },
        },
        {
            .name = "default_static_search",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"-L",
                (char*)directory,
                (char*)"-lfdriver_choice",
            },
            .success = true,
            .status = 19,
        },
        {
            // -pie with -static is a static PIE, as GNU ld and lld read them; this one needs no relocation.
            .name = "static_pie_from_pie_and_static",
            .arguments = {
                (char*)"-pie",
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"-L",
                (char*)directory,
                (char*)"-static",
                (char*)"-lfdriver_choice",
            },
            .success = true,
            .status = 19,
        },
        {
            .name = "static_explicit_dso",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                files->needed,
            },
        },
        {
            .name = "static_dynamic_search_override",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"-L",
                (char*)directory,
                (char*)"-Bdynamic",
                (char*)"-lfdriver_choice",
            },
        },
        {
            .name = "eager_undefined",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
            },
        },
        {
            // The conversion on lanes (foreign_link_lanes.c) refuses the same name with the same diagnostic.
            .name = "eager_undefined_lanes",
            .arguments = {
                (char*)"--lanes=8",
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
            },
        },
        {
            .name = "gc_disabled_last",
            .arguments = {
                (char*)"--gc-sections",
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"--no-gc-sections",
            },
        },
        {
            .name = "gc_enabled_last",
            .arguments = {
                (char*)"--no-gc-sections",
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"--gc-sections",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "gc_live_undefined",
            .arguments = {
                (char*)"--gc-sections",
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"-e",
                (char*)"driver_dead",
            },
        },
        {
            .name = "gc_forced_undefined",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"--gc-sections",
                (char*)"-u",
                (char*)"driver_missing",
            },
        },
        {
            .name = "gc_forced_dead_function",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"--gc-sections",
                (char*)"--undefined=driver_dead",
            },
        },
        {
            .name = "no_implicit_crt",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_MAIN],
            },
        },
        {
            .name = "no_implicit_crt_lanes",
            .arguments = {
                (char*)"--lanes=3",
                files->objects[FOREIGN_DRIVER_TEST_MAIN],
            },
        },
        {
            .name = "missing_entry",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--entry",
                (char*)"driver_absent",
            },
        },
        {
            .name = "call_out_of_range",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                files->objects[FOREIGN_DRIVER_TEST_ABSOLUTE],
            },
        },
        {
            .name = "no_inputs",
        },
        {
            .name = "unknown_option",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--not-a-link-foreign-option",
            },
        },
        {
            .name = "missing_entry_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--entry",
            },
        },
        {
            .name = "missing_output_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"-o",
            },
        },
        {
            .name = "missing_loader_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"-pie",
                (char*)"--dynamic-linker",
            },
        },
        {
            .name = "missing_lanes_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
            },
        },
        {
            .name = "zero_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
                (char*)"0",
            },
        },
        {
            .name = "negative_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
                (char*)"-1",
            },
        },
        {
            .name = "invalid_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
                (char*)"2x",
            },
        },
        {
            .name = "overflow_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
                (char*)"4294967296",
            },
        },
        {
            .name = "empty_joined_output",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--output=",
            },
        },
        {
            .name = "empty_joined_entry",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--entry=",
            },
        },
        {
            .name = "static_ifunc_unapplied",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_IFUNC],
            },
        },
        {
            .name = "static_pie_ifunc_unapplied",
            .arguments = {
                (char*)"-static",
                (char*)"-pie",
                files->objects[FOREIGN_DRIVER_TEST_IFUNC],
            },
        },
        {
            .name = "static_ifunc_inert",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_IFUNC_INERT],
            },
        },
        {
            .name = "static_pie_ifunc_inert",
            .arguments = {
                (char*)"-static",
                (char*)"-pie",
                files->objects[FOREIGN_DRIVER_TEST_IFUNC_INERT_DYNAMIC],
            },
        },
        // #405's second review's chain without libc: a _start that mentions __libc_start_main only in data, and a
        // __libc_start_main of its own that mentions what libc.a's refers to (and a static PIE's relocator, _DYNAMIC).
        {
            .name = "static_ifunc_inert_chain",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_INERT_START],
                files->objects[FOREIGN_DRIVER_TEST_OWN_LIBC_START],
            },
        },
        {
            .name = "static_pie_ifunc_inert_chain",
            .arguments = {
                (char*)"-static",
                (char*)"-pie",
                files->objects[FOREIGN_DRIVER_TEST_INERT_START_PIE],
                files->objects[FOREIGN_DRIVER_TEST_OWN_LIBC_START],
                files->objects[FOREIGN_DRIVER_TEST_OWN_RELOCATOR],
            },
        },
        {
            .name = "static_ifunc_unflagged",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_IFUNC_APPLIED],
            },
        },
        {
            .name = "static_ifunc_applied",
            .arguments = {
                (char*)"--startup-applies-irelative",
                files->objects[FOREIGN_DRIVER_TEST_IFUNC_APPLIED],
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "static_pie_ifunc_applied",
            .arguments = {
                (char*)"-static",
                (char*)"-pie",
                (char*)"--startup-applies-irelative",
                files->objects[FOREIGN_DRIVER_TEST_IFUNC_PIE_APPLIED],
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "shared_with_static",
            .arguments = {
                (char*)"-shared",
                (char*)"-static",
                files->objects[FOREIGN_DRIVER_TEST_FIRST],
            },
        },
        {
            .name = "no_dynamic_linker_with_pie",
            .arguments = {
                (char*)"-pie",
                (char*)"--no-dynamic-linker",
                files->objects[FOREIGN_DRIVER_TEST_START],
            },
        },
        {
            .name = "missing_soname_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_FIRST],
                (char*)"-shared",
                (char*)"-soname",
            },
        },
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        ForeignDriverTestCase* test = &cases[c];
        uint32_t count = 0;
        while (count < BUSTER_ARRAY_LENGTH(test->arguments) && test->arguments[count] != 0)
        {
            count += 1;
        }
        const String8 bytes = foreign_driver_test_invoke(tally, directory, test->name, c % 2 == 0 ? "-o" : "--output", test->arguments, count, test->success, test->status, arena);
        if (c == 0)
        {
            foreign_driver_test_structure(tally, bytes, ET_EXEC, (String8){ 0 }, test->name);
        }
        else if (strcmp(test->name, "static_pie_from_pie_and_static") == 0)
        {
            foreign_driver_test_structure(tally, bytes, ET_DYN, (String8){ 0 }, test->name);
        }
        else if (strcmp(test->name, "option_named_symbols") == 0)
        {
            static const uint8_t first[] = { 0xb8, 0xe1, 0x47, 0xab, 0x13, 0xc3 };
            static const uint8_t second[] = { 0xb8, 0xf2, 0x68, 0xcd, 0x35, 0xc3 };
            const ExecutableTestImage image = executable_test_image(bytes);
            const Elf64_Shdr* section = executable_test_section(&image, ".text");
            bool found_first = false;
            bool found_second = false;
            const bool inside = section != 0 && section->sh_offset <= bytes.length && section->sh_size <= bytes.length - section->sh_offset;
            for (uint64_t offset = 0; inside && offset + sizeof(first) <= section->sh_size; offset += 1)
            {
                const char* code = bytes.pointer + section->sh_offset + offset;
                found_first = found_first || memcmp(code, first, sizeof(first)) == 0;
                found_second = found_second || memcmp(code, second, sizeof(second)) == 0;
            }
            foreign_test_expect(tally, found_first && found_second, "both option-looking forced roots retain their distinct code through GC", test->name);
        }
        else if (strcmp(test->name, "strip_option_named_symbols") == 0)
        {
            const ForeignDriverSymbolsTable table = foreign_driver_symbols_table(bytes);
            Elf64_Sym short_name = { 0 };
            Elf64_Sym long_name = { 0 };
            foreign_test_expect(tally, foreign_driver_symbols_find(&table, "-s", &short_name) == 1 && foreign_driver_symbols_find(&table, "--strip-all", &long_name) == 1 &&
                                      short_name.st_shndx != SHN_UNDEF && long_name.st_shndx != SHN_UNDEF,
                                "strip-looking undefined values remain named GC roots without stripping", test->name);
        }
    }
    // A static link refuses an IFUNC whose IRELATIVE relocation nothing applies, naming it and saying why.
    static const char* const unapplied[] = {
        "static_ifunc_unapplied",
        "static_pie_ifunc_unapplied",
        "static_ifunc_inert",
        "static_pie_ifunc_inert",
        "static_ifunc_inert_chain",
        "static_pie_ifunc_inert_chain",
        "static_ifunc_unflagged",
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(unapplied); c += 1)
    {
        ForeignTestPath errors;
        snprintf(errors, sizeof(errors), "%s/fdriver_%s.err", directory, unapplied[c]);
        const String8 diagnostic = foreign_test_read(arena, errors);
        const bool named = diagnostic.pointer != 0 && memmem(diagnostic.pointer, diagnostic.length, "driver_ifunc", 12) != 0 &&
                           memmem(diagnostic.pointer, diagnostic.length, "glibc's startup", 15) != 0 &&
                           memmem(diagnostic.pointer, diagnostic.length, "--startup-applies-irelative", 27) != 0;
        foreign_test_expect(tally, named, "a static IFUNC nothing applies is refused, naming it and saying why", unapplied[c]);
    }
    // A refusal's diagnostic is the same from the conversion on lanes as from the calling thread's.
    static const char* const refused[][2] = {
        { "eager_undefined", "eager_undefined_lanes" },
        { "no_implicit_crt", "no_implicit_crt_lanes" },
    };
    for (uint32_t pair = 0; pair < BUSTER_ARRAY_LENGTH(refused); pair += 1)
    {
        ForeignTestPath one;
        ForeignTestPath many;
        snprintf(one, sizeof(one), "%s/fdriver_%s.err", directory, refused[pair][0]);
        snprintf(many, sizeof(many), "%s/fdriver_%s.err", directory, refused[pair][1]);
        const String8 first = foreign_test_read(arena, one);
        const String8 second = foreign_test_read(arena, many);
        foreign_test_expect(tally, first.pointer != 0 && first.length != 0 && foreign_driver_test_same(first, second), "lanes refuse with the same diagnostic",
                            refused[pair][1]);
    }
    ForeignTestPath missing;
    snprintf(missing, sizeof(missing), "%s/fdriver_missing.o", directory);
    char* arguments[] = {
        files->objects[FOREIGN_DRIVER_TEST_START],
        missing,
    };
    foreign_driver_test_invoke(tally, directory, "missing_file", "-o", arguments, BUSTER_ARRAY_LENGTH(arguments), false, 0, arena);
    // A failure to open an input is the same by default, whose calling thread opens the first inputs (#594), as on one
    // lane and on 3: a file missing after an object, after the large one, after four of the large one (whose bytes pay
    // for the most lanes, so that the lanes open the missing one), and a -l name no directory has.
    char* after_large[] = {
        files->large,
        missing,
    };
    char* after_four[] = {
        files->large,
        files->large,
        files->large,
        files->large,
        missing,
    };
    char* nowhere[] = {
        files->objects[FOREIGN_DRIVER_TEST_START],
        (char*)"-L",
        (char*)directory,
        (char*)"-lfdriver_nowhere",
    };
    char* const* failing[] = {
        arguments,
        after_large,
        after_four,
        nowhere,
    };
    const uint32_t failing_counts[] = {
        BUSTER_ARRAY_LENGTH(arguments),
        BUSTER_ARRAY_LENGTH(after_large),
        BUSTER_ARRAY_LENGTH(after_four),
        BUSTER_ARRAY_LENGTH(nowhere),
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(failing); f += 1)
    {
        String8 reference = { 0 };
        for (uint32_t way = 0; way < 3; way += 1)
        {
            char* inputs[8];
            uint32_t count = 0;
            if (way != 0)
            {
                inputs[count++] = way == 1 ? (char*)"--lanes=1" : (char*)"--lanes=3";
            }
            memcpy(inputs + count, failing[f], failing_counts[f] * sizeof(char*));
            count += failing_counts[f];
            char name[64];
            ForeignTestPath errors;
            snprintf(name, sizeof(name), "open_failure_%u_%u", f, way);
            snprintf(errors, sizeof(errors), "%s/fdriver_%s.err", directory, name);
            foreign_driver_test_invoke(tally, directory, name, "-o", inputs, count, false, 0, arena);
            const String8 diagnostic = foreign_test_read(arena, errors);
            reference = way == 0 ? diagnostic : reference;
            foreign_test_expect(tally, way == 0 || foreign_driver_test_same(reference, diagnostic), "an input that doesn't open fails alike by default and on lanes", name);
        }
    }
    char* joined[] = {
        files->objects[FOREIGN_DRIVER_TEST_START],
    };
    foreign_driver_test_invoke(tally, directory, "attached=fdriver_equals_regression", "-oFILE", joined, BUSTER_ARRAY_LENGTH(joined), true, 42, arena);
    foreign_driver_test_invoke(tally, directory, "joined_output", "--output=FILE", joined, BUSTER_ARRAY_LENGTH(joined), true, 42, arena);
}

// The original fixtures put two relocated addresses in their only DATA atom and read them at runtime. This oracle
// inspects those addresses independently of the regular symbol table, so it cannot pass from metadata alone.
// A static link's IFUNCs need glibc's own startup files (#405's reviews), checked in both static modes with GCC's
// line around them (crti.o, crtbegin, libgcc and libc.a in a group, crtend, crtn.o): main calling an IFUNC after the
// system's start file (crt1.o, or rcrt1.o for a static PIE) links and runs, and so it does through a symbolic link to
// the start file, on 3 lanes; a copy of the start file elsewhere is refused, as are a _start of its own in its place
// that mentions __libc_start_main only in data (on 3 lanes too), and the system's start file with a __libc_start_main
// of the test's own (and for a static PIE, a relocator of its own), each naming the reason.
static void foreign_driver_test_startup(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena)
{
    static const char* const variants[] = {
        "glibc",
        "linked",
        "copied",
        "inert",
        "own_libc_start",
    };
    for (uint32_t pie = 0; pie < 2; pie += 1)
    {
        const char* kind = pie != 0 ? "static_pie" : "static";
        Runtime system;
        const bool found = (pie != 0 ? runtime_import_static_pie(&system) : runtime_import_static(&system)) == RUNTIME_ERROR_NONE;
        ForeignTestPath objects[RUNTIME_OBJECTS] = { 0 };
        ForeignTestPath archives[BUSTER_RUNTIME_ARCHIVES] = { 0 };
        const uint32_t archive_count = found ? system.archive_count : 0;
        for (uint32_t i = 0; found && i < RUNTIME_OBJECTS; i += 1)
        {
            snprintf(objects[i], sizeof(objects[i]), "%.*s", (int)system.object_paths[i].length, system.object_paths[i].pointer);
        }
        for (uint32_t i = 0; i < archive_count; i += 1)
        {
            snprintf(archives[i], sizeof(archives[i]), "%.*s", (int)system.archives[i].path.length, system.archives[i].path.pointer);
        }
        runtime_release(&system);
        ForeignTestPath copied;
        ForeignTestPath linked;
        snprintf(copied, sizeof(copied), "%s/fdriver_%s_copied_start.o", directory, kind);
        snprintf(linked, sizeof(linked), "%s/fdriver_%s_linked_start.o", directory, kind);
        const String8 start = found ? foreign_test_read(arena, objects[RUNTIME_OBJECT_START]) : (String8){ 0 };
        unlink(linked);
        const bool placed = start.pointer != 0 && foreign_test_write(copied, start.pointer, start.length) && symlink(objects[RUNTIME_OBJECT_START], linked) == 0;
        foreign_test_expect(tally, found && archive_count != 0 && placed, "find the system's static start file and archives, copied and linked", kind);
        for (uint32_t v = 0; placed && v < BUSTER_ARRAY_LENGTH(variants); v += 1)
        {
            char* arguments[BUSTER_FOREIGN_DRIVER_TEST_ARGUMENTS];
            uint32_t count = 0;
            if (pie != 0)
            {
                arguments[count++] = (char*)"-static";
                arguments[count++] = (char*)"-pie";
                arguments[count++] = (char*)"--no-dynamic-linker";
                arguments[count++] = (char*)"-z";
                arguments[count++] = (char*)"text";
            }
            if (v == 1 || v == 3)
            {
                arguments[count++] = (char*)"--lanes";
                arguments[count++] = (char*)"3";
            }
            arguments[count++] = v == 1 ? linked : v == 2 ? copied : v == 3 ? files->objects[pie != 0 ? FOREIGN_DRIVER_TEST_INERT_START_PIE : FOREIGN_DRIVER_TEST_INERT_START] : objects[RUNTIME_OBJECT_START];
            for (uint32_t i = RUNTIME_OBJECT_INIT_PROLOGUE; i < RUNTIME_OBJECT_END; i += 1)
            {
                arguments[count++] = objects[i];
            }
            if (v != 3)
            {
                arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_IFUNC_MAIN];
            }
            if (v == 4)
            {
                arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_OWN_LIBC_START];
            }
            if (v == 4 && pie != 0)
            {
                arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_OWN_RELOCATOR];
            }
            arguments[count++] = (char*)"--start-group";
            for (uint32_t i = 0; i < archive_count; i += 1)
            {
                arguments[count++] = archives[i];
            }
            arguments[count++] = (char*)"--end-group";
            for (uint32_t i = RUNTIME_OBJECT_END; i < RUNTIME_OBJECTS; i += 1)
            {
                arguments[count++] = objects[i];
            }
            char name[64];
            snprintf(name, sizeof(name), "%s_startup_%s", kind, variants[v]);
            const bool success = v < 2;
            foreign_driver_test_invoke(tally, directory, name, "-o", arguments, count, success, 42, arena);
            if (!success)
            {
                ForeignTestPath errors;
                snprintf(errors, sizeof(errors), "%s/fdriver_%s.err", directory, name);
                const String8 diagnostic = foreign_test_read(arena, errors);
                const bool reason = diagnostic.pointer != 0 && memmem(diagnostic.pointer, diagnostic.length, "glibc's startup", 15) != 0 &&
                                    memmem(diagnostic.pointer, diagnostic.length, "--startup-applies-irelative", 27) != 0;
                foreign_test_expect(tally, reason, "a static IFUNC is refused unless glibc's own startup files apply it, saying why", name);
            }
        }
    }
}

static bool foreign_driver_test_string_values(String8 bytes, uint64_t* first, uint64_t* second)
{
    const ExecutableTestImage image = executable_test_image(bytes);
    const Elf64_Shdr* data = executable_test_section(&image, ".data");
    const bool valid = data != 0 && data->sh_size == 2 * sizeof(uint64_t) && data->sh_offset <= bytes.length && data->sh_size <= bytes.length - data->sh_offset;
    if (valid)
    {
        memcpy(first, bytes.pointer + data->sh_offset, sizeof(*first));
        memcpy(second, bytes.pointer + data->sh_offset + sizeof(*first), sizeof(*second));
    }
    return valid;
}

static void foreign_driver_test_strings(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena)
{
    String8 gc_reference = { 0 };
    for (uint32_t mode = 0; mode < 6; mode += 1)
    {
        char name[64];
        char* arguments[4];
        uint32_t count = 0;
        snprintf(name, sizeof(name), "strings_%u", mode);
        arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_STRINGS];
        if (mode >= 4)
        {
            arguments[count++] = (char*)"--gc-sections";
            arguments[count++] = (char*)"--merge-strings";
            if (mode == 5)
            {
                arguments[count++] = (char*)"--scalar";
            }
        }
        else if (mode != 0)
        {
            arguments[count++] = (char*)(mode == 3 ? "--no-merge-strings" : "--merge-strings");
        }
        if (mode == 2 || mode == 3)
        {
            arguments[count++] = (char*)(mode == 2 ? "--no-merge-strings" : "--merge-strings");
        }
        const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "-o", arguments, count, true, 42, arena);
        uint64_t first = 0;
        uint64_t second = 0;
        const bool named = foreign_driver_test_string_values(bytes, &first, &second);
        foreign_test_expect(tally, named, "driver emits both relocated string addresses", name);
        foreign_test_expect(tally, named && ((first == second) == (mode == 1 || mode >= 3)), "last string option controls pooling, initially disabled", name);
        if (mode == 4)
        {
            gc_reference = bytes;
        }
        else if (mode == 5)
        {
            foreign_test_expect(tally, foreign_driver_test_same(gc_reference, bytes), "GC then string pooling has identical scalar and vector output", name);
        }
    }
    // A named PC-relative relocation does not preserve enough field-bias information for string pooling. The
    // successful fallback must keep the complete original layout, including both literal addresses and fixups.
    char* baseline_arguments[] = {
        files->objects[FOREIGN_DRIVER_TEST_STRINGS_PC],
        (char*)"--no-merge-strings",
    };
    const String8 baseline = foreign_driver_test_invoke(tally, directory, "strings_pc_baseline", "-o", baseline_arguments, BUSTER_ARRAY_LENGTH(baseline_arguments), true, 42, arena);
    uint64_t first = 0;
    uint64_t second = 0;
    const bool distinct = foreign_driver_test_string_values(baseline, &first, &second) && first != second;
    foreign_test_expect(tally, distinct, "named PC-relative fallback baseline keeps distinct literal addresses", 0);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        char* arguments[] = {
            files->objects[FOREIGN_DRIVER_TEST_STRINGS_PC],
            (char*)"--merge-strings",
            (char*)"--scalar",
        };
        const char* name = path == 0 ? "strings_pc_fallback_vector" : "strings_pc_fallback_exact";
        const uint32_t count = BUSTER_ARRAY_LENGTH(arguments) - (path == 0 ? 1u : 0u);
        const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "-o", arguments, count, true, 42, arena);
        foreign_test_expect(tally, foreign_driver_test_same(baseline, bytes), "named PC-relative string merge fallback preserves complete output", name);
    }
}

static void foreign_driver_test_dynamic(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, ForeignTestSystem* system, String8 interpreter)
{
    char loader_option[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    snprintf(loader_option, sizeof(loader_option), "--dynamic-linker=%.*s", (int)interpreter.length, interpreter.pointer);
    for (uint32_t explicit_loader = 0; explicit_loader < 2; explicit_loader += 1)
    {
        char* arguments[] = {
            (char*)"-pie",
            files->objects[FOREIGN_DRIVER_TEST_CALL],
            (char*)"-L",
            (char*)directory,
            (char*)"-lfdriver_choice",
            loader_option,
        };
        const char* name = explicit_loader == 0 ? "default_pie_search_and_loader" : "default_pie_search_joined_loader";
        const uint32_t count = BUSTER_ARRAY_LENGTH(arguments) - (explicit_loader == 0 ? 1u : 0u);
        const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "-o", arguments, count, true, 42, arena);
        foreign_driver_test_structure(tally, bytes, ET_DYN, interpreter, name);
        const DynamicTestImage image = dynamic_test_image(bytes);
        const char* needed[2] = { 0 };
        const uint32_t found = dynamic_test_needed(&image, needed, BUSTER_ARRAY_LENGTH(needed));
        foreign_test_expect(tally, image.valid && found == 1 && needed[0] != 0 && strcmp(needed[0], "libfdriver_choice.so") == 0,
                            "PIE defaults to shared library with different result from same-name archive", name);
    }
    for (uint32_t mode = 0; mode < 3; mode += 1)
    {
        char name[64];
        char* arguments[24];
        uint32_t count = 0;
        snprintf(name, sizeof(name), "needed_%u", mode);
        arguments[count++] = (char*)"-pie";
        arguments[count++] = (char*)"--dynamic-linker";
        arguments[count++] = interpreter.pointer;
        arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_CALL];
        arguments[count++] = (char*)"-L";
        arguments[count++] = (char*)directory;
        arguments[count++] = (char*)(mode == 2 ? "-Bstatic" : "-Bdynamic");
        arguments[count++] = (char*)"--lanes";
        arguments[count++] = (char*)"3";
        arguments[count++] = (char*)"-lfdriver_pick";
        arguments[count++] = (char*)(mode == 0 ? "--as-needed" : "--no-as-needed");
        arguments[count++] = (char*)"--no-gc-sections";
        arguments[count++] = (char*)"-Bdynamic";
        arguments[count++] = (char*)"-l";
        arguments[count++] = (char*)"fdriver_unused";
        const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "--output", arguments, count, true, 42, arena);
        foreign_driver_test_structure(tally, bytes, ET_DYN, interpreter, name);
        const DynamicTestImage image = dynamic_test_image(bytes);
        const char* needed[4] = { 0 };
        const uint32_t expected = mode == 1 ? 2 : 1;
        const uint32_t found = dynamic_test_needed(&image, needed, BUSTER_ARRAY_LENGTH(needed));
        const bool first = found != 0 && needed[0] != 0 && strcmp(needed[0], mode == 2 ? "libfdriver_unused.so" : "libfdriver_pick.so") == 0;
        const bool second = mode != 1 || (found > 1 && needed[1] != 0 && strcmp(needed[1], "libfdriver_unused.so") == 0);
        foreign_test_expect(tally, image.valid && found == expected && first && second, "driver preserves library search mode and as-needed scope", name);
    }
    {
        char* arguments[] = {
            (char*)"-pie", (char*)"-dynamic-linker", interpreter.pointer, files->objects[FOREIGN_DRIVER_TEST_CALL],
            (char*)"-L", (char*)directory, (char*)"--as-needed", (char*)"-Bdynamic", (char*)"--push-state",
            (char*)"-Bstatic", (char*)"--no-as-needed", (char*)"--push-state", (char*)"-Bdynamic",
            (char*)"--pop-state", (char*)"-lfdriver_pick", (char*)"--pop-state", (char*)"-lfdriver_unused",
        };
        const String8 bytes = foreign_driver_test_invoke(tally, directory, "nested_state", "-o", arguments, BUSTER_ARRAY_LENGTH(arguments), true, 42, arena);
        const DynamicTestImage image = dynamic_test_image(bytes);
        const char* needed[2] = { 0 };
        foreign_test_expect(tally, image.valid && dynamic_test_needed(&image, needed, BUSTER_ARRAY_LENGTH(needed)) == 0,
                            "nested state restores static library selection and as-needed", 0);
    }
    ForeignTestPath no_archive;
    snprintf(no_archive, sizeof(no_archive), "%s/libfdriver_unused.a", directory);
    foreign_test_expect(tally, access(no_archive, F_OK) != 0 && errno == ENOENT, "scoped-static fixture has no DSO archive fallback", no_archive);
    for (uint32_t alias = 0; alias < 2; alias += 1)
    {
        const char* name = alias == 0 ? "scoped_static_restores_dso" : "scoped_bstatic_restores_dso";
        char* arguments[] = {
            (char*)"-pie", (char*)"-dynamic-linker", interpreter.pointer, files->objects[FOREIGN_DRIVER_TEST_SCOPED_CALL],
            (char*)"-L", (char*)directory, (char*)"--as-needed", (char*)"--push-state",
            (char*)(alias == 0 ? "-static" : "-Bstatic"), (char*)"--push-state", (char*)"-Bdynamic", (char*)"--pop-state",
            (char*)"-lfdriver_pick", (char*)"--pop-state", (char*)"-lfdriver_unused",
        };
        const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "-o", arguments, BUSTER_ARRAY_LENGTH(arguments), true, 49, arena);
        foreign_driver_test_structure(tally, bytes, ET_DYN, interpreter, name);
        const DynamicTestImage image = dynamic_test_image(bytes);
        const char* needed[2] = { 0 };
        const uint32_t found = dynamic_test_needed(&image, needed, BUSTER_ARRAY_LENGTH(needed));
        foreign_test_expect(tally, image.valid && found == 1 && needed[0] != 0 && strcmp(needed[0], "libfdriver_unused.so") == 0,
                            "scoped static archive is required and pop restores the required DSO", name);
    }
    foreign_test_expect(tally, system->found, "find explicit system CRT inputs", 0);
    if (system->found)
    {
        char* arguments[BUSTER_FOREIGN_DRIVER_TEST_ARGUMENTS];
        uint32_t count = 0;
        arguments[count++] = (char*)"-pie";
        arguments[count++] = (char*)"-dynamic-linker";
        arguments[count++] = interpreter.pointer;
        for (uint32_t i = 0; i < RUNTIME_OBJECT_END; i += 1)
        {
            arguments[count++] = system->objects[i];
        }
        arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_MAIN];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(system->directories); i += 1)
        {
            arguments[count++] = system->directories[i];
        }
        arguments[count++] = (char*)"-lgcc";
        arguments[count++] = (char*)"--push-state";
        arguments[count++] = (char*)"--as-needed";
        arguments[count++] = (char*)"-lgcc_s";
        arguments[count++] = (char*)"--pop-state";
        arguments[count++] = (char*)"-lc";
        arguments[count++] = (char*)"-lgcc";
        arguments[count++] = (char*)"--push-state";
        arguments[count++] = (char*)"--as-needed";
        arguments[count++] = (char*)"-lgcc_s";
        arguments[count++] = (char*)"--pop-state";
        for (uint32_t i = RUNTIME_OBJECT_END; i < RUNTIME_OBJECTS; i += 1)
        {
            arguments[count++] = system->objects[i];
        }
        const String8 bytes = foreign_driver_test_invoke(tally, directory, "explicit_crt", "-o", arguments, count, true, 42, arena);
        foreign_driver_test_structure(tally, bytes, ET_DYN, interpreter, "explicit_crt");
    }
}

static void foreign_driver_test_oracles(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, String8 interpreter)
{
    static const char* const linkers[] = {
        "ld.bfd",
        "ld.lld",
    };
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(linkers); l += 1)
    {
        for (uint32_t fixture = 0; fixture < 3; fixture += 1)
        {
            ForeignTestPath executable;
            ForeignTestPath output;
            ForeignTestPath errors;
            snprintf(executable, sizeof(executable), "%s/fdriver_oracle_%u_%u", directory, l, fixture);
            snprintf(output, sizeof(output), "%s/fdriver_oracle_%u_%u.out", directory, l, fixture);
            snprintf(errors, sizeof(errors), "%s/fdriver_oracle_%u_%u.err", directory, l, fixture);
            char* arguments[16];
            uint32_t count = 0;
            arguments[count++] = (char*)linkers[l];
            arguments[count++] = (char*)"-o";
            arguments[count++] = executable;
            arguments[count++] = (char*)"-e";
            arguments[count++] = (char*)"_start";
            arguments[count++] = files->objects[fixture == 2 ? FOREIGN_DRIVER_TEST_CALL : FOREIGN_DRIVER_TEST_START];
            if (fixture == 2)
            {
                arguments[count++] = (char*)"-pie";
                arguments[count++] = (char*)"--dynamic-linker";
                arguments[count++] = interpreter.pointer;
                arguments[count++] = files->needed;
            }
            else
            {
                arguments[count++] = (char*)"-static";
                if (fixture == 1)
                {
                    arguments[count++] = (char*)"--gc-sections";
                    arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_DEAD];
                }
            }
            arguments[count] = 0;
            const bool linked = object_test_run(arguments, output, errors) == 0;
            foreign_test_expect(tally, linked, "reference linker accepts original driver fixture", linkers[l]);
            if (linked)
            {
                foreign_test_expect(tally, foreign_driver_test_run(directory, executable, output, errors, arena) == 42, "reference driver fixture exits 42", linkers[l]);
                const String8 bytes = foreign_test_read(arena, executable);
                foreign_driver_test_structure(tally, bytes, fixture == 2 ? ET_DYN : ET_EXEC, fixture == 2 ? interpreter : (String8){ 0 }, linkers[l]);
            }
        }
    }
}

static void foreign_driver_test_interpreter(ForeignTestTally* tally, const char* directory, const Runtime* imported)
{
    Runtime discovered;
    const RuntimeError found = runtime_discover_interpreter(&discovered);
    const bool same = found == RUNTIME_ERROR_NONE && discovered.loader_path.length == imported->loader_path.length &&
                      memcmp(discovered.loader_path.pointer, imported->loader_path.pointer, imported->loader_path.length) == 0;
    foreign_test_expect(tally, same, "path-only discovery matches the complete runtime interpreter", 0);
    bool unloaded = discovered.tables.base == 0 && discovered.names.base == 0 && discovered.gcc_directory.length == 0 &&
                    discovered.gcc_versions.length == 0 && discovered.libc_bytes.length == 0 && discovered.loader_bytes.length == 0 &&
                    discovered.member_count == 0 && discovered.link.atoms.count == 0 && discovered.link.symbols.count == 0;
    for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
    {
        unloaded = unloaded && discovered.object_paths[i].length == 0 && discovered.object_bytes[i].length == 0;
    }
    for (uint32_t i = 0; i < discovered.archive_count; i += 1)
    {
        unloaded = unloaded && discovered.archives[i].bytes.length == 0 && discovered.archives[i].archive.member_count == 0;
    }
    foreign_test_expect(tally, unloaded && discovered.probe_count != 0 && runtime_probes_current(&discovered),
                        "discovery records paths without loading or reserving runtime symbol tables", 0);
    runtime_release(&discovered);

    char path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/interpreter-script.ld", directory);
    char text[4 * BUSTER_RUNTIME_PATH_BYTES];
    const RuntimeError expected[] = { RUNTIME_ERROR_NONE, RUNTIME_ERROR_MISSING, RUNTIME_ERROR_MISSING, RUNTIME_ERROR_IMPORT, RUNTIME_ERROR_FILE, RUNTIME_ERROR_MEMORY };
    // libc.so's archive, which the scripts name as the system's does.
    const String8 nonshared = runtime_libc_archive(imported)->path;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(expected); i += 1)
    {
        int length = 0;
        if (i == 0 || i == 5)
        {
            length = snprintf(text, sizeof(text), "GROUP ( %.*s %.*s AS_NEEDED ( %.*s ) )\n", (int)imported->libc_path.length, imported->libc_path.pointer,
                              (int)nonshared.length, nonshared.pointer, (int)imported->loader_path.length, imported->loader_path.pointer);
        }
        else if (i == 1)
        {
            length = snprintf(text, sizeof(text), "GROUP ( %.*s %.*s )\n", (int)imported->libc_path.length, imported->libc_path.pointer, (int)nonshared.length,
                              nonshared.pointer);
        }
        else if (i == 2)
        {
            length = snprintf(text, sizeof(text), "GROUP ( libc.so.6 libc_nonshared.a AS_NEEDED ( ld-linux-x86-64.so.2 ) )\n");
        }
        else
        {
            length = snprintf(text, sizeof(text), "GROUP (\n");
        }
        const bool written = length > 0 && (uint32_t)length < sizeof(text) && object_write_file(path, (const uint8_t*)text, (uint64_t)length) == OBJECT_ERROR_NONE;
        const bool removed = i != 4 || unlink(path) == 0;
        Runtime scripted;
        const bool begun = runtime_begin(&scripted);
        scripted.libc_script = begun ? runtime_copy_path(&scripted, path) : (String8){ 0 };
        scripted.probe_count = i == 5 ? scripted.probe_capacity : scripted.probe_count;
        const RuntimeError result = written && removed && scripted.libc_script.length != 0 ? runtime_read_interpreter(&scripted) : RUNTIME_ERROR_MEMORY;
        const bool loader = i == 0 ? scripted.loader_path.length == imported->loader_path.length &&
                            memcmp(scripted.loader_path.pointer, imported->loader_path.pointer, imported->loader_path.length) == 0 : scripted.loader_path.length == 0;
        foreign_test_expect(tally, result == expected[i] && loader, "interpreter script requirements and failure do not publish a partial path", path);
        runtime_release(&scripted);
    }
    unlink(path);
}

// The large object (ForeignDriverTestFiles.large): FOREIGN_DRIVER_TEST_START's _start and twice
// BUSTER_FOREIGN_DRIVER_LANE_BYTES of zeros in .rodata, so that its bytes pay for 3 lanes by default, or the CPUs'.
static bool foreign_driver_test_large(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files)
{
    char text[512];
    snprintf(text, sizeof(text),
             ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
             "mov $42,%%edi\nmov $60,%%eax\nsyscall\n.size _start,.-_start\n"
             ".section .rodata.large,\"a\",@progbits\n.globl driver_large\ndriver_large:\n.zero %llu\n"
             ".section .note.GNU-stack,\"\",@progbits\n",
             (unsigned long long)(2 * BUSTER_FOREIGN_DRIVER_LANE_BYTES));
    ForeignTestPath source;
    ForeignTestPath output;
    ForeignTestPath errors;
    snprintf(source, sizeof(source), "%s/fdriver_large.S", directory);
    snprintf(files->large, sizeof(files->large), "%s/fdriver_large.o", directory);
    snprintf(output, sizeof(output), "%s/fdriver_large_compile.out", directory);
    snprintf(errors, sizeof(errors), "%s/fdriver_large_compile.err", directory);
    char* arguments[] = {
        (char*)"cc",
        (char*)"-c",
        (char*)"-fPIC",
        source,
        (char*)"-o",
        files->large,
        0,
    };
    struct stat status;
    const bool made = foreign_test_write(source, text, strlen(text)) && object_test_run(arguments, output, errors) == 0 && stat(files->large, &status) == 0 &&
                      (uint64_t)status.st_size >= 2 * BUSTER_FOREIGN_DRIVER_LANE_BYTES;
    foreign_test_expect(tally, made, "assemble the large driver fixture", files->large);
    return made;
}

// --time: a link reports every stage (the first inputs' opening and the session's start among them, with the session's
// lanes) and the phases of the resolution, the conversion and the writer on standard error, at 1 and 3 lanes and by
// default, and still writes the program; a link that fails reports no stages, only its diagnostic; one of 8 lanes
// started on one CPU runs on one lane, the most the command takes (foreign_driver_cpus), on a session of one; and by
// default, a link of one small object runs on one lane of a session of the most its objects may pay for, at most one per
// core (#905), and its opening reports the objects' bytes that chose it.
static void foreign_driver_test_time(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena)
{
    static const char* const expected[] = {
        "link-foreign: ",
        "  arguments ",
        "  interpreter ",
        "  open ",
        "  lanes ",
        "  resolve ",
        " winners ",
        "  convert ",
        " sections ",
        " relocations ",
        " exports ",
        "  dedup-unwind ",
        "  merge-strings ",
        "  write ",
        " lanes+symbols ",
        " chunks ",
        " finish ",
        "  release ",
    };
    const uint32_t cpus = foreign_driver_cpus();
    for (uint32_t way = 0; way < 5; way += 1)
    {
        const bool fails = way == 2;
        const bool pinned = way == 3;
        const bool chosen = way == 4;
        ForeignTestPath executable;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(executable, sizeof(executable), "%s/fdriver_time_%u", directory, way);
        snprintf(output, sizeof(output), "%s/fdriver_time_%u.out", directory, way);
        snprintf(errors, sizeof(errors), "%s/fdriver_time_%u.err", directory, way);
        char* arguments[8];
        uint32_t count = 0;
        arguments[count++] = (char*)"/proc/self/exe";
        arguments[count++] = (char*)"link-foreign";
        arguments[count++] = (char*)"--time";
        if (!chosen)
        {
            arguments[count++] = way == 1 ? (char*)"--lanes=3" : pinned ? (char*)"--lanes=8" : (char*)"--lanes=1";
        }
        arguments[count++] = (char*)"-o";
        arguments[count++] = executable;
        arguments[count++] = fails ? files->objects[FOREIGN_DRIVER_TEST_POISON] : files->objects[FOREIGN_DRIVER_TEST_START];
        arguments[count] = 0;
        // The pinned way's child starts on this thread's first CPU alone, as posix_spawn gives it this thread's mask.
        uint64_t saved[16] = { 0 };
        const bool masked = pinned && syscall(SYS_sched_getaffinity, 0, sizeof(saved), saved) > 0;
        uint64_t one[16] = { 0 };
        uint32_t word = 0;
        while (masked && word + 1 < BUSTER_ARRAY_LENGTH(saved) && saved[word] == 0)
        {
            word += 1;
        }
        one[word] = saved[word] & (~saved[word] + 1);
        const bool narrowed = masked && syscall(SYS_sched_setaffinity, 0, sizeof(one), one) == 0;
        const int status = !pinned || narrowed ? object_test_run(arguments, output, errors) : -1;
        const bool restored = !narrowed || syscall(SYS_sched_setaffinity, 0, sizeof(saved), saved) == 0;
        const String8 report = foreign_test_read(arena, errors);
        // The lanes it ran on: 1, 3 (or fewer CPUs'), pinned 1, and by default 1, its object's bytes paying for no more.
        const uint32_t lanes = way == 1 ? (cpus < 3 ? cpus : 3) : 1;
        char header[64];
        snprintf(header, sizeof(header), " ms on %u lane%s\n", lanes, lanes == 1 ? "" : "s");
        bool complete = report.pointer != 0 && memmem(report.pointer, report.length, header, strlen(header)) != 0;
        // The opening's objects, by default: the object's own bytes.
        struct stat object;
        char opened[64];
        const bool sized = chosen && stat(files->objects[FOREIGN_DRIVER_TEST_START], &object) == 0;
        snprintf(opened, sizeof(opened), " objects %llu bytes\n", sized ? (unsigned long long)object.st_size : 0ull);
        const bool counted = report.pointer != 0 && memmem(report.pointer, report.length, " objects ", strlen(" objects ")) != 0;
        complete = complete && (chosen ? sized && memmem(report.pointer, report.length, opened, strlen(opened)) != 0 : !counted);
        // The session's lanes (#905): as many as the link may take, the most its objects may pay for by default, at most
        // one per core (by /proc/cpuinfo, or sysfs where it doesn't list them), though it runs on fewer.
        const uint32_t most = cpus < BUSTER_FOREIGN_DRIVER_LANES ? cpus : BUSTER_FOREIGN_DRIVER_LANES;
        const uint32_t listed = chosen ? lane_test_process_cores() : 0;
        const uint32_t cores = listed != 0 ? listed : lane_core_count();
        const uint32_t session = way == 1 ? lanes : chosen ? (cores < most ? cores : most) : 1;
        char sessioned[64];
        snprintf(sessioned, sizeof(sessioned), " session %u of %u\n", session, session);
        complete = complete && (fails || memmem(report.pointer, report.length, sessioned, strlen(sessioned)) != 0);
        bool none = report.pointer != 0;
        for (uint32_t i = 0; report.pointer != 0 && i < BUSTER_ARRAY_LENGTH(expected); i += 1)
        {
            const bool found = memmem(report.pointer, report.length, expected[i], strlen(expected[i])) != 0;
            complete = complete && found;
            none = none && (i == 0 || !found);
        }
        if (fails)
        {
            foreign_test_expect(tally, status > 0 && none, "a failed link with --time reports no stages", errors);
        }
        else
        {
            foreign_test_expect(tally, status == 0 && complete && restored, "--time reports every stage and phase, and the lanes it ran on", errors);
            foreign_test_expect(tally, status == 0 && foreign_driver_test_run(directory, executable, output, errors, arena) == 42, "a link with --time runs", executable);
        }
    }
}

// A link whose threads can't start (#501, #905): in a child limited to the processes it has, link-foreign on 3 lanes
// must run on one lane (its session's fallback, which no later stage retries) and link, as on one lane, which starts no
// thread; and by default (#594), the large object, whose bytes pay for lanes, link on one lane, and a small one, which
// takes one, link. RLIMIT_NPROC doesn't limit root, and with one CPU the command takes one lane. An alarm, which execve
// keeps, ends a child that hangs.
static void foreign_driver_test_unstarted(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, bool large)
{
    static const char sentinel[] = "existing driver output\000must survive failed link\377";
    if (geteuid() == 0)
    {
        printf("  %s: a team that doesn't start isn't checked as root, whom RLIMIT_NPROC doesn't limit\n", tally->check);
    }
    for (uint32_t way = 0; geteuid() != 0 && way < 4; way += 1)
    {
        const bool chosen = way >= 2;
        const bool laned = way == 1 && foreign_driver_cpus() > 1;
        const bool skipped = way == 2 && !large;
        ForeignTestPath executable;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(executable, sizeof(executable), "%s/fdriver_unstarted_%u", directory, way);
        snprintf(output, sizeof(output), "%s/fdriver_unstarted_%u.out", directory, way);
        snprintf(errors, sizeof(errors), "%s/fdriver_unstarted_%u.err", directory, way);
        char* arguments[8];
        uint32_t count = 0;
        arguments[count++] = (char*)"/proc/self/exe";
        arguments[count++] = (char*)"link-foreign";
        if (!chosen)
        {
            arguments[count++] = way == 0 ? (char*)"--lanes=1" : (char*)"--lanes=3";
        }
        arguments[count++] = (char*)"-o";
        arguments[count++] = executable;
        arguments[count++] = way == 2 ? files->large : files->objects[FOREIGN_DRIVER_TEST_START];
        arguments[count] = 0;
        const bool prepared = !skipped && foreign_test_write(executable, sentinel, sizeof(sentinel));
        fflush(stdout);
        fflush(stderr);
        const pid_t child = prepared ? fork() : -1;
        if (child == 0)
        {
#if BUSTER_SANITIZER_RESOURCES
            const bool limited = setenv("BUSTER_TEST_NO_LANE_THREADS", "1", 1) == 0;
#else
            const struct rlimit limit = {
                .rlim_cur = 0,
                .rlim_max = 0,
            };
            const bool limited = setrlimit(RLIMIT_NPROC, &limit) == 0;
#endif
            const int out = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            const int err = open(errors, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out >= 0 && err >= 0 && dup2(out, 1) == 1 && dup2(err, 2) == 2 && limited)
            {
                alarm(BUSTER_OBJECT_TEST_RUN_SECONDS);
                execv(arguments[0], arguments);
            }
            _exit(127);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status);
        const int exited = waited ? WEXITSTATUS(status) : -1;
        if (laned)
        {
            const String8 diagnostic = foreign_test_read(arena, errors);
            foreign_test_expect(tally, exited == 0 && diagnostic.length == 0 && foreign_driver_test_run(directory, executable, output, errors, arena) == 42,
                                "a link of --lanes whose threads can't start runs on one lane", errors);
        }
        else if (way == 2 && !skipped)
        {
            foreign_test_expect(tally, exited == 0 && foreign_driver_test_run(directory, executable, output, errors, arena) == 42,
                                "a link that chose its lanes runs on one when its threads can't start", executable);
        }
        else if (!skipped)
        {
            foreign_test_expect(tally, exited == 0 && foreign_driver_test_run(directory, executable, output, errors, arena) == 42, "a link on one lane starts no thread", executable);
        }
    }
}

// The command's threads (#501, #905): a library preloaded into link-foreign counts its calls of pthread_create, which
// its session makes once for the whole command, before any input is opened, one per lane past the first: none on one
// lane, and on 3 (or the CPUs') as many as the lanes past the first, for a static executable, a PIE, and with section
// GC, unwind deduplication and string merging after the conversion; and by default, whatever lanes its objects' bytes
// then pay for (#594), the most the command takes (BUSTER_FOREIGN_DRIVER_LANES, at most the CPUs' cores, #905, and its
// lane 1, which counts them, where one core leaves it no other), for a small object and for the large one, whose link on
// the lanes its bytes pay for writes the bytes of its link on one lane, which starts none. Each program must still run.
static void foreign_driver_test_threads(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, String8 interpreter, bool large)
{
    static const char counter[] = "#define _GNU_SOURCE\n"
                                  "#include <dlfcn.h>\n"
                                  "#include <pthread.h>\n"
                                  "#include <stdio.h>\n"
                                  "#include <stdlib.h>\n"
                                  "typedef int (*Create)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);\n"
                                  "static unsigned created;\n"
                                  "int pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*start)(void*), void* argument)\n"
                                  "{\n"
                                  "    Create create = (Create)dlsym(RTLD_NEXT, \"pthread_create\");\n"
                                  "    __atomic_add_fetch(&created, 1, __ATOMIC_RELAXED);\n"
                                  "    return create != 0 ? create(thread, attributes, start, argument) : 11;\n"
                                  "}\n"
                                  "__attribute__((destructor)) static void report(void)\n"
                                  "{\n"
                                  "    const char* path = getenv(\"FDRIVER_THREADS\");\n"
                                  "    FILE* file = path != 0 ? fopen(path, \"w\") : 0;\n"
                                  "    if (file != 0)\n"
                                  "    {\n"
                                  "        fprintf(file, \"%u\\n\", created);\n"
                                  "        fclose(file);\n"
                                  "    }\n"
                                  "}\n";
    ForeignTestPath source;
    ForeignTestPath library;
    ForeignTestPath output;
    ForeignTestPath errors;
    snprintf(source, sizeof(source), "%s/fdriver_threads.c", directory);
    snprintf(library, sizeof(library), "%s/fdriver_threads.so", directory);
    snprintf(output, sizeof(output), "%s/fdriver_threads.out", directory);
    snprintf(errors, sizeof(errors), "%s/fdriver_threads.err", directory);
    char* compile[] = {
        (char*)"cc",
        (char*)"-shared",
        (char*)"-fPIC",
        (char*)"-O1",
        source,
        (char*)"-o",
        library,
        0,
    };
    const bool built = foreign_test_write(source, counter, strlen(counter)) && object_test_run(compile, output, errors) == 0;
    foreign_test_expect(tally, built, "compile the preloaded counter of pthread_create", source);
    const uint32_t cpus = foreign_driver_cpus();
    // The large object's link on its lanes by default, which its link on one lane must write again.
    String8 chosen_bytes = { 0 };
    for (uint32_t way = 0; built && way < 7; way += 1)
    {
        const uint64_t saved = arena->length;
        const bool chosen = way == 4 || way == 5;
        const bool on_large = way == 5 || way == 6;
        const uint32_t most = BUSTER_FOREIGN_DRIVER_LANES < cpus ? BUSTER_FOREIGN_DRIVER_LANES : cpus;
        // By default one lane per core of the most it takes (by /proc/cpuinfo, or sysfs where it doesn't list them): its
        // session's lane 1, created from 2 lanes on, counts them, and runs alone where they leave it no other lane.
        const uint32_t listed = chosen ? lane_test_process_cores() : 0;
        const uint32_t cores = listed != 0 ? listed : lane_core_count();
        const uint32_t per_core = cores < most ? cores : most;
        const uint32_t lanes = way == 0 || way == 6 ? 1 : chosen ? per_core : cpus < 3 ? cpus : 3;
        const uint32_t threads = chosen && most > 1 && per_core < 2 ? 1 : lanes - 1;
        ForeignTestPath executable;
        ForeignTestPath counted;
        char preload[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
        char count_path[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
        snprintf(executable, sizeof(executable), "%s/fdriver_threads_%u", directory, way);
        snprintf(counted, sizeof(counted), "%s/fdriver_threads_%u.count", directory, way);
        snprintf(preload, sizeof(preload), "LD_PRELOAD=%s", library);
        snprintf(count_path, sizeof(count_path), "FDRIVER_THREADS=%s", counted);
        char* arguments[16];
        uint32_t count = 0;
        arguments[count++] = (char*)"/proc/self/exe";
        arguments[count++] = (char*)"link-foreign";
        if (!chosen)
        {
            arguments[count++] = way == 0 || way == 6 ? (char*)"--lanes=1" : (char*)"--lanes=3";
        }
        arguments[count++] = (char*)"-o";
        arguments[count++] = executable;
        if (on_large)
        {
            arguments[count++] = files->large;
        }
        else if (way == 2)
        {
            arguments[count++] = (char*)"-pie";
            arguments[count++] = (char*)"--dynamic-linker";
            arguments[count++] = interpreter.pointer;
            arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_CALL];
            arguments[count++] = files->needed;
        }
        else
        {
            arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_START];
        }
        if (way == 3)
        {
            arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_DEAD];
            arguments[count++] = (char*)"--gc-sections";
            arguments[count++] = (char*)"--dedup-unwind";
            arguments[count++] = (char*)"--merge-strings";
        }
        arguments[count] = 0;
        // The environment without its own LD_PRELOAD and FDRIVER_THREADS, and with the counter's.
        uint64_t inherited = 0;
        for (char** entry = environ; entry != 0 && *entry != 0; entry += 1)
        {
            inherited += 1;
        }
        char** environment = BUSTER_ARENA_PUSH_ARRAY(arena, char*, inherited + 3);
        uint64_t used = 0;
        for (uint64_t i = 0; environment != 0 && i < inherited; i += 1)
        {
            if (strncmp(environ[i], "LD_PRELOAD=", strlen("LD_PRELOAD=")) != 0 && strncmp(environ[i], "FDRIVER_THREADS=", strlen("FDRIVER_THREADS=")) != 0)
            {
                environment[used] = environ[i];
                used += 1;
            }
        }
        unlink(counted);
        int status = -1;
        if (environment != 0)
        {
            environment[used] = preload;
            environment[used + 1] = count_path;
            environment[used + 2] = 0;
            status = object_test_run_environment(arguments, output, errors, environment);
        }
        const String8 text = foreign_test_read(arena, counted);
        char expected[32];
#if __has_feature(thread_sanitizer)
        // ThreadSanitizer starts a thread of its own with the process's first other one, through the same call.
        const uint32_t detector = threads > 0 ? 1 : 0;
#else
        const uint32_t detector = 0;
#endif
        snprintf(expected, sizeof(expected), "%u\n", threads + detector);
        const bool started = text.pointer != 0 && text.length == strlen(expected) && memcmp(text.pointer, expected, text.length) == 0;
        char detail[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
        snprintf(detail, sizeof(detail), "%s: %.*s thread(s) for %u lane(s)", executable, text.pointer != 0 ? (int)strcspn(text.pointer, "\n") : 0, text.pointer != 0 ? text.pointer : "",
                 lanes);
        const bool ran = !on_large || large;
        foreign_test_expect(tally, !ran || (status == 0 && started), "link-foreign starts a thread per lane past the first, once for the command", detail);
        foreign_test_expect(tally, !ran || (status == 0 && foreign_driver_test_run(directory, executable, output, errors, arena) == 42), "a link on the command's threads runs",
                            executable);
        arena_pop_to(arena, saved);
        // Read after the pop: the large link's bytes stay below the next way's pushes.
        if (way == 5 && ran)
        {
            chosen_bytes = foreign_test_read(arena, executable);
        }
        if (way == 6 && ran)
        {
            const uint64_t mark = arena->length;
            const String8 one = foreign_test_read(arena, executable);
            foreign_test_expect(tally, foreign_driver_test_same(chosen_bytes, one), "a link on the lanes its bytes pay for writes its link on one lane's bytes", executable);
            arena_pop_to(arena, mark);
        }
    }
}

// -shared, with its soname by -soname and by GNU ld's -h: a library without an entry point or an interpreter, with
// DT_SONAME and its definition exported, which a PIE that calls it through its PLT links and the loader runs.
static void foreign_driver_test_shared(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, String8 interpreter)
{
    static const char* const forms[] = { "-soname", "-h" };
    for (uint32_t form = 0; form < BUSTER_ARRAY_LENGTH(forms); form += 1)
    {
        ForeignTestPath library_directory;
        ForeignTestPath library;
        ForeignTestPath output;
        ForeignTestPath errors;
        char soname[64];
        snprintf(library_directory, sizeof(library_directory), "%s/fdriver_shared_%u", directory, form);
        snprintf(soname, sizeof(soname), "libfdriver_ours%u.so", form);
        snprintf(library, sizeof(library), "%s/%s", library_directory, soname);
        snprintf(output, sizeof(output), "%s/link.out", library_directory);
        snprintf(errors, sizeof(errors), "%s/link.err", library_directory);
        char* arguments[] = {
            (char*)"/proc/self/exe",
            (char*)"link-foreign",
            (char*)"-shared",
            (char*)forms[form],
            soname,
            (char*)"-o",
            library,
            files->objects[FOREIGN_DRIVER_TEST_FIRST],
            0,
        };
        const bool linked = mkdir(library_directory, 0755) == 0 && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, linked, "driver links a shared library", forms[form]);
        const String8 bytes = linked ? foreign_test_read(arena, library) : (String8){ 0 };
        const DynamicTestImage image = dynamic_test_image(bytes);
        uint64_t value = 0;
        bool named = false;
        const Elf64_Shdr* strings = dynamic_test_section(&image, ".dynstr");
        if (image.valid && strings != 0 && dynamic_test_tag(&image, DT_SONAME, &value) && value < strings->sh_size)
        {
            named = strcmp((const char*)dynamic_test_bytes(&image, strings) + value, soname) == 0;
        }
        bool interpreted = false;
        for (uint32_t p = 0; image.valid && p < image.header->e_phnum; p += 1)
        {
            interpreted = interpreted || image.program_headers[p].p_type == PT_INTERP;
        }
        Elf64_Sym pick = { 0 };
        const bool exported = image.valid && foreign_dynamic_test_symbol(&image, "driver_pick", &pick) != 0 && pick.st_shndx != SHN_UNDEF;
        foreign_test_expect(tally, image.valid && image.header->e_entry == 0 && !interpreted && named && exported,
                            "a shared library has no entry point or interpreter, its soname and its export", forms[form]);
        // A PIE that calls driver_pick, which returns 42, from the library.
        ForeignTestPath executable;
        snprintf(executable, sizeof(executable), "%s/fdriver_shared_program_%u", directory, form);
        char* program[] = {
            (char*)"/proc/self/exe",
            (char*)"link-foreign",
            (char*)"-pie",
            (char*)"--dynamic-linker",
            interpreter.pointer,
            (char*)"-o",
            executable,
            files->objects[FOREIGN_DRIVER_TEST_CALL],
            library,
            0,
        };
        const bool built = linked && object_test_run(program, output, errors) == 0;
        foreign_test_expect(tally, built && foreign_driver_test_run(library_directory, executable, output, errors, arena) == 42, "a PIE runs the shared library's function",
                            forms[form]);
    }
}

static bool foreign_driver_test_contains(String8 text, const char* needle)
{
    return text.pointer != 0 && strstr(text.pointer, needle) != 0;
}

// Runs a link that must be refused and returns its diagnostics.
static String8 foreign_driver_test_diagnostic(ForeignTestTally* tally, const char* directory, const char* name, char* const* inputs, uint32_t count, Arena* arena)
{
    ForeignTestPath errors;
    snprintf(errors, sizeof(errors), "%s/fdriver_%s.err", directory, name);
    foreign_driver_test_invoke(tally, directory, name, "-o", inputs, count, false, 0, arena);
    return foreign_test_read(arena, errors);
}

// GCC's collect2 and LLVM's drivers pass LTO plugin options to the linker. They are taken and ignored when no input
// needs the plugin; an object or loaded archive member that does is refused by input, member and section.
static void foreign_driver_test_plugin(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena)
{
    ForeignTestPath plugin;
    ForeignTestPath llvm_plugin;
    ForeignTestPath other_plugin;
    ForeignTestPath bitcode;
    snprintf(plugin, sizeof(plugin), "%s/liblto_plugin.so", directory);
    snprintf(llvm_plugin, sizeof(llvm_plugin), "%s/LLVMgold.so", directory);
    snprintf(other_plugin, sizeof(other_plugin), "%s/other_plugin.so", directory);
    snprintf(bitcode, sizeof(bitcode), "%s/fdriver_input.bc", directory);
    static const char plugin_bytes[] = "not a plugin this command reads";
    static const unsigned char bitcode_bytes[] = { 0x42, 0x43, 0xc0, 0xde, 0x00 };
    const bool ready = foreign_test_write(plugin, plugin_bytes, sizeof(plugin_bytes)) && foreign_test_write(llvm_plugin, plugin_bytes, sizeof(plugin_bytes)) &&
                       foreign_test_write(other_plugin, plugin_bytes, sizeof(plugin_bytes)) && foreign_test_write(bitcode, bitcode_bytes, sizeof(bitcode_bytes));
    foreign_test_expect(tally, ready, "write plugin and bitcode fixtures", 0);
    if (!ready)
    {
        return;
    }
    {
        char* baseline_arguments[] = {
            files->objects[FOREIGN_DRIVER_TEST_START],
        };
        const String8 baseline = foreign_driver_test_invoke(tally, directory, "plugin_baseline", "-o", baseline_arguments, BUSTER_ARRAY_LENGTH(baseline_arguments), true, 42, arena);
        char resolution[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
        snprintf(resolution, sizeof(resolution), "-plugin-opt=-fresolution=%s/resolution.res", directory);
        char* arguments[] = {
            (char*)"-plugin",
            plugin,
            resolution,
            (char*)"-plugin-opt=-pass-through=-lgcc",
            (char*)"-plugin-opt",
            (char*)"-pass-through=-lc",
            (char*)"--push-state",
            (char*)"--as-needed",
            files->needed,
            (char*)"--pop-state",
            files->objects[FOREIGN_DRIVER_TEST_START],
        };
        const String8 linked = foreign_driver_test_invoke(tally, directory, "plugin_options", "-o", arguments, BUSTER_ARRAY_LENGTH(arguments), true, 42, arena);
        foreign_test_expect(tally, foreign_driver_test_same(baseline, linked), "plugin options leave the output unchanged", 0);
    }
    {
        char joined[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
        snprintf(joined, sizeof(joined), "--plugin=%s", llvm_plugin);
        char* arguments[] = {
            joined,
            files->objects[FOREIGN_DRIVER_TEST_START],
        };
        foreign_driver_test_invoke(tally, directory, "plugin_llvm", "-o", arguments, BUSTER_ARRAY_LENGTH(arguments), true, 42, arena);
    }
    {
        char* arguments[] = {
            (char*)"-plugin",
            other_plugin,
            files->objects[FOREIGN_DRIVER_TEST_START],
        };
        const String8 diagnostic = foreign_driver_test_diagnostic(tally, directory, "plugin_other", arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
        foreign_test_expect(tally, foreign_driver_test_contains(diagnostic, "liblto_plugin.so"), "an arbitrary plugin is refused by name", diagnostic.pointer);
    }
    {
        char* arguments[] = {
            (char*)"-plugin",
        };
        const String8 diagnostic = foreign_driver_test_diagnostic(tally, directory, "plugin_missing", arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
        foreign_test_expect(tally, foreign_driver_test_contains(diagnostic, "-plugin"), "a plugin without its path is refused", diagnostic.pointer);
    }
    {
        char* arguments[] = {
            (char*)"-plugin-opt",
        };
        const String8 diagnostic = foreign_driver_test_diagnostic(tally, directory, "plugin_opt_missing", arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
        foreign_test_expect(tally, foreign_driver_test_contains(diagnostic, "-plugin-opt"), "a plugin option without its value is refused", diagnostic.pointer);
    }
    {
        char* arguments[] = {
            (char*)"-plugin",
            plugin,
            (char*)"-plugin-opt=-fresolution=/dev/null",
            files->objects[FOREIGN_DRIVER_TEST_LTO],
        };
        const String8 diagnostic = foreign_driver_test_diagnostic(tally, directory, "plugin_lto_direct", arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
        foreign_test_expect(tally,
                            foreign_driver_test_contains(diagnostic, ".gnu.lto_.opts") && foreign_driver_test_contains(diagnostic, files->objects[FOREIGN_DRIVER_TEST_LTO]),
                            "a GCC LTO object is refused by section and input", diagnostic.pointer);
    }
    {
        char* arguments[] = {
            files->objects[FOREIGN_DRIVER_TEST_LTO_LLVM],
        };
        const String8 diagnostic = foreign_driver_test_diagnostic(tally, directory, "plugin_lto_llvm_direct", arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
        foreign_test_expect(tally,
                            foreign_driver_test_contains(diagnostic, ".llvm.lto") && foreign_driver_test_contains(diagnostic, files->objects[FOREIGN_DRIVER_TEST_LTO_LLVM]),
                            "a clang fat LTO object is refused by section and input", diagnostic.pointer);
    }
    {
        char* arguments[] = {
            bitcode,
        };
        const String8 diagnostic = foreign_driver_test_diagnostic(tally, directory, "plugin_bitcode", arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
        foreign_test_expect(tally, foreign_driver_test_contains(diagnostic, "LLVM bitcode") && foreign_driver_test_contains(diagnostic, bitcode),
                            "raw LLVM bitcode is refused by input", diagnostic.pointer);
    }
    const char* members[] = {
        "fdriver_lto_function.o",
    };
    char* lazy = foreign_test_archive(directory, "libfdriver_lto_lazy.a", members, BUSTER_ARRAY_LENGTH(members), false, arena);
    char* thin = foreign_test_archive(directory, "libfdriver_lto_thin.a", members, BUSTER_ARRAY_LENGTH(members), true, arena);
    foreign_test_expect(tally, lazy != 0 && thin != 0, "write LTO archives", 0);
    if (lazy != 0)
    {
        char* arguments[] = {
            files->objects[FOREIGN_DRIVER_TEST_START],
            lazy,
        };
        foreign_driver_test_invoke(tally, directory, "plugin_lto_unneeded", "-o", arguments, BUSTER_ARRAY_LENGTH(arguments), true, 42, arena);
    }
    if (lazy != 0)
    {
        char* arguments[] = {
            files->objects[FOREIGN_DRIVER_TEST_LTO_CALL],
            lazy,
        };
        const String8 diagnostic = foreign_driver_test_diagnostic(tally, directory, "plugin_lto_lazy", arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
        foreign_test_expect(tally,
                            foreign_driver_test_contains(diagnostic, ".gnu.lto_.opts") && foreign_driver_test_contains(diagnostic, lazy) &&
                                foreign_driver_test_contains(diagnostic, "fdriver_lto_function.o"),
                            "a lazy archive's LTO member is refused by archive, member and section", diagnostic.pointer);
    }
    if (thin != 0)
    {
        char* arguments[] = {
            files->objects[FOREIGN_DRIVER_TEST_LTO_CALL],
            thin,
        };
        const String8 diagnostic = foreign_driver_test_diagnostic(tally, directory, "plugin_lto_thin", arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
        foreign_test_expect(tally,
                            foreign_driver_test_contains(diagnostic, ".gnu.lto_.opts") && foreign_driver_test_contains(diagnostic, thin) &&
                                foreign_driver_test_contains(diagnostic, "fdriver_lto_function.o"),
                            "a thin archive's LTO member is refused by archive, member and section", diagnostic.pointer);
    }
    if (lazy != 0)
    {
        char* arguments[] = {
            (char*)"--whole-archive",
            lazy,
            (char*)"--no-whole-archive",
            files->objects[FOREIGN_DRIVER_TEST_START],
        };
        const String8 diagnostic = foreign_driver_test_diagnostic(tally, directory, "plugin_lto_whole", arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
        foreign_test_expect(tally, foreign_driver_test_contains(diagnostic, ".gnu.lto_.opts") && foreign_driver_test_contains(diagnostic, lazy),
                            "a whole archive's LTO member is refused though nothing calls it", diagnostic.pointer);
    }
}

static bool foreign_driver_test(const char* directory, Arena* arena, bool cross_check, ForeignTestSystem* system)
{
    const uint64_t saved = arena->length;
    ForeignTestTally tally = {
        .check = "foreign command driver",
    };
    ForeignDriverTestFiles files = { 0 };
    Runtime runtime;
    const bool found = runtime_import(&runtime) == RUNTIME_ERROR_NONE && runtime.loader_path.length != 0;
    foreign_test_expect(&tally, found, "find system loader for raw driver tests", 0);
    if (found && foreign_driver_test_compile(&tally, directory, &files, arena))
    {
        const bool large = foreign_driver_test_large(&tally, directory, &files);
        foreign_driver_test_interpreter(&tally, directory, &runtime);
        foreign_driver_test_paths(&tally, directory, &files, arena, runtime.loader_path);
        foreign_driver_symbols_test(&tally, directory, &files, arena, runtime.loader_path);
        foreign_driver_test_cases(&tally, directory, &files, arena);
        foreign_driver_test_startup(&tally, directory, &files, arena);
        foreign_driver_test_shared(&tally, directory, &files, arena, runtime.loader_path);
        foreign_driver_test_time(&tally, directory, &files, arena);
        foreign_driver_test_unstarted(&tally, directory, &files, arena, large);
        foreign_driver_test_threads(&tally, directory, &files, arena, runtime.loader_path, large);
        foreign_driver_test_strings(&tally, directory, &files, arena);
        foreign_driver_test_dynamic(&tally, directory, &files, arena, system, runtime.loader_path);
        foreign_driver_test_plugin(&tally, directory, &files, arena);
        if (cross_check)
        {
            foreign_driver_test_oracles(&tally, directory, &files, arena, runtime.loader_path);
        }
    }
    runtime_release(&runtime);
    arena_pop_to(arena, saved);
    return foreign_test_report(&tally);
}
