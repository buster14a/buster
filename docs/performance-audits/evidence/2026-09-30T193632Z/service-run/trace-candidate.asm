0000000000004f30 <audit_query_trace>:
    4f30:	55                   	push   %rbp
    4f31:	41 57                	push   %r15
    4f33:	41 56                	push   %r14
    4f35:	41 55                	push   %r13
    4f37:	41 54                	push   %r12
    4f39:	53                   	push   %rbx
    4f3a:	48 83 ec 68          	sub    $0x68,%rsp
    4f3e:	b8 04 00 00 00       	mov    $0x4,%eax
    4f43:	49 89 fe             	mov    %rdi,%r14
    4f46:	85 f6                	test   %esi,%esi
    4f48:	75 0f                	jne    4f59 <audit_query_trace+0x29>
    4f4a:	41 8b 86 30 01 00 00 	mov    0x130(%r14),%eax
    4f51:	85 c0                	test   %eax,%eax
    4f53:	0f 84 63 02 00 00    	je     51bc <audit_query_trace+0x28c>
    4f59:	49 8d 96 38 01 00 00 	lea    0x138(%r14),%rdx
    4f60:	49 8d 8e 48 01 00 00 	lea    0x148(%r14),%rcx
    4f67:	4c 8d 3d 22 52 00 00 	lea    0x5222(%rip),%r15        # a190 <_fini+0x8e4>
    4f6e:	41 89 c1             	mov    %eax,%r9d
    4f71:	31 db                	xor    %ebx,%ebx
    4f73:	31 c0                	xor    %eax,%eax
    4f75:	89 74 24 0c          	mov    %esi,0xc(%rsp)
    4f79:	4c 89 4c 24 10       	mov    %r9,0x10(%rsp)
    4f7e:	48 89 4c 24 20       	mov    %rcx,0x20(%rsp)
    4f83:	48 89 54 24 18       	mov    %rdx,0x18(%rsp)
    4f88:	eb 1e                	jmp    4fa8 <audit_query_trace+0x78>
    4f8a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    4f90:	31 c9                	xor    %ecx,%ecx
    4f92:	41 01 cc             	add    %ecx,%r12d
    4f95:	4c 01 e0             	add    %r12,%rax
    4f98:	48 ff c3             	inc    %rbx
    4f9b:	49 83 c7 04          	add    $0x4,%r15
    4f9f:	49 39 d9             	cmp    %rbx,%r9
    4fa2:	0f 84 16 02 00 00    	je     51be <audit_query_trace+0x28e>
    4fa8:	40 84 f6             	test   %sil,%sil
    4fab:	74 13                	je     4fc0 <audit_query_trace+0x90>
    4fad:	41 8b 2f             	mov    (%r15),%ebp
    4fb0:	4d 8b 16             	mov    (%r14),%r10
    4fb3:	4d 85 d2             	test   %r10,%r10
    4fb6:	75 12                	jne    4fca <audit_query_trace+0x9a>
    4fb8:	eb de                	jmp    4f98 <audit_query_trace+0x68>
    4fba:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    4fc0:	89 dd                	mov    %ebx,%ebp
    4fc2:	4d 8b 16             	mov    (%r14),%r10
    4fc5:	4d 85 d2             	test   %r10,%r10
    4fc8:	74 ce                	je     4f98 <audit_query_trace+0x68>
    4fca:	41 8b 8e 30 01 00 00 	mov    0x130(%r14),%ecx
    4fd1:	41 89 ed             	mov    %ebp,%r13d
    4fd4:	41 c1 ed 06          	shr    $0x6,%r13d
    4fd8:	39 cd                	cmp    %ecx,%ebp
    4fda:	0f 83 f0 00 00 00    	jae    50d0 <audit_query_trace+0x1a0>
    4fe0:	48 83 3a 00          	cmpq   $0x0,(%rdx)
    4fe4:	74 30                	je     5016 <audit_query_trace+0xe6>
    4fe6:	49 8b 8e 40 01 00 00 	mov    0x140(%r14),%rcx
    4fed:	49 3b 8e 28 01 00 00 	cmp    0x128(%r14),%rcx
    4ff4:	0f 85 d6 00 00 00    	jne    50d0 <audit_query_trace+0x1a0>
    4ffa:	41 83 be 74 01 00 00 	cmpl   $0x4,0x174(%r14)
    5001:	04 
    5002:	0f 87 c8 00 00 00    	ja     50d0 <audit_query_trace+0x1a0>
    5008:	45 3b ae 70 01 00 00 	cmp    0x170(%r14),%r13d
    500f:	72 6a                	jb     507b <audit_query_trace+0x14b>
    5011:	e9 ba 00 00 00       	jmp    50d0 <audit_query_trace+0x1a0>
    5016:	4d 8b 86 28 01 00 00 	mov    0x128(%r14),%r8
    501d:	41 8b be 34 01 00 00 	mov    0x134(%r14),%edi
    5024:	4d 89 96 38 01 00 00 	mov    %r10,0x138(%r14)
    502b:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
    502f:	4d 89 86 40 01 00 00 	mov    %r8,0x140(%r14)
    5036:	4c 8b 44 24 20       	mov    0x20(%rsp),%r8
    503b:	39 cf                	cmp    %ecx,%edi
    503d:	0f 47 cf             	cmova  %edi,%ecx
    5040:	48 83 c1 3f          	add    $0x3f,%rcx
    5044:	48 c1 e9 06          	shr    $0x6,%rcx
    5048:	c4 c1 7c 11 00       	vmovups %ymm0,(%r8)
    504d:	49 c7 40 20 00 00 00 	movq   $0x0,0x20(%r8)
    5054:	00 
    5055:	41 89 8e 70 01 00 00 	mov    %ecx,0x170(%r14)
    505c:	41 c7 86 74 01 00 00 	movl   $0x0,0x174(%r14)
    5063:	00 00 00 00 
    5067:	49 c7 86 78 01 00 00 	movq   $0x0,0x178(%r14)
    506e:	00 00 00 00 
    5072:	45 3b ae 70 01 00 00 	cmp    0x170(%r14),%r13d
    5079:	73 55                	jae    50d0 <audit_query_trace+0x1a0>
    507b:	48 8d 7c 24 30       	lea    0x30(%rsp),%rdi
    5080:	45 31 e4             	xor    %r12d,%r12d
    5083:	4c 89 f6             	mov    %r14,%rsi
    5086:	89 e9                	mov    %ebp,%ecx
    5088:	45 31 c0             	xor    %r8d,%r8d
    508b:	48 89 44 24 28       	mov    %rax,0x28(%rsp)
    5090:	c5 f8 77             	vzeroupper
    5093:	e8 08 34 00 00       	call   84a0 <ir_abi_context_value_validated>
    5098:	8b 4c 24 60          	mov    0x60(%rsp),%ecx
    509c:	48 8b 44 24 28       	mov    0x28(%rsp),%rax
    50a1:	4c 8b 4c 24 10       	mov    0x10(%rsp),%r9
    50a6:	48 8b 54 24 18       	mov    0x18(%rsp),%rdx
    50ab:	8b 74 24 0c          	mov    0xc(%rsp),%esi
    50af:	4d 8b 16             	mov    (%r14),%r10
    50b2:	03 4c 24 38          	add    0x38(%rsp),%ecx
    50b6:	48 01 c8             	add    %rcx,%rax
    50b9:	b9 00 00 00 00       	mov    $0x0,%ecx
    50be:	4d 85 d2             	test   %r10,%r10
    50c1:	0f 84 cb fe ff ff    	je     4f92 <audit_query_trace+0x62>
    50c7:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
    50ce:	00 00 
    50d0:	41 8b 8e 30 01 00 00 	mov    0x130(%r14),%ecx
    50d7:	45 31 e4             	xor    %r12d,%r12d
    50da:	39 cd                	cmp    %ecx,%ebp
    50dc:	0f 83 ae fe ff ff    	jae    4f90 <audit_query_trace+0x60>
    50e2:	48 83 3a 00          	cmpq   $0x0,(%rdx)
    50e6:	74 31                	je     5119 <audit_query_trace+0x1e9>
    50e8:	49 8b 8e 40 01 00 00 	mov    0x140(%r14),%rcx
    50ef:	49 3b 8e 28 01 00 00 	cmp    0x128(%r14),%rcx
    50f6:	0f 85 94 fe ff ff    	jne    4f90 <audit_query_trace+0x60>
    50fc:	41 83 be 74 01 00 00 	cmpl   $0x4,0x174(%r14)
    5103:	04 
    5104:	0f 87 86 fe ff ff    	ja     4f90 <audit_query_trace+0x60>
    510a:	45 3b ae 70 01 00 00 	cmp    0x170(%r14),%r13d
    5111:	0f 83 79 fe ff ff    	jae    4f90 <audit_query_trace+0x60>
    5117:	eb 69                	jmp    5182 <audit_query_trace+0x252>
    5119:	4d 8b 86 28 01 00 00 	mov    0x128(%r14),%r8
    5120:	41 8b be 34 01 00 00 	mov    0x134(%r14),%edi
    5127:	4d 89 96 38 01 00 00 	mov    %r10,0x138(%r14)
    512e:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
    5132:	4d 89 86 40 01 00 00 	mov    %r8,0x140(%r14)
    5139:	4c 8b 44 24 20       	mov    0x20(%rsp),%r8
    513e:	39 cf                	cmp    %ecx,%edi
    5140:	0f 47 cf             	cmova  %edi,%ecx
    5143:	48 83 c1 3f          	add    $0x3f,%rcx
    5147:	48 c1 e9 06          	shr    $0x6,%rcx
    514b:	c4 c1 7c 11 00       	vmovups %ymm0,(%r8)
    5150:	49 c7 40 20 00 00 00 	movq   $0x0,0x20(%r8)
    5157:	00 
    5158:	41 89 8e 70 01 00 00 	mov    %ecx,0x170(%r14)
    515f:	41 c7 86 74 01 00 00 	movl   $0x0,0x174(%r14)
    5166:	00 00 00 00 
    516a:	49 c7 86 78 01 00 00 	movq   $0x0,0x178(%r14)
    5171:	00 00 00 00 
    5175:	45 3b ae 70 01 00 00 	cmp    0x170(%r14),%r13d
    517c:	0f 83 0e fe ff ff    	jae    4f90 <audit_query_trace+0x60>
    5182:	48 8d 7c 24 30       	lea    0x30(%rsp),%rdi
    5187:	41 b8 01 00 00 00    	mov    $0x1,%r8d
    518d:	4c 89 f6             	mov    %r14,%rsi
    5190:	89 e9                	mov    %ebp,%ecx
    5192:	49 89 c4             	mov    %rax,%r12
    5195:	c5 f8 77             	vzeroupper
    5198:	e8 03 33 00 00       	call   84a0 <ir_abi_context_value_validated>
    519d:	4c 89 e0             	mov    %r12,%rax
    51a0:	4c 8b 4c 24 10       	mov    0x10(%rsp),%r9
    51a5:	48 8b 54 24 18       	mov    0x18(%rsp),%rdx
    51aa:	8b 74 24 0c          	mov    0xc(%rsp),%esi
    51ae:	44 8b 64 24 38       	mov    0x38(%rsp),%r12d
    51b3:	8b 4c 24 60          	mov    0x60(%rsp),%ecx
    51b7:	e9 d6 fd ff ff       	jmp    4f92 <audit_query_trace+0x62>
    51bc:	31 c0                	xor    %eax,%eax
    51be:	48 83 c4 68          	add    $0x68,%rsp
    51c2:	5b                   	pop    %rbx
    51c3:	41 5c                	pop    %r12
    51c5:	41 5d                	pop    %r13
    51c7:	41 5e                	pop    %r14
    51c9:	41 5f                	pop    %r15
    51cb:	5d                   	pop    %rbp
    51cc:	c5 f8 77             	vzeroupper
    51cf:	c3                   	ret
