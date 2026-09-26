0000000000006580 <blocked_overread_number>:
    6580:	push   %rbp
    6581:	push   %r15
    6583:	push   %r14
    6585:	push   %r13
    6587:	push   %r12
    6589:	push   %rbx
    658a:	sub    $0x28,%rsp
    658e:	mov    $0xa,%ecx
    6593:	cmp    $0x2,%rsi
    6597:	jb     65d7 <blocked_overread_number+0x57>
    6599:	cmpb   $0x30,(%rdi)
    659c:	jne    65d7 <blocked_overread_number+0x57>
    659e:	movzbl 0x1(%rdi),%eax
    65a2:	and    $0xdf,%al
    65a4:	cmp    $0x42,%al
    65a6:	mov    $0x2,%ecx
    65ab:	mov    $0x8,%r8d
    65b1:	cmove  %ecx,%r8d
    65b5:	cmp    $0x58,%al
    65b7:	mov    $0x10,%ecx
    65bc:	cmovne %r8d,%ecx
    65c0:	xor    %eax,%eax
    65c2:	cmp    $0x8,%ecx
    65c5:	setne  %al
    65c8:	add    %eax,%eax
    65ca:	mov    %rsi,%r15
    65cd:	sub    %rax,%r15
    65d0:	ja     65e5 <blocked_overread_number+0x65>
    65d2:	jmp    69c1 <blocked_overread_number+0x441>
    65d7:	xor    %eax,%eax
    65d9:	mov    %rsi,%r15
    65dc:	sub    %rax,%r15
    65df:	jbe    69c1 <blocked_overread_number+0x441>
    65e5:	mov    %rdx,0x10(%rsp)
    65ea:	movabs $0xcfcfcfcfcfcfcfd0,%r8
    65f4:	movabs $0x8080808080808080,%r9
    65fe:	movabs $0x7f7f7f7f7f7f7f7f,%r12
    6608:	cmp    $0xa,%ecx
    660b:	mov    $0xa,%edx
    6610:	cmovb  %ecx,%edx
    6613:	or     $0x30,%edx
    6616:	movabs $0xfefefefefefefeff,%r11
    6620:	imul   %rdx,%r11
    6624:	mov    (%rdi,%rax,1),%rdx
    6628:	mov    %rdx,%r10
    662b:	or     %r9,%r10
    662e:	add    %r10,%r8
    6631:	mov    %r11,0x20(%rsp)
    6636:	add    %r11,%r10
    6639:	or     %rdx,%r10
    663c:	andn   %r8,%r10,%r10
    6641:	and    %r9,%r10
    6644:	and    %r12,%r8
    6647:	cmp    $0x10,%ecx
    664a:	mov    %rdi,0x8(%rsp)
    664f:	jne    66af <blocked_overread_number+0x12f>
    6651:	movabs $0xa0a0a0a0a0a0a0a0,%r11
    665b:	or     %rdx,%r11
    665e:	movabs $0x9e9e9e9e9e9e9e9f,%rbx
    6668:	add    %r11,%rbx
    666b:	movabs $0x6767676767676766,%r14
    6675:	sub    %r11,%r14
    6678:	andn   %r9,%rdx,%rdi
    667d:	and    %r14,%rdi
    6680:	and    %rbx,%rdi
    6683:	movabs $0x28a8a8a8a8a8a8a9,%rbx
    668d:	add    %r11,%rbx
    6690:	and    %r12,%rbx
    6693:	lea    (%rdi,%rdi,1),%r11
    6697:	or     %rdi,%r10
    669a:	shr    $0x7,%rdi
    669e:	sub    %rdi,%r11
    66a1:	andn   %r8,%r11,%rdi
    66a6:	and    %rbx,%r11
    66a9:	or     %rdi,%r11
    66ac:	mov    %r11,%r8
    66af:	lea    0x0(,%r15,8),%edi
    66b7:	andn   %r9,%r10,%r11
    66bc:	cmp    $0x8,%r15
    66c0:	mov    $0xffffffffffffffff,%rbx
    66c7:	shlx   %rdi,%rbx,%rdi
    66cc:	not    %rdi
    66cf:	cmovae %rbx,%rdi
    66d3:	mov    $0x8,%ebp
    66d8:	cmovb  %r15d,%ebp
    66dc:	and    %r11,%rdi
    66df:	tzcnt  %rdi,%rbx
    66e4:	shr    $0x3,%ebx
    66e7:	test   %rdi,%rdi
    66ea:	cmove  %ebp,%ebx
    66ed:	mov    %ebx,%r13d
    66f0:	cmp    $0x7,%ebx
    66f3:	ja     6726 <blocked_overread_number+0x1a6>
    66f5:	cmp    %r13,%r15
    66f8:	jbe    6726 <blocked_overread_number+0x1a6>
    66fa:	mov    0x8(%rsp),%rdx
    66ff:	add    %rax,%rdx
    6702:	cmpb   $0x27,(%rdx,%r13,1)
    6707:	jne    6726 <blocked_overread_number+0x1a6>
    6709:	mov    0x8(%rsp),%rdi
    670e:	mov    0x10(%rsp),%rdx
    6713:	add    $0x28,%rsp
    6717:	pop    %rbx
    6718:	pop    %r12
    671a:	pop    %r13
    671c:	pop    %r14
    671e:	pop    %r15
    6720:	pop    %rbp
    6721:	jmp    5e50 <scalar_number>
    6726:	test   %ebx,%ebx
    6728:	je     6986 <blocked_overread_number+0x406>
    672e:	mov    %ecx,%r14d
    6731:	imul   %r14d,%r14d
    6735:	mov    %r14d,%r15d
    6738:	imul   %r15d,%r15d
    673c:	mov    %ecx,%r12d
    673f:	shl    $0x8,%r12d
    6743:	shl    $0x10,%r14d
    6747:	shl    $0x20,%r15
    674b:	or     $0x1,%r12
    674f:	or     $0x1,%r14
    6753:	or     $0x1,%r15
    6757:	shr    $0x7,%r10
    675b:	mov    %r10,%rdx
    675e:	shl    $0x8,%rdx
    6762:	sub    %r10,%rdx
    6765:	and    %r8,%rdx
    6768:	cmp    $0x8,%ebx
    676b:	je     677c <blocked_overread_number+0x1fc>
    676d:	lea    0x0(,%rbx,8),%edi
    6774:	neg    %dil
    6777:	shlx   %rdi,%rdx,%rdx
    677c:	movabs $0xff00ff00ff00ff,%rdi
    6786:	movabs $0xffff0000ffff,%r8
    6790:	imul   %r12,%rdx
    6794:	shr    $0x8,%rdx
    6798:	and    %rdi,%rdx
    679b:	imul   %r14,%rdx
    679f:	shr    $0x10,%rdx
    67a3:	and    %r8,%rdx
    67a6:	imul   %r15,%rdx
    67aa:	shr    $0x20,%rdx
    67ae:	add    %r13,%rax
    67b1:	xor    %edi,%edi
    67b3:	cmp    $0x8,%ebx
    67b6:	jne    698c <blocked_overread_number+0x40c>
    67bc:	cmp    %rsi,%rax
    67bf:	jae    698c <blocked_overread_number+0x40c>
    67c5:	mov    %ecx,%r8d
    67c8:	lea    (%r8,%r8,8),%r8
    67cc:	lea    0x2a0d(%rip),%r10        # 91e0 <blocked_powers>
    67d3:	lea    (%r10,%r8,8),%rdi
    67d7:	mov    %rdi,0x18(%rsp)
    67dc:	nopl   0x0(%rax)
    67e0:	mov    0x8(%rsp),%rdi
    67e5:	mov    (%rdi,%rax,1),%r8
    67e9:	mov    %r8,%r11
    67ec:	or     %r9,%r11
    67ef:	movabs $0xcfcfcfcfcfcfcfd0,%rdi
    67f9:	lea    (%r11,%rdi,1),%r13
    67fd:	add    0x20(%rsp),%r11
    6802:	or     %r8,%r11
    6805:	andn   %r9,%r11,%rbx
    680a:	and    %r13,%rbx
    680d:	movabs $0x7f7f7f7f7f7f7f7f,%r10
    6817:	and    %r10,%r13
    681a:	cmp    $0x10,%ecx
    681d:	jne    687e <blocked_overread_number+0x2fe>
    681f:	andn   %r9,%r8,%r11
    6824:	movabs $0xa0a0a0a0a0a0a0a0,%rdi
    682e:	or     %rdi,%r8
    6831:	movabs $0x9e9e9e9e9e9e9e9f,%rdi
    683b:	lea    (%r8,%rdi,1),%rbp
    683f:	movabs $0x6767676767676766,%rdi
    6849:	sub    %r8,%rdi
    684c:	and    %r11,%rdi
    684f:	and    %rbp,%rdi
    6852:	movabs $0x28a8a8a8a8a8a8a9,%r11
    685c:	add    %r11,%r8
    685f:	and    %r10,%r8
    6862:	lea    (%rdi,%rdi,1),%r11
    6866:	or     %rdi,%rbx
    6869:	shr    $0x7,%rdi
    686d:	sub    %rdi,%r11
    6870:	andn   %r13,%r11,%rdi
    6875:	and    %r8,%r11
    6878:	or     %rdi,%r11
    687b:	mov    %r11,%r13
    687e:	mov    %rsi,%rbp
    6881:	sub    %rax,%rbp
    6884:	lea    0x0(,%rbp,8),%edi
    688b:	andn   %r9,%rbx,%r8
    6890:	cmp    $0x8,%rbp
    6894:	mov    $0xffffffffffffffff,%r10
    689b:	shlx   %rdi,%r10,%rdi
    68a0:	not    %rdi
    68a3:	cmovae %r10,%rdi
    68a7:	mov    %ebp,%r11d
    68aa:	mov    $0x8,%r10d
    68b0:	cmovae %r10d,%r11d
    68b4:	and    %r8,%rdi
    68b7:	tzcnt  %rdi,%r8
    68bc:	shr    $0x3,%r8d
    68c0:	test   %rdi,%rdi
    68c3:	cmove  %r11d,%r8d
    68c7:	mov    %r8d,%r11d
    68ca:	cmp    $0x7,%r8d
    68ce:	ja     68e8 <blocked_overread_number+0x368>
    68d0:	cmp    %r11,%rbp
    68d3:	jbe    68e8 <blocked_overread_number+0x368>
    68d5:	mov    0x8(%rsp),%rdi
    68da:	add    %rax,%rdi
    68dd:	cmpb   $0x27,(%rdi,%r11,1)
    68e2:	je     6709 <blocked_overread_number+0x189>
    68e8:	shr    $0x7,%rbx
    68ec:	mov    %rbx,%rbp
    68ef:	shl    $0x8,%rbp
    68f3:	sub    %rbx,%rbp
    68f6:	and    %r13,%rbp
    68f9:	cmp    $0x8,%r8d
    68fd:	je     6918 <blocked_overread_number+0x398>
    68ff:	test   %r8d,%r8d
    6902:	je     69b4 <blocked_overread_number+0x434>
    6908:	lea    0x0(,%r8,8),%edi
    6910:	neg    %dil
    6913:	shlx   %rdi,%rbp,%rbp
    6918:	imul   %r12,%rbp
    691c:	shr    $0x8,%rbp
    6920:	movabs $0xff00ff00ff00ff,%rdi
    692a:	and    %rdi,%rbp
    692d:	imul   %r14,%rbp
    6931:	shr    $0x10,%rbp
    6935:	movabs $0xffff0000ffff,%rdi
    693f:	and    %rdi,%rbp
    6942:	mov    0x18(%rsp),%rdi
    6947:	mulx   (%rdi,%r11,8),%rdx,%rbx
    694d:	imul   %r15,%rbp
    6951:	shr    $0x20,%rbp
    6955:	add    %rbp,%rdx
    6958:	adc    $0x0,%rbx
    695c:	add    %r11,%rax
    695f:	cmp    $0x8,%r8d
    6963:	jne    6973 <blocked_overread_number+0x3f3>
    6965:	cmp    %rsi,%rax
    6968:	jae    6973 <blocked_overread_number+0x3f3>
    696a:	test   %rbx,%rbx
    696d:	je     67e0 <blocked_overread_number+0x260>
    6973:	test   %rbx,%rbx
    6976:	sete   %cl
    6979:	test   %cl,%cl
    697b:	sete   %dil
    697f:	test   %dil,%dil
    6982:	jne    69c1 <blocked_overread_number+0x441>
    6984:	jmp    6991 <blocked_overread_number+0x411>
    6986:	mov    $0x1,%dil
    6989:	mov    %r13,%rdx
    698c:	test   %dil,%dil
    698f:	jne    69c1 <blocked_overread_number+0x441>
    6991:	mov    %rdx,%rbx
    6994:	mov    0x8(%rsp),%rdi
    6999:	add    %rax,%rdi
    699c:	sub    %rax,%rsi
    699f:	call   69e0 <inc_suffix_valid>
    69a4:	test   %al,%al
    69a6:	je     69c1 <blocked_overread_number+0x441>
    69a8:	mov    0x10(%rsp),%rax
    69ad:	mov    %rbx,(%rax)
    69b0:	mov    $0x1,%al
    69b2:	jmp    69c3 <blocked_overread_number+0x443>
    69b4:	mov    $0x1,%cl
    69b6:	test   %cl,%cl
    69b8:	sete   %dil
    69bc:	test   %dil,%dil
    69bf:	je     6991 <blocked_overread_number+0x411>
    69c1:	xor    %eax,%eax
    69c3:	add    $0x28,%rsp
    69c7:	pop    %rbx
    69c8:	pop    %r12
    69ca:	pop    %r13
    69cc:	pop    %r14
    69ce:	pop    %r15
    69d0:	pop    %rbp
    69d1:	ret
    69d2:	data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
