probe_measure:                          # @probe_measure
	.cfi_startproc
# %bb.0:
	mov	rax, -3
	cmp	rsi, 286331153
	ja	.LBB1_8
# %bb.1:
	push	rbp
	.cfi_def_cfa_offset 16
	.cfi_offset rbp, -16
	mov	rbp, rsp
	.cfi_def_cfa_register rbp
	push	r15
	push	r14
	push	r13
	push	r12
	push	rbx
	and	rsp, -64
	sub	rsp, 320
	.cfi_offset rbx, -56
	.cfi_offset r12, -48
	.cfi_offset r13, -40
	.cfi_offset r14, -32
	.cfi_offset r15, -24
	cmp	rsi, 16
	jae	.LBB1_3
# %bb.2:
	xor	r14d, r14d
	xor	r15d, r15d
	jmp	.LBB1_5
.LBB1_3:
	vpbroadcastq	zmm4, qword ptr [rip + .LCPI1_24] # zmm4 = [61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32]
	vpbroadcastq	zmm5, qword ptr [rip + .LCPI1_25] # zmm5 = [0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38]
	vpbroadcastd	zmm6, dword ptr [rip + .LCPI1_3] # zmm6 = [117507847,117507847,117507847,117507847,117507847,117507847,117507847,117507847,117507847,117507847,117507847,117507847,117507847,117507847,117507847,117507847]
	vpbroadcastd	zmm7, dword ptr [rip + .LCPI1_4] # zmm7 = [1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120,1074053120]
	vpbroadcastd	zmm8, dword ptr [rip + .LCPI1_5] # zmm8 = [4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4]
	vpbroadcastd	zmm9, dword ptr [rip + .LCPI1_6] # zmm9 = [33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431]
	vpbroadcastd	zmm11, dword ptr [rip + .LCPI1_7] # zmm11 = [252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252]
	vpbroadcastd	zmm12, dword ptr [rip + .LCPI1_8] # zmm12 = [262396,262396,262396,262396,262396,262396,262396,262396,262396,262396,262396,262396,262396,262396,262396,262396]
	vpbroadcastd	zmm13, dword ptr [rip + .LCPI1_9] # zmm13 = [16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216]
	vpbroadcastd	zmm14, dword ptr [rip + .LCPI1_10] # zmm14 = [983040,983040,983040,983040,983040,983040,983040,983040,983040,983040,983040,983040,983040,983040,983040,983040]
	vpbroadcastd	zmm15, dword ptr [rip + .LCPI1_11] # zmm15 = [33554432,33554432,33554432,33554432,33554432,33554432,33554432,33554432,33554432,33554432,33554432,33554432,33554432,33554432,33554432,33554432]
	vpbroadcastd	zmm16, dword ptr [rip + .LCPI1_12] # zmm16 = [458752,458752,458752,458752,458752,458752,458752,458752,458752,458752,458752,458752,458752,458752,458752,458752]
	vpbroadcastd	zmm17, dword ptr [rip + .LCPI1_13] # zmm17 = [1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024]
	vpbroadcastd	zmm18, dword ptr [rip + .LCPI1_14] # zmm18 = [24576,24576,24576,24576,24576,24576,24576,24576,24576,24576,24576,24576,24576,24576,24576,24576]
	vpbroadcastd	zmm19, dword ptr [rip + .LCPI1_15] # zmm19 = [8192,8192,8192,8192,8192,8192,8192,8192,8192,8192,8192,8192,8192,8192,8192,8192]
	vpbroadcastd	zmm20, dword ptr [rip + .LCPI1_16] # zmm20 = [512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512]
	vpbroadcastd	zmm21, dword ptr [rip + .LCPI1_17] # zmm21 = [56319,56319,56319,56319,56319,56319,56319,56319,56319,56319,56319,56319,56319,56319,56319,56319]
	vpbroadcastd	zmm22, dword ptr [rip + .LCPI1_18] # zmm22 = [16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16]
	vpbroadcastd	zmm23, dword ptr [rip + .LCPI1_19] # zmm23 = [240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240]
	vpbroadcastd	zmm24, dword ptr [rip + .LCPI1_20] # zmm24 = [1776,1776,1776,1776,1776,1776,1776,1776,1776,1776,1776,1776,1776,1776,1776,1776]
	vpbroadcastd	zmm25, dword ptr [rip + .LCPI1_21] # zmm25 = [15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15]
	vpbroadcastq	xmm26, qword ptr [rip + .LCPI1_22] # xmm26 = [72340172838076673,72340172838076673]
	vmovdqa64	xmm27, xmmword ptr [rip + .LCPI1_23] # xmm27 = [128,128,128,128,128,128,128,128,7,7,7,7,7,7,7,7]
	lea	rcx, [rip + x86_template_storage]
	vpternlogd	zmm10, zmm10, zmm10, 255
	xor	eax, eax
	mov	r8, rdi
	xor	r14d, r14d
	.p2align	4, 0x90
