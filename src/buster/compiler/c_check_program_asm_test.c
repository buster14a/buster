// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C's inline assembly (#147, c_check_program_asm.c), lowered by both of the checker's paths and run natively against
// clang -O0's build of the same source (check_test_program_source's harness): every shape the lowering places, and
// each refusal by the feature it names. Include after c_check_program_test.c's harness.
#ifndef BUSTER_C_CHECK_PROGRAM_ASM_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_ASM_TEST_INCLUDED

#include <cpuid.h>

// The planner without room (#929's review): a statement of x87 operands and a template of no instructions, one of an
// x87 operand and an instruction, and one refused at its first operand (a flag output's) before its x87 operand, whose
// later places the planner never writes. check_program_asm_plan of them, by both paths, with the output arena's room cut
// to none and then 8 bytes more at a time until the plan succeeds: each run before it must fail for want of room
// (CHECK_PROGRAM_RESOURCE) with both arenas as they were, among them a run whose room ends before each statement's
// places; then the plans must be the whole arena's: a helper each for the first two, and the third refused by name.
static void check_test_program_asm_room(CheckTest* test)
{
    static const char source[] =
        "float through(float x){__asm__ volatile(\"\" : \"+t\"(x));return x;}"
        "long double root(long double x){__asm__(\"fsqrt\" : \"+t\"(x));return x;}"
        "int flagged(int a, float x){int z;__asm__(\"cmpl $0, %2\" : \"=@ccz\"(z), \"+t\"(x) : \"r\"(a));return z+(int)x;}";
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const PredefinesOptions predefines = check_test_program_predefines();
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                        pp_run_source(&pp, "asm-room", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "asm room: source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { .scalar = path == 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0, .predefines = &predefines };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch) && input.asm_count == 3;
        char description[256];
        snprintf(description, sizeof(description), "asm room, %s: the program is ready", path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready, description);
        const uint64_t output_mark = output.length;
        const uint64_t scratch_mark = scratch.length;
        const uint64_t capacity = output.capacity;
        CheckProgramAsms planned = { 0 };
        CheckProgramHelpers helpers = { 0 };
        bool done = false;
        bool kept = true;
        uint32_t failures = 0;
        for (uint64_t room = 0; ready && !done && room <= (UINT64_C(1) << 20); room += 8)
        {
            helpers = (CheckProgramHelpers){ 0 };
            output.capacity = output_mark + room;
            done = check_program_asm_plan(&planned, &helpers, &program, path == 0, &output, &scratch);
            output.capacity = capacity;
            failures += !done;
            kept &= done || (planned.status == CHECK_PROGRAM_RESOURCE && output.length == output_mark && scratch.length == scratch_mark && helpers.count == 0);
        }
        snprintf(description, sizeof(description), "asm room, %s: %u runs without room fail with the arenas as they were, then one plans", path == 0 ? "scalar" : "vector",
                 failures);
        check_test_expect(test, ready && done && kept && failures != 0, description);
        const bool plans_right = done && planned.plans != NULL && planned.plans[0].reason == NULL && planned.plans[0].helper != BUSTER_CHECK_NONE &&
                                 planned.plans[1].reason == NULL && planned.plans[1].helper != BUSTER_CHECK_NONE && planned.plans[2].reason != NULL &&
                                 strcmp(planned.plans[2].reason, "inline assembly flag outputs") == 0 && helpers.count == 2;
        snprintf(description, sizeof(description), "asm room, %s: the x87 statements have helpers, and the flag output is refused by name", path == 0 ? "scalar" : "vector");
        check_test_expect(test, plans_right, description);
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_program_asm(CheckTest* test, const char* clang)
{
    check_test_program_asm_room(test);
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
    check_test_program_source(test,
        "int tested(int n){unsigned lo,hi;__asm__ volatile(\"pause\\n\\tlfence\\n\\tmfence\\n\\tsfence\\n\\trdtsc\" : \"=a\"(lo), \"=d\"(hi) : : \"memory\");"
        "return (lo!=0 || hi!=0)+n;}", "asm-pause-fences-rdtsc", clang);
    check_test_program_source(test,
        "int tested(int n){__asm__ volatile(\"rep nop\\n\\trep; nop\" : : : \"memory\");return n+1;}",
        "asm-rep-nop", clang);
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
    // The semantics harness saves all eight masks at offsets 2240..2296. Compile/link this regression on every
    // host; execute only with the AVX-512BW state support KMOVQ requires, including k0 as an ordinary operand.
    const bool masks_native = wide_native && (native.features & BUSTER_X86_FEATURE(AVX512BW)) != 0;
    if (!masks_native)
    {
        printf("test-checker: asm-naked-kmov: AVX-512BW unavailable; checking compile/link without native execution\n");
    }
    check_test_program_source_native(test,
        "static __attribute__((naked,noinline,target(\"avx512f,avx512bw\"))) void copy_masks(const unsigned long long* p,unsigned long long* q){"
        "__asm__(\".intel_syntax noprefix\\n"
        "kmovq k0,qword ptr [rdi+2240]\\nkmovq k1,qword ptr [rdi+2248]\\nkmovq k2,qword ptr [rdi+2256]\\nkmovq k3,qword ptr [rdi+2264]\\n"
        "kmovq k4,qword ptr [rdi+2272]\\nkmovq k5,qword ptr [rdi+2280]\\nkmovq k6,qword ptr [rdi+2288]\\nkmovq k7,qword ptr [rdi+2296]\\n"
        "kmovq qword ptr [rsi],k0\\nkmovq qword ptr [rsi+8],k1\\nkmovq qword ptr [rsi+16],k2\\nkmovq qword ptr [rsi+24],k3\\n"
        "kmovq qword ptr [rsi+32],k4\\nkmovq qword ptr [rsi+40],k5\\nkmovq qword ptr [rsi+48],k6\\nkmovq qword ptr [rsi+56],k7\\n"
        "ret\\n.att_syntax prefix\\n\");}"
        "int tested(int n){unsigned long long p[288],q[8];for(int i=0;i<8;i++){p[280+i]=((unsigned long long)(unsigned)n<<32)^"
        "(0xfedcba9876543210ULL+(unsigned)i*0x0101010101010101ULL);q[i]=0;}copy_masks(p,q);int r=0;"
        "for(int i=0;i<8;i++)r+=q[i]==p[280+i];return r+n;}",
        "asm-naked-kmov", clang, masks_native);
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
    // The forms of musl's x86_64 atomics (arch/x86_64/atomic_arch.h), in functions of this test's own: lock prefixes as
    // statements of their own before cmpxchg, xadd, and, or, xor, inc and dec on memory outputs (an "=m" one beside
    // an "m" input of its object, and "+m"), xchg, a store fenced by a locked or of the stack, pause, the bit scans
    // (of operands never zero, whose result the scans leave undefined), and a load of the thread pointer through %fs.
    // hlt is compiled, assembled and linked, never run.
    check_test_program_source(test,
        "static int swap_if(volatile int* slot, int expected, int wanted){int seen;__asm__ __volatile__(\"lock ; cmpxchg %3, %1\" : \"=a\"(seen), \"=m\"(*slot) : \"a\"(expected), \"r\"(wanted) : \"memory\");return seen;}"
        "static void* swap_pointer_if(void* volatile* slot, void* expected, void* wanted){__asm__(\"lock ; cmpxchg %3, %1\" : \"=a\"(expected), \"=m\"(*slot) : \"a\"(expected), \"r\"(wanted) : \"memory\");return expected;}"
        "static long swap_long_if(volatile long* slot, long expected, long wanted){__asm__ __volatile__(\"lock ; cmpxchgq %2, %1\" : \"+a\"(expected), \"+m\"(*slot) : \"r\"(wanted) : \"memory\", \"cc\");return expected;}"
        "static int trade(volatile int* slot, int value){int old;__asm__ __volatile__(\"xchg %0, %1\" : \"=r\"(old), \"=m\"(*slot) : \"0\"(value) : \"memory\");return old;}"
        "static int add_returning_old(volatile int* slot, int amount){int old;__asm__ __volatile__(\"lock ; xadd %0, %1\" : \"=r\"(old), \"=m\"(*slot) : \"0\"(amount) : \"memory\");return old;}"
        "static long add_long_returning_old(volatile long* slot, long amount){__asm__ __volatile__(\"lock ; xadd %0, %1\" : \"+r\"(amount), \"+m\"(*slot) : : \"memory\", \"cc\");return amount;}"
        "static void keep_bits(volatile unsigned* slot, unsigned bits){__asm__ __volatile__(\"lock ; and %1, %0\" : \"=m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void set_bits(volatile unsigned* slot, unsigned bits){__asm__ __volatile__(\"lock ; or %1, %0\" : \"=m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void keep_wide_bits(volatile unsigned long* slot, unsigned long bits){__asm__ __volatile(\"lock ; and %1, %0\" : \"=m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void set_wide_bits(volatile unsigned long* slot, unsigned long bits){__asm__ __volatile__(\"lock ; or %1, %0\" : \"+m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void flip_wide_bits(volatile unsigned long* slot, unsigned long bits){__asm__ __volatile__(\"lock ; xor %1, %0\" : \"+m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void count_up(volatile int* slot){__asm__ __volatile__(\"lock ; incl %0\" : \"=m\"(*slot) : \"m\"(*slot) : \"memory\");}"
        "static void count_down(volatile int* slot){__asm__ __volatile__(\"lock ; decl %0\" : \"=m\"(*slot) : \"m\"(*slot) : \"memory\");}"
        "static void count_down_long(volatile long* slot){__asm__ __volatile__(\"lock ; decq %0\" : \"+m\"(*slot) : : \"memory\");}"
        "static void fenced_store(volatile long* slot, long value){__asm__ __volatile__(\"movq %1, %0 ; lock ; orl $0,(%%rsp)\" : \"=m\"(*slot) : \"r\"(value) : \"memory\");}"
        "static void wait_a_little(void){__asm__ __volatile__(\"pause\" : : : \"memory\");}"
        "static int lowest_set(unsigned long bits){__asm__(\"bsf %1,%0\" : \"=r\"(bits) : \"r\"(bits));return (int)bits;}"
        "static int highest_set(unsigned long bits){__asm__(\"bsr %1,%0\" : \"=r\"(bits) : \"r\"(bits));return (int)bits;}"
        "static unsigned long thread_pointer(void){unsigned long here;__asm__(\"mov %%fs:0,%0\" : \"=r\"(here));return here;}"
        "void stop_here(void){__asm__ __volatile__(\"hlt\" : : : \"memory\");}"
        "int tested(int n){volatile int word=n*7;int r=swap_if(&word,n*7,n+3);r=r*5+swap_if(&word,-1,9)+word*3;"
        "void* volatile cell=&r;void* before=swap_pointer_if(&cell,&r,(void*)&word);r+=(before==(void*)&r)*2+(cell==(void*)&word)*4;"
        "volatile long big=(long)n<<36;long previous=swap_long_if(&big,(long)n<<36,-n);r+=(int)(previous>>36)+(int)big;"
        "r=r*3+trade(&word,n-1)+word;r=r*7+add_returning_old(&word,n*5)+word;volatile long total=1000;r+=(int)add_long_returning_old(&total,n)+(int)total;"
        "volatile unsigned mask=0xf0f0u+n;keep_bits(&mask,0x3c3cu);set_bits(&mask,(unsigned)n<<8);"
        "volatile unsigned long flags=(unsigned long)n*0x100000001ul;keep_wide_bits(&flags,0xffff0000fffful);set_wide_bits(&flags,(unsigned long)n<<44);flip_wide_bits(&flags,0x11);"
        "volatile int tally=n;count_up(&tally);count_up(&tally);count_down(&tally);volatile long countdown=n*4;count_down_long(&countdown);"
        "volatile long stored=0;fenced_store(&stored,(long)r*2);wait_a_little();unsigned long probe=((unsigned long)n*2654435761u+1)<<(n&15);"
        "return r+(int)mask*11+(int)(flags>>20)+(int)flags+tally*13+(int)countdown*17+(int)stored+lowest_set(probe)*19+highest_set(probe)*23+"
        "(thread_pointer()!=0);}",
        "asm-lock-prefix-statements", clang);
    // The forms of musl's x86_64 math (src/math/x86_64), in functions of this test's own: sqrtsd and sqrtss, cvtsd2si and
    // cvtss2si into 64- and 32-bit registers under MXCSR's rounding, and a magnitude's mask, all ones (pcmpeqd) shifted
    // right (psrlq, psrld), and-ed in (andps).
    check_test_program_source(test,
        "static double root(double v){double r;__asm__(\"sqrtsd %1, %0\" : \"=x\"(r) : \"x\"(v));return r;}"
        "static float root_float(float v){float r;__asm__(\"sqrtss %1, %0\" : \"=x\"(r) : \"x\"(v));return r;}"
        "static long to_long(double v){long r;__asm__(\"cvtsd2si %1, %0\" : \"=r\"(r) : \"x\"(v));return r;}"
        "static long float_to_long(float v){long r;__asm__(\"cvtss2si %1, %0\" : \"=r\"(r) : \"x\"(v));return r;}"
        "static int to_int(double v){int r;__asm__(\"cvtsd2si %1, %0\" : \"=r\"(r) : \"x\"(v));return r;}"
        "static int float_to_int(float v){int r;__asm__(\"cvtss2si %1, %0\" : \"=r\"(r) : \"x\"(v));return r;}"
        "static double magnitude_mask(void){double m;__asm__(\"pcmpeqd %0, %0\" : \"=x\"(m));__asm__(\"psrlq   $1, %0\" : \"+x\"(m));return m;}"
        "static double magnitude(double v){double m=magnitude_mask();__asm__(\"andps   %1, %0\" : \"+x\"(v) : \"x\"(m));return v;}"
        "static float float_magnitude_mask(void){float m;__asm__(\"pcmpeqd %0, %0\" : \"=x\"(m));__asm__(\"psrld   $1, %0\" : \"+x\"(m));return m;}"
        "static float float_magnitude(float v){float m=float_magnitude_mask();__asm__(\"andps   %1, %0\" : \"+x\"(v) : \"x\"(m));return v;}"
        "int tested(int n){double d=n*0.625-3.5;float f=(float)n*-0.875f+1.5f;double a=magnitude(d);float b=float_magnitude(f);"
        "return (int)(root(a)*1000)+(int)(root_float(b)*100)*3+(int)(to_long(d*1e11/9)%100000)+(int)float_to_long(f*5)*5+to_int(d*27)*7+float_to_int(f*19)*11+"
        "(int)(a*8)+(int)(b*4)*13;}",
        "asm-sse-scalar-math", clang);
    // dr_flac's lzcnt, `rep; bsr` in GCC's dialect alternatives (the AT&T one, `bsrq %1, %0`), and tzcnt so: their bytes
    // (F3 0F BD and BC), which run as the scans where the processor has no LZCNT or BMI (of operands never zero, alike).
    check_test_program_source(test,
        "static unsigned long clz(unsigned long x){unsigned long r;__asm__ __volatile__(\"rep; bsr{q %1, %0| %0, %1}\" : \"=r\"(r) : \"r\"(x) : \"cc\");return r;}"
        "static unsigned clz32(unsigned x){unsigned r;__asm__ __volatile__(\"rep; bsr{l %1, %0| %0, %1}\" : \"=r\"(r) : \"r\"(x) : \"cc\");return r;}"
        "static unsigned long ctz(unsigned long x){unsigned long r;__asm__ __volatile__(\"rep\\n\\tbsfq %1, %0\" : \"=r\"(r) : \"r\"(x) : \"cc\");return r;}"
        "int tested(int n){unsigned long x=(unsigned long)(n*n*1000003+1)<<(n&15);return (int)clz(x)*100+(int)clz32((unsigned)x|1)*10+(int)ctz(x);}",
        "asm-rep-bsr", clang);
    // FMA3's scalar forms, as musl's fma and fmaf take them where the target has FMA, in functions of this test's own:
    // compiled and linked everywhere, run where the host has FMA.
    const bool fused_native = (native.features & BUSTER_X86_FEATURE(FMA)) != 0;
    if (!fused_native)
    {
        printf("test-checker: asm-fma-scalar: FMA unavailable; checking compile/link without native execution\n");
    }
    check_test_program_source_native(test,
        "static double fused(double a,double b,double c){__asm__(\"vfmadd132sd %1, %2, %0\" : \"+x\"(a) : \"x\"(b), \"x\"(c));return a;}"
        "static float fused_float(float a,float b,float c){__asm__(\"vfmadd132ss %1, %2, %0\" : \"+x\"(a) : \"x\"(b), \"x\"(c));return a;}"
        "int tested(int n){double x=n*0.3+1/7.0,y=n-0.4,z=3e-17*n;float u=n*0.7f+1/7.0f,v=n-0.4f,w=3e-8f*n;"
        "double r=fused(x,y,z)-x*y;float s=fused_float(u,v,w)-u*v;return (int)(r*1e18)+(int)(s*1e9f)*7+(int)fused(y,y,x)*3;}",
        "asm-fma-scalar", clang, fused_native);
    // Explicit register variables as operands, in the shape of musl's syscall wrappers (arch/x86_64/syscall_arch.h), in
    // functions of this test's own: the fourth to sixth arguments in r10, r8 and r9 (`register long fourth __asm__("r10")
    // = d;` and "r"(fourth)), beside the fixed registers of the others. getppid, getuid, prctl's PR_GET_NAME,
    // rt_sigprocmask's mask and two pages mapped, written and unmapped, each the same in clang's build in the same
    // process.
    check_test_program_source(test,
        "static long call0(long number){long result;__asm__ __volatile__(\"syscall\" : \"=a\"(result) : \"0\"(number) : \"rcx\", \"r11\", \"memory\");return result;}"
        "static long call2(long number, long first, long second){long result;__asm__ __volatile__(\"syscall\" : \"=a\"(result) : \"a\"(number), \"D\"(first),"
        " \"S\"(second) : \"rcx\", \"r11\", \"memory\");return result;}"
        "static long call4(long number, long a, long b, long c, long d){long result;register long fourth __asm__(\"r10\") = d;"
        "__asm__ __volatile__(\"syscall\" : \"=a\"(result) : \"a\"(number), \"D\"(a), \"S\"(b), \"d\"(c), \"r\"(fourth) : \"rcx\", \"r11\", \"memory\");return result;}"
        "static long call6(long number, long a, long b, long c, long d, long e, long f){long result;register long fourth __asm__(\"r10\") = d;"
        "register long fifth __asm__(\"r8\") = e;register long sixth __asm__(\"r9\") = f;__asm__ __volatile__(\"syscall\" : \"=a\"(result) : \"a\"(number), \"D\"(a),"
        " \"S\"(b), \"d\"(c), \"r\"(fourth), \"r\"(fifth), \"r\"(sixth) : \"rcx\", \"r11\", \"memory\");return result;}"
        "int tested(int n){long parent=call0(110);long user=call0(102);char name[16]={0};long named=call2(157,16,(long)name);"
        "unsigned long old_mask[2]={0,0};long masked=call4(14,0,0,(long)old_mask,8);long page=call6(9,0,8192,3,0x22,-1,0);"
        "if(page>0)((volatile char*)page)[4096]=(char)n;long gone=call2(11,page,8192);"
        "return (parent>0)+(user>=0)*2+(named==0)*3+(name[0]!=0)*5+(int)masked*7+(page>0&&(page&4095)==0)*11+(int)gone*13+n;}",
        "asm-register-variables-syscalls", clang);
    // Each shape of a register variable's operand, as clang places it: an input in the variable's register whatever its
    // letters ("a"), through a cast that keeps its width, by a 32-bit name of a 64-bit register and as an int's 32-bit
    // name; outputs in their variables' registers, a memory output's among them; a `+` operand in a callee-saved one; a
    // matching input in its variable's register, not its output's; an XMM register's; and a template of no instructions.
    check_test_program_source(test,
        "int tested(int n){register long a __asm__(\"r10\")=n*3;long o1;__asm__(\"movq %%r10, %0\" : \"=r\"(o1) : \"a\"(a));"
        "register unsigned long b __asm__(\"r9\")=(unsigned long)n<<33;long o2;__asm__(\"movq %%r9, %0\" : \"=r\"(o2) : \"r\"((long)b));"
        "register long c __asm__(\"ebx\")=n-7;long o3;__asm__(\"movq %%rbx, %0\" : \"=r\"(o3) : \"r\"(c));"
        "register int d __asm__(\"r12\")=n*5;int o4;__asm__(\"movl %1, %0\" : \"=r\"(o4) : \"r\"(d));"
        "register long e __asm__(\"r11\");__asm__(\"movq $42, %%r11\" : \"=r\"(e));register long f __asm__(\"r8\");__asm__(\"movq $7, %%r8\" : \"=m\"(f));"
        "register long g __asm__(\"rbx\")=n;__asm__(\"addq $5, %%rbx\" : \"+r\"(g));"
        "register long h __asm__(\"r13\")=n*9;long o5;__asm__(\"movq %%r13, %0\" : \"=r\"(o5) : \"0\"(h));"
        "register double x __asm__(\"xmm5\")=n*0.5;double o6;__asm__(\"movaps %%xmm5, %0\" : \"=x\"(o6) : \"x\"(x));"
        "register long i __asm__(\"r10\")=n+100;__asm__(\"\" : \"+r\"(i));"
        "return (int)o1+(int)(o2>>33)*3+(int)o3*5+o4*7+(int)e+(int)f*11+(int)g*13+(int)o5*17+(int)(o6*4)*19+(int)i*23;}",
        "asm-register-variables", clang);
    // The forms of musl's long double functions (src/math/x86_64), in functions of this test's own: fabs, fsqrt and
    // frndint on a `+t` operand; fprem's and fprem1's loops with y in st(1) and the status word through fnstsw %ax ("=a"),
    // whose st(1) the helper pops after it stores x; a remainder's quotient bits from that word (C0, C3 and C1, its bits
    // 2, 1 and 0); fistpll's store of a "t" input it pops (its "st" clobber); and "X" operands of a template of no
    // instructions.
    check_test_program_source(test,
        "static long double absolute(long double v){__asm__(\"fabs\" : \"+t\"(v));return v;}"
        "static long double square_root(long double v){__asm__(\"fsqrt\" : \"+t\"(v));return v;}"
        "static long double rounded(long double v){__asm__(\"frndint\" : \"+t\"(v));return v;}"
        "static long double truncated_remainder(long double x, long double y){unsigned short status;"
        "do __asm__(\"fprem; fnstsw %%ax\" : \"+t\"(x), \"=a\"(status) : \"u\"(y));while((status>>10)&1);return x;}"
        "static long double nearest_remainder(long double x, long double y, int* quotient){unsigned short status;long double r=x;"
        "const void* inputs[2]={&x,&y};__asm__(\"\" :: \"X\"(inputs[0]), \"X\"(inputs[1]));"
        "do __asm__(\"fprem1\\n\\tfnstsw %%ax\" : \"+t\"(r), \"=a\"(status) : \"u\"(y));while((status>>10)&1);"
        "int low=((status>>8)&1)<<2|((status>>14)&1)<<1|((status>>9)&1);*quotient=(x<0)!=(y<0) ? -low : low;return r;}"
        "static long whole(long double v){long r;__asm__(\"fistpll %0\" : \"=m\"(r) : \"t\"(v) : \"st\");return r;}"
        "static long long whole_long_long(long double v){long long r;__asm__(\"fistpll %0\" : \"=m\"(r) : \"t\"(v) : \"st\");return r;}"
        "int tested(int n){long double a=n*1.25L-4,b=n%6+0.75L;int q=0;long double r=nearest_remainder(a*9,b,&q);"
        "return (int)(square_root(absolute(a))*1000)+(int)(rounded(a*3.5L)*3)+(int)(truncated_remainder(a*13,b)*100)*5+(int)(r*100)*7+q*11+"
        "(int)whole(a*2.2L)*13+(int)whole_long_long(a*-2.75L)*17;}",
        "asm-x87-library-shapes", clang);
    // Each shape of the x87's operands, by the rules clang's code follows (LLVM's stackifier): two outputs from st(0)
    // (fsincos), a matching input popped with st(1)'s clobbered input (fyl2x) and with st(1)'s kept one, which the helper
    // pops (fscale), outputs of a template that pushes them (fld1, fldpi into a double), a float's and a double's
    // operands, and templates of no instructions.
    check_test_program_source(test,
        "static long double x_sincos(long double x, long double* s){long double c,t;__asm__(\"fsincos\" : \"=t\"(c), \"=u\"(t) : \"0\"(x));*s=t;return c;}"
        "static long double x_yl2x(long double x, long double y){long double r;__asm__(\"fyl2x\" : \"=t\"(r) : \"0\"(x), \"u\"(y) : \"st(1)\");return r;}"
        "static long double x_scale(long double x, long double y){long double r;__asm__(\"fscale\" : \"=t\"(r) : \"0\"(x), \"u\"(y));return r;}"
        "static double x_one(void){double r;__asm__(\"fld1\" : \"=t\"(r));return r;}"
        "static double x_pi(void){double r;__asm__(\"fldpi\" : \"=t\"(r));return r;}"
        "static float x_sqrtf(float x){__asm__(\"fsqrt\" : \"+t\"(x));return x;}"
        "static double x_sqrt(double x){double r;__asm__(\"fsqrt\" : \"=t\"(r) : \"0\"(x));return r;}"
        "static long double x_keep(long double x){__asm__(\"\" : \"+t\"(x));return x;}"
        "static long double x_copy(long double x){long double r;__asm__(\"\" : \"=t\"(r) : \"0\"(x));return r;}"
        "int tested(int n){long double s=0;long double c=x_sincos(n*0.25L,&s);"
        "return (int)(c*10000)+(int)(s*10000)*3+(int)(x_yl2x(n*n+1.5L,n+0.5L)*1000)*5+(int)(x_scale(1.5L,n%7)*64)*7+(int)(x_one()*11)+(int)(x_pi()*1e6)+"
        "(int)(x_sqrtf(n*n+2.f)*1000)*13+(int)(x_sqrt(n+5.0)*1e6)%100000+(int)(x_keep(n*0.5L)*4)*17+(int)(x_copy(n*0.75L)*4)*19;}",
        "asm-x87-shapes", clang);
    // A float's and a double's signaling NaNs through `+t` in templates of no instructions: their helper passes them
    // through the x87's stack as clang's code does, whose fld quiets them and raises the invalid operation (masked). The
    // results' bits and the status word's flag after fnclex, and a long double's value, must be clang's build's.
    check_test_program_source(test,
        "static float through_float(float v){__asm__ volatile(\"\" : \"+t\"(v));return v;}"
        "static double through_double(double v){__asm__ volatile(\"\" : \"+t\"(v));return v;}"
        "static long double through_long_double(long double v){__asm__ volatile(\"\" : \"+t\"(v));return v;}"
        "static unsigned short status_word(void){unsigned short word;__asm__ volatile(\"fnstsw %0\" : \"=m\"(word));return word;}"
        "static void clear_exceptions(void){__asm__ volatile(\"fnclex\");}"
        "int tested(int n){unsigned bits=0x7f800001u|((unsigned)n&0xffu)<<8;float f;__builtin_memcpy(&f,&bits,4);"
        "clear_exceptions();float g=through_float(f);unsigned short after_float=status_word();unsigned out;__builtin_memcpy(&out,&g,4);"
        "unsigned long long wide=0x7ff0000000000001ull|((unsigned long long)n&0xffu)<<20;double d;__builtin_memcpy(&d,&wide,8);"
        "clear_exceptions();double e=through_double(d);unsigned short after_double=status_word();unsigned long long wide_out;__builtin_memcpy(&wide_out,&e,8);"
        "long double x=n*0.5L;clear_exceptions();long double y=through_long_double(x);unsigned short after_long_double=status_word();"
        "return (int)(out>>8)+(int)(wide_out>>40)*3+(after_float&1)*5+(after_double&1)*7+(after_long_double&1)*11+(int)(y*4)*13;}",
        "asm-x87-empty-signaling", clang);
    // What the lowering refuses, by name.
    check_test_program_boundary_reason(test, "int tested(int n){long r;__asm__(\"movq %1, %0\" : \"=r\"(r) : \"Z\"(0x100000000ull));return (int)r+n;}",
                                       "asm-z-range", true, false, 0, 0, "inline assembly constants outside their constraint's range");
    check_test_program_boundary_reason(test, "int tested(int n){int r;__asm__(\"movl %1, %0\" : \"=r\"(r) : \"N\"((unsigned char)255));return r+n;}",
                                       "asm-n-range", true, false, 0, 0, "inline assembly constants outside their constraint's range");
    check_test_program_boundary_reason(test, "int tested(int n){_Bool z;__asm__(\"testl %1, %1\" : \"=@ccz\"(z) : \"r\"(n));return z;}",
                                       "asm-flag-output", true, false, 0, 0, "inline assembly flag outputs");
    // The x87's operands the lowering doesn't place: f's register of the compiler's choice; an integer; shapes off the
    // stack's top, which clang's code refuses too (an input at st(1) without one at st(0), a clobber below the stack's
    // top), and two inputs at st(0), which clang compiles as one register's; and an x87 register named in the template,
    // which the assembler doesn't parse.
    check_test_program_boundary_reason(test, "int tested(int n){long double x=n;__asm__ volatile(\"\" : : \"f\"(x));return n;}",
                                       "asm-x87-any", true, false, 0, 1, "inline assembly operands in x87 registers the compiler chooses");
    check_test_program_boundary_reason(test, "int tested(int n){int i=n;__asm__(\"\" : \"+t\"(i));return i;}",
                                       "asm-x87-integer", true, false, 0, 1, "inline assembly operands of this type in their constraint's registers");
    check_test_program_boundary_reason(test, "int tested(int n){long double x=n;__asm__ volatile(\"\" : : \"u\"(x));return n;}",
                                       "asm-x87-gap", true, false, 0, 1, "inline assembly's x87 operands and clobbers off the stack's top");
    check_test_program_boundary_reason(test, "int tested(int n){long double x=n,y=n;__asm__ volatile(\"\" : : \"t\"(x), \"t\"(y));return n;}",
                                       "asm-x87-doubled", true, false, 0, 2, "inline assembly's x87 operands and clobbers off the stack's top");
    check_test_program_boundary_reason(test, "int tested(int n){long double x=n;__asm__ volatile(\"\" : : \"t\"(x) : \"st(1)\");return n;}",
                                       "asm-x87-clobber", true, false, 0, 1, "inline assembly's x87 operands and clobbers off the stack's top");
    check_test_program_boundary_reason(test, "int tested(int n){long double x=n;__asm__(\"fsqrt # %0\" : \"+t\"(x));return (int)x;}",
                                       "asm-x87-named", true, false, 0, 1, "x87 registers named in inline assembly templates");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__ volatile(\"rep ret\" ::: \"memory\");return n;}",
                                      "asm-returning-template", true, false, 0, 0, "inline assembly templates that return from their helper");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__(\".byte 0x90\");return n;}",
                                       "asm-directive", true, false, 0, 0, "inline assembly takes no directive but .intel_syntax and .att_syntax");
    // Explicit register variables the lowering doesn't place: in rsp (the helper's own), a high byte and an XMM register
    // past XMM15; an input of a memory constraint, which clang's code refuses too; and an integer in an XMM register.
    check_test_program_boundary_reason(test, "int tested(int n){register long r __asm__(\"rsp\");__asm__ volatile(\"\" : : \"r\"(r));return n;}",
                                       "asm-register-variable-rsp", true, false, 0, 0, "explicit register variables of this register as inline assembly operands");
    check_test_program_boundary_reason(test, "int tested(int n){register char r __asm__(\"ah\")=(char)n;__asm__ volatile(\"\" : : \"q\"(r));return n;}",
                                       "asm-register-variable-high", true, false, 0, 1, "explicit register variables of this register as inline assembly operands");
    check_test_program_boundary_reason(test, "int tested(int n){register double r __asm__(\"xmm16\")=n;__asm__ volatile(\"\" : : \"v\"(r));return n;}",
                                       "asm-register-variable-xmm16", true, false, 0, 1, "explicit register variables of this register as inline assembly operands");
    check_test_program_boundary_reason(test, "int tested(int n){register long r __asm__(\"r10\")=n;__asm__ volatile(\"\" : : \"m\"(r));return n;}",
                                       "asm-register-variable-memory", true, false, 0, 1, "explicit register variables as inline assembly memory inputs");
    check_test_program_boundary_reason(test, "int tested(int n){register long r __asm__(\"xmm3\")=n;__asm__ volatile(\"\" : : \"x\"(r));return n;}",
                                       "asm-register-variable-integer-xmm", true, false, 0, 1, "inline assembly operands of this type in their constraint's registers");
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
