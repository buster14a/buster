00000000000084a0 <ir_abi_context_value_validated>:
    84a0:	55                   	push   %rbp
    84a1:	41 57                	push   %r15
    84a3:	41 56                	push   %r14
    84a5:	41 55                	push   %r13
    84a7:	41 54                	push   %r12
    84a9:	53                   	push   %rbx
    84aa:	48 81 ec 88 00 00 00 	sub    $0x88,%rsp
    84b1:	45 89 c4             	mov    %r8d,%r12d
    84b4:	4a 8b 44 e2 10       	mov    0x10(%rdx,%r12,8),%rax
    84b9:	89 cd                	mov    %ecx,%ebp
    84bb:	49 89 d6             	mov    %rdx,%r14
    84be:	48 85 c0             	test   %rax,%rax
    84c1:	0f 84 50 01 00 00    	je     8617 <ir_abi_context_value_validated+0x177>
    84c7:	89 eb                	mov    %ebp,%ebx
    84c9:	c1 eb 06             	shr    $0x6,%ebx
    84cc:	41 89 ef             	mov    %ebp,%r15d
    84cf:	41 83 e7 3f          	and    $0x3f,%r15d
    84d3:	48 8b 0c d8          	mov    (%rax,%rbx,8),%rcx
    84d7:	48 85 c9             	test   %rcx,%rcx
    84da:	74 2a                	je     8506 <ir_abi_context_value_validated+0x66>
    84dc:	48 8b 91 00 0e 00 00 	mov    0xe00(%rcx),%rdx
    84e3:	4c 0f a3 fa          	bt     %r15,%rdx
    84e7:	73 1d                	jae    8506 <ir_abi_context_value_validated+0x66>
    84e9:	49 6b c7 38          	imul   $0x38,%r15,%rax
    84ed:	c5 fc 10 04 01       	vmovups (%rcx,%rax,1),%ymm0
    84f2:	c5 fc 10 4c 01 18    	vmovups 0x18(%rcx,%rax,1),%ymm1
    84f8:	c5 fc 11 4f 18       	vmovups %ymm1,0x18(%rdi)
    84fd:	c5 fc 11 07          	vmovups %ymm0,(%rdi)
    8501:	e9 fc 00 00 00       	jmp    8602 <ir_abi_context_value_validated+0x162>
    8506:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
    850a:	c5 fc 11 44 24 28    	vmovups %ymm0,0x28(%rsp)
    8510:	44 89 44 24 04       	mov    %r8d,0x4(%rsp)
    8515:	48 89 7c 24 08       	mov    %rdi,0x8(%rsp)
    851a:	c5 fc 11 44 24 10    	vmovups %ymm0,0x10(%rsp)
    8520:	4c 8b 2c d8          	mov    (%rax,%rbx,8),%r13
    8524:	4d 85 ed             	test   %r13,%r13
    8527:	75 3a                	jne    8563 <ir_abi_context_value_validated+0xc3>
    8529:	49 8b 3e             	mov    (%r14),%rdi
    852c:	49 89 f5             	mov    %rsi,%r13
    852f:	be 08 0e 00 00       	mov    $0xe08,%esi
    8534:	ba 08 00 00 00       	mov    $0x8,%edx
    8539:	c5 f8 77             	vzeroupper
    853c:	e8 ff 90 ff ff       	call   1640 <arena_allocate_bytes>
    8541:	48 c7 80 00 0e 00 00 	movq   $0x0,0xe00(%rax)
    8548:	00 00 00 00 
    854c:	4c 89 ee             	mov    %r13,%rsi
    854f:	49 89 c5             	mov    %rax,%r13
    8552:	4b 8b 44 e6 10       	mov    0x10(%r14,%r12,8),%rax
    8557:	4c 89 2c d8          	mov    %r13,(%rax,%rbx,8)
    855b:	49 81 46 28 08 0e 00 	addq   $0xe08,0x28(%r14)
    8562:	00 
    8563:	48 8b 86 28 01 00 00 	mov    0x128(%rsi),%rax
    856a:	89 e9                	mov    %ebp,%ecx
    856c:	48 c1 e1 07          	shl    $0x7,%rcx
    8570:	80 7c 08 48 01       	cmpb   $0x1,0x48(%rax,%rcx,1)
    8575:	75 71                	jne    85e8 <ir_abi_context_value_validated+0x148>
    8577:	b8 01 00 00 00       	mov    $0x1,%eax
    857c:	45 31 c0             	xor    %r8d,%r8d
    857f:	41 8b 4e 3c          	mov    0x3c(%r14),%ecx
    8583:	45 0f b6 4e 40       	movzbl 0x40(%r14),%r9d
    8588:	48 8d 7c 24 50       	lea    0x50(%rsp),%rdi
    858d:	89 ea                	mov    %ebp,%edx
    858f:	c4 e2 81 f7 d8       	shlx   %r15,%rax,%rbx
    8594:	4d 6b ff 38          	imul   $0x38,%r15,%r15
    8598:	83 7c 24 04 00       	cmpl   $0x0,0x4(%rsp)
    859d:	41 0f 95 c0          	setne  %r8b
    85a1:	c5 f8 77             	vzeroupper
    85a4:	e8 27 cc ff ff       	call   51d0 <ir_classify_abi_value>
    85a9:	c5 fc 10 44 24 50    	vmovups 0x50(%rsp),%ymm0
    85af:	c5 fc 10 4c 24 68    	vmovups 0x68(%rsp),%ymm1
    85b5:	c4 81 7c 11 4c 3d 18 	vmovups %ymm1,0x18(%r13,%r15,1)
    85bc:	c4 81 7c 11 44 3d 00 	vmovups %ymm0,0x0(%r13,%r15,1)
    85c3:	49 09 9d 00 0e 00 00 	or     %rbx,0xe00(%r13)
    85ca:	49 ff 46 30          	incq   0x30(%r14)
    85ce:	c4 81 7c 10 44 3d 00 	vmovups 0x0(%r13,%r15,1),%ymm0
    85d5:	c4 81 7c 10 4c 3d 18 	vmovups 0x18(%r13,%r15,1),%ymm1
    85dc:	c5 fc 11 4c 24 28    	vmovups %ymm1,0x28(%rsp)
    85e2:	c5 fc 11 44 24 10    	vmovups %ymm0,0x10(%rsp)
    85e8:	c5 fc 10 44 24 10    	vmovups 0x10(%rsp),%ymm0
    85ee:	c5 fc 10 4c 24 28    	vmovups 0x28(%rsp),%ymm1
    85f4:	48 8b 44 24 08       	mov    0x8(%rsp),%rax
    85f9:	c5 fc 11 48 18       	vmovups %ymm1,0x18(%rax)
    85fe:	c5 fc 11 00          	vmovups %ymm0,(%rax)
    8602:	48 81 c4 88 00 00 00 	add    $0x88,%rsp
    8609:	5b                   	pop    %rbx
    860a:	41 5c                	pop    %r12
    860c:	41 5d                	pop    %r13
    860e:	41 5e                	pop    %r14
    8610:	41 5f                	pop    %r15
    8612:	5d                   	pop    %rbp
    8613:	c5 f8 77             	vzeroupper
    8616:	c3                   	ret
    8617:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
    861b:	c5 fc 11 44 24 28    	vmovups %ymm0,0x28(%rsp)
    8621:	c5 fc 11 44 24 10    	vmovups %ymm0,0x10(%rsp)
    8627:	48 89 f3             	mov    %rsi,%rbx
    862a:	48 89 7c 24 08       	mov    %rdi,0x8(%rsp)
    862f:	ba 08 00 00 00       	mov    $0x8,%edx
    8634:	41 89 ef             	mov    %ebp,%r15d
    8637:	44 89 44 24 04       	mov    %r8d,0x4(%rsp)
    863c:	41 83 e7 3f          	and    $0x3f,%r15d
    8640:	41 8b 76 38          	mov    0x38(%r14),%esi
    8644:	49 8b 3e             	mov    (%r14),%rdi
    8647:	48 c1 e6 03          	shl    $0x3,%rsi
    864b:	c5 f8 77             	vzeroupper
    864e:	e8 ed 8f ff ff       	call   1640 <arena_allocate_bytes>
    8653:	4b 89 44 e6 10       	mov    %rax,0x10(%r14,%r12,8)
    8658:	48 89 c7             	mov    %rax,%rdi
    865b:	31 f6                	xor    %esi,%esi
    865d:	41 8b 56 38          	mov    0x38(%r14),%edx
    8661:	48 c1 e2 03          	shl    $0x3,%rdx
    8665:	e8 36 8a ff ff       	call   10a0 <memset@plt>
    866a:	41 8b 46 38          	mov    0x38(%r14),%eax
    866e:	48 89 de             	mov    %rbx,%rsi
    8671:	89 eb                	mov    %ebp,%ebx
    8673:	c1 eb 06             	shr    $0x6,%ebx
    8676:	48 c1 e0 03          	shl    $0x3,%rax
    867a:	49 01 46 28          	add    %rax,0x28(%r14)
    867e:	4b 8b 44 e6 10       	mov    0x10(%r14,%r12,8),%rax
    8683:	4c 8b 2c d8          	mov    (%rax,%rbx,8),%r13
    8687:	4d 85 ed             	test   %r13,%r13
    868a:	0f 85 d3 fe ff ff    	jne    8563 <ir_abi_context_value_validated+0xc3>
    8690:	e9 94 fe ff ff       	jmp    8529 <ir_abi_context_value_validated+0x89>
    8695:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
    869c:	00 00 00 
    869f:	90                   	nop
