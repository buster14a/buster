/* Original input for buster#3166. Build twice with WIN=1 and WIN=0. */
#if !WIN && UNIQUE
#define PICK other_pick3166
#else
#define PICK pick3166
#endif
.section .text.PICK,"axG",@progbits,PICK,comdat
.weak PICK
.type PICK,@function
PICK:
    .cfi_startproc
#if WIN
    mov $40, %eax
#else
    mov $99, %eax
#endif
    ret
    .cfi_endproc
.size PICK, .-PICK

.text
#if WIN
.globl _start
.type _start,@function
_start:
    .cfi_startproc
    call pick3166
    mov %eax, %edi
#if BOUND
    movabs $__start_table3166, %rcx
#else
    movabs $refs3166, %rcx
#endif
    call *(%rcx)
    add %eax, %edi
    mov $60, %eax
    syscall
    .cfi_endproc
.size _start, .-_start
#else
.globl kept3166
.type kept3166,@function
kept3166:
    .cfi_startproc
    mov $2, %eax
    ret
    .cfi_endproc
.size kept3166, .-kept3166

/* Both labels are in the retained CIE. The two interior expressions
   designate offsets 12 and 8, respectively, in the original frame. */
.section .eh_frame,"a",@progbits
.globl middle3166, other3166
.set middle3166, . + 8
.set other3166, . + 12

#if BOUND
.section table3166,"a",@progbits
#else
.section .rodata.refs3166,"a",@progbits
#endif
.balign 8
.globl refs3166
refs3166:
.rept REFS
    .quad kept3166
.endr
    .quad middle3166
    .reloc ., R_X86_64_NONE, middle3166+7
    .quad 0
    .quad kept3166+1
#if MODE == 1 || MODE == 4
    .quad middle3166+4
    .long other3166-.-4
#elif MODE == 2
    .long other3166-.-4
    .quad middle3166+4
#elif MODE == 3
    /* The eight-byte absolute relocation has just four bytes to patch. */
    .reloc ., R_X86_64_64, kept3166
    .long 0
#endif
#endif
.section .note.GNU-stack,"",@progbits
