# Independent GNU as / LLVM MC input for x86_64_tls_test.c.
# The GD padding is assembler prefix syntax, not copied instruction bytes.
# GAS selects accumulator opcode 05 for symbolic ADD to RAX (six bytes).
# LLVM selects group 81 (seven bytes); that is the fixed-width IE rewrite.
# Assemble both, compare .text.gd/.text.le/.text.ie exactly, and compare
# .text.add with that single documented encoding-alternative exception.
# .text.ie.mov/.text.mov cover the load-to-immediate IE relaxation for all
# sixteen registers; the symbolic immediate fixes MOV's sign-extending imm32.
.section .text.gd,"ax",@progbits
data16 leaq tls_value@TLSGD(%rip), %rdi
data16
data16
rex64
call __tls_get_addr@PLT
.section .text.le,"ax",@progbits
movq %fs:0, %rax
{disp32} leaq -4(%rax), %rax
.section .text.ie,"ax",@progbits
addq tls_value@GOTTPOFF(%rip), %rax
addq tls_value@GOTTPOFF(%rip), %rcx
addq tls_value@GOTTPOFF(%rip), %rdx
addq tls_value@GOTTPOFF(%rip), %rbx
addq tls_value@GOTTPOFF(%rip), %rsp
addq tls_value@GOTTPOFF(%rip), %rbp
addq tls_value@GOTTPOFF(%rip), %rsi
addq tls_value@GOTTPOFF(%rip), %rdi
addq tls_value@GOTTPOFF(%rip), %r8
addq tls_value@GOTTPOFF(%rip), %r9
addq tls_value@GOTTPOFF(%rip), %r10
addq tls_value@GOTTPOFF(%rip), %r11
addq tls_value@GOTTPOFF(%rip), %r12
addq tls_value@GOTTPOFF(%rip), %r13
addq tls_value@GOTTPOFF(%rip), %r14
addq tls_value@GOTTPOFF(%rip), %r15
.section .text.add,"ax",@progbits
addq $tls_offset, %rax
addq $tls_offset, %rcx
addq $tls_offset, %rdx
addq $tls_offset, %rbx
addq $tls_offset, %rsp
addq $tls_offset, %rbp
addq $tls_offset, %rsi
addq $tls_offset, %rdi
addq $tls_offset, %r8
addq $tls_offset, %r9
addq $tls_offset, %r10
addq $tls_offset, %r11
addq $tls_offset, %r12
addq $tls_offset, %r13
addq $tls_offset, %r14
addq $tls_offset, %r15

.section .text.ie.mov,"ax",@progbits
movq tls_value@GOTTPOFF(%rip), %rax
movq tls_value@GOTTPOFF(%rip), %rcx
movq tls_value@GOTTPOFF(%rip), %rdx
movq tls_value@GOTTPOFF(%rip), %rbx
movq tls_value@GOTTPOFF(%rip), %rsp
movq tls_value@GOTTPOFF(%rip), %rbp
movq tls_value@GOTTPOFF(%rip), %rsi
movq tls_value@GOTTPOFF(%rip), %rdi
movq tls_value@GOTTPOFF(%rip), %r8
movq tls_value@GOTTPOFF(%rip), %r9
movq tls_value@GOTTPOFF(%rip), %r10
movq tls_value@GOTTPOFF(%rip), %r11
movq tls_value@GOTTPOFF(%rip), %r12
movq tls_value@GOTTPOFF(%rip), %r13
movq tls_value@GOTTPOFF(%rip), %r14
movq tls_value@GOTTPOFF(%rip), %r15
.section .text.mov,"ax",@progbits
movq $tls_offset, %rax
movq $tls_offset, %rcx
movq $tls_offset, %rdx
movq $tls_offset, %rbx
movq $tls_offset, %rsp
movq $tls_offset, %rbp
movq $tls_offset, %rsi
movq $tls_offset, %rdi
movq $tls_offset, %r8
movq $tls_offset, %r9
movq $tls_offset, %r10
movq $tls_offset, %r11
movq $tls_offset, %r12
movq $tls_offset, %r13
movq $tls_offset, %r14
movq $tls_offset, %r15
.section .note.GNU-stack,"",@progbits
