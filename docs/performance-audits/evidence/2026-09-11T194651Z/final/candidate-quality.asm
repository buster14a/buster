
/tmp/issue37-predicate-final/candidate-quality.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <predicate_mask_chain>:
   0:	55                   	push   %rbp
   1:	48 89 e5             	mov    %rsp,%rbp
   4:	48 83 ec 10          	sub    $0x10,%rsp
   8:	f6 04 24 00          	testb  $0x0,(%rsp)
   c:	62 f1 7f 48 6f 06    	vmovdqu8 (%rsi),%zmm0
  12:	b8 03 00 00 00       	mov    $0x3,%eax
  17:	48 0f b6 c8          	movzbq %al,%rcx
  1b:	62 f2 7d 48 7a c9    	vpbroadcastb %ecx,%zmm1
  21:	62 f1 7d 48 74 c9    	vpcmpeqb %zmm1,%zmm0,%k1
  27:	ba 07 00 00 00       	mov    $0x7,%edx
  2c:	4c 0f b6 c2          	movzbq %dl,%r8
  30:	62 d2 7d 48 7a d0    	vpbroadcastb %r8d,%zmm2
  36:	62 f1 7d 48 74 d2    	vpcmpeqb %zmm2,%zmm0,%k2
  3c:	41 b9 0b 00 00 00    	mov    $0xb,%r9d
  42:	4d 0f b6 d1          	movzbq %r9b,%r10
  46:	62 d2 7d 48 7a da    	vpbroadcastb %r10d,%zmm3
  4c:	62 f3 7d 48 3e db 01 	vpcmpltub %zmm3,%zmm0,%k3
  53:	c4 e1 f4 45 e2       	korq   %k2,%k1,%k4
  58:	c4 e1 dc 41 eb       	kandq  %k3,%k4,%k5
  5d:	62 f1 7f 4d 7f 07    	vmovdqu8 %zmm0,(%rdi){%k5}
  63:	41 bb 00 00 00 00    	mov    $0x0,%r11d
  69:	c5 f8 77             	vzeroupper
  6c:	48 89 ec             	mov    %rbp,%rsp
  6f:	5d                   	pop    %rbp
  70:	c3                   	ret
  71:	90                   	nop
  72:	90                   	nop
  73:	90                   	nop
  74:	90                   	nop
  75:	90                   	nop
  76:	90                   	nop
  77:	90                   	nop
  78:	90                   	nop
  79:	90                   	nop
  7a:	90                   	nop
  7b:	90                   	nop
  7c:	90                   	nop
  7d:	90                   	nop
  7e:	90                   	nop
  7f:	90                   	nop

0000000000000080 <predicate_word_boundary>:
  80:	55                   	push   %rbp
  81:	48 89 e5             	mov    %rsp,%rbp
  84:	48 83 ec 10          	sub    $0x10,%rsp
  88:	f6 04 24 00          	testb  $0x0,(%rsp)
  8c:	62 f1 7f 48 6f 07    	vmovdqu8 (%rdi),%zmm0
  92:	62 f1 7d 48 76 c8    	vpcmpeqd %zmm0,%zmm0,%k1
  98:	c5 f8 93 c1          	kmovw  %k1,%eax
  9c:	48 89 85 f8 ff ff ff 	mov    %rax,-0x8(%rbp)
  a3:	c5 f8 77             	vzeroupper
  a6:	48 89 ec             	mov    %rbp,%rsp
  a9:	5d                   	pop    %rbp
  aa:	c3                   	ret
