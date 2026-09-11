
/workspace/scratch/0194f2e71ee3/scratch-kernels/final/benchmark:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

0000000000006390 <c_ir_decode_quoted>:
    6390:	48 85 d2             	test   rdx,rdx
    6393:	74 3f                	je     63d4 <c_ir_decode_quoted+0x44>
    6395:	55                   	push   rbp
    6396:	41 57                	push   r15
    6398:	41 56                	push   r14
    639a:	41 55                	push   r13
    639c:	41 54                	push   r12
    639e:	53                   	push   rbx
    639f:	48 83 ec 18          	sub    rsp,0x18
    63a3:	48 89 cb             	mov    rbx,rcx
    63a6:	49 89 d7             	mov    r15,rdx
    63a9:	49 89 f4             	mov    r12,rsi
    63ac:	49 89 fe             	mov    r14,rdi
    63af:	31 c9                	xor    ecx,ecx
    63b1:	66 66 66 66 66 66 2e 	data16 data16 data16 data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
    63b8:	0f 1f 84 00 00 00 00
    63bf:	00
    63c0:	41 80 3c 0c 22       	cmp    BYTE PTR [r12+rcx*1],0x22
    63c5:	74 10                	je     63d7 <c_ir_decode_quoted+0x47>
    63c7:	48 ff c1             	inc    rcx
    63ca:	49 39 cf             	cmp    r15,rcx
    63cd:	75 f1                	jne    63c0 <c_ir_decode_quoted+0x30>
    63cf:	e9 5c 01 00 00       	jmp    6530 <c_ir_decode_quoted+0x1a0>
    63d4:	31 c0                	xor    eax,eax
    63d6:	c3                   	ret
    63d7:	48 85 c9             	test   rcx,rcx
    63da:	74 2e                	je     640a <c_ir_decode_quoted+0x7a>
    63dc:	48 83 f9 02          	cmp    rcx,0x2
    63e0:	0f 85 4a 01 00 00    	jne    6530 <c_ir_decode_quoted+0x1a0>
    63e6:	41 80 3c 24 75       	cmp    BYTE PTR [r12],0x75
    63eb:	0f 85 3f 01 00 00    	jne    6530 <c_ir_decode_quoted+0x1a0>
    63f1:	31 c0                	xor    eax,eax
    63f3:	49 83 ff 04          	cmp    r15,0x4
    63f7:	0f 82 35 01 00 00    	jb     6532 <c_ir_decode_quoted+0x1a2>
    63fd:	41 80 7c 24 01 38    	cmp    BYTE PTR [r12+0x1],0x38
    6403:	74 0f                	je     6414 <c_ir_decode_quoted+0x84>
    6405:	e9 28 01 00 00       	jmp    6532 <c_ir_decode_quoted+0x1a2>
    640a:	49 83 ff 01          	cmp    r15,0x1
    640e:	0f 84 1c 01 00 00    	je     6530 <c_ir_decode_quoted+0x1a0>
    6414:	43 80 7c 3c ff 22    	cmp    BYTE PTR [r12+r15*1-0x1],0x22
    641a:	0f 85 10 01 00 00    	jne    6530 <c_ir_decode_quoted+0x1a0>
    6420:	48 ff c1             	inc    rcx
    6423:	4d 8d 6f ff          	lea    r13,[r15-0x1]
    6427:	48 c7 44 24 08 00 00 	mov    QWORD PTR [rsp+0x8],0x0
    642e:	00 00
    6430:	4c 39 e9             	cmp    rcx,r13
    6433:	0f 83 e7 00 00 00    	jae    6520 <c_ir_decode_quoted+0x190>
    6439:	48 c7 c5 ff ff ff ff 	mov    rbp,0xffffffffffffffff
    6440:	eb 44                	jmp    6486 <c_ir_decode_quoted+0xf6>
    6442:	66 66 66 66 66 2e 0f 	data16 data16 data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
    6449:	1f 84 00 00 00 00 00
    6450:	48 83 f8 40          	cmp    rax,0x40
    6454:	bf 40 00 00 00       	mov    edi,0x40
    6459:	62 f1 7f 49 7f 06    	vmovdqu8 ZMMWORD PTR [rsi]{k1},zmm0
    645f:	48 0f 43 c7          	cmovae rax,rdi
    6463:	48 01 c2             	add    rdx,rax
    6466:	48 01 c1             	add    rcx,rax
    6469:	b0 01                	mov    al,0x1
    646b:	48 89 54 24 08       	mov    QWORD PTR [rsp+0x8],rdx
    6470:	48 89 4c 24 10       	mov    QWORD PTR [rsp+0x10],rcx
    6475:	84 c0                	test   al,al
    6477:	0f 84 9f 00 00 00    	je     651c <c_ir_decode_quoted+0x18c>
    647d:	4c 39 e9             	cmp    rcx,r13
    6480:	0f 83 96 00 00 00    	jae    651c <c_ir_decode_quoted+0x18c>
    6486:	4c 89 e8             	mov    rax,r13
    6489:	48 29 c8             	sub    rax,rcx
    648c:	c4 e2 f9 f7 d5       	shlx   rdx,rbp,rax
    6491:	48 83 f8 40          	cmp    rax,0x40
    6495:	48 f7 d2             	not    rdx
    6498:	48 0f 43 d5          	cmovae rdx,rbp
    649c:	c4 e1 fb 92 ca       	kmovq  k1,rdx
    64a1:	48 8b 54 24 08       	mov    rdx,QWORD PTR [rsp+0x8]
    64a6:	62 d1 7f c9 6f 04 0c 	vmovdqu8 zmm0{k1}{z},ZMMWORD PTR [r12+rcx*1]
    64ad:	62 f1 7d 48 74 05 49 	vpcmpeqb k0,zmm0,ZMMWORD PTR [rip+0x1149]        # 7600 <_IO_stdin_used+0x600>
    64b4:	11 00 00
    64b7:	49 8d 34 16          	lea    rsi,[r14+rdx*1]
    64bb:	c4 e1 f8 98 c0       	kortestq k0,k0
    64c0:	74 8e                	je     6450 <c_ir_decode_quoted+0xc0>
    64c2:	c4 e1 fb 93 c0       	kmovq  rax,k0
    64c7:	4c 8d 4c 24 08       	lea    r9,[rsp+0x8]
    64cc:	4d 89 f8             	mov    r8,r15
    64cf:	f3 48 0f bc c0       	tzcnt  rax,rax
    64d4:	c4 e2 f9 f7 fd       	shlx   rdi,rbp,rax
    64d9:	48 01 c2             	add    rdx,rax
    64dc:	48 8d 44 01 01       	lea    rax,[rcx+rax*1+0x1]
    64e1:	4c 89 f1             	mov    rcx,r14
    64e4:	48 f7 d7             	not    rdi
    64e7:	c4 e1 fb 92 cf       	kmovq  k1,rdi
    64ec:	4c 89 e7             	mov    rdi,r12
    64ef:	62 f1 7f 49 7f 06    	vmovdqu8 ZMMWORD PTR [rsi]{k1},zmm0
    64f5:	48 89 54 24 08       	mov    QWORD PTR [rsp+0x8],rdx
    64fa:	48 8d 54 24 10       	lea    rdx,[rsp+0x10]
    64ff:	4c 89 ee             	mov    rsi,r13
    6502:	48 89 44 24 10       	mov    QWORD PTR [rsp+0x10],rax
    6507:	c5 f8 77             	vzeroupper
    650a:	e8 51 01 00 00       	call   6660 <c_ir_decode_escape>
    650f:	48 8b 4c 24 10       	mov    rcx,QWORD PTR [rsp+0x10]
    6514:	84 c0                	test   al,al
    6516:	0f 85 61 ff ff ff    	jne    647d <c_ir_decode_quoted+0xed>
    651c:	84 c0                	test   al,al
    651e:	74 10                	je     6530 <c_ir_decode_quoted+0x1a0>
    6520:	48 8b 44 24 08       	mov    rax,QWORD PTR [rsp+0x8]
    6525:	4c 89 33             	mov    QWORD PTR [rbx],r14
    6528:	48 89 43 08          	mov    QWORD PTR [rbx+0x8],rax
    652c:	b0 01                	mov    al,0x1
    652e:	eb 02                	jmp    6532 <c_ir_decode_quoted+0x1a2>
    6530:	31 c0                	xor    eax,eax
    6532:	48 83 c4 18          	add    rsp,0x18
    6536:	5b                   	pop    rbx
    6537:	41 5c                	pop    r12
    6539:	41 5d                	pop    r13
    653b:	41 5e                	pop    r14
    653d:	41 5f                	pop    r15
    653f:	5d                   	pop    rbp
    6540:	c5 f8 77             	vzeroupper
    6543:	c3                   	ret

Disassembly of section .fini:
