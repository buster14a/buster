// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C's inline assembly (#147, c_check_program_asm.c), lowered by both of the checker's paths and run natively against
// clang -O0's build of the same source (check_test_program_source's harness): every shape the lowering places, and
// each refusal by the feature it names. Include after c_check_program_test.c's harness.
#ifndef BUSTER_C_CHECK_PROGRAM_ASM_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_ASM_TEST_INCLUDED

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
    // XCR0, and rsp's alignment at the template (16, as clang -O0's function bodies have it), and basic asm statements.
    check_test_program_source(test,
        "int tested(int n){unsigned lo,hi;__asm__ volatile(\"xgetbv\" : \"=a\"(lo),\"=d\"(hi) : \"c\"(0));long sp;"
        "__asm__ volatile(\"movq %%rsp, %0\" : \"=r\"(sp));__asm__(\"nop\");__asm__ volatile(\"nop\\n\\tnop\");return (int)(lo&0xff)+(int)hi+(int)(sp&15)+n;}",
        "asm-xgetbv-and-alignment", clang);
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
    // What the lowering refuses, by name.
    check_test_program_boundary_reason(test, "int tested(int n){long r;__asm__(\"movq %1, %0\" : \"=r\"(r) : \"Z\"(0x100000000ull));return (int)r+n;}",
                                       "asm-z-range", true, false, 0, 0, "inline assembly constants outside their constraint's range");
    check_test_program_boundary_reason(test, "int tested(int n){int r;__asm__(\"movl %1, %0\" : \"=r\"(r) : \"N\"((unsigned char)255));return r+n;}",
                                       "asm-n-range", true, false, 0, 0, "inline assembly constants outside their constraint's range");
    check_test_program_boundary_reason(test, "int tested(int n){_Bool z;__asm__(\"testl %1, %1\" : \"=@ccz\"(z) : \"r\"(n));return z;}",
                                       "asm-flag-output", true, false, 0, 0, "inline assembly flag outputs");
    check_test_program_boundary_reason(test, "int tested(int n){double d=n;__asm__(\"fsqrt\" : \"+t\"(d));return (int)d;}",
                                       "asm-x87-operand", true, false, 0, 1, "inline assembly operands in x87 registers");
    check_test_program_boundary_reason(test, "__attribute__((naked)) void raw(void){__asm__(\"ret\");}int tested(int n){return n;}",
                                       "asm-naked-function", true, false, 0, 0, "naked functions");
    check_test_program_boundary_reason(test, "int global;int tested(int n){int r;__asm__(\"movl global(%%rip), %0\" : \"=r\"(r));return r+n;}",
                                       "asm-symbol-reference", true, false, 0, 0, "inline assembly that refers to symbols");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__(\".byte 0x90\");return n;}",
                                       "asm-directive", true, false, 0, 0, "inline assembly takes no directive but .intel_syntax and .att_syntax");
    check_test_program_boundary_reason(test, "int tested(int n){register long r __asm__(\"r10\")=n;__asm__(\"\" : \"+r\"(r));return (int)r;}",
                                       "asm-register-variable", true, false, 0, 1, "explicit register variables as inline assembly operands");
}

#endif
