
work/local-plainkeys:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .text:

000000000008c880 <refine_pass_b_64_ordinary>:
   8c880:	push   %rbp
   8c881:	mov    %rsp,%rbp
   8c884:	push   %r15
   8c886:	push   %r14
   8c888:	push   %r13
   8c88a:	push   %r12
   8c88c:	push   %rbx
   8c88d:	and    $0xffffffffffffffc0,%rsp
   8c891:	sub    $0x1ac0,%rsp
   8c898:	vmovdqa 0x5f5b0(%rip),%xmm0        # ebe50 <refine_test_report_fault.suffix+0x1c20>
   8c8a0:	vpxor  %xmm2,%xmm2,%xmm2
   8c8a4:	mov    %rdx,0xf8(%rsp)
   8c8ac:	mov    %rdi,0x38(%rsp)
   8c8b1:	mov    %r8d,0x94(%rsp)
   8c8b9:	vmovdqa64 0x576fd(%rip),%zmm10        # e3fc0 <_IO_stdin_used+0xafc0>
   8c8c3:	vpmovsxbd 0x4c794(%rip),%xmm11        # d9060 <_IO_stdin_used+0x60>
   8c8cc:	vmovdqa64 0x576ea(%rip),%zmm12        # e3fc0 <_IO_stdin_used+0xafc0>
   8c8d6:	vbroadcasti32x4 0x5f5a0(%rip),%zmm13        # ebe80 <refine_test_report_fault.suffix+0x1c50>
   8c8e0:	vbroadcasti32x4 0x5f5a6(%rip),%zmm14        # ebe90 <refine_test_report_fault.suffix+0x1c60>
   8c8ea:	vbroadcasti32x4 0x5f57c(%rip),%zmm15        # ebe70 <refine_test_report_fault.suffix+0x1c40>
   8c8f4:	mov    %rsi,%r12
   8c8f7:	lea    0xa00(%rsp),%r13
   8c8ff:	lea    0x1c0(%rsp),%rdx
   8c907:	mov    $0xffffffffffffffff,%rsi
   8c90e:	mov    $0xffffffff,%edi
   8c913:	xor    %r15d,%r15d
   8c916:	mov    %fs:0x28,%rax
   8c91f:	mov    %rax,0x1a80(%rsp)
   8c927:	lea    0x1040(%rcx),%rax
   8c92e:	mov    %rcx,0x78(%rsp)
   8c933:	mov    %r12,0x68(%rsp)
   8c938:	mov    0x3140(%rcx),%r9d
   8c93f:	mov    %rax,0xc0(%rsp)
   8c947:	lea    0x104c(%rcx),%rax
   8c94e:	vaesenc %xmm2,%xmm0,%xmm1
   8c953:	mov    %rax,0xb8(%rsp)
   8c95b:	lea    0x1044(%rcx),%rax
   8c962:	mov    %rax,0xa8(%rsp)
   8c96a:	mov    $0x1,%al
   8c96c:	vaesenc %xmm2,%xmm1,%xmm3
   8c971:	vmovdqa 0x5f4e7(%rip),%xmm1        # ebe60 <refine_test_report_fault.suffix+0x1c30>
   8c979:	mov    %r9,0x80(%rsp)
   8c981:	vaesenc %xmm2,%xmm1,%xmm4
   8c986:	vaesenc %xmm2,%xmm4,%xmm4
   8c98b:	vpxor  %xmm3,%xmm4,%xmm3
   8c98f:	vpxor  %xmm4,%xmm4,%xmm4
   8c993:	vshufi64x2 $0x0,%zmm3,%zmm3,%zmm3
   8c99a:	jmp    8c9cf <refine_pass_b_64_ordinary+0x14f>
   8c99c:	nopl   0x0(%rax)
   8c9a0:	testb  $0x1,0x28(%rsp)
   8c9a5:	mov    0x80(%rsp),%r9
   8c9ad:	mov    $0x40,%r15d
   8c9b3:	mov    $0x0,%eax
   8c9b8:	mov    %r10,0x800(%rdx)
   8c9bf:	movb   $0x0,0x808(%rdx)
   8c9c6:	mov    %r13,%rdx
   8c9c9:	je     8ceea <refine_pass_b_64_ordinary+0x66a>
   8c9cf:	mov    %r9,%rcx
   8c9d2:	sub    %r15,%rcx
   8c9d5:	jbe    8ceea <refine_pass_b_64_ordinary+0x66a>
   8c9db:	mov    0xc0(%rsp),%r11
   8c9e3:	mov    0x38(%rsp),%r10
   8c9e8:	mov    0x78(%rsp),%r9
   8c9ed:	mov    %eax,0x28(%rsp)
   8c9f1:	cmp    $0x40,%ecx
   8c9f4:	mov    $0x40,%r14d
   8c9fa:	cmovb  %ecx,%r14d
   8c9fe:	xor    %r8d,%r8d
   8ca01:	mov    %r14d,%ebx
   8ca04:	lea    (%r11,%r15,4),%rax
   8ca08:	mov    (%r10),%r11
   8ca0b:	mov    0x8(%r10),%r10
   8ca0f:	lea    (%r9,%r15,4),%r9
   8ca13:	mov    %rax,0x30(%rsp)
   8ca18:	lea    0x400(%rdx),%rax
   8ca1f:	mov    %rax,0x48(%rsp)
   8ca24:	cmp    $0x4,%rcx
   8ca28:	jb     8cd1b <refine_pass_b_64_ordinary+0x49b>
   8ca2e:	mov    0xb8(%rsp),%rax
   8ca36:	mov    %r10,%rcx
   8ca39:	shl    $0x6,%rcx
   8ca3d:	mov    %r14d,0x40(%rsp)
   8ca42:	mov    %r9,0x20(%rsp)
   8ca47:	lea    0x400(%rdx),%r14
   8ca4e:	mov    $0x1111,%r9w
   8ca53:	mov    %r15,0x18(%rsp)
   8ca58:	xor    %r8d,%r8d
   8ca5b:	vpbroadcastq %rcx,%zmm5
   8ca61:	lea    (%rax,%r15,4),%rcx
   8ca65:	data16 cs nopw 0x0(%rax,%rax,1)
   8ca70:	mov    %r8,%r15
   8ca73:	mov    -0xc(%rcx,%r8,4),%r8d
   8ca78:	mov    -0x104c(%rcx,%r15,4),%eax
   8ca80:	mov    -0x1048(%rcx,%r15,4),%r13d
   8ca88:	bzhi   %r8d,%edi,%r8d
   8ca8d:	kmovd  %r8d,%k1
   8ca92:	mov    -0x1044(%rcx,%r15,4),%r8d
   8ca9a:	vmovdqu8 (%r12,%rax,1),%ymm6{%k1}{z}
   8caa1:	mov    -0x8(%rcx,%r15,4),%eax
   8caa6:	bzhi   %eax,%edi,%eax
   8caab:	kmovd  %eax,%k1
   8caaf:	mov    -0x4(%rcx,%r15,4),%eax
   8cab4:	vmovdqu8 (%r12,%r13,1),%ymm7{%k1}{z}
   8cabb:	bzhi   %eax,%edi,%eax
   8cac0:	kmovd  %eax,%k1
   8cac4:	mov    (%rcx,%r15,4),%eax
   8cac8:	vmovdqu8 (%r12,%r8,1),%ymm8{%k1}{z}
   8cacf:	mov    -0x1040(%rcx,%r15,4),%r8d
   8cad7:	vinserti64x4 $0x1,%ymm7,%zmm6,%zmm6
   8cade:	bzhi   %eax,%edi,%eax
   8cae3:	kmovd  %eax,%k1
   8cae7:	vmovdqu8 (%r12,%r8,1),%ymm9{%k1}{z}
   8caee:	kmovd  %r9d,%k1
   8caf3:	lea    0x4(%r15),%r8
   8caf7:	vinserti64x4 $0x1,%ymm9,%zmm8,%zmm7
   8cafe:	vmovdqu -0xc(%rcx,%r15,4),%xmm8
   8cb05:	add    $0x8,%r15
   8cb09:	vshufi64x2 $0x88,%zmm7,%zmm6,%zmm9
   8cb10:	vshufi64x2 $0xdd,%zmm7,%zmm6,%zmm6
   8cb17:	vpxorq %zmm14,%zmm6,%zmm6
   8cb1d:	vaesenc %zmm4,%zmm6,%zmm6
   8cb23:	vpexpandd %zmm8,%zmm8{%k1}{z}
   8cb29:	vaesenc %zmm4,%zmm6,%zmm6
   8cb2f:	vpternlogq $0x96,%zmm13,%zmm9,%zmm8
   8cb36:	vaesenc %zmm4,%zmm8,%zmm7
   8cb3c:	vaesenc %zmm4,%zmm7,%zmm7
   8cb42:	vpternlogq $0x96,%zmm3,%zmm7,%zmm6
   8cb49:	vporq  %zmm15,%zmm9,%zmm7
   8cb4f:	vaesenc %zmm4,%zmm6,%zmm6
   8cb55:	vmovdqa64 %zmm7,(%r14)
   8cb5b:	vpsllq $0x6,%zmm6,%zmm6
   8cb62:	vpandq %zmm5,%zmm6,%zmm6
   8cb68:	vmovq  %xmm6,%rax
   8cb6d:	vextracti128 $0x1,%ymm6,%xmm7
   8cb73:	vmovdqa64 %zmm6,-0x400(%r14)
   8cb7a:	add    $0x40,%r14
   8cb7e:	prefetcht0 (%r11,%rax,1)
   8cb83:	vmovq  %xmm7,%rax
   8cb88:	vextracti32x4 $0x2,%zmm6,%xmm7
   8cb8f:	vextracti32x4 $0x3,%zmm6,%xmm6
   8cb96:	prefetcht0 (%r11,%rax,1)
   8cb9b:	vmovq  %xmm7,%rax
   8cba0:	prefetcht0 (%r11,%rax,1)
   8cba5:	vmovq  %xmm6,%rax
   8cbaa:	prefetcht0 (%r11,%rax,1)
   8cbaf:	cmp    %rbx,%r15
   8cbb2:	jbe    8ca70 <refine_pass_b_64_ordinary+0x1f0>
   8cbb8:	vpbroadcastd 0x4c4aa(%rip),%zmm5        # d906c <_IO_stdin_used+0x6c>
   8cbc2:	mov    0x30(%rsp),%rsi
   8cbc7:	mov    $0xffff,%r9d
   8cbcd:	mov    %rbx,0x140(%rsp)
   8cbd5:	bzhi   %r8d,%r9d,%ecx
   8cbda:	kmovd  %ecx,%k1
   8cbde:	vpcmpltud (%rsi),%zmm5,%k0{%k1}
   8cbe5:	kmovw  %k0,%ecx
   8cbe9:	cmp    $0x10,%r8
   8cbed:	jbe    8cc60 <refine_pass_b_64_ordinary+0x3e0>
   8cbef:	lea    -0x10(%r8),%eax
   8cbf3:	bzhi   %eax,%r9d,%eax
   8cbf8:	kmovd  %eax,%k1
   8cbfc:	vpcmpltud 0x40(%rsi),%zmm5,%k0{%k1}
   8cc04:	kmovd  %k0,%eax
   8cc08:	shl    $0x10,%eax
   8cc0b:	or     %rax,%rcx
   8cc0e:	cmp    $0x21,%r8
   8cc12:	jb     8cc60 <refine_pass_b_64_ordinary+0x3e0>
   8cc14:	lea    -0x20(%r8),%eax
   8cc18:	bzhi   %eax,%r9d,%eax
   8cc1d:	kmovd  %eax,%k1
   8cc21:	vpcmpltud 0x80(%rsi),%zmm5,%k0{%k1}
   8cc29:	kmovw  %k0,%eax
   8cc2d:	shl    $0x20,%rax
   8cc31:	or     %rax,%rcx
   8cc34:	cmp    $0x31,%r8
   8cc38:	jb     8cc60 <refine_pass_b_64_ordinary+0x3e0>
   8cc3a:	lea    -0x30(%r8),%eax
   8cc3e:	bzhi   %eax,%r9d,%eax
   8cc43:	kmovd  %eax,%k1
   8cc47:	vpcmpltud 0xc0(%rsi),%zmm5,%k0{%k1}
   8cc4f:	kmovd  %k0,%eax
   8cc53:	movzwl %ax,%eax
   8cc56:	shl    $0x30,%rax
   8cc5a:	or     %rax,%rcx
   8cc5d:	nopl   (%rax)
   8cc60:	mov    0x20(%rsp),%r9
   8cc65:	mov    0x68(%rsp),%r12
   8cc6a:	mov    $0xffffffffffffffff,%rbx
   8cc71:	test   %rcx,%rcx
   8cc74:	je     8ccfa <refine_pass_b_64_ordinary+0x47a>
   8cc7a:	nopw   0x0(%rax,%rax,1)
   8cc80:	tzcnt  %rcx,%r14
   8cc85:	lea    0x0(,%r14,4),%eax
   8cc8d:	shl    $0x4,%r14d
   8cc91:	mov    (%rsi,%rax,1),%r15d
   8cc95:	mov    (%r9,%rax,1),%eax
   8cc99:	bzhi   %r15,%rbx,%r13
   8cc9e:	vmovd  %r15d,%xmm6
   8cca3:	kmovq  %r13,%k1
   8cca8:	vmovdqu8 (%r12,%rax,1),%zmm5{%k1}{z}
   8ccaf:	vpternlogq $0x96,%zmm12,%zmm5,%zmm6
   8ccb6:	vaesenc %zmm4,%zmm6,%zmm5
   8ccbc:	vaesenc %zmm4,%zmm5,%zmm5
   8ccc2:	vextracti64x4 $0x1,%zmm5,%ymm6
   8ccc9:	vpxorq %zmm5,%zmm6,%zmm5
   8cccf:	vextracti128 $0x1,%ymm5,%xmm6
   8ccd5:	vpxor  %xmm5,%xmm6,%xmm5
   8ccd9:	vaesenc %xmm2,%xmm5,%xmm5
   8ccde:	vmovq  %xmm5,%rax
   8cce3:	and    %r10,%rax
   8cce6:	shl    $0x6,%rax
   8ccea:	blsr   %rcx,%rcx
   8ccef:	mov    %rax,(%rdx,%r14,1)
   8ccf3:	prefetcht0 (%r11,%rax,1)
   8ccf8:	jne    8cc80 <refine_pass_b_64_ordinary+0x400>
   8ccfa:	mov    0x18(%rsp),%r15
   8ccff:	mov    0x140(%rsp),%rbx
   8cd07:	mov    0x40(%rsp),%r14d
   8cd0c:	lea    0xa00(%rsp),%r13
   8cd14:	mov    $0xffffffffffffffff,%rsi
   8cd1b:	cmp    %r14d,%r8d
   8cd1e:	jae    8c9a0 <refine_pass_b_64_ordinary+0x120>
   8cd24:	mov    %r14d,%eax
   8cd27:	sub    %r8d,%eax
   8cd2a:	mov    %r8d,%r14d
   8cd2d:	mov    %r14,%rcx
   8cd30:	test   $0x1,%al
   8cd32:	je     8cdbc <refine_pass_b_64_ordinary+0x53c>
   8cd38:	mov    0x30(%rsp),%rax
   8cd3d:	mov    (%r9,%r14,4),%r8d
   8cd41:	mov    (%rax,%r14,4),%eax
   8cd45:	bzhi   %rax,%rsi,%rcx
   8cd4a:	vmovd  %eax,%xmm6
   8cd4e:	kmovq  %rcx,%k1
   8cd53:	mov    %r14,%rcx
   8cd56:	shl    $0x4,%rcx
   8cd5a:	vmovdqu8 (%r12,%r8,1),%zmm5{%k1}{z}
   8cd61:	mov    0x48(%rsp),%r8
   8cd66:	vpternlogq $0x96,%zmm12,%zmm5,%zmm6
   8cd6d:	vpor   0x5f0fb(%rip),%xmm5,%xmm5        # ebe70 <refine_test_report_fault.suffix+0x1c40>
   8cd75:	vaesenc %zmm4,%zmm6,%zmm6
   8cd7b:	vaesenc %zmm4,%zmm6,%zmm6
   8cd81:	vextracti64x4 $0x1,%zmm6,%ymm7
   8cd88:	vpxorq %zmm6,%zmm7,%zmm6
   8cd8e:	vextracti128 $0x1,%ymm6,%xmm7
   8cd94:	vpxor  %xmm6,%xmm7,%xmm6
   8cd98:	vaesenc %xmm2,%xmm6,%xmm6
   8cd9d:	vmovq  %xmm6,%rax
   8cda2:	and    %r10,%rax
   8cda5:	shl    $0x6,%rax
   8cda9:	mov    %rax,(%rdx,%rcx,1)
   8cdad:	vmovdqa %xmm5,(%r8,%rcx,1)
   8cdb3:	prefetcht0 (%r11,%rax,1)
   8cdb8:	lea    0x1(%r14),%rcx
   8cdbc:	lea    -0x1(%rbx),%rax
   8cdc0:	cmp    %r14,%rax
   8cdc3:	je     8c9a0 <refine_pass_b_64_ordinary+0x120>
   8cdc9:	mov    0xa8(%rsp),%r8
   8cdd1:	mov    %rcx,%rax
   8cdd4:	shl    $0x4,%rax
   8cdd8:	lea    0x410(%rdx,%rax,1),%rax
   8cde0:	lea    (%r8,%r15,4),%r9
   8cde4:	data16 data16 cs nopw 0x0(%rax,%rax,1)
   8cdf0:	mov    -0x4(%r9,%rcx,4),%r8d
   8cdf5:	mov    -0x1044(%r9,%rcx,4),%r15d
   8cdfd:	bzhi   %r8,%rsi,%r14
   8ce02:	vmovd  %r8d,%xmm6
   8ce07:	kmovq  %r14,%k1
   8ce0c:	vmovdqu8 (%r12,%r15,1),%zmm5{%k1}{z}
   8ce13:	vpternlogq $0x96,%zmm5,%zmm10,%zmm6
   8ce1a:	vpor   %xmm5,%xmm11,%xmm5
   8ce1e:	vaesenc %zmm4,%zmm6,%zmm6
   8ce24:	vaesenc %zmm4,%zmm6,%zmm6
   8ce2a:	vextracti64x4 $0x1,%zmm6,%ymm7
   8ce31:	vpxorq %zmm6,%zmm7,%zmm6
   8ce37:	vextracti128 $0x1,%ymm6,%xmm7
   8ce3d:	vpxor  %xmm6,%xmm7,%xmm6
   8ce41:	vaesenc %xmm2,%xmm6,%xmm6
   8ce46:	vmovq  %xmm6,%r8
   8ce4b:	and    %r10,%r8
   8ce4e:	shl    $0x6,%r8
   8ce52:	mov    %r8,-0x410(%rax)
   8ce59:	vmovdqa %xmm5,-0x10(%rax)
   8ce5e:	prefetcht0 (%r11,%r8,1)
   8ce63:	mov    (%r9,%rcx,4),%r8d
   8ce67:	mov    -0x1040(%r9,%rcx,4),%r15d
   8ce6f:	add    $0x2,%rcx
   8ce73:	bzhi   %r8,%rsi,%r14
   8ce78:	vmovd  %r8d,%xmm6
   8ce7d:	kmovq  %r14,%k1
   8ce82:	vmovdqu8 (%r12,%r15,1),%zmm5{%k1}{z}
   8ce89:	vpternlogq $0x96,%zmm5,%zmm10,%zmm6
   8ce90:	vpor   %xmm5,%xmm11,%xmm5
   8ce94:	vaesenc %zmm4,%zmm6,%zmm6
   8ce9a:	vaesenc %zmm4,%zmm6,%zmm6
   8cea0:	vextracti64x4 $0x1,%zmm6,%ymm7
   8cea7:	vpxorq %zmm6,%zmm7,%zmm6
   8cead:	vextracti128 $0x1,%ymm6,%xmm7
   8ceb3:	vpxor  %xmm6,%xmm7,%xmm6
   8ceb7:	vaesenc %xmm2,%xmm6,%xmm6
   8cebc:	vmovq  %xmm6,%r8
   8cec1:	and    %r10,%r8
   8cec4:	shl    $0x6,%r8
   8cec8:	mov    %r8,-0x400(%rax)
   8cecf:	vmovdqa %xmm5,(%rax)
   8ced3:	prefetcht0 (%r11,%r8,1)
   8ced8:	add    $0x20,%rax
   8cedc:	cmp    %rbx,%rcx
   8cedf:	jb     8cdf0 <refine_pass_b_64_ordinary+0x570>
   8cee5:	jmp    8c9a0 <refine_pass_b_64_ordinary+0x120>
   8ceea:	test   %r9d,%r9d
   8ceed:	je     8e5ec <refine_pass_b_64_ordinary+0x1d6c>
   8cef3:	vpxor  %xmm7,%xmm7,%xmm7
   8cef7:	vaesenc %xmm7,%xmm0,%xmm0
   8cefc:	vaesenc %xmm7,%xmm1,%xmm1
   8cf01:	mov    0x78(%rsp),%rax
   8cf06:	vpbroadcastd 0x4c148(%rip),%zmm6        # d9058 <_IO_stdin_used+0x58>
   8cf10:	mov    $0x1111,%r13w
   8cf15:	vpxor  %xmm9,%xmm9,%xmm9
   8cf1a:	xor    %esi,%esi
   8cf1c:	xor    %edx,%edx
   8cf1e:	xor    %edi,%edi
   8cf20:	vaesenc %xmm7,%xmm0,%xmm0
   8cf25:	vaesenc %xmm7,%xmm1,%xmm1
   8cf2a:	lea    0x2080(%rax),%rcx
   8cf31:	mov    %rcx,0xf0(%rsp)
   8cf39:	lea    -0x80(%r9),%ecx
   8cf3d:	mov    %ecx,0x88(%rsp)
   8cf44:	lea    0x208c(%rax),%rcx
   8cf4b:	vpxor  %xmm0,%xmm1,%xmm0
   8cf4f:	mov    %rcx,0xe8(%rsp)
   8cf57:	lea    0x124c(%rax),%rcx
   8cf5e:	add    $0x1244,%rax
   8cf64:	vshufi64x2 $0x0,%zmm0,%zmm0,%zmm8
   8cf6b:	mov    %rcx,0xd0(%rsp)
   8cf73:	mov    %r9d,%ecx
   8cf76:	mov    %rax,0xc8(%rsp)
   8cf7e:	vmovdqa64 %zmm0,0x180(%rsp)
   8cf86:	vmovdqa64 %zmm8,0x140(%rsp)
   8cf8e:	mov    %ecx,0x90(%rsp)
   8cf95:	mov    %ecx,%ecx
   8cf97:	cmp    $0x40,%rcx
   8cf9b:	mov    $0x40,%eax
   8cfa0:	mov    %esi,0x8c(%rsp)
   8cfa7:	mov    %r9d,%r10d
   8cfaa:	mov    %edi,%r8d
   8cfad:	mov    %rdi,0x68(%rsp)
   8cfb2:	cmovae %rax,%rcx
   8cfb6:	mov    %esi,%eax
   8cfb8:	shl    $0x6,%eax
   8cfbb:	mov    %rcx,0x70(%rsp)
   8cfc0:	mov    0x88(%rsp),%ecx
   8cfc7:	mov    %ecx,%esi
   8cfc9:	sub    %eax,%esi
   8cfcb:	mov    $0x40,%ecx
   8cfd0:	cmp    $0x40,%esi
   8cfd3:	cmovae %ecx,%esi
   8cfd6:	mov    %rsi,0xa0(%rsp)
   8cfde:	mov    %r9d,%esi
   8cfe1:	mov    0x78(%rsp),%r9
   8cfe6:	sub    %eax,%esi
   8cfe8:	cmp    $0x40,%esi
   8cfeb:	cmovae %ecx,%esi
   8cfee:	mov    %r8,%rcx
   8cff1:	sub    %edi,%r10d
   8cff4:	mov    0x38(%rsp),%rdi
   8cff9:	mov    $0x40,%r8d
   8cfff:	mov    %rcx,0x58(%rsp)
   8d004:	cmp    $0x40,%r10d
   8d008:	mov    %r10d,0x100(%rsp)
   8d010:	cmovb  %r10d,%r8d
   8d014:	lea    (%r9,%rcx,4),%rax
   8d018:	mov    %r8,0x20(%rsp)
   8d01d:	mov    %rax,0x48(%rsp)
   8d022:	mov    0xc0(%rsp),%rax
   8d02a:	mov    0x8(%rdi),%r9
   8d02e:	lea    (%rax,%rcx,4),%r14
   8d032:	mov    %edx,%eax
   8d034:	imul   $0x840,%rax,%r11
   8d03b:	mov    (%rdi),%rdx
   8d03e:	mov    %rax,0xb0(%rsp)
   8d046:	mov    %r14,0x28(%rsp)
   8d04b:	lea    0x1c0(%rsp,%r11,1),%rax
   8d053:	mov    %r11,0x18(%rsp)
   8d058:	mov    %rax,0x40(%rsp)
   8d05d:	cmp    %r9,0x9c0(%rsp,%r11,1)
   8d065:	je     8d59a <refine_pass_b_64_ordinary+0xd1a>
   8d06b:	mov    0x18(%rsp),%rax
   8d070:	xor    %r11d,%r11d
   8d073:	lea    0x1c0(%rsp,%rax,1),%rax
   8d07b:	lea    0x400(%rax),%r15
   8d082:	cmp    $0x4,%r10d
   8d086:	jb     8d379 <refine_pass_b_64_ordinary+0xaf9>
   8d08c:	vmovdqa64 0x180(%rsp),%zmm0
   8d094:	mov    0xb8(%rsp),%rdi
   8d09c:	mov    %r9,%r11
   8d09f:	shl    $0x6,%r11
   8d0a3:	mov    %r8d,%r14d
   8d0a6:	mov    $0x1111,%r8w
   8d0ab:	mov    $0xffffffff,%eax
   8d0b0:	mov    %r15,0x30(%rsp)
   8d0b5:	vpbroadcastq %r11,%zmm1
   8d0bb:	xor    %r11d,%r11d
   8d0be:	lea    (%rdi,%rcx,4),%rbx
   8d0c2:	vshufi64x2 $0x0,%zmm0,%zmm0,%zmm0
   8d0c9:	nopl   0x0(%rax)
   8d0d0:	mov    %r11,%r13
   8d0d3:	mov    -0xc(%rbx,%r11,4),%r11d
   8d0d8:	mov    -0x104c(%rbx,%r13,4),%r10d
   8d0e0:	mov    -0x1048(%rbx,%r13,4),%edi
   8d0e8:	bzhi   %r11d,%eax,%r11d
   8d0ed:	kmovd  %r11d,%k1
   8d0f2:	lea    0x4(%r13),%r11
   8d0f6:	vmovdqu8 (%r12,%r10,1),%ymm2{%k1}{z}
   8d0fd:	mov    -0x8(%rbx,%r13,4),%r10d
   8d102:	bzhi   %r10d,%eax,%r10d
   8d107:	kmovd  %r10d,%k1
   8d10c:	mov    -0x1044(%rbx,%r13,4),%r10d
   8d114:	vmovdqu8 (%r12,%rdi,1),%ymm3{%k1}{z}
   8d11b:	mov    -0x4(%rbx,%r13,4),%edi
   8d120:	bzhi   %edi,%eax,%edi
   8d125:	kmovd  %edi,%k1
   8d129:	mov    (%rbx,%r13,4),%edi
   8d12d:	vmovdqu8 (%r12,%r10,1),%ymm4{%k1}{z}
   8d134:	mov    -0x1040(%rbx,%r13,4),%r10d
   8d13c:	vinserti64x4 $0x1,%ymm3,%zmm2,%zmm2
   8d143:	bzhi   %edi,%eax,%edi
   8d148:	kmovd  %edi,%k1
   8d14c:	vmovdqu8 (%r12,%r10,1),%ymm5{%k1}{z}
   8d153:	kmovd  %r8d,%k1
   8d158:	vinserti64x4 $0x1,%ymm5,%zmm4,%zmm3
   8d15f:	vmovdqu -0xc(%rbx,%r13,4),%xmm4
   8d166:	add    $0x8,%r13
   8d16a:	vshufi64x2 $0x88,%zmm3,%zmm2,%zmm5
   8d171:	vshufi64x2 $0xdd,%zmm3,%zmm2,%zmm2
   8d178:	vpxorq %zmm14,%zmm2,%zmm2
   8d17e:	vaesenc %zmm9,%zmm2,%zmm2
   8d184:	vpexpandd %zmm4,%zmm4{%k1}{z}
   8d18a:	vaesenc %zmm9,%zmm2,%zmm2
   8d190:	vpternlogq $0x96,%zmm13,%zmm5,%zmm4
   8d197:	vaesenc %zmm9,%zmm4,%zmm3
   8d19d:	vaesenc %zmm9,%zmm3,%zmm3
   8d1a3:	vpternlogq $0x96,%zmm0,%zmm3,%zmm2
   8d1aa:	vporq  %zmm15,%zmm5,%zmm3
   8d1b0:	vaesenc %zmm9,%zmm2,%zmm2
   8d1b6:	vmovdqa64 %zmm3,(%r15)
   8d1bc:	vpsllq $0x6,%zmm2,%zmm2
   8d1c3:	vpandq %zmm1,%zmm2,%zmm2
   8d1c9:	vmovq  %xmm2,%rdi
   8d1ce:	vextracti128 $0x1,%ymm2,%xmm3
   8d1d4:	vmovdqa64 %zmm2,-0x400(%r15)
   8d1db:	add    $0x40,%r15
   8d1df:	prefetcht0 (%rdx,%rdi,1)
   8d1e3:	vmovq  %xmm3,%rdi
   8d1e8:	vextracti32x4 $0x2,%zmm2,%xmm3
   8d1ef:	vextracti32x4 $0x3,%zmm2,%xmm2
   8d1f6:	prefetcht0 (%rdx,%rdi,1)
   8d1fa:	vmovq  %xmm3,%rdi
   8d1ff:	prefetcht0 (%rdx,%rdi,1)
   8d203:	vmovq  %xmm2,%rdi
   8d208:	prefetcht0 (%rdx,%rdi,1)
   8d20c:	cmp    %r14,%r13
   8d20f:	jbe    8d0d0 <refine_pass_b_64_ordinary+0x850>
   8d215:	vpbroadcastd 0x4be4d(%rip),%zmm0        # d906c <_IO_stdin_used+0x6c>
   8d21f:	mov    0x28(%rsp),%r8
   8d224:	mov    $0xffff,%eax
   8d229:	bzhi   %r11d,%eax,%ebx
   8d22e:	kmovd  %ebx,%k1
   8d232:	vpcmpltud (%r8),%zmm0,%k0{%k1}
   8d239:	kmovw  %k0,%ebx
   8d23d:	cmp    $0x10,%r11
   8d241:	jbe    8d2c0 <refine_pass_b_64_ordinary+0xa40>
   8d243:	lea    -0x10(%r11),%edi
   8d247:	bzhi   %edi,%eax,%edi
   8d24c:	kmovd  %edi,%k1
   8d250:	vpcmpltud 0x40(%r8),%zmm0,%k0{%k1}
   8d258:	kmovd  %k0,%edi
   8d25c:	shl    $0x10,%edi
   8d25f:	or     %rdi,%rbx
   8d262:	cmp    $0x21,%r11
   8d266:	jb     8d2c0 <refine_pass_b_64_ordinary+0xa40>
   8d268:	lea    -0x20(%r11),%edi
   8d26c:	bzhi   %edi,%eax,%edi
   8d271:	kmovd  %edi,%k1
   8d275:	vpcmpltud 0x80(%r8),%zmm0,%k0{%k1}
   8d27d:	kmovw  %k0,%edi
   8d281:	shl    $0x20,%rdi
   8d285:	or     %rdi,%rbx
   8d288:	cmp    $0x31,%r11
   8d28c:	jb     8d2c0 <refine_pass_b_64_ordinary+0xa40>
   8d28e:	lea    -0x30(%r11),%edi
   8d292:	bzhi   %edi,%eax,%edi
   8d297:	kmovd  %edi,%k1
   8d29b:	vpcmpltud 0xc0(%r8),%zmm0,%k0{%k1}
   8d2a3:	kmovd  %k0,%edi
   8d2a7:	movzwl %di,%edi
   8d2aa:	shl    $0x30,%rdi
   8d2ae:	or     %rdi,%rbx
   8d2b1:	data16 data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
   8d2c0:	mov    0x18(%rsp),%rcx
   8d2c5:	mov    0x48(%rsp),%r8
   8d2ca:	mov    0x28(%rsp),%r13
   8d2cf:	mov    $0xffffffffffffffff,%rax
   8d2d6:	lea    0x1c0(%rsp,%rcx,1),%rcx
   8d2de:	test   %rbx,%rbx
   8d2e1:	je     8d36a <refine_pass_b_64_ordinary+0xaea>
   8d2e7:	nopw   0x0(%rax,%rax,1)
   8d2f0:	tzcnt  %rbx,%r14
   8d2f5:	lea    0x0(,%r14,4),%edi
   8d2fd:	shl    $0x4,%r14d
   8d301:	mov    0x0(%r13,%rdi,1),%r10d
   8d306:	mov    (%r8,%rdi,1),%edi
   8d30a:	bzhi   %r10,%rax,%r15
   8d30f:	vmovd  %r10d,%xmm1
   8d314:	kmovq  %r15,%k1
   8d319:	vmovdqu8 (%r12,%rdi,1),%zmm0{%k1}{z}
   8d320:	vpternlogq $0x96,%zmm12,%zmm0,%zmm1
   8d327:	vaesenc %zmm9,%zmm1,%zmm0
   8d32d:	vaesenc %zmm9,%zmm0,%zmm0
   8d333:	vextracti64x4 $0x1,%zmm0,%ymm1
   8d33a:	vpxorq %zmm0,%zmm1,%zmm0
   8d340:	vextracti128 $0x1,%ymm0,%xmm1
   8d346:	vpxor  %xmm0,%xmm1,%xmm0
   8d34a:	vaesenc %xmm7,%xmm0,%xmm0
   8d34f:	vmovq  %xmm0,%rdi
   8d354:	and    %r9,%rdi
   8d357:	shl    $0x6,%rdi
   8d35b:	blsr   %rbx,%rbx
   8d360:	mov    %rdi,(%rcx,%r14,1)
   8d364:	prefetcht0 (%rdx,%rdi,1)
   8d368:	jne    8d2f0 <refine_pass_b_64_ordinary+0xa70>
   8d36a:	mov    0x58(%rsp),%rcx
   8d36f:	mov    0x20(%rsp),%r8
   8d374:	mov    0x30(%rsp),%r15
   8d379:	cmp    %r8d,%r11d
   8d37c:	jae    8d563 <refine_pass_b_64_ordinary+0xce3>
   8d382:	mov    %esi,%edi
   8d384:	mov    $0xffffffffffffffff,%rax
   8d38b:	sub    %r11d,%edi
   8d38e:	mov    %r11d,%ebx
   8d391:	mov    %rbx,%r11
   8d394:	test   $0x1,%dil
   8d398:	je     8d42d <refine_pass_b_64_ordinary+0xbad>
   8d39e:	mov    0x28(%rsp),%rdi
   8d3a3:	mov    0x48(%rsp),%r8
   8d3a8:	mov    (%rdi,%rbx,4),%edi
   8d3ab:	mov    (%r8,%rbx,4),%r11d
   8d3af:	bzhi   %rdi,%rax,%r10
   8d3b4:	vmovd  %edi,%xmm1
   8d3b8:	mov    0x18(%rsp),%rax
   8d3bd:	kmovq  %r10,%k1
   8d3c2:	mov    %rbx,%r10
   8d3c5:	shl    $0x4,%r10
   8d3c9:	vmovdqu8 (%r12,%r11,1),%zmm0{%k1}{z}
   8d3d0:	lea    0x1(%rbx),%r11
   8d3d4:	lea    0x1c0(%rsp,%rax,1),%rax
   8d3dc:	vpternlogq $0x96,%zmm12,%zmm0,%zmm1
   8d3e3:	vpor   0x5ea85(%rip),%xmm0,%xmm0        # ebe70 <refine_test_report_fault.suffix+0x1c40>
   8d3eb:	vaesenc %zmm9,%zmm1,%zmm1
   8d3f1:	vaesenc %zmm9,%zmm1,%zmm1
   8d3f7:	vextracti64x4 $0x1,%zmm1,%ymm2
   8d3fe:	vpxorq %zmm1,%zmm2,%zmm1
   8d404:	vextracti128 $0x1,%ymm1,%xmm2
   8d40a:	vpxor  %xmm1,%xmm2,%xmm1
   8d40e:	vaesenc %xmm7,%xmm1,%xmm1
   8d413:	vmovq  %xmm1,%rdi
   8d418:	and    %r9,%rdi
   8d41b:	shl    $0x6,%rdi
   8d41f:	mov    %rdi,(%rax,%r10,1)
   8d423:	vmovdqa %xmm0,(%r15,%r10,1)
   8d429:	prefetcht0 (%rdx,%rdi,1)
   8d42d:	lea    -0x1(%rsi),%rdi
   8d431:	mov    $0xffffffffffffffff,%rax
   8d438:	cmp    %rbx,%rdi
   8d43b:	je     8d563 <refine_pass_b_64_ordinary+0xce3>
   8d441:	mov    0xa8(%rsp),%rdi
   8d449:	mov    %r11,%r14
   8d44c:	shl    $0x4,%r14
   8d450:	lea    0x5d0(%rsp),%r8
   8d458:	mov    0x20(%rsp),%ebx
   8d45c:	add    %r8,%r14
   8d45f:	add    0x18(%rsp),%r14
   8d464:	lea    (%rdi,%rcx,4),%r10
   8d468:	nopl   0x0(%rax,%rax,1)
   8d470:	mov    -0x4(%r10,%r11,4),%edi
   8d475:	mov    -0x1044(%r10,%r11,4),%r13d
   8d47d:	bzhi   %rdi,%rax,%r15
   8d482:	vmovd  %edi,%xmm1
   8d486:	kmovq  %r15,%k1
   8d48b:	vmovdqu8 (%r12,%r13,1),%zmm0{%k1}{z}
   8d492:	vpternlogq $0x96,%zmm0,%zmm10,%zmm1
   8d499:	vpor   %xmm0,%xmm11,%xmm0
   8d49d:	vaesenc %zmm9,%zmm1,%zmm1
   8d4a3:	vaesenc %zmm9,%zmm1,%zmm1
   8d4a9:	vextracti64x4 $0x1,%zmm1,%ymm2
   8d4b0:	vpxorq %zmm1,%zmm2,%zmm1
   8d4b6:	vextracti128 $0x1,%ymm1,%xmm2
   8d4bc:	vpxor  %xmm1,%xmm2,%xmm1
   8d4c0:	vaesenc %xmm7,%xmm1,%xmm1
   8d4c5:	vmovq  %xmm1,%rdi
   8d4ca:	and    %r9,%rdi
   8d4cd:	shl    $0x6,%rdi
   8d4d1:	mov    %rdi,-0x410(%r14)
   8d4d8:	vmovdqa %xmm0,-0x10(%r14)
   8d4de:	prefetcht0 (%rdx,%rdi,1)
   8d4e2:	mov    (%r10,%r11,4),%edi
   8d4e6:	mov    -0x1040(%r10,%r11,4),%r13d
   8d4ee:	add    $0x2,%r11
   8d4f2:	bzhi   %rdi,%rax,%r15
   8d4f7:	vmovd  %edi,%xmm1
   8d4fb:	kmovq  %r15,%k1
   8d500:	vmovdqu8 (%r12,%r13,1),%zmm0{%k1}{z}
   8d507:	vpternlogq $0x96,%zmm0,%zmm10,%zmm1
   8d50e:	vpor   %xmm0,%xmm11,%xmm0
   8d512:	vaesenc %zmm9,%zmm1,%zmm1
   8d518:	vaesenc %zmm9,%zmm1,%zmm1
   8d51e:	vextracti64x4 $0x1,%zmm1,%ymm2
   8d525:	vpxorq %zmm1,%zmm2,%zmm1
   8d52b:	vextracti128 $0x1,%ymm1,%xmm2
   8d531:	vpxor  %xmm1,%xmm2,%xmm1
   8d535:	vaesenc %xmm7,%xmm1,%xmm1
   8d53a:	vmovq  %xmm1,%rdi
   8d53f:	and    %r9,%rdi
   8d542:	shl    $0x6,%rdi
   8d546:	mov    %rdi,-0x400(%r14)
   8d54d:	vmovdqa %xmm0,(%r14)
   8d552:	prefetcht0 (%rdx,%rdi,1)
   8d556:	add    $0x20,%r14
   8d55a:	cmp    %rbx,%r11
   8d55d:	jb     8d470 <refine_pass_b_64_ordinary+0xbf0>
   8d563:	mov    0x18(%rsp),%rax
   8d568:	mov    0x38(%rsp),%rdi
   8d56d:	mov    0x28(%rsp),%r14
   8d572:	mov    0x20(%rsp),%r8
   8d577:	mov    0x100(%rsp),%r10d
   8d57f:	mov    $0x1111,%r13w
   8d584:	lea    0x1c0(%rsp,%rax,1),%rax
   8d58c:	mov    %r9,0x800(%rax)
   8d593:	movb   $0x0,0x808(%rax)
   8d59a:	mov    0xe8(%rsp),%r9
   8d5a2:	mov    0x10(%rdi),%rax
   8d5a6:	mov    0x40(%rdi),%r15
   8d5aa:	xor    %ebx,%ebx
   8d5ac:	lea    (%r9,%rcx,4),%r9
   8d5b0:	mov    %rax,0x60(%rsp)
   8d5b5:	cmp    $0x4,%r10d
   8d5b9:	jb     8d6ca <refine_pass_b_64_ordinary+0xe4a>
   8d5bf:	mov    0x18(%rsp),%rax
   8d5c4:	lea    0x5f0(%rsp),%rdi
   8d5cc:	mov    %r8d,%r10d
   8d5cf:	mov    $0x8888,%r8w
   8d5d4:	xor    %ebx,%ebx
   8d5d6:	lea    (%rdi,%rax,1),%r11
   8d5da:	nopw   0x0(%rax,%rax,1)
   8d5e0:	mov    -0x430(%r11),%rdi
   8d5e7:	vbroadcasti32x4 -0x30(%r11),%zmm1
   8d5ee:	kmovd  %r13d,%k0
   8d5f3:	mov    %rbx,%r14
   8d5f6:	add    $0x8,%r14
   8d5fa:	vmovdqa64 (%rdx,%rdi,1),%zmm0
   8d601:	vpcmpeqd %zmm1,%zmm0,%k1
   8d607:	kaddw  %k0,%k1,%k2
   8d60b:	kmovd  %r8d,%k1
   8d610:	kandw  %k1,%k2,%k2
   8d614:	vpcompressd %zmm0,%zmm0{%k2}{z}
   8d61a:	vmovd  %xmm0,-0xc(%r9,%rbx,4)
   8d621:	mov    -0x420(%r11),%rdi
   8d628:	vbroadcasti32x4 -0x20(%r11),%zmm1
   8d62f:	vmovdqa64 (%rdx,%rdi,1),%zmm0
   8d636:	vpcmpeqd %zmm1,%zmm0,%k2
   8d63c:	kaddw  %k0,%k2,%k2
   8d640:	kandw  %k1,%k2,%k2
   8d644:	vpcompressd %zmm0,%zmm0{%k2}{z}
   8d64a:	vmovd  %xmm0,-0x8(%r9,%rbx,4)
   8d651:	mov    -0x410(%r11),%rdi
   8d658:	vbroadcasti32x4 -0x10(%r11),%zmm1
   8d65f:	vmovdqa64 (%rdx,%rdi,1),%zmm0
   8d666:	vpcmpeqd %zmm1,%zmm0,%k2
   8d66c:	kaddw  %k0,%k2,%k2
   8d670:	kandw  %k1,%k2,%k2
   8d674:	vpcompressd %zmm0,%zmm0{%k2}{z}
   8d67a:	vmovd  %xmm0,-0x4(%r9,%rbx,4)
   8d681:	mov    -0x400(%r11),%rdi
   8d688:	vbroadcasti32x4 (%r11),%zmm1
   8d68e:	add    $0x40,%r11
   8d692:	vmovdqa64 (%rdx,%rdi,1),%zmm0
   8d699:	vpcmpeqd %zmm1,%zmm0,%k2
   8d69f:	kaddw  %k0,%k2,%k0
   8d6a3:	kandw  %k1,%k0,%k1
   8d6a7:	vpcompressd %zmm0,%zmm0{%k1}{z}
   8d6ad:	vmovd  %xmm0,(%r9,%rbx,4)
   8d6b3:	add    $0x4,%rbx
   8d6b7:	cmp    %r10,%r14
   8d6ba:	jbe    8d5e0 <refine_pass_b_64_ordinary+0xd60>
   8d6c0:	mov    0x28(%rsp),%r14
   8d6c5:	mov    0x20(%rsp),%r8
   8d6ca:	mov    0xf0(%rsp),%rdi
   8d6d2:	lea    (%rdi,%rcx,4),%rax
   8d6d6:	mov    %rax,0x30(%rsp)
   8d6db:	cmp    %r8d,%ebx
   8d6de:	jae    8d899 <refine_pass_b_64_ordinary+0x1019>
   8d6e4:	mov    0x30(%rsp),%rcx
   8d6e9:	mov    %esi,%edi
   8d6eb:	sub    %ebx,%edi
   8d6ed:	mov    $0x8888,%ax
   8d6f1:	mov    %ebx,%r11d
   8d6f4:	mov    %r11,%r10
   8d6f7:	test   $0x3,%dil
   8d6fb:	je     8d785 <refine_pass_b_64_ordinary+0xf05>
   8d701:	mov    %r11,%r14
   8d704:	shl    $0x4,%r14
   8d708:	lea    0x5c0(%rsp),%rdi
   8d710:	mov    %r11,%r10
   8d713:	add    %rdi,%r14
   8d716:	mov    0x70(%rsp),%rdi
   8d71b:	add    0x18(%rsp),%r14
   8d720:	sub    %bl,%dil
   8d723:	xor    %ebx,%ebx
   8d725:	movzbl %dil,%r8d
   8d729:	and    $0x3,%r8d
   8d72d:	shl    $0x4,%r8d
   8d731:	data16 data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
   8d740:	mov    -0x400(%r14,%rbx,1),%rdi
   8d748:	vbroadcasti32x4 (%r14,%rbx,1),%zmm1
   8d74f:	kmovd  %r13d,%k1
   8d754:	add    $0x10,%rbx
   8d758:	vmovdqa64 (%rdx,%rdi,1),%zmm0
   8d75f:	vpcmpeqd %zmm1,%zmm0,%k0
   8d765:	kaddw  %k1,%k0,%k0
   8d769:	kmovd  %eax,%k1
   8d76d:	kandw  %k1,%k0,%k1
   8d771:	vpcompressd %zmm0,%zmm0{%k1}{z}
   8d777:	vmovd  %xmm0,(%rcx,%r10,4)
   8d77d:	inc    %r10
   8d780:	cmp    %rbx,%r8
   8d783:	jne    8d740 <refine_pass_b_64_ordinary+0xec0>
   8d785:	mov    0x28(%rsp),%r14
   8d78a:	sub    %rsi,%r11
   8d78d:	cmp    $0xfffffffffffffffc,%r11
   8d791:	mov    $0x8888,%r11w
   8d796:	ja     8d899 <refine_pass_b_64_ordinary+0x1019>
   8d79c:	mov    %r10,%r8
   8d79f:	shl    $0x4,%r8
   8d7a3:	lea    0x5f0(%rsp),%rdi
   8d7ab:	mov    0x20(%rsp),%esi
   8d7af:	add    %rdi,%r8
   8d7b2:	add    0x18(%rsp),%r8
   8d7b7:	nopw   0x0(%rax,%rax,1)
   8d7c0:	mov    -0x430(%r8),%rdi
   8d7c7:	vbroadcasti32x4 -0x30(%r8),%zmm1
   8d7ce:	kmovd  %r13d,%k0
   8d7d3:	vmovdqa64 (%rdx,%rdi,1),%zmm0
   8d7da:	vpcmpeqd %zmm1,%zmm0,%k1
   8d7e0:	kaddw  %k0,%k1,%k2
   8d7e4:	kmovd  %r11d,%k1
   8d7e9:	kandw  %k1,%k2,%k2
   8d7ed:	vpcompressd %zmm0,%zmm0{%k2}{z}
   8d7f3:	vmovd  %xmm0,-0xc(%r9,%r10,4)
   8d7fa:	mov    -0x420(%r8),%rdi
   8d801:	vbroadcasti32x4 -0x20(%r8),%zmm1
   8d808:	vmovdqa64 (%rdx,%rdi,1),%zmm0
   8d80f:	vpcmpeqd %zmm1,%zmm0,%k2
   8d815:	kaddw  %k0,%k2,%k2
   8d819:	kandw  %k1,%k2,%k2
   8d81d:	vpcompressd %zmm0,%zmm0{%k2}{z}
   8d823:	vmovd  %xmm0,-0x8(%r9,%r10,4)
   8d82a:	mov    -0x410(%r8),%rdi
   8d831:	vbroadcasti32x4 -0x10(%r8),%zmm1
   8d838:	vmovdqa64 (%rdx,%rdi,1),%zmm0
   8d83f:	vpcmpeqd %zmm1,%zmm0,%k2
   8d845:	kaddw  %k0,%k2,%k2
   8d849:	kandw  %k1,%k2,%k2
   8d84d:	vpcompressd %zmm0,%zmm0{%k2}{z}
   8d853:	vmovd  %xmm0,-0x4(%r9,%r10,4)
   8d85a:	mov    -0x400(%r8),%rdi
   8d861:	vbroadcasti32x4 (%r8),%zmm1
   8d867:	add    $0x40,%r8
   8d86b:	vmovdqa64 (%rdx,%rdi,1),%zmm0
   8d872:	vpcmpeqd %zmm1,%zmm0,%k2
   8d878:	kaddw  %k0,%k2,%k0
   8d87c:	kandw  %k1,%k0,%k1
   8d880:	vpcompressd %zmm0,%zmm0{%k1}{z}
   8d886:	vmovd  %xmm0,(%r9,%r10,4)
   8d88c:	add    $0x4,%r10
   8d890:	cmp    %rsi,%r10
   8d893:	jb     8d7c0 <refine_pass_b_64_ordinary+0xf40>
   8d899:	mov    0x20(%rsp),%rcx
   8d89e:	mov    $0xffff,%esi
   8d8a3:	mov    0x30(%rsp),%r9
   8d8a8:	mov    0x100(%rsp),%eax
   8d8af:	mov    %r14,%rdi
   8d8b2:	bzhi   %ecx,%esi,%edx
   8d8b7:	kmovd  %edx,%k1
   8d8bb:	vmovdqu32 (%r14),%zmm0{%k1}{z}
   8d8c1:	vmovdqu32 (%r9),%zmm1{%k1}{z}
   8d8c7:	vpcmpnleud %zmm6,%zmm0,%k0{%k1}
   8d8ce:	kmovw  %k0,%r8d
   8d8d2:	vpcmpnleud 0x4b77f(%rip){1to16},%zmm0,%k0{%k1}        # d905c <_IO_stdin_used+0x5c>
   8d8dd:	vptestnmd %zmm1,%zmm1,%k1{%k1}
   8d8e3:	kmovw  %k1,%ebx
   8d8e7:	kmovw  %k0,%r14d
   8d8eb:	cmp    $0x11,%eax
   8d8ee:	jb     8da10 <refine_pass_b_64_ordinary+0x1190>
   8d8f4:	lea    -0x10(%rcx),%edx
   8d8f7:	bzhi   %edx,%esi,%edx
   8d8fc:	kmovd  %edx,%k1
   8d900:	vmovdqu32 0x40(%rdi),%zmm0{%k1}{z}
   8d907:	vmovdqu32 0x40(%r9),%zmm1{%k1}{z}
   8d90e:	vpcmpnleud %zmm6,%zmm0,%k0{%k1}
   8d915:	kmovd  %k0,%edx
   8d919:	vpcmpnleud 0x4b738(%rip){1to16},%zmm0,%k0{%k1}        # d905c <_IO_stdin_used+0x5c>
   8d924:	shl    $0x10,%edx
   8d927:	or     %rdx,%r8
   8d92a:	kmovd  %k0,%edx
   8d92e:	vptestnmd %zmm1,%zmm1,%k0{%k1}
   8d934:	shl    $0x10,%edx
   8d937:	or     %rdx,%r14
   8d93a:	kmovd  %k0,%edx
   8d93e:	shl    $0x10,%edx
   8d941:	or     %rdx,%rbx
   8d944:	cmp    $0x21,%eax
   8d947:	jb     8da10 <refine_pass_b_64_ordinary+0x1190>
   8d94d:	lea    -0x20(%rcx),%edx
   8d950:	bzhi   %edx,%esi,%edx
   8d955:	kmovd  %edx,%k1
   8d959:	vmovdqu32 0x80(%rdi),%zmm0{%k1}{z}
   8d960:	vmovdqu32 0x80(%r9),%zmm1{%k1}{z}
   8d967:	vpcmpnleud %zmm6,%zmm0,%k0{%k1}
   8d96e:	kmovw  %k0,%edx
   8d972:	vpcmpnleud 0x4b6df(%rip){1to16},%zmm0,%k0{%k1}        # d905c <_IO_stdin_used+0x5c>
   8d97d:	vptestnmd %zmm1,%zmm1,%k1{%k1}
   8d983:	shl    $0x20,%rdx
   8d987:	or     %rdx,%r8
   8d98a:	kmovw  %k0,%edx
   8d98e:	shl    $0x20,%rdx
   8d992:	or     %rdx,%r14
   8d995:	kmovw  %k1,%edx
   8d999:	shl    $0x20,%rdx
   8d99d:	or     %rdx,%rbx
   8d9a0:	cmp    $0x31,%eax
   8d9a3:	jb     8da10 <refine_pass_b_64_ordinary+0x1190>
   8d9a5:	lea    -0x30(%rcx),%eax
   8d9a8:	bzhi   %eax,%esi,%eax
   8d9ad:	kmovd  %eax,%k1
   8d9b1:	vmovdqu32 0xc0(%rdi),%zmm0{%k1}{z}
   8d9b8:	vmovdqu32 0xc0(%r9),%zmm1{%k1}{z}
   8d9bf:	vpcmpnleud %zmm6,%zmm0,%k0{%k1}
   8d9c6:	kmovd  %k0,%eax
   8d9ca:	vpcmpnleud 0x4b687(%rip){1to16},%zmm0,%k0{%k1}        # d905c <_IO_stdin_used+0x5c>
   8d9d5:	movzwl %ax,%eax
   8d9d8:	shl    $0x30,%rax
   8d9dc:	or     %rax,%r8
   8d9df:	kmovd  %k0,%eax
   8d9e3:	vptestnmd %zmm1,%zmm1,%k0{%k1}
   8d9e9:	movzwl %ax,%eax
   8d9ec:	shl    $0x30,%rax
   8d9f0:	or     %rax,%r14
   8d9f3:	kmovd  %k0,%eax
   8d9f7:	movzwl %ax,%eax
   8d9fa:	shl    $0x30,%rax
   8d9fe:	or     %rax,%rbx
   8da01:	data16 data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
   8da10:	mov    0x40(%rsp),%rax
   8da15:	cmpb   $0x0,0x808(%rax)
   8da1c:	jne    8e538 <refine_pass_b_64_ordinary+0x1cb8>
   8da22:	or     %r8,%rbx
   8da25:	mov    0x30(%rsp),%r11
   8da2a:	mov    %r8,0x20(%rsp)
   8da2f:	andn   %r14,%rbx,%rax
   8da34:	mov    0x60(%rsp),%r14
   8da39:	mov    %rax,%rcx
   8da3c:	je     8da59 <refine_pass_b_64_ordinary+0x11d9>
   8da3e:	xchg   %ax,%ax
   8da40:	tzcnt  %rcx,%rdx
   8da45:	blsr   %rcx,%rcx
   8da4a:	mov    (%r11,%rdx,4),%edx
   8da4e:	mov    (%r14,%rdx,8),%edx
   8da52:	prefetcht0 (%r15,%rdx,1)
   8da57:	jne    8da40 <refine_pass_b_64_ordinary+0x11c0>
   8da59:	mov    0x68(%rsp),%rcx
   8da5e:	mov    0x80(%rsp),%rdx
   8da66:	mov    0x48(%rsp),%r9
   8da6b:	mov    0x28(%rsp),%r10
   8da70:	mov    $0xffffffffffffffff,%r8
   8da77:	add    $0x80,%ecx
   8da7d:	mov    %edx,%edi
   8da7f:	sub    %ecx,%edi
   8da81:	jbe    8e046 <refine_pass_b_64_ordinary+0x17c6>
   8da87:	mov    0xb0(%rsp),%rcx
   8da8f:	sub    $0x1,%ecx
   8da92:	mov    $0x2,%edx
   8da97:	lea    0x200(%r10),%rsi
   8da9e:	mov    $0x40,%r10d
   8daa4:	lea    0x200(%r9),%r9
   8daab:	cmovb  %edx,%ecx
   8daae:	cmp    $0x40,%edi
   8dab1:	mov    %rsi,0x100(%rsp)
   8dab9:	cmovb  %edi,%r10d
   8dabd:	imul   $0x840,%rcx,%r13
   8dac4:	xor    %r8d,%r8d
   8dac7:	lea    0x1c0(%rsp,%r13,1),%rcx
   8dacf:	lea    0x5c0(%rsp,%r13,1),%r11
   8dad7:	mov    %rcx,0x18(%rsp)
   8dadc:	mov    0x38(%rsp),%rcx
   8dae1:	mov    %r11,0x70(%rsp)
   8dae6:	mov    (%rcx),%rsi
   8dae9:	mov    0x8(%rcx),%rdx
   8daed:	cmp    $0x4,%edi
   8daf0:	jb     8de09 <refine_pass_b_64_ordinary+0x1589>
   8daf6:	mov    0xd0(%rsp),%rdi
   8dafe:	mov    0x58(%rsp),%rcx
   8db03:	mov    %r13,0x98(%rsp)
   8db0b:	mov    %rdx,0xe0(%rsp)
   8db13:	mov    %r9,0xd8(%rsp)
   8db1b:	mov    %r10d,0x54(%rsp)
   8db20:	mov    %r10d,%r9d
   8db23:	xor    %r8d,%r8d
   8db26:	lea    (%rdi,%rcx,4),%r13
   8db2a:	mov    %rdx,%rdi
   8db2d:	shl    $0x6,%rdi
   8db31:	mov    $0x1111,%cx
   8db35:	mov    $0xffffffff,%edx
   8db3a:	vpbroadcastq %rdi,%zmm0
   8db40:	mov    %r11,%rdi
   8db43:	data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
   8db50:	mov    %r8,%r14
   8db53:	mov    -0xc(%r13,%r8,4),%r8d
   8db58:	mov    -0x104c(%r13,%r14,4),%r10d
   8db60:	mov    -0x1048(%r13,%r14,4),%r11d
   8db68:	bzhi   %r8d,%edx,%r8d
   8db6d:	kmovd  %r8d,%k1
   8db72:	mov    -0x8(%r13,%r14,4),%r8d
   8db77:	vmovdqu8 (%r12,%r10,1),%ymm1{%k1}{z}
   8db7e:	mov    -0x1044(%r13,%r14,4),%r10d
   8db86:	bzhi   %r8d,%edx,%r8d
   8db8b:	kmovd  %r8d,%k1
   8db90:	mov    -0x4(%r13,%r14,4),%r8d
   8db95:	vmovdqu8 (%r12,%r11,1),%ymm2{%k1}{z}
   8db9c:	bzhi   %r8d,%edx,%r8d
   8dba1:	kmovd  %r8d,%k1
   8dba6:	mov    0x0(%r13,%r14,4),%r8d
   8dbab:	vmovdqu8 (%r12,%r10,1),%ymm3{%k1}{z}
   8dbb2:	mov    -0x1040(%r13,%r14,4),%r10d
   8dbba:	vinserti64x4 $0x1,%ymm2,%zmm1,%zmm1
   8dbc1:	bzhi   %r8d,%edx,%r8d
   8dbc6:	kmovd  %r8d,%k1
   8dbcb:	vmovdqu8 (%r12,%r10,1),%ymm4{%k1}{z}
   8dbd2:	kmovd  %ecx,%k1
   8dbd6:	vinserti64x4 $0x1,%ymm4,%zmm3,%zmm2
   8dbdd:	vmovdqu -0xc(%r13,%r14,4),%xmm3
   8dbe4:	vshufi64x2 $0x88,%zmm2,%zmm1,%zmm4
   8dbeb:	vshufi64x2 $0xdd,%zmm2,%zmm1,%zmm1
   8dbf2:	vpxorq %zmm14,%zmm1,%zmm1
   8dbf8:	vaesenc %zmm9,%zmm1,%zmm1
   8dbfe:	vpexpandd %zmm3,%zmm3{%k1}{z}
   8dc04:	vaesenc %zmm9,%zmm1,%zmm1
   8dc0a:	vpternlogq $0x96,%zmm13,%zmm4,%zmm3
   8dc11:	vaesenc %zmm9,%zmm3,%zmm2
   8dc17:	vaesenc %zmm9,%zmm2,%zmm2
   8dc1d:	vpternlogq $0x96,%zmm8,%zmm2,%zmm1
   8dc24:	vporq  %zmm15,%zmm4,%zmm2
   8dc2a:	vaesenc %zmm9,%zmm1,%zmm1
   8dc30:	vmovdqa64 %zmm2,(%rdi)
   8dc36:	vpsllq $0x6,%zmm1,%zmm1
   8dc3d:	vpandq %zmm0,%zmm1,%zmm1
   8dc43:	vmovq  %xmm1,%r8
   8dc48:	vextracti128 $0x1,%ymm1,%xmm2
   8dc4e:	vmovdqa64 %zmm1,-0x400(%rdi)
   8dc55:	add    $0x40,%rdi
   8dc59:	prefetcht0 (%rsi,%r8,1)
   8dc5e:	vmovq  %xmm2,%r8
   8dc63:	vextracti32x4 $0x2,%zmm1,%xmm2
   8dc6a:	vextracti32x4 $0x3,%zmm1,%xmm1
   8dc71:	prefetcht0 (%rsi,%r8,1)
   8dc76:	vmovq  %xmm2,%r8
   8dc7b:	prefetcht0 (%rsi,%r8,1)
   8dc80:	vmovq  %xmm1,%r8
   8dc85:	prefetcht0 (%rsi,%r8,1)
   8dc8a:	lea    0x4(%r14),%r8
   8dc8e:	add    $0x8,%r14
   8dc92:	cmp    %r9,%r14
   8dc95:	jbe    8db50 <refine_pass_b_64_ordinary+0x12d0>
   8dc9b:	vpbroadcastd 0x4b3c7(%rip),%zmm0        # d906c <_IO_stdin_used+0x6c>
   8dca5:	mov    0x100(%rsp),%rdx
   8dcad:	mov    $0xffff,%ecx
   8dcb2:	bzhi   %r8d,%ecx,%edi
   8dcb7:	kmovd  %edi,%k1
   8dcbb:	vpcmpltud (%rdx),%zmm0,%k0{%k1}
   8dcc2:	kmovw  %k0,%r13d
   8dcc6:	cmp    $0x10,%r8
   8dcca:	jbe    8dd40 <refine_pass_b_64_ordinary+0x14c0>
   8dccc:	mov    0x28(%rsp),%rdx
   8dcd1:	lea    -0x10(%r8),%edi
   8dcd5:	bzhi   %edi,%ecx,%edi
   8dcda:	kmovd  %edi,%k1
   8dcde:	vpcmpltud 0x240(%rdx),%zmm0,%k0{%k1}
   8dce6:	kmovd  %k0,%edi
   8dcea:	shl    $0x10,%edi
   8dced:	or     %rdi,%r13
   8dcf0:	cmp    $0x21,%r8
   8dcf4:	jb     8dd40 <refine_pass_b_64_ordinary+0x14c0>
   8dcf6:	lea    -0x20(%r8),%edi
   8dcfa:	bzhi   %edi,%ecx,%edi
   8dcff:	kmovd  %edi,%k1
   8dd03:	vpcmpltud 0x280(%rdx),%zmm0,%k0{%k1}
   8dd0b:	kmovw  %k0,%edi
   8dd0f:	shl    $0x20,%rdi
   8dd13:	or     %rdi,%r13
   8dd16:	cmp    $0x31,%r8
   8dd1a:	jb     8dd40 <refine_pass_b_64_ordinary+0x14c0>
   8dd1c:	lea    -0x30(%r8),%edi
   8dd20:	bzhi   %edi,%ecx,%edi
   8dd25:	kmovd  %edi,%k1
   8dd29:	vpcmpltud 0x2c0(%rdx),%zmm0,%k0{%k1}
   8dd31:	kmovd  %k0,%edi
   8dd35:	movzwl %di,%edi
   8dd38:	shl    $0x30,%rdi
   8dd3c:	or     %rdi,%r13
   8dd3f:	nop
   8dd40:	mov    0xe0(%rsp),%rdx
   8dd48:	mov    0xd8(%rsp),%rcx
   8dd50:	mov    0x100(%rsp),%r14
   8dd58:	test   %r13,%r13
   8dd5b:	je     8ddf9 <refine_pass_b_64_ordinary+0x1579>
   8dd61:	data16 data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
   8dd70:	tzcnt  %r13,%rdi
   8dd75:	mov    $0xffffffffffffffff,%r11
   8dd7c:	lea    0x0(,%rdi,4),%r9d
   8dd84:	shl    $0x4,%edi
   8dd87:	mov    (%r14,%r9,1),%r10d
   8dd8b:	mov    (%rcx,%r9,1),%r9d
   8dd8f:	bzhi   %r10,%r11,%r11
   8dd94:	vmovd  %r10d,%xmm1
   8dd99:	mov    0x18(%rsp),%r10
   8dd9e:	kmovq  %r11,%k1
   8dda3:	vmovdqu8 (%r12,%r9,1),%zmm0{%k1}{z}
   8ddaa:	vpternlogq $0x96,%zmm12,%zmm0,%zmm1
   8ddb1:	vaesenc %zmm9,%zmm1,%zmm0
   8ddb7:	vaesenc %zmm9,%zmm0,%zmm0
   8ddbd:	vextracti64x4 $0x1,%zmm0,%ymm1
   8ddc4:	vpxorq %zmm0,%zmm1,%zmm0
   8ddca:	vextracti128 $0x1,%ymm0,%xmm1
   8ddd0:	vpxor  %xmm0,%xmm1,%xmm0
   8ddd4:	vaesenc %xmm7,%xmm0,%xmm0
   8ddd9:	vmovq  %xmm0,%r9
   8ddde:	and    %rdx,%r9
   8dde1:	shl    $0x6,%r9
   8dde5:	blsr   %r13,%r13
   8ddea:	mov    %r9,(%r10,%rdi,1)
   8ddee:	prefetcht0 (%rsi,%r9,1)
   8ddf3:	jne    8dd70 <refine_pass_b_64_ordinary+0x14f0>
   8ddf9:	mov    0x98(%rsp),%r13
   8de01:	mov    0x54(%rsp),%r10d
   8de06:	mov    %rcx,%r9
   8de09:	cmp    %r10d,%r8d
   8de0c:	jae    8e013 <refine_pass_b_64_ordinary+0x1793>
   8de12:	mov    %rdx,%r11
   8de15:	mov    0xa0(%rsp),%rdx
   8de1d:	mov    %r9,%rcx
   8de20:	mov    %r8d,%edi
   8de23:	mov    %rdi,%r14
   8de26:	mov    %edx,%r9d
   8de29:	sub    %r8d,%r9d
   8de2c:	mov    %r11,%rdx
   8de2f:	test   $0x1,%r9b
   8de33:	je     8ded6 <refine_pass_b_64_ordinary+0x1656>
   8de39:	mov    0x100(%rsp),%r8
   8de41:	mov    %r10d,%r14d
   8de44:	mov    (%rcx,%rdi,4),%r10d
   8de48:	mov    $0xffffffffffffffff,%r9
   8de4f:	lea    0x1c0(%rsp,%r13,1),%rcx
   8de57:	mov    (%r8,%rdi,4),%r8d
   8de5b:	bzhi   %r8,%r9,%r9
   8de60:	vmovd  %r8d,%xmm1
   8de65:	kmovq  %r9,%k1
   8de6a:	mov    %rdi,%r9
   8de6d:	shl    $0x4,%r9
   8de71:	vmovdqu8 (%r12,%r10,1),%zmm0{%k1}{z}
   8de78:	mov    %r14d,%r10d
   8de7b:	mov    0x70(%rsp),%r14
   8de80:	vpternlogq $0x96,%zmm12,%zmm0,%zmm1
   8de87:	vpor   0x5dfe1(%rip),%xmm0,%xmm0        # ebe70 <refine_test_report_fault.suffix+0x1c40>
   8de8f:	vaesenc %zmm9,%zmm1,%zmm1
   8de95:	vaesenc %zmm9,%zmm1,%zmm1
   8de9b:	vextracti64x4 $0x1,%zmm1,%ymm2
   8dea2:	vpxorq %zmm1,%zmm2,%zmm1
   8dea8:	vextracti128 $0x1,%ymm1,%xmm2
   8deae:	vpxor  %xmm1,%xmm2,%xmm1
   8deb2:	vaesenc %xmm7,%xmm1,%xmm1
   8deb7:	vmovq  %xmm1,%r8
   8debc:	and    %rdx,%r8
   8debf:	shl    $0x6,%r8
   8dec3:	mov    %r8,(%rcx,%r9,1)
   8dec7:	vmovdqa %xmm0,(%r14,%r9,1)
   8decd:	prefetcht0 (%rsi,%r8,1)
   8ded2:	lea    0x1(%rdi),%r14
   8ded6:	mov    0xa0(%rsp),%rcx
   8dede:	dec    %rcx
   8dee1:	cmp    %rdi,%rcx
   8dee4:	mov    $0xffffffffffffffff,%rcx
   8deeb:	je     8e013 <refine_pass_b_64_ordinary+0x1793>
   8def1:	mov    0xc8(%rsp),%rdi
   8def9:	mov    0x58(%rsp),%rdx
   8defe:	mov    %r10d,%r8d
   8df01:	mov    %r14,%r10
   8df04:	shl    $0x4,%r10
   8df08:	lea    (%rdi,%rdx,4),%r9
   8df0c:	mov    %r11,%rdx
   8df0f:	lea    0x5d0(%rsp),%r11
   8df17:	add    %r11,%r10
   8df1a:	add    %r13,%r10
   8df1d:	nopl   (%rax)
   8df20:	mov    -0x4(%r9,%r14,4),%edi
   8df25:	mov    -0x1044(%r9,%r14,4),%r13d
   8df2d:	bzhi   %rdi,%rcx,%r11
   8df32:	vmovd  %edi,%xmm1
   8df36:	kmovq  %r11,%k1
   8df3b:	vmovdqu8 (%r12,%r13,1),%zmm0{%k1}{z}
   8df42:	vpternlogq $0x96,%zmm0,%zmm10,%zmm1
   8df49:	vpor   %xmm0,%xmm11,%xmm0
   8df4d:	vaesenc %zmm9,%zmm1,%zmm1
   8df53:	vaesenc %zmm9,%zmm1,%zmm1
   8df59:	vextracti64x4 $0x1,%zmm1,%ymm2
   8df60:	vpxorq %zmm1,%zmm2,%zmm1
   8df66:	vextracti128 $0x1,%ymm1,%xmm2
   8df6c:	vpxor  %xmm1,%xmm2,%xmm1
   8df70:	vaesenc %xmm7,%xmm1,%xmm1
   8df75:	vmovq  %xmm1,%rdi
   8df7a:	and    %rdx,%rdi
   8df7d:	shl    $0x6,%rdi
   8df81:	mov    %rdi,-0x410(%r10)
   8df88:	vmovdqa %xmm0,-0x10(%r10)
   8df8e:	prefetcht0 (%rsi,%rdi,1)
   8df92:	mov    (%r9,%r14,4),%edi
   8df96:	mov    -0x1040(%r9,%r14,4),%r13d
   8df9e:	add    $0x2,%r14
   8dfa2:	bzhi   %rdi,%rcx,%r11
   8dfa7:	vmovd  %edi,%xmm1
   8dfab:	kmovq  %r11,%k1
   8dfb0:	vmovdqu8 (%r12,%r13,1),%zmm0{%k1}{z}
   8dfb7:	vpternlogq $0x96,%zmm0,%zmm10,%zmm1
   8dfbe:	vpor   %xmm0,%xmm11,%xmm0
   8dfc2:	vaesenc %zmm9,%zmm1,%zmm1
   8dfc8:	vaesenc %zmm9,%zmm1,%zmm1
   8dfce:	vextracti64x4 $0x1,%zmm1,%ymm2
   8dfd5:	vpxorq %zmm1,%zmm2,%zmm1
   8dfdb:	vextracti128 $0x1,%ymm1,%xmm2
   8dfe1:	vpxor  %xmm1,%xmm2,%xmm1
   8dfe5:	vaesenc %xmm7,%xmm1,%xmm1
   8dfea:	vmovq  %xmm1,%rdi
   8dfef:	and    %rdx,%rdi
   8dff2:	shl    $0x6,%rdi
   8dff6:	mov    %rdi,-0x400(%r10)
   8dffd:	vmovdqa %xmm0,(%r10)
   8e002:	prefetcht0 (%rsi,%rdi,1)
   8e006:	add    $0x20,%r10
   8e00a:	cmp    %r8,%r14
   8e00d:	jb     8df20 <refine_pass_b_64_ordinary+0x16a0>
   8e013:	mov    0x18(%rsp),%rcx
   8e018:	mov    0x48(%rsp),%r9
   8e01d:	mov    0x28(%rsp),%r10
   8e022:	mov    0x30(%rsp),%r11
   8e027:	mov    0x60(%rsp),%r14
   8e02c:	mov    $0x1111,%r13w
   8e031:	mov    $0xffffffffffffffff,%r8
   8e038:	mov    %rdx,0x800(%rcx)
   8e03f:	movb   $0x0,0x808(%rcx)
   8e046:	test   %rax,%rax
   8e049:	je     8e0b0 <refine_pass_b_64_ordinary+0x1830>
   8e04b:	nopl   0x0(%rax,%rax,1)
   8e050:	tzcnt  %rax,%rcx
   8e055:	lea    0x0(,%rcx,4),%edx
   8e05c:	mov    (%r10,%rdx,1),%esi
   8e060:	bzhi   %rsi,%r8,%rdi
   8e065:	inc    %esi
   8e067:	kmovq  %rdi,%k1
   8e06c:	mov    (%r9,%rdx,1),%edi
   8e070:	mov    (%r11,%rdx,1),%edx
   8e074:	bzhi   %rsi,%r8,%rsi
   8e079:	mov    (%r14,%rdx,8),%edx
   8e07d:	vmovdqu8 (%r12,%rdi,1),%zmm0{%k1}{z}
   8e084:	kmovq  %rsi,%k1
   8e089:	vmovdqu8 (%r15,%rdx,1),%zmm1{%k1}{z}
   8e090:	xor    %edx,%edx
   8e092:	vpcmpneqb %zmm1,%zmm0,%k0
   8e099:	ktestq %k1,%k0
   8e09e:	setne  %dl
   8e0a1:	shlx   %rcx,%rdx,%rcx
   8e0a6:	or     %rcx,%rbx
   8e0a9:	blsr   %rax,%rax
   8e0ae:	jne    8e050 <refine_pass_b_64_ordinary+0x17d0>
   8e0b0:	mov    0x38(%rsp),%rdi
   8e0b5:	test   %rbx,%rbx
   8e0b8:	je     8e4c0 <refine_pass_b_64_ordinary+0x1c40>
   8e0be:	xchg   %ax,%ax
   8e0c0:	tzcnt  %rbx,%r15
   8e0c5:	mov    0x20(%rsp),%rax
   8e0ca:	mov    (%r9,%r15,4),%ecx
   8e0ce:	bt     %r15,%rax
   8e0d2:	jae    8e140 <refine_pass_b_64_ordinary+0x18c0>
   8e0d4:	mov    (%r10,%r15,4),%r8d
   8e0d8:	mov    0xf8(%rsp),%rdx
   8e0e0:	mov    0x94(%rsp),%r9d
   8e0e8:	mov    %r12,%rsi
   8e0eb:	vzeroupper
   8e0ee:	call   933f0 <refine_intern_identifier>
   8e0f3:	vmovdqa64 0x140(%rsp),%zmm8
   8e0fb:	vpbroadcastd 0x4af53(%rip),%zmm6        # d9058 <_IO_stdin_used+0x58>
   8e105:	vbroadcasti32x4 0x5dd61(%rip),%zmm15        # ebe70 <refine_test_report_fault.suffix+0x1c40>
   8e10f:	vbroadcasti32x4 0x5dd77(%rip),%zmm14        # ebe90 <refine_test_report_fault.suffix+0x1c60>
   8e119:	vbroadcasti32x4 0x5dd5d(%rip),%zmm13        # ebe80 <refine_test_report_fault.suffix+0x1c50>
   8e123:	vpmovsxbd 0x4af34(%rip),%xmm11        # d9060 <_IO_stdin_used+0x60>
   8e12c:	vmovdqa64 0x55e8a(%rip),%zmm10        # e3fc0 <_IO_stdin_used+0xafc0>
   8e136:	jmp    8e420 <refine_pass_b_64_ordinary+0x1ba0>
   8e13b:	nopl   0x0(%rax,%rax,1)
   8e140:	mov    (%r10,%r15,4),%esi
   8e144:	mov    0x8(%rdi),%r14
   8e148:	mov    $0xffffffffffffffff,%rdx
   8e14f:	bzhi   %rsi,%r8,%rax
   8e154:	kmovq  %rax,%k1
   8e159:	vmovdqu8 (%r12,%rcx,1),%zmm3{%k1}{z}
   8e160:	mov    0x40(%rsp),%rcx
   8e165:	cmp    0x800(%rcx),%r14
   8e16c:	jne    8e17e <refine_pass_b_64_ordinary+0x18fe>
   8e16e:	mov    %r15d,%eax
   8e171:	shl    $0x4,%eax
   8e174:	mov    (%rcx,%rax,1),%r13
   8e178:	shr    $0x6,%r13
   8e17c:	jmp    8e1c5 <refine_pass_b_64_ordinary+0x1945>
   8e17e:	vmovd  %esi,%xmm0
   8e182:	vpternlogq $0x96,0x55e33(%rip),%zmm3,%zmm0        # e3fc0 <_IO_stdin_used+0xafc0>
   8e18d:	vpxor  %xmm1,%xmm1,%xmm1
   8e191:	vaesenc %zmm1,%zmm0,%zmm0
   8e197:	vaesenc %zmm1,%zmm0,%zmm0
   8e19d:	vextracti64x4 $0x1,%zmm0,%ymm1
   8e1a4:	vpxorq %zmm0,%zmm1,%zmm0
   8e1aa:	vextracti128 $0x1,%ymm0,%xmm1
   8e1b0:	vpxor  %xmm0,%xmm1,%xmm0
   8e1b4:	vpxor  %xmm1,%xmm1,%xmm1
   8e1b8:	vaesenc %xmm1,%xmm0,%xmm0
   8e1bd:	vmovq  %xmm0,%r13
   8e1c2:	and    %r14,%r13
   8e1c5:	mov    (%rdi),%rcx
   8e1c8:	lea    0x1(%rsi),%eax
   8e1cb:	vshufi64x2 $0x0,%zmm3,%zmm3,%zmm0
   8e1d2:	mov    %rsi,0x18(%rsp)
   8e1d7:	bzhi   %rax,%rdx,%rax
   8e1dc:	mov    %r13,%rdx
   8e1df:	kmovq  %rax,%k1
   8e1e4:	xor    %eax,%eax
   8e1e6:	cmp    $0xc,%esi
   8e1e9:	jb     8e47f <refine_pass_b_64_ordinary+0x1bff>
   8e1ef:	nop
   8e1f0:	mov    %rdx,%rsi
   8e1f3:	shl    $0x6,%rsi
   8e1f7:	vmovdqa64 (%rcx,%rsi,1),%zmm1
   8e1fe:	vpcmpeqd %zmm0,%zmm1,%k0
   8e204:	kmovd  %k0,%edi
   8e208:	and    $0x7777,%edi
   8e20e:	add    $0x1111,%edi
   8e214:	and    $0x8888,%di
   8e219:	je     8e270 <refine_pass_b_64_ordinary+0x19f0>
   8e21b:	mov    0x38(%rsp),%r10
   8e220:	add    %rcx,%rsi
   8e223:	movzwl %di,%r8d
   8e227:	mov    0x10(%r10),%r9
   8e22b:	mov    0x40(%r10),%r10
   8e22f:	nop
   8e230:	mov    %eax,%r11d
   8e233:	tzcnt  %r8d,%eax
   8e238:	mov    (%rsi,%rax,4),%eax
   8e23b:	mov    (%r9,%rax,8),%edi
   8e23f:	vmovdqu8 (%r10,%rdi,1),%zmm2{%k1}{z}
   8e246:	vpcmpneqb %zmm2,%zmm3,%k0
   8e24d:	ktestq %k1,%k0
   8e252:	sete   %dil
   8e256:	cmovne %r11d,%eax
   8e25a:	je     8e272 <refine_pass_b_64_ordinary+0x19f2>
   8e25c:	blsr   %r8d,%r8d
   8e261:	jne    8e230 <refine_pass_b_64_ordinary+0x19b0>
   8e263:	jmp    8e272 <refine_pass_b_64_ordinary+0x19f2>
   8e265:	data16 cs nopw 0x0(%rax,%rax,1)
   8e270:	xor    %edi,%edi
   8e272:	vptestnmd %zmm1,%zmm1,%k0
   8e278:	kmovd  %k0,%esi
   8e27c:	test   $0x8888,%esi
   8e282:	jne    8e293 <refine_pass_b_64_ordinary+0x1a13>
   8e284:	inc    %rdx
   8e287:	and    %r14,%rdx
   8e28a:	test   %dil,%dil
   8e28d:	je     8e1f0 <refine_pass_b_64_ordinary+0x1970>
   8e293:	test   %eax,%eax
   8e295:	jne    8e420 <refine_pass_b_64_ordinary+0x1ba0>
   8e29b:	mov    0x38(%rsp),%rsi
   8e2a0:	mov    0x68(%rsi),%eax
   8e2a3:	cmp    $0xfffffff,%eax
   8e2a8:	ja     8e5b8 <refine_pass_b_64_ordinary+0x1d38>
   8e2ae:	cmp    0x18(%rsi),%eax
   8e2b1:	jb     8e32e <refine_pass_b_64_ordinary+0x1aae>
   8e2b3:	mov    0x38(%rsp),%rdi
   8e2b8:	vmovdqa64 %zmm3,0x100(%rsp)
   8e2c0:	kmovq  %k1,0x60(%rsp)
   8e2c7:	vzeroupper
   8e2ca:	call   354d0 <intern_grow>
   8e2cf:	vmovdqa64 0x100(%rsp),%zmm3
   8e2d7:	vmovdqa64 0x140(%rsp),%zmm8
   8e2df:	vpbroadcastd 0x4ad6f(%rip),%zmm6        # d9058 <_IO_stdin_used+0x58>
   8e2e9:	vbroadcasti32x4 0x5db7d(%rip),%zmm15        # ebe70 <refine_test_report_fault.suffix+0x1c40>
   8e2f3:	vbroadcasti32x4 0x5db93(%rip),%zmm14        # ebe90 <refine_test_report_fault.suffix+0x1c60>
   8e2fd:	vbroadcasti32x4 0x5db79(%rip),%zmm13        # ebe80 <refine_test_report_fault.suffix+0x1c50>
   8e307:	vpmovsxbd 0x4ad50(%rip),%xmm11        # d9060 <_IO_stdin_used+0x60>
   8e310:	vmovdqa64 0x55ca6(%rip),%zmm10        # e3fc0 <_IO_stdin_used+0xafc0>
   8e31a:	kmovq  0x60(%rsp),%k1
   8e321:	mov    0x38(%rsp),%rsi
   8e326:	test   %al,%al
   8e328:	je     8e5b8 <refine_pass_b_64_ordinary+0x1d38>
   8e32e:	mov    0x50(%rsi),%rcx
   8e332:	mov    0x58(%rsi),%rax
   8e336:	sub    %rcx,%rax
   8e339:	jb     8e5b8 <refine_pass_b_64_ordinary+0x1d38>
   8e33f:	cmp    0x18(%rsp),%rax
   8e344:	jbe    8e5b8 <refine_pass_b_64_ordinary+0x1d38>
   8e34a:	mov    0x18(%rsp),%rax
   8e34f:	lea    0x1(%rax,%rcx,1),%rax
   8e354:	add    0x48(%rsi),%rcx
   8e358:	mov    %rax,0x50(%rsi)
   8e35c:	je     8e5b8 <refine_pass_b_64_ordinary+0x1d38>
   8e362:	mov    0x68(%rsi),%eax
   8e365:	vmovdqu8 %zmm3,(%rcx){%k1}
   8e36b:	mov    0x18(%rsp),%rdi
   8e370:	mov    0x10(%rsi),%rdx
   8e374:	sub    0x40(%rsi),%ecx
   8e377:	mov    %ecx,(%rdx,%rax,8)
   8e37a:	mov    %edi,0x4(%rdx,%rax,8)
   8e37e:	incl   0x68(%rsi)
   8e381:	mov    0x8(%rsi),%rcx
   8e385:	cmp    %r14,%rcx
   8e388:	je     8e3ce <refine_pass_b_64_ordinary+0x1b4e>
   8e38a:	vmovd  %edi,%xmm0
   8e38e:	vpternlogq $0x96,0x55c27(%rip),%zmm3,%zmm0        # e3fc0 <_IO_stdin_used+0xafc0>
   8e399:	vpxor  %xmm1,%xmm1,%xmm1
   8e39d:	vaesenc %zmm1,%zmm0,%zmm0
   8e3a3:	vaesenc %zmm1,%zmm0,%zmm0
   8e3a9:	vextracti64x4 $0x1,%zmm0,%ymm1
   8e3b0:	vpxorq %zmm0,%zmm1,%zmm0
   8e3b6:	vextracti128 $0x1,%ymm0,%xmm1
   8e3bc:	vpxor  %xmm0,%xmm1,%xmm0
   8e3c0:	vpxor  %xmm1,%xmm1,%xmm1
   8e3c4:	vaesenc %xmm1,%xmm0,%xmm0
   8e3c9:	vmovq  %xmm0,%r13
   8e3ce:	mov    (%rsi),%rdx
   8e3d1:	data16 data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
   8e3e0:	and    %rcx,%r13
   8e3e3:	mov    %r13,%rsi
   8e3e6:	shl    $0x6,%rsi
   8e3ea:	inc    %r13
   8e3ed:	vmovdqa64 (%rdx,%rsi,1),%zmm0
   8e3f4:	vptestnmd %zmm0,%zmm0,%k0
   8e3fa:	kmovd  %k0,%edi
   8e3fe:	and    $0x8888,%di
   8e403:	je     8e3e0 <refine_pass_b_64_ordinary+0x1b60>
   8e405:	tzcnt  %edi,%ecx
   8e409:	add    %rsi,%rdx
   8e40c:	vpinsrd $0x3,%eax,%xmm3,%xmm0
   8e412:	lea    -0xc(,%rcx,4),%ecx
   8e419:	vmovdqu %xmm0,(%rcx,%rdx,1)
   8e41e:	xchg   %ax,%ax
   8e420:	mov    0x30(%rsp),%rcx
   8e425:	test   %eax,%eax
   8e427:	mov    %eax,(%rcx,%r15,4)
   8e42b:	setne  %al
   8e42e:	je     8e4d0 <refine_pass_b_64_ordinary+0x1c50>
   8e434:	mov    0x38(%rsp),%rdi
   8e439:	mov    0x48(%rsp),%r9
   8e43e:	mov    0x28(%rsp),%r10
   8e443:	blsr   %rbx,%rbx
   8e448:	mov    $0x1111,%r13w
   8e44d:	mov    $0xffffffffffffffff,%r8
   8e454:	jne    8e0c0 <refine_pass_b_64_ordinary+0x1840>
   8e45a:	jmp    8e4d5 <refine_pass_b_64_ordinary+0x1c55>
   8e45f:	nop
   8e460:	vptestnmd %zmm1,%zmm1,%k0
   8e466:	inc    %rdx
   8e469:	kmovd  %k0,%edi
   8e46d:	and    %r14,%rdx
   8e470:	and    $0x8888,%edi
   8e476:	or     %di,%si
   8e479:	jne    8e293 <refine_pass_b_64_ordinary+0x1a13>
   8e47f:	mov    %rdx,%rdi
   8e482:	shl    $0x6,%rdi
   8e486:	vmovdqa64 (%rcx,%rdi,1),%zmm1
   8e48d:	vpcmpeqd %zmm0,%zmm1,%k0
   8e493:	kmovd  %k0,%esi
   8e497:	and    $0x7777,%esi
   8e49d:	add    $0x1111,%esi
   8e4a3:	and    $0x8888,%si
   8e4a8:	je     8e460 <refine_pass_b_64_ordinary+0x1be0>
   8e4aa:	add    %rcx,%rdi
   8e4ad:	tzcnt  %esi,%eax
   8e4b1:	mov    (%rdi,%rax,4),%eax
   8e4b4:	jmp    8e460 <refine_pass_b_64_ordinary+0x1be0>
   8e4b6:	cs nopw 0x0(%rax,%rax,1)
   8e4c0:	mov    $0x1,%al
   8e4c2:	jmp    8e4d5 <refine_pass_b_64_ordinary+0x1c55>
   8e4c4:	data16 data16 cs nopw 0x0(%rax,%rax,1)
   8e4d0:	mov    $0x1111,%r13w
   8e4d5:	mov    0xb0(%rsp),%rcx
   8e4dd:	vpxor  %xmm7,%xmm7,%xmm7
   8e4e1:	vpxor  %xmm9,%xmm9,%xmm9
   8e4e6:	lea    0x1(%rcx),%edx
   8e4e9:	cmp    $0x2,%rcx
   8e4ed:	mov    $0x0,%ecx
   8e4f2:	cmove  %ecx,%edx
   8e4f5:	test   %al,%al
   8e4f7:	je     8e5c7 <refine_pass_b_64_ordinary+0x1d47>
   8e4fd:	vmovdqa64 0x55ab9(%rip),%zmm12        # e3fc0 <_IO_stdin_used+0xafc0>
   8e507:	mov    0x68(%rsp),%rdi
   8e50c:	mov    0x8c(%rsp),%esi
   8e513:	mov    0x90(%rsp),%ecx
   8e51a:	mov    0x80(%rsp),%r9
   8e522:	add    $0x40,%edi
   8e525:	inc    %esi
   8e527:	add    $0xffffffc0,%ecx
   8e52a:	cmp    %r9d,%edi
   8e52d:	jb     8cf8e <refine_pass_b_64_ordinary+0x70e>
   8e533:	jmp    8e5c7 <refine_pass_b_64_ordinary+0x1d47>
   8e538:	mov    0x48(%rsp),%rsi
   8e53d:	mov    0x28(%rsp),%rdx
   8e542:	mov    %r8,0x20(%rsp)
   8e547:	mov    %r12,%rdi
   8e54a:	xor    %r8d,%r8d
   8e54d:	vzeroupper
   8e550:	call   92fd0 <refine_batch_specials>
   8e555:	vmovdqa64 0x140(%rsp),%zmm8
   8e55d:	vpbroadcastd 0x4aaf1(%rip),%zmm6        # d9058 <_IO_stdin_used+0x58>
   8e567:	vbroadcasti32x4 0x5d8ff(%rip),%zmm15        # ebe70 <refine_test_report_fault.suffix+0x1c40>
   8e571:	vbroadcasti32x4 0x5d915(%rip),%zmm14        # ebe90 <refine_test_report_fault.suffix+0x1c60>
   8e57b:	vbroadcasti32x4 0x5d8fb(%rip),%zmm13        # ebe80 <refine_test_report_fault.suffix+0x1c50>
   8e585:	vmovdqa64 0x55a31(%rip),%zmm12        # e3fc0 <_IO_stdin_used+0xafc0>
   8e58f:	vpmovsxbd 0x4aac8(%rip),%xmm11        # d9060 <_IO_stdin_used+0x60>
   8e598:	vmovdqa64 0x55a1e(%rip),%zmm10        # e3fc0 <_IO_stdin_used+0xafc0>
   8e5a2:	mov    0x20(%rsp),%r8
   8e5a7:	vpxor  %xmm9,%xmm9,%xmm9
   8e5ac:	vpxor  %xmm7,%xmm7,%xmm7
   8e5b0:	or     %rax,%r8
   8e5b3:	jmp    8da22 <refine_pass_b_64_ordinary+0x11a2>
   8e5b8:	mov    0x30(%rsp),%rax
   8e5bd:	movl   $0x0,(%rax,%r15,4)
   8e5c5:	xor    %eax,%eax
   8e5c7:	mov    %fs:0x28,%rcx
   8e5d0:	cmp    0x1a80(%rsp),%rcx
   8e5d8:	jne    8e601 <refine_pass_b_64_ordinary+0x1d81>
   8e5da:	lea    -0x28(%rbp),%rsp
   8e5de:	pop    %rbx
   8e5df:	pop    %r12
   8e5e1:	pop    %r13
   8e5e3:	pop    %r14
   8e5e5:	pop    %r15
   8e5e7:	pop    %rbp
   8e5e8:	vzeroupper
   8e5eb:	ret
   8e5ec:	mov    $0x1,%al
   8e5ee:	mov    %fs:0x28,%rcx
   8e5f7:	cmp    0x1a80(%rsp),%rcx
   8e5ff:	je     8e5da <refine_pass_b_64_ordinary+0x1d5a>
   8e601:	vzeroupper
   8e604:	call   13150 <__stack_chk_fail@plt>

Disassembly of section .fini:
