
../issue58-b-ide:	file format elf64-x86-64

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
 1826385:      	jne	0x1826a8e <machine_x64_select_load+0x74e>
 182638b:      	mov	rax, qword ptr [rdi + 0x478]
 1826392:      	movzx	eax, byte ptr [rax + rbx]
 1826396:      	test	al, al
 1826398:      	je	0x1826a8e <machine_x64_select_load+0x74e>
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
 18263d0:      	mov	dword ptr [rbp - 0xf0], r8d
 18263d7:      	mov	dword ptr [rbp - 0xf4], ecx
 18263dd:      	mov	ecx, r8d
 18263e0:      	shr	ecx, 0x8
 18263e3:      	cmp	edx, -0x1
 18263e6:      	mov	dword ptr [rbp - 0xfc], ecx
 18263ec:      	sete	r11b
 18263f0:      	setne	r12b
 18263f4:      	and	r8d, 0x8
 18263f8:      	movzx	ecx, byte ptr [rdi + 0x4c8]
 18263ff:      	shr	r8d, 0x3
 1826403:      	and	r8b, r12b
 1826406:      	xor	r12b, r8b
 1826409:      	cmp	r14d, -0x1
 182640d:      	setne	r13b
 1826411:      	xor	r15d, r15d
 1826414:      	cmp	r10b, 0x8
 1826418:      	movzx	r12d, r12b
 182641c:      	cmove	ecx, r15d
 1826420:      	test	r8b, r8b
 1826423:      	cmove	ecx, r15d
 1826427:      	or	ecx, r12d
 182642a:      	je	0x182648b <machine_x64_select_load+0x14b>
 182642c:      	mov	r9, qword ptr [rdi + 0x368]
 1826433:      	mov	rcx, qword ptr [rdi + 0x350]
 182643a:      	cmp	al, 0x1
 182643c:      	mov	r9d, dword ptr [r9 + 4*rbx]
 1826440:      	mov	esi, dword ptr [rcx + 4*rbx]
 1826443:      	setne	cl
 1826446:      	cmp	r9d, -0x1
 182644a:      	setne	r10b
 182644e:      	or	r10b, cl
 1826451:      	je	0x18264fb <machine_x64_select_load+0x1bb>
 1826457:      	cmp	r9d, -0x1
 182645b:      	mov	r14d, dword ptr [rbp - 0xf0]
 1826462:      	setne	cl
 1826465:      	cmp	esi, -0x1
 1826468:      	setne	r10b
 182646c:      	cmp	al, 0x2
 182646e:      	sete	r9b
 1826472:      	xor	r12d, r12d
 1826475:      	or	r9b, cl
 1826478:      	and	r9b, r10b
 182647b:      	xor	eax, eax
 182647d:      	test	r12b, r12b
 1826480:      	je	0x1826645 <machine_x64_select_load+0x305>
 1826486:      	jmp	0x182664e <machine_x64_select_load+0x30e>
 182648b:      	mov	rdx, qword ptr [rdi + 0x8]
 182648f:      	and	r13b, r11b
 1826492:      	cmp	r10b, 0x8
 1826496:      	mov	dword ptr [rbp - 0xf8], r14d
 182649d:      	mov	r8d, r13d
 18264a0:      	setne	cl
 18264a3:      	xor	r8b, 0x1
 18264a7:      	or	r8b, cl
 18264aa:      	jne	0x182651d <machine_x64_select_load+0x1dd>
 18264ac:      	mov	rax, qword ptr [r9 + 0x48]
 18264b0:      	mov	rcx, rbx
 18264b3:      	shl	rcx, 0x4
 18264b7:      	mov	r8d, dword ptr [rdx + 0x130]
 18264be:      	mov	eax, dword ptr [rax + rcx]
 18264c1:      	cmp	eax, r8d
 18264c4:      	jae	0x182671f <machine_x64_select_load+0x3df>
 18264ca:      	mov	rcx, qword ptr [rdx + 0x128]
 18264d1:      	mov	r10d, dword ptr [rbp - 0xf0]
 18264d8:      	test	rcx, rcx
 18264db:      	je	0x1826726 <machine_x64_select_load+0x3e6>
 18264e1:      	shl	rax, 0x7
 18264e5:      	add	rcx, rax
 18264e8:      	cmp	byte ptr [rcx + 0x48], 0x1
 18264ec:      	jne	0x1826726 <machine_x64_select_load+0x3e6>
 18264f2:      	mov	rax, qword ptr [rcx + 0x38]
 18264f6:      	jmp	0x1826728 <machine_x64_select_load+0x3e8>
 18264fb:      	mov	r14d, dword ptr [rbp - 0xf0]
 1826502:      	cmp	esi, -0x1
 1826505:      	je	0x1826630 <machine_x64_select_load+0x2f0>
 182650b:      	mov	al, 0x1
 182650d:      	cmp	edx, esi
 182650f:      	je	0x1826a90 <machine_x64_select_load+0x750>
 1826515:      	xor	r12d, r12d
 1826518:      	jmp	0x182664e <machine_x64_select_load+0x30e>
 182651d:      	test	r13b, r13b
 1826520:      	je	0x1826a8e <machine_x64_select_load+0x74e>
 1826526:      	mov	rcx, qword ptr [rdx + 0x128]
 182652d:      	mov	rdx, rsi
 1826530:      	shl	rdx, 0x7
 1826534:      	mov	r14, qword ptr [rcx + rdx + 0x38]
 1826539:      	mov	r8, r14
 182653c:      	shr	r8, 0x20
 1826540:      	setne	r8b
 1826544:      	cmp	byte ptr [rcx + rdx + 0x48], 0x0
 1826549:      	sete	cl
 182654c:      	or	cl, r8b
 182654f:      	jne	0x1826a8e <machine_x64_select_load+0x74e>
 1826555:      	cmp	al, 0x1
 1826557:      	mov	r9b, 0x1
 182655a:      	sete	cl
 182655d:      	cmp	dword ptr [rbp - 0xf4], -0x1
 1826564:      	setne	dl
 1826567:      	and	dl, cl
 1826569:      	cmp	al, 0x2
 182656b:      	je	0x182657c <machine_x64_select_load+0x23c>
 182656d:      	mov	rax, qword ptr [rdi + 0x368]
 1826574:      	cmp	dword ptr [rax + 4*rbx], -0x1
 1826578:      	setne	r9b
 182657c:      	mov	rax, qword ptr [rdi + 0x350]
 1826583:      	mov	r12d, dword ptr [rbp - 0xf8]
 182658a:      	mov	ecx, edx
 182658c:      	not	cl
 182658e:      	mov	r8d, dword ptr [rax + 4*rbx]
 1826592:      	cmp	r8d, -0x1
 1826596:      	setne	al
 1826599:      	and	al, r9b
 182659c:      	and	al, cl
 182659e:      	or	dl, al
 18265a0:      	cmp	dl, 0x1
 18265a3:      	jne	0x1826a8e <machine_x64_select_load+0x74e>
 18265a9:      	mov	r9d, dword ptr [rbp - 0xf4]
 18265b0:      	or	r12d, 0xa0000000
 18265b7:      	test	al, al
 18265b9:      	mov	ecx, 0x20000000
 18265be:      	mov	edx, 0xa0000000
 18265c3:      	movzx	r15d, al
 18265c7:      	mov	rbx, rdi
 18265ca:      	cmovne	edx, ecx
 18265cd:      	mov	dword ptr [rbp - 0xe8], r12d
 18265d4:      	cmovne	r9d, r8d
 18265d8:      	or	r15d, 0x52
 18265dc:      	or	edx, r9d
 18265df:      	mov	dword ptr [rbp - 0xe4], edx
 18265e5:      	mov	qword ptr [rbp - 0xe0], 0x0
 18265f0:      	call	0x181d760 <machine_x64_type_is_f80>
 18265f5:      	test	al, al
 18265f7:      	mov	ecx, 0xa
 18265fc:      	cmove	ecx, r14d
 1826600:      	mov	dword ptr [rbp - 0xd8], ecx
 1826606:      	mov	word ptr [rbp - 0xd4], r15w
 182660e:      	mov	word ptr [rbp - 0xd2], 0x0
 1826617:      	mov	rax, qword ptr [rbp - 0xd8]
 182661e:      	mov	qword ptr [rsp + 0x10], rax
 1826623:      	vmovups	xmm0, xmmword ptr [rbp - 0xe8]
 182662b:      	jmp	0x18268a7 <machine_x64_select_load+0x567>
 1826630:      	cmp	dword ptr [rbp - 0xf4], -0x1
 1826637:      	setne	r12b
 182663b:      	xor	r9d, r9d
 182663e:      	xor	eax, eax
 1826640:      	test	r12b, r12b
 1826643:      	jne	0x182664e <machine_x64_select_load+0x30e>
 1826645:      	test	r9b, r9b
 1826648:      	je	0x1826a90 <machine_x64_select_load+0x750>
 182664e:      	mov	ecx, r14d
 1826651:      	shr	ecx, 0x10
 1826654:      	test	r8b, r8b
 1826657:      	mov	r8d, 0x6f
 182665d:      	mov	r9d, 0x5
 1826663:      	mov	r10d, 0x2d
 1826669:      	mov	r13d, edx
 182666c:      	movzx	r15d, cl
 1826670:      	cmovne	r9d, r8d
 1826674:      	mov	r8d, 0x70
 182667a:      	cmovne	r10d, r8d
 182667e:      	lea	ecx, [r15 + 0x32]
 1826682:      	mov	r8d, 0x72
 1826688:      	cmove	r8d, ecx
 182668c:      	test	r12b, r12b
 182668f:      	cmovne	r8d, r10d
 1826693:      	test	al, al
 1826695:      	mov	eax, edx
 1826697:      	cmovne	r8d, r9d
 182669b:      	or	eax, 0x20000000
 18266a0:      	test	r12b, r12b
 18266a3:      	je	0x18266b3 <machine_x64_select_load+0x373>
 18266a5:      	mov	esi, dword ptr [rbp - 0xf4]
 18266ab:      	or	esi, 0xa0000000
 18266b1:      	jmp	0x18266b9 <machine_x64_select_load+0x379>
 18266b3:      	or	esi, 0x20000000
 18266b9:      	mov	dword ptr [rbp - 0x68], eax
 18266bc:      	mov	dword ptr [rbp - 0x64], esi
 18266bf:      	mov	qword ptr [rbp - 0x60], 0x0
 18266c7:      	mov	dword ptr [rbp - 0x58], 0x0
 18266ce:      	mov	word ptr [rbp - 0x54], r8w
 18266d3:      	mov	dword ptr [rbp - 0xf0], eax
 18266d9:      	mov	word ptr [rbp - 0x52], 0x0
 18266df:      	mov	rbx, rdi
 18266e2:      	mov	rax, qword ptr [rbp - 0x58]
 18266e6:      	mov	qword ptr [rsp + 0x10], rax
 18266eb:      	vmovups	xmm0, xmmword ptr [rbp - 0x68]
 18266f0:      	vmovups	xmmword ptr [rsp], xmm0
 18266f5:      	call	0x181dc80 <machine_x64_select_row>
 18266fa:      	cmp	r13d, dword ptr [rbx + 0x480]
 1826701:      	jae	0x1826828 <machine_x64_select_load+0x4e8>
 1826707:      	mov	esi, dword ptr [rbx + 0x68]
 182670a:      	cmp	r13d, esi
 182670d:      	jae	0x18267ea <machine_x64_select_load+0x4aa>
 1826713:      	mov	rcx, qword ptr [rbx + 0x418]
 182671a:      	jmp	0x182680f <machine_x64_select_load+0x4cf>
 182671f:      	mov	r10d, dword ptr [rbp - 0xf0]
 1826726:      	xor	eax, eax
 1826728:      	movzx	ecx, byte ptr [rbp - 0xfc]
 182672f:      	popcnt	r9, rax
 1826734:      	mov	r11w, 0x31
 1826739:      	cmp	r9d, 0x1
 182673d:      	jne	0x182675f <machine_x64_select_load+0x41f>
 182673f:      	tzcnt	r9, rax
 1826744:      	cmp	r9, 0x3
 1826748:      	ja	0x182675f <machine_x64_select_load+0x41f>
 182674a:      	add	r9d, 0x32
 182674e:      	mov	r15b, 0x1
 1826751:      	mov	r11d, r9d
 1826754:      	lea	r13d, [rcx - 0xb]
 1826758:      	cmp	esi, r8d
 182675b:      	jb	0x182676b <machine_x64_select_load+0x42b>
 182675d:      	jmp	0x182678a <machine_x64_select_load+0x44a>
 182675f:      	xor	r15d, r15d
 1826762:      	lea	r13d, [rcx - 0xb]
 1826766:      	cmp	esi, r8d
 1826769:      	jae	0x182678a <machine_x64_select_load+0x44a>
 182676b:      	mov	rdx, qword ptr [rdx + 0x128]
 1826772:      	test	rdx, rdx
 1826775:      	je	0x182678a <machine_x64_select_load+0x44a>
 1826777:      	shl	rsi, 0x7
 182677b:      	add	rdx, rsi
 182677e:      	cmp	byte ptr [rdx + 0x48], 0x1
 1826782:      	jne	0x182678a <machine_x64_select_load+0x44a>
 1826784:      	mov	r12, qword ptr [rdx + 0x38]
 1826788:      	jmp	0x182678d <machine_x64_select_load+0x44d>
 182678a:      	xor	r12d, r12d
 182678d:      	cmp	r13d, 0x2
 1826791:      	setb	dl
 1826794:      	cmp	ecx, 0x2
 1826797:      	sete	cl
 182679a:      	and	r10d, 0x40
 182679e:      	shr	r10d, 0x6
 18267a2:      	and	r10b, cl
 18267a5:      	or	r10b, dl
 18267a8:      	cmp	r10b, 0x1
 18267ac:      	jne	0x182691d <machine_x64_select_load+0x5dd>
 18267b2:      	cmp	rax, 0x10
 18267b6:      	jne	0x182691d <machine_x64_select_load+0x5dd>
 18267bc:      	lea	rax, [r12 - 0x9]
 18267c1:      	cmp	rax, 0x7
 18267c5:      	ja	0x182691d <machine_x64_select_load+0x5dd>
 18267cb:      	cmp	byte ptr [rdi + 0x494], 0x1
 18267d2:      	jne	0x18268bb <machine_x64_select_load+0x57b>
 18267d8:      	vmovups	ymm0, ymmword ptr [rdi + 0x4a0]
 18267e0:      	vmovups	ymmword ptr [rbp - 0x50], ymm0
 18267e5:      	jmp	0x1826912 <machine_x64_select_load+0x5d2>
 18267ea:      	lea	rcx, [rbx + 0x50]
 18267ee:      	add	r13d, esi
 18267f1:      	nop	word ptr cs:[rax + rax]
 1826800:      	mov	rcx, qword ptr [rcx]
 1826803:      	sub	r13d, esi
 1826806:      	cmp	r13d, esi
 1826809:      	jae	0x1826800 <machine_x64_select_load+0x4c0>
 182680b:      	add	rcx, 0x10
 182680f:      	mov	edx, r13d
 1826812:      	shl	rdx, 0x4
 1826816:      	cmp	dword ptr [rcx + rdx], -0x1
 182681a:      	jne	0x1826828 <machine_x64_select_load+0x4e8>
 182681c:      	lea	eax, [4*rax + 0x3]
 1826823:      	add	rcx, rdx
 1826826:      	mov	dword ptr [rcx], eax
 1826828:      	movzx	eax, byte ptr [rbp - 0xfc]
 182682f:      	test	r14b, 0x20
 1826833:      	mov	edx, 0x60b0a
 1826838:      	mov	esi, 0x60807
 182683d:      	cmove	rsi, rdx
 1826841:      	cmp	eax, 0x2
 1826844:      	lea	ecx, [rax - 0x1]
 1826847:      	cmovne	rsi, rdx
 182684b:      	xor	eax, eax
 182684d:      	cmp	ecx, 0x2
 1826850:      	cmovb	rax, rsi
 1826854:      	shl	r15d, 0x3
 1826858:      	shrx	rcx, rax, r15
 182685d:      	mov	al, 0x1
 182685f:      	and	cx, 0xff
 1826864:      	sete	dl
 1826867:      	xor	r12b, 0x1
 182686b:      	or	r12b, dl
 182686e:      	jne	0x1826a90 <machine_x64_select_load+0x750>
 1826874:      	mov	eax, dword ptr [rbp - 0xf0]
 182687a:      	mov	dword ptr [rbp - 0x80], eax
 182687d:      	mov	dword ptr [rbp - 0x7c], eax
 1826880:      	mov	qword ptr [rbp - 0x78], 0x0
 1826888:      	mov	dword ptr [rbp - 0x70], 0x0
 182688f:      	mov	word ptr [rbp - 0x6c], cx
 1826893:      	mov	word ptr [rbp - 0x6a], 0x0
 1826899:      	mov	rax, qword ptr [rbp - 0x70]
 182689d:      	mov	qword ptr [rsp + 0x10], rax
 18268a2:      	vmovups	xmm0, xmmword ptr [rbp - 0x80]
 18268a7:      	mov	rdi, rbx
 18268aa:      	vmovups	xmmword ptr [rsp], xmm0
 18268af:      	call	0x181dc80 <machine_x64_select_row>
 18268b4:      	mov	al, 0x1
 18268b6:      	jmp	0x1826a90 <machine_x64_select_load+0x750>
 18268bb:      	mov	esi, dword ptr [rdi + 0x488]
 18268c1:      	mov	edx, dword ptr [rdi + 0x48c]
 18268c7:      	cmp	edx, 0x2
 18268ca:      	jne	0x18268ec <machine_x64_select_load+0x5ac>
 18268cc:      	cmp	esi, dword ptr [rip + 0x4c3e4e] # 0x1cea720 <target_native>
 18268d2:      	jne	0x18268ec <machine_x64_select_load+0x5ac>
 18268d4:      	cmp	byte ptr [rip + 0x4c3e51], 0x0 # 0x1cea72c <target_native+0xc>
 18268db:      	je	0x18268ec <machine_x64_select_load+0x5ac>
 18268dd:      	vmovups	ymm0, ymmword ptr [rip + 0x4c3e53] # 0x1cea738 <target_native+0x18>
 18268e5:      	vmovups	ymmword ptr [rbp - 0x50], ymm0
 18268ea:      	jmp	0x1826912 <machine_x64_select_load+0x5d2>
 18268ec:      	lea	rax, [rbp - 0x50]
 18268f0:      	mov	qword ptr [rbp - 0xf0], rdi
 18268f7:      	mov	r14, r12
 18268fa:      	mov	r12d, r11d
 18268fd:      	mov	rdi, rax
 1826900:      	call	0x1475a90 <target_cpu_features_default>
 1826905:      	mov	rdi, qword ptr [rbp - 0xf0]
 182690c:      	mov	r11d, r12d
 182690f:      	mov	r12, r14
 1826912:      	cmp	word ptr [rbp - 0x50], 0x0
 1826917:      	js	0x1826ab8 <machine_x64_select_load+0x778>
 182691d:      	cmp	r13d, 0x2
 1826921:      	setae	al
 1826924:      	xor	r15b, 0x1
 1826928:      	or	r15b, al
 182692b:      	jne	0x1826a8e <machine_x64_select_load+0x74e>
 1826931:      	mov	r15d, dword ptr [rdi + 0x30]
 1826935:      	mov	r14d, dword ptr [rdi + 0x60]
 1826939:      	mov	rax, qword ptr [rdi + 0x70]
 182693d:      	mov	word ptr [rbp - 0xf0], r11w
 1826945:      	cmp	rax, qword ptr [rdi + 0x78]
 1826949:      	jne	0x1826988 <machine_x64_select_load+0x648>
 182694b:      	mov	rax, qword ptr [rdi + 0x18]
 182694f:      	mov	rcx, qword ptr [rdi + 0x58]
 1826953:      	lea	rsi, [rdi + 0x50]
 1826957:      	test	rcx, rcx
 182695a:      	je	0x1826962 <machine_x64_select_load+0x622>
 182695c:      	mov	edx, dword ptr [rdi + 0x68]
 182695f:      	mov	dword ptr [rcx + 0x8], edx
 1826962:      	mov	r13, rdi
 1826965:      	mov	rdi, rax
 1826968:      	vzeroupper
 182696b:      	call	0x183aa90 <machine_stream_chunk_push>
 1826970:      	mov	ecx, dword ptr [r13 + 0x68]
 1826974:      	mov	rdi, r13
 1826977:      	shl	rcx, 0x4
 182697b:      	lea	rcx, [rax + rcx + 0x10]
 1826980:      	add	rax, 0x10
 1826984:      	mov	qword ptr [r13 + 0x78], rcx
 1826988:      	mov	ecx, 0xffffffff
 182698d:      	lea	rsi, [rax + 0x10]
 1826991:      	lea	rdx, [rcx + 4*r15 + 0x4]
 1826996:      	mov	qword ptr [rdi + 0x70], rsi
 182699a:      	lea	esi, [r14 + 0x1]
 182699e:      	mov	r15, rdi
 18269a1:      	mov	dword ptr [rdi + 0x60], esi
 18269a4:      	mov	esi, ebx
 18269a6:      	mov	qword ptr [rax], rdx
 18269a9:      	mov	qword ptr [rax + 0x8], rcx
 18269ad:      	mov	edx, r14d
 18269b0:      	xor	ecx, ecx
 18269b2:      	vzeroupper
 18269b5:      	call	0x18441f0 <machine_x64_select_place_address_offset>
 18269ba:      	mov	ecx, eax
 18269bc:      	xor	eax, eax
 18269be:      	test	cl, cl
 18269c0:      	je	0x1826a90 <machine_x64_select_load+0x750>
 18269c6:      	mov	rdi, r15
 18269c9:      	mov	rbx, r15
 18269cc:      	call	0x181dfc0 <machine_x64_synthesize_register>
 18269d1:      	movzx	ecx, word ptr [rbp - 0xf0]
 18269d8:      	mov	r15d, eax
 18269db:      	or	r15d, 0x20000000
 18269e2:      	or	r14d, 0x20000000
 18269e9:      	mov	rdi, rbx
 18269ec:      	mov	dword ptr [rbp - 0xb8], r15d
 18269f3:      	mov	dword ptr [rbp - 0xb4], r14d
 18269fa:      	mov	qword ptr [rbp - 0xb0], 0x0
 1826a05:      	mov	dword ptr [rbp - 0xa8], 0x0
 1826a0f:      	mov	word ptr [rbp - 0xa4], cx
 1826a16:      	mov	word ptr [rbp - 0xa2], 0x0
 1826a1f:      	mov	rax, qword ptr [rbp - 0xa8]
 1826a26:      	mov	qword ptr [rsp + 0x10], rax
 1826a2b:      	vmovups	xmm0, xmmword ptr [rbp - 0xb8]
 1826a33:      	vmovups	xmmword ptr [rsp], xmm0
 1826a38:      	call	0x181dc80 <machine_x64_select_row>
 1826a3d:      	mov	r12d, dword ptr [rbp - 0xf8]
 1826a44:      	movabs	rax, 0x3100000000
 1826a4e:      	or	r12d, 0xa0000000
 1826a55:      	mov	dword ptr [rbp - 0xd0], r12d
 1826a5c:      	mov	dword ptr [rbp - 0xcc], r15d
 1826a63:      	mov	qword ptr [rbp - 0xc8], 0x0
 1826a6e:      	mov	qword ptr [rbp - 0xc0], rax
 1826a75:      	mov	rax, qword ptr [rbp - 0xc0]
 1826a7c:      	mov	qword ptr [rsp + 0x10], rax
 1826a81:      	vmovups	xmm0, xmmword ptr [rbp - 0xd0]
 1826a89:      	jmp	0x18268a7 <machine_x64_select_load+0x567>
 1826a8e:      	xor	eax, eax
 1826a90:      	mov	rcx, qword ptr fs:[0x28]
 1826a99:      	cmp	rcx, qword ptr [rbp - 0x30]
 1826a9d:      	jne	0x1826b2c <machine_x64_select_load+0x7ec>
 1826aa3:      	add	rsp, 0xf8
 1826aaa:      	pop	rbx
 1826aab:      	pop	r12
 1826aad:      	pop	r13
 1826aaf:      	pop	r14
 1826ab1:      	pop	r15
 1826ab3:      	pop	rbp
 1826ab4:      	vzeroupper
 1826ab7:      	ret
 1826ab8:      	mov	r15, rdi
 1826abb:      	vzeroupper
 1826abe:      	call	0x181dfc0 <machine_x64_synthesize_register>
 1826ac3:      	mov	r14d, eax
 1826ac6:      	mov	rdi, r15
 1826ac9:      	mov	esi, ebx
 1826acb:      	mov	edx, eax
 1826acd:      	xor	ecx, ecx
 1826acf:      	call	0x18441f0 <machine_x64_select_place_address_offset>
 1826ad4:      	mov	ecx, eax
 1826ad6:      	xor	eax, eax
 1826ad8:      	test	cl, cl
 1826ada:      	je	0x1826a90 <machine_x64_select_load+0x750>
 1826adc:      	vmovd	xmm0, dword ptr [rbp - 0xf8]
 1826ae4:      	mov	rdi, r15
 1826ae7:      	vpinsrd	xmm0, xmm0, r14d, 0x1
 1826aed:      	vpshufd	xmm0, xmm0, 0x40        # xmm0 = xmm0[0,0,0,1]
 1826af2:      	vpor	xmm0, xmm0, xmmword ptr [rip - 0xa50d1a] # 0xdd5de0 <x86_64_metadata_tests.residual_blocked_expected+0x320>
 1826afa:      	vmovdqa	xmmword ptr [rbp - 0xa0], xmm0
 1826b02:      	mov	dword ptr [rbp - 0x90], r12d
 1826b09:      	mov	dword ptr [rbp - 0x8c], 0x83
 1826b13:      	mov	rax, qword ptr [rbp - 0x90]
 1826b1a:      	mov	qword ptr [rsp + 0x10], rax
 1826b1f:      	vmovaps	xmm0, xmmword ptr [rbp - 0xa0]
 1826b27:      	jmp	0x18268aa <machine_x64_select_load+0x56a>
 1826b2c:      	vzeroupper
 1826b2f:      	call	0xfe5430 <__stack_chk_fail$plt>
 1826b34:      	nop	word ptr cs:[rax + rax]
