/* A directive outside the vocabulary. The driver must name it and the line it
   is on rather than dropping it and emitting an object that is quietly
   missing whatever the directive was there to do. */

.text
.globl basic_asm_unsupported
basic_asm_unsupported:
	ret
	.subsection 1
