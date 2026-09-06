
/workspace/scratch/0194f2e71ee3/scratch-kernels/final/benchmark:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

0000000000006990 <c_symbols_intern_tokens>:
    6990:	55                   	push   rbp
    6991:	41 57                	push   r15
    6993:	41 56                	push   r14
    6995:	41 55                	push   r13
    6997:	41 54                	push   r12
    6999:	53                   	push   rbx
    699a:	48 89 54 24 f0       	mov    QWORD PTR [rsp-0x10],rdx
    699f:	48 85 c9             	test   rcx,rcx
    69a2:	0f 84 b0 00 00 00    	je     6a58 <c_symbols_intern_tokens+0xc8>
    69a8:	4d 85 c0             	test   r8,r8
    69ab:	0f 84 17 01 00 00    	je     6ac8 <c_symbols_intern_tokens+0x138>
    69b1:	49 8d 50 ff          	lea    rdx,[r8-0x1]
    69b5:	48 89 54 24 f8       	mov    QWORD PTR [rsp-0x8],rdx
    69ba:	49 83 f8 41          	cmp    r8,0x41
    69be:	0f 83 12 01 00 00    	jae    6ad6 <c_symbols_intern_tokens+0x146>
    69c4:	31 c0                	xor    eax,eax
    69c6:	f6 44 24 f8 40       	test   BYTE PTR [rsp-0x8],0x40
    69cb:	0f 85 f7 00 00 00    	jne    6ac8 <c_symbols_intern_tokens+0x138>
    69d1:	49 29 c0             	sub    r8,rax
    69d4:	48 c7 c2 ff ff ff ff 	mov    rdx,0xffffffffffffffff
    69db:	49 83 f8 40          	cmp    r8,0x40
    69df:	c4 62 b9 f7 c2       	shlx   r8,rdx,r8
    69e4:	49 f7 d0             	not    r8
    69e7:	4c 0f 43 c2          	cmovae r8,rdx
    69eb:	c4 c1 fb 92 c8       	kmovq  k1,r8
    69f0:	62 f1 7f c9 6f 04 01 	vmovdqu8 zmm0{k1}{z},ZMMWORD PTR [rcx+rax*1]
    69f7:	62 f1 7d 48 74 05 bf 	vpcmpeqb k0,zmm0,ZMMWORD PTR [rip+0xdbf]        # 77c0 <_IO_stdin_used+0x7c0>
    69fe:	0d 00 00
    6a01:	c4 e1 f8 98 c0       	kortestq k0,k0
    6a06:	0f 84 bc 00 00 00    	je     6ac8 <c_symbols_intern_tokens+0x138>
    6a0c:	48 8b 54 24 f0       	mov    rdx,QWORD PTR [rsp-0x10]
    6a11:	48 8d 04 40          	lea    rax,[rax+rax*2]
    6a15:	c4 e1 fb 93 c8       	kmovq  rcx,k0
    6a1a:	48 8d 04 82          	lea    rax,[rdx+rax*4]
    6a1e:	66 90                	xchg   ax,ax
    6a20:	f3 48 0f bc d1       	tzcnt  rdx,rcx
    6a25:	4c 8b 57 08          	mov    r10,QWORD PTR [rdi+0x8]
    6a29:	4c 8b 0f             	mov    r9,QWORD PTR [rdi]
    6a2c:	48 8d 14 52          	lea    rdx,[rdx+rdx*2]
    6a30:	44 8b 04 90          	mov    r8d,DWORD PTR [rax+rdx*4]
    6a34:	4d 8d 5a 01          	lea    r11,[r10+0x1]
    6a38:	41 01 f0             	add    r8d,esi
    6a3b:	44 2b 47 10          	sub    r8d,DWORD PTR [rdi+0x10]
    6a3f:	4c 89 5f 08          	mov    QWORD PTR [rdi+0x8],r11
    6a43:	47 89 04 91          	mov    DWORD PTR [r9+r10*4],r8d
    6a47:	41 ff c0             	inc    r8d
    6a4a:	c4 e2 f0 f3 c9       	blsr   rcx,rcx
    6a4f:	44 89 44 90 04       	mov    DWORD PTR [rax+rdx*4+0x4],r8d
    6a54:	75 ca                	jne    6a20 <c_symbols_intern_tokens+0x90>
    6a56:	eb 70                	jmp    6ac8 <c_symbols_intern_tokens+0x138>
    6a58:	4d 85 c0             	test   r8,r8
    6a5b:	74 6b                	je     6ac8 <c_symbols_intern_tokens+0x138>
    6a5d:	44 89 c0             	mov    eax,r8d
    6a60:	83 e0 03             	and    eax,0x3
    6a63:	49 83 f8 04          	cmp    r8,0x4
    6a67:	0f 83 be 01 00 00    	jae    6c2b <c_symbols_intern_tokens+0x29b>
    6a6d:	31 c9                	xor    ecx,ecx
    6a6f:	48 85 c0             	test   rax,rax
    6a72:	74 54                	je     6ac8 <c_symbols_intern_tokens+0x138>
    6a74:	48 8b 54 24 f0       	mov    rdx,QWORD PTR [rsp-0x10]
    6a79:	48 8d 0c 49          	lea    rcx,[rcx+rcx*2]
    6a7d:	c1 e0 02             	shl    eax,0x2
    6a80:	48 8d 04 40          	lea    rax,[rax+rax*2]
    6a84:	48 8d 0c 8a          	lea    rcx,[rdx+rcx*4]
    6a88:	31 d2                	xor    edx,edx
    6a8a:	eb 0d                	jmp    6a99 <c_symbols_intern_tokens+0x109>
    6a8c:	0f 1f 40 00          	nop    DWORD PTR [rax+0x0]
    6a90:	48 83 c2 0c          	add    rdx,0xc
    6a94:	48 39 d0             	cmp    rax,rdx
    6a97:	74 2f                	je     6ac8 <c_symbols_intern_tokens+0x138>
    6a99:	80 7c 11 0a 02       	cmp    BYTE PTR [rcx+rdx*1+0xa],0x2
    6a9e:	75 f0                	jne    6a90 <c_symbols_intern_tokens+0x100>
    6aa0:	44 8b 04 11          	mov    r8d,DWORD PTR [rcx+rdx*1]
    6aa4:	4c 8b 57 08          	mov    r10,QWORD PTR [rdi+0x8]
    6aa8:	4c 8b 0f             	mov    r9,QWORD PTR [rdi]
    6aab:	41 01 f0             	add    r8d,esi
    6aae:	44 2b 47 10          	sub    r8d,DWORD PTR [rdi+0x10]
    6ab2:	4d 8d 5a 01          	lea    r11,[r10+0x1]
    6ab6:	4c 89 5f 08          	mov    QWORD PTR [rdi+0x8],r11
    6aba:	47 89 04 91          	mov    DWORD PTR [r9+r10*4],r8d
    6abe:	41 ff c0             	inc    r8d
    6ac1:	44 89 44 11 04       	mov    DWORD PTR [rcx+rdx*1+0x4],r8d
    6ac6:	eb c8                	jmp    6a90 <c_symbols_intern_tokens+0x100>
    6ac8:	5b                   	pop    rbx
    6ac9:	41 5c                	pop    r12
    6acb:	41 5d                	pop    r13
    6acd:	41 5e                	pop    r14
    6acf:	41 5f                	pop    r15
    6ad1:	5d                   	pop    rbp
    6ad2:	c5 f8 77             	vzeroupper
    6ad5:	c3                   	ret
    6ad6:	62 f2 7d 48 5a 05 c0 	vbroadcasti32x4 zmm0,XMMWORD PTR [rip+0xdc0]        # 78a0 <buster_x86_metadata_base64_pack_control+0xa0>
    6add:	0d 00 00
    6ae0:	48 c1 ea 06          	shr    rdx,0x6
    6ae4:	49 c7 c3 ff ff ff ff 	mov    r11,0xffffffffffffffff
    6aeb:	31 c0                	xor    eax,eax
    6aed:	31 db                	xor    ebx,ebx
    6aef:	48 ff c2             	inc    rdx
    6af2:	48 83 e2 fe          	and    rdx,0xfffffffffffffffe
    6af6:	eb 19                	jmp    6b11 <c_symbols_intern_tokens+0x181>
    6af8:	0f 1f 84 00 00 00 00 	nop    DWORD PTR [rax+rax*1+0x0]
    6aff:	00
    6b00:	48 83 e8 80          	sub    rax,0xffffffffffffff80
    6b04:	48 83 c3 02          	add    rbx,0x2
    6b08:	48 39 d3             	cmp    rbx,rdx
    6b0b:	0f 84 b5 fe ff ff    	je     69c6 <c_symbols_intern_tokens+0x36>
    6b11:	4d 89 c6             	mov    r14,r8
    6b14:	49 29 c6             	sub    r14,rax
    6b17:	49 83 fe 40          	cmp    r14,0x40
    6b1b:	c4 42 89 f7 f3       	shlx   r14,r11,r14
    6b20:	49 f7 d6             	not    r14
    6b23:	4d 0f 43 f3          	cmovae r14,r11
    6b27:	c4 c1 fb 92 ce       	kmovq  k1,r14
    6b2c:	62 f1 7f c9 6f 0c 01 	vmovdqu8 zmm1{k1}{z},ZMMWORD PTR [rcx+rax*1]
    6b33:	62 f1 75 48 74 c0    	vpcmpeqb k0,zmm1,zmm0
    6b39:	c4 e1 f8 98 c0       	kortestq k0,k0
    6b3e:	74 54                	je     6b94 <c_symbols_intern_tokens+0x204>
    6b40:	4c 8b 4c 24 f0       	mov    r9,QWORD PTR [rsp-0x10]
    6b45:	4c 8d 3c 40          	lea    r15,[rax+rax*2]
    6b49:	c4 61 fb 93 f0       	kmovq  r14,k0
    6b4e:	4f 8d 3c b9          	lea    r15,[r9+r15*4]
    6b52:	66 66 66 66 66 2e 0f 	data16 data16 data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
    6b59:	1f 84 00 00 00 00 00
    6b60:	f3 4d 0f bc e6       	tzcnt  r12,r14
    6b65:	4c 8b 4f 08          	mov    r9,QWORD PTR [rdi+0x8]
    6b69:	4c 8b 2f             	mov    r13,QWORD PTR [rdi]
    6b6c:	4f 8d 24 64          	lea    r12,[r12+r12*2]
    6b70:	43 8b 2c a7          	mov    ebp,DWORD PTR [r15+r12*4]
    6b74:	4d 8d 51 01          	lea    r10,[r9+0x1]
    6b78:	01 f5                	add    ebp,esi
    6b7a:	2b 6f 10             	sub    ebp,DWORD PTR [rdi+0x10]
    6b7d:	4c 89 57 08          	mov    QWORD PTR [rdi+0x8],r10
    6b81:	43 89 6c 8d 00       	mov    DWORD PTR [r13+r9*4+0x0],ebp
    6b86:	ff c5                	inc    ebp
    6b88:	c4 c2 88 f3 ce       	blsr   r14,r14
    6b8d:	43 89 6c a7 04       	mov    DWORD PTR [r15+r12*4+0x4],ebp
    6b92:	75 cc                	jne    6b60 <c_symbols_intern_tokens+0x1d0>
    6b94:	49 89 c7             	mov    r15,rax
    6b97:	49 83 cf 40          	or     r15,0x40
    6b9b:	4d 89 c1             	mov    r9,r8
    6b9e:	4d 29 f9             	sub    r9,r15
    6ba1:	49 83 f9 40          	cmp    r9,0x40
    6ba5:	c4 42 b1 f7 cb       	shlx   r9,r11,r9
    6baa:	49 f7 d1             	not    r9
    6bad:	4d 0f 43 cb          	cmovae r9,r11
    6bb1:	c4 c1 fb 92 c9       	kmovq  k1,r9
    6bb6:	62 f1 7f c9 6f 4c 01 	vmovdqu8 zmm1{k1}{z},ZMMWORD PTR [rcx+rax*1+0x40]
    6bbd:	01
    6bbe:	62 f1 75 48 74 c0    	vpcmpeqb k0,zmm1,zmm0
    6bc4:	c4 e1 f8 98 c0       	kortestq k0,k0
    6bc9:	0f 84 31 ff ff ff    	je     6b00 <c_symbols_intern_tokens+0x170>
    6bcf:	4c 8b 54 24 f0       	mov    r10,QWORD PTR [rsp-0x10]
    6bd4:	4f 8d 0c 7f          	lea    r9,[r15+r15*2]
    6bd8:	c4 61 fb 93 f0       	kmovq  r14,k0
    6bdd:	4f 8d 3c 8a          	lea    r15,[r10+r9*4]
    6be1:	66 66 66 66 66 66 2e 	data16 data16 data16 data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
    6be8:	0f 1f 84 00 00 00 00
    6bef:	00
    6bf0:	f3 4d 0f bc ce       	tzcnt  r9,r14
    6bf5:	4c 8b 6f 08          	mov    r13,QWORD PTR [rdi+0x8]
    6bf9:	4c 8b 27             	mov    r12,QWORD PTR [rdi]
    6bfc:	4f 8d 0c 49          	lea    r9,[r9+r9*2]
    6c00:	47 8b 14 8f          	mov    r10d,DWORD PTR [r15+r9*4]
    6c04:	49 8d 6d 01          	lea    rbp,[r13+0x1]
    6c08:	41 01 f2             	add    r10d,esi
    6c0b:	44 2b 57 10          	sub    r10d,DWORD PTR [rdi+0x10]
    6c0f:	48 89 6f 08          	mov    QWORD PTR [rdi+0x8],rbp
    6c13:	47 89 14 ac          	mov    DWORD PTR [r12+r13*4],r10d
    6c17:	41 ff c2             	inc    r10d
    6c1a:	c4 c2 88 f3 ce       	blsr   r14,r14
    6c1f:	47 89 54 8f 04       	mov    DWORD PTR [r15+r9*4+0x4],r10d
    6c24:	75 ca                	jne    6bf0 <c_symbols_intern_tokens+0x260>
    6c26:	e9 d5 fe ff ff       	jmp    6b00 <c_symbols_intern_tokens+0x170>
    6c2b:	4c 8b 4c 24 f0       	mov    r9,QWORD PTR [rsp-0x10]
    6c30:	49 83 e0 fc          	and    r8,0xfffffffffffffffc
    6c34:	31 c9                	xor    ecx,ecx
    6c36:	eb 19                	jmp    6c51 <c_symbols_intern_tokens+0x2c1>
    6c38:	0f 1f 84 00 00 00 00 	nop    DWORD PTR [rax+rax*1+0x0]
    6c3f:	00
    6c40:	48 83 c1 04          	add    rcx,0x4
    6c44:	49 83 c1 30          	add    r9,0x30
    6c48:	49 39 c8             	cmp    r8,rcx
    6c4b:	0f 84 1e fe ff ff    	je     6a6f <c_symbols_intern_tokens+0xdf>
    6c51:	41 80 79 0a 02       	cmp    BYTE PTR [r9+0xa],0x2
    6c56:	74 18                	je     6c70 <c_symbols_intern_tokens+0x2e0>
    6c58:	41 80 79 16 02       	cmp    BYTE PTR [r9+0x16],0x2
    6c5d:	74 39                	je     6c98 <c_symbols_intern_tokens+0x308>
    6c5f:	41 80 79 22 02       	cmp    BYTE PTR [r9+0x22],0x2
    6c64:	74 5b                	je     6cc1 <c_symbols_intern_tokens+0x331>
    6c66:	41 80 79 2e 02       	cmp    BYTE PTR [r9+0x2e],0x2
    6c6b:	75 d3                	jne    6c40 <c_symbols_intern_tokens+0x2b0>
    6c6d:	eb 7f                	jmp    6cee <c_symbols_intern_tokens+0x35e>
    6c6f:	90                   	nop
    6c70:	41 8b 11             	mov    edx,DWORD PTR [r9]
    6c73:	4c 8b 5f 08          	mov    r11,QWORD PTR [rdi+0x8]
    6c77:	4c 8b 17             	mov    r10,QWORD PTR [rdi]
    6c7a:	01 f2                	add    edx,esi
    6c7c:	2b 57 10             	sub    edx,DWORD PTR [rdi+0x10]
    6c7f:	49 8d 5b 01          	lea    rbx,[r11+0x1]
    6c83:	48 89 5f 08          	mov    QWORD PTR [rdi+0x8],rbx
    6c87:	43 89 14 9a          	mov    DWORD PTR [r10+r11*4],edx
    6c8b:	ff c2                	inc    edx
    6c8d:	41 89 51 04          	mov    DWORD PTR [r9+0x4],edx
    6c91:	41 80 79 16 02       	cmp    BYTE PTR [r9+0x16],0x2
    6c96:	75 c7                	jne    6c5f <c_symbols_intern_tokens+0x2cf>
    6c98:	41 8b 51 0c          	mov    edx,DWORD PTR [r9+0xc]
    6c9c:	4c 8b 5f 08          	mov    r11,QWORD PTR [rdi+0x8]
    6ca0:	4c 8b 17             	mov    r10,QWORD PTR [rdi]
    6ca3:	01 f2                	add    edx,esi
    6ca5:	2b 57 10             	sub    edx,DWORD PTR [rdi+0x10]
    6ca8:	49 8d 5b 01          	lea    rbx,[r11+0x1]
    6cac:	48 89 5f 08          	mov    QWORD PTR [rdi+0x8],rbx
    6cb0:	43 89 14 9a          	mov    DWORD PTR [r10+r11*4],edx
    6cb4:	ff c2                	inc    edx
    6cb6:	41 89 51 10          	mov    DWORD PTR [r9+0x10],edx
    6cba:	41 80 79 22 02       	cmp    BYTE PTR [r9+0x22],0x2
    6cbf:	75 a5                	jne    6c66 <c_symbols_intern_tokens+0x2d6>
    6cc1:	41 8b 51 18          	mov    edx,DWORD PTR [r9+0x18]
    6cc5:	4c 8b 5f 08          	mov    r11,QWORD PTR [rdi+0x8]
    6cc9:	4c 8b 17             	mov    r10,QWORD PTR [rdi]
    6ccc:	01 f2                	add    edx,esi
    6cce:	2b 57 10             	sub    edx,DWORD PTR [rdi+0x10]
    6cd1:	49 8d 5b 01          	lea    rbx,[r11+0x1]
    6cd5:	48 89 5f 08          	mov    QWORD PTR [rdi+0x8],rbx
    6cd9:	43 89 14 9a          	mov    DWORD PTR [r10+r11*4],edx
    6cdd:	ff c2                	inc    edx
    6cdf:	41 89 51 1c          	mov    DWORD PTR [r9+0x1c],edx
    6ce3:	41 80 79 2e 02       	cmp    BYTE PTR [r9+0x2e],0x2
    6ce8:	0f 85 52 ff ff ff    	jne    6c40 <c_symbols_intern_tokens+0x2b0>
    6cee:	41 8b 51 24          	mov    edx,DWORD PTR [r9+0x24]
    6cf2:	4c 8b 5f 08          	mov    r11,QWORD PTR [rdi+0x8]
    6cf6:	4c 8b 17             	mov    r10,QWORD PTR [rdi]
    6cf9:	01 f2                	add    edx,esi
    6cfb:	2b 57 10             	sub    edx,DWORD PTR [rdi+0x10]
    6cfe:	49 8d 5b 01          	lea    rbx,[r11+0x1]
    6d02:	48 89 5f 08          	mov    QWORD PTR [rdi+0x8],rbx
    6d06:	43 89 14 9a          	mov    DWORD PTR [r10+r11*4],edx
    6d0a:	ff c2                	inc    edx
    6d0c:	41 89 51 28          	mov    DWORD PTR [r9+0x28],edx
    6d10:	e9 2b ff ff ff       	jmp    6c40 <c_symbols_intern_tokens+0x2b0>

Disassembly of section .fini:
