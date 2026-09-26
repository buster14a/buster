0000000000005c80 <inc_number>:
    5c80:	push   %rbp
    5c81:	push   %r15
    5c83:	push   %r14
    5c85:	push   %rbx
    5c86:	push   %rax
    5c87:	mov    %rdx,%rbx
    5c8a:	mov    $0xa,%eax
    5c8f:	cmp    $0x2,%rsi
    5c93:	jb     5cd0 <inc_number+0x50>
    5c95:	cmpb   $0x30,(%rdi)
    5c98:	jne    5cd0 <inc_number+0x50>
    5c9a:	movzbl 0x1(%rdi),%ecx
    5c9e:	mov    $0x10,%eax
    5ca3:	mov    $0x2,%r8d
    5ca9:	cmp    $0x61,%ecx
    5cac:	jg     5cba <inc_number+0x3a>
    5cae:	cmp    $0x42,%ecx
    5cb1:	je     5cc4 <inc_number+0x44>
    5cb3:	cmp    $0x58,%ecx
    5cb6:	je     5cd3 <inc_number+0x53>
    5cb8:	jmp    5ccb <inc_number+0x4b>
    5cba:	cmp    $0x78,%ecx
    5cbd:	je     5cd3 <inc_number+0x53>
    5cbf:	cmp    $0x62,%ecx
    5cc2:	jne    5ccb <inc_number+0x4b>
    5cc4:	mov    $0x2,%eax
    5cc9:	jmp    5cd3 <inc_number+0x53>
    5ccb:	mov    $0x8,%eax
    5cd0:	xor    %r8d,%r8d
    5cd3:	mov    %eax,%r9d
    5cd6:	mov    $0xffffffffffffffff,%rax
    5cdd:	xor    %edx,%edx
    5cdf:	div    %r9
    5ce2:	xor    %ecx,%ecx
    5ce4:	cmp    %rsi,%r8
    5ce7:	jae    5e3c <inc_number+0x1bc>
    5ced:	mov    $0x1,%r11b
    5cf0:	xor    %r14d,%r14d
    5cf3:	mov    $0xffffffff,%ecx
    5cf8:	xor    %r10d,%r10d
    5cfb:	jmp    5d1d <inc_number+0x9d>
    5cfd:	nopl   (%rax)
    5d00:	add    $0xffffffd0,%r11d
    5d04:	cmp    %r9d,%r11d
    5d07:	setb   %r11b
    5d0b:	test   %r11b,%r11b
    5d0e:	je     5e17 <inc_number+0x197>
    5d14:	cmp    %rsi,%r8
    5d17:	jae    5e17 <inc_number+0x197>
    5d1d:	movzbl (%rdi,%r8,1),%r15d
    5d22:	cmp    $0x27,%r15d
    5d26:	jne    5d70 <inc_number+0xf0>
    5d28:	test   $0x1,%r10b
    5d2c:	je     5e3a <inc_number+0x1ba>
    5d32:	inc    %r8
    5d35:	cmp    %rsi,%r8
    5d38:	jae    5e3a <inc_number+0x1ba>
    5d3e:	movzbl (%rdi,%r8,1),%r11d
    5d43:	lea    -0x30(%r11),%ebp
    5d47:	cmp    $0x9,%bpl
    5d4b:	jbe    5d00 <inc_number+0x80>
    5d4d:	lea    -0x61(%r11),%ebp
    5d51:	cmp    $0x5,%bpl
    5d55:	ja     5e02 <inc_number+0x182>
    5d5b:	add    $0xffffffa9,%r11d
    5d5f:	jmp    5d04 <inc_number+0x84>
    5d61:	data16 data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
    5d70:	lea    -0x30(%r15),%ebp
    5d74:	cmp    $0x9,%bpl
    5d78:	ja     5d90 <inc_number+0x110>
    5d7a:	add    $0xffffffd0,%r15d
    5d7e:	cmp    %r9d,%r15d
    5d81:	jb     5dba <inc_number+0x13a>
    5d83:	jmp    5e17 <inc_number+0x197>
    5d88:	nopl   0x0(%rax,%rax,1)
    5d90:	lea    -0x61(%r15),%ebp
    5d94:	cmp    $0x5,%bpl
    5d98:	ja     5da5 <inc_number+0x125>
    5d9a:	add    $0xffffffa9,%r15d
    5d9e:	cmp    %r9d,%r15d
    5da1:	jb     5dba <inc_number+0x13a>
    5da3:	jmp    5e17 <inc_number+0x197>
    5da5:	lea    -0x41(%r15),%ebp
    5da9:	add    $0xffffffc9,%r15d
    5dad:	cmp    $0x6,%bpl
    5db1:	cmovae %ecx,%r15d
    5db5:	cmp    %r9d,%r15d
    5db8:	jae    5e17 <inc_number+0x197>
    5dba:	cmp    %rax,%r14
    5dbd:	jae    5dd0 <inc_number+0x150>
    5dbf:	imul   %r9,%r14
    5dc3:	mov    %r15d,%r10d
    5dc6:	jmp    5deb <inc_number+0x16b>
    5dc8:	nopl   0x0(%rax,%rax,1)
    5dd0:	sete   %r10b
    5dd4:	cmp    %edx,%r15d
    5dd7:	setbe  %r11b
    5ddb:	and    %r10b,%r11b
    5dde:	cmp    $0x1,%r11b
    5de2:	jne    5e3a <inc_number+0x1ba>
    5de4:	mov    %r15d,%r10d
    5de7:	imul   %r9,%r14
    5deb:	add    %r10,%r14
    5dee:	inc    %r8
    5df1:	mov    $0x1,%r11b
    5df4:	mov    $0x1,%r10b
    5df7:	test   %r11b,%r11b
    5dfa:	jne    5d14 <inc_number+0x94>
    5e00:	jmp    5e17 <inc_number+0x197>
    5e02:	lea    -0x41(%r11),%ebp
    5e06:	add    $0xffffffc9,%r11d
    5e0a:	cmp    $0x6,%bpl
    5e0e:	cmovae %ecx,%r11d
    5e12:	jmp    5d04 <inc_number+0x84>
    5e17:	xor    %ecx,%ecx
    5e19:	test   %r11b,%r11b
    5e1c:	je     5e3c <inc_number+0x1bc>
    5e1e:	and    $0x1,%r10b
    5e22:	je     5e3c <inc_number+0x1bc>
    5e24:	add    %r8,%rdi
    5e27:	sub    %r8,%rsi
    5e2a:	call   69e0 <inc_suffix_valid>
    5e2f:	test   %al,%al
    5e31:	je     5e3a <inc_number+0x1ba>
    5e33:	mov    %r14,(%rbx)
    5e36:	mov    $0x1,%cl
    5e38:	jmp    5e3c <inc_number+0x1bc>
    5e3a:	xor    %ecx,%ecx
    5e3c:	mov    %ecx,%eax
    5e3e:	add    $0x8,%rsp
    5e42:	pop    %rbx
    5e43:	pop    %r14
    5e45:	pop    %r15
    5e47:	pop    %rbp
    5e48:	ret
    5e49:	nopl   0x0(%rax)
