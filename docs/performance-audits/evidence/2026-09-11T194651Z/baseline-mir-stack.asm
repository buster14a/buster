
/tmp/issue37-predicate-measurement/baseline-mir-stack.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <predicate_mask_chain>:
   0:	55                   	push   %rbp
   1:	48 89 e5             	mov    %rsp,%rbp
   4:	48 81 ec 80 01 00 00 	sub    $0x180,%rsp
   b:	f6 04 24 00          	testb  $0x0,(%rsp)
   f:	48 89 f8             	mov    %rdi,%rax
  12:	48 89 85 f8 ff ff ff 	mov    %rax,-0x8(%rbp)
  19:	48 89 f0             	mov    %rsi,%rax
  1c:	48 89 85 f0 ff ff ff 	mov    %rax,-0x10(%rbp)
  23:	48 8b 8d f0 ff ff ff 	mov    -0x10(%rbp),%rcx
  2a:	62 f1 7f 48 6f 01    	vmovdqu8 (%rcx),%zmm0
  30:	62 f1 7f 48 7f 85 b0 	vmovdqu8 %zmm0,-0x50(%rbp)
  37:	ff ff ff
  3a:	b8 03 00 00 00       	mov    $0x3,%eax
  3f:	48 89 85 a8 ff ff ff 	mov    %rax,-0x58(%rbp)
  46:	48 8b 8d a8 ff ff ff 	mov    -0x58(%rbp),%rcx
  4d:	48 0f b6 c1          	movzbq %cl,%rax
  51:	48 89 85 a0 ff ff ff 	mov    %rax,-0x60(%rbp)
  58:	48 8b 8d a0 ff ff ff 	mov    -0x60(%rbp),%rcx
  5f:	62 f2 7d 48 7a c1    	vpbroadcastb %ecx,%zmm0
  65:	62 f1 7f 48 7f 85 60 	vmovdqu8 %zmm0,-0xa0(%rbp)
  6c:	ff ff ff
  6f:	62 f1 7f 48 6f 8d b0 	vmovdqu8 -0x50(%rbp),%zmm1
  76:	ff ff ff
  79:	62 f1 7f 48 6f 95 60 	vmovdqu8 -0xa0(%rbp),%zmm2
  80:	ff ff ff
  83:	62 f1 75 48 74 ca    	vpcmpeqb %zmm2,%zmm1,%k1
  89:	c4 e1 fb 93 c1       	kmovq  %k1,%rax
  8e:	48 89 85 58 ff ff ff 	mov    %rax,-0xa8(%rbp)
  95:	b8 07 00 00 00       	mov    $0x7,%eax
  9a:	48 89 85 50 ff ff ff 	mov    %rax,-0xb0(%rbp)
  a1:	48 8b 8d 50 ff ff ff 	mov    -0xb0(%rbp),%rcx
  a8:	48 0f b6 c1          	movzbq %cl,%rax
  ac:	48 89 85 48 ff ff ff 	mov    %rax,-0xb8(%rbp)
  b3:	48 8b 8d 48 ff ff ff 	mov    -0xb8(%rbp),%rcx
  ba:	62 f2 7d 48 7a c1    	vpbroadcastb %ecx,%zmm0
  c0:	62 f1 7f 48 7f 45 fc 	vmovdqu8 %zmm0,-0x100(%rbp)
  c7:	62 f1 7f 48 6f 8d b0 	vmovdqu8 -0x50(%rbp),%zmm1
  ce:	ff ff ff
  d1:	62 f1 7f 48 6f 55 fc 	vmovdqu8 -0x100(%rbp),%zmm2
  d8:	62 f1 75 48 74 ca    	vpcmpeqb %zmm2,%zmm1,%k1
  de:	c4 e1 fb 93 c1       	kmovq  %k1,%rax
  e3:	48 89 85 f8 fe ff ff 	mov    %rax,-0x108(%rbp)
  ea:	b8 0b 00 00 00       	mov    $0xb,%eax
  ef:	48 89 85 f0 fe ff ff 	mov    %rax,-0x110(%rbp)
  f6:	48 8b 8d f0 fe ff ff 	mov    -0x110(%rbp),%rcx
  fd:	48 0f b6 c1          	movzbq %cl,%rax
 101:	48 89 85 e8 fe ff ff 	mov    %rax,-0x118(%rbp)
 108:	48 8b 8d e8 fe ff ff 	mov    -0x118(%rbp),%rcx
 10f:	62 f2 7d 48 7a c1    	vpbroadcastb %ecx,%zmm0
 115:	62 f1 7f 48 7f 85 a0 	vmovdqu8 %zmm0,-0x160(%rbp)
 11c:	fe ff ff
 11f:	62 f1 7f 48 6f 8d b0 	vmovdqu8 -0x50(%rbp),%zmm1
 126:	ff ff ff
 129:	62 f1 7f 48 6f 95 a0 	vmovdqu8 -0x160(%rbp),%zmm2
 130:	fe ff ff
 133:	62 f3 75 48 3e ca 01 	vpcmpltub %zmm2,%zmm1,%k1
 13a:	c4 e1 fb 93 c1       	kmovq  %k1,%rax
 13f:	48 89 85 98 fe ff ff 	mov    %rax,-0x168(%rbp)
 146:	48 8b 85 58 ff ff ff 	mov    -0xa8(%rbp),%rax
 14d:	48 8b 95 f8 fe ff ff 	mov    -0x108(%rbp),%rdx
 154:	48 09 d0             	or     %rdx,%rax
 157:	48 89 85 90 fe ff ff 	mov    %rax,-0x170(%rbp)
 15e:	48 8b 85 90 fe ff ff 	mov    -0x170(%rbp),%rax
 165:	48 8b 95 98 fe ff ff 	mov    -0x168(%rbp),%rdx
 16c:	48 21 d0             	and    %rdx,%rax
 16f:	48 89 85 88 fe ff ff 	mov    %rax,-0x178(%rbp)
 176:	48 8b 85 f8 ff ff ff 	mov    -0x8(%rbp),%rax
 17d:	48 8b 8d 88 fe ff ff 	mov    -0x178(%rbp),%rcx
 184:	62 f1 7f 48 6f 95 b0 	vmovdqu8 -0x50(%rbp),%zmm2
 18b:	ff ff ff
 18e:	c4 e1 fb 92 c9       	kmovq  %rcx,%k1
 193:	62 f1 7f 49 7f 10    	vmovdqu8 %zmm2,(%rax){%k1}
 199:	b8 00 00 00 00       	mov    $0x0,%eax
 19e:	48 89 85 80 fe ff ff 	mov    %rax,-0x180(%rbp)
 1a5:	c5 f8 77             	vzeroupper
 1a8:	48 89 ec             	mov    %rbp,%rsp
 1ab:	5d                   	pop    %rbp
 1ac:	c3                   	ret
 1ad:	90                   	nop
 1ae:	90                   	nop
 1af:	90                   	nop

