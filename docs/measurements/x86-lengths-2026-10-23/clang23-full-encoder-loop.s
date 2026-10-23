.intel_syntax noprefix
.LBB5_6:                                # =>This Inner Loop Header: Depth=1
	movzx	eax, word ptr [r8 - 240]
	movzx	r11d, word ptr [r8 - 224]
	movzx	r14d, word ptr [r8 - 208]
	movzx	r12d, word ptr [r8 - 192]
	vmovdqu64	zmm19, zmmword ptr [r8 - 252]
	vmovdqu64	zmm26, zmmword ptr [r8 - 188]
	vmovdqu64	zmm28, zmmword ptr [r8 - 124]
	vmovdqu64	zmm29, zmmword ptr [r8 - 60]
	vbroadcasti32x4	zmm0, xmmword ptr [rip + .LCPI5_39] # zmm0 = [8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40]
                                        # zmm0 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vbroadcasti32x4	zmm4, xmmword ptr [rip + .LCPI5_40] # zmm4 = [61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32]
                                        # zmm4 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vbroadcasti32x4	zmm5, xmmword ptr [rip + .LCPI5_41] # zmm5 = [0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38]
                                        # zmm5 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vbroadcasti32x4	zmm6, xmmword ptr [rip + .LCPI5_42] # zmm6 = [0,0,0,0,2,128,0,0,0,0,0,0,2,128,0,0,0,0,0,0,2,128,0,0,0,0,0,0,2,128,0,0,0,0,0,0,2,128,0,0,0,0,0,0,2,128,0,0,0,0,0,0,2,128,0,0,0,0,0,0,2,128,0,0]
                                        # zmm6 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vpbroadcastd	zmm7, dword ptr [rip + .LCPI5_11] # zmm7 = [33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431,33554431]
	shl	eax, 6
	shl	r11d, 6
	shl	r14d, 6
	shl	r12d, 6
	vmovdqa	ymm1, ymmword ptr [rax + r9 + 64]
	movzx	eax, word ptr [r8 - 176]
	vinserti64x4	zmm10, zmm1, ymmword ptr [r14 + r9 + 64], 1
	movzx	r14d, word ptr [r8 - 144]
	vmovdqa	ymm1, ymmword ptr [r11 + r9 + 96]
	movzx	r11d, word ptr [r8 - 160]
	vinserti64x4	zmm21, zmm1, ymmword ptr [r12 + r9 + 96], 1
	movzx	r12d, word ptr [r8 - 128]
	vpunpckldq	zmm14, zmm28, zmm29     # zmm14 = zmm28[0],zmm29[0],zmm28[1],zmm29[1],zmm28[4],zmm29[4],zmm28[5],zmm29[5],zmm28[8],zmm29[8],zmm28[9],zmm29[9],zmm28[12],zmm29[12],zmm28[13],zmm29[13]
	vpunpckhdq	zmm27, zmm19, zmm26     # zmm27 = zmm19[2],zmm26[2],zmm19[3],zmm26[3],zmm19[6],zmm26[6],zmm19[7],zmm26[7],zmm19[10],zmm26[10],zmm19[11],zmm26[11],zmm19[14],zmm26[14],zmm19[15],zmm26[15]
	vpunpckhdq	zmm28, zmm28, zmm29     # zmm28 = zmm28[2],zmm29[2],zmm28[3],zmm29[3],zmm28[6],zmm29[6],zmm28[7],zmm29[7],zmm28[10],zmm29[10],zmm28[11],zmm29[11],zmm28[14],zmm29[14],zmm28[15],zmm29[15]
	vpunpckldq	zmm13, zmm19, zmm26     # zmm13 = zmm19[0],zmm26[0],zmm19[1],zmm26[1],zmm19[4],zmm26[4],zmm19[5],zmm26[5],zmm19[8],zmm26[8],zmm19[9],zmm26[9],zmm19[12],zmm26[12],zmm19[13],zmm26[13]
	vpunpcklqdq	zmm8, zmm27, zmm28      # zmm8 = zmm27[0],zmm28[0],zmm27[2],zmm28[2],zmm27[4],zmm28[4],zmm27[6],zmm28[6]
	vpunpckhqdq	zmm29, zmm13, zmm14     # zmm29 = zmm13[1],zmm14[1],zmm13[3],zmm14[3],zmm13[5],zmm14[5],zmm13[7],zmm14[7]
	vpmultishiftqb	zmm4, zmm4, zmm8
	vpmultishiftqb	zmm5, zmm5, zmm8
	vptestmd	k0, zmm29, zmm29
	vmovdqa64	zmm26, zmm8
	shl	eax, 6
	vmovdqa	ymm1, ymmword ptr [rax + r9 + 64]
	movzx	eax, word ptr [r8 - 112]
	shl	r14d, 6
	shl	r11d, 6
	vinserti64x4	zmm15, zmm1, ymmword ptr [r14 + r9 + 64], 1
	movzx	r14d, word ptr [r8 - 80]
	vmovdqa	ymm1, ymmword ptr [r11 + r9 + 96]
	movzx	r11d, word ptr [r8 - 96]
	shl	r12d, 6
	vinserti64x4	zmm23, zmm1, ymmword ptr [r12 + r9 + 96], 1
	movzx	r12d, word ptr [r8 - 64]
	vpblendmq	zmm11 {k1}, zmm10, zmm21
	shl	eax, 6
	vmovdqa	ymm1, ymmword ptr [rax + r9 + 64]
	movzx	eax, word ptr [r8 - 48]
	shl	r14d, 6
	shl	r11d, 6
	vinserti64x4	zmm17, zmm1, ymmword ptr [r14 + r9 + 64], 1
	movzx	r14d, word ptr [r8 - 16]
	vmovdqa	ymm1, ymmword ptr [r11 + r9 + 96]
	movzx	r11d, word ptr [r8 - 32]
	shl	r12d, 6
	vinserti64x4	zmm24, zmm1, ymmword ptr [r12 + r9 + 96], 1
	movzx	r12d, word ptr [r8]
	add	r8, 256
	vpblendmq	zmm16 {k1}, zmm15, zmm23
	shl	eax, 6
	shl	r11d, 6
	shl	r14d, 6
	vmovdqa	ymm1, ymmword ptr [rax + r9 + 64]
	vinserti64x4	zmm20, zmm1, ymmword ptr [r14 + r9 + 64], 1
	shl	r12d, 6
	vmovdqa	ymm1, ymmword ptr [r11 + r9 + 96]
	vinserti64x4	zmm25, zmm1, ymmword ptr [r12 + r9 + 96], 1
	vpunpckhdq	zmm1, zmm11, zmm16      # zmm1 = zmm11[2],zmm16[2],zmm11[3],zmm16[3],zmm11[6],zmm16[6],zmm11[7],zmm16[7],zmm11[10],zmm16[10],zmm11[11],zmm16[11],zmm11[14],zmm16[14],zmm11[15],zmm16[15]
	vpblendmq	zmm18 {k1}, zmm17, zmm24
	vpblendmq	zmm22 {k1}, zmm20, zmm25
	vpunpckhdq	zmm2, zmm18, zmm22      # zmm2 = zmm18[2],zmm22[2],zmm18[3],zmm22[3],zmm18[6],zmm22[6],zmm18[7],zmm22[7],zmm18[10],zmm22[10],zmm18[11],zmm22[11],zmm18[14],zmm22[14],zmm18[15],zmm22[15]
	vpunpckhqdq	zmm19, zmm1, zmm2       # zmm19 = zmm1[1],zmm2[1],zmm1[3],zmm2[3],zmm1[5],zmm2[5],zmm1[7],zmm2[7]
	vpmultishiftqb	zmm1, zmm0, zmm8
	vpsrld	zmm2, zmm8, 26
	vpermd	zmm31, zmm2, zmmword ptr [rip + .LCPI5_1]
	vpermd	zmm30, zmm2, zmmword ptr [rip + .LCPI5_2]
	vpsrad	zmm0, zmm29, 31
	vpermd	zmm2, zmm2, zmmword ptr [rip + .LCPI5_3]
	vpternlogd	zmm4, zmm1, dword ptr [rip + .LCPI5_7]{1to16}, 216 # zmm4 = zmm4 ^ (m32bcst & (zmm4 ^ zmm1))
	vpslld	zmm1, zmm8, 4
	vpsrad	zmm1, zmm1, 31
	vpternlogd	zmm5, zmm4, dword ptr [rip + .LCPI5_8]{1to16}, 228 # zmm5 = zmm4 ^ (m32bcst & (zmm5 ^ zmm4))
	vpshufb	zmm4, zmm6, zmm8
	vpternlogd	zmm30, zmm5, zmm31, 248 # zmm30 = zmm30 | (zmm5 & zmm31)
	vpslld	zmm31, zmm4, 16
	vpandd	zmm4, zmm1, zmm4
	vpternlogd	zmm4, zmm2, dword ptr [rip + .LCPI5_25]{1to16}, 236 # zmm4 = (zmm4 & m32bcst) | zmm2
	vpsrad	zmm6, zmm31, 31
	vpmovm2d	zmm31, k0
	vpternlogd	zmm4, zmm19, dword ptr [rip + .LCPI5_26]{1to16}, 248 # zmm4 = zmm4 | (zmm19 & m32bcst)
	vpternlogd	zmm31, zmm1, zmm6, 200  # zmm31 = zmm1 & (zmm31 | zmm6)
	vpxord	zmm6, zmm0, zmm29
	vpbroadcastd	zmm0, dword ptr [rip + .LCPI5_9] # zmm0 = [4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4]
	vplzcntd	zmm6, zmm6
	vpsrlvd	zmm6, zmm7, zmm6
	vpaddd	zmm7, zmm29, zmm12
	vpternlogd	zmm7, zmm29, zmm6, 20   # zmm7 = ~zmm6 & (zmm7 ^ zmm29)
	vpsrld	zmm6, zmm8, 8
	vpternlogd	zmm26, zmm0, dword ptr [rip + .LCPI5_14]{1to16}, 108 # zmm26 = zmm0 ^ (zmm26 & m32bcst)
	vpternlogd	zmm6, zmm0, dword ptr [rip + .LCPI5_15]{1to16}, 108 # zmm6 = zmm0 ^ (zmm6 & m32bcst)
	vprold	zmm0, zmm29, 1
	vpaddd	zmm26, zmm26, zmm12
	vpternlogd	zmm26, zmm9, dword ptr [rip + .LCPI5_16]{1to16}, 236 # zmm26 = (zmm26 & m32bcst) | zmm9
	vpaddd	zmm6, zmm6, zmm12
	vpternlogd	zmm6, zmm26, dword ptr [rip + .LCPI5_18]{1to16}, 236 # zmm6 = (zmm6 & m32bcst) | zmm26
	vpandd	zmm26, zmm5, dword ptr [rip + .LCPI5_19]{1to16}
	vpsrld	zmm5, zmm5, 3
	vpternlogd	zmm5, zmm19, dword ptr [rip + .LCPI5_22]{1to16}, 162 # zmm5 = m32bcst & (zmm5 | ~zmm19)
	vpminud	zmm2, zmm5, dword ptr [rip + .LCPI5_23]{1to16}
	vpternlogd	zmm26, zmm19, zmm6, 248 # zmm26 = zmm26 | (zmm19 & zmm6)
	vpminud	zmm26, zmm26, dword ptr [rip + .LCPI5_20]{1to16}
	vpsrld	zmm6, zmm19, 29
	vpaddd	zmm5, zmm5, zmm12
	vprorvd	zmm7, zmm7, zmm6
	vprorvd	zmm6, zmm0, zmm6
	vmovdqa64	zmm0, zmmword ptr [rip + .LCPI5_0] # zmm0 = [2,3,8,9,6,7,12,13]
	vpsrad	zmm7, zmm7, 31
	vpternlogd	zmm6, zmm31, zmm29, 226 # zmm6 = zmm29 ^ (zmm31 & (zmm6 ^ zmm29))
	vpternlogd	zmm6, zmm7, zmm29, 226  # zmm6 = zmm29 ^ (zmm7 & (zmm6 ^ zmm29))
	vpternlogd	zmm4, zmm2, zmm26, 254  # zmm4 = zmm4 | zmm2 | zmm26
	vmovdqa64	zmm2, zmm30
	vmovdqa64	zmm26, zmm30
	vpternlogd	zmm2, zmm1, dword ptr [rip + .LCPI5_13]{1to16}, 248 # zmm2 = zmm2 | (zmm1 & m32bcst)
	vpternlogd	zmm26, zmm31, dword ptr [rip + .LCPI5_12]{1to16}, 248 # zmm26 = zmm26 | (zmm31 & m32bcst)
	vpandd	zmm1, zmm1, dword ptr [rip + .LCPI5_28]{1to16}
	vpermt2q	zmm10, zmm0, zmm21
	vpermt2q	zmm15, zmm0, zmm23
	vpermt2q	zmm17, zmm0, zmm24
	vpermt2q	zmm20, zmm0, zmm25
	vpsrld	zmm0, zmm30, 16
	vpsrld	zmm23, zmm30, 3
	vpunpckhqdq	zmm21, zmm27, zmm28     # zmm21 = zmm27[1],zmm28[1],zmm27[3],zmm28[3],zmm27[5],zmm28[5],zmm27[7],zmm28[7]
	vpternlogd	zmm23, zmm0, dword ptr [rip + .LCPI5_21]{1to16}, 236 # zmm23 = (zmm23 & m32bcst) | zmm0
	vpord	zmm8, zmm21, zmm8
	vbroadcasti32x4	zmm0, xmmword ptr [rip + .LCPI5_43] # zmm0 = [0,0,13,24,0,0,45,56,0,0,13,24,0,0,45,56,0,0,13,24,0,0,45,56,0,0,13,24,0,0,45,56,0,0,13,24,0,0,45,56,0,0,13,24,0,0,45,56,0,0,13,24,0,0,45,56,0,0,13,24,0,0,45,56]
                                        # zmm0 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vpternlogd	zmm2, zmm7, zmm26, 184  # zmm2 = zmm2 ^ (zmm7 & (zmm2 ^ zmm26))
	vpandd	zmm26, zmm31, dword ptr [rip + .LCPI5_27]{1to16}
	vpternlogd	zmm1, zmm7, zmm26, 184  # zmm1 = zmm1 ^ (zmm7 & (zmm1 ^ zmm26))
	vpunpcklqdq	zmm7, zmm13, zmm14      # zmm7 = zmm13[0],zmm14[0],zmm13[2],zmm14[2],zmm13[4],zmm14[4],zmm13[6],zmm14[6]
	vpsrad	zmm13, zmm5, 31
	#APP
	#NO_APP
	vpunpckhdq	zmm5, zmm7, zmm6        # zmm5 = zmm7[2],zmm6[2],zmm7[3],zmm6[3],zmm7[6],zmm6[6],zmm7[7],zmm6[7],zmm7[10],zmm6[10],zmm7[11],zmm6[11],zmm7[14],zmm6[14],zmm7[15],zmm6[15]
	vpord	zmm14, zmm1, zmm4
	vpslld	zmm1, zmm23, 8
	vpslld	zmm4, zmm8, 7
	vpternlogd	zmm1, zmm13, dword ptr [rip + .LCPI5_29]{1to16}, 8 # zmm1 = zmm13 & m32bcst & ~zmm1
	vpternlogd	zmm1, zmm4, dword ptr [rip + .LCPI5_30]{1to16}, 216 # zmm1 = zmm1 ^ (m32bcst & (zmm1 ^ zmm4))
	vpmultishiftqb	zmm4, zmm0, zmm21
	vpternlogd	zmm4, zmm23, dword ptr [rip + .LCPI5_31]{1to16}, 216 # zmm4 = zmm4 ^ (m32bcst & (zmm4 ^ zmm23))
	vpshufd	zmm0, zmm14, 85                 # zmm0 = zmm14[1,1,1,1,5,5,5,5,9,9,9,9,13,13,13,13]
	vpshufbitqmb	k2, zmm0, zmm15
	vpshufd	zmm0, zmm14, 170                # zmm0 = zmm14[2,2,2,2,6,6,6,6,10,10,10,10,14,14,14,14]
	kmovq	r11, k2
	popcnt	r11, r11
	vpternlogd	zmm4, zmm1, dword ptr [rip + .LCPI5_32]{1to16}, 216 # zmm4 = zmm4 ^ (m32bcst & (zmm4 ^ zmm1))
	vpunpckldq	zmm1, zmm7, zmm6        # zmm1 = zmm7[0],zmm6[0],zmm7[1],zmm6[1],zmm7[4],zmm6[4],zmm7[5],zmm6[5],zmm7[8],zmm6[8],zmm7[9],zmm6[9],zmm7[12],zmm6[12],zmm7[13],zmm6[13]
	vpunpckldq	zmm6, zmm2, zmm4        # zmm6 = zmm2[0],zmm4[0],zmm2[1],zmm4[1],zmm2[4],zmm4[4],zmm2[5],zmm4[5],zmm2[8],zmm4[8],zmm2[9],zmm4[9],zmm2[12],zmm4[12],zmm2[13],zmm4[13]
	vpunpckhdq	zmm2, zmm2, zmm4        # zmm2 = zmm2[2],zmm4[2],zmm2[3],zmm4[3],zmm2[6],zmm4[6],zmm2[7],zmm4[7],zmm2[10],zmm4[10],zmm2[11],zmm4[11],zmm2[14],zmm4[14],zmm2[15],zmm4[15]
	vpunpcklqdq	zmm4, zmm1, zmm6        # zmm4 = zmm1[0],zmm6[0],zmm1[2],zmm6[2],zmm1[4],zmm6[4],zmm1[6],zmm6[6]
	vpunpckhqdq	zmm1, zmm1, zmm6        # zmm1 = zmm1[1],zmm6[1],zmm1[3],zmm6[3],zmm1[5],zmm6[5],zmm1[7],zmm6[7]
	vpunpcklqdq	zmm6, zmm5, zmm2        # zmm6 = zmm5[0],zmm2[0],zmm5[2],zmm2[2],zmm5[4],zmm2[4],zmm5[6],zmm2[6]
	vpunpckhqdq	zmm2, zmm5, zmm2        # zmm2 = zmm5[1],zmm2[1],zmm5[3],zmm2[3],zmm5[5],zmm2[5],zmm5[7],zmm2[7]
	vpshufd	zmm5, zmm14, 0                  # zmm5 = zmm14[0,0,0,0,4,4,4,4,8,8,8,8,12,12,12,12]
	vpshufb	zmm4, zmm4, zmm10
	vpshufbitqmb	k3, zmm5, zmm10
	vpshufb	zmm1, zmm1, zmm15
	vpshufb	zmm2, zmm2, zmm20
	vpternlogq	zmm4, zmm11, zmm3, 120  # zmm4 = zmm4 ^ (zmm11 & zmm3)
	vpternlogq	zmm1, zmm16, zmm3, 120  # zmm1 = zmm1 ^ (zmm16 & zmm3)
	vpternlogq	zmm2, zmm22, zmm3, 120  # zmm2 = zmm2 ^ (zmm22 & zmm3)
	vpcompressb	zmm4 {k3} {z}, zmm4
	vpcompressb	zmm1 {k2} {z}, zmm1
	kmovq	rax, k3
	vpshufbitqmb	k3, zmm0, zmm17
	vpshufd	zmm0, zmm14, 255                # zmm0 = zmm14[3,3,3,3,7,7,7,7,11,11,11,11,15,15,15,15]
	vpternlogd	zmm14, zmm19, dword ptr [rip + .LCPI5_34]{1to16}, 32 # zmm14 = zmm14 & m32bcst & ~zmm19
	popcnt	rax, rax
	vpshufbitqmb	k2, zmm0, zmm20
	vpandd	zmm0, zmm19, dword ptr [rip + .LCPI5_35]{1to16}
	kmovq	r14, k3
	popcnt	r14, r14
	vmovdqu64	zmmword ptr [r10], zmm4
	vpshufb	zmm4, zmm6, zmm17
	vmovdqu64	zmmword ptr [r10 + rax], zmm1
	add	rax, r10
	sub	r10d, ebx
	vpternlogq	zmm4, zmm18, zmm3, 120  # zmm4 = zmm4 ^ (zmm18 & zmm3)
	vpxor	xmm1, xmm1, xmm1
	vpcompressb	zmm1 {k3} {z}, zmm4
	vmovdqu64	zmmword ptr [r11 + rax], zmm1
	vpxor	xmm1, xmm1, xmm1
	vpcompressb	zmm1 {k2} {z}, zmm2
	add	r11, rax
	vpopcntd	zmm2, zmm14
	kmovq	rax, k2
	popcnt	rax, rax
	add	rax, r14
	add	rax, r11
	vmovdqu64	zmmword ptr [r14 + r11], zmm1
	vpaddd	zmm1, zmm13, zmm0
	vmovdqa	xmm0, xmmword ptr [rip + .LCPI5_36] # xmm0 = [0,16,32,48,4,20,36,52,8,24,40,56,12,28,44,60]
	mov	dword ptr [rsp + 64], r10d
	mov	r10, rax
	vpaddd	zmm1, zmm1, zmm2
	#APP
	#NO_APP
	vpermb	zmm1, zmm0, zmm1
	vpmullq	xmm2, xmm1, qword ptr [rip + .LCPI5_37]{1to2}
	vpsubb	xmm1, xmm2, xmm1
	vpshufb	xmm2, xmm2, xmmword ptr [rip + .LCPI5_38] # xmm2 = zero,zero,zero,zero,zero,zero,zero,zero,xmm2[7,7,7,7,7,7,7,7]
	vpaddb	xmm1, xmm1, xmm2
	vpmovzxbd	zmm1, xmm1              # zmm1 = xmm1[0],zero,zero,zero,xmm1[1],zero,zero,zero,xmm1[2],zero,zero,zero,xmm1[3],zero,zero,zero,xmm1[4],zero,zero,zero,xmm1[5],zero,zero,zero,xmm1[6],zero,zero,zero,xmm1[7],zero,zero,zero,xmm1[8],zero,zero,zero,xmm1[9],zero,zero,zero,xmm1[10],zero,zero,zero,xmm1[11],zero,zero,zero,xmm1[12],zero,zero,zero,xmm1[13],zero,zero,zero,xmm1[14],zero,zero,zero,xmm1[15],zero,zero,zero
	vpaddd	zmm1, zmm1, dword ptr [rsp + 64]{1to16}
	vmovdqu64	zmmword ptr [rcx + 4*r15], zmm1
	add	r15, 16
	dec	rdx
	jne	.LBB5_6
