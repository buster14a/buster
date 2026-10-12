	.file	"spin.c"
	.text
	.p2align 4
	.globl	spin
	.type	spin, @function
spin:
.LFB0:
	.cfi_startproc
	jmp	.L8
	.p2align 4
	.p2align 4,,10
	.p2align 3
.L3:
	movl	(%rdi), %eax
	movl	%eax, (%rdi)
.L8:
	movl	(%rdi), %eax
	testl	%eax, %eax
	jne	.L3
	ret
	.cfi_endproc
.LFE0:
	.size	spin, .-spin
	.ident	"GCC: (GNU) 16.2.1 20260810"
	.section	.note.GNU-stack,"",@progbits
