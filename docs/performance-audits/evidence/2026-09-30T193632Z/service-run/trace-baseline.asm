0000000000004120 <audit_query_trace>:
    4120:	55                   	push   %rbp
    4121:	41 57                	push   %r15
    4123:	41 56                	push   %r14
    4125:	41 55                	push   %r13
    4127:	41 54                	push   %r12
    4129:	53                   	push   %rbx
    412a:	48 83 ec 38          	sub    $0x38,%rsp
    412e:	48 89 fb             	mov    %rdi,%rbx
    4131:	85 f6                	test   %esi,%esi
    4133:	0f 84 00 01 00 00    	je     4239 <audit_query_trace+0x119>
    4139:	49 89 e6             	mov    %rsp,%r14
    413c:	4c 89 f7             	mov    %r14,%rdi
    413f:	48 89 de             	mov    %rbx,%rsi
    4142:	31 d2                	xor    %edx,%edx
    4144:	31 c9                	xor    %ecx,%ecx
    4146:	e8 65 01 00 00       	call   42b0 <ir_type_abi_value>
    414b:	44 8b 7c 24 08       	mov    0x8(%rsp),%r15d
    4150:	b9 01 00 00 00       	mov    $0x1,%ecx
    4155:	4c 89 f7             	mov    %r14,%rdi
    4158:	48 89 de             	mov    %rbx,%rsi
    415b:	31 d2                	xor    %edx,%edx
    415d:	44 03 7c 24 30       	add    0x30(%rsp),%r15d
    4162:	e8 49 01 00 00       	call   42b0 <ir_type_abi_value>
    4167:	44 8b 64 24 08       	mov    0x8(%rsp),%r12d
    416c:	ba 02 00 00 00       	mov    $0x2,%edx
    4171:	4c 89 f7             	mov    %r14,%rdi
    4174:	48 89 de             	mov    %rbx,%rsi
    4177:	31 c9                	xor    %ecx,%ecx
    4179:	44 03 64 24 30       	add    0x30(%rsp),%r12d
    417e:	4d 01 fc             	add    %r15,%r12
    4181:	e8 2a 01 00 00       	call   42b0 <ir_type_abi_value>
    4186:	44 8b 6c 24 08       	mov    0x8(%rsp),%r13d
    418b:	ba 02 00 00 00       	mov    $0x2,%edx
    4190:	b9 01 00 00 00       	mov    $0x1,%ecx
    4195:	4c 89 f7             	mov    %r14,%rdi
    4198:	48 89 de             	mov    %rbx,%rsi
    419b:	44 03 6c 24 30       	add    0x30(%rsp),%r13d
    41a0:	e8 0b 01 00 00       	call   42b0 <ir_type_abi_value>
    41a5:	44 8b 7c 24 08       	mov    0x8(%rsp),%r15d
    41aa:	ba 06 00 00 00       	mov    $0x6,%edx
    41af:	4c 89 f7             	mov    %r14,%rdi
    41b2:	48 89 de             	mov    %rbx,%rsi
    41b5:	31 c9                	xor    %ecx,%ecx
    41b7:	44 03 7c 24 30       	add    0x30(%rsp),%r15d
    41bc:	4d 01 ef             	add    %r13,%r15
    41bf:	4d 01 e7             	add    %r12,%r15
    41c2:	e8 e9 00 00 00       	call   42b0 <ir_type_abi_value>
    41c7:	44 8b 64 24 08       	mov    0x8(%rsp),%r12d
    41cc:	ba 06 00 00 00       	mov    $0x6,%edx
    41d1:	b9 01 00 00 00       	mov    $0x1,%ecx
    41d6:	4c 89 f7             	mov    %r14,%rdi
    41d9:	48 89 de             	mov    %rbx,%rsi
    41dc:	44 03 64 24 30       	add    0x30(%rsp),%r12d
    41e1:	e8 ca 00 00 00       	call   42b0 <ir_type_abi_value>
    41e6:	44 8b 6c 24 08       	mov    0x8(%rsp),%r13d
    41eb:	ba 0e 00 00 00       	mov    $0xe,%edx
    41f0:	4c 89 f7             	mov    %r14,%rdi
    41f3:	48 89 de             	mov    %rbx,%rsi
    41f6:	31 c9                	xor    %ecx,%ecx
    41f8:	44 03 6c 24 30       	add    0x30(%rsp),%r13d
    41fd:	4d 01 e5             	add    %r12,%r13
    4200:	e8 ab 00 00 00       	call   42b0 <ir_type_abi_value>
    4205:	44 8b 64 24 08       	mov    0x8(%rsp),%r12d
    420a:	ba 0e 00 00 00       	mov    $0xe,%edx
    420f:	b9 01 00 00 00       	mov    $0x1,%ecx
    4214:	4c 89 f7             	mov    %r14,%rdi
    4217:	48 89 de             	mov    %rbx,%rsi
    421a:	44 03 64 24 30       	add    0x30(%rsp),%r12d
    421f:	4d 01 ec             	add    %r13,%r12
    4222:	4d 01 fc             	add    %r15,%r12
    4225:	e8 86 00 00 00       	call   42b0 <ir_type_abi_value>
    422a:	44 8b 7c 24 08       	mov    0x8(%rsp),%r15d
    422f:	44 03 7c 24 30       	add    0x30(%rsp),%r15d
    4234:	4d 01 e7             	add    %r12,%r15
    4237:	eb 61                	jmp    429a <audit_query_trace+0x17a>
    4239:	8b ab 30 01 00 00    	mov    0x130(%rbx),%ebp
    423f:	45 31 f6             	xor    %r14d,%r14d
    4242:	85 ed                	test   %ebp,%ebp
    4244:	74 51                	je     4297 <audit_query_trace+0x177>
    4246:	49 89 e4             	mov    %rsp,%r12
    4249:	45 31 ff             	xor    %r15d,%r15d
    424c:	0f 1f 40 00          	nopl   0x0(%rax)
    4250:	4c 89 e7             	mov    %r12,%rdi
    4253:	48 89 de             	mov    %rbx,%rsi
    4256:	44 89 f2             	mov    %r14d,%edx
    4259:	31 c9                	xor    %ecx,%ecx
    425b:	e8 50 00 00 00       	call   42b0 <ir_type_abi_value>
    4260:	44 8b 6c 24 08       	mov    0x8(%rsp),%r13d
    4265:	b9 01 00 00 00       	mov    $0x1,%ecx
    426a:	4c 89 e7             	mov    %r12,%rdi
    426d:	48 89 de             	mov    %rbx,%rsi
    4270:	44 89 f2             	mov    %r14d,%edx
    4273:	44 03 6c 24 30       	add    0x30(%rsp),%r13d
    4278:	4d 01 fd             	add    %r15,%r13
    427b:	e8 30 00 00 00       	call   42b0 <ir_type_abi_value>
    4280:	44 8b 7c 24 08       	mov    0x8(%rsp),%r15d
    4285:	41 ff c6             	inc    %r14d
    4288:	44 03 7c 24 30       	add    0x30(%rsp),%r15d
    428d:	4d 01 ef             	add    %r13,%r15
    4290:	44 39 f5             	cmp    %r14d,%ebp
    4293:	75 bb                	jne    4250 <audit_query_trace+0x130>
    4295:	eb 03                	jmp    429a <audit_query_trace+0x17a>
    4297:	45 31 ff             	xor    %r15d,%r15d
    429a:	4c 89 f8             	mov    %r15,%rax
    429d:	48 83 c4 38          	add    $0x38,%rsp
    42a1:	5b                   	pop    %rbx
    42a2:	41 5c                	pop    %r12
    42a4:	41 5d                	pop    %r13
    42a6:	41 5e                	pop    %r14
    42a8:	41 5f                	pop    %r15
    42aa:	5d                   	pop    %rbp
    42ab:	c3                   	ret
    42ac:	0f 1f 40 00          	nopl   0x0(%rax)
