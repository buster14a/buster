.intel_syntax noprefix
.section .text.got,"ax",@progbits
mov rax, qword ptr [rip+target@GOTPCREL]
mov rcx, qword ptr [rip+target@GOTPCREL]
mov rdx, qword ptr [rip+target@GOTPCREL]
mov rbx, qword ptr [rip+target@GOTPCREL]
mov rsp, qword ptr [rip+target@GOTPCREL]
mov rbp, qword ptr [rip+target@GOTPCREL]
mov rsi, qword ptr [rip+target@GOTPCREL]
mov rdi, qword ptr [rip+target@GOTPCREL]
mov r8, qword ptr [rip+target@GOTPCREL]
mov r9, qword ptr [rip+target@GOTPCREL]
mov r10, qword ptr [rip+target@GOTPCREL]
mov r11, qword ptr [rip+target@GOTPCREL]
mov r12, qword ptr [rip+target@GOTPCREL]
mov r13, qword ptr [rip+target@GOTPCREL]
mov r14, qword ptr [rip+target@GOTPCREL]
mov r15, qword ptr [rip+target@GOTPCREL]
.section .text.address,"ax",@progbits
lea rax, [rip+target]
lea rcx, [rip+target]
lea rdx, [rip+target]
lea rbx, [rip+target]
lea rsp, [rip+target]
lea rbp, [rip+target]
lea rsi, [rip+target]
lea rdi, [rip+target]
lea r8, [rip+target]
lea r9, [rip+target]
lea r10, [rip+target]
lea r11, [rip+target]
lea r12, [rip+target]
lea r13, [rip+target]
lea r14, [rip+target]
lea r15, [rip+target]
.section .text.got32,"ax",@progbits
mov eax, dword ptr [rip+target@GOTPCREL]
mov ecx, dword ptr [rip+target@GOTPCREL]
mov edx, dword ptr [rip+target@GOTPCREL]
mov ebx, dword ptr [rip+target@GOTPCREL]
mov esp, dword ptr [rip+target@GOTPCREL]
mov ebp, dword ptr [rip+target@GOTPCREL]
mov esi, dword ptr [rip+target@GOTPCREL]
mov edi, dword ptr [rip+target@GOTPCREL]
mov r8d, dword ptr [rip+target@GOTPCREL]
mov r9d, dword ptr [rip+target@GOTPCREL]
mov r10d, dword ptr [rip+target@GOTPCREL]
mov r11d, dword ptr [rip+target@GOTPCREL]
mov r12d, dword ptr [rip+target@GOTPCREL]
mov r13d, dword ptr [rip+target@GOTPCREL]
mov r14d, dword ptr [rip+target@GOTPCREL]
mov r15d, dword ptr [rip+target@GOTPCREL]
.section .text.immediate32,"ax",@progbits
mov eax, offset target
mov ecx, offset target
mov edx, offset target
mov ebx, offset target
mov esp, offset target
mov ebp, offset target
mov esi, offset target
mov edi, offset target
mov r8d, offset target
mov r9d, offset target
mov r10d, offset target
mov r11d, offset target
mov r12d, offset target
mov r13d, offset target
mov r14d, offset target
mov r15d, offset target
.section .text.alu,"ax",@progbits
add eax, dword ptr [rip+target@GOTPCREL]
add r8d, dword ptr [rip+target@GOTPCREL]
add rax, qword ptr [rip+target@GOTPCREL]
add r8, qword ptr [rip+target@GOTPCREL]
or eax, dword ptr [rip+target@GOTPCREL]
or r8d, dword ptr [rip+target@GOTPCREL]
or rax, qword ptr [rip+target@GOTPCREL]
or r8, qword ptr [rip+target@GOTPCREL]
adc eax, dword ptr [rip+target@GOTPCREL]
adc r8d, dword ptr [rip+target@GOTPCREL]
adc rax, qword ptr [rip+target@GOTPCREL]
adc r8, qword ptr [rip+target@GOTPCREL]
sbb eax, dword ptr [rip+target@GOTPCREL]
sbb r8d, dword ptr [rip+target@GOTPCREL]
sbb rax, qword ptr [rip+target@GOTPCREL]
sbb r8, qword ptr [rip+target@GOTPCREL]
and eax, dword ptr [rip+target@GOTPCREL]
and r8d, dword ptr [rip+target@GOTPCREL]
and rax, qword ptr [rip+target@GOTPCREL]
and r8, qword ptr [rip+target@GOTPCREL]
sub eax, dword ptr [rip+target@GOTPCREL]
sub r8d, dword ptr [rip+target@GOTPCREL]
sub rax, qword ptr [rip+target@GOTPCREL]
sub r8, qword ptr [rip+target@GOTPCREL]
xor eax, dword ptr [rip+target@GOTPCREL]
xor r8d, dword ptr [rip+target@GOTPCREL]
xor rax, qword ptr [rip+target@GOTPCREL]
xor r8, qword ptr [rip+target@GOTPCREL]
cmp eax, dword ptr [rip+target@GOTPCREL]
cmp r8d, dword ptr [rip+target@GOTPCREL]
cmp rax, qword ptr [rip+target@GOTPCREL]
cmp r8, qword ptr [rip+target@GOTPCREL]
.section .text.alu_immediate,"ax",@progbits
add eax, offset target
add r8d, offset target
add rax, offset target
add r8, offset target
or eax, offset target
or r8d, offset target
or rax, offset target
or r8, offset target
adc eax, offset target
adc r8d, offset target
adc rax, offset target
adc r8, offset target
sbb eax, offset target
sbb r8d, offset target
sbb rax, offset target
sbb r8, offset target
and eax, offset target
and r8d, offset target
and rax, offset target
and r8, offset target
sub eax, offset target
sub r8d, offset target
sub rax, offset target
sub r8, offset target
xor eax, offset target
xor r8d, offset target
xor rax, offset target
xor r8, offset target
cmp eax, offset target
cmp r8d, offset target
cmp rax, offset target
cmp r8, offset target
.section .text.test,"ax",@progbits
test dword ptr [rip+target@GOTPCREL], eax
test dword ptr [rip+target@GOTPCREL], r8d
test qword ptr [rip+target@GOTPCREL], rax
test qword ptr [rip+target@GOTPCREL], r8
.section .text.test_immediate,"ax",@progbits
test eax, offset target
test r8d, offset target
test rax, offset target
test r8, offset target
.section .text.indirect,"ax",@progbits
call qword ptr [rip+target@GOTPCREL]
jmp qword ptr [rip+target@GOTPCREL]
push qword ptr [rip+target@GOTPCREL]
.section .text.direct,"ax",@progbits
call target
jmp target
push offset target
.section .note.GNU-stack,"",@progbits
