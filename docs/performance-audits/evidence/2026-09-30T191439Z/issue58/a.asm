
../issue58-a-ide:	file format elf64-x86-64

Disassembly of section .text:

0000000001826340 <machine_x64_select_load>:
 1826340:      	push	rbp
 1826341:      	mov	rbp, rsp
 1826344:      	push	r15
 1826346:      	push	r14
 1826348:      	push	r13
 182634a:      	push	r12
 182634c:      	push	rbx
 182634d:      	sub	rsp, 0xf8
 1826354:      	mov	rax, qword ptr fs:[0x28]
 182635d:      	mov	ecx, 0xffffffff
 1826362:      	mov	qword ptr [rbp - 0x30], rax
 1826366:      	mov	rax, qword ptr [rsi]
 1826369:      	mov	r9, qword ptr [rdi + 0x10]
 182636d:      	mov	r8d, dword ptr [rsi + 0x28]
 1826371:      	mov	ebx, dword ptr [rax]
 1826373:      	cmp	ebx, dword ptr [r9 + 0xa0]
 182637a:      	setae	al
 182637d:      	cmp	r8, rcx
 1826380:      	sete	cl
 1826383:      	or	cl, al
 1826385:      	jne	0x1826a7e <machine_x64_select_load+0x73e>
 182638b:      	mov	rax, qword ptr [rdi + 0x478]
 1826392:      	movzx	eax, byte ptr [rax + rbx]
 1826396:      	test	eax, eax
 1826398:      	je	0x1826a7e <machine_x64_select_load+0x73e>
 182639e:      	mov	rcx, qword ptr [rdi + 0x360]
 18263a5:      	mov	r10, rsi
 18263a8:      	mov	esi, dword ptr [rsi + 0x18]
 18263ab:      	mov	r14d, dword ptr [rcx + 4*r8]
 18263af:      	mov	r8d, 0xffff0e00
 18263b5:      	cmp	esi, dword ptr [rdi + 0x4e0]
 18263bb:      	jae	0x18263c8 <machine_x64_select_load+0x88>
 18263bd:      	mov	r8, qword ptr [rdi + 0x4d8]
 18263c4:      	mov	r8d, dword ptr [r8 + 4*rsi]
 18263c8:      	mov	ecx, dword ptr [rcx + 4*rbx]
 18263cb:      	movzx	r10d, byte ptr [r10 + 0x34]
 18263d0:      	mov	qword ptr [rbp - 0xf8], rsi
 18263d7:      	mov	esi, r8d
 18263da:      	mov	dword ptr [rbp - 0xf0], ecx
 18263e0:      	mov	ecx, r8d
 18263e3:      	shr	ecx, 0x8
 18263e6:      	cmp	edx, -0x1
 18263e9:      	mov	dword ptr [rbp - 0x100], ecx
 18263ef:      	sete	r11b
 18263f3:      	setne	r12b
 18263f7:      	and	r8d, 0x8
 18263fb:      	movzx	ecx, byte ptr [rdi + 0x4c8]
 1826402:      	shr	r8d, 0x3
 1826406:      	and	r8b, r12b
 1826409:      	xor	r12b, r8b
 182640c:      	cmp	r14d, -0x1
 1826410:      	setne	r13b
 1826414:      	xor	r15d, r15d
 1826417:      	cmp	r10b, 0x8
 182641b:      	movzx	r12d, r12b
 182641f:      	cmove	ecx, r15d
 1826423:      	test	r8b, r8b
 1826426:      	cmove	ecx, r15d
 182642a:      	or	ecx, r12d
 182642d:      	je	0x182647a <machine_x64_select_load+0x13a>
 182642f:      	mov	r14d, esi
 1826432:      	cmp	eax, 0x2
 1826435:      	je	0x1826502 <machine_x64_select_load+0x1c2>
 182643b:      	cmp	eax, 0x1
 182643e:      	jne	0x18264f1 <machine_x64_select_load+0x1b1>
 1826444:      	mov	rax, qword ptr [rdi + 0x368]
 182644b:      	cmp	dword ptr [rax + 4*rbx], -0x1
 182644f:      	jne	0x1826502 <machine_x64_select_load+0x1c2>
 1826455:      	mov	rax, qword ptr [rdi + 0x350]
 182645c:      	mov	esi, dword ptr [rax + 4*rbx]
 182645f:      	cmp	esi, -0x1
 1826462:      	je	0x1826b1c <machine_x64_select_load+0x7dc>
 1826468:      	mov	al, 0x1
 182646a:      	cmp	edx, esi
 182646c:      	je	0x1826a80 <machine_x64_select_load+0x740>
 1826472:      	xor	r12d, r12d
 1826475:      	jmp	0x182651d <machine_x64_select_load+0x1dd>
 182647a:      	mov	rdx, qword ptr [rdi + 0x8]
 182647e:      	and	r13b, r11b
 1826481:      	cmp	r10b, 0x8
 1826485:      	mov	dword ptr [rbp - 0xec], r14d
 182648c:      	mov	r8d, r13d
 182648f:      	setne	cl
 1826492:      	xor	r8b, 0x1
 1826496:      	or	r8b, cl
 1826499:      	jne	0x1826589 <machine_x64_select_load+0x249>
 182649f:      	mov	rax, qword ptr [r9 + 0x48]
 18264a3:      	mov	rcx, rbx
 18264a6:      	shl	rcx, 0x4
 18264aa:      	mov	r8d, dword ptr [rdx + 0x130]
 18264b1:      	mov	eax, dword ptr [rax + rcx]
 18264b4:      	cmp	eax, r8d
 18264b7:      	jae	0x182670e <machine_x64_select_load+0x3ce>
 18264bd:      	mov	rcx, qword ptr [rdx + 0x128]
 18264c4:      	mov	r10d, esi
 18264c7:      	test	rcx, rcx
 18264ca:      	je	0x1826711 <machine_x64_select_load+0x3d1>
 18264d0:      	shl	rax, 0x7
 18264d4:      	mov	rsi, qword ptr [rbp - 0xf8]
 18264db:      	add	rcx, rax
 18264de:      	cmp	byte ptr [rcx + 0x48], 0x1
 18264e2:      	jne	0x1826718 <machine_x64_select_load+0x3d8>
 18264e8:      	mov	rax, qword ptr [rcx + 0x38]
 18264ec:      	jmp	0x182671a <machine_x64_select_load+0x3da>
 18264f1:      	mov	rax, qword ptr [rdi + 0x368]
 18264f8:      	cmp	dword ptr [rax + 4*rbx], -0x1
 18264fc:      	je	0x1826a7e <machine_x64_select_load+0x73e>
 1826502:      	mov	rax, qword ptr [rdi + 0x350]
 1826509:      	xor	r12d, r12d
 182650c:      	mov	esi, dword ptr [rax + 4*rbx]
 182650f:      	mov	eax, 0x0
 1826514:      	cmp	esi, -0x1
 1826517:      	je	0x1826a80 <machine_x64_select_load+0x740>
 182651d:      	mov	ecx, r14d
 1826520:      	shr	ecx, 0x10
 1826523:      	test	r8b, r8b
 1826526:      	mov	r8d, 0x6f
 182652c:      	mov	r9d, 0x5
 1826532:      	mov	r10d, 0x2d
 1826538:      	mov	r13d, edx
 182653b:      	movzx	r15d, cl
 182653f:      	cmovne	r9d, r8d
 1826543:      	mov	r8d, 0x70
 1826549:      	cmovne	r10d, r8d
 182654d:      	lea	ecx, [r15 + 0x32]
 1826551:      	mov	r8d, 0x72
 1826557:      	cmove	r8d, ecx
 182655b:      	test	r12b, r12b
 182655e:      	cmovne	r8d, r10d
 1826562:      	test	al, al
 1826564:      	mov	eax, edx
 1826566:      	cmovne	r8d, r9d
 182656a:      	or	eax, 0x20000000
 182656f:      	test	r12b, r12b
 1826572:      	je	0x18266a2 <machine_x64_select_load+0x362>
 1826578:      	mov	esi, dword ptr [rbp - 0xf0]
 182657e:      	or	esi, 0xa0000000
 1826584:      	jmp	0x18266a8 <machine_x64_select_load+0x368>
 1826589:      	test	r13b, r13b
 182658c:      	je	0x1826a7e <machine_x64_select_load+0x73e>
 1826592:      	mov	rsi, qword ptr [rbp - 0xf8]
 1826599:      	mov	rcx, qword ptr [rdx + 0x128]
 18265a0:      	mov	rdx, rsi
 18265a3:      	shl	rdx, 0x7
 18265a7:      	mov	r14, qword ptr [rcx + rdx + 0x38]
 18265ac:      	mov	r8, r14
 18265af:      	shr	r8, 0x20
 18265b3:      	setne	r8b
 18265b7:      	cmp	byte ptr [rcx + rdx + 0x48], 0x0
 18265bc:      	sete	cl
 18265bf:      	or	cl, r8b
 18265c2:      	jne	0x1826a7e <machine_x64_select_load+0x73e>
 18265c8:      	mov	r10d, dword ptr [rbp - 0xf0]
 18265cf:      	cmp	eax, 0x1
 18265d2:      	mov	r9b, 0x1
 18265d5:      	sete	cl
 18265d8:      	cmp	r10d, -0x1
 18265dc:      	setne	dl
 18265df:      	and	dl, cl
 18265e1:      	cmp	eax, 0x2
 18265e4:      	je	0x18265f5 <machine_x64_select_load+0x2b5>
 18265e6:      	mov	rax, qword ptr [rdi + 0x368]
 18265ed:      	cmp	dword ptr [rax + 4*rbx], -0x1
 18265f1:      	setne	r9b
 18265f5:      	mov	rax, qword ptr [rdi + 0x350]
 18265fc:      	mov	r12d, dword ptr [rbp - 0xec]
 1826603:      	mov	ecx, edx
 1826605:      	not	cl
 1826607:      	mov	r8d, dword ptr [rax + 4*rbx]
 182660b:      	cmp	r8d, -0x1
 182660f:      	setne	al
 1826612:      	and	al, r9b
 1826615:      	and	al, cl
 1826617:      	or	dl, al
 1826619:      	cmp	dl, 0x1
 182661c:      	jne	0x1826a7e <machine_x64_select_load+0x73e>
 1826622:      	or	r12d, 0xa0000000
 1826629:      	test	al, al
 182662b:      	mov	ecx, 0x20000000
 1826630:      	mov	edx, 0xa0000000
 1826635:      	movzx	r15d, al
 1826639:      	mov	rbx, rdi
 182663c:      	cmovne	r10d, r8d
 1826640:      	cmovne	edx, ecx
 1826643:      	mov	dword ptr [rbp - 0xe8], r12d
 182664a:      	or	r15d, 0x52
 182664e:      	or	edx, r10d
 1826651:      	mov	dword ptr [rbp - 0xe4], edx
 1826657:      	mov	qword ptr [rbp - 0xe0], 0x0
 1826662:      	call	0x181d760 <machine_x64_type_is_f80>
 1826667:      	test	al, al
 1826669:      	mov	ecx, 0xa
 182666e:      	cmove	ecx, r14d
 1826672:      	mov	dword ptr [rbp - 0xd8], ecx
 1826678:      	mov	word ptr [rbp - 0xd4], r15w
 1826680:      	mov	word ptr [rbp - 0xd2], 0x0
 1826689:      	mov	rax, qword ptr [rbp - 0xd8]
 1826690:      	mov	qword ptr [rsp + 0x10], rax
 1826695:      	vmovups	xmm0, xmmword ptr [rbp - 0xe8]
 182669d:      	jmp	0x1826897 <machine_x64_select_load+0x557>
 18266a2:      	or	esi, 0x20000000
 18266a8:      	mov	dword ptr [rbp - 0x68], eax
 18266ab:      	mov	dword ptr [rbp - 0x64], esi
 18266ae:      	mov	qword ptr [rbp - 0x60], 0x0
 18266b6:      	mov	dword ptr [rbp - 0x58], 0x0
 18266bd:      	mov	word ptr [rbp - 0x54], r8w
 18266c2:      	mov	dword ptr [rbp - 0xf8], eax
 18266c8:      	mov	word ptr [rbp - 0x52], 0x0
 18266ce:      	mov	rbx, rdi
 18266d1:      	mov	rax, qword ptr [rbp - 0x58]
 18266d5:      	mov	qword ptr [rsp + 0x10], rax
 18266da:      	vmovups	xmm0, xmmword ptr [rbp - 0x68]
 18266df:      	vmovups	xmmword ptr [rsp], xmm0
 18266e4:      	call	0x181dc80 <machine_x64_select_row>
 18266e9:      	cmp	r13d, dword ptr [rbx + 0x480]
 18266f0:      	jae	0x1826818 <machine_x64_select_load+0x4d8>
 18266f6:      	mov	esi, dword ptr [rbx + 0x68]
 18266f9:      	cmp	r13d, esi
 18266fc:      	jae	0x18267dc <machine_x64_select_load+0x49c>
 1826702:      	mov	rcx, qword ptr [rbx + 0x418]
 1826709:      	jmp	0x18267ff <machine_x64_select_load+0x4bf>
 182670e:      	mov	r10d, esi
 1826711:      	mov	rsi, qword ptr [rbp - 0xf8]
 1826718:      	xor	eax, eax
 182671a:      	movzx	ecx, byte ptr [rbp - 0x100]
 1826721:      	popcnt	r9, rax
 1826726:      	mov	r11w, 0x31
 182672b:      	cmp	r9d, 0x1
 182672f:      	jne	0x1826751 <machine_x64_select_load+0x411>
 1826731:      	tzcnt	r9, rax
 1826736:      	cmp	r9, 0x3
 182673a:      	ja	0x1826751 <machine_x64_select_load+0x411>
 182673c:      	add	r9d, 0x32
 1826740:      	mov	r15b, 0x1
 1826743:      	mov	r11d, r9d
 1826746:      	lea	r13d, [rcx - 0xb]
 182674a:      	cmp	esi, r8d
 182674d:      	jb	0x182675d <machine_x64_select_load+0x41d>
 182674f:      	jmp	0x182677c <machine_x64_select_load+0x43c>
 1826751:      	xor	r15d, r15d
 1826754:      	lea	r13d, [rcx - 0xb]
 1826758:      	cmp	esi, r8d
 182675b:      	jae	0x182677c <machine_x64_select_load+0x43c>
 182675d:      	mov	rdx, qword ptr [rdx + 0x128]
 1826764:      	test	rdx, rdx
 1826767:      	je	0x182677c <machine_x64_select_load+0x43c>
 1826769:      	shl	rsi, 0x7
 182676d:      	add	rdx, rsi
 1826770:      	cmp	byte ptr [rdx + 0x48], 0x1
 1826774:      	jne	0x182677c <machine_x64_select_load+0x43c>
 1826776:      	mov	r12, qword ptr [rdx + 0x38]
 182677a:      	jmp	0x182677f <machine_x64_select_load+0x43f>
 182677c:      	xor	r12d, r12d
 182677f:      	cmp	r13d, 0x2
 1826783:      	setb	dl
 1826786:      	cmp	ecx, 0x2
 1826789:      	sete	cl
 182678c:      	and	r10d, 0x40
 1826790:      	shr	r10d, 0x6
 1826794:      	and	r10b, cl
 1826797:      	or	r10b, dl
 182679a:      	cmp	r10b, 0x1
 182679e:      	jne	0x182690d <machine_x64_select_load+0x5cd>
 18267a4:      	cmp	rax, 0x10
 18267a8:      	jne	0x182690d <machine_x64_select_load+0x5cd>
 18267ae:      	lea	rax, [r12 - 0x9]
 18267b3:      	cmp	rax, 0x7
 18267b7:      	ja	0x182690d <machine_x64_select_load+0x5cd>
 18267bd:      	cmp	byte ptr [rdi + 0x494], 0x1
 18267c4:      	jne	0x18268ab <machine_x64_select_load+0x56b>
 18267ca:      	vmovups	ymm0, ymmword ptr [rdi + 0x4a0]
 18267d2:      	vmovups	ymmword ptr [rbp - 0x50], ymm0
 18267d7:      	jmp	0x1826902 <machine_x64_select_load+0x5c2>
 18267dc:      	lea	rcx, [rbx + 0x50]
 18267e0:      	add	r13d, esi
 18267e3:      	nop	word ptr cs:[rax + rax]
 18267f0:      	mov	rcx, qword ptr [rcx]
 18267f3:      	sub	r13d, esi
 18267f6:      	cmp	r13d, esi
 18267f9:      	jae	0x18267f0 <machine_x64_select_load+0x4b0>
 18267fb:      	add	rcx, 0x10
 18267ff:      	mov	edx, r13d
 1826802:      	shl	rdx, 0x4
 1826806:      	cmp	dword ptr [rcx + rdx], -0x1
 182680a:      	jne	0x1826818 <machine_x64_select_load+0x4d8>
 182680c:      	lea	eax, [4*rax + 0x3]
 1826813:      	add	rcx, rdx
 1826816:      	mov	dword ptr [rcx], eax
 1826818:      	movzx	eax, byte ptr [rbp - 0x100]
 182681f:      	test	r14b, 0x20
 1826823:      	mov	edx, 0x60b0a
 1826828:      	mov	esi, 0x60807
 182682d:      	cmove	rsi, rdx
 1826831:      	cmp	eax, 0x2
 1826834:      	lea	ecx, [rax - 0x1]
 1826837:      	cmovne	rsi, rdx
 182683b:      	xor	eax, eax
 182683d:      	cmp	ecx, 0x2
 1826840:      	cmovb	rax, rsi
 1826844:      	shl	r15d, 0x3
 1826848:      	shrx	rcx, rax, r15
 182684d:      	mov	al, 0x1
 182684f:      	and	cx, 0xff
 1826854:      	sete	dl
 1826857:      	xor	r12b, 0x1
 182685b:      	or	r12b, dl
 182685e:      	jne	0x1826a80 <machine_x64_select_load+0x740>
 1826864:      	mov	eax, dword ptr [rbp - 0xf8]
 182686a:      	mov	dword ptr [rbp - 0x80], eax
 182686d:      	mov	dword ptr [rbp - 0x7c], eax
 1826870:      	mov	qword ptr [rbp - 0x78], 0x0
 1826878:      	mov	dword ptr [rbp - 0x70], 0x0
 182687f:      	mov	word ptr [rbp - 0x6c], cx
 1826883:      	mov	word ptr [rbp - 0x6a], 0x0
 1826889:      	mov	rax, qword ptr [rbp - 0x70]
 182688d:      	mov	qword ptr [rsp + 0x10], rax
 1826892:      	vmovups	xmm0, xmmword ptr [rbp - 0x80]
 1826897:      	mov	rdi, rbx
 182689a:      	vmovups	xmmword ptr [rsp], xmm0
 182689f:      	call	0x181dc80 <machine_x64_select_row>
 18268a4:      	mov	al, 0x1
 18268a6:      	jmp	0x1826a80 <machine_x64_select_load+0x740>
 18268ab:      	mov	esi, dword ptr [rdi + 0x488]
 18268b1:      	mov	edx, dword ptr [rdi + 0x48c]
 18268b7:      	cmp	edx, 0x2
 18268ba:      	jne	0x18268dc <machine_x64_select_load+0x59c>
 18268bc:      	cmp	esi, dword ptr [rip + 0x4c3e5e] # 0x1cea720 <target_native>
 18268c2:      	jne	0x18268dc <machine_x64_select_load+0x59c>
 18268c4:      	cmp	byte ptr [rip + 0x4c3e61], 0x0 # 0x1cea72c <target_native+0xc>
 18268cb:      	je	0x18268dc <machine_x64_select_load+0x59c>
 18268cd:      	vmovups	ymm0, ymmword ptr [rip + 0x4c3e63] # 0x1cea738 <target_native+0x18>
 18268d5:      	vmovups	ymmword ptr [rbp - 0x50], ymm0
 18268da:      	jmp	0x1826902 <machine_x64_select_load+0x5c2>
 18268dc:      	lea	rax, [rbp - 0x50]
 18268e0:      	mov	qword ptr [rbp - 0x100], rdi
 18268e7:      	mov	r14, r12
 18268ea:      	mov	r12d, r11d
 18268ed:      	mov	rdi, rax
 18268f0:      	call	0x1475a90 <target_cpu_features_default>
 18268f5:      	mov	rdi, qword ptr [rbp - 0x100]
 18268fc:      	mov	r11d, r12d
 18268ff:      	mov	r12, r14
 1826902:      	cmp	word ptr [rbp - 0x50], 0x0
 1826907:      	js	0x1826aa8 <machine_x64_select_load+0x768>
 182690d:      	cmp	r13d, 0x2
 1826911:      	setae	al
 1826914:      	xor	r15b, 0x1
 1826918:      	or	r15b, al
 182691b:      	jne	0x1826a7e <machine_x64_select_load+0x73e>
 1826921:      	mov	r15d, dword ptr [rdi + 0x30]
 1826925:      	mov	r14d, dword ptr [rdi + 0x60]
 1826929:      	mov	rax, qword ptr [rdi + 0x70]
 182692d:      	mov	word ptr [rbp - 0x100], r11w
 1826935:      	cmp	rax, qword ptr [rdi + 0x78]
 1826939:      	jne	0x1826978 <machine_x64_select_load+0x638>
 182693b:      	mov	rax, qword ptr [rdi + 0x18]
 182693f:      	mov	rcx, qword ptr [rdi + 0x58]
 1826943:      	lea	rsi, [rdi + 0x50]
 1826947:      	test	rcx, rcx
 182694a:      	je	0x1826952 <machine_x64_select_load+0x612>
 182694c:      	mov	edx, dword ptr [rdi + 0x68]
 182694f:      	mov	dword ptr [rcx + 0x8], edx
 1826952:      	mov	r13, rdi
 1826955:      	mov	rdi, rax
 1826958:      	vzeroupper
 182695b:      	call	0x183aa90 <machine_stream_chunk_push>
 1826960:      	mov	ecx, dword ptr [r13 + 0x68]
 1826964:      	mov	rdi, r13
 1826967:      	shl	rcx, 0x4
 182696b:      	lea	rcx, [rax + rcx + 0x10]
 1826970:      	add	rax, 0x10
 1826974:      	mov	qword ptr [r13 + 0x78], rcx
 1826978:      	mov	ecx, 0xffffffff
 182697d:      	lea	rsi, [rax + 0x10]
 1826981:      	lea	rdx, [rcx + 4*r15 + 0x4]
 1826986:      	mov	qword ptr [rdi + 0x70], rsi
 182698a:      	lea	esi, [r14 + 0x1]
 182698e:      	mov	r15, rdi
 1826991:      	mov	dword ptr [rdi + 0x60], esi
 1826994:      	mov	esi, ebx
 1826996:      	mov	qword ptr [rax], rdx
 1826999:      	mov	qword ptr [rax + 0x8], rcx
 182699d:      	mov	edx, r14d
 18269a0:      	xor	ecx, ecx
 18269a2:      	vzeroupper
 18269a5:      	call	0x18441f0 <machine_x64_select_place_address_offset>
 18269aa:      	mov	ecx, eax
 18269ac:      	xor	eax, eax
 18269ae:      	test	cl, cl
 18269b0:      	je	0x1826a80 <machine_x64_select_load+0x740>
 18269b6:      	mov	rdi, r15
 18269b9:      	mov	rbx, r15
 18269bc:      	call	0x181dfc0 <machine_x64_synthesize_register>
 18269c1:      	movzx	ecx, word ptr [rbp - 0x100]
 18269c8:      	mov	r15d, eax
 18269cb:      	or	r15d, 0x20000000
 18269d2:      	or	r14d, 0x20000000
 18269d9:      	mov	rdi, rbx
 18269dc:      	mov	dword ptr [rbp - 0xb8], r15d
 18269e3:      	mov	dword ptr [rbp - 0xb4], r14d
 18269ea:      	mov	qword ptr [rbp - 0xb0], 0x0
 18269f5:      	mov	dword ptr [rbp - 0xa8], 0x0
 18269ff:      	mov	word ptr [rbp - 0xa4], cx
 1826a06:      	mov	word ptr [rbp - 0xa2], 0x0
 1826a0f:      	mov	rax, qword ptr [rbp - 0xa8]
 1826a16:      	mov	qword ptr [rsp + 0x10], rax
 1826a1b:      	vmovups	xmm0, xmmword ptr [rbp - 0xb8]
 1826a23:      	vmovups	xmmword ptr [rsp], xmm0
 1826a28:      	call	0x181dc80 <machine_x64_select_row>
 1826a2d:      	mov	r12d, dword ptr [rbp - 0xec]
 1826a34:      	movabs	rax, 0x3100000000
 1826a3e:      	or	r12d, 0xa0000000
 1826a45:      	mov	dword ptr [rbp - 0xd0], r12d
 1826a4c:      	mov	dword ptr [rbp - 0xcc], r15d
 1826a53:      	mov	qword ptr [rbp - 0xc8], 0x0
 1826a5e:      	mov	qword ptr [rbp - 0xc0], rax
 1826a65:      	mov	rax, qword ptr [rbp - 0xc0]
 1826a6c:      	mov	qword ptr [rsp + 0x10], rax
 1826a71:      	vmovups	xmm0, xmmword ptr [rbp - 0xd0]
 1826a79:      	jmp	0x1826897 <machine_x64_select_load+0x557>
 1826a7e:      	xor	eax, eax
 1826a80:      	mov	rcx, qword ptr fs:[0x28]
 1826a89:      	cmp	rcx, qword ptr [rbp - 0x30]
 1826a8d:      	jne	0x1826b38 <machine_x64_select_load+0x7f8>
 1826a93:      	add	rsp, 0xf8
 1826a9a:      	pop	rbx
 1826a9b:      	pop	r12
 1826a9d:      	pop	r13
 1826a9f:      	pop	r14
 1826aa1:      	pop	r15
 1826aa3:      	pop	rbp
 1826aa4:      	vzeroupper
 1826aa7:      	ret
 1826aa8:      	mov	r15, rdi
 1826aab:      	vzeroupper
 1826aae:      	call	0x181dfc0 <machine_x64_synthesize_register>
 1826ab3:      	mov	r14d, eax
 1826ab6:      	mov	rdi, r15
 1826ab9:      	mov	esi, ebx
 1826abb:      	mov	edx, eax
 1826abd:      	xor	ecx, ecx
 1826abf:      	call	0x18441f0 <machine_x64_select_place_address_offset>
 1826ac4:      	mov	ecx, eax
 1826ac6:      	xor	eax, eax
 1826ac8:      	test	cl, cl
 1826aca:      	je	0x1826a80 <machine_x64_select_load+0x740>
 1826acc:      	vmovd	xmm0, dword ptr [rbp - 0xec]
 1826ad4:      	mov	rdi, r15
 1826ad7:      	vpinsrd	xmm0, xmm0, r14d, 0x1
 1826add:      	vpshufd	xmm0, xmm0, 0x40        # xmm0 = xmm0[0,0,0,1]
 1826ae2:      	vpor	xmm0, xmm0, xmmword ptr [rip - 0xa50d0a] # 0xdd5de0 <x86_64_metadata_tests.residual_blocked_expected+0x320>
 1826aea:      	vmovdqa	xmmword ptr [rbp - 0xa0], xmm0
 1826af2:      	mov	dword ptr [rbp - 0x90], r12d
 1826af9:      	mov	dword ptr [rbp - 0x8c], 0x83
 1826b03:      	mov	rax, qword ptr [rbp - 0x90]
 1826b0a:      	mov	qword ptr [rsp + 0x10], rax
 1826b0f:      	vmovaps	xmm0, xmmword ptr [rbp - 0xa0]
 1826b17:      	jmp	0x182689a <machine_x64_select_load+0x55a>
 1826b1c:      	xor	eax, eax
 1826b1e:      	cmp	dword ptr [rbp - 0xf0], -0x1
 1826b25:      	je	0x1826a80 <machine_x64_select_load+0x740>
 1826b2b:      	mov	r12b, 0x1
 1826b2e:      	mov	esi, 0xffffffff
 1826b33:      	jmp	0x182651d <machine_x64_select_load+0x1dd>
 1826b38:      	vzeroupper
 1826b3b:      	call	0xfe5430 <__stack_chk_fail$plt>