.LBB1_4:                                # =>This Inner Loop Header: Depth=1
	vmovdqu64	zmm31, zmmword ptr [r8]
	vmovdqu64	zmm1, zmmword ptr [r8 + 64]
	mov	r9, r8
	#APP
	#NO_APP
	movzx	r10d, word ptr [r9 + 60]
	vmovdqu64	zmm29, zmmword ptr [r8 + 128]
	vmovdqu64	zmm30, zmmword ptr [r8 + 192]
	lea	r15, [rax + 16]
	add	r8, 256
	shl	r10d, 6
	vpunpckldq	zmm28, zmm31, zmm1      # zmm28 = zmm31[0],zmm1[0],zmm31[1],zmm1[1],zmm31[4],zmm1[4],zmm31[5],zmm1[5],zmm31[8],zmm1[8],zmm31[9],zmm1[9],zmm31[12],zmm1[12],zmm31[13],zmm1[13]
	vpunpckhdq	zmm31, zmm31, zmm1      # zmm31 = zmm31[2],zmm1[2],zmm31[3],zmm1[3],zmm31[6],zmm1[6],zmm31[7],zmm1[7],zmm31[10],zmm1[10],zmm31[11],zmm1[11],zmm31[14],zmm1[14],zmm31[15],zmm1[15]
	vmovd	xmm1, dword ptr [r10 + rcx + 76] # xmm1 = mem[0],zero,zero,zero
	movzx	r10d, word ptr [r9 + 44]
	vpunpckldq	zmm0, zmm29, zmm30      # zmm0 = zmm29[0],zmm30[0],zmm29[1],zmm30[1],zmm29[4],zmm30[4],zmm29[5],zmm30[5],zmm29[8],zmm30[8],zmm29[9],zmm30[9],zmm29[12],zmm30[12],zmm29[13],zmm30[13]
	vpunpckhdq	zmm29, zmm29, zmm30     # zmm29 = zmm29[2],zmm30[2],zmm29[3],zmm30[3],zmm29[6],zmm30[6],zmm29[7],zmm30[7],zmm29[10],zmm30[10],zmm29[11],zmm30[11],zmm29[14],zmm30[14],zmm29[15],zmm30[15]
	vpunpckhqdq	zmm28, zmm28, zmm0      # zmm28 = zmm28[1],zmm0[1],zmm28[3],zmm0[3],zmm28[5],zmm0[5],zmm28[7],zmm0[7]
	vpunpcklqdq	zmm30, zmm31, zmm29     # zmm30 = zmm31[0],zmm29[0],zmm31[2],zmm29[2],zmm31[4],zmm29[4],zmm31[6],zmm29[6]
	vpbroadcastq	zmm29, qword ptr [rip + .LCPI1_26] # zmm29 = [8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40]
	vpmultishiftqb	zmm31, zmm4, zmm30
	vptestmd	k0, zmm28, zmm28
	shl	r10d, 6
	vmovd	xmm0, dword ptr [r10 + rcx + 76] # xmm0 = mem[0],zero,zero,zero
	movzx	r10d, word ptr [r9 + 124]
	vpmultishiftqb	zmm29, zmm29, zmm30
	vpternlogd	zmm31, zmm29, zmm6, 216
	vpslld	zmm29, zmm30, 4
	shl	r10d, 6
	vpsrad	zmm29, zmm29, 31
	vpinsrd	xmm1, xmm1, dword ptr [r10 + rcx + 76], 1
	movzx	r10d, word ptr [r9 + 108]
	shl	r10d, 6
	vpinsrd	xmm0, xmm0, dword ptr [r10 + rcx + 76], 1
	movzx	r10d, word ptr [r9 + 188]
	shl	r10d, 6
	vpinsrd	xmm1, xmm1, dword ptr [r10 + rcx + 76], 2
	movzx	r10d, word ptr [r9 + 28]
	shl	r10d, 6
	vmovd	xmm2, dword ptr [r10 + rcx + 76] # xmm2 = mem[0],zero,zero,zero
	movzx	r10d, word ptr [r9 + 172]
	shl	r10d, 6
	vpinsrd	xmm0, xmm0, dword ptr [r10 + rcx + 76], 2
	movzx	r10d, word ptr [r9 + 92]
	shl	r10d, 6
	vpinsrd	xmm2, xmm2, dword ptr [r10 + rcx + 76], 1
	movzx	r10d, word ptr [r9 + 156]
	shl	r10d, 6
	vpinsrd	xmm2, xmm2, dword ptr [r10 + rcx + 76], 2
	movzx	r10d, word ptr [r9 + 12]
	shl	r10d, 6
	vmovd	xmm3, dword ptr [r10 + rcx + 76] # xmm3 = mem[0],zero,zero,zero
	movzx	r10d, word ptr [r9 + 252]
	shl	r10d, 6
	vpinsrd	xmm1, xmm1, dword ptr [r10 + rcx + 76], 3
	movzx	r10d, word ptr [r9 + 76]
	shl	r10d, 6
	vpinsrd	xmm3, xmm3, dword ptr [r10 + rcx + 76], 1
	movzx	r10d, word ptr [r9 + 236]
	shl	r10d, 6
	vpinsrd	xmm0, xmm0, dword ptr [r10 + rcx + 76], 3
	movzx	r10d, word ptr [r9 + 140]
	shl	r10d, 6
	vpinsrd	xmm3, xmm3, dword ptr [r10 + rcx + 76], 2
	movzx	r10d, word ptr [r9 + 220]
	movzx	r9d, word ptr [r9 + 204]
	vinserti128	ymm0, ymm0, xmm1, 1
	vpsrld	zmm1, zmm30, 26
	vpermd	zmm1, zmm1, zmmword ptr [rip + x86_mode_presence]
	shl	r10d, 6
	shl	r9d, 6
	vpinsrd	xmm2, xmm2, dword ptr [r10 + rcx + 76], 3
	vpinsrd	xmm3, xmm3, dword ptr [r9 + rcx + 76], 3
	vinserti128	ymm2, ymm3, xmm2, 1
	vpmultishiftqb	zmm3, zmm5, zmm30
	vpternlogd	zmm3, zmm31, zmm7, 228
	vmovdqa64	zmm31, zmmword ptr [rip + x86_base_kinds]
	vinserti64x4	zmm0, zmm2, ymm0, 1
	vpsrld	zmm2, zmm30, 8
	vpternlogd	zmm2, zmm8, zmm12, 108
	vpaddd	zmm2, zmm2, zmm10
	vpshufb	zmm31, zmm31, zmm30
	vpternlogd	zmm30, zmm8, zmm11, 108
	vpaddd	zmm30, zmm30, zmm10
	vpternlogd	zmm30, zmm13, zmm14, 234
	vpternlogd	zmm2, zmm30, zmm15, 236
	vpandd	zmm30, zmm3, zmm16
	vpternlogd	zmm30, zmm0, zmm2, 248
	vpsrld	zmm2, zmm3, 3
	vpminud	zmm3, zmm30, zmm17
	vpandd	zmm30, zmm29, zmm31
	vpternlogd	zmm2, zmm0, zmm18, 162
	vpternlogd	zmm30, zmm1, zmm20, 236
	vpminud	zmm1, zmm2, zmm19
	vpaddd	zmm2, zmm2, zmm10
	vpternlogd	zmm30, zmm0, zmm21, 248
	vpsrad	zmm2, zmm2, 31
	vpternlogd	zmm30, zmm1, zmm3, 254
	vpsrad	zmm3, zmm28, 31
	vpxord	zmm1, zmm3, zmm28
	vpaddd	zmm3, zmm28, zmm10
	vplzcntd	zmm1, zmm1
	vpsrlvd	zmm1, zmm9, zmm1
	vpternlogd	zmm3, zmm28, zmm1, 20
	vpslld	zmm1, zmm31, 16
	vpsrld	zmm31, zmm0, 29
	vpmovm2d	zmm28, k0
	vpsrad	zmm1, zmm1, 31
	vpternlogd	zmm28, zmm29, zmm1, 200
	vprorvd	zmm1, zmm3, zmm31
	vpandd	zmm3, zmm28, zmm22
	vpandd	zmm28, zmm29, zmm23
	vpsrad	zmm1, zmm1, 31
	vpternlogd	zmm28, zmm3, zmm1, 216
	vpord	zmm1, zmm28, zmm30
	vpternlogd	zmm1, zmm0, zmm24, 32
	vpandd	zmm0, zmm0, zmm25
	vpaddd	zmm0, zmm2, zmm0
	vpopcntd	zmm1, zmm1
	vpaddd	zmm0, zmm0, zmm1
	vmovdqa64	zmm1, zmmword ptr [rip + x86_length_bytes]
	vpermb	zmm0, zmm1, zmm0
	vpmullq	xmm1, xmm0, xmm26
	vpshufb	xmm2, xmm1, xmm27
	vpaddb	xmm1, xmm2, xmm1
	vpbroadcastd	zmm2, r14d
	vpsubb	xmm0, xmm1, xmm0
	vpextrb	r9d, xmm1, 15
	vpmovzxbd	zmm0, xmm0              # zmm0 = xmm0[0],zero,zero,zero,xmm0[1],zero,zero,zero,xmm0[2],zero,zero,zero,xmm0[3],zero,zero,zero,xmm0[4],zero,zero,zero,xmm0[5],zero,zero,zero,xmm0[6],zero,zero,zero,xmm0[7],zero,zero,zero,xmm0[8],zero,zero,zero,xmm0[9],zero,zero,zero,xmm0[10],zero,zero,zero,xmm0[11],zero,zero,zero,xmm0[12],zero,zero,zero,xmm0[13],zero,zero,zero,xmm0[14],zero,zero,zero,xmm0[15],zero,zero,zero
	add	r14d, r9d
	vpaddd	zmm0, zmm2, zmm0
	vmovdqu64	zmmword ptr [rdx + 4*rax], zmm0
	add	rax, 32
	cmp	rax, rsi
	mov	rax, r15
	jbe	.LBB1_4
