// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Transparent unions' arguments, parameters and va_arg (#147), which clang passes as each union's first member
// (useFirstFieldIfTransparentUnion), run against clang -O0's build of the same programs: their calls of the harness's
// functions (check_test_program_harness's, clang's code), the harness's calls of theirs through pointers and their own
// calls of each other, with each kind of member value a conversion to a union selects (check_lower_transparent_value)
// and the unions' own values, named and unnamed, in registers and on the stack; and what the lowering refuses by name.
// Include after c_check_program_test.c's source and execution helpers.

// Calls of the harness's function with each union's members and values; a definition of the program's that takes them
// and passes them on, called by the program and by the harness through a pointer; the registers a short, an unsigned
// short and a _Bool arrive in, which clang's caller extends as their types (signext, zeroext); and an _Atomic union's
// argument, which passes as its atomic type.
static const char check_test_program_transparent_calls[] =
    BUSTER_CHECK_TEST_TRANSPARENT_UNIONS
    "unsigned long external_transparent(int,ExternalTF,ExternalTS,ExternalTP,ExternalTQ,ExternalTD,ExternalTW,ExternalTA,ExternalTB);"
    "unsigned long external_transparent_apply(unsigned long(*)(int,ExternalTF,ExternalTS,ExternalTP,ExternalTQ,ExternalTD,ExternalTW,ExternalTA,ExternalTB),int);"
    "int external_short_bits(ExternalTS);int external_ushort_bits(ExternalTZ);int external_bool_bits(ExternalTO);"
    "unsigned long external_atomic_transparent(_Atomic ExternalTF);"
    "unsigned long mine(int k,ExternalTF f,ExternalTS s,ExternalTP p,ExternalTQ q,ExternalTD d,ExternalTW w,ExternalTA a,ExternalTB b){"
    "return external_transparent(k,f,s,p,q,d,w,a,b)*3+(unsigned long)(f.q.a+s.s+a.a[1]);}"
    "int tested(int n){struct external_ip ip={n,n*2};struct external_fp fp={n*0.5f,1.5f};struct external_dv dv={n*0.25};"
    "struct external_w ws={n,n+9};struct external_big big={n,n+1,n+2};struct external_big_bytes bytes={{1,2,n}};long x=n*5,y=n-4;"
    "ExternalTF tf={.q={n,3}};"
    "unsigned long h=external_transparent(0,ip,(short)-n,&x,&y,dv,(__int128)n*8,ip,big);"
    "h^=external_transparent(1,fp,(unsigned short)(n*3),(long)n*7,&y,(long)n*11,ws,ip,bytes);"
    "h^=external_transparent(3,tf,(short)n,(long)n,n*0.5,dv,(long double)n,ip,big);"
    "h^=mine(0,ip,(short)-n,&x,&y,dv,(__int128)n*8,ip,big);"
    "h^=mine(3,fp,(unsigned short)(n*3),(long)n,n*1.5,(long)n*13,ws,ip,bytes);"
    "h^=external_transparent_apply(mine,n);h^=external_atomic_transparent(tf);"
    "h+=(unsigned long)external_short_bits((short)-n)+(unsigned long)external_ushort_bits((unsigned short)-n)*3+(unsigned long)external_bool_bits((char)n)*5;"
    "return (int)(h%1000003);}";

// The unions as unnamed arguments, which pass as their first members too, and va_arg's reading of them: the program's
// calls of the harness's variadic function and of its own, and the harness's call of the program's through a pointer.
static const char check_test_program_transparent_variadic[] =
    BUSTER_CHECK_TEST_TRANSPARENT_UNIONS
    "unsigned long external_transparent_variadic(int,...);unsigned long external_transparent_variadic_apply(unsigned long(*)(int,...),int);"
    "static unsigned long fold(unsigned long h,unsigned long v){return (h^v)*1099511628211UL;}"
    "static unsigned long wide_bits(ExternalTW w){return (unsigned long)w.s.a+(unsigned long)(w.s.b&0xffff)*7;}"
    "unsigned long mine_variadic(int count,...){__builtin_va_list a;__builtin_va_start(a,count);unsigned long h=(unsigned long)count;"
    "for(int i=0;i<count;i++){ExternalTF f=__builtin_va_arg(a,ExternalTF);ExternalTS s=__builtin_va_arg(a,ExternalTS);"
    "ExternalTD d=__builtin_va_arg(a,ExternalTD);ExternalTW w=__builtin_va_arg(a,ExternalTW);ExternalTA r=__builtin_va_arg(a,ExternalTA);"
    "ExternalTB b=__builtin_va_arg(a,ExternalTB);h=fold(h,(unsigned long)(f.q.a+f.q.b*3+s.s*5));h=fold(h,(unsigned long)d.l);h=fold(h,wide_bits(w));"
    "h=fold(h,(unsigned long)(r.a[0]+r.a[1]*3));h=fold(h,(unsigned long)(b.s.a+b.s.b*3+b.s.c*5));}__builtin_va_end(a);return h;}"
    "int tested(int n){ExternalTF tf={.q={n,-n}};ExternalTS ts={.s=(short)(n-7)};ExternalTD td={.l=(long)n*1000003};"
    "ExternalTW tw={.w=(__int128)n*3};ExternalTA ta={.q={n,5}};ExternalTB tb={.s={n,2,3}};"
    "unsigned long h=external_transparent_variadic(2,tf,ts,td,tw,ta,tb,tf,ts,td,tw,ta,tb);"
    "h^=mine_variadic(2,tf,ts,td,tw,ta,tb,tf,ts,td,tw,ta,tb);h^=external_transparent_variadic_apply(mine_variadic,n);return (int)(h%1000003);}";

