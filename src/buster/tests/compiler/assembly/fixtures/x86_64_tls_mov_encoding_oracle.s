# Independent GNU as / LLVM MC input for x86_64_tls_test.c's MOV IE rows.
# Symbolic operands require disp32/imm32. Both seven-byte envelopes preserve
# instruction length; the immediate form moves the GPR extension REX.R to B.
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
