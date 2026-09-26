00000000006a23f0 <c_conditional_number>:
  6a23f0:	push   %rbp
  6a23f1:	mov    %rsp,%rbp
  6a23f4:	sub    $0x60,%rsp
  6a23f8:	mov    %rdi,-0x28(%rbp)
  6a23fc:	mov    %rsi,-0x20(%rbp)
  6a2400:	mov    %rdx,-0x58(%rbp)
  6a2404:	movl   $0xa,-0x8(%rbp)
  6a240b:	movq   $0x0,-0x10(%rbp)
  6a2413:	cmpq   $0x2,-0x20(%rbp)
  6a2418:	jb     6a2487 <c_conditional_number+0x97>
  6a241a:	mov    -0x28(%rbp),%rax
  6a241e:	movzbl (%rax),%eax
  6a2421:	cmp    $0x30,%eax
  6a2424:	jne    6a2487 <c_conditional_number+0x97>
  6a2426:	mov    -0x28(%rbp),%rax
  6a242a:	movzbl 0x1(%rax),%eax
  6a242e:	cmp    $0x78,%eax
  6a2431:	je     6a2440 <c_conditional_number+0x50>
  6a2433:	mov    -0x28(%rbp),%rax
  6a2437:	movzbl 0x1(%rax),%eax
  6a243b:	cmp    $0x58,%eax
  6a243e:	jne    6a2451 <c_conditional_number+0x61>
  6a2440:	movl   $0x10,-0x8(%rbp)
  6a2447:	movq   $0x2,-0x10(%rbp)
  6a244f:	jmp    6a2485 <c_conditional_number+0x95>
  6a2451:	mov    -0x28(%rbp),%rax
  6a2455:	movzbl 0x1(%rax),%eax
  6a2459:	cmp    $0x62,%eax
  6a245c:	je     6a246b <c_conditional_number+0x7b>
  6a245e:	mov    -0x28(%rbp),%rax
  6a2462:	movzbl 0x1(%rax),%eax
  6a2466:	cmp    $0x42,%eax
  6a2469:	jne    6a247c <c_conditional_number+0x8c>
  6a246b:	movl   $0x2,-0x8(%rbp)
  6a2472:	movq   $0x2,-0x10(%rbp)
  6a247a:	jmp    6a2483 <c_conditional_number+0x93>
  6a247c:	movl   $0x8,-0x8(%rbp)
  6a2483:	jmp    6a2485 <c_conditional_number+0x95>
  6a2485:	jmp    6a2487 <c_conditional_number+0x97>
  6a2487:	movq   $0x0,-0x30(%rbp)
  6a248f:	mov    -0x8(%rbp),%ecx
  6a2492:	mov    $0xffffffffffffffff,%rax
  6a2499:	xor    %edx,%edx
  6a249b:	div    %rcx
  6a249e:	mov    %rax,-0x40(%rbp)
  6a24a2:	mov    -0x8(%rbp),%ecx
  6a24a5:	mov    $0xffffffffffffffff,%rax
  6a24ac:	xor    %edx,%edx
  6a24ae:	div    %rcx
  6a24b1:	mov    %edx,-0x34(%rbp)
  6a24b4:	movb   $0x0,-0x2(%rbp)
  6a24b8:	movb   $0x1,-0x1(%rbp)
  6a24bc:	xor    %eax,%eax
  6a24be:	testb  $0x1,-0x1(%rbp)
  6a24c2:	je     6a24cf <c_conditional_number+0xdf>
  6a24c4:	mov    -0x10(%rbp),%rax
  6a24c8:	cmp    -0x20(%rbp),%rax
  6a24cc:	setb   %al
  6a24cf:	test   $0x1,%al
  6a24d1:	jne    6a24d8 <c_conditional_number+0xe8>
  6a24d3:	jmp    6a25c6 <c_conditional_number+0x1d6>
  6a24d8:	mov    -0x28(%rbp),%rax
  6a24dc:	mov    -0x10(%rbp),%rcx
  6a24e0:	mov    (%rax,%rcx,1),%al
  6a24e3:	mov    %al,-0x3(%rbp)
  6a24e6:	movzbl -0x3(%rbp),%eax
  6a24ea:	cmp    $0x27,%eax
  6a24ed:	jne    6a2532 <c_conditional_number+0x142>
  6a24ef:	xor    %eax,%eax
  6a24f1:	testb  $0x1,-0x2(%rbp)
  6a24f5:	je     6a251f <c_conditional_number+0x12f>
  6a24f7:	mov    -0x10(%rbp),%rcx
  6a24fb:	add    $0x1,%rcx
  6a24ff:	xor    %eax,%eax
  6a2501:	cmp    -0x20(%rbp),%rcx
  6a2505:	jae    6a251f <c_conditional_number+0x12f>
  6a2507:	mov    -0x28(%rbp),%rax
  6a250b:	mov    -0x10(%rbp),%rcx
  6a250f:	movzbl 0x1(%rax,%rcx,1),%edi
  6a2514:	call   6a7b30 <c_integer_digit>
  6a2519:	cmp    -0x8(%rbp),%eax
  6a251c:	setb   %al
  6a251f:	and    $0x1,%al
  6a2521:	mov    %al,-0x1(%rbp)
  6a2524:	mov    -0x10(%rbp),%rax
  6a2528:	add    $0x1,%rax
  6a252c:	mov    %rax,-0x10(%rbp)
  6a2530:	jmp    6a25af <c_conditional_number+0x1bf>
  6a2532:	movzbl -0x3(%rbp),%edi
  6a2536:	call   6a7b30 <c_integer_digit>
  6a253b:	mov    %eax,-0x18(%rbp)
  6a253e:	mov    -0x18(%rbp),%eax
  6a2541:	cmp    -0x8(%rbp),%eax
  6a2544:	jb     6a254f <c_conditional_number+0x15f>
  6a2546:	movl   $0x3,-0x14(%rbp)
  6a254d:	jmp    6a25a7 <c_conditional_number+0x1b7>
  6a254f:	mov    -0x30(%rbp),%rcx
  6a2553:	mov    $0x1,%al
  6a2555:	cmp    -0x40(%rbp),%rcx
  6a2559:	jb     6a2570 <c_conditional_number+0x180>
  6a255b:	mov    -0x30(%rbp),%rcx
  6a255f:	xor    %eax,%eax
  6a2561:	cmp    -0x40(%rbp),%rcx
  6a2565:	jne    6a2570 <c_conditional_number+0x180>
  6a2567:	mov    -0x18(%rbp),%eax
  6a256a:	cmp    -0x34(%rbp),%eax
  6a256d:	setbe  %al
  6a2570:	and    $0x1,%al
  6a2572:	mov    %al,-0x1(%rbp)
  6a2575:	testb  $0x1,-0x1(%rbp)
  6a2579:	je     6a25a0 <c_conditional_number+0x1b0>
  6a257b:	mov    -0x30(%rbp),%rax
  6a257f:	mov    -0x8(%rbp),%ecx
  6a2582:	imul   %rcx,%rax
  6a2586:	mov    -0x18(%rbp),%ecx
  6a2589:	add    %rcx,%rax
  6a258c:	mov    %rax,-0x30(%rbp)
  6a2590:	movb   $0x1,-0x2(%rbp)
  6a2594:	mov    -0x10(%rbp),%rax
  6a2598:	add    $0x1,%rax
  6a259c:	mov    %rax,-0x10(%rbp)
  6a25a0:	movl   $0x0,-0x14(%rbp)
  6a25a7:	cmpl   $0x0,-0x14(%rbp)
  6a25ab:	jne    6a25b6 <c_conditional_number+0x1c6>
  6a25ad:	jmp    6a25af <c_conditional_number+0x1bf>
  6a25af:	movl   $0x0,-0x14(%rbp)
  6a25b6:	mov    -0x14(%rbp),%eax
  6a25b9:	test   %eax,%eax
  6a25bb:	je     6a25c1 <c_conditional_number+0x1d1>
  6a25bd:	jmp    6a25bf <c_conditional_number+0x1cf>
  6a25bf:	jmp    6a25c6 <c_conditional_number+0x1d6>
  6a25c1:	jmp    6a24bc <c_conditional_number+0xcc>
  6a25c6:	xor    %eax,%eax
  6a25c8:	testb  $0x1,-0x1(%rbp)
  6a25cc:	je     6a25fb <c_conditional_number+0x20b>
  6a25ce:	xor    %eax,%eax
  6a25d0:	testb  $0x1,-0x2(%rbp)
  6a25d4:	je     6a25fb <c_conditional_number+0x20b>
  6a25d6:	mov    -0x28(%rbp),%rax
  6a25da:	add    -0x10(%rbp),%rax
  6a25de:	mov    %rax,-0x50(%rbp)
  6a25e2:	mov    -0x20(%rbp),%rax
  6a25e6:	sub    -0x10(%rbp),%rax
  6a25ea:	mov    %rax,-0x48(%rbp)
  6a25ee:	mov    -0x50(%rbp),%rdi
  6a25f2:	mov    -0x48(%rbp),%rsi
  6a25f6:	call   6a7b70 <c_integer_suffix_valid>
  6a25fb:	and    $0x1,%al
  6a25fd:	mov    %al,-0x1(%rbp)
  6a2600:	testb  $0x1,-0x1(%rbp)
  6a2604:	je     6a2611 <c_conditional_number+0x221>
  6a2606:	mov    -0x30(%rbp),%rax
  6a260a:	mov    -0x58(%rbp),%rcx
  6a260e:	mov    %rax,(%rcx)
  6a2611:	mov    -0x1(%rbp),%al
  6a2614:	movl   $0x1,-0x14(%rbp)
  6a261b:	and    $0x1,%al
  6a261d:	movzbl %al,%eax
  6a2620:	add    $0x60,%rsp
  6a2624:	pop    %rbp
  6a2625:	ret
  6a2626:	cs nopw 0x0(%rax,%rax,1)
