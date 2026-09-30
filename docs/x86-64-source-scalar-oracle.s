# Independent GNU as / LLVM integrated-assembler source layout oracle.
.section .text.intel,"ax",@progbits
.intel_syntax noprefix
nop
ret
int3
cbw
cwde
cdqe
cwd
cdq
cqo
mov r8d, dword ptr [rbp]
mov word ptr [r12+r9*4-128], r10w
mov r11, qword ptr [r13+external_mov]
add byte ptr [r13], 127
sub word ptr [r12+r9*4-129], 128
adc r8d, dword ptr [r13+127]
sbb r9, qword ptr [r13+128]
and qword ptr [rip+external_alu], 127
or r10d, 128
xor r11d, dword ptr [r12+r9*4]
cmp byte ptr [r13], 255
test qword ptr [r13], 128
imul r8w, word ptr [r13], -128
imul r9d, dword ptr [r13], -129
mul byte ptr [r13]
idiv qword ptr [r13+128]
inc byte ptr [r13+external_unary]
dec r8w
neg r9d
not r10
push qword ptr [r13+external_push]
pop qword ptr [r12+r9*4]
bsf r8w, word ptr [r13]
bsr r9d, dword ptr [r13+127]
bswap r10
bt qword ptr [r13], r8
bts dword ptr [rip+external_bit], 7
lock xadd qword ptr [r13], r8
lock cmpxchg byte ptr [r13], r8b
sete byte ptr [r13+external_set]
cmovne r9, qword ptr [r13+128]
call external_call
jmp external_jump
jne external_condition
.section .text.att,"ax",@progbits
.att_syntax prefix
nop
ret
int3
cbtw
cwtl
cltq
cwtd
cltd
cqto
movl (%rbp), %r8d
movw %r10w, -128(%r12,%r9,4)
movq external_mov(%r13), %r11
addb $127, (%r13)
subw $128, -129(%r12,%r9,4)
adcl 127(%r13), %r8d
sbbq 128(%r13), %r9
andq $127, external_alu(%rip)
orl $128, %r10d
xorl (%r12,%r9,4), %r11d
cmpb $255, (%r13)
testq $128, (%r13)
imulw $-128, (%r13), %r8w
imull $-129, (%r13), %r9d
mulb (%r13)
idivq 128(%r13)
incb external_unary(%r13)
decw %r8w
negl %r9d
notq %r10
pushq external_push(%r13)
popq (%r12,%r9,4)
bsfw (%r13), %r8w
bsrl 127(%r13), %r9d
bswapq %r10
btq %r8, (%r13)
btsl $7, external_bit(%rip)
lock xaddq %r8, (%r13)
lock cmpxchgb %r8b, (%r13)
sete external_set(%r13)
cmovneq 128(%r13), %r9
call external_call
jmp external_jump
jne external_condition
