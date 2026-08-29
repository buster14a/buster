/* A complete assembly translation unit taken as driver input.
 *
 * It exercises the directive vocabulary a hand-written libc writes -- two
 * sections, `.globl`/`.weak`/`.hidden`/`.type`/`.size`, `.align`, the integer
 * and string data directives, `.cfi_*` accepted and dropped -- together with
 * the local numeric labels and the two relocation families that go with them.
 *
 * The program is its own answer: it exits zero only when the backward and
 * forward local labels, the PC-relative reference into the other section, and
 * the absolute pointer the linker had to relocate all came out right. Any one
 * of them wrong exits one.
 */

.section .rodata
.globl basic_asm_unit_table
.type basic_asm_unit_table,@object
.align 8
basic_asm_unit_table:
	.long 3
	.long 5
	.quad 0x1122334455667788
	.ascii "buster"
	.byte 0
	.short 1
.size basic_asm_unit_table,.-basic_asm_unit_table

.globl basic_asm_unit_pointer
.type basic_asm_unit_pointer,@object
basic_asm_unit_pointer:
	.quad basic_asm_unit_helper

/* Declared and never defined, the way a startup object names a symbol only a
   dynamic link would supply. Nothing references it, so no relocation depends
   on how an undefined weak resolves. */
.weak basic_asm_unit_weak
.hidden basic_asm_unit_weak

.text
.globl basic_asm_unit_helper
.hidden basic_asm_unit_helper
.type basic_asm_unit_helper,@function
basic_asm_unit_helper:
	xor %eax,%eax
	ret
.size basic_asm_unit_helper,.-basic_asm_unit_helper

.globl basic_asm_unit_start
.type basic_asm_unit_start,@function
basic_asm_unit_start:
	.cfi_startproc
	mov $2,%ecx
	xor %eax,%eax
1:	add $3,%eax                        # 1b below closes this loop
	dec %ecx
	jnz 1b
	cmp $6,%eax
	jne 2f
	lea basic_asm_unit_helper(%rip),%rdx
	cmp basic_asm_unit_pointer(%rip),%rdx
	jne 2f
	movl basic_asm_unit_table(%rip),%eax
	cmp $3,%eax
	jne 2f
	call basic_asm_unit_helper
	jmp 3f
2:	mov $1,%edi
	jmp 4f
3:	mov %eax,%edi
4:	mov $60,%eax                       /* SYS_exit */
	syscall
	hlt
	.cfi_endproc
.size basic_asm_unit_start,.-basic_asm_unit_start
