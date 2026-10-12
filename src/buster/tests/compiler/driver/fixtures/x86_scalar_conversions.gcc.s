	.file	"conversions.c"
	.text
	.p2align 4
	.globl	l2d
	.type	l2d, @function
l2d:
.LFB0:
	.cfi_startproc
	pxor	%xmm0, %xmm0
	cvtsi2sdq	%rdi, %xmm0
	ret
	.cfi_endproc
.LFE0:
	.size	l2d, .-l2d
	.p2align 4
	.globl	i2f
	.type	i2f, @function
i2f:
.LFB1:
	.cfi_startproc
	pxor	%xmm0, %xmm0
	cvtsi2ssl	%edi, %xmm0
	ret
	.cfi_endproc
.LFE1:
	.size	i2f, .-i2f
	.ident	"GCC: (GNU) 16.2.1 20260810"
	.section	.note.GNU-stack,"",@progbits
