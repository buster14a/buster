.intel_syntax noprefix
.LBB1_4:                                # =>This Inner Loop Header: Depth=1
	mov	r9, r8
	#APP
	#NO_APP
	movzx	r10d, word ptr [r9 + 156]
	movzx	r11d, word ptr [r9 + 44]
	kxnorb	k4, k0, k0
	lea	r15, [rax + 16]
	shl	r10d, 6
	shl	r11d, 6
	lea	r10, [r10 + rcx + 64]
	lea	r11, [r11 + rcx + 64]
	vmovq	xmm2, r10
	movzx	r10d, word ptr [r9 + 92]
	vmovq	xmm1, r11
	movzx	r11d, word ptr [r9 + 220]
	shl	r10d, 6
	shl	r11d, 6
	lea	r10, [r10 + rcx + 64]
	lea	r11, [r11 + rcx + 64]
	vmovq	xmm3, r10
	movzx	r10d, word ptr [r9 + 28]
	vmovq	xmm31, r11
	vpunpcklqdq	xmm2, xmm3, xmm2        # xmm2 = xmm3[0],xmm2[0]
	vpunpcklqdq	xmm1, xmm31, xmm1       # xmm1 = xmm31[0],xmm1[0]
	vpxord	xmm31, xmm31, xmm31
	vinserti128	ymm1, ymm2, xmm1, 1
	shl	r10d, 6
	lea	r10, [r10 + rcx + 64]
	vmovq	xmm4, r10
	movzx	r10d, word ptr [r9 + 204]
	shl	r10d, 6
	lea	r10, [r10 + rcx + 64]
	vmovq	xmm5, r10
	movzx	r10d, word ptr [r9 + 140]
	vpunpcklqdq	xmm4, xmm5, xmm4        # xmm4 = xmm5[0],xmm4[0]
	shl	r10d, 6
	lea	r10, [r10 + rcx + 64]
	vmovq	xmm6, r10
	movzx	r10d, word ptr [r9 + 76]
	shl	r10d, 6
	lea	r10, [r10 + rcx + 64]
	vmovq	xmm7, r10
	movzx	r10d, word ptr [r9 + 60]
	vpunpcklqdq	xmm6, xmm7, xmm6        # xmm6 = xmm7[0],xmm6[0]
	vinserti128	ymm2, ymm6, xmm4, 1
	vbroadcasti32x4	zmm4, xmmword ptr [rip + .LCPI1_31] # zmm4 = [8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40]
                                        # zmm4 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vinserti64x4	zmm1, zmm2, ymm1, 1
	vpgatherqd	ymm31 {k4}, dword ptr [1*zmm1 + 12]
	kxnorw	k4, k0, k0
	shl	r10d, 6
	lea	r10, [r10 + rcx + 64]
	vmovq	xmm8, r10
	movzx	r10d, word ptr [r9 + 236]
	shl	r10d, 6
	lea	r10, [r10 + rcx + 64]
	vmovq	xmm3, r10
	movzx	r10d, word ptr [r9 + 172]
	vpunpcklqdq	xmm2, xmm3, xmm8        # xmm2 = xmm3[0],xmm8[0]
	shl	r10d, 6
	lea	r10, [r10 + rcx + 64]
	vmovq	xmm5, r10
	movzx	r10d, word ptr [r9 + 108]
	shl	r10d, 6
	lea	r10, [r10 + rcx + 64]
	vmovq	xmm7, r10
	movzx	r10d, word ptr [r9 + 12]
	vpunpcklqdq	xmm1, xmm7, xmm5        # xmm1 = xmm7[0],xmm5[0]
	vbroadcasti32x4	zmm5, xmmword ptr [rip + .LCPI1_32] # zmm5 = [61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32]
                                        # zmm5 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vinserti128	ymm1, ymm1, xmm2, 1
	vpxor	xmm2, xmm2, xmm2
	vpgatherqd	xmm2 {k4}, dword ptr [1*ymm1 + 12]
	vmovdqa64	zmm1, zmmword ptr [rip + .LCPI1_1] # zmm1 = [16,0,1,2,3,4,5,6,7,u,u,u,u,u,u,u]
	shl	r10d, 6
	vmovd	xmm3, dword ptr [r10 + rcx + 76] # xmm3 = mem[0],zero,zero,zero
	movzx	r10d, word ptr [r9 + 124]
	shl	r10d, 6
	vpermt2d	zmm31, zmm1, zmm3
	vmovdqa64	zmm3, zmmword ptr [rip + .LCPI1_2] # zmm3 = [0,1,2,3,4,5,6,7,8,16,17,18,19,u,u,u]
	vpermt2d	zmm31, zmm3, zmm2
	vmovdqu64	zmm2, zmmword ptr [r8]
	vmovdqu64	zmm3, zmmword ptr [r8 + 128]
	vpermt2d	zmm2, zmm0, zmmword ptr [r8 + 64]
	vpermt2d	zmm3, zmm0, zmmword ptr [r8 + 192]
	add	r8, 256
	vpbroadcastd	zmm31 {k1}, dword ptr [r10 + rcx + 76]
	movzx	r10d, word ptr [r9 + 188]
	movzx	r9d, word ptr [r9 + 252]
	vpunpckhqdq	zmm1, zmm2, zmm3        # zmm1 = zmm2[1],zmm3[1],zmm2[3],zmm3[3],zmm2[5],zmm3[5],zmm2[7],zmm3[7]
	vpunpcklqdq	zmm2, zmm2, zmm3        # zmm2 = zmm2[0],zmm3[0],zmm2[2],zmm3[2],zmm2[4],zmm3[4],zmm2[6],zmm3[6]
	vpmultishiftqb	zmm4, zmm4, zmm2
	vpmultishiftqb	zmm5, zmm5, zmm2
	shl	r10d, 6
	shl	r9d, 6
	vpsrld	zmm3, zmm2, 26
	vpermd	zmm3, zmm3, zmmword ptr [rip + .LCPI1_3]
	vpsrld	zmm7, zmm2, 8
	vpshufb	zmm6, zmm10, zmm2
	vptestmd	k0, zmm1, zmm1
	vpternlogd	zmm5, zmm4, dword ptr [rip + .LCPI1_7]{1to16}, 216 # zmm5 = zmm5 ^ (m32bcst & (zmm5 ^ zmm4))
	vbroadcasti32x4	zmm4, xmmword ptr [rip + .LCPI1_33] # zmm4 = [0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38]
                                        # zmm4 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vpternlogd	zmm7, zmm9, zmm14, 108  # zmm7 = zmm9 ^ (zmm7 & zmm14)
	vpaddd	zmm7, zmm7, zmm12
	vpbroadcastd	zmm31 {k2}, dword ptr [r10 + rcx + 76]
	vpmultishiftqb	zmm4, zmm4, zmm2
	vpternlogd	zmm4, zmm5, dword ptr [rip + .LCPI1_8]{1to16}, 228 # zmm4 = zmm5 ^ (m32bcst & (zmm4 ^ zmm5))
	vpslld	zmm5, zmm2, 4
	vpternlogd	zmm2, zmm9, zmm13, 108  # zmm2 = zmm9 ^ (zmm2 & zmm13)
	vpaddd	zmm2, zmm2, zmm12
	vpsrad	zmm5, zmm5, 31
	vpbroadcastd	zmm31 {k3}, dword ptr [r9 + rcx + 76]
	vpternlogd	zmm2, zmm15, zmm16, 234 # zmm2 = (zmm2 & zmm15) | zmm16
	vpandd	zmm8, zmm5, zmm6
	vpslld	zmm6, zmm6, 16
	mov	r9d, r14d
	vpternlogd	zmm7, zmm2, zmm17, 236  # zmm7 = (zmm7 & zmm17) | zmm2
	vpternlogd	zmm8, zmm3, zmm22, 236  # zmm8 = (zmm8 & zmm22) | zmm3
	vpandd	zmm2, zmm4, zmm18
	vpsrld	zmm4, zmm4, 3
	vpternlogd	zmm2, zmm31, zmm7, 248  # zmm2 = zmm2 | (zmm31 & zmm7)
	vpternlogd	zmm4, zmm31, zmm20, 162 # zmm4 = zmm20 & (zmm4 | ~zmm31)
	vpternlogd	zmm8, zmm31, zmm23, 248 # zmm8 = zmm8 | (zmm31 & zmm23)
	vpminud	zmm2, zmm2, zmm19
	vpminud	zmm3, zmm4, zmm21
	vpternlogd	zmm8, zmm3, zmm2, 254   # zmm8 = zmm8 | zmm3 | zmm2
	vpsrad	zmm3, zmm1, 31
	vpxord	zmm2, zmm3, zmm1
	vpaddd	zmm3, zmm1, zmm12
	vplzcntd	zmm2, zmm2
	vpsrlvd	zmm2, zmm11, zmm2
	vpternlogd	zmm3, zmm1, zmm2, 20    # zmm3 = ~zmm2 & (zmm3 ^ zmm1)
	vpsrld	zmm1, zmm31, 29
	vpsrad	zmm2, zmm6, 31
	vprorvd	zmm1, zmm3, zmm1
	vpmovm2d	zmm3, k0
	vpternlogd	zmm3, zmm5, zmm2, 200   # zmm3 = zmm5 & (zmm3 | zmm2)
	vpandd	zmm5, zmm5, zmm25
	vpandd	zmm2, zmm3, zmm24
	vpandd	zmm3, zmm31, zmm27
	vpsrad	zmm1, zmm1, 31
	vpternlogd	zmm5, zmm2, zmm1, 216   # zmm5 = zmm5 ^ (zmm1 & (zmm5 ^ zmm2))
	vpaddd	zmm2, zmm4, zmm12
	vpord	zmm1, zmm5, zmm8
	vpsrad	zmm2, zmm2, 31
	vpternlogd	zmm1, zmm31, zmm26, 32  # zmm1 = zmm1 & zmm26 & ~zmm31
	vpaddd	zmm2, zmm2, zmm3
	vpopcntd	zmm1, zmm1
	vpaddd	zmm1, zmm2, zmm1
	vpermb	zmm1, zmm28, zmm1
	vpmullq	xmm2, xmm1, xmm29
	vpshufb	xmm3, xmm2, xmm30
	vpaddb	xmm2, xmm3, xmm2
	vpbroadcastd	zmm3, r14d
	vpsubb	xmm1, xmm2, xmm1
	vpextrb	r14d, xmm2, 15
	vpmovzxbd	zmm1, xmm1              # zmm1 = xmm1[0],zero,zero,zero,xmm1[1],zero,zero,zero,xmm1[2],zero,zero,zero,xmm1[3],zero,zero,zero,xmm1[4],zero,zero,zero,xmm1[5],zero,zero,zero,xmm1[6],zero,zero,zero,xmm1[7],zero,zero,zero,xmm1[8],zero,zero,zero,xmm1[9],zero,zero,zero,xmm1[10],zero,zero,zero,xmm1[11],zero,zero,zero,xmm1[12],zero,zero,zero,xmm1[13],zero,zero,zero,xmm1[14],zero,zero,zero,xmm1[15],zero,zero,zero
	add	r14d, r9d
	vpaddd	zmm1, zmm3, zmm1
	vmovdqu64	zmmword ptr [rdx + 4*rax], zmm1
	add	rax, 32
	cmp	rax, rsi
	mov	rax, r15
	jbe	.LBB1_4