.LBB1_5:
	sub	rsi, r15
	jbe	.LBB1_7
# %bb.6:
	mov	rax, r15
	shl	rax, 4
	vpxor	xmm0, xmm0, xmm0
	mov	rbx, rsp
	mov	r13, rsi
	mov	r12, rdx
	add	rdi, rax
	mov	rax, rsi
	shl	rax, 4
	vmovdqa64	zmmword ptr [rsp + 192], zmm0
	vmovdqa64	zmmword ptr [rsp + 128], zmm0
	vmovdqa64	zmmword ptr [rsp + 64], zmm0
	vmovdqa64	zmmword ptr [rsp], zmm0
	mov	rcx, rdi
	mov	rdi, rbx
	mov	rsi, rcx
	mov	rdx, rax
	vzeroupper
	call	memcpy@PLT
	#APP
	#NO_APP
	movzx	ecx, word ptr [rbx + 60]
	lea	rax, [rip + x86_template_storage]
	vmovdqa64	zmm4, zmmword ptr [rsp]
	vmovdqa64	zmm5, zmmword ptr [rsp + 64]
	vmovdqa64	zmm6, zmmword ptr [rsp + 128]
	vmovdqa64	zmm7, zmmword ptr [rsp + 192]
	shl	ecx, 6
	vmovd	xmm0, dword ptr [rcx + rax + 76] # xmm0 = mem[0],zero,zero,zero
	movzx	ecx, word ptr [rbx + 124]
	vpunpckldq	zmm8, zmm4, zmm5        # zmm8 = zmm4[0],zmm5[0],zmm4[1],zmm5[1],zmm4[4],zmm5[4],zmm4[5],zmm5[5],zmm4[8],zmm5[8],zmm4[9],zmm5[9],zmm4[12],zmm5[12],zmm4[13],zmm5[13]
	vpunpckhdq	zmm4, zmm4, zmm5        # zmm4 = zmm4[2],zmm5[2],zmm4[3],zmm5[3],zmm4[6],zmm5[6],zmm4[7],zmm5[7],zmm4[10],zmm5[10],zmm4[11],zmm5[11],zmm4[14],zmm5[14],zmm4[15],zmm5[15]
	vpunpckhdq	zmm5, zmm6, zmm7        # zmm5 = zmm6[2],zmm7[2],zmm6[3],zmm7[3],zmm6[6],zmm7[6],zmm6[7],zmm7[7],zmm6[10],zmm7[10],zmm6[11],zmm7[11],zmm6[14],zmm7[14],zmm6[15],zmm7[15]
	vpunpckldq	zmm9, zmm6, zmm7        # zmm9 = zmm6[0],zmm7[0],zmm6[1],zmm7[1],zmm6[4],zmm7[4],zmm6[5],zmm7[5],zmm6[8],zmm7[8],zmm6[9],zmm7[9],zmm6[12],zmm7[12],zmm6[13],zmm7[13]
	vpunpcklqdq	zmm4, zmm4, zmm5        # zmm4 = zmm4[0],zmm5[0],zmm4[2],zmm5[2],zmm4[4],zmm5[4],zmm4[6],zmm5[6]
	vpbroadcastq	zmm5, qword ptr [rip + .LCPI1_25] # zmm5 = [0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38]
	vpunpckhqdq	zmm8, zmm8, zmm9        # zmm8 = zmm8[1],zmm9[1],zmm8[3],zmm9[3],zmm8[5],zmm9[5],zmm8[7],zmm9[7]
	vpslld	zmm6, zmm4, 4
	vptestmd	k0, zmm8, zmm8
	vpsrad	zmm10, zmm8, 31
	vpsrld	zmm12, zmm4, 8
	shl	ecx, 6
	vpmovm2d	zmm9, k0
	vpinsrd	xmm0, xmm0, dword ptr [rcx + rax + 76], 1
	movzx	ecx, word ptr [rbx + 188]
	shl	ecx, 6
	vpinsrd	xmm0, xmm0, dword ptr [rcx + rax + 76], 2
	movzx	ecx, word ptr [rbx + 252]
	shl	ecx, 6
	vpinsrd	xmm0, xmm0, dword ptr [rcx + rax + 76], 3
	movzx	ecx, word ptr [rbx + 44]
	shl	ecx, 6
	vmovd	xmm1, dword ptr [rcx + rax + 76] # xmm1 = mem[0],zero,zero,zero
	movzx	ecx, word ptr [rbx + 108]
	shl	ecx, 6
	vpinsrd	xmm1, xmm1, dword ptr [rcx + rax + 76], 1
	movzx	ecx, word ptr [rbx + 172]
	shl	ecx, 6
	vpinsrd	xmm1, xmm1, dword ptr [rcx + rax + 76], 2
	movzx	ecx, word ptr [rbx + 236]
	shl	ecx, 6
	vpinsrd	xmm1, xmm1, dword ptr [rcx + rax + 76], 3
	movzx	ecx, word ptr [rbx + 28]
	shl	ecx, 6
	vmovd	xmm2, dword ptr [rcx + rax + 76] # xmm2 = mem[0],zero,zero,zero
	movzx	ecx, word ptr [rbx + 92]
	vinserti128	ymm0, ymm1, xmm0, 1
	shl	ecx, 6
	vpinsrd	xmm2, xmm2, dword ptr [rcx + rax + 76], 1
	movzx	ecx, word ptr [rbx + 156]
	shl	ecx, 6
	vpinsrd	xmm2, xmm2, dword ptr [rcx + rax + 76], 2
	movzx	ecx, word ptr [rbx + 220]
	shl	ecx, 6
	vpinsrd	xmm2, xmm2, dword ptr [rcx + rax + 76], 3
	movzx	ecx, word ptr [rbx + 12]
	shl	ecx, 6
	vmovd	xmm3, dword ptr [rcx + rax + 76] # xmm3 = mem[0],zero,zero,zero
	movzx	ecx, word ptr [rbx + 76]
	shl	ecx, 6
	vpinsrd	xmm3, xmm3, dword ptr [rcx + rax + 76], 1
	movzx	ecx, word ptr [rbx + 140]
	shl	ecx, 6
	vpinsrd	xmm3, xmm3, dword ptr [rcx + rax + 76], 2
	movzx	ecx, word ptr [rbx + 204]
	shl	ecx, 6
	vpinsrd	xmm3, xmm3, dword ptr [rcx + rax + 76], 3
	mov	eax, -1
	shlx	eax, eax, r13d
	not	eax
	kmovd	k1, eax
	vinserti128	ymm1, ymm3, xmm2, 1
	vpbroadcastq	zmm3, qword ptr [rip + .LCPI1_26] # zmm3 = [8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40]
	vpsrld	zmm2, zmm4, 26
	vpermd	zmm2, zmm2, zmmword ptr [rip + x86_mode_presence]
	vinserti64x4	zmm0, zmm1, ymm0, 1
	vpmultishiftqb	zmm1, zmm3, zmm4
	vpbroadcastq	zmm3, qword ptr [rip + .LCPI1_24] # zmm3 = [61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32]
	vpmultishiftqb	zmm3, zmm3, zmm4
	vpternlogd	zmm3, zmm1, dword ptr [rip + .LCPI1_3]{1to16}, 216
	vpmultishiftqb	zmm1, zmm5, zmm4
	vpbroadcastd	zmm5, dword ptr [rip + .LCPI1_5] # zmm5 = [4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4]
	vpternlogd	zmm1, zmm3, dword ptr [rip + .LCPI1_4]{1to16}, 228
	vpsrad	zmm3, zmm6, 31
	vmovdqa64	zmm6, zmmword ptr [rip + x86_base_kinds]
	vpternlogd	zmm12, zmm5, dword ptr [rip + .LCPI1_8]{1to16}, 108
	vpshufb	zmm6, zmm6, zmm4
	vpternlogd	zmm4, zmm5, dword ptr [rip + .LCPI1_7]{1to16}, 108
	vpslld	zmm7, zmm6, 16
	vpandd	zmm6, zmm3, zmm6
	vpternlogd	zmm6, zmm2, dword ptr [rip + .LCPI1_16]{1to16}, 236
	vpandd	zmm2, zmm3, dword ptr [rip + .LCPI1_19]{1to16}
	vpsrad	zmm7, zmm7, 31
	vpternlogd	zmm6, zmm0, dword ptr [rip + .LCPI1_17]{1to16}, 248
	vpternlogd	zmm9, zmm3, zmm7, 200
	vpxord	zmm7, zmm10, zmm8
	vpbroadcastd	zmm10, dword ptr [rip + .LCPI1_6] # zmm10 = [33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431]
	vplzcntd	zmm7, zmm7
	vpsrlvd	zmm7, zmm10, zmm7
	vpternlogd	zmm10, zmm10, zmm10, 255
	vpaddd	zmm11, zmm8, zmm10
	vpaddd	zmm4, zmm4, zmm10
	vpaddd	zmm5, zmm12, zmm10
	vpternlogd	zmm11, zmm8, zmm7, 20
	vpsrld	zmm7, zmm0, 29
	vprorvd	zmm7, zmm11, zmm7
	vpbroadcastd	zmm11, dword ptr [rip + .LCPI1_9] # zmm11 = [16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216,16777216]
	vpternlogd	zmm11, zmm4, dword ptr [rip + .LCPI1_10]{1to16}, 234
	vpandd	zmm4, zmm1, dword ptr [rip + .LCPI1_12]{1to16}
	vpsrld	zmm1, zmm1, 3
	vpternlogd	zmm1, zmm0, dword ptr [rip + .LCPI1_14]{1to16}, 162
	vpternlogd	zmm11, zmm5, dword ptr [rip + .LCPI1_11]{1to16}, 248
	vpsrad	zmm7, zmm7, 31
	vpaddd	zmm5, zmm1, zmm10
	vpminud	zmm1, zmm1, dword ptr [rip + .LCPI1_15]{1to16}
	vpternlogd	zmm4, zmm0, zmm11, 248
	vpminud	zmm4, zmm4, dword ptr [rip + .LCPI1_13]{1to16}
	vpsrad	zmm5, zmm5, 31
	vpternlogd	zmm6, zmm1, zmm4, 254
	vpandd	zmm4, zmm9, dword ptr [rip + .LCPI1_18]{1to16}
	vpternlogd	zmm2, zmm7, zmm4, 184
	vpord	zmm1, zmm2, zmm6
	vpternlogd	zmm1, zmm0, dword ptr [rip + .LCPI1_20]{1to16}, 32
	vpandd	zmm0, zmm0, dword ptr [rip + .LCPI1_21]{1to16}
	vpopcntd	zmm1, zmm1
	vpaddd	zmm0, zmm5, zmm0
	vpaddd	zmm0, zmm0, zmm1
	vmovdqa64	zmm1, zmmword ptr [rip + x86_length_bytes]
	vpermb	zmm0, zmm1, zmm0
	vpmullq	xmm1, xmm0, qword ptr [rip + .LCPI1_22]{1to2}
	vpshufb	xmm2, xmm1, xmmword ptr [rip + .LCPI1_23] # xmm2 = zero,zero,zero,zero,zero,zero,zero,zero,xmm1[7,7,7,7,7,7,7,7]
	vpaddb	xmm1, xmm2, xmm1
	vpbroadcastd	zmm2, r14d
	vpsubb	xmm0, xmm1, xmm0
	vpextrb	eax, xmm1, 15
	vpmovzxbd	zmm0, xmm0              # zmm0 = xmm0[0],zero,zero,zero,xmm0[1],zero,zero,zero,xmm0[2],zero,zero,zero,xmm0[3],zero,zero,zero,xmm0[4],zero,zero,zero,xmm0[5],zero,zero,zero,xmm0[6],zero,zero,zero,xmm0[7],zero,zero,zero,xmm0[8],zero,zero,zero,xmm0[9],zero,zero,zero,xmm0[10],zero,zero,zero,xmm0[11],zero,zero,zero,xmm0[12],zero,zero,zero,xmm0[13],zero,zero,zero,xmm0[14],zero,zero,zero,xmm0[15],zero,zero,zero
	add	eax, r14d
	mov	r14d, eax
	vpaddd	zmm0, zmm2, zmm0
	vmovdqu32	zmmword ptr [r12 + 4*r15] {k1}, zmm0
.LBB1_7:
	mov	eax, r14d
	lea	rsp, [rbp - 40]
	pop	rbx
	pop	r12
	pop	r13
	pop	r14
	pop	r15
	pop	rbp
	.cfi_def_cfa rsp, 8
	.cfi_restore rbx
	.cfi_restore r12
	.cfi_restore r13
	.cfi_restore r14
	.cfi_restore r15
	.cfi_restore rbp
.LBB1_8:
	vzeroupper
	ret
.Lfunc_end1:
	.size	probe_measure, .Lfunc_end1-probe_measure
