.intel_syntax noprefix
.LBB1_4:                                # =>This Inner Loop Header: Depth=1
	vmovdqu64	zmm29, zmmword ptr [rax - 192]
	vmovdqu64	zmm31, zmmword ptr [rax - 128]
	vmovdqu64	zmm1, zmmword ptr [rax - 64]
	vmovdqu64	zmm2, zmmword ptr [rax]
	vbroadcasti32x4	zmm5, xmmword ptr [rip + .LCPI1_29] # zmm5 = [8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40,8,8,11,8,40,40,43,40]
                                        # zmm5 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	kxnorw	k1, k0, k0
	mov	r9d, ebx
	lea	r14, [rcx + 16]
	add	rax, 256
	vpunpckldq	zmm3, zmm1, zmm2        # zmm3 = zmm1[0],zmm2[0],zmm1[1],zmm2[1],zmm1[4],zmm2[4],zmm1[5],zmm2[5],zmm1[8],zmm2[8],zmm1[9],zmm2[9],zmm1[12],zmm2[12],zmm1[13],zmm2[13]
	vpunpckhdq	zmm4, zmm29, zmm31      # zmm4 = zmm29[2],zmm31[2],zmm29[3],zmm31[3],zmm29[6],zmm31[6],zmm29[7],zmm31[7],zmm29[10],zmm31[10],zmm29[11],zmm31[11],zmm29[14],zmm31[14],zmm29[15],zmm31[15]
	vpunpckhdq	zmm1, zmm1, zmm2        # zmm1 = zmm1[2],zmm2[2],zmm1[3],zmm2[3],zmm1[6],zmm2[6],zmm1[7],zmm2[7],zmm1[10],zmm2[10],zmm1[11],zmm2[11],zmm1[14],zmm2[14],zmm1[15],zmm2[15]
	vpunpckldq	zmm30, zmm29, zmm31     # zmm30 = zmm29[0],zmm31[0],zmm29[1],zmm31[1],zmm29[4],zmm31[4],zmm29[5],zmm31[5],zmm29[8],zmm31[8],zmm29[9],zmm31[9],zmm29[12],zmm31[12],zmm29[13],zmm31[13]
	vpxord	xmm29, xmm29, xmm29
	vpunpcklqdq	zmm2, zmm4, zmm1        # zmm2 = zmm4[0],zmm1[0],zmm4[2],zmm1[2],zmm4[4],zmm1[4],zmm4[6],zmm1[6]
	vpunpckhqdq	zmm1, zmm4, zmm1        # zmm1 = zmm4[1],zmm1[1],zmm4[3],zmm1[3],zmm4[5],zmm1[5],zmm4[7],zmm1[7]
	vbroadcasti32x4	zmm4, xmmword ptr [rip + .LCPI1_30] # zmm4 = [61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32,61,13,18,0,29,45,50,32]
                                        # zmm4 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vpunpckhqdq	zmm30, zmm30, zmm3      # zmm30 = zmm30[1],zmm3[1],zmm30[3],zmm3[3],zmm30[5],zmm3[5],zmm30[7],zmm3[7]
	vpslld	zmm1, zmm1, 4
	vpandd	zmm1, zmm1, dword ptr [rip + .LCPI1_1]{1to16}
	vpmultishiftqb	zmm3, zmm5, zmm2
	vpsrld	zmm5, zmm2, 8
	vptestmd	k0, zmm30, zmm30
	vpternlogd	zmm5, zmm0, zmm12, 108  # zmm5 = zmm0 ^ (zmm5 & zmm12)
	vpaddd	zmm5, zmm5, zmm10
	vpmultishiftqb	zmm31, zmm4, zmm2
	vbroadcasti32x4	zmm4, xmmword ptr [rip + .LCPI1_31] # zmm4 = [0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38,0,18,1,6,0,50,33,38]
                                        # zmm4 = mem[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]
	vpgatherdd	zmm29 {k1}, dword ptr [r8 + 4*zmm1 + 76]
	vpsrld	zmm1, zmm2, 26
	vpermd	zmm1, zmm1, zmmword ptr [rip + .LCPI1_2]
	vpternlogd	zmm31, zmm3, zmm6, 216  # zmm31 = zmm31 ^ (zmm6 & (zmm31 ^ zmm3))
	vpmultishiftqb	zmm3, zmm4, zmm2
	vpslld	zmm4, zmm2, 4
	vpternlogd	zmm3, zmm31, zmm7, 228  # zmm3 = zmm31 ^ (zmm7 & (zmm3 ^ zmm31))
	vpsrad	zmm31, zmm4, 31
	vpshufb	zmm4, zmm8, zmm2
	vpternlogd	zmm2, zmm0, zmm11, 108  # zmm2 = zmm0 ^ (zmm2 & zmm11)
	vpaddd	zmm2, zmm2, zmm10
	vpternlogd	zmm2, zmm13, zmm14, 234 # zmm2 = (zmm2 & zmm13) | zmm14
	vpternlogd	zmm5, zmm2, zmm15, 236  # zmm5 = (zmm5 & zmm15) | zmm2
	vpandd	zmm2, zmm3, zmm16
	vpsrld	zmm3, zmm3, 3
	vpternlogd	zmm2, zmm29, zmm5, 248  # zmm2 = zmm2 | (zmm29 & zmm5)
	vpandd	zmm5, zmm31, zmm4
	vpternlogd	zmm3, zmm29, zmm18, 162 # zmm3 = zmm18 & (zmm3 | ~zmm29)
	vpslld	zmm4, zmm4, 16
	vpternlogd	zmm5, zmm1, zmm20, 236  # zmm5 = (zmm5 & zmm20) | zmm1
	vpminud	zmm2, zmm2, zmm17
	vpminud	zmm1, zmm3, zmm19
	vpternlogd	zmm5, zmm29, zmm21, 248 # zmm5 = zmm5 | (zmm29 & zmm21)
	vpternlogd	zmm5, zmm1, zmm2, 254   # zmm5 = zmm5 | zmm1 | zmm2
	vpsrad	zmm2, zmm30, 31
	vpxord	zmm1, zmm2, zmm30
	vpaddd	zmm2, zmm30, zmm10
	vplzcntd	zmm1, zmm1
	vpsrlvd	zmm1, zmm9, zmm1
	vpternlogd	zmm2, zmm30, zmm1, 20   # zmm2 = ~zmm1 & (zmm2 ^ zmm30)
	vpsrad	zmm1, zmm4, 31
	vpsrld	zmm30, zmm29, 29
	vpmovm2d	zmm4, k0
	vpternlogd	zmm4, zmm31, zmm1, 200  # zmm4 = zmm31 & (zmm4 | zmm1)
	vprorvd	zmm1, zmm2, zmm30
	vpandd	zmm2, zmm4, zmm22
	vpandd	zmm4, zmm31, zmm23
	vpsrad	zmm1, zmm1, 31
	vpternlogd	zmm4, zmm2, zmm1, 216   # zmm4 = zmm4 ^ (zmm1 & (zmm4 ^ zmm2))
	vpaddd	zmm2, zmm3, zmm10
	vpandd	zmm3, zmm29, zmm25
	vpord	zmm1, zmm4, zmm5
	vpsrad	zmm2, zmm2, 31
	vpternlogd	zmm1, zmm29, zmm24, 32  # zmm1 = zmm1 & zmm24 & ~zmm29
	vpaddd	zmm2, zmm2, zmm3
	vpopcntd	zmm1, zmm1
	vpaddd	zmm1, zmm2, zmm1
	vpermb	zmm1, zmm26, zmm1
	vpmullq	xmm2, xmm1, xmm27
	vpshufb	xmm3, xmm2, xmm28
	vpaddb	xmm2, xmm3, xmm2
	vpbroadcastd	zmm3, ebx
	vpsubb	xmm1, xmm2, xmm1
	vpextrb	ebx, xmm2, 15
	vpmovzxbd	zmm1, xmm1              # zmm1 = xmm1[0],zero,zero,zero,xmm1[1],zero,zero,zero,xmm1[2],zero,zero,zero,xmm1[3],zero,zero,zero,xmm1[4],zero,zero,zero,xmm1[5],zero,zero,zero,xmm1[6],zero,zero,zero,xmm1[7],zero,zero,zero,xmm1[8],zero,zero,zero,xmm1[9],zero,zero,zero,xmm1[10],zero,zero,zero,xmm1[11],zero,zero,zero,xmm1[12],zero,zero,zero,xmm1[13],zero,zero,zero,xmm1[14],zero,zero,zero,xmm1[15],zero,zero,zero
	add	ebx, r9d
	vpaddd	zmm1, zmm3, zmm1
	vmovdqu64	zmmword ptr [rdx + 4*rcx], zmm1
	add	rcx, 32
	cmp	rcx, rsi
	mov	rcx, r14
	jbe	.LBB1_4