00000000000001b0 <predicate_word_boundary>:
 1b0:	55                   	push   %rbp
 1b1:	48 89 e5             	mov    %rsp,%rbp
 1b4:	48 83 ec 60          	sub    $0x60,%rsp
 1b8:	f6 04 24 00          	testb  $0x0,(%rsp)
 1bc:	48 89 f8             	mov    %rdi,%rax
 1bf:	48 89 85 f8 ff ff ff 	mov    %rax,-0x8(%rbp)
 1c6:	48 8b 8d f8 ff ff ff 	mov    -0x8(%rbp),%rcx
 1cd:	62 f1 7f 48 6f 01    	vmovdqu8 (%rcx),%zmm0
 1d3:	62 f1 7f 48 7f 85 b0 	vmovdqu8 %zmm0,-0x50(%rbp)
 1da:	ff ff ff
 1dd:	62 f1 7f 48 6f 8d b0 	vmovdqu8 -0x50(%rbp),%zmm1
 1e4:	ff ff ff
 1e7:	62 f1 7f 48 6f 95 b0 	vmovdqu8 -0x50(%rbp),%zmm2
 1ee:	ff ff ff
 1f1:	62 f1 75 48 76 ca    	vpcmpeqd %zmm2,%zmm1,%k1
 1f7:	c4 e1 fb 93 c1       	kmovq  %k1,%rax
 1fc:	48 89 85 a8 ff ff ff 	mov    %rax,-0x58(%rbp)
 203:	48 8b 85 a8 ff ff ff 	mov    -0x58(%rbp),%rax
 20a:	c5 f8 77             	vzeroupper
 20d:	48 89 ec             	mov    %rbp,%rsp
 210:	5d                   	pop    %rbp
 211:	c3                   	ret
