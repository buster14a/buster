
../issue58-baseline-ide:	file format elf64-x86-64

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
 1826369:      	mov	r8, qword ptr [rdi + 0x10]
 182636d:      	mov	r9d, dword ptr [rsi + 0x28]
 1826371:      	mov	ebx, dword ptr [rax]
 1826373:      	cmp	ebx, dword ptr [r8 + 0xa0]
 182637a:      	setae	al
 182637d:      	cmp	r9, rcx
 1826380:      	sete	cl
 1826383:      	or	cl, al
 1826385:      	jne	0x1826a62 <machine_x64_select_load+0x722>
 182638b:      	mov	rax, qword ptr [rdi + 0x478]
 1826392:      	movzx	eax, byte ptr [rax + rbx]
 1826396:      	test	al, al
 1826398:      	je	0x1826a62 <machine_x64_select_load+0x722>
 182639e:      	mov	rcx, qword ptr [rdi + 0x360]
 18263a5:      	mov	r10, rsi
 18263a8:      	mov	esi, dword ptr [rsi + 0x18]
 18263ab:      	mov	r14d, dword ptr [rcx + 4*r9]
 18263af:      	mov	r9d, 0xffff0e00
 18263b5:      	cmp	esi, dword ptr [rdi + 0x4e0]
 18263bb:      	jae	0x18263c8 <machine_x64_select_load+0x88>
 18263bd:      	mov	r9, qword ptr [rdi + 0x4d8]
 18263c4:      	mov	r9d, dword ptr [r9 + 4*rsi]
 18263c8:      	mov	ecx, dword ptr [rcx + 4*rbx]
 18263cb:      	movzx	r10d, byte ptr [r10 + 0x34]
 18263d0:      	mov	dword ptr [rbp - 0xf0], r9d
 18263d7:      	mov	dword ptr [rbp - 0xf8], ecx
 18263dd:      	mov	ecx, r9d
 18263e0:      	shr	ecx, 0x8
 18263e3:      	cmp	edx, -0x1
 18263e6:      	mov	dword ptr [rbp - 0xfc], ecx
 18263ec:      	sete	r11b
 18263f0:      	setne	r13b
 18263f4:      	and	r9d, 0x8
 18263f8:      	movzx	ecx, byte ptr [rdi + 0x4c8]
 18263ff:      	shr	r9d, 0x3
 1826403:      	and	r9b, r13b
 1826406:      	xor	r13b, r9b
 1826409:      	cmp	r14d, -0x1
 182640d:      	setne	r12b
 1826411:      	xor	r15d, r15d
 1826414:      	cmp	r10b, 0x8
 1826418:      	movzx	r13d, r13b
 182641c:      	cmove	ecx, r15d
 1826420:      	test	r9b, r9b
 1826423:      	cmove	ecx, r15d
 1826427:      	or	ecx, r13d
 182642a:      	je	0x182657b <machine_x64_select_load+0x23b>
 1826430:      	mov	rcx, qword ptr [rdi + 0x350]
 1826437:      	mov	r8, qword ptr [rdi + 0x368]
 182643e:      	mov	esi, dword ptr [rcx + 4*rbx]
 1826441:      	cmp	esi, -0x1
 1826444:      	sete	cl
 1826447:      	setne	r10b
 182644b:      	cmp	al, 0x1
 182644d:      	sete	r11b
 1826451:      	cmp	dword ptr [r8 + 4*rbx], -0x1
 1826456:      	mov	r8d, r10d
 1826459:      	sete	r15b
 182645d:      	setne	bl
 1826460:      	and	r15b, r11b
 1826463:      	mov	r11d, dword ptr [rbp - 0xf8]
 182646a:      	and	r8b, r15b
 182646d:      	cmp	r11d, -0x1
 1826471:      	setne	r12b
 1826475:      	and	r12b, cl
 1826478:      	and	r12b, r15b
 182647b:      	cmp	al, 0x2
 182647d:      	sete	al
 1826480:      	or	al, bl
 1826482:      	and	al, r10b
 1826485:      	or	al, r12b
 1826488:      	or	al, r8b
 182648b:      	cmp	edx, esi
 182648d:      	sete	cl
 1826490:      	and	cl, r8b
 1826493:      	cmp	cl, al
 1826495:      	je	0x1826a64 <machine_x64_select_load+0x724>
 182649b:      	mov	r14d, dword ptr [rbp - 0xf0]
 18264a2:      	mov	ecx, r12d
 18264a5:      	mov	dword ptr [rbp - 0xec], eax
 18264ab:      	mov	r15d, edx
 18264ae:      	or	r15d, 0x20000000
 18264b5:      	mov	r10d, 0x2d
 18264bb:      	mov	ebx, r11d
 18264be:      	mov	r11d, 0x5
 18264c4:      	mov	r13d, edx
 18264c7:      	not	cl
 18264c9:      	mov	dword ptr [rbp - 0x68], r15d
 18264cd:      	movzx	ecx, cl
 18264d0:      	shl	ecx, 0x1f
 18264d3:      	mov	eax, r14d
 18264d6:      	shr	eax, 0x10
 18264d9:      	test	r9b, r9b
 18264dc:      	mov	r9d, 0x70
 18264e2:      	movzx	eax, al
 18264e5:      	cmovne	r10d, r9d
 18264e9:      	mov	r9d, 0x72
 18264ef:      	mov	qword ptr [rbp - 0x108], rax
 18264f6:      	lea	eax, [rax + 0x32]
 18264f9:      	cmove	r9d, eax
 18264fd:      	mov	eax, 0x6f
 1826502:      	cmovne	r11d, eax
 1826506:      	test	r12b, r12b
 1826509:      	cmovne	esi, ebx
 182650c:      	cmovne	r9d, r10d
 1826510:      	test	r8b, r8b
 1826513:      	mov	rbx, rdi
 1826516:      	lea	eax, [rcx + rsi - 0x60000000]
 182651d:      	cmovne	r9d, r11d
 1826521:      	mov	dword ptr [rbp - 0x64], eax
 1826524:      	mov	qword ptr [rbp - 0x60], 0x0
 182652c:      	mov	dword ptr [rbp - 0x58], 0x0
 1826533:      	mov	word ptr [rbp - 0x54], r9w
 1826538:      	mov	word ptr [rbp - 0x52], 0x0
 182653e:      	mov	rax, qword ptr [rbp - 0x58]
 1826542:      	mov	qword ptr [rsp + 0x10], rax
 1826547:      	vmovups	xmm0, xmmword ptr [rbp - 0x68]
 182654c:      	vmovups	xmmword ptr [rsp], xmm0
 1826551:      	call	0x181dc80 <machine_x64_select_row>
 1826556:      	cmp	r13d, dword ptr [rbx + 0x480]
 182655d:      	jae	0x18267f8 <machine_x64_select_load+0x4b8>
 1826563:      	mov	esi, dword ptr [rbx + 0x68]
 1826566:      	cmp	r13d, esi
 1826569:      	jae	0x18267c5 <machine_x64_select_load+0x485>
 182656f:      	mov	rdx, qword ptr [rbx + 0x418]
 1826576:      	jmp	0x18267df <machine_x64_select_load+0x49f>
 182657b:      	mov	r15d, dword ptr [rbp - 0xf8]
 1826582:      	mov	rdx, qword ptr [rdi + 0x8]
 1826586:      	and	r12b, r11b
 1826589:      	cmp	r10b, 0x8
 182658d:      	mov	dword ptr [rbp - 0xec], r14d
 1826594:      	mov	r9d, r12d
 1826597:      	setne	cl
 182659a:      	xor	r9b, 0x1
 182659e:      	or	r9b, cl
 18265a1:      	jne	0x18265f2 <machine_x64_select_load+0x2b2>
 18265a3:      	mov	rax, qword ptr [r8 + 0x48]
 18265a7:      	mov	rcx, rbx
 18265aa:      	shl	rcx, 0x4
 18265ae:      	mov	r8d, dword ptr [rdx + 0x130]
 18265b5:      	mov	eax, dword ptr [rax + rcx]
 18265b8:      	cmp	eax, r8d
 18265bb:      	jae	0x18266fb <machine_x64_select_load+0x3bb>
 18265c1:      	mov	rcx, qword ptr [rdx + 0x128]
 18265c8:      	mov	r10d, dword ptr [rbp - 0xf0]
 18265cf:      	test	rcx, rcx
 18265d2:      	je	0x1826702 <machine_x64_select_load+0x3c2>
 18265d8:      	shl	rax, 0x7
 18265dc:      	add	rcx, rax
 18265df:      	cmp	byte ptr [rcx + 0x48], 0x1
 18265e3:      	jne	0x1826702 <machine_x64_select_load+0x3c2>
 18265e9:      	mov	rax, qword ptr [rcx + 0x38]
 18265ed:      	jmp	0x1826704 <machine_x64_select_load+0x3c4>
 18265f2:      	test	r12b, r12b
 18265f5:      	je	0x1826a62 <machine_x64_select_load+0x722>
 18265fb:      	mov	rcx, qword ptr [rdx + 0x128]
 1826602:      	mov	rdx, rsi
 1826605:      	shl	rdx, 0x7
 1826609:      	mov	r14, qword ptr [rcx + rdx + 0x38]
 182660e:      	mov	r8, r14
 1826611:      	shr	r8, 0x20
 1826615:      	setne	r8b
 1826619:      	cmp	byte ptr [rcx + rdx + 0x48], 0x0
 182661e:      	sete	cl
 1826621:      	or	cl, r8b
 1826624:      	jne	0x1826a62 <machine_x64_select_load+0x722>
 182662a:      	cmp	al, 0x1
 182662c:      	mov	r9b, 0x1
 182662f:      	sete	cl
 1826632:      	cmp	r15d, -0x1
 1826636:      	setne	dl
 1826639:      	and	dl, cl
 182663b:      	cmp	al, 0x2
 182663d:      	je	0x182664e <machine_x64_select_load+0x30e>
 182663f:      	mov	rax, qword ptr [rdi + 0x368]
 1826646:      	cmp	dword ptr [rax + 4*rbx], -0x1
 182664a:      	setne	r9b
 182664e:      	mov	rax, qword ptr [rdi + 0x350]
 1826655:      	mov	r13d, dword ptr [rbp - 0xec]
 182665c:      	mov	ecx, edx
 182665e:      	not	cl
 1826660:      	mov	r8d, dword ptr [rax + 4*rbx]
 1826664:      	cmp	r8d, -0x1
 1826668:      	setne	al
 182666b:      	and	al, r9b
 182666e:      	and	al, cl
 1826670:      	or	dl, al
 1826672:      	cmp	dl, 0x1
 1826675:      	jne	0x1826a62 <machine_x64_select_load+0x722>
 182667b:      	or	r13d, 0xa0000000
 1826682:      	test	al, al
 1826684:      	mov	ecx, 0x20000000
 1826689:      	mov	edx, 0xa0000000
 182668e:      	mov	rbx, rdi
 1826691:      	cmovne	r15d, r8d
 1826695:      	cmovne	edx, ecx
 1826698:      	mov	dword ptr [rbp - 0xe8], r13d
 182669f:      	or	edx, r15d
 18266a2:      	movzx	r15d, al
 18266a6:      	mov	dword ptr [rbp - 0xe4], edx
 18266ac:      	mov	qword ptr [rbp - 0xe0], 0x0
 18266b7:      	or	r15d, 0x52
 18266bb:      	call	0x181d760 <machine_x64_type_is_f80>
 18266c0:      	test	al, al
 18266c2:      	mov	ecx, 0xa
 18266c7:      	cmove	ecx, r14d
 18266cb:      	mov	dword ptr [rbp - 0xd8], ecx
 18266d1:      	mov	word ptr [rbp - 0xd4], r15w
 18266d9:      	mov	word ptr [rbp - 0xd2], 0x0
 18266e2:      	mov	rax, qword ptr [rbp - 0xd8]
 18266e9:      	mov	qword ptr [rsp + 0x10], rax
 18266ee:      	vmovups	xmm0, xmmword ptr [rbp - 0xe8]
 18266f6:      	jmp	0x1826a5a <machine_x64_select_load+0x71a>
 18266fb:      	mov	r10d, dword ptr [rbp - 0xf0]
 1826702:      	xor	eax, eax
 1826704:      	movzx	ecx, byte ptr [rbp - 0xfc]
 182670b:      	popcnt	r9, rax
 1826710:      	mov	r11w, 0x31
 1826715:      	cmp	r9d, 0x1
 1826719:      	jne	0x182673b <machine_x64_select_load+0x3fb>
 182671b:      	tzcnt	r9, rax
 1826720:      	cmp	r9, 0x3
 1826724:      	ja	0x182673b <machine_x64_select_load+0x3fb>
 1826726:      	add	r9d, 0x32
 182672a:      	mov	r15b, 0x1
 182672d:      	mov	r11d, r9d
 1826730:      	lea	r12d, [rcx - 0xb]
 1826734:      	cmp	esi, r8d
 1826737:      	jb	0x1826747 <machine_x64_select_load+0x407>
 1826739:      	jmp	0x1826766 <machine_x64_select_load+0x426>
 182673b:      	xor	r15d, r15d
 182673e:      	lea	r12d, [rcx - 0xb]
 1826742:      	cmp	esi, r8d
 1826745:      	jae	0x1826766 <machine_x64_select_load+0x426>
 1826747:      	mov	rdx, qword ptr [rdx + 0x128]
 182674e:      	test	rdx, rdx
 1826751:      	je	0x1826766 <machine_x64_select_load+0x426>
 1826753:      	shl	rsi, 0x7
 1826757:      	add	rdx, rsi
 182675a:      	cmp	byte ptr [rdx + 0x48], 0x1
 182675e:      	jne	0x1826766 <machine_x64_select_load+0x426>
 1826760:      	mov	r13, qword ptr [rdx + 0x38]
 1826764:      	jmp	0x1826769 <machine_x64_select_load+0x429>
 1826766:      	xor	r13d, r13d
 1826769:      	cmp	r12d, 0x2
 182676d:      	setb	dl
 1826770:      	cmp	ecx, 0x2
 1826773:      	sete	cl
 1826776:      	and	r10d, 0x40
 182677a:      	shr	r10d, 0x6
 182677e:      	and	r10b, cl
 1826781:      	or	r10b, dl
 1826784:      	cmp	r10b, 0x1
 1826788:      	jne	0x18268ec <machine_x64_select_load+0x5ac>
 182678e:      	cmp	rax, 0x10
 1826792:      	jne	0x18268ec <machine_x64_select_load+0x5ac>
 1826798:      	lea	rax, [r13 - 0x9]
 182679c:      	cmp	rax, 0x7
 18267a0:      	ja	0x18268ec <machine_x64_select_load+0x5ac>
 18267a6:      	cmp	byte ptr [rdi + 0x494], 0x1
 18267ad:      	jne	0x182688a <machine_x64_select_load+0x54a>
 18267b3:      	vmovups	ymm0, ymmword ptr [rdi + 0x4a0]
 18267bb:      	vmovups	ymmword ptr [rbp - 0x50], ymm0
 18267c0:      	jmp	0x18268e1 <machine_x64_select_load+0x5a1>
 18267c5:      	lea	rdx, [rbx + 0x50]
 18267c9:      	add	r13d, esi
 18267cc:      	nop	dword ptr [rax]
 18267d0:      	mov	rdx, qword ptr [rdx]
 18267d3:      	sub	r13d, esi
 18267d6:      	cmp	r13d, esi
 18267d9:      	jae	0x18267d0 <machine_x64_select_load+0x490>
 18267db:      	add	rdx, 0x10
 18267df:      	mov	ecx, r13d
 18267e2:      	shl	rcx, 0x4
 18267e6:      	cmp	dword ptr [rdx + rcx], -0x1
 18267ea:      	jne	0x18267f8 <machine_x64_select_load+0x4b8>
 18267ec:      	lea	eax, [4*rax + 0x3]
 18267f3:      	add	rdx, rcx
 18267f6:      	mov	dword ptr [rdx], eax
 18267f8:      	movzx	eax, byte ptr [rbp - 0xfc]
 18267ff:      	test	r14b, 0x20
 1826803:      	mov	edx, 0x60b0a
 1826808:      	mov	esi, 0x60807
 182680d:      	cmove	rsi, rdx
 1826811:      	cmp	eax, 0x2
 1826814:      	lea	ecx, [rax - 0x1]
 1826817:      	cmovne	rsi, rdx
 182681b:      	xor	eax, eax
 182681d:      	cmp	ecx, 0x2
 1826820:      	mov	rcx, qword ptr [rbp - 0x108]
 1826827:      	cmovb	rax, rsi
 182682b:      	shl	ecx, 0x3
 182682e:      	shrx	rax, rax, rcx
 1826833:      	and	ax, 0xff
 1826837:      	sete	cl
 182683a:      	xor	r12b, 0x1
 182683e:      	or	r12b, cl
 1826841:      	jne	0x182687f <machine_x64_select_load+0x53f>
 1826843:      	mov	dword ptr [rbp - 0x80], r15d
 1826847:      	mov	dword ptr [rbp - 0x7c], r15d
 182684b:      	mov	qword ptr [rbp - 0x78], 0x0
 1826853:      	mov	dword ptr [rbp - 0x70], 0x0
 182685a:      	mov	word ptr [rbp - 0x6c], ax
 182685e:      	mov	word ptr [rbp - 0x6a], 0x0
 1826864:      	mov	rdi, rbx
 1826867:      	mov	rax, qword ptr [rbp - 0x70]
 182686b:      	mov	qword ptr [rsp + 0x10], rax
 1826870:      	vmovups	xmm0, xmmword ptr [rbp - 0x80]
 1826875:      	vmovups	xmmword ptr [rsp], xmm0
 182687a:      	call	0x181dc80 <machine_x64_select_row>
 182687f:      	mov	eax, dword ptr [rbp - 0xec]
 1826885:      	jmp	0x1826a64 <machine_x64_select_load+0x724>
 182688a:      	mov	esi, dword ptr [rdi + 0x488]
 1826890:      	mov	edx, dword ptr [rdi + 0x48c]
 1826896:      	cmp	edx, 0x2
 1826899:      	jne	0x18268bb <machine_x64_select_load+0x57b>
 182689b:      	cmp	esi, dword ptr [rip + 0x4c3e5f] # 0x1cea700 <target_native>
 18268a1:      	jne	0x18268bb <machine_x64_select_load+0x57b>
 18268a3:      	cmp	byte ptr [rip + 0x4c3e62], 0x0 # 0x1cea70c <target_native+0xc>
 18268aa:      	je	0x18268bb <machine_x64_select_load+0x57b>
 18268ac:      	vmovups	ymm0, ymmword ptr [rip + 0x4c3e64] # 0x1cea718 <target_native+0x18>
 18268b4:      	vmovups	ymmword ptr [rbp - 0x50], ymm0
 18268b9:      	jmp	0x18268e1 <machine_x64_select_load+0x5a1>
 18268bb:      	lea	rax, [rbp - 0x50]
 18268bf:      	mov	qword ptr [rbp - 0xf8], rdi
 18268c6:      	mov	r14, r13
 18268c9:      	mov	r13d, r11d
 18268cc:      	mov	rdi, rax
 18268cf:      	call	0x1475a90 <target_cpu_features_default>
 18268d4:      	mov	rdi, qword ptr [rbp - 0xf8]
 18268db:      	mov	r11d, r13d
 18268de:      	mov	r13, r14
 18268e1:      	cmp	word ptr [rbp - 0x50], 0x0
 18268e6:      	js	0x1826a8c <machine_x64_select_load+0x74c>
 18268ec:      	cmp	r12d, 0x2
 18268f0:      	setae	al
 18268f3:      	xor	r15b, 0x1
 18268f7:      	or	r15b, al
 18268fa:      	jne	0x1826a62 <machine_x64_select_load+0x722>
 1826900:      	mov	r15d, dword ptr [rdi + 0x30]
 1826904:      	mov	r14d, dword ptr [rdi + 0x60]
 1826908:      	mov	rax, qword ptr [rdi + 0x70]
 182690c:      	mov	word ptr [rbp - 0xf8], r11w
 1826914:      	cmp	rax, qword ptr [rdi + 0x78]
 1826918:      	jne	0x1826959 <machine_x64_select_load+0x619>
 182691a:      	mov	rax, qword ptr [rdi + 0x18]
 182691e:      	mov	rcx, qword ptr [rdi + 0x58]
 1826922:      	lea	rsi, [rdi + 0x50]
 1826926:      	test	rcx, rcx
 1826929:      	je	0x1826931 <machine_x64_select_load+0x5f1>
 182692b:      	mov	edx, dword ptr [rdi + 0x68]
 182692e:      	mov	dword ptr [rcx + 0x8], edx
 1826931:      	mov	r12, rdi
 1826934:      	mov	rdi, rax
 1826937:      	vzeroupper
 182693a:      	call	0x183aa70 <machine_stream_chunk_push>
 182693f:      	mov	ecx, dword ptr [r12 + 0x68]
 1826944:      	mov	rdi, r12
 1826947:      	shl	rcx, 0x4
 182694b:      	lea	rcx, [rax + rcx + 0x10]
 1826950:      	add	rax, 0x10
 1826954:      	mov	qword ptr [r12 + 0x78], rcx
 1826959:      	mov	ecx, 0xffffffff
 182695e:      	lea	rsi, [rax + 0x10]
 1826962:      	lea	rdx, [rcx + 4*r15 + 0x4]
 1826967:      	mov	qword ptr [rdi + 0x70], rsi
 182696b:      	lea	esi, [r14 + 0x1]
 182696f:      	mov	r15, rdi
 1826972:      	mov	dword ptr [rdi + 0x60], esi
 1826975:      	mov	esi, ebx
 1826977:      	mov	qword ptr [rax], rdx
 182697a:      	mov	qword ptr [rax + 0x8], rcx
 182697e:      	mov	edx, r14d
 1826981:      	xor	ecx, ecx
 1826983:      	vzeroupper
 1826986:      	call	0x18441d0 <machine_x64_select_place_address_offset>
 182698b:      	mov	ecx, eax
 182698d:      	xor	eax, eax
 182698f:      	test	cl, cl
 1826991:      	je	0x1826a64 <machine_x64_select_load+0x724>
 1826997:      	mov	rdi, r15
 182699a:      	mov	rbx, r15
 182699d:      	call	0x181dfc0 <machine_x64_synthesize_register>
 18269a2:      	movzx	ecx, word ptr [rbp - 0xf8]
 18269a9:      	mov	r15d, eax
 18269ac:      	or	r15d, 0x20000000
 18269b3:      	or	r14d, 0x20000000
 18269ba:      	mov	rdi, rbx
 18269bd:      	mov	dword ptr [rbp - 0xb8], r15d
 18269c4:      	mov	dword ptr [rbp - 0xb4], r14d
 18269cb:      	mov	qword ptr [rbp - 0xb0], 0x0
 18269d6:      	mov	dword ptr [rbp - 0xa8], 0x0
 18269e0:      	mov	word ptr [rbp - 0xa4], cx
 18269e7:      	mov	word ptr [rbp - 0xa2], 0x0
 18269f0:      	mov	rax, qword ptr [rbp - 0xa8]
 18269f7:      	mov	qword ptr [rsp + 0x10], rax
 18269fc:      	vmovups	xmm0, xmmword ptr [rbp - 0xb8]
 1826a04:      	vmovups	xmmword ptr [rsp], xmm0
 1826a09:      	call	0x181dc80 <machine_x64_select_row>
 1826a0e:      	mov	r13d, dword ptr [rbp - 0xec]
 1826a15:      	movabs	rax, 0x3100000000
 1826a1f:      	or	r13d, 0xa0000000
 1826a26:      	mov	dword ptr [rbp - 0xd0], r13d
 1826a2d:      	mov	dword ptr [rbp - 0xcc], r15d
 1826a34:      	mov	qword ptr [rbp - 0xc8], 0x0
 1826a3f:      	mov	qword ptr [rbp - 0xc0], rax
 1826a46:      	mov	rax, qword ptr [rbp - 0xc0]
 1826a4d:      	mov	qword ptr [rsp + 0x10], rax
 1826a52:      	vmovups	xmm0, xmmword ptr [rbp - 0xd0]
 1826a5a:      	mov	rdi, rbx
 1826a5d:      	jmp	0x1826afe <machine_x64_select_load+0x7be>
 1826a62:      	xor	eax, eax
 1826a64:      	mov	rcx, qword ptr fs:[0x28]
 1826a6d:      	cmp	rcx, qword ptr [rbp - 0x30]
 1826a71:      	jne	0x1826b0f <machine_x64_select_load+0x7cf>
 1826a77:      	add	rsp, 0xf8
 1826a7e:      	pop	rbx
 1826a7f:      	pop	r12
 1826a81:      	pop	r13
 1826a83:      	pop	r14
 1826a85:      	pop	r15
 1826a87:      	pop	rbp
 1826a88:      	vzeroupper
 1826a8b:      	ret
 1826a8c:      	mov	r12, rdi
 1826a8f:      	vzeroupper
 1826a92:      	call	0x181dfc0 <machine_x64_synthesize_register>
 1826a97:      	mov	r15d, eax
 1826a9a:      	mov	r14, r12
 1826a9d:      	mov	rdi, r12
 1826aa0:      	mov	esi, ebx
 1826aa2:      	mov	edx, eax
 1826aa4:      	xor	ecx, ecx
 1826aa6:      	call	0x18441d0 <machine_x64_select_place_address_offset>
 1826aab:      	mov	ecx, eax
 1826aad:      	xor	eax, eax
 1826aaf:      	test	cl, cl
 1826ab1:      	je	0x1826a64 <machine_x64_select_load+0x724>
 1826ab3:      	vmovd	xmm0, dword ptr [rbp - 0xec]
 1826abb:      	mov	rdi, r14
 1826abe:      	vpinsrd	xmm0, xmm0, r15d, 0x1
 1826ac4:      	vpshufd	xmm0, xmm0, 0x40        # xmm0 = xmm0[0,0,0,1]
 1826ac9:      	vpor	xmm0, xmm0, xmmword ptr [rip - 0xa50cf1] # 0xdd5de0 <x86_64_metadata_tests.residual_blocked_expected+0x320>
 1826ad1:      	vmovdqa	xmmword ptr [rbp - 0xa0], xmm0
 1826ad9:      	mov	dword ptr [rbp - 0x90], r13d
 1826ae0:      	mov	dword ptr [rbp - 0x8c], 0x83
 1826aea:      	mov	rax, qword ptr [rbp - 0x90]
 1826af1:      	mov	qword ptr [rsp + 0x10], rax
 1826af6:      	vmovaps	xmm0, xmmword ptr [rbp - 0xa0]
 1826afe:      	vmovups	xmmword ptr [rsp], xmm0
 1826b03:      	call	0x181dc80 <machine_x64_select_row>
 1826b08:      	mov	al, 0x1
 1826b0a:      	jmp	0x1826a64 <machine_x64_select_load+0x724>
 1826b0f:      	vzeroupper
 1826b12:      	call	0xfe5430 <__stack_chk_fail$plt>
 1826b17:      	nop	word ptr [rax + rax]
