# Independent GNU as / LLVM APX source-layout oracle; encoding checks only.
.section .text.intel,"ax",@progbits
.intel_syntax noprefix
mov r16, r17
add r18d, r19d
adc qword ptr [r20+r21*4-128], r22
add r23, r24, qword ptr [r25+r26*8+127]
sub r27, r28, r29
{nf} xor r30, r31, qword ptr [r16+external_nf]
{nf} add dword ptr [rip+external_rip], 127
imul r17, qword ptr [r18+128], 128
{nf} shl r19, 255
lea r20, [r21+r22*4+external_lea]
movzx r23d, byte ptr [r24]
push2 r16, r17
pop2 r30, r31
.section .text.att,"ax",@progbits
.att_syntax prefix
movq %r17, %r16
addl %r19d, %r18d
adcq %r22, -128(%r20,%r21,4)
addq 127(%r25,%r26,8), %r24, %r23
subq %r29, %r28, %r27
{nf} xorq external_nf(%r16), %r31, %r30
{nf} addl $127, external_rip(%rip)
imulq $128, 128(%r18), %r17
{nf} shlq $255, %r19
leaq external_lea(%r21,%r22,4), %r20
movzbl (%r24), %r23d
push2 %r17, %r16
pop2 %r31, %r30