// The unions after 6 longs and 8 doubles, all on the stack: passed to the harness, received from it through a pointer
// and passed on.
static const char check_test_program_transparent_stack[] =
    BUSTER_CHECK_TEST_TRANSPARENT_UNIONS
    "unsigned long external_transparent_late(long,long,long,long,long,long,double,double,double,double,double,double,double,double,"
    "ExternalTF,ExternalTS,ExternalTP,ExternalTD,ExternalTW,ExternalTA,ExternalTB);"
    "unsigned long external_transparent_late_apply(unsigned long(*)(long,long,long,long,long,long,double,double,double,double,double,double,double,double,"
    "ExternalTF,ExternalTS,ExternalTP,ExternalTD,ExternalTW,ExternalTA,ExternalTB),int);"
    "unsigned long mine_late(long l0,long l1,long l2,long l3,long l4,long l5,double d0,double d1,double d2,double d3,double d4,double d5,double d6,"
    "double d7,ExternalTF f,ExternalTS s,ExternalTP p,ExternalTD d,ExternalTW w,ExternalTA a,ExternalTB b){"
    "return external_transparent_late(l0,l1,l2,l3,l4,l5,d0,d1,d2,d3,d4,d5,d6,d7,f,s,p,d,w,a,b)+(unsigned long)(f.q.a+s.s+a.a[1]);}"
    "int tested(int n){struct external_ip ip={n,7};"
    "unsigned long h=external_transparent_late(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,ip,(short)-n,(long)n*9,(long)n*3,(__int128)n*5,ip,(struct external_big){n,n,n});"
    "h^=mine_late(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,ip,(short)-n,(long)n*9,(long)n*3,(__int128)n*5,ip,(struct external_big){n,n,n});"
    "h^=external_transparent_late_apply(mine_late,n);return (int)(h%1000003);}";

static void check_test_program_transparent(CheckTest* test, const char* clang)
{
    check_test_program_source(test, check_test_program_transparent_calls, "transparent-union-calls", clang);
    check_test_program_source(test, check_test_program_transparent_variadic, "transparent-union-variadic", clang);
    check_test_program_source(test, check_test_program_transparent_stack, "transparent-union-stack", clang);
    // glibc's socket addresses' shape (__SOCKADDR_ARG with _GNU_SOURCE): restrict pointers to structs of one layout and a
    // pointer to const void, which a void pointer selects the first of, called directly, through a pointer of the
    // union's function type and through one of the first member's, which passes as it does.
    check_test_program_source(test,
        "struct sa{int f;};struct sb{int g;};typedef union __attribute__((transparent_union)){struct sa*__restrict a;struct sb*__restrict b;const void*v;}SockArg;"
        "static int peek(SockArg x){return x.a->f;}int(*indirect)(SockArg)=peek;int(*plain)(struct sa*)=peek;"
        "int tested(int n){struct sb s={n};struct sa t={n*2};return peek(&s)+peek(&t)*3+peek((void*)&s)*5+indirect(&t)*7+plain(&t)*11+indirect((const void*)&s)*13;}",
        "transparent-union-pointers", clang);
    // A union made transparent after a function definition, whose classification clang's code keeps by the function's
    // type from where it first met it: taken where the union's first member passes as the union does (pointers), and
    // refused by name where it doesn't (a struct of floats beside one of ints).
    check_test_program_source(test,
        "union U{int*a;long*b;};int f(union U u){return *u.a;}int early(union U u){return f(u);}"
        "typedef union U T __attribute__((transparent_union));int late(int*p){return f(p)+f((long*)p);}"
        "int tested(int n){int x=n;union U u={&x};return early(u)+late(&x)*3;}",
        "transparent-union-made-after-a-definition", clang);
    check_test_program_boundary_reason(test,
        "union U{struct{float x,y;}p;struct{int a,b;}q;};int f(union U u);int early(union U u){return f(u);}"
        "typedef union U T __attribute__((transparent_union));int tested(int n){return n;}",
        "transparent-union-made-after-a-definition-apart", true, false, 1, 0, "unions made transparent after a function definition, whose first members pass otherwise");
    // A union aligned past its first member by an attribute of its own: clang's code takes its registers from the member
    // and its stack slot from the union, so the lowering refuses it by name, as a call's argument, a parameter and va_arg's.
    check_test_program_boundary_reason(test,
        "typedef union __attribute__((transparent_union,aligned(16))){int*p;long*q;}A;int take(A);int tested(int n){return take(&n);}",
        "transparent-union-aligned-argument", true, false, 1, 0, "transparent unions sized or aligned otherwise than their first member");
    check_test_program_boundary_reason(test,
        "typedef union __attribute__((transparent_union,aligned(16))){int*p;long*q;}A;int take(A a){return *a.p;}int tested(int n){return n;}",
        "transparent-union-aligned-parameter", true, false, 0, 0, "transparent unions sized or aligned otherwise than their first member");
    check_test_program_boundary_reason(test,
        "typedef union __attribute__((transparent_union,aligned(16))){int*p;long*q;}A;"
        "int pick(int n,...){__builtin_va_list a;__builtin_va_start(a,n);A x=__builtin_va_arg(a,A);__builtin_va_end(a);return *x.p;}int tested(int n){return n;}",
        "transparent-union-aligned-va-arg", true, false, 0, 1, "transparent unions sized or aligned otherwise than their first member");
}
