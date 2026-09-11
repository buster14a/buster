
/workspace/scratch/0194f2e71ee3/scratch-kernels/final/benchmark:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

0000000000005f60 <buster_x86_metadata_decode_base64_chunk_avx512>:
    5f60:	48 8d 04 8d 00 00 00 	lea    rax,[rcx*4+0x0]
    5f67:	00
    5f68:	48 39 c2             	cmp    rdx,rax
    5f6b:	49 89 c0             	mov    r8,rax
    5f6e:	4c 0f 42 c2          	cmovb  r8,rdx
    5f72:	48 85 c9             	test   rcx,rcx
    5f75:	0f 84 f5 00 00 00    	je     6070 <buster_x86_metadata_decode_base64_chunk_avx512+0x110>
    5f7b:	53                   	push   rbx
    5f7c:	62 f1 fd 48 6f 05 ba 	vmovdqa64 zmm0,ZMMWORD PTR [rip+0x41ba]        # a140 <buster_x86_metadata_base64_values>
    5f83:	41 00 00
    5f86:	62 f1 fd 48 6f 0d f0 	vmovdqa64 zmm1,ZMMWORD PTR [rip+0x41f0]        # a180 <buster_x86_metadata_base64_values+0x40>
    5f8d:	41 00 00
    5f90:	62 f1 fd 48 6f 15 66 	vmovdqa64 zmm2,ZMMWORD PTR [rip+0x1866]        # 7800 <buster_x86_metadata_base64_pack_control>
    5f97:	18 00 00
    5f9a:	62 f2 7d 48 5a 25 dc 	vbroadcasti32x4 zmm4,XMMWORD PTR [rip+0x18dc]        # 7880 <buster_x86_metadata_base64_pack_control+0x80>
    5fa1:	18 00 00
    5fa4:	62 f2 7d 48 5a 2d e2 	vbroadcasti32x4 zmm5,XMMWORD PTR [rip+0x18e2]        # 7890 <buster_x86_metadata_base64_pack_control+0x90>
    5fab:	18 00 00
    5fae:	62 f2 fd 48 59 35 98 	vpbroadcastq zmm6,QWORD PTR [rip+0x1898]        # 7850 <buster_x86_metadata_base64_pack_control+0x50>
    5fb5:	18 00 00
    5fb8:	49 c7 c1 ff ff ff ff 	mov    r9,0xffffffffffffffff
    5fbf:	31 c9                	xor    ecx,ecx
    5fc1:	c5 e1 ef db          	vpxor  xmm3,xmm3,xmm3
    5fc5:	49 89 c2             	mov    r10,rax
    5fc8:	eb 64                	jmp    602e <buster_x86_metadata_decode_base64_chunk_avx512+0xce>
    5fca:	66 0f 1f 44 00 00    	nop    WORD PTR [rax+rax*1+0x0]
    5fd0:	62 f3 45 48 3f cb 05 	vpcmpnltb k1,zmm7,zmm3
    5fd7:	41 c1 eb 02          	shr    r11d,0x2
    5fdb:	48 89 cb             	mov    rbx,rcx
    5fde:	48 c1 eb 02          	shr    rbx,0x2
    5fe2:	48 83 c1 40          	add    rcx,0x40
    5fe6:	49 83 c2 c0          	add    r10,0xffffffffffffffc0
    5fea:	49 83 c0 c0          	add    r8,0xffffffffffffffc0
    5fee:	62 f2 7d c9 75 f9    	vpermi2b zmm7{k1}{z},zmm0,zmm1
    5ff4:	47 8d 1c 5b          	lea    r11d,[r11+r11*2]
    5ff8:	48 8d 1c 5b          	lea    rbx,[rbx+rbx*2]
    5ffc:	c4 42 a1 f7 d9       	shlx   r11,r9,r11
    6001:	49 f7 d3             	not    r11
    6004:	c4 c1 fb 92 cb       	kmovq  k1,r11
    6009:	62 72 dd 48 83 c7    	vpmultishiftqb zmm8,zmm4,zmm7
    600f:	62 f2 d5 48 83 ff    	vpmultishiftqb zmm7,zmm5,zmm7
    6015:	62 f3 bd 48 25 fe d8 	vpternlogq zmm7,zmm8,zmm6,0xd8
    601c:	62 f2 6d 48 8d ff    	vpermb zmm7,zmm2,zmm7
    6022:	62 f1 7f 49 7f 3c 1f 	vmovdqu8 ZMMWORD PTR [rdi+rbx*1]{k1},zmm7
    6029:	48 39 c1             	cmp    rcx,rax
    602c:	73 41                	jae    606f <buster_x86_metadata_decode_base64_chunk_avx512+0x10f>
    602e:	49 83 fa 40          	cmp    r10,0x40
    6032:	41 bb 40 00 00 00    	mov    r11d,0x40
    6038:	c5 c1 ef ff          	vpxor  xmm7,xmm7,xmm7
    603c:	4d 0f 42 da          	cmovb  r11,r10
    6040:	48 39 ca             	cmp    rdx,rcx
    6043:	76 8b                	jbe    5fd0 <buster_x86_metadata_decode_base64_chunk_avx512+0x70>
    6045:	49 83 f8 40          	cmp    r8,0x40
    6049:	bb 40 00 00 00       	mov    ebx,0x40
    604e:	49 0f 42 d8          	cmovb  rbx,r8
    6052:	c4 c2 e1 f7 d9       	shlx   rbx,r9,rbx
    6057:	48 f7 d3             	not    rbx
    605a:	49 0f 43 d9          	cmovae rbx,r9
    605e:	c4 e1 fb 92 cb       	kmovq  k1,rbx
    6063:	62 f1 7f c9 6f 3c 0e 	vmovdqu8 zmm7{k1}{z},ZMMWORD PTR [rsi+rcx*1]
    606a:	e9 61 ff ff ff       	jmp    5fd0 <buster_x86_metadata_decode_base64_chunk_avx512+0x70>
    606f:	5b                   	pop    rbx
    6070:	c5 f8 77             	vzeroupper
    6073:	c3                   	ret

Disassembly of section .fini:
