// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C's inline assembly (#147, c_check_program_asm.c), lowered by both of the checker's paths and run natively against
// clang -O0's build of the same source (check_test_program_source's harness): every shape the lowering places, and
// each refusal by the feature it names. Include after c_check_program_test.c's harness.
#ifndef BUSTER_C_CHECK_PROGRAM_ASM_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_ASM_TEST_INCLUDED

#include <cpuid.h>

static void check_test_program_asm(CheckTest* test, const char* clang)
{
    // Templates of no instructions, the compiler barriers: their operands evaluated, and nothing called. A comment with
    // operands substituted into it assembles to nothing too.
    check_test_program_source(test,
        "int tested(int n){int x=n*3;__asm__ volatile(\"\" : : \"r\"(x) : \"memory\");__asm__ volatile(\"\" ::: \"memory\");int y=n;"
        "__asm__(\"\" : \"+r\"(y));__asm__ volatile(\"\" : : \"m\"(x), \"r\"(&y) : \"memory\");__asm__ volatile(\"# %0 %1\" : : \"r\"(x), \"m\"(y));"
        "int t;__asm__(\"\" : \"=r\"(t) : \"0\"(n+5));return x+y*2+t*3;}",
        "asm-barriers", clang);
    // Registers of a class, a matching input, an early clobber, an integer constant, and the size modifiers.
    check_test_program_source(test,
        "int tested(int n){int x=n;__asm__(\"addl $1, %0\" : \"+r\"(x));long a=n,b=2*n,r;__asm__(\"leaq (%1,%2), %0\" : \"=r\"(r) : \"r\"(a), \"r\"(b));"
        "int t;__asm__(\"incl %0\" : \"=r\"(t) : \"0\"(n));int i;__asm__(\"movl %1, %0\\n\\taddl %2, %0\" : \"=&r\"(i) : \"i\"(40), \"r\"(n));"
        "long w=0;__asm__(\"movq %q1, %0\\n\\tmovb %b1, %b0\" : \"=&r\"(w) : \"r\"(n));"
        "return x*7+(int)r*3+t*5+i+(int)(w&0xffff);}",
        "asm-registers-and-modifiers", clang);
    // Operands by name, and g taking a constant.
    check_test_program_source(test,
        "int tested(int n){int r;__asm__(\"movl %[in], %[out]\\n\\tsubl $3, %[out]\" : [out] \"=r\"(r) : [in] \"r\"(n));int s=n;"
        "__asm__(\"addl %[k], %[v]\" : [v] \"+r\"(s) : [k] \"g\"(7));return r*11+s;}",
        "asm-named-operands", clang);
    // Fixed registers: cpuid's, with rbx (callee-saved) among them, and matching inputs.
    check_test_program_source(test,
        "int tested(int n){unsigned a,b,c,d;__asm__ volatile(\"cpuid\" : \"=a\"(a),\"=b\"(b),\"=c\"(c),\"=d\"(d) : \"0\"(0u),\"2\"(0u));"
        "return (int)((b^c^d)&0xffff)+n;}",
        "asm-cpuid-fixed-registers", clang);
    // clang's cpuid.h on x86-64: dialect alternatives, %q1, and rbx swapped with an operand's register; into array elements.
    check_test_program_source(test,
        "#define CPUID(l,s,a,b,c,d) __asm(\"  xchg{q|}  {%%|}rbx,%q1\\n  cpuid\\n  xchg{q|}  {%%|}rbx,%q1\" : \"=a\"(a),\"=r\"(b),\"=c\"(c),\"=d\"(d) : \"0\"(l),\"2\"(s))\n"
        "int tested(int n){unsigned a,b,c,d;CPUID(0,0,a,b,c,d);unsigned w[4];CPUID(1,0,w[0],w[1],w[2],w[3]);"
        "return (int)((a^b^c^d)&0xfff)+(int)(w[0]&0xfff)+n;}",
        "asm-cpuid-header-form", clang);
    // The x87's control and status words and MXCSR through memory operands, outputs and inputs.
    check_test_program_source(test,
        "int tested(int n){unsigned short c,d;__asm__ volatile(\"fnstcw %0\" : \"=m\"(c));d=c;__asm__ volatile(\"fldcw %0\" : : \"m\"(d));"
        "unsigned short s;__asm__ volatile(\"fnclex\\n\\tfnstsw %0\" : \"=m\"(s));unsigned m,k;__asm__ volatile(\"stmxcsr %0\" : \"=m\"(m));k=m;"
        "__asm__ volatile(\"ldmxcsr %0\" : : \"m\"(k));return (int)c+(int)(s&0x3f)+(int)(m&0xffc0)+n;}",
        "asm-control-words", clang);
    // syscall (getpid), with an input sharing its output's fixed register, a + operand, and register clobbers.
    check_test_program_source(test,
        "int tested(int n){long r;__asm__ volatile(\"syscall\" : \"=a\"(r) : \"a\"(39L) : \"rcx\",\"r11\",\"memory\");long q=39;"
        "__asm__ volatile(\"syscall\" : \"+a\"(q) : : \"rcx\",\"r11\",\"memory\");return (r==q)*100+(r>0)+n;}",
        "asm-syscall", clang);
    // Branches to the template's own labels: %='s labels forward, a numeric one backward in a loop.
    check_test_program_source(test,
        "int tested(int n){int r;__asm__(\"testl %1, %1\\n\\tjle .Lneg%=\\n\\tmovl $5, %0\\n\\tjmp .Lend%=\\n.Lneg%=:\\n\\tmovl $7, %0\\n.Lend%=:\" : \"=r\"(r) : \"r\"(n));"
        "int s=0,k=(n&7)+1;__asm__(\"1:\\n\\taddl $2, %0\\n\\tdecl %1\\n\\tjnz 1b\" : \"+r\"(s), \"+r\"(k));return r*100+s+n;}",
        "asm-local-branches", clang);
    // Callee-saved clobbers inside a loop of live values, and a statement expression's asm under a live value.
    check_test_program_source(test,
        "int tested(int n){long a=n,b=n+1,c=n+2,d=n+3,e=n+4,f=n+5,g=n+6;long r;"
        "for(int i=0;i<3;i++){__asm__(\"movq %1, %%rbx\\n\\tmovq %%rbx, %%r12\\n\\tleaq 1(%%r12), %0\" : \"=r\"(r) : \"r\"(a+i) : \"rbx\",\"r12\");a=r-i;}"
        "long t=a+({long u;__asm__(\"movq %1, %0\\n\\tnegq %0\" : \"=r\"(u) : \"r\"(b));u;})+c*d;return (int)(t+e*f+g);}",
        "asm-callee-saved-clobbers", clang);
    // Every callee-saved register but rbp clobbered with values of the template's own, under eight live values, which
    // Tier 0 keeps in callee-saved registers across the call: the helper must restore them.
    check_test_program_source(test,
        "int tested(int n){long a=n,b=n+1,c=n+2,d=n+3,e=n+4,f=n+5,g=n+6,h=n+7;for(int i=0;i<2;i++){"
        "__asm__ volatile(\"movq $-1, %%rbx\\n\\tmovq $-2, %%r12\\n\\tmovq $-3, %%r13\\n\\tmovq $-4, %%r14\\n\\tmovq $-5, %%r15\" : : : "
        "\"rbx\",\"r12\",\"r13\",\"r14\",\"r15\");a+=b;b+=c;c+=d*i;}return (int)(a*3+b*5+c*7+d*11+e*13+f*17+g*19+h*23);}",
        "asm-callee-saved-live", clang);
    // Memory operands: members, elements at a runtime index, a global, and through a pointer.
    check_test_program_source(test,
        "struct S{int a;long b[3];};int g;int tested(int n){struct S s={n,{1,2,3}};int arr[4]={n,n+1,n+2,n+3};"
        "__asm__(\"incl %0\" : \"+m\"(s.a));__asm__(\"addq $5, %0\" : \"+m\"(s.b[1]));__asm__(\"negl %0\" : \"+m\"(arr[n&3]));"
        "__asm__(\"movl %1, %0\" : \"=m\"(g) : \"r\"(n*9));int*p=&arr[1];__asm__(\"subl $2, %0\" : \"+m\"(*p));"
        "return s.a+(int)s.b[1]+arr[0]+arr[1]+arr[2]+arr[3]+g;}",
        "asm-memory-operands", clang);
    // Outputs of every width into globals and a pointer, and the high byte (%h).
    check_test_program_source(test,
        "long counter;unsigned char small;short mid;int tested(int n){__asm__(\"addq %1, %0\" : \"+r\"(counter) : \"r\"((long)n));"
        "__asm__(\"movb %1, %0\" : \"=q\"(small) : \"q\"((unsigned char)(n*3)));__asm__(\"movw %1, %0\\n\\tincw %0\" : \"=r\"(mid) : \"r\"((short)n));"
        "char*p=0;__asm__(\"leaq 16(%1), %0\" : \"=r\"(p) : \"r\"(p));unsigned short v=(unsigned short)(n*257+3);unsigned char hi;"
        "__asm__(\"movb %h1, %0\" : \"=q\"(hi) : \"Q\"(v));return (int)counter+small+mid+(int)(long)p+hi;}",
        "asm-output-widths", clang);
    // rsp's alignment at the template (16, as clang -O0's function bodies have it), and basic asm statements.
    check_test_program_source(test,
        "int tested(int n){long sp;__asm__ volatile(\"movq %%rsp, %0\" : \"=r\"(sp));"
        "__asm__(\"nop\");__asm__ volatile(\"nop\\n\\tnop\");return (int)(sp&15)+n;}",
        "asm-alignment", clang);
    // XGETBV is executable only with XSAVE and OSXSAVE, even on an x86-64 host.
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) && (ecx & (3u << 26)) == (3u << 26))
    {
        check_test_program_source(test,
            "int tested(int n){unsigned lo,hi;__asm__ volatile(\"xgetbv\" : \"=a\"(lo),\"=d\"(hi) : \"c\"(0));"
            "return (int)(lo&0xff)+(int)hi+n;}", "asm-xgetbv", clang);
    }
    else
    {
        printf("  asm-xgetbv: skipped execution (XSAVE or OSXSAVE unavailable)\n");
    }
    // The x87's, MMX's and SSE's state of 512 bytes, saved and restored by fxsave and fxrstor and their 64-bit forms, in
    // aligned arrays, as main.c's test of DAZ saves it: MXCSR's control bits and its mask, which the state holds at bytes
    // 24 and 28, against stmxcsr's.
    check_test_program_source(test,
        "int tested(int n){unsigned char s[512] __attribute__((aligned(16))),t[512] __attribute__((aligned(16)));"
        "__asm__ volatile(\"fxsave %0\" : \"=m\"(s));__asm__ volatile(\"fxrstor %0\" : : \"m\"(s));"
        "__asm__ volatile(\"fxsave64 %0\" : \"=m\"(t));__asm__ volatile(\"fxrstor64 %0\" : : \"m\"(t));"
        "unsigned csr,mask,wide,now;__builtin_memcpy(&csr,s+24,4);__builtin_memcpy(&mask,s+28,4);__builtin_memcpy(&wide,t+24,4);"
        "__asm__ volatile(\"stmxcsr %0\" : \"=m\"(now));return ((csr^now)&0xffc0)==0?(int)(csr&0xffc0)+((wide^now)&0xffc0?1:2)+(int)((mask>>6)&1)*4+n:-1;}",
        "asm-fx-state", clang);
    // XMM operands of double and float.
    check_test_program_source(test,
        "int tested(int n){double d=n*0.5,e=n+0.25;__asm__(\"addpd %1, %0\" : \"+x\"(d) : \"x\"(e));float f=(float)n,g;"
        "__asm__(\"movaps %1, %0\\n\\taddps %1, %0\" : \"=&x\"(g) : \"x\"(f));return (int)(d*4)+(int)(g*2);}",
        "asm-xmm-operands", clang);
    // Operands evaluated in clang's order, each once: the outputs' lvalues (a `+` one's value loaded through its own),
    // then the inputs, in a template of instructions and in one of none.
    check_test_program_source(test,
        "int seq[16];int at;int g1,g2,g3;int*out1(void){seq[at++]=1;return &g1;}int*out2(void){seq[at++]=2;return &g2;}"
        "int*out3(void){seq[at++]=5;return &g3;}int in1(void){seq[at++]=3;return 4;}int in2(void){seq[at++]=4;return 5;}"
        "int in3(void){seq[at++]=6;return 7;}int tested(int n){at=0;g2=n;"
        "__asm__(\"movl %2, %0\\n\\taddl %3, %1\" : \"=r\"(*out1()), \"+r\"(*out2()) : \"r\"(in1()), \"r\"(in2()));int r=g1*100+g2*10;"
        "__asm__ volatile(\"\" : \"=r\"(*out3()) : \"r\"(in3()), \"m\"(*out1()));int code=0;for(int i=0;i<at;i++)code=code*10+seq[i];"
        "return code+r;}",
        "asm-operand-order", clang);
    // rdi and rsi as operands (the block's address comes in rdi, loaded last), constants sign-extended from their types
    // as clang prints them (0xffffffffu is $-1), a memory input after a register output, and an XMM matching input.
    check_test_program_source(test,
        "int tested(int n){long a=n,b=3*n,r;__asm__(\"leaq (%1,%2), %0\" : \"=r\"(r) : \"D\"(a), \"S\"(b));long m,k;"
        "__asm__(\"movq %1, %0\" : \"=r\"(m) : \"i\"(0xffffffffu));__asm__(\"movq %1, %0\" : \"=r\"(k) : \"i\"(-5));long e=n*5,d;"
        "__asm__(\"movq %1, %0\" : \"=r\"(d) : \"m\"(e));float f=(float)n,g=2.5f,h;__asm__(\"addps %1, %0\" : \"=x\"(h) : \"x\"(f), \"0\"(g));"
        "return (int)r+(int)(m>>32)*1000+(int)k*10+(int)d*7+(int)(h*2);}",
        "asm-fixed-registers-and-constants", clang);
    // Matching inputs of other widths than their outputs', in templates of no instructions and of one: a narrower one
    // zero-extended from its own width, as GCC and clang place it ("=r"(long) : "0"(-1) is 4294967295), and a wider one's
    // low bits; the inputs run below 0.
    check_test_program_source(test,
        "int tested(int n){long a;__asm__(\"\" : \"=r\"(a) : \"0\"(n));int b;__asm__(\"\" : \"=r\"(b) : \"0\"((signed char)n));"
        "long c;__asm__(\"\" : \"=r\"(c) : \"0\"((short)(n*3)));unsigned long d;__asm__(\"\" : \"=r\"(d) : \"0\"((unsigned char)(n+200)));"
        "long e;__asm__(\"nop\" : \"=r\"(e) : \"0\"(n));int t;__asm__(\"\" : \"=r\"(t) : \"0\"((long)n*0x100000001L));"
        "return (int)(a>>32)*1000+(int)a+b*3+(int)(c>>16)*7+(int)c+(int)d+(int)(e>>32)*100+t;}",
        "asm-tied-widths", clang);
    // Z's constants, which clang's backend checks by their zero-extended bits and prints sign-extended, as every constant.
    check_test_program_source(test,
        "int tested(int n){long a,b,c,d;__asm__(\"movq %1, %0\" : \"=r\"(a) : \"Z\"(0xffffffffu));__asm__(\"movq %1, %0\" : \"=r\"(b) : \"Z\"(0x80000000u));"
        "__asm__(\"movq %1, %0\" : \"=r\"(c) : \"Z\"((signed char)-1));__asm__(\"movl %1, %k0\" : \"=r\"(d) : \"Z\"((unsigned long)0xffffffff));"
        "return n+(int)(a>>32)*3+(int)(b>>40)*5+(int)c*7+(int)(d>>8);}",
        "asm-z-constants", clang);
    // A constraint of a register and bounded immediates: a constant its letters take is the immediate ("rI"(31), which only
    // %c1's displacement assembles; "cI"(3)), and another goes in the register ("rI"(40); "cI"(40), which %b1 prints %cl;
    // "Kr"(-200)), as GCC and clang place them.
    check_test_program_source(test,
        "int tested(int n){long p=n;int a,b=n,x=n,y=n;__asm__(\"leal %c1(%2), %0\" : \"=r\"(a) : \"rI\"(31), \"r\"(p));"
        "__asm__(\"addl %1, %0\" : \"+r\"(b) : \"rI\"(40));__asm__(\"shll %b1, %0\" : \"+r\"(x) : \"cI\"(3));"
        "__asm__(\"shll %b1, %0\" : \"+r\"(y) : \"cI\"(40));int c=n;__asm__(\"subl %1, %0\" : \"+r\"(c) : \"Kr\"(-200));"
        "return a+b*3+x*5+y+c*7;}",
        "asm-register-or-immediate", clang);
    // Naked functions: their templates, in AT&T syntax, are their whole code, with an integer constant operand.
    check_test_program_source(test,
        "__attribute__((naked)) long add3(long a,long b,long c){__asm__(\"leaq (%rdi,%rsi), %rax\\n\\taddq %rdx, %rax\\n\\tret\");}"
        "__attribute__((naked)) long seven(void){__asm__(\"movl %0, %%eax\\n\\tret\" : : \"i\"(7));}"
        "int tested(int n){return (int)add3(n,2*n,7)+(int)seven()*100;}",
        "asm-naked-att", clang);
    // A label in a naked function's template, a second entry that another function's template takes the address of.
    check_test_program_source(test,
        "static __attribute__((naked,noinline)) long twice(long x){__asm__(\"leaq (%rdi,%rdi), %rax\\n\\tret\\nthrice_entry:\\n\\tleaq (%rdi,%rdi,2), %rax\\n\\tret\\n\");}"
        "static long thrice_address(void){long a;__asm__(\"leaq thrice_entry(%%rip), %0\" : \"=r\"(a));return a;}"
        "int tested(int n){long (*f)(long)=(long (*)(long))thrice_address();return (int)(twice(n)*10+f(n));}",
        "asm-naked-entries", clang);
    // interpreter_native_bridge's shape: a frame of its own, the flags, a string copy, SSE2's movq, the x87's load and
    // store, a call through a register and a numeric label's branch.
    check_test_program_source(test,
        "struct frame{long (*target)(long,double);long argument;double value;long result;double back;char bytes[16];char copy[16];long double ext;};"
        "_Static_assert(__builtin_offsetof(struct frame,bytes)==40,\"bytes\");"
        "_Static_assert(__builtin_offsetof(struct frame,copy)==56,\"copy\");"
        "_Static_assert(__builtin_offsetof(struct frame,ext)==80,\"ext\");"
        "static long target(long a,double d){return a*10+(long)(d*2);}"
        "static __attribute__((naked,noinline)) void bridge(struct frame* f){__asm__("
        "\"pushq %rbp\\n\\tmovq %rsp, %rbp\\n\\tpushq %r12\\n\\tmovq %rdi, %r12\\n\\tandq $-16, %rsp\\n\\tpushfq\\n\\tpopfq\\n\\tcld\\n\\t\""
        "\"leaq 40(%r12), %rsi\\n\\tleaq 56(%r12), %rdi\\n\\tmovq $16, %rcx\\n\\trep movsb\\n\\t\""
        "\"movq 8(%r12), %rdi\\n\\tmovq 16(%r12), %xmm0\\n\\tmovq %xmm0, 32(%r12)\\n\\tfldl 16(%r12)\\n\\tfstpt 80(%r12)\\n\\t\""
        "\"movq 0(%r12), %r11\\n\\tcallq *%r11\\n\\tmovq %rax, 24(%r12)\\n\\tcmpq $0, 8(%r12)\\n\\tje 1f\\n\\tincq 24(%r12)\\n1:\\n\\t\""
        "\"leaq -8(%rbp), %rsp\\n\\tpopq %r12\\n\\tpopq %rbp\\n\\tretq\\n\");}"
        "int tested(int n){struct frame f={target,n,n*0.5,0,0,{0},{0},0};for(int i=0;i<16;i++)f.bytes[i]=(char)(n+i);bridge(&f);"
        "int s=0;for(int i=0;i<16;i++)s+=f.copy[i];return (int)f.result*3+(int)(f.back*4)+s+(int)(f.ext*8);}",
        "asm-naked-bridge", clang);
    // x86_semantics_test_enter's shape, in Intel syntax: statics RIP-relative, a jump through one to another naked
    // function, whose template jumps back to the first one's label. Execute SSE2 moves on every x86-64 host;
    // assemble AVX-512 moves separately so the oracle can run without AVX-512.
    check_test_program_source(test,
        "static __attribute__((used)) long saved_rsp;"
        "static __attribute__((used)) long saved_arg;"
        "static __attribute__((used)) long continuation;"
        "static __attribute__((naked,noinline)) long enter(long x){__asm__(\".intel_syntax noprefix\\n\""
        "\"push rbx\\nmov qword ptr [rip + saved_rsp], rsp\\nmov qword ptr [rip + saved_arg], rdi\\nlea rbx, [rdi + 5]\\n\""
        "\"movdqu xmm0, xmm1\\nmovdqa xmm1, xmm0\\njmp qword ptr [rip + continuation]\\n\""
        "\"enter_return:\\nmov rax, rbx\\nadd rax, qword ptr [rip + saved_arg]\\npop rbx\\nret\\n.att_syntax prefix\\n\");}"
        "static __attribute__((used,naked,noinline)) void vector_moves(void){__asm__(\".intel_syntax noprefix\\n"
        "vmovdqu64 zmm16, zmm17\\nvmovdqa64 zmm17, zmm16\\nvzeroupper\\nret\\n.att_syntax prefix\\n\");}"
        "static __attribute__((used,naked,noinline)) void bounce(void){__asm__(\"addq $100, %rbx\\n\\tjmp enter_return\\n\");}"
        "static long bounce_address(void){long a;__asm__(\"leaq bounce(%%rip), %0\" : \"=r\"(a));return a;}"
        "int tested(int n){continuation=bounce_address();long r=enter(n);return (int)r+(saved_rsp!=0)+(int)(saved_arg-n);}",
        "asm-naked-intel", clang);
    // The original wide-register path also executes where CPU and OS save-state support permit AVX-512F. Its
    // compilation, constraints, lowering, encoding, oracle compilation and link remain required on every host.
    const X86Target native = x86_target_native();
    const bool wide_native = (native.features & BUSTER_X86_FEATURE(AVX512F)) != 0;
    if (!wide_native)
    {
        printf("test-checker: asm-naked-intel-avx512: AVX-512F unavailable; checking compile/link without native execution\n");
    }
    check_test_program_source_native(test,
        "static __attribute__((used)) long saved_rsp;"
        "static __attribute__((used)) long saved_arg;"
        "static __attribute__((used)) long continuation;"
        "static __attribute__((naked,noinline)) long enter(long x){__asm__(\".intel_syntax noprefix\\n\""
        "\"push rbx\\nmov qword ptr [rip + saved_rsp], rsp\\nmov qword ptr [rip + saved_arg], rdi\\nlea rbx, [rdi + 5]\\n\""
        "\"vmovdqu64 zmm16, zmm17\\nvmovdqa64 zmm17, zmm16\\njmp qword ptr [rip + continuation]\\n\""
        "\"enter_return:\\nmov rax, rbx\\nadd rax, qword ptr [rip + saved_arg]\\nvzeroupper\\npop rbx\\nret\\n.att_syntax prefix\\n\");}"
        "static __attribute__((used,naked,noinline)) void bounce(void){__asm__(\"addq $100, %rbx\\n\\tjmp enter_return\\n\");}"
        "static long bounce_address(void){long a;__asm__(\"leaq bounce(%%rip), %0\" : \"=r\"(a));return a;}"
        "int tested(int n){continuation=bounce_address();long r=enter(n);return (int)r+(saved_rsp!=0)+(int)(saved_arg-n);}",
        "asm-naked-intel-avx512", clang, wide_native);
    // Vectors in vector registers' constraints in templates of no instructions, main.c's compiler barriers (c_lexer.c's
    // and x86_64_encoder.c's `__asm__("" : "+v"(x))`): a `+` operand keeps its value, an output through a call's pointer
    // is evaluated once, and an input's value is evaluated once and dropped, a call's that returns a vector and one loaded
    // through a call's pointer in a statement of no outputs, both calls counting their evaluations (#773's review); for
    // x86-64-v4, run where the host has its features.
    X86Target vectors = { 0 };
    const bool vectors_parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &vectors);
    check_test_expect(test, vectors_parsed, "the vector barriers' target, x86-64-v4");
    static const char asm_vector_types[] =
        "typedef long long v8di __attribute__((vector_size(64)));typedef int v8si __attribute__((vector_size(32)));"
        "typedef float v4sf __attribute__((vector_size(16)));";
    static const char asm_vector_program[] =
        "int count;int inputs;v8di saved[3];static v8di* next(void){count+=1;return &saved[count%3];}"
        "static v8di twice(v8di x){inputs+=1;return x+x;}static v8di* held(void){inputs+=10;return &saved[2];}"
        "int tested(int n){v8di a={n,n+1,n+2,n+3,n+4,n+5,n+6,n+7};__asm__(\"\" : \"+v\"(a));v8si b={n*2,1,2,3,4,5,6,7};"
        "__asm__ volatile(\"\" : \"+x\"(b) : \"v\"(twice(a)));__asm__ volatile(\"\" : : \"x\"(*held()));"
        "v4sf c={1.5f,(float)n,2.5f,3.5f};__asm__(\"# no instructions\" : \"+v\"(c) : : \"memory\");"
        "saved[1]=a;__asm__(\"\" : \"+v\"(*next()));long long s=0;for(int i=0;i<8;i++)s+=a[i]*(i+1)+saved[1][i]+b[i];"
        "return (int)(s+(long long)c[1]*5+count*7+inputs*11);}";
    if (vectors_parsed)
    {
        const bool vectors_native = (native.features & vectors.features) == vectors.features;
        if (!vectors_native)
        {
            printf("  asm-vector-barriers: native execution skipped (the host lacks x86-64-v4's features or their saved state)\n");
        }
        char source[2048];
        const int length = snprintf(source, sizeof(source), "%s%s", asm_vector_types, asm_vector_program);
        check_test_expect(test, length > 0 && (size_t)length < sizeof(source), "the vector barriers' source fits its buffer");
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64-v4";
        check_test_program_source_native_limits(test, source, "asm-vector-barriers", clang, false, -1, 0, &vectors, vectors_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
    }
    // A naked function whose attribute isn't on its first declaration (#565's review): an unannotated prototype before a
    // naked definition that returns by its own ret, and a naked redeclaration between a prototype and an unannotated
    // definition, both naked as clang takes them; and a naked redeclaration after a definition, which clang ignores with
    // a warning: an ordinary function.
    check_test_program_source(test,
        "static long first_plain(long);"
        "static __attribute__((naked,noinline)) long first_plain(long x){__asm__(\"leaq 1(%rdi), %rax\\n\\tret\\n\");}"
        "static long middle_naked(long);"
        "static __attribute__((naked)) long middle_naked(long);"
        "static __attribute__((noinline)) long middle_naked(long x){__asm__(\"leaq 2(%rdi), %rax\\n\\tret\\n\");}"
        "static long after_definition(long x){return x+3;}"
        "static __attribute__((naked)) long after_definition(long);"
        "int tested(int n){return (int)(first_plain(n)*7+middle_naked(n)*5+after_definition(n));}",
        "asm-naked-redeclared", clang);
    // Templates that name symbols: the harness's function and data, the unit's static function and data, and a .L label
    // one statement's template defines and another's takes the address of.
    check_test_program_source(test,
        "extern int external_data;int external_helper(int);"
        "static __attribute__((used)) int counter;"
        "static __attribute__((used)) int bump(int x){return x+1;}"
        "int tested(int n){int r,s;long here,there;"
        "__asm__ volatile(\"movl %1, %%edi\\n\\tcall external_helper\\n\\taddl external_data(%%rip), %%eax\\n\\tmovl %%eax, %0\" : \"=r\"(r) : \"r\"(n)"
        ": \"rax\",\"rcx\",\"rdx\",\"rsi\",\"rdi\",\"r8\",\"r9\",\"r10\",\"r11\",\"xmm0\",\"xmm1\",\"xmm2\",\"xmm3\",\"xmm4\",\"xmm5\",\"xmm6\",\"xmm7\",\"xmm8\",\"xmm9\",\"xmm10\",\"xmm11\",\"xmm12\",\"xmm13\",\"xmm14\",\"xmm15\",\"memory\",\"cc\");"
        "__asm__ volatile(\"movl %1, %%edi\\n\\tcall bump\\n\\taddl %%eax, counter(%%rip)\\n\\tmovl counter(%%rip), %0\" : \"=r\"(s) : \"r\"(n)"
        ": \"rax\",\"rcx\",\"rdx\",\"rsi\",\"rdi\",\"r8\",\"r9\",\"r10\",\"r11\",\"xmm0\",\"xmm1\",\"xmm2\",\"xmm3\",\"xmm4\",\"xmm5\",\"xmm6\",\"xmm7\",\"xmm8\",\"xmm9\",\"xmm10\",\"xmm11\",\"xmm12\",\"xmm13\",\"xmm14\",\"xmm15\",\"memory\",\"cc\");"
        "__asm__(\"leaq .Lshared_point(%%rip), %0\\n.Lshared_point:\\n\\tnop\" : \"=r\"(here));"
        "__asm__(\"leaq .Lshared_point(%%rip), %0\" : \"=r\"(there));"
        "return r*7+s+(here==there)+external_helper(0);}",
        "asm-symbol-references", clang);
    // A label inside a helper's template at its exact record: another statement's template calls the snippet it marks,
    // which the first template jumps over.
    check_test_program_source(test,
        "extern int external_data;int external_helper(int);int tested(int n){int v;"
        "__asm__ volatile(\"jmp 1f\\n.Lforty_two:\\n\\tmovl $42, %%eax\\n\\taddl %%edi, %%eax\\n\\tret\\n1:\" ::: \"rax\");"
        "__asm__ volatile(\"movl %1, %%edi\\n\\tcall .Lforty_two\\n\\tmovl %%eax, %0\" : \"=r\"(v) : \"r\"(n)"
        ": \"rax\",\"rcx\",\"rdx\",\"rsi\",\"rdi\",\"r8\",\"r9\",\"r10\",\"r11\",\"memory\",\"cc\");"
        "return v+external_helper(0);}",
        "asm-helper-labels", clang);
    // A naked function's references beside a body's call of a runtime function (__divti3), in a unit of so few symbols
    // that its names' indices fall among the runtime's provisional symbols, which aren't theirs.
    check_test_program_source(test,
        "static __attribute__((naked,noinline)) long pick(long x){__asm__("
        "\"testq %rdi, %rdi\\n\\tjz l0\\n\\tcmpq $1, %rdi\\n\\tje l1\\n\\tcmpq $2, %rdi\\n\\tje l2\\n\\tcmpq $3, %rdi\\n\\tje l3\\n\\tjmp l4\\n\""
        "\"l0:\\n\\tmovq $10, %rax\\n\\tret\\nl1:\\n\\tmovq $11, %rax\\n\\tret\\nl2:\\n\\tmovq $12, %rax\\n\\tret\\nl3:\\n\\tmovq $13, %rax\\n\\tret\\n\""
        "\"l4:\\n\\tleaq l5(%rip), %rax\\n\\tleaq l0(%rip), %rdx\\n\\tsubq %rdx, %rax\\n\\tret\\nl5:\\n\");}"
        "int tested(int n){__int128 w=(__int128)n*1000000007;long q=(long)(w/(__int128)(n|1));return (int)pick(n&7)+(int)(q&0xff);}",
        "asm-naked-runtime", clang);
    // A jump table beside the templates' names, in one unit: the function whose switch takes a table has templates that
    // call the unit's static function, update its static and take the address of a label a naked function's template
    // defines. The names' own symbols come after the tables' and their labels' (CheckProgramLowered.case_symbol_base),
    // and neither's references are the other's to rewrite.
    static const char* const asm_tables =
        "static __attribute__((used)) int bump(int x){return x+1;}"
        "static int total;"
        "static __attribute__((naked,noinline)) int relay(int x){__asm__(\"addl $2, %edi\\nrelay_tail:\\n\\tjmp bump\\n\");}"
        "static int dense(int x){int r;long tail,start;"
        "switch(x){case 0:r=3;break;case 1:r=5;break;case 2:r=7;break;case 3:r=11;break;case 5:r=13;break;default:r=-1;}"
        "__asm__ volatile(\"movl %1, %%edi\\n\\tcall bump\\n\\taddl %%eax, total(%%rip)\\n\\tmovl %%eax, %0\" : \"=r\"(r) : \"r\"(r)"
        ": \"rax\",\"rcx\",\"rdx\",\"rsi\",\"rdi\",\"r8\",\"r9\",\"r10\",\"r11\",\"xmm0\",\"xmm1\",\"xmm2\",\"xmm3\",\"xmm4\",\"xmm5\",\"xmm6\",\"xmm7\",\"xmm8\",\"xmm9\",\"xmm10\",\"xmm11\",\"xmm12\",\"xmm13\",\"xmm14\",\"xmm15\",\"memory\",\"cc\");"
        "__asm__(\"leaq relay_tail(%%rip), %0\" : \"=r\"(tail));"
        "__asm__(\"leaq relay(%%rip), %0\" : \"=r\"(start));"
        "return r+(int)(tail-start)+bump(0);}"
        "int tested(int n){unsigned s=0;for(int i=-3;i<9;i++)s=s*7+(unsigned)dense(i+n)+(unsigned)relay(i);return (int)s+total;}";
    check_test_program_source(test, asm_tables, "asm-names-switch-tables", clang);
    check_test_program_tables(test, asm_tables, "asm-names-switch-tables", 1, 6);
    // Clang takes its flag-output path; buster selects the equivalent setcc fallback until it supports flag outputs.
    check_test_program_source(test,
        "int tested(int n){int z;\n"
        "#ifdef __GCC_ASM_FLAG_OUTPUTS__\n"
        "__asm__(\"testl %1,%1\" : \"=@ccz\"(z) : \"r\"(n) : \"cc\");\n"
        "#else\n"
        "__asm__(\"testl %1,%1; sete %b0; movzbl %b0,%0\" : \"=r\"(z) : \"r\"(n) : \"cc\");\n"
        "#endif\n"
        "return z;}", "asm-flag-output-fallback", clang);
    // What the lowering refuses, by name.
    check_test_program_boundary_reason(test, "int tested(int n){long r;__asm__(\"movq %1, %0\" : \"=r\"(r) : \"Z\"(0x100000000ull));return (int)r+n;}",
                                       "asm-z-range", true, false, 0, 0, "inline assembly constants outside their constraint's range");
    check_test_program_boundary_reason(test, "int tested(int n){int r;__asm__(\"movl %1, %0\" : \"=r\"(r) : \"N\"((unsigned char)255));return r+n;}",
                                       "asm-n-range", true, false, 0, 0, "inline assembly constants outside their constraint's range");
    check_test_program_boundary_reason(test, "int tested(int n){_Bool z;__asm__(\"testl %1, %1\" : \"=@ccz\"(z) : \"r\"(n));return z;}",
                                       "asm-flag-output", true, false, 0, 0, "inline assembly flag outputs");
    check_test_program_boundary_reason(test, "int tested(int n){double d=n;__asm__(\"fsqrt\" : \"+t\"(d));return (int)d;}",
                                       "asm-x87-operand", true, false, 0, 1, "inline assembly operands in x87 registers");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__(\".byte 0x90\");return n;}",
                                       "asm-directive", true, false, 0, 0, "inline assembly takes no directive but .intel_syntax and .att_syntax");
    check_test_program_boundary_reason(test, "int tested(int n){register long r __asm__(\"r10\")=n;__asm__(\"\" : \"+r\"(r));return (int)r;}",
                                       "asm-register-variable", true, false, 0, 1, "explicit register variables as inline assembly operands");
    // #554 rejects ordinary statements and parameter operands before lowering. An uninitialized named-register VLA
    // passes that constraint, but evaluating its bound still gives the naked planner a non-asm control to refuse.
    check_test_program_boundary_reason(test, "__attribute__((naked)) void raw(int n){register int x[n] asm(\"rax\");__asm__(\"ret\");}int tested(int n){return n;}",
                                       "asm-naked-vla-register", true, false, 0, 0, "statements other than asm statements in naked functions");
    // A constant with a register constraint reaches the planner without a forbidden parameter reference.
    check_test_program_boundary_reason(test, "__attribute__((naked)) void raw(void){__asm__(\"ret\" : : \"r\"(1));}int tested(int n){return n;}",
                                       "asm-naked-operand", true, false, 0, 0, "naked functions' inline assembly operands other than integer constants");
    check_test_program_boundary_reason(test, "__attribute__((naked)) void raw(void){}int tested(int n){return n;}",
                                       "asm-naked-empty", true, false, 0, 0, "naked functions of no instructions");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__(\"twice: nop\");__asm__(\"twice: nop\");return n;}",
                                       "asm-label-twice", true, false, 0, 0, "inline assembly labels another template defines too");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__(\"jmp .Lnowhere\");return n;}",
                                       "asm-local-undefined", true, false, 0, 0, "inline assembly's local labels that no template defines");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__(\"lonely:\");return n;}",
                                       "asm-label-no-instructions", true, false, 0, 0, "inline assembly labels in templates of no instructions");
    check_test_program_boundary_reason(test, "int tested(int n){long a;__asm__(\"leaq 1f(%%rip), %0\\n1:\" : \"=r\"(a));return n+(int)a;}",
                                       "asm-numeric-data", true, false, 0, 0, "inline assembly's numeric labels and `.` are only its own jmp's and jcc's targets");
    // A vector in a vector register's constraint where the template has instructions, refers to it, or it is a volatile
    // output (whose load and store clang's code makes), which the helper doesn't place yet.
    static const char* const held[][2] = {
        { "asm-vector-instructions", "int tested(int n){v8di a={n};__asm__(\"vpaddq %0, %0, %0\" : \"+v\"(a));return (int)a[0];}" },
        { "asm-vector-other-instructions", "int tested(int n){v8di a={n};__asm__(\"nop\" : \"+v\"(a));return (int)a[0];}" },
        { "asm-vector-input-instructions", "int tested(int n){v8di a={n};__asm__ volatile(\"nop\" : : \"v\"(a));return (int)a[0];}" },
        { "asm-vector-referred", "int tested(int n){v8di a={n};__asm__(\"# %0\" : \"+v\"(a));return (int)a[0];}" },
        { "asm-vector-volatile", "int tested(int n){volatile v8di a={n};__asm__(\"\" : \"+v\"(a));return (int)a[0];}" },
        { "asm-vector-matched", "int tested(int n){v8di a={n},b;__asm__(\"\" : \"=v\"(b) : \"0\"(a));return (int)b[0];}" },
    };
    for (uint32_t i = 0; vectors_parsed && i < BUSTER_ARRAY_LENGTH(held); i += 1)
    {
        char source[512];
        snprintf(source, sizeof(source), "%s%s", asm_vector_types, held[i][1]);
        const char* reason = i + 1 == BUSTER_ARRAY_LENGTH(held) ? "inline assembly inputs matching an output that isn't a register's of its own" :
                                                                  "inline assembly operands of this type in their constraint's registers";
        check_test_program_boundary_target(test, source, held[i][0], true, false, 0, 1, reason, &vectors);
    }
}

#endif
