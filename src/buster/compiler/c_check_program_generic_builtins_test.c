// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's generic builtins of an instruction or two and of clang's own Sema checks in test-checker, beside the alignment
// builtins (check_test_align): source fixtures of __builtin_classify_type's classes, __builtin_eh_return_data_regno's
// registers, __builtin_launder's and __builtin_dwarf_cfa's types and the leaves' prototypes (c_builtin_x86_leaves.inc),
// with their refusals, each checked by both paths against clang -fsyntax-only; then programs run natively against clang
// -O0's build of the same source on both paths: the folds, launder's pointer, the CFA against the caller's stack address,
// and the leaves (debugtrap's trap handled, the counters, the thread and stack pointers, the no-ops' arguments evaluated
// once in order, memcpy_inline's and memset_inline's bytes, and the alloca variants, the checks, the annotation and the
// addresses). Include before c_check_program_align_test.c.
#ifndef BUSTER_C_CHECK_PROGRAM_GENERIC_BUILTINS_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_GENERIC_BUILTINS_TEST_INCLUDED

static __attribute__((noinline)) void check_test_generic_builtin_sources(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/generic-builtins, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/generic-builtins", "the generic builtins' Clang oracle ran");
}

static __attribute__((noinline)) void check_test_generic_builtin_programs(CheckTest* test, const char* clang)
{
    // The folds at run time and in static data, launder's pointers, and the CFA of a callee, which is the caller's stack
    // address at the call.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}struct S{int a;};union U{int a;};int g;"
        "static int classes[]={__builtin_classify_type(0),__builtin_classify_type((_Bool)0),__builtin_classify_type((int*)0),"
        "__builtin_classify_type(1.0),__builtin_classify_type((_Complex float)0),__builtin_classify_type((struct S){0}),"
        "__builtin_classify_type((union U){0}),__builtin_classify_type((void)0),__builtin_classify_type((_BitInt(9))0),"
        "__builtin_eh_return_data_regno(0),__builtin_eh_return_data_regno(1),__builtin_eh_return_data_regno(7)};"
        "static int* laundered=__builtin_launder(&g);"
        "static __attribute__((noinline)) char* cfa(void){return __builtin_dwarf_cfa();}"
        "int tested(int n){h=(unsigned)n;for(unsigned i=0;i<sizeof classes/sizeof classes[0];i++)mix((unsigned)classes[i]);"
        "int k=n;mix((unsigned)__builtin_classify_type(k++));mix((unsigned)k);int x=n*3;int* p=__builtin_launder(&x);const int* q=__builtin_launder((const int*)p);"
        "mix((unsigned)(*p+*q));mix(laundered==&g);char* s=__builtin_stack_address();char* c=cfa();mix((unsigned long long)(c-s));"
        "return (int)(h^(h>>32));}",
        "generic-folds-launder-and-the-cfa", clang);
    // The leaves: debugtrap's trap, which a SIGTRAP handler counts and returns past; the cycle counter, which doesn't go
    // back, and the steady one's 0; the thread pointer, which glibc's TCB holds at its own address; the stack address,
    // below a local; rsp's DWARF register; the no-ops' arguments, evaluated once, in order; and the inline copy's and fill's
    // bytes.
    check_test_program_source(test,
        "typedef void (*H)(int);H signal(int,H);static volatile int trapped;static void on_trap(int s){trapped+=s;}"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}static int order;static char* at(char* p,int t){order=order*10+t;return p;}"
        "int tested(int n){h=(unsigned)n;signal(5,on_trap);__builtin_debugtrap();__builtin_debugtrap();mix((unsigned)trapped);"
        "unsigned long long a=__builtin_readcyclecounter(),b=__builtin_readcyclecounter();mix(b>=a);mix(__builtin_readsteadycounter());"
        "void* tp=__builtin_thread_pointer();mix(tp==*(void**)tp);char local;long d=&local-(char*)__builtin_stack_address();mix(d>=0&&d<4096);"
        "mix(__builtin_dwarf_sp_column());char s[32]=\"abcdefghijklmnopqrstuvwxyz\",t[32]={0};"
        "__builtin___clear_cache(at(s,1),at(s+4,2));__builtin_assume_separate_storage(at(s,3),at(t,4));__builtin_assume_dereferenceable(at(s+1,5),8);"
        "mix((unsigned)order);__builtin_memcpy_inline(t+(n&3),s+1,13);__builtin_memset_inline(s+2,'A'+(n&7),5);__builtin_memcpy_inline(t+20,s,0);"
        "for(int i=0;i<32;i++){mix((unsigned char)s[i]);mix((unsigned char)t[i]);}"
        "return (int)(h^(h>>32));}",
        "generic-leaves", clang);
    // The unwinder's table of DWARF registers' sizes, over a buffer's bytes; each rounding mode in the x87's control word
    // and MXCSR, and a division rounded by it; the pointer's value; the values unwind_init leaves; and verbose_trap's
    // SIGILL, in a child process.
    check_test_program_source(test,
        "int fork(void);int waitpid(int,int*,int);void _exit(int);"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "static unsigned cw(void){unsigned short w;__asm__ volatile(\"fnstcw %0\":\"=m\"(w));return w;}"
        "int tested(int n){h=(unsigned)n;unsigned char t[24];for(int i=0;i<24;i++)t[i]=(unsigned char)(n+i);"
        "__builtin_init_dwarf_reg_size_table(t);for(int i=0;i<24;i++)mix(t[i]);"
        "for(int v=0;v<4;v++){__builtin_set_flt_rounds((v+n)&3);mix(cw()&0xc00);mix(__builtin_ia32_stmxcsr()&0x6000);"
        "volatile double x=1.0,y=3.0;mix((unsigned long long)(x/y*1e17));}__builtin_set_flt_rounds(1);"
        "int x=n;unsigned long long e=__builtin_extend_pointer(&x);mix(e==(unsigned long long)&x);"
        "int a=n*7,b=n+3;__builtin_unwind_init();mix((unsigned)(a+b));"
        "int pid=fork();if(pid==0){__builtin_verbose_trap(\"category\",\"the message\");_exit(9);}"
        "int status=0;waitpid(pid,&status,0);mix((unsigned)status&0x7f);"
        "return (int)(h^(h>>32));}",
        "generic-rounding-unwinder-and-trap", clang);
    // __builtin_unwind_init's function saves the callee-saved registers rbx and r12 to r15 in its prologue, as clang's
    // does (#1126's review), where a function that doesn't call it, and uses none of them, saves none: the leading pushes
    // of each function's code, read by the program itself (past an endbr64, and a frame pointer's move).
    check_test_program_source(test,
        "static void saver(void){__builtin_unwind_init();}"
        "static int plain(int x){return x+1;}"
        "static int kept(int x){int a=x*3,b=x*5;__builtin_unwind_init();return a+b;}"
        "static unsigned pushed(const unsigned char* code){unsigned mask=0;unsigned i=0;int going=1;"
        "if(code[0]==0xf3&&code[1]==0x0f&&code[2]==0x1e&&code[3]==0xfa)i=4;"
        "while(going&&i<64){if(code[i]>=0x50&&code[i]<=0x57){mask|=1u<<(code[i]-0x50);i+=1;}"
        "else if(code[i]==0x41&&code[i+1]>=0x50&&code[i+1]<=0x57){mask|=1u<<(code[i+1]-0x50+8);i+=2;}"
        "else if(code[i]==0x48&&code[i+1]==0x89&&code[i+2]==0xe5){i+=3;}else going=0;}return mask;}"
        "int tested(int n){const unsigned callee=1u<<3|1u<<12|1u<<13|1u<<14|1u<<15;saver();"
        "return (int)((pushed((const unsigned char*)saver)&callee)*4+(pushed((const unsigned char*)kept)&callee)*2+"
        "((pushed((const unsigned char*)plain)&callee)!=0))+plain(n)*7+kept(n);}",
        "generic-unwind-init-saves", clang);
    // __builtin_counted_by_ref (#148): stores through the count's address, of an `->`'s and a `.`'s access, of a count in an
    // anonymous struct's enclosing record, and a null void * of a flexible array member of no count; and
    // __builtin_dynamic_object_size of clang's counted_by forms, at counts below zero and above: a flexible array member,
    // its element's address (and through a char *), another member's (an array's element too), a counted pointer and a
    // sized_by one, a count in the enclosing record, mode 3's 0, and __builtin_object_size, which ignores the count.
    check_test_program_source(test,
        "typedef unsigned long size_t;void* malloc(size_t);"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "struct S{char tag;long n;int fam[] __attribute__((counted_by(n)));};"
        "struct U{unsigned char n;short arr[3];char c;long fam[] __attribute__((counted_by(n)));};"
        "struct P{int x;unsigned short m;double* p __attribute__((counted_by(m)));char* q __attribute__((sized_by(x)));};"
        "struct N{int k;struct{int x;char fam[] __attribute__((counted_by(k)));};};"
        "struct Q{int n;int fam[];};"
        "__attribute__((noinline)) static unsigned long f0(struct S* s){return __builtin_dynamic_object_size(s->fam,0);}"
        "__attribute__((noinline)) static unsigned long f1(struct S* s,int i){return __builtin_dynamic_object_size(&s->fam[i],1);}"
        "__attribute__((noinline)) static unsigned long f2(struct S* s){return __builtin_dynamic_object_size(&s->tag,0);}"
        "__attribute__((noinline)) static unsigned long f3(struct U* u,int i){return __builtin_dynamic_object_size(&u->arr[i],2);}"
        "__attribute__((noinline)) static unsigned long f4(struct P* p,int i){return __builtin_dynamic_object_size(&p->p[i],0)+1000*__builtin_dynamic_object_size(p->q,1);}"
        "__attribute__((noinline)) static unsigned long f5(struct N* n){return __builtin_dynamic_object_size(n->fam,0);}"
        "__attribute__((noinline)) static unsigned long f6(struct S* s){return __builtin_dynamic_object_size(s->fam,3);}"
        "__attribute__((noinline)) static unsigned long f7(struct Q* q){return __builtin_dynamic_object_size(q->fam,0);}"
        "__attribute__((noinline)) static unsigned long f8(struct S* s,int i){return __builtin_dynamic_object_size(&((char*)s->fam)[i],0);}"
        "__attribute__((noinline)) static unsigned long f9(struct U* u){return __builtin_dynamic_object_size(u->fam,0);}"
        "__attribute__((noinline)) static unsigned long f10(struct S* s){return __builtin_object_size(s->fam,0);}"
        "int tested(int n){h=(unsigned)n;struct S* s=malloc(sizeof(struct S)+64);struct U* u=malloc(sizeof(struct U)+64);"
        "struct P p={5,3,0,0};struct N* m=malloc(sizeof(struct N)+8);struct Q* q=malloc(sizeof(struct Q)+8);"
        "for(long c=-2;c<=6+n;c+=4){*__builtin_counted_by_ref(s->fam)=c;u->n=(unsigned char)c;*__builtin_counted_by_ref(m->fam)=(int)c;"
        "*__builtin_counted_by_ref(p.p)=(unsigned short)(c+3);"
        "mix(f0(s));mix(f1(s,2));mix(f1(s,-1));mix(f1(s,9));mix(f2(s));mix(f3(u,1));mix(f4(&p,1));mix(f4(&p,5));mix(f5(m));mix(f6(s));"
        "mix(f7(q));mix(f8(s,3));mix(f9(u));mix(f10(s));mix((unsigned long)s->n);mix(m->k);mix(p.m);}"
        "mix((void*)__builtin_counted_by_ref(q->fam)==0);"
        "return (int)(h^(h>>32));}",
        "generic-counted-by", clang);
    // __builtin_available (#148): 1 for every platform's spec on Linux, in conditions, negated, as a value, through its
    // type's queries, which never run it, and in a loop's condition.
    check_test_program_source(test,
        "int tested(int n){int r=0;if(__builtin_available(macos 10.12,ios 10,*))r+=1;if(!__builtin_available(*))r+=2;"
        "_Bool b=__builtin_available(android 21,*);r+=b*4;r+=(int)sizeof(__builtin_available(*))*8;"
        "r+=_Generic(__builtin_available(*),_Bool:16,default:0);typeof(__builtin_available(*)) t=7;r+=t*32;"
        "for(int i=0;i<n&&__builtin_available(*);i++)r+=64;return r+(n>3?__builtin_available(swift 5,*):0)*1024;}",
        "generic-available", clang);
    // The alloca variants' bytes and alignments, in a loop whose allocations each stay apart; the checks' values; the
    // annotation's integer, once evaluated; and addressof's and function_start's addresses, of a local, a member, an
    // array, a function, and in static data.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}struct S{int a,b;};static int g;"
        "static int callee(int x){return x*5+1;}static int* pg=__builtin_addressof(g);static void* ps=__builtin_function_start(callee);"
        "static int counter;static int bump(void){return ++counter;}"
        "int tested(int n){h=(unsigned)n;unsigned char* last=0;for(int i=0;i<4;i++){"
        "unsigned char* a=__builtin_alloca_uninitialized(n+i);unsigned char* b=__builtin_alloca_with_align(i+3,64);"
        "unsigned char* c=__builtin_alloca_with_align_uninitialized(5,8*4096);unsigned char* d=__builtin_alloca_with_align(7,8);"
        "for(int k=0;k<n+i;k++)a[k]=(unsigned char)(k+i);for(int k=0;k<i+3;k++)b[k]=(unsigned char)(k*3);for(int k=0;k<5;k++)c[k]=(unsigned char)k;"
        "for(int k=0;k<7;k++)d[k]=(unsigned char)(k^i);mix(((unsigned long)b&7)==0);mix(((unsigned long)c&4095)==0);mix(((unsigned long)d&7)==0);"
        "mix(last==0||(last!=a&&last!=b&&last!=c&&last!=d));last=a;unsigned s=0;for(int k=0;k<n+i;k++)s+=a[k];for(int k=0;k<i+3;k++)s+=b[k];"
        "for(int k=0;k<5;k++)s+=c[k];for(int k=0;k<7;k++)s+=d[k];mix(s);}"
        "mix(__builtin_allow_runtime_check(\"x\"));mix(__builtin_allow_sanitize_check(\"address\"));mix(__builtin_allow_sanitize_check(\"thread\"));"
        "counter=0;long y=__builtin_annotation((long)bump()*n,\"note\");mix((unsigned long long)y);mix((unsigned)counter);"
        "int x=n;int* px=__builtin_addressof(x);*px+=3;mix((unsigned)x);struct S st={n,2*n};int* pb=__builtin_addressof(st.b);mix((unsigned)*pb);"
        "int arr[4]={1,2,3,n};int (*pa)[4]=__builtin_addressof(arr);mix((unsigned)(*pa)[3]);mix(pg==&g);"
        "int (*f)(int)=__builtin_addressof(callee);mix((unsigned)f(n));mix(ps==(void*)callee);mix(__builtin_function_start(callee)==(void*)&callee);"
        "return (int)(h^(h>>32));}",
        "generic-alloca-checks-and-addresses", clang);
    // __builtin_function_start of a function's address and its casts, in static data and at run time, the function's start
    // (#1126's review).
    check_test_program_source(test,
        "static int callee(int x){return x*5+1;}"
        "static void* const starts[]={__builtin_function_start(&callee),__builtin_function_start((&callee)),"
        "__builtin_function_start((int(*)(void))callee),__builtin_function_start((void*)&callee),__builtin_function_start(*&callee),"
        "__builtin_function_start(1?callee:callee),__builtin_function_start(callee+0)};"
        "int tested(int n){unsigned r=(unsigned)n;for(int i=0;i<7;i++)r=r*31+(starts[i]==(void*)callee);"
        "r=r*31+(__builtin_function_start(&callee)==(void*)&callee);r=r*31+(__builtin_function_start((void(*)(void))&callee)==(void*)callee);"
        "r=r*31+(__builtin_function_start((&callee))==starts[n&3]);int (*f)(int)=(int(*)(int))__builtin_function_start(&callee);"
        "r=r*31+(unsigned)f(n);return (int)r;}",
        "generic-function-start-of-addresses", clang);
    // __builtin_FILE_NAME's and __FILE_NAME__'s component of presumed names, as clang's llvm::sys::path::filename takes it
    // (#1126's review): a root's, a trailing separator's ".", a network root name's, an empty name's and a plain path's.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "static void mixs(const char* s){while(*s)mix((unsigned char)*s++);mix(0);}\n"
        "int tested(int n){h=(unsigned)n;\n"
        "#line 20 \"/\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 30 \"/foo/\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 40 \"a/\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 50 \"//\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 60 \"//net\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 70 \"\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 80 \"../\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 90 \"/dir/name.c\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);mixs(__builtin_FILE());\n"
        "return (int)(h^(h>>32));}\n",
        "generic-file-name-components", clang);
    // The builtins of a place (#148): __builtin_LINE's and __builtin_COLUMN's, through a macro, in an enumerator, an
    // _Static_assert and a static initializer; __builtin_FILE's, __builtin_FILE_NAME's and __builtin_FUNCTION's strings, at
    // file scope ("" for the function) and in one, after a #line that names the file; and the queries of a type,
    // __builtin_vectorelements' (of a vector, an extended vector type and an expression) and
    // __builtin_omp_required_simd_align's. (The oracle's tested is a macro's oracle_tested, so a function's name is a
    // helper's.)
    check_test_program_source(test,
        "#line 7 \"dir/places.c\"\n"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "static void mixs(const char* s){while(*s)mix((unsigned char)*s++);mix(0);}static const char* who(void){return __builtin_FUNCTION();}\n"
        "#define HERE __builtin_LINE()\n"
        "typedef int V __attribute__((vector_size(16)));typedef float W __attribute__((ext_vector_type(3)));\n"
        "static const char* pf=__builtin_FILE();static const char* pn=__builtin_FILE_NAME();static const char* pu=__builtin_FUNCTION();\n"
        "static unsigned line=__builtin_LINE();enum{E=__builtin_LINE(),C=__builtin_COLUMN()};_Static_assert(__builtin_LINE()==12,\"\");\n"
        "int tested(int n){h=(unsigned)n;mixs(pf);mixs(pn);mixs(pu);mix(line);mix(E);mix(C);mix(HERE);mix(__builtin_COLUMN());\n"
        "mixs(who());mixs(__builtin_FILE());V v={n};mix(__builtin_vectorelements(v));mix(__builtin_vectorelements(W));\n"
        "mix(__builtin_omp_required_simd_align(double));mix(sizeof(__builtin_FILE()));mix(_Generic(__builtin_LINE(),unsigned:1,default:2));\n"
        "mix(_Generic(__builtin_FILE(),char*:3,default:4));mix(_Generic(__builtin_vectorelements(v),unsigned long:5,default:6));return (int)(h^(h>>32));}\n",
        "generic-source-places-and-type-queries", clang);
    // __builtin_setjmp and __builtin_longjmp (#148): a longjmp through eight frames of their own, after which a local the
    // function didn't change keeps its value and a volatile one has its last, four times over, and the callee-saved
    // registers the harness keeps across tested come back as they were; and the buffer's first word, the frame address.
    check_test_program_source(test,
        "static void* buf[5];static int depth;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "static void deep(int n){volatile int pad[16];pad[n&15]=n;depth+=pad[n&15]+1;if(n>0)deep(n-1);else __builtin_longjmp(buf,1);}\n"
        "static int jumps(int n){int kept=n*7+1;volatile int changed=0;if(__builtin_setjmp(buf)==0){changed=5;deep(n&7);return -1;}return kept+changed*100;}\n"
        "static int frames(void){void* f[5];int r=__builtin_setjmp(f);return r==0&&f[0]==__builtin_frame_address(0);}\n"
        "int tested(int n){h=(unsigned)n;depth=0;for(int i=0;i<4;i++)mix((unsigned)jumps(n+i));mix((unsigned)depth);mix((unsigned)frames());return (int)(h^(h>>32));}\n",
        "generic-setjmp-longjmp", clang);
    // A value an expression holds across __builtin_setjmp's call, which a longjmp's resumption finds, though calls after it
    // saved values of their own below their arguments (#1242's review): an int's, a double's beside an int's, and nested
    // operands of a call, each resumed once; and a control that holds none.
    check_test_program_source(test,
        "static void* buf[5];static volatile int sink;static volatile double dsink;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "__attribute__((noinline)) static int side(int x){return x+5;}__attribute__((noinline)) static double dside(double x){return x+0.5;}\n"
        "__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}\n"
        "__attribute__((noinline)) static int run(int n){int result=n+100+__builtin_setjmp(buf);if(result==n+100){sink=12345+side(8);jump();}return result;}\n"
        "__attribute__((noinline)) static double drun(int k){double result=(double)k*1.5+__builtin_setjmp(buf);if(result==k*1.5){dsink=7.25+dside(1.0)+side(1);jump();}return result;}\n"
        "__attribute__((noinline)) static int nested(int a){int r=a+(a*3+(side(a)+__builtin_setjmp(buf)));if(r==a+a*3+side(a)){sink=1+(2+(3+side(4)));jump();}return r;}\n"
        "__attribute__((noinline)) static int control(void){if(__builtin_setjmp(buf)==0){sink=99+side(1);jump();}return sink;}\n"
        "int tested(int n){h=(unsigned)n;mix((unsigned)run(n));mix((unsigned)sink);mix((unsigned long long)(drun(n&7)*4));mix((unsigned long long)(dsink*4));\n"
        "mix((unsigned)nested(n&15));mix((unsigned)control());return (int)(h^(h>>32));}\n",
        "generic-setjmp-held-values", clang);
    // A long double's, an __int128's and vectors' values held across __builtin_setjmp's call in their depths' slots, which
    // calls before the longjmp reuse (#1843): the call snapshots them and puts them back. A 256-bit int vector and a
    // 512-bit float vector, and the three classes nested in one expression.
    check_test_program_source(test,
        "typedef int V __attribute__((vector_size(32)));typedef float W __attribute__((vector_size(64)));\n"
        "static void* buf[5];static volatile long double lsink;static volatile __int128 wsink;static V vsink;static W wvsink;\n"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}__attribute__((noinline)) static int side(int x){return x+5;}\n"
        "__attribute__((noinline)) static long double lside(long double x){return x+0.25L;}__attribute__((noinline)) static __int128 wside(__int128 x){return x*3;}\n"
        "__attribute__((noinline)) static V vside(V x){return x+1;}\n"
        "__attribute__((noinline)) static long double lrun(int k){long double r=(long double)k*1.5L+__builtin_setjmp(buf);if(r==k*1.5L){lsink=7.25L+lside(9.5L)*(lside(2.0L)+side(1));jump();}return r;}\n"
        "__attribute__((noinline)) static __int128 wrun(int k){__int128 r=((__int128)k<<70)+__builtin_setjmp(buf);if(r==((__int128)k<<70)){wsink=((__int128)99<<80)+wside((__int128)5<<66)*(wside(3)+side(1));jump();}return r;}\n"
        "__attribute__((noinline)) static V vrun(int k){V b={k,k+1,k+2,k+3,k+4,k+5,k+6,k+7};V r=b*3+__builtin_setjmp(buf);if(r[0]==k*3){vsink=(V){9,9,9,9,9,9,9,9}+vside(b*7)*(vside(b)+side(2));jump();}return r;}\n"
        "__attribute__((noinline)) static W wvrun(int k){W b={k,1,2,3,4,5,6,7,8,9,10,11,12,13,14,k};W r=b*2.5f+(float)__builtin_setjmp(buf);if(r[0]==k*2.5f){wvsink=(W){1}+b*(b+(float)side(3))*(b+(float)side(4));jump();}return r;}\n"
        "__attribute__((noinline)) static long double mixed(int k){long double a=k;__int128 w=k;int r=(int)(a*2+(w*3+(side(k)+__builtin_setjmp(buf))));if(r==k*5+k+5){lsink=lside(1.0L)+(long double)(wside(7)+side(1));jump();}return r+a;}\n"
        "int tested(int n){h=(unsigned)n;int k=(n&15)+1;mix((unsigned long long)(lrun(k)*4));mix((unsigned long long)(lsink*16));__int128 w=wrun(k);mix((unsigned long long)(w>>64));mix((unsigned long long)w);\n"
        "mix((unsigned long long)(wsink>>64));V v=vrun(k);for(int i=0;i<8;i++)mix((unsigned)v[i]);mix((unsigned)vsink[3]);W q=wvrun(k);for(int i=0;i<16;i++)mix((unsigned long long)(q[i]*2));\n"
        "mix((unsigned long long)wvsink[5]);mix((unsigned long long)(mixed(k)*2));return (int)(h^(h>>32));}\n",
        "generic-setjmp-held-wide", clang);
    // A deep expression's values held across __builtin_setjmp's call in their deep slots, integer and floating, and a mask
    // on the mask stack in its slot (#2465), which expressions before the longjmp reuse: the call snapshots them and puts
    // them back. The mask's runs where AVX-512F is.
    check_test_program_source(test,
        "static void* buf[5];static volatile int sink;static volatile double dsink;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}__attribute__((noinline)) static int side(int x){return x+5;}\n"
        "__attribute__((noinline)) static int deep(int a){int b=a+1,c=a+2,d=a+3,e=a+4,f=a+5,g=a+6,i=a+7,j=a+8,k=a+9,l=a+10,m=a+11,o=a+12,p=a+13,q=a+14,s=a+15,t=a+16,u=a+17;\n"
        "int r=a+(b*(c-(d+(e*(f-(g+(i*(j-(k+(l*(m-(o+(p*(q-(s+(t*(u-__builtin_setjmp(buf))))))))))))))))));\n"
        "if(r==a+(b*(c-(d+(e*(f-(g+(i*(j-(k+(l*(m-(o+(p*(q-(s+(t*u))))))))))))))))){sink=u+(t*(s-(q+(p*(o-(m+(l*(k-(j+(i*(g-(f+(e*(d-(c+(b*side(a)))))))))))))))));jump();}return r;}\n"
        "__attribute__((noinline)) static double fdeep(int a){double b=a+0.5,c=a+1.5,d=a+2.5,e=a+3.5,f=a+4.5,g=a+5.5,i=a+6.5,j=a+7.5,k=a+8.5,l=a+9.5,m=a+10.5,o=a+11.5,p=a+12.5,q=a+13.5,s=a+14.5,t=a+15.5,u=a+16.5,v=a+17.5;\n"
        "double r=b+(c*(d-(e+(f*(g-(i+(j*(k-(l+(m*(o-(p+(q*(s-(t+(u*(v-(double)__builtin_setjmp(buf))))))))))))))))));\n"
        "if(r==b+(c*(d-(e+(f*(g-(i+(j*(k-(l+(m*(o-(p+(q*(s-(t+(u*v))))))))))))))))){dsink=v+(u*(t-(s+(q*(p-(o+(m*(l-(k+(j*(i-(g+(f*(e-(d+(c*(double)side(a)))))))))))))))));jump();}return r;}\n"
        "int tested(int n){h=(unsigned)n;int k=(n&15)+1;mix((unsigned)deep(k));mix((unsigned)sink);mix((unsigned long long)(fdeep(k)*4));mix((unsigned long long)(dsink*4));return (int)(h^(h>>32));}\n",
        "generic-setjmp-held-deep", clang);
    const bool masks_native = (x86_target_native().features & BUSTER_X86_FEATURE(AVX512F)) != 0;
    if (!masks_native)
    {
        printf("test-checker: generic-setjmp-held-mask: AVX-512F unavailable; checking compile/link without native execution\n");
    }
    check_test_program_source_native(test,
        "static void* buf[5];static volatile unsigned short msink;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}__attribute__((noinline)) static int side(int x){return x+5;}\n"
        "__attribute__((noinline,target(\"avx512f\"))) static int mrun(int k){int r=(int)__builtin_ia32_kandhi((unsigned short)(k*0x1111+1),(unsigned short)(0xff00|__builtin_setjmp(buf)*0xff));\n"
        "if(r==((k*0x1111+1)&0xff00)){msink=__builtin_ia32_kandhi((unsigned short)side(3),(unsigned short)(0xf0f0|side(9)));jump();}return r;}\n"
        "int tested(int n){h=(unsigned)n;int k=(n&7)+1;mix((unsigned)mrun(k));mix(msink);return (int)(h^(h>>32));}\n",
        "generic-setjmp-held-mask", clang, masks_native);
    // Builtins called through their names in parentheses (#1242's review), whose arguments run once.
    check_test_program_source(test,
        "static int calls;static unsigned next(unsigned v){calls++;return v;}\n"
        "int tested(int n){unsigned u=(unsigned)n|1u;return (int)((__builtin_popcount)(next(u))+((__builtin_clz))(u)*100+(__builtin_bswap32)(u)%1000*10000)+calls*7;}\n",
        "generic-builtin-names-in-parentheses", clang);
    // __builtin_clear_padding (#148): the bytes of objects filled with ones, then cleared, against clang's build: a struct's
    // holes and tail, bit-fields' unused bits and an unnamed one, a union's bytes no member holds, an x87 long double's last
    // six, a _BitInt's high bits, an atomic struct's, an ext vector's tail, an anonymous member's, arrays of structs in a
    // loop (and of arrays), a volatile object, and a variable length array, which clang's code leaves as it is; an element
    // past 4 KiB, and elements whose stores are many, a table's loop's (#1842).
    check_test_program_source(test,
        "void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "struct A{char c;int i;short s;};struct B{int x:3;int:4;unsigned y:9;char z;long double ld;};union U{char c;int i;struct{char a;short b;}s;};\n"
        "struct K{char k[3];};struct C{struct A a[2];_Complex long double z;_Atomic(struct K) at;_BitInt(37) bi;};typedef int V __attribute__((ext_vector_type(3)));\n"
        "struct D{V v;char tail;};struct E{char c;struct{char d;long e;};};struct F{char c;int a[2][2];struct{char p:1;unsigned long q:50;}bits[2];};\n"
        "static void bytes(const void* p,unsigned long n){for(unsigned long i=0;i<n;i++)mix(((const unsigned char*)p)[i]);}\n"
        "#define T(decl) do{decl;memset((void*)&v,0xff,sizeof v);__builtin_clear_padding(&v);bytes((const void*)&v,sizeof v);}while(0)\n"
        "static struct A big[300];struct G{char c[5000];int i;char d;};struct H{char c;struct A a[64];short t:3;};struct I{struct H h[3];long double x[40];};\n"
        "int tested(int n){h=(unsigned)n;T(struct A v);T(struct B v);T(union U v);T(struct C v);T(struct D v);T(struct E v);T(long double v);T(struct F v);\n"
        "T(struct A v[3]);T(struct A v[2][3]);T(struct G v);T(struct H v);T(struct I v);T(struct I v[2]);T(volatile struct A v);T(_Complex long double v);memset(big,0xff,sizeof big);__builtin_clear_padding(&big);bytes(big,sizeof big);\n"
        "int k=(n&3)+1;int vla[k];memset(vla,0xff,sizeof vla);__builtin_clear_padding(&vla);bytes(vla,sizeof vla);return (int)(h^(h>>32));}\n",
        "generic-clear-padding", clang);
    // __builtin_clear_padding of arrays whose types carry sugar (#1842's 1b), which clang's walk keeps as they are: a
    // typedef's array, typeof's, a pointer declared with parentheses (`(*p)[2]`, a parameter's too, through `*pp`), a
    // typedef of such a pointer, an array of a typedef's array, a subscript into one, and a member of a typedef's array, whose
    // record's other padding is cleared; an array of a typedef's struct is cleared.
    check_test_program_source(test,
        "void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "struct S{char c;int i;};typedef struct S A[2];typedef struct S TS;typedef struct S (*PA)[2];typedef A *PTA;struct M{A a;char z;int y;};\n"
        "struct Q{struct M2{A a;char z;}m;int k;};\n"
        "static void bytes(const void* p,unsigned long n){for(unsigned long i=0;i<n;i++)mix(((const unsigned char*)p)[i]);}\n"
        "#define T(decl,arg) do{decl;memset((void*)&v,0xff,sizeof v);__builtin_clear_padding(arg);bytes((const void*)&v,sizeof v);}while(0)\n"
        "static void param(struct S (*p)[2],struct S b[2][2],A *q){__builtin_clear_padding(p);__builtin_clear_padding(b);__builtin_clear_padding(q);}\n"
        "int tested(int n){h=(unsigned)n;T(struct S v[2],&v);T(A v,&v);T(TS v[2],&v);T(struct S v[2];struct S (*p)[2]=&v,p);T(struct S v[2];__typeof__(v) *q=&v,q);\n"
        "T(struct S v[2];__typeof__(struct S[2]) *q=&v,q);T(struct M v,&v);T(struct Q v,&v);T(A v[2],&v);T(A v[3],&v[1]);T(struct S v[3][2],&v[1]);\n"
        "T(struct S v[2];PA p=&v,p);T(A v;PTA p=&v,p);T(struct S v[2];struct S (*p)[2]=&v;struct S (**pp)[2]=&p,*pp);T(struct S v[2];struct S ((*p))[2]=&v,p);\n"
        "struct S b[2];struct S d[2][2];A a;memset(b,0xff,sizeof b);memset(d,0xff,sizeof d);memset(a,0xff,sizeof a);param(&b,d,&a);bytes(b,sizeof b);bytes(d,sizeof d);bytes(a,sizeof a);\n"
        "return (int)(h^(h>>32));}\n",
        "generic-clear-padding-sugar", clang);
    // __builtin_clear_padding of a type nested 90 levels (#1842), past the walk's first room, whose stack grows: before, past
    // 64 levels it was refused, and its frame written past the stack.
    check_test_program_source(test,
        "void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "struct L0{char c;int i;};struct L1{char c;struct L0 in;short s;};struct L2{char c;struct L1 in;short s;};\n"
        "struct L3{char c;struct L2 in;short s;};struct L4{char c;struct L3 in;short s;};struct L5{char c;struct L4 in;short s;};\n"
        "struct L6{char c;struct L5 in;short s;};struct L7{char c;struct L6 in;short s;};struct L8{char c;struct L7 in;short s;};\n"
        "struct L9{char c;struct L8 in;short s;};struct L10{char c;struct L9 in;short s;};struct L11{char c;struct L10 in;short s;};\n"
        "struct L12{char c;struct L11 in;short s;};struct L13{char c;struct L12 in;short s;};struct L14{char c;struct L13 in;short s;};\n"
        "struct L15{char c;struct L14 in;short s;};struct L16{char c;struct L15 in;short s;};struct L17{char c;struct L16 in;short s;};\n"
        "struct L18{char c;struct L17 in;short s;};struct L19{char c;struct L18 in;short s;};struct L20{char c;struct L19 in;short s;};\n"
        "struct L21{char c;struct L20 in;short s;};struct L22{char c;struct L21 in;short s;};struct L23{char c;struct L22 in;short s;};\n"
        "struct L24{char c;struct L23 in;short s;};struct L25{char c;struct L24 in;short s;};struct L26{char c;struct L25 in;short s;};\n"
        "struct L27{char c;struct L26 in;short s;};struct L28{char c;struct L27 in;short s;};struct L29{char c;struct L28 in;short s;};\n"
        "struct L30{struct L29 in[2];char t;};struct L31{char c;struct L30 in;short s;};struct L32{char c;struct L31 in;short s;};\n"
        "struct L33{char c;struct L32 in;short s;};struct L34{char c;struct L33 in;short s;};struct L35{char c;struct L34 in;short s;};\n"
        "struct L36{char c;struct L35 in;short s;};struct L37{char c;struct L36 in;short s;};struct L38{char c;struct L37 in;short s;};\n"
        "struct L39{char c;struct L38 in;short s;};struct L40{char c;struct L39 in;short s;};struct L41{char c;struct L40 in;short s;};\n"
        "struct L42{char c;struct L41 in;short s;};struct L43{char c;struct L42 in;short s;};struct L44{char c;struct L43 in;short s;};\n"
        "struct L45{char c;struct L44 in;short s;};struct L46{char c;struct L45 in;short s;};struct L47{char c;struct L46 in;short s;};\n"
        "struct L48{char c;struct L47 in;short s;};struct L49{char c;struct L48 in;short s;};struct L50{char c;struct L49 in;short s;};\n"
        "struct L51{char c;struct L50 in;short s;};struct L52{char c;struct L51 in;short s;};struct L53{char c;struct L52 in;short s;};\n"
        "struct L54{char c;struct L53 in;short s;};struct L55{char c;struct L54 in;short s;};struct L56{char c;struct L55 in;short s;};\n"
        "struct L57{char c;struct L56 in;short s;};struct L58{char c;struct L57 in;short s;};struct L59{char c;struct L58 in;short s;};\n"
        "struct L60{struct L59 in[2];char t;};struct L61{char c;struct L60 in;short s;};struct L62{char c;struct L61 in;short s;};\n"
        "struct L63{char c;struct L62 in;short s;};struct L64{char c;struct L63 in;short s;};struct L65{char c;struct L64 in;short s;};\n"
        "struct L66{char c;struct L65 in;short s;};struct L67{char c;struct L66 in;short s;};struct L68{char c;struct L67 in;short s;};\n"
        "struct L69{char c;struct L68 in;short s;};struct L70{char c;struct L69 in;short s;};struct L71{char c;struct L70 in;short s;};\n"
        "struct L72{char c;struct L71 in;short s;};struct L73{char c;struct L72 in;short s;};struct L74{char c;struct L73 in;short s;};\n"
        "struct L75{char c;struct L74 in;short s;};struct L76{char c;struct L75 in;short s;};struct L77{char c;struct L76 in;short s;};\n"
        "struct L78{char c;struct L77 in;short s;};struct L79{char c;struct L78 in;short s;};struct L80{char c;struct L79 in;short s;};\n"
        "struct L81{char c;struct L80 in;short s;};struct L82{char c;struct L81 in;short s;};struct L83{char c;struct L82 in;short s;};\n"
        "struct L84{char c;struct L83 in;short s;};struct L85{char c;struct L84 in;short s;};struct L86{char c;struct L85 in;short s;};\n"
        "struct L87{char c;struct L86 in;short s;};struct L88{char c;struct L87 in;short s;};struct L89{char c;struct L88 in;short s;};\n"
        "static struct L89 deep;int tested(int n){h=(unsigned)n;memset(&deep,0xff,sizeof deep);__builtin_clear_padding(&deep);\n"
        "for(unsigned long i=0;i<sizeof deep;i++)mix(((const unsigned char*)&deep)[i]);return (int)(h^(h>>32));}\n",
        "generic-clear-padding-deep", clang);
    // va_arg of a Microsoft list (#148, clang's EmitMSVAArg) against clang's build: 8-byte slots built by hand, read as an
    // int, a float and a double, structs of 3, 4, 8 and 16 bytes (the 3- and 16-byte ones through their slots' pointers),
    // a long double, a _Complex double and an __int128 through theirs, then __builtin_ms_va_copy into a list through a
    // pointer, __builtin_ms_va_end, and va_arg through that pointer and of the copy, whose place is the last value.
    check_test_program_source(test,
        "void *memcpy(void *, const void *, unsigned long); static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "struct S3 { char c[3]; }; struct S4 { short a, b; }; struct S8 { int a, b; }; struct S16 { long a, b; };\n"
        "static unsigned long long slots[16]; static struct S3 s3 = { { 1, 2, 3 } }; static struct S16 s16 = { 11, 12 };\n"
        "static long double ld = 2.5L; static _Complex double cd = 1.0 + 2.0i; static __int128 wide = ((__int128)7 << 64) | 9;\n"
        "static double take(char** list) { return __builtin_va_arg(*list, double); }\n"
        "static void copy_end(__builtin_ms_va_list* to, __builtin_ms_va_list from) { __builtin_ms_va_copy(*to, from); __builtin_ms_va_end(from); }\n"
        "int tested(int n) { h = (unsigned)n; struct S4 s4 = { 5, 6 }; struct S8 s8 = { 7, 8 }; float f = 1.5f; double d = 3.25;\n"
        "unsigned long long k = 0; slots[k++] = (unsigned)n; memcpy(&slots[k++], &f, sizeof f); memcpy(&slots[k++], &d, sizeof d);\n"
        "slots[k++] = (unsigned long long)&s3; memcpy(&slots[k++], &s4, sizeof s4); memcpy(&slots[k++], &s8, sizeof s8);\n"
        "slots[k++] = (unsigned long long)&s16; slots[k++] = (unsigned long long)&ld; slots[k++] = (unsigned long long)&cd;\n"
        "slots[k++] = (unsigned long long)&wide; slots[k++] = (unsigned long long)(long long)-3; memcpy(&slots[k++], &d, sizeof d);\n"
        "slots[k++] = 0x1234; char* ap = (char*)slots; __builtin_ms_va_list copy; mix((unsigned)__builtin_va_arg(ap, int));\n"
        "mix((unsigned long long)(__builtin_va_arg(ap, float) * 4)); mix((unsigned long long)(__builtin_va_arg(ap, double) * 4));\n"
        "struct S3 r3 = __builtin_va_arg(ap, struct S3); mix(r3.c[0] + r3.c[1] * 10 + r3.c[2] * 100); struct S4 r4 = __builtin_va_arg(ap, struct S4);\n"
        "mix(r4.a * 10 + r4.b); struct S8 r8 = __builtin_va_arg(ap, struct S8); mix(r8.a * 10 + r8.b);\n"
        "struct S16 r16 = __builtin_va_arg(ap, struct S16); mix(r16.a * 100 + r16.b);\n"
        "mix((unsigned long long)(__builtin_va_arg(ap, long double) * 8)); _Complex double rc = __builtin_va_arg(ap, _Complex double);\n"
        "mix((unsigned long long)(__real__ rc * 10 + __imag__ rc)); __int128 rw = __builtin_va_arg(ap, __int128);\n"
        "mix((unsigned long long)(rw >> 64) * 100 + (unsigned long long)rw); mix((unsigned long long)__builtin_va_arg(ap, long long));\n"
        "copy_end(&copy, ap); mix((unsigned long long)(take(&copy) * 4)); mix((unsigned)__builtin_va_arg(copy, short));\n"
        "mix((unsigned long long)(copy - (char*)slots)); return (int)(h ^ (h >> 32)); }\n",
        "generic-microsoft-va-arg", clang);
    // __builtin_infer_alloc_token (#148) against clang's build: the tokens of scalars, pointers, arrays, function pointers,
    // records with and without pointers (through members, nested records and uintptr_t's typedefs), typedef-named and
    // nested records, an atomic and a qualified type, by sizeof's type or its expression's, through arithmetic and a
    // variable's initializer, and the fallback 0 of no type; arguments never run. (An unnamed record's name holds its file's
    // path, which differs between this build's source and clang's.)
    check_test_program_source(test,
        "typedef unsigned long uintptr_t; typedef uintptr_t handle; static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "struct I; struct P { struct I* p; }; struct Q { int a[3]; long b; }; typedef struct { char c; } T; enum E { E0 }; union U { int a; float b; };\n"
        "struct R { struct R* next; }; struct W { handle u; }; struct O { struct N { int x; } n; struct { char c; } a; }; struct V { struct Q q[2]; struct W w; };\n"
        "int tested(int n) { int i = n; unsigned long s = sizeof(struct Q) * 2; h = (unsigned)n; mix(__builtin_infer_alloc_token(i++));\n"
        "mix(__builtin_infer_alloc_token(sizeof(struct I*))); mix(__builtin_infer_alloc_token(sizeof(struct P))); mix(__builtin_infer_alloc_token(s));\n"
        "mix(__builtin_infer_alloc_token(3 + sizeof(T))); mix(__builtin_infer_alloc_token(sizeof(enum E))); mix(__builtin_infer_alloc_token(sizeof(union U)));\n"
        "mix(__builtin_infer_alloc_token(sizeof(struct R))); mix(__builtin_infer_alloc_token(sizeof(uintptr_t))); mix(__builtin_infer_alloc_token(sizeof(struct W)));\n"
        "mix(__builtin_infer_alloc_token(sizeof(int[4]))); mix(__builtin_infer_alloc_token(sizeof(int (*)(int)))); mix(__builtin_infer_alloc_token(sizeof(const char*)));\n"
        "mix(__builtin_infer_alloc_token(sizeof i)); mix(__builtin_infer_alloc_token(4, sizeof(double))); mix(__builtin_infer_alloc_token(sizeof(_Atomic(int))));\n"
        "mix(__builtin_infer_alloc_token(sizeof(volatile int))); mix(__builtin_infer_alloc_token(sizeof(struct N)));\n"
        "mix(__builtin_infer_alloc_token(n * sizeof(struct V) << 1)); mix(__builtin_infer_alloc_token((sizeof(struct V) / 2) - 1)); mix((unsigned)i);\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-infer-alloc-token", clang);
    // __builtin_os_log_format's buffers against clang's build (#148, the bytes of clang's helper's stores): privacy
    // annotations (sensitive over a later public), scalars of each promoted size, %m and %%, a string's count from its
    // precision's argument and from a constant, %.8P, masks, a field width and a precision of arguments, a double, a
    // pointer, and an array (its address, compared in place, since the two builds place it apart); buffer sizes of
    // positions, of a %P without its length and of missing data; and escaped percent signs, with specifiers past the
    // format's NUL that take nothing (#1329).
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static unsigned char buffer[512]; static char arr[4] = { 1, 2, 3, 4 };\n"
        "static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); } int tested(int n) { h = (unsigned)n;\n"
        "const char* s = (const char*)(unsigned long)(0x1000 + n);\n"
        "char c = (char)(n + 3); short w = (short)(n * 7); long long q = -n; float f = 1.5f * n; double d = 0.25 * n;\n"
        "void* r = __builtin_os_log_format(buffer, \"%d %s %{public}s\", n, s, s);\n"
        "mix(r == (void*)buffer); bytes(__builtin_os_log_format_buffer_size(\"%d %s %{public}s\", n, s, s));\n"
        "__builtin_os_log_format(buffer, \"%{private}c %hd %lld %f %{sensitive, public}lu\", c, w, q, f, (unsigned long)n);\n"
        "bytes(__builtin_os_log_format_buffer_size(\"%{private}c %hd %lld %f %{sensitive, public}lu\", c, w, q, f, (unsigned long)n));\n"
        "__builtin_os_log_format(buffer, \"%m %% %.*s %.8P %{mask.abc}d %*.*d\", 3, s, s, n, 4, 2, n);\n"
        "bytes(__builtin_os_log_format_buffer_size(\"%m %% %.*s %.8P %{mask.abc}d %*.*d\", 3, s, s, n, 4, 2, n));\n"
        "__builtin_os_log_format(buffer, \"%{public,mask.xy}.16s %e %p %s\", s, d, (void*)s, arr);\n"
        "unsigned long size = __builtin_os_log_format_buffer_size(\"%{public,mask.xy}.16s %e %p %s\", s, d, (void*)s, arr);\n"
        "unsigned long v = 0; for (int i = 0; i < 8; i++) v |= (unsigned long)buffer[size - 8 + i] << (8 * i);\n"
        "mix(v == (unsigned long)arr); bytes(size - 8);\n"
        "mix(__builtin_os_log_format_buffer_size(\"plain\") + __builtin_os_log_format_buffer_size(\"%2$d %1$s\", s, n) * 100 +\n"
        "__builtin_os_log_format_buffer_size(\"%P %d\", n, n) * 10000 + __builtin_os_log_format_buffer_size(\"%d %d\", n) * 1000000);\n"
        "__builtin_os_log_format(buffer, \"%%%% %{public}d %%\\0%s %d\", n); bytes(__builtin_os_log_format_buffer_size(\"%%%% %{public}d %%\\0%s %d\", n));\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-os-log", clang);
    // __builtin_os_log_format's data in its items' order (#148, as clang's emitBuiltinOSLogFormat evaluates it): positions
    // that take the data out of order (their calls' order checked), data no item takes (its call never made), data taken
    // again where no side effect ran since (a field width's and a precision's argument too); and data of 16 bytes or of
    // 2, unpromoted: a long double's 10 bytes then zeros, an __int128, a 16-byte vector, _Float16 and __bf16 (an __fp16 as
    // a double), and signed _BitInts narrower than their classes, zero-extended from their widths, as clang stores them.
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static unsigned char buffer[512]; static int order[8]; static int made;\n"
        "static int next(int v) { order[made++ & 7] = v; return v; }\n"
        "static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); }\n"
        "typedef float V4 __attribute__((vector_size(16))); typedef short V2 __attribute__((vector_size(4)));\n"
        "int tested(int n) { h = (unsigned)n;\n"
        "__builtin_os_log_format(buffer, \"%3$d %1$d %2$d\", next(n), next(n + 1), next(n + 2));\n"
        "bytes(__builtin_os_log_format_buffer_size(\"%3$d %1$d %2$d\", 1, 2, 3)); for (int i = 0; i < made; i++) mix(order[i]); made = 0;\n"
        "__builtin_os_log_format(buffer, \"%d\", n, next(5)); bytes(__builtin_os_log_format_buffer_size(\"%d\", 1, 2)); mix(made);\n"
        "__builtin_os_log_format(buffer, \"%1$d %1$d %2$d\", n * 3, next(n)); bytes(__builtin_os_log_format_buffer_size(\"%1$d %1$d %2$d\", 1, 2));\n"
        "__builtin_os_log_format(buffer, \"%2$*1$d %2$.*1$d\", n, n + 7); bytes(__builtin_os_log_format_buffer_size(\"%2$*1$d %2$.*1$d\", 1, 1));\n"
        "long double l = -1.5L * n; __int128 w = -(__int128)n << 70; V4 v = { 1.0f, 2.0f, -1.0f, (float)n }; V2 s = { (short)-n, 7 };\n"
        "__builtin_os_log_format(buffer, \"%Lf %d %d %d\", l, w, v, s); bytes(__builtin_os_log_format_buffer_size(\"%Lf %d %d %d\", l, w, v, s));\n"
        "_Float16 f = (_Float16)(n * 0.5); __bf16 b = (__bf16)(n * 0.25f); __fp16 p = (__fp16)n;\n"
        "__builtin_os_log_format(buffer, \"%f %f %f\", f, b, p); bytes(__builtin_os_log_format_buffer_size(\"%f %f %f\", f, b, p));\n"
        "_BitInt(24) a = -n; _BitInt(7) c = -n; _BitInt(72) d = -n; _BitInt(40) e = -n; unsigned _BitInt(24) u = (unsigned _BitInt(24))-n;\n"
        "__builtin_os_log_format(buffer, \"%d %d %d %d %d\", a, c, d, e, u); bytes(__builtin_os_log_format_buffer_size(\"%d %d %d %d %d\", a, c, d, e, u));\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-os-log-order", clang);
    // __builtin_os_log_format's data evaluated once an item (#148, as clang's emitBuiltinOSLogFormat evaluates each item's
    // expression): data taken again after a side effect ran, and an item's call made again; and a __float128's 16 bytes
    // and a 2-byte vector's, read as they are evaluated.
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static unsigned char buffer[512]; static int made;\n"
        "static int next(int v) { made++; return v + made; }\n"
        "static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); }\n"
        "typedef char V2 __attribute__((vector_size(2)));\n"
        "int tested(int n) { h = (unsigned)n; int g = n;\n"
        "__builtin_os_log_format(buffer, \"%1$d %2$d %1$d\", g, next(n)); bytes(__builtin_os_log_format_buffer_size(\"%1$d %2$d %1$d\", 1, 2));\n"
        "__builtin_os_log_format(buffer, \"%1$d %2$d %1$d\", made, next(n)); bytes(__builtin_os_log_format_buffer_size(\"%1$d %2$d %1$d\", 1, 2));\n"
        "__builtin_os_log_format(buffer, \"%1$d %1$d %1$d\", next(n)); bytes(__builtin_os_log_format_buffer_size(\"%1$d %1$d %1$d\", 1)); mix(made);\n"
        "__float128 q = (__float128)n / 3; V2 v = { (char)n, (char)-n };\n"
        "__builtin_os_log_format(buffer, \"%f %d %2$d %1$f\", q, v); bytes(__builtin_os_log_format_buffer_size(\"%f %d %2$d %1$f\", q, v));\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-os-log-reevaluated", clang);
    // __builtin_os_log_format past the operands' 32-bit mask (#1785, clang's BuiltinOSLogFormat takes up to 255 data
    // arguments): 40 data arguments, those past the 32nd of other types and one a call of a static function nothing else
    // names (so the program reaches it through the full mask), positions past the 32nd, and 255 data arguments, the most
    // clang takes, each with its buffer's size.
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static unsigned char buffer[2048]; static int made;\n"
        "static int late(int v) { made++; return v * 5 + made; }\n"
        "static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); }\n"
        "#define F8 \"%d %d %d %d %d %d %d %d \"\n#define A8(x) x, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7\n"
        "#define F32 F8 F8 F8 F8\n#define A32(x) A8(x), A8(x + 8), A8(x + 16), A8(x + 24)\n"
        "#define F255 F32 F32 F32 F32 F32 F32 F32 F8 F8 F8 \"%d %d %d %d %d %d %d\"\n"
        "int tested(int n) { h = (unsigned)n; long long q = -n; double d = 0.5 * n;\n"
        "__builtin_os_log_format(buffer, F32 \"%d %lld %f %{private}d %hhd %hd %d %d\", A32(n), late(n), q, d, n * 3, (char)n, (short)-n, made, n);\n"
        "bytes(__builtin_os_log_format_buffer_size(F32 \"%d %lld %f %{private}d %hhd %hd %d %d\", A32(n), late(n), q, d, n * 3, (char)n, (short)-n, made, n));\n"
        "__builtin_os_log_format(buffer, \"%40$d %33$d %1$d %35$f\", A32(n), late(n), 0, d, 0, 0, 0, 0, n - 9);\n"
        "bytes(__builtin_os_log_format_buffer_size(\"%40$d %33$d %1$d %35$f\", A32(n), late(n), 0, d, 0, 0, 0, 0, n - 9)); mix(made);\n"
        "__builtin_os_log_format(buffer, F255, A32(n), A32(n + 1), A32(n + 2), A32(n + 3), A32(n + 4), A32(n + 5), A32(n + 6),\n"
        "A8(n), A8(-n), A8(n * 2), n, n + 1, n + 2, n + 3, n + 4, n + 5, late(n));\n"
        "unsigned long size = __builtin_os_log_format_buffer_size(F255, A32(0), A32(1), A32(2), A32(3), A32(4), A32(5), A32(6),\n"
        "A8(0), A8(1), A8(2), 0, 1, 2, 3, 4, 5, 6); mix(size); bytes(size); mix(made);\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-os-log-many", clang);
    // __builtin_dump_struct (#148): clang's calls of the printer, a line each, hashed by a printer that reads each format's
    // operands as its conversions say (a pointer as its offset from the record): the record's name, each field's type
    // name as its declaration spells it (typedefs kept, an unnamed record's "struct (unnamed)"), its name, a bit-field's
    // width, and its value promoted (bool, the character types, the integers, size_t's %zu, an enum's underlying type,
    // the floating types, strings of at most 32 characters, pointers) or its address (an array, an __int128, a complex);
    // nested records' fields deeper, anonymous members' flattened, unnamed bit-fields skipped; the printer and the rest
    // of the arguments evaluated again for each call (a call through a pointer, the rest's side effects), the record
    // once; a union's dump, a nested record's and a typedef'd unnamed record's.
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static const char* base;\n"
        "static int pr(int tag, const char* f, ...) { __builtin_va_list ap; __builtin_va_start(ap, f); mix((unsigned)tag);\n"
        "for (const char* p = f; *p; p++) { mix((unsigned char)*p); if (*p != '%') continue; p++;\n"
        "while (*p == '.' || (*p >= '0' && *p <= '9')) { mix((unsigned char)*p); p++; }\n"
        "int l = 0, L = 0; while (*p == 'h' || *p == 'l' || *p == 'z' || *p == 'j' || *p == 't' || *p == 'L') { l += *p == 'l' || *p == 'z' || *p == 'j' || *p == 't'; L |= *p == 'L'; mix((unsigned char)*p); p++; }\n"
        "mix((unsigned char)*p);\n"
        "if (*p == 's') { const char* s = __builtin_va_arg(ap, const char*); for (int i = 0; s && s[i] && i < 32; i++) mix((unsigned char)s[i]); }\n"
        "else if (*p == 'p') { const char* q = __builtin_va_arg(ap, const char*); mix(q ? (unsigned long long)(q - base) : 7777); }\n"
        "else if (*p == 'f' && L) { long double d = __builtin_va_arg(ap, long double); mix((unsigned long long)(long long)(d * 1000)); }\n"
        "else if (*p == 'f') { double d = __builtin_va_arg(ap, double); mix((unsigned long long)(long long)(d * 1000)); }\n"
        "else if (l) mix((unsigned long long)__builtin_va_arg(ap, long)); else mix((unsigned long long)__builtin_va_arg(ap, int)); }\n"
        "__builtin_va_end(ap); return 0; }\n"
        "typedef unsigned long size_t; typedef signed char i8; typedef struct { int q; short r; } Anon; enum Color { RED, GREEN }; enum Neg { MINUS = -1 };\n"
        "struct In { int x; long y; };\n"
        "struct S { _Bool b; char c; i8 sc; unsigned char uc; short s; unsigned short us; int i; unsigned u; long l; unsigned long long ull;\n"
        "float f; double d; long double ld; const char* str; char* mstr; void* vp; int* np; int arr[2]; size_t z; enum Color col; enum Neg ng;\n"
        "struct In in; Anon an; int bf : 3; unsigned ubf : 5; int : 4; _Bool bb : 1; long long lbf : 40; enum Color cbf : 2;\n"
        "struct { int ia; union { int ib; float fc; }; }; struct { int named_in; } named; union U { int ux; char uy; } un; __int128 w; _Complex double cd; };\n"
        "static int calls; static int next(void) { return calls++; }\n"
        "int tested(int n) { h = (unsigned)n; calls = 0; struct S s = { 0 }; base = (const char*)&s;\n"
        "s.b = n & 1; s.c = (char)-n; s.sc = (i8)(n - 100); s.uc = (unsigned char)(200 + n); s.s = (short)(-300 * n); s.us = (unsigned short)(65000 + n);\n"
        "s.i = -7 * n; s.u = 4000000000u + (unsigned)n; s.l = -8L * n; s.ull = 11ULL << n; s.f = 1.5f * n; s.d = -2.25 * n; s.ld = 3.125L * n;\n"
        "s.str = \"hello\"; s.mstr = \"a string longer than thirty-two characters for sure\"; s.vp = &s.i; s.z = (size_t)n * 3; s.col = GREEN; s.ng = MINUS;\n"
        "s.in.x = n; s.in.y = -n; s.an.q = 3; s.an.r = (short)n; s.bf = -2; s.ubf = (unsigned)n & 31; s.bb = 1; s.lbf = -12345678901LL * n; s.cbf = GREEN;\n"
        "s.ia = 13; s.ib = n; s.named.named_in = 15; s.un.ux = 0x41 + n;\n"
        "__builtin_dump_struct(&s, pr, n);\n"
        "int (*volatile through)(int, const char*, ...) = pr; __builtin_dump_struct(&s.in, through, next());\n"
        "Anon a = { n, 6 }; __builtin_dump_struct(&a, pr, next() * 10);\n"
        "union U u = { n }; __builtin_dump_struct(&u, pr, 3);\n"
        "__builtin_dump_struct(&s.named, pr, 4); mix((unsigned)calls);\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-dump-struct", clang);
    // __builtin_invoke (#148): calls through it, of a function, a pointer, a member and a variadic function, each argument
    // evaluated once, as the call's.
    check_test_program_source(test,
        "static int calls;static int next(int v){calls++;return v;}static int twice(int x){return 2*x;}static int sum(int n,...){return n;}\n"
        "struct S{int (*fn)(int);};\n"
        "int tested(int n){int (*p)(int)=twice;struct S s={twice};return __builtin_invoke(twice,next(n))+__builtin_invoke(p,3)*10+__builtin_invoke(*p,1)*100+\n"
        "__builtin_invoke(s.fn,next(2))*1000+__builtin_invoke(sum,7,1.5,\"x\")*10000+calls;}\n",
        "generic-invoke", clang);
    // clang's type traits of C (#148), their values in code: of complete, incomplete and variably modified types.
    check_test_program_source(test,
        "struct T{int x;};\n"
        "int tested(int n){int k=n>0?n:1;return __is_destructible(int)+__is_destructible(void)*2+__is_destructible(int[])*4+__is_destructible(int[k])*8+\n"
        "__is_nothrow_destructible(struct T)*16+__is_bitwise_cloneable(int[])*32+__is_bitwise_cloneable(void(void))*64+__is_bitwise_cloneable(void)*128+\n"
        "__is_bitwise_cloneable(int[k][2])*256+n*1000;}\n",
        "generic-type-traits", clang);
    // __builtin_vectorelements of a comma expression (#1126's review), its last operand's type, none of its operands
    // evaluated (a call among them).
    check_test_program_source(test,
        "typedef int A __attribute__((ext_vector_type(2)));typedef int B __attribute__((ext_vector_type(4)));typedef double C __attribute__((vector_size(32)));\n"
        "static int calls;static A side(void){calls++;A a={0};return a;}\n"
        "int tested(int n){A a={n};B b={n};C c={0};unsigned long r=__builtin_vectorelements(a,b)*1000+__builtin_vectorelements(b,a)*100+\n"
        "__builtin_vectorelements(side(),b,c)*10+__builtin_vectorelements(B);return (int)r+calls*100000+n;}\n",
        "generic-vectorelements-of-comma-expressions", clang);
    // A place through macros (#1126's review): clang's SourceLocExpr reads getExpansionRange(...).getEnd(), the outermost
    // expansion's end: a function-like invocation's closing parenthesis (on a later line too), an object-like macro's
    // name, a nested one's outer invocation, an argument's invocation (of a builtin and of another macro), in constant
    // expressions and at run time, before and after a #line. (The first #line keeps the lines the oracle's added macro
    // would shift.)
    check_test_program_source(test,
        "#line 1 \"macro-places.c\"\n"
        "#define COLUMN() __builtin_COLUMN()\n"
        "#define LINE() __builtin_LINE()\n"
        "#define WRAPPED() COLUMN()\n"
        "#define OBJECT __builtin_COLUMN()\n"
        "#define WRAPPEDARG(x) COLUMN()\n"
        "#define ID(x) x\n"
        "#define WHERE() (__builtin_LINE() * 1000 + __builtin_COLUMN())\n"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "enum{S=COLUMN()*1000+WRAPPED(),T=OBJECT*1000+WRAPPEDARG(1),U=ID(LINE()\n)};\n"
        "int tested(int n){h=(unsigned)n;mix(S);mix(T);mix(U);mix(COLUMN());mix(WRAPPED());mix(OBJECT);mix(WRAPPEDARG(42));\n"
        "mix(ID(__builtin_COLUMN()));mix(ID(COLUMN()));mix(LINE(\n));mix(ID(\n  __builtin_LINE()));mix(WHERE(\n   ));\n"
        "#line 300 \"moved.c\"\n"
        "mix(WRAPPED());mix(LINE());mix(ID(WRAPPED()));mix(WHERE());mix(OBJECT);\n"
        "return (int)(h^(h>>32));}\n",
        "generic-source-places-through-macros", clang);
    // __builtin_complex (#148): its folds in static data (signed zeros, an infinity, a NaN, a const object's value, long
    // double's and _Float16's) and in an enumerator, and its values at run time, of each real floating type, from
    // qualified, aligned and volatile operands, evaluated once each, in order, and taken apart, multiplied and summed.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "static void mixd(double d){unsigned long long b;__builtin_memcpy(&b,&d,8);mix(b);}static const double c=1.5;"
        "static _Complex double zd=__builtin_complex(1.0,-0.0);static _Complex float zf=__builtin_complex(__builtin_inff(),2.5f);"
        "static _Complex long double zl=__builtin_complex(3.0L,4.0L);static _Complex double zc=__builtin_complex(c,2.0);"
        "static _Complex _Float16 zh=__builtin_complex((_Float16)1.5,(_Float16)-2);"
        "static _Complex double zn=__builtin_complex(__builtin_nan(\"\"),-__builtin_inf());enum{E=(int)__imag__ __builtin_complex(3.0,7.0)};"
        "static int order;static double at(double v,int t){order=order*10+t;return v;}typedef double D __attribute__((aligned(16)));"
        "int tested(int n){h=(unsigned)n;mixd(__real__ zd);mixd(__imag__ zd);mixd(__real__ zf);mixd(__imag__ zf);"
        "mixd((double)__real__ zl);mixd((double)__imag__ zl);mixd(__real__ zc);mixd(__imag__ zc);mixd((double)__real__ zh);mixd((double)__imag__ zh);"
        "mix(__real__ zn!=__real__ zn);mixd(__imag__ zn);mix(E);double x=n*0.5,y=-n;_Complex double a=__builtin_complex(x,y);mixd(__real__ a);mixd(__imag__ a);"
        "_Complex double b=__builtin_complex(at(x,1),at(y,2))*__builtin_complex(y,x);mixd(__real__ b);mixd(__imag__ b);mix((unsigned)order);"
        "float fx=(float)n;volatile float vf=2.0f;_Complex float f=__builtin_complex(fx,vf);mixd(__real__ f);mixd(__imag__ f);"
        "long double lx=n*3.0L;_Complex long double l=__builtin_complex(lx,-lx);mixd((double)__real__ l);mixd((double)__imag__ l);"
        "D dd=n+0.25;const double cd=-n;_Complex double g=__builtin_complex(dd,cd);mixd(__real__ g);mixd(__imag__ g);"
        "_Float16 hx=(_Float16)n;_Complex _Float16 hh=__builtin_complex(hx,(_Float16)0.5);mixd((double)__real__ hh);mixd((double)__imag__ hh);"
        "__bf16 bx=(__bf16)n;mixd((double)(float)__real__ __builtin_complex(bx,bx));mixd((double)(float)__imag__ __builtin_complex(bx,(__bf16)3));"
        "__float128 qx=n;_Complex __float128 qq=__builtin_complex(qx,qx*2);mixd((double)__real__ qq);mixd((double)__imag__ qq);"
        "mix(sizeof(__builtin_complex(1.0f,2.0f)));mix(_Generic(__builtin_complex(1.0,2.0),_Complex double:1,default:2));"
        "_Complex double s[2]={__builtin_complex(x,1.0),__builtin_complex(2.0,y)};mixd(__real__ s[1]);mixd(__imag__ s[0]);"
        "mixd(__imag__ (__builtin_complex(x,y)+1.0));return (int)(h^(h>>32));}",
        "generic-complex", clang);
    // __arithmetic_fence's values of each floating type, a complex one and a vector, and in static data; the xray events
    // and __warn_memset_zero_len, whose arguments never run; and
    // trivially_relocate's moves, overlapping ones among them, of ints, doubles and arrays of ints, with its destination.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "static void mixd(double d){unsigned long long b;__builtin_memcpy(&b,&d,8);mix(b);}"
        "static int counter;static int bump(void){return ++counter;}"
        "static const double c=2.5;static double fenced=__arithmetic_fence(c)+__arithmetic_fence(0.5);"
        "typedef float V __attribute__((vector_size(16)));"
        "int tested(int n){h=(unsigned)n;mixd(fenced);double x=n*0.75;float y=(float)n/3;long double l=n*1.25L;_Float16 hf=(_Float16)n;"
        "mixd(__arithmetic_fence(x+1.0));mixd(__arithmetic_fence(y));mixd((double)__arithmetic_fence(l));mixd((double)__arithmetic_fence(hf));"
        "_Complex double z=__arithmetic_fence(x+2.0i);mixd(__real__ z);mixd(__imag__ z);V v={y,1,2,3};V w=__arithmetic_fence(v);mixd(w[0]);mixd(w[3]);"
        "mix(_Generic(__arithmetic_fence(y),float:1,default:2));"
        "counter=0;__xray_customevent((const char*)(long)bump(),(unsigned long)bump());__xray_typedevent(bump(),\"x\",bump());__warn_memset_zero_len();"
        "mix((unsigned)counter);"
        "int a[8];for(int i=0;i<8;i++)a[i]=i*n+1;int* pa=a;int* r=__builtin_trivially_relocate(pa+2,pa,(unsigned long)(n&3)+3);mix(r==a+2);for(int i=0;i<8;i++)mix((unsigned)a[i]);"
        "double d[4]={1,2,3,4},e[4];double* pe=e;double* pd=d;double* q=__builtin_trivially_relocate(pe,pd,4.0);mix(q==e);for(int i=0;i<4;i++)mixd(e[i]);"
        "int m[2][3]={{1,2,3},{4,5,n}},k[2][3];int (*pk)[3]=k;int (*pm)[3]=m;__builtin_trivially_relocate(pk,pm,2);for(int i=0;i<2;i++)for(int j=0;j<3;j++)mix((unsigned)k[i][j]);"
        "long s[3]={n,2,3};long* t=s;__builtin_trivially_relocate(t+1,t,bump());mix((unsigned long)s[2]);mix((unsigned)counter);"
        "return (int)(h^(h>>32));}"
        , "generic-fence-events-and-relocate", clang);
}

// __builtin_cpu_init with libgcc's model, of this host, against clang's build (the table's every name is
// test-checker-driver's, c_check_library_driver_test.c's check_test_cpu_model_runs).
static __attribute__((noinline)) void check_test_generic_cpu_programs(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "int tested(int n){h=(unsigned)n;__builtin_cpu_init();mix(__builtin_cpu_supports(\"sse2\"));mix(__builtin_cpu_supports(\"avx2\"));"
        "mix(__builtin_cpu_supports(\"avx512f\"));mix(__builtin_cpu_supports(\"x86-64-v3\"));mix(__builtin_cpu_supports(\"x86-64-v4\"));"
        "mix(__builtin_cpu_is(\"intel\"));mix(__builtin_cpu_is(\"amd\"));mix(__builtin_cpu_is(\"znver5\"));mix(__builtin_cpu_is(\"amdfam1ah\"));"
        "return (int)(h^(h>>32));}",
        "generic-cpu-init", clang);
}

static __attribute__((noinline)) void check_test_generic_builtins(CheckTest* test, const char* clang)
{
    check_test_generic_builtin_sources(test, clang);
    check_test_generic_builtin_programs(test, clang);
    check_test_generic_cpu_programs(test, clang);
}

#endif
