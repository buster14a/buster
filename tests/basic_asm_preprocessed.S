/* A `.S` is assembly the C preprocessor runs over first. This driver does not
   run it over assembly, so the input is refused by name; the macro below is
   what such a file is for and what a refusal has to avoid mis-assembling. */

#define BASIC_ASM_PREPROCESSED_STATUS 0

.text
.globl basic_asm_preprocessed
basic_asm_preprocessed:
	mov $BASIC_ASM_PREPROCESSED_STATUS,%eax
	ret
