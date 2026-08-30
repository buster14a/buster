.text
.globl basic_asm_abs32_function
basic_asm_abs32_function:
	mov $7, %eax
	ret
.globl basic_asm_abs32_slot
basic_asm_abs32_slot:
	.long basic_asm_abs32_function
