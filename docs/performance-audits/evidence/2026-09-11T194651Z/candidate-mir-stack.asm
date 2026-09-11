
/tmp/issue37-predicate-measurement/candidate-mir-stack.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <predicate_mask_chain>:
   0:	55                   	push   %rbp
   1:	48 89 e5             	mov    %rsp,%rbp
   4:	48 81 ec a0 01 00 00 	sub    $0x1a0,%rsp
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
  89:	c4 e1 f8 91 8d 88 fe 	kmovq  %k1,-0x178(%rbp)
  90:	ff ff
  92:	b8 07 00 00 00       	mov    $0x7,%eax
  97:	48 89 85 58 ff ff ff 	mov    %rax,-0xa8(%rbp)
  9e:	48 8b 8d 58 ff ff ff 	mov    -0xa8(%rbp),%rcx
  a5:	48 0f b6 c1          	movzbq %cl,%rax
  a9:	48 89 85 50 ff ff ff 	mov    %rax,-0xb0(%rbp)
  b0:	48 8b 8d 50 ff ff ff 	mov    -0xb0(%rbp),%rcx
  b7:	62 f2 7d 48 7a c1    	vpbroadcastb %ecx,%zmm0
  bd:	62 f1 7f 48 7f 85 10 	vmovdqu8 %zmm0,-0xf0(%rbp)
  c4:	ff ff ff
  c7:	62 f1 7f 48 6f 8d b0 	vmovdqu8 -0x50(%rbp),%zmm1
  ce:	ff ff ff
  d1:	62 f1 7f 48 6f 95 10 	vmovdqu8 -0xf0(%rbp),%zmm2
  d8:	ff ff ff
  db:	62 f1 75 48 74 ca    	vpcmpeqb %zmm2,%zmm1,%k1
  e1:	c4 e1 f8 91 8d 80 fe 	kmovq  %k1,-0x180(%rbp)
  e8:	ff ff
  ea:	b8 0b 00 00 00       	mov    $0xb,%eax
  ef:	48 89 85 08 ff ff ff 	mov    %rax,-0xf8(%rbp)
  f6:	48 8b 8d 08 ff ff ff 	mov    -0xf8(%rbp),%rcx
  fd:	48 0f b6 c1          	movzbq %cl,%rax
 101:	48 89 85 00 ff ff ff 	mov    %rax,-0x100(%rbp)
 108:	48 8b 8d 00 ff ff ff 	mov    -0x100(%rbp),%rcx
 10f:	62 f2 7d 48 7a c1    	vpbroadcastb %ecx,%zmm0
 115:	62 f1 7f 48 7f 45 fb 	vmovdqu8 %zmm0,-0x140(%rbp)
 11c:	62 f1 7f 48 6f 8d b0 	vmovdqu8 -0x50(%rbp),%zmm1
 123:	ff ff ff
 126:	62 f1 7f 48 6f 55 fb 	vmovdqu8 -0x140(%rbp),%zmm2
 12d:	62 f3 75 48 3e ca 01 	vpcmpltub %zmm2,%zmm1,%k1
 134:	c4 e1 f8 91 8d 78 fe 	kmovq  %k1,-0x188(%rbp)
 13b:	ff ff
 13d:	c4 e1 f8 90 8d 88 fe 	kmovq  -0x178(%rbp),%k1
 144:	ff ff
 146:	c4 e1 f8 90 95 80 fe 	kmovq  -0x180(%rbp),%k2
 14d:	ff ff
 14f:	c4 e1 f4 45 da       	korq   %k2,%k1,%k3
 154:	c4 e1 f8 91 9d 70 fe 	kmovq  %k3,-0x190(%rbp)
 15b:	ff ff
 15d:	c4 e1 f8 90 8d 70 fe 	kmovq  -0x190(%rbp),%k1
 164:	ff ff
 166:	c4 e1 f8 90 95 78 fe 	kmovq  -0x188(%rbp),%k2
 16d:	ff ff
 16f:	c4 e1 f4 41 da       	kandq  %k2,%k1,%k3
 174:	c4 e1 f8 91 9d 68 fe 	kmovq  %k3,-0x198(%rbp)
 17b:	ff ff
 17d:	48 8b 85 f8 ff ff ff 	mov    -0x8(%rbp),%rax
 184:	62 f1 7f 48 6f 95 b0 	vmovdqu8 -0x50(%rbp),%zmm2
 18b:	ff ff ff
 18e:	c4 e1 f8 90 8d 68 fe 	kmovq  -0x198(%rbp),%k1
 195:	ff ff
 197:	62 f1 7f 49 7f 10    	vmovdqu8 %zmm2,(%rax){%k1}
 19d:	b8 00 00 00 00       	mov    $0x0,%eax
 1a2:	48 89 85 b8 fe ff ff 	mov    %rax,-0x148(%rbp)
 1a9:	c5 f8 77             	vzeroupper
 1ac:	48 89 ec             	mov    %rbp,%rsp
 1af:	5d                   	pop    %rbp
 1b0:	c3                   	ret
 1b1:	90                   	nop
 1b2:	90                   	nop
 1b3:	90                   	nop
 1b4:	90                   	nop
 1b5:	90                   	nop
 1b6:	90                   	nop
 1b7:	90                   	nop
 1b8:	90                   	nop
 1b9:	90                   	nop
 1ba:	90                   	nop
 1bb:	90                   	nop
 1bc:	90                   	nop
 1bd:	90                   	nop
 1be:	90                   	nop
 1bf:	90                   	nop

00000000000001c0 <predicate_word_boundary>:
 1c0:	55                   	push   %rbp
 1c1:	48 89 e5             	mov    %rsp,%rbp
 1c4:	48 83 ec 70          	sub    $0x70,%rsp
 1c8:	f6 04 24 00          	testb  $0x0,(%rsp)
 1cc:	48 89 f8             	mov    %rdi,%rax
 1cf:	48 89 85 f8 ff ff ff 	mov    %rax,-0x8(%rbp)
 1d6:	48 8b 8d f8 ff ff ff 	mov    -0x8(%rbp),%rcx
 1dd:	62 f1 7f 48 6f 01    	vmovdqu8 (%rcx),%zmm0
 1e3:	62 f1 7f 48 7f 85 b0 	vmovdqu8 %zmm0,-0x50(%rbp)
 1ea:	ff ff ff
 1ed:	62 f1 7f 48 6f 8d b0 	vmovdqu8 -0x50(%rbp),%zmm1
 1f4:	ff ff ff
 1f7:	62 f1 7f 48 6f 95 b0 	vmovdqu8 -0x50(%rbp),%zmm2
 1fe:	ff ff ff
 201:	62 f1 75 48 76 ca    	vpcmpeqd %zmm2,%zmm1,%k1
 207:	c4 e1 f8 91 4d 98    	kmovq  %k1,-0x68(%rbp)
 20d:	c4 e1 f8 90 4d 98    	kmovq  -0x68(%rbp),%k1
 213:	c5 f8 93 c1          	kmovw  %k1,%eax
 217:	48 89 85 a8 ff ff ff 	mov    %rax,-0x58(%rbp)
 21e:	48 8b 85 a8 ff ff ff 	mov    -0x58(%rbp),%rax
 225:	c5 f8 77             	vzeroupper
 228:	48 89 ec             	mov    %rbp,%rsp
 22b:	5d                   	pop    %rbp
 22c:	c3                   	ret
