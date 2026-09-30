0000000000005e50 <scalar_number>:
    5e50:	push   %rbp
    5e51:	push   %r15
    5e53:	push   %r14
    5e55:	push   %r13
    5e57:	push   %r12
    5e59:	push   %rbx
    5e5a:	push   %rax
    5e5b:	movabs $0x1999999999999999,%r9
    5e65:	mov    $0x13,%r11d
    5e6b:	mov    $0xa,%r8d
    5e71:	movl   $0x5,0x4(%rsp)
    5e79:	cmp    $0x2,%rsi
    5e7d:	jb     5ec7 <scalar_number+0x77>
    5e7f:	cmpb   $0x30,(%rdi)
    5e82:	jne    5ec7 <scalar_number+0x77>
    5e84:	movzbl 0x1(%rdi),%eax
    5e88:	and    $0xdf,%al
    5e8a:	cmp    $0x58,%al
    5e8c:	jne    5fc2 <scalar_number+0x172>
    5e92:	movabs $0xfffffffffffffff,%r9
    5e9c:	mov    $0x2,%ecx
    5ea1:	mov    $0x10,%r8d
    5ea7:	movl   $0xf,0x4(%rsp)
    5eaf:	mov    $0x10,%r11d
    5eb5:	xor    %r14d,%r14d
    5eb8:	mov    $0x0,%eax
    5ebd:	cmp    %rsi,%rcx
    5ec0:	jb     5eda <scalar_number+0x8a>
    5ec2:	jmp    6029 <scalar_number+0x1d9>
    5ec7:	xor    %ecx,%ecx
    5ec9:	xor    %r14d,%r14d
    5ecc:	mov    $0x0,%eax
    5ed1:	cmp    %rsi,%rcx
    5ed4:	jae    6029 <scalar_number+0x1d9>
    5eda:	mov    %r8d,%eax
    5edd:	mov    $0x1,%bpl
    5ee0:	lea    0x31f9(%rip),%r10        # 90e0 <scalar_digit_table>
    5ee7:	xor    %r15d,%r15d
    5eea:	jmp    5f1f <scalar_number+0xcf>
    5eec:	nopl   0x0(%rax)
    5ef0:	mov    %ebp,%r12d
    5ef3:	and    $0x1,%r12b
    5ef7:	mov    %r15,%rbx
    5efa:	imul   %rax,%rbx
    5efe:	mov    %r13d,%r13d
    5f01:	add    %rbx,%r13
    5f04:	test   %r12b,%r12b
    5f07:	cmovne %r13,%r15
    5f0b:	movzbl %r12b,%r13d
    5f0f:	add    %r13d,%r14d
    5f12:	add    %r13,%rcx
    5f15:	test   %r12b,%r12b
    5f18:	je     5f94 <scalar_number+0x144>
    5f1a:	cmp    %rsi,%rcx
    5f1d:	jae    5f94 <scalar_number+0x144>
    5f1f:	movzbl (%rdi,%rcx,1),%r12d
    5f24:	movzbl (%r12,%r10,1),%r13d
    5f29:	cmp    %r13d,%r8d
    5f2c:	jbe    5f50 <scalar_number+0x100>
    5f2e:	cmp    %r11d,%r14d
    5f31:	jb     5ef0 <scalar_number+0xa0>
    5f33:	mov    $0x1,%bpl
    5f36:	cmp    %r9,%r15
    5f39:	jb     5ef0 <scalar_number+0xa0>
    5f3b:	sete   %r12b
    5f3f:	cmp    %r13d,0x4(%rsp)
    5f44:	setae  %bpl
    5f48:	and    %r12b,%bpl
    5f4b:	jmp    5ef0 <scalar_number+0xa0>
    5f4d:	nopl   (%rax)
    5f50:	cmp    $0x27,%r12b
    5f54:	jne    5f94 <scalar_number+0x144>
    5f56:	test   %r14d,%r14d
    5f59:	je     5f80 <scalar_number+0x130>
    5f5b:	lea    0x1(%rcx),%r12
    5f5f:	cmp    %rsi,%r12
    5f62:	jae    5f80 <scalar_number+0x130>
    5f64:	movzbl (%rdi,%r12,1),%ebx
    5f69:	movzbl (%rbx,%r10,1),%ebx
    5f6e:	cmp    %ebx,%r8d
    5f71:	seta   %r12b
    5f75:	jmp    5f83 <scalar_number+0x133>
    5f77:	nopw   0x0(%rax,%rax,1)
    5f80:	xor    %r12d,%r12d
    5f83:	mov    $0x1,%r13d
    5f89:	mov    %r12d,%ebp
    5f8c:	add    %r13,%rcx
    5f8f:	test   %r12b,%r12b
    5f92:	jne    5f1a <scalar_number+0xca>
    5f94:	xor    %eax,%eax
    5f96:	test   $0x1,%bpl
    5f9a:	je     6029 <scalar_number+0x1d9>
    5fa0:	test   %r14d,%r14d
    5fa3:	je     6029 <scalar_number+0x1d9>
    5fa9:	mov    %rdx,%r14
    5fac:	add    %rcx,%rdi
    5faf:	sub    %rcx,%rsi
    5fb2:	call   69e0 <inc_suffix_valid>
    5fb7:	test   %al,%al
    5fb9:	je     6027 <scalar_number+0x1d7>
    5fbb:	mov    %r15,(%r14)
    5fbe:	mov    $0x1,%al
    5fc0:	jmp    6029 <scalar_number+0x1d9>
    5fc2:	xor    %ecx,%ecx
    5fc4:	cmp    $0x42,%al
    5fc6:	sete   %cl
    5fc9:	mov    $0x2,%eax
    5fce:	mov    $0x8,%r8d
    5fd4:	cmove  %eax,%r8d
    5fd8:	movabs $0x7fffffffffffffff,%rax
    5fe2:	movabs $0x1fffffffffffffff,%r9
    5fec:	cmove  %rax,%r9
    5ff0:	mov    $0x1,%eax
    5ff5:	mov    $0x7,%r10d
    5ffb:	cmove  %eax,%r10d
    5fff:	mov    %r10d,0x4(%rsp)
    6004:	mov    $0x40,%eax
    6009:	mov    $0x15,%r11d
    600f:	cmove  %eax,%r11d
    6013:	add    %ecx,%ecx
    6015:	xor    %r14d,%r14d
    6018:	mov    $0x0,%eax
    601d:	cmp    %rsi,%rcx
    6020:	jae    6029 <scalar_number+0x1d9>
    6022:	jmp    5eda <scalar_number+0x8a>
    6027:	xor    %eax,%eax
    6029:	add    $0x8,%rsp
    602d:	pop    %rbx
    602e:	pop    %r12
    6030:	pop    %r13
    6032:	pop    %r14
    6034:	pop    %r15
    6036:	pop    %rbp
    6037:	ret
    6038:	nopl   0x0(%rax,%rax,1)
