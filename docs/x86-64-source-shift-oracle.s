# Independent GNU as / LLVM integrated-assembler byte and relocation oracle.
# Both sections must agree; no instruction is specified with literal bytes.
.section .text.intel,"ax",@progbits
.intel_syntax noprefix
rol al, 1
ror ax, cl
rcl r8d, 127
rcr r9, 255
shl ah, 1
shr spl, cl
sar r10w, 128
rol byte ptr [r13], 1
ror word ptr [r12+r9*4-128], cl
rcl dword ptr [r12+r9*4-129], 127
rcr qword ptr [r13+external_disp], 255
shl qword ptr [rip+external_rip], 1
shr dword ptr [r12+r9*4+127], cl
sar word ptr [r12+r9*4+128], 255
shld ax, r8w, cl
shrd r9d, r10d, 127
shld qword ptr [r13+external_double_disp], r8, 255
shrd qword ptr [rip+external_double_rip], r9, 128
.section .text.att,"ax",@progbits
.att_syntax prefix
rolb $1, %al
rorw %cl, %ax
rcll $127, %r8d
rcrq $255, %r9
shlb $1, %ah
shrb %cl, %spl
sarw $128, %r10w
rolb $1, (%r13)
rorw %cl, -128(%r12,%r9,4)
rcll $127, -129(%r12,%r9,4)
rcrq $255, external_disp(%r13)
shlq $1, external_rip(%rip)
shrl %cl, 127(%r12,%r9,4)
sarw $255, 128(%r12,%r9,4)
shldw %cl, %r8w, %ax
shrdl $127, %r10d, %r9d
shldq $255, %r8, external_double_disp(%r13)
shrdq $128, %r9, external_double_rip(%rip)
