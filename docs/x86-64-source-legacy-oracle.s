# Independent GNU as / LLVM source oracle for x87, MMX, SSE and VEX.
.section .text.intel,"ax",@progbits
.intel_syntax noprefix
fld dword ptr [rbp]
fld qword ptr [r13+127]
fld tbyte ptr [r12+r9*4+128]
fstp tbyte ptr [r13+external_x87]
fild word ptr [r13]
fistp qword ptr [r13+128]
fadd dword ptr [rip+external_float]
fsub st(0), st(3)
fdivp st(3), st(0)
fucomi st(0), st(2)
fxch st(7)
fnstcw word ptr [r13]
fwait
emms
movq mm7, qword ptr [r13+external_mmx]
paddw mm7, qword ptr [r12+r9*4-128]
pxor mm7, mm6
movups xmm8, xmmword ptr [rbp]
movapd xmm9, xmmword ptr [r12+r9*4-129]
movdqu xmmword ptr [r13+external_sse], xmm10
addpd xmm11, xmmword ptr [rip+external_packed]
mulps xmm12, xmm13
addss xmm14, dword ptr [r13+127]
divsd xmm15, qword ptr [r13+128]
vmovups ymm8, ymmword ptr [r12+r9*4-128]
vmovdqu ymmword ptr [r13+external_vex], ymm9
vaddps ymm10, ymm11, ymmword ptr [r12+r9*4-129]
vsubpd ymm12, ymm13, ymmword ptr [rip+external_vector]
vpxor ymm14, ymm15, ymm8
.section .text.att,"ax",@progbits
.att_syntax prefix
flds (%rbp)
fldl 127(%r13)
fldt 128(%r12,%r9,4)
fstpt external_x87(%r13)
filds (%r13)
fistpll 128(%r13)
fadds external_float(%rip)
fsub %st(3), %st
fdivrp %st, %st(3)
fucomi %st(2), %st
fxch %st(7)
fnstcw (%r13)
fwait
emms
movq external_mmx(%r13), %mm7
paddw -128(%r12,%r9,4), %mm7
pxor %mm6, %mm7
movups (%rbp), %xmm8
movapd -129(%r12,%r9,4), %xmm9
movdqu %xmm10, external_sse(%r13)
addpd external_packed(%rip), %xmm11
mulps %xmm13, %xmm12
addss 127(%r13), %xmm14
divsd 128(%r13), %xmm15
vmovups -128(%r12,%r9,4), %ymm8
vmovdqu %ymm9, external_vex(%r13)
vaddps -129(%r12,%r9,4), %ymm11, %ymm10
vsubpd external_vector(%rip), %ymm13, %ymm12
vpxor %ymm8, %ymm15, %ymm14
