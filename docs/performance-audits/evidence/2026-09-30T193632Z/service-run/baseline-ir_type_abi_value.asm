00000000000042b0 <ir_type_abi_value>:
    42b0:	55                   	push   %rbp
    42b1:	41 57                	push   %r15
    42b3:	41 56                	push   %r14
    42b5:	41 55                	push   %r13
    42b7:	41 54                	push   %r12
    42b9:	53                   	push   %rbx
    42ba:	48 81 ec 88 00 00 00 	sub    $0x88,%rsp
    42c1:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
    42c5:	c5 fc 11 47 18       	vmovups %ymm0,0x18(%rdi)
    42ca:	c5 fc 11 07          	vmovups %ymm0,(%rdi)
    42ce:	89 d3                	mov    %edx,%ebx
    42d0:	48 8b 16             	mov    (%rsi),%rdx
    42d3:	48 85 d2             	test   %rdx,%rdx
    42d6:	0f 84 8a 02 00 00    	je     4566 <ir_type_abi_value+0x2b6>
    42dc:	44 8b 86 30 01 00 00 	mov    0x130(%rsi),%r8d
    42e3:	44 39 c3             	cmp    %r8d,%ebx
    42e6:	0f 83 7a 02 00 00    	jae    4566 <ir_type_abi_value+0x2b6>
    42ec:	48 8b 86 38 01 00 00 	mov    0x138(%rsi),%rax
    42f3:	48 85 c0             	test   %rax,%rax
    42f6:	74 2e                	je     4326 <ir_type_abi_value+0x76>
    42f8:	c5 fc 11 44 24 28    	vmovups %ymm0,0x28(%rsp)
    42fe:	c5 fc 11 44 24 10    	vmovups %ymm0,0x10(%rsp)
    4304:	48 8b 96 40 01 00 00 	mov    0x140(%rsi),%rdx
    430b:	48 3b 96 28 01 00 00 	cmp    0x128(%rsi),%rdx
    4312:	0f 85 39 02 00 00    	jne    4551 <ir_type_abi_value+0x2a1>
    4318:	83 be 74 01 00 00 04 	cmpl   $0x4,0x174(%rsi)
    431f:	76 7b                	jbe    439c <ir_type_abi_value+0xec>
    4321:	e9 2b 02 00 00       	jmp    4551 <ir_type_abi_value+0x2a1>
    4326:	44 8b 8e 34 01 00 00 	mov    0x134(%rsi),%r9d
    432d:	48 8b 86 28 01 00 00 	mov    0x128(%rsi),%rax
    4334:	48 89 96 38 01 00 00 	mov    %rdx,0x138(%rsi)
    433b:	45 39 c1             	cmp    %r8d,%r9d
    433e:	48 89 86 40 01 00 00 	mov    %rax,0x140(%rsi)
    4345:	c5 fc 11 86 48 01 00 	vmovups %ymm0,0x148(%rsi)
    434c:	00 
    434d:	48 c7 86 68 01 00 00 	movq   $0x0,0x168(%rsi)
    4354:	00 00 00 00 
    4358:	48 89 d0             	mov    %rdx,%rax
    435b:	45 0f 47 c1          	cmova  %r9d,%r8d
    435f:	49 83 c0 3f          	add    $0x3f,%r8
    4363:	49 c1 e8 06          	shr    $0x6,%r8
    4367:	44 89 86 70 01 00 00 	mov    %r8d,0x170(%rsi)
    436e:	c7 86 74 01 00 00 00 	movl   $0x0,0x174(%rsi)
    4375:	00 00 00 
    4378:	48 c7 86 78 01 00 00 	movq   $0x0,0x178(%rsi)
    437f:	00 00 00 00 
    4383:	c5 fc 11 44 24 28    	vmovups %ymm0,0x28(%rsp)
    4389:	c5 fc 11 44 24 10    	vmovups %ymm0,0x10(%rsp)
    438f:	83 be 74 01 00 00 04 	cmpl   $0x4,0x174(%rsi)
    4396:	0f 87 b5 01 00 00    	ja     4551 <ir_type_abi_value+0x2a1>
    439c:	44 8b 86 70 01 00 00 	mov    0x170(%rsi),%r8d
    43a3:	89 dd                	mov    %ebx,%ebp
    43a5:	c1 ed 06             	shr    $0x6,%ebp
    43a8:	44 39 c5             	cmp    %r8d,%ebp
    43ab:	0f 83 a0 01 00 00    	jae    4551 <ir_type_abi_value+0x2a1>
    43b1:	41 89 cf             	mov    %ecx,%r15d
    43b4:	4a 8b 94 fe 48 01 00 	mov    0x148(%rsi,%r15,8),%rdx
    43bb:	00 
    43bc:	48 85 d2             	test   %rdx,%rdx
    43bf:	75 63                	jne    4424 <ir_type_abi_value+0x174>
    43c1:	49 c1 e0 03          	shl    $0x3,%r8
    43c5:	ba 08 00 00 00       	mov    $0x8,%edx
    43ca:	49 89 fe             	mov    %rdi,%r14
    43cd:	49 89 f4             	mov    %rsi,%r12
    43d0:	48 89 c7             	mov    %rax,%rdi
    43d3:	41 89 cd             	mov    %ecx,%r13d
    43d6:	4c 89 c6             	mov    %r8,%rsi
    43d9:	c5 f8 77             	vzeroupper
    43dc:	e8 5f d2 ff ff       	call   1640 <arena_allocate_bytes>
    43e1:	4b 89 84 fc 48 01 00 	mov    %rax,0x148(%r12,%r15,8)
    43e8:	00 
    43e9:	48 89 c7             	mov    %rax,%rdi
    43ec:	31 f6                	xor    %esi,%esi
    43ee:	41 8b 94 24 70 01 00 	mov    0x170(%r12),%edx
    43f5:	00 
    43f6:	48 c1 e2 03          	shl    $0x3,%rdx
    43fa:	e8 a1 cc ff ff       	call   10a0 <memset@plt>
    43ff:	41 8b 84 24 70 01 00 	mov    0x170(%r12),%eax
    4406:	00 
    4407:	44 89 e9             	mov    %r13d,%ecx
    440a:	4c 89 e6             	mov    %r12,%rsi
    440d:	4c 89 f7             	mov    %r14,%rdi
    4410:	48 c1 e0 03          	shl    $0x3,%rax
    4414:	49 01 84 24 60 01 00 	add    %rax,0x160(%r12)
    441b:	00 
    441c:	4b 8b 94 fc 48 01 00 	mov    0x148(%r12,%r15,8),%rdx
    4423:	00 
    4424:	41 89 ec             	mov    %ebp,%r12d
    4427:	4e 8b 34 e2          	mov    (%rdx,%r12,8),%r14
    442b:	4d 85 f6             	test   %r14,%r14
    442e:	75 51                	jne    4481 <ir_type_abi_value+0x1d1>
    4430:	48 8b 86 38 01 00 00 	mov    0x138(%rsi),%rax
    4437:	49 89 f6             	mov    %rsi,%r14
    443a:	be 08 0e 00 00       	mov    $0xe08,%esi
    443f:	ba 08 00 00 00       	mov    $0x8,%edx
    4444:	49 89 fd             	mov    %rdi,%r13
    4447:	89 cd                	mov    %ecx,%ebp
    4449:	48 89 c7             	mov    %rax,%rdi
    444c:	c5 f8 77             	vzeroupper
    444f:	e8 ec d1 ff ff       	call   1640 <arena_allocate_bytes>
    4454:	48 c7 80 00 0e 00 00 	movq   $0x0,0xe00(%rax)
    445b:	00 00 00 00 
    445f:	4c 89 f6             	mov    %r14,%rsi
    4462:	49 89 c6             	mov    %rax,%r14
    4465:	89 e9                	mov    %ebp,%ecx
    4467:	4c 89 ef             	mov    %r13,%rdi
    446a:	4a 8b 84 fe 48 01 00 	mov    0x148(%rsi,%r15,8),%rax
    4471:	00 
    4472:	4e 89 34 e0          	mov    %r14,(%rax,%r12,8)
    4476:	48 81 86 60 01 00 00 	addq   $0xe08,0x160(%rsi)
    447d:	08 0e 00 00 
    4481:	49 8b 86 00 0e 00 00 	mov    0xe00(%r14),%rax
    4488:	41 89 dc             	mov    %ebx,%r12d
    448b:	41 83 e4 3f          	and    $0x3f,%r12d
    448f:	48 0f a3 d8          	bt     %rbx,%rax
    4493:	73 22                	jae    44b7 <ir_type_abi_value+0x207>
    4495:	49 6b c4 38          	imul   $0x38,%r12,%rax
    4499:	c4 c1 7c 10 04 06    	vmovups (%r14,%rax,1),%ymm0
    449f:	c4 c1 7c 10 4c 06 18 	vmovups 0x18(%r14,%rax,1),%ymm1
    44a6:	c5 fc 11 4c 24 28    	vmovups %ymm1,0x28(%rsp)
    44ac:	c5 fc 11 44 24 10    	vmovups %ymm0,0x10(%rsp)
    44b2:	e9 9a 00 00 00       	jmp    4551 <ir_type_abi_value+0x2a1>
    44b7:	48 8b 86 28 01 00 00 	mov    0x128(%rsi),%rax
    44be:	89 da                	mov    %ebx,%edx
    44c0:	48 c1 e2 07          	shl    $0x7,%rdx
    44c4:	80 7c 10 48 01       	cmpb   $0x1,0x48(%rax,%rdx,1)
    44c9:	0f 85 82 00 00 00    	jne    4551 <ir_type_abi_value+0x2a1>
    44cf:	44 8b 96 74 01 00 00 	mov    0x174(%rsi),%r10d
    44d6:	44 0f b6 8e 78 01 00 	movzbl 0x178(%rsi),%r9d
    44dd:	00 
    44de:	b8 01 00 00 00       	mov    $0x1,%eax
    44e3:	49 6b ec 38          	imul   $0x38,%r12,%rbp
    44e7:	45 31 c0             	xor    %r8d,%r8d
    44ea:	85 c9                	test   %ecx,%ecx
    44ec:	48 8d 4c 24 50       	lea    0x50(%rsp),%rcx
    44f1:	49 89 fd             	mov    %rdi,%r13
    44f4:	49 89 f7             	mov    %rsi,%r15
    44f7:	89 da                	mov    %ebx,%edx
    44f9:	c4 e2 99 f7 c0       	shlx   %r12,%rax,%rax
    44fe:	41 0f 95 c0          	setne  %r8b
    4502:	48 89 cf             	mov    %rcx,%rdi
    4505:	48 89 44 24 08       	mov    %rax,0x8(%rsp)
    450a:	44 89 d1             	mov    %r10d,%ecx
    450d:	c5 f8 77             	vzeroupper
    4510:	e8 6b 00 00 00       	call   4580 <ir_classify_abi_value>
    4515:	c5 fc 10 44 24 50    	vmovups 0x50(%rsp),%ymm0
    451b:	c5 fc 10 4c 24 68    	vmovups 0x68(%rsp),%ymm1
    4521:	48 8b 44 24 08       	mov    0x8(%rsp),%rax
    4526:	4c 89 ef             	mov    %r13,%rdi
    4529:	c4 c1 7c 11 4c 2e 18 	vmovups %ymm1,0x18(%r14,%rbp,1)
    4530:	c4 c1 7c 11 04 2e    	vmovups %ymm0,(%r14,%rbp,1)
    4536:	49 09 86 00 0e 00 00 	or     %rax,0xe00(%r14)
    453d:	49 ff 87 68 01 00 00 	incq   0x168(%r15)
    4544:	49 85 86 00 0e 00 00 	test   %rax,0xe00(%r14)
    454b:	0f 85 44 ff ff ff    	jne    4495 <ir_type_abi_value+0x1e5>
    4551:	c5 fc 10 4c 24 28    	vmovups 0x28(%rsp),%ymm1
    4557:	c5 fc 10 44 24 10    	vmovups 0x10(%rsp),%ymm0
    455d:	c5 fc 11 4f 18       	vmovups %ymm1,0x18(%rdi)
    4562:	c5 fc 11 07          	vmovups %ymm0,(%rdi)
    4566:	48 81 c4 88 00 00 00 	add    $0x88,%rsp
    456d:	5b                   	pop    %rbx
    456e:	41 5c                	pop    %r12
    4570:	41 5d                	pop    %r13
    4572:	41 5e                	pop    %r14
    4574:	41 5f                	pop    %r15
    4576:	5d                   	pop    %rbp
    4577:	c5 f8 77             	vzeroupper
    457a:	c3                   	ret
    457b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
