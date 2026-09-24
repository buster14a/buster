# Independent GNU as / LLVM integrated assembler oracle for source
# MOVZX, MOVSX, and MOVSXD layout. The two sections should have identical
# bytes and fixups; unresolved base displacement must occupy four bytes.
.section .text.intel,"ax"
.intel_syntax noprefix
movzx r8d, byte ptr [rbp]
movsx r9, word ptr [r12+r10*4+127]
movsx r9, word ptr [r12+r10*4+128]
movsxd r11, dword ptr [r13+external_disp]
movsx rax, byte ptr [rip+external_rip]

.section .text.att,"ax"
.att_syntax prefix
movzbl (%rbp), %r8d
movswq 127(%r12,%r10,4), %r9
movswq 128(%r12,%r10,4), %r9
movslq external_disp(%r13), %r11
movsbq external_rip(%rip), %rax
