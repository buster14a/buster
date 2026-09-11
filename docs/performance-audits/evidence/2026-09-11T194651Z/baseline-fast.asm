
/tmp/issue37-predicate-measurement/baseline-fast.o:     file format elf64-x86-64


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
  27:	c4 e1 fb 93 d1       	kmovq  %k1,%rdx
  2c:	41 b8 07 00 00 00    	mov    $0x7,%r8d
  32:	4d 0f b6 c8          	movzbq %r8b,%r9
  36:	62 d2 7d 48 7a d1    	vpbroadcastb %r9d,%zmm2
  3c:	62 f1 7d 48 74 ca    	vpcmpeqb %zmm2,%zmm0,%k1
  42:	c4 61 fb 93 d1       	kmovq  %k1,%r10
  47:	41 bb 0b 00 00 00    	mov    $0xb,%r11d
  4d:	49 0f b6 c3          	movzbq %r11b,%rax
  51:	62 f2 7d 48 7a d8    	vpbroadcastb %eax,%zmm3
  57:	62 f3 7d 48 3e cb 01 	vpcmpltub %zmm3,%zmm0,%k1
  5e:	c4 e1 fb 93 c1       	kmovq  %k1,%rax
  63:	48 89 85 f8 ff ff ff 	mov    %rax,-0x8(%rbp)
  6a:	48 89 d0             	mov    %rdx,%rax
  6d:	4c 89 d2             	mov    %r10,%rdx
  70:	48 09 d0             	or     %rdx,%rax
  73:	48 89 85 f0 ff ff ff 	mov    %rax,-0x10(%rbp)
  7a:	48 89 c0             	mov    %rax,%rax
  7d:	48 8b 95 f8 ff ff ff 	mov    -0x8(%rbp),%rdx
  84:	48 21 d0             	and    %rdx,%rax
  87:	c4 e1 fb 92 c8       	kmovq  %rax,%k1
  8c:	62 f1 7f 49 7f 07    	vmovdqu8 %zmm0,(%rdi){%k1}
  92:	41 ba 00 00 00 00    	mov    $0x0,%r10d
  98:	c5 f8 77             	vzeroupper
  9b:	48 89 ec             	mov    %rbp,%rsp
  9e:	5d                   	pop    %rbp
  9f:	c3                   	ret

00000000000000a0 <predicate_word_boundary>:
  a0:	55                   	push   %rbp
  a1:	48 89 e5             	mov    %rsp,%rbp
  a4:	48 83 ec 10          	sub    $0x10,%rsp
  a8:	f6 04 24 00          	testb  $0x0,(%rsp)
  ac:	62 f1 7f 48 6f 07    	vmovdqu8 (%rdi),%zmm0
  b2:	62 f1 7d 48 76 c8    	vpcmpeqd %zmm0,%zmm0,%k1
  b8:	c4 e1 fb 93 c1       	kmovq  %k1,%rax
  bd:	48 89 85 f8 ff ff ff 	mov    %rax,-0x8(%rbp)
  c4:	c5 f8 77             	vzeroupper
  c7:	48 89 ec             	mov    %rbp,%rsp
  ca:	5d                   	pop    %rbp
  cb:	c3                   	ret
