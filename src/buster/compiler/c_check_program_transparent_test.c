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

// A union of ExternalTF's layout made transparent after initializers that name no function, and before one that does,
// which clang's code passes as its first member, two floats' bytes in xmm0, where the union's own classes take a general
// register: called with its value and with its first member, directly and through the pointer, the callee the harness's
// clang code (check_test_program_harness's external_transparent_first, of ExternalTF).
static const char check_test_program_transparent_first[] =
    "union L{struct{float x,y;}p;struct{int a,b;}q;};unsigned long external_transparent_first(union L);"
    "int seed=3;int*seed_address=&seed;typedef union L TL __attribute__((transparent_union));"
    "unsigned long(*first)(union L)=external_transparent_first;"
    "int tested(int n){union L l={.q={n,*seed_address}};"
    "return (int)(external_transparent_first(l)+first(l)*3+external_transparent_first(l.p)*5+first(l.p)*7);}";

// Unions of a stack slot of their own (#1844, BUSTER_CHECK_TEST_TRANSPARENT_OWN_UNIONS): the harness's clang functions
// called with each member's value, a union's own and a member's of another type (a long for an aggregate's union), in
// registers and on the stack; the program's own definitions, called by the program and by the harness through pointers;
// a volatile struct's bytes, which its copy into the union's object reads once.
static const char check_test_program_transparent_own[] =
    BUSTER_CHECK_TEST_TRANSPARENT_UNIONS
    BUSTER_CHECK_TEST_TRANSPARENT_OWN_UNIONS
    "unsigned long external_own(int,ExternalOL,ExternalOM,ExternalOB,ExternalOP,ExternalOD,ExternalOK,ExternalOQ);"
    "unsigned long external_own_late(long,long,long,long,long,long,double,double,double,double,double,double,double,double,int,ExternalOL,ExternalOM,"
    "ExternalOB,ExternalOP,ExternalOD,ExternalOK,ExternalOQ);"
    "unsigned long external_own_apply(unsigned long(*)(int,ExternalOL,ExternalOM,ExternalOB,ExternalOP,ExternalOD,ExternalOK,ExternalOQ),"
    "unsigned long(*)(long,long,long,long,long,long,double,double,double,double,double,double,double,double,int,ExternalOL,ExternalOM,ExternalOB,"
    "ExternalOP,ExternalOD,ExternalOK,ExternalOQ),int);"
    "unsigned long mine(int k,ExternalOL l,ExternalOM m,ExternalOB b,ExternalOP p,ExternalOD d,ExternalOK s,ExternalOQ q){"
    "return external_own(k,l,m,b,p,d,s,q)*3+(unsigned long)(l.s.a+m.m.b+b.s.c+s.p.i+q.p.c);}"
    "unsigned long mine_late(long l0,long l1,long l2,long l3,long l4,long l5,double d0,double d1,double d2,double d3,double d4,double d5,double d6,"
    "double d7,int k,ExternalOL l,ExternalOM m,ExternalOB b,ExternalOP p,ExternalOD d,ExternalOK s,ExternalOQ q){"
    "return external_own_late(l0,l1,l2,l3,l4,l5,d0,d1,d2,d3,d4,d5,d6,d7,k,l,m,b,p,d,s,q)+(unsigned long)(l.s.a*7+b.s.b+(long)d.s.v+s.p.c+q.p.i);}"
    "int tested(int n){long x=n*11;struct external_mix m={n,-n,2.5f,n*0.5f};struct external_pk k={(char)(n+1),n*3};struct external_big big={n,7,n*n};"
    "volatile struct external_l vl={n*13};ExternalOL ol={.s={n*2}};ExternalOM om={.m=m};ExternalOK ok={.p=k};ExternalOQ oq={.p=k};"
    "unsigned long h=external_own(0,(struct external_l){n},m,big,&x,(struct external_dv){n*0.25},k,k);"
    "h^=external_own(1,ol,om,(struct external_big_bytes){{n,2,3}},(long)n,(struct external_dv){1.5},ok,oq);"
    "h^=external_own(0,vl,m,big,&x,(struct external_dv){n*0.5},k,oq);"
    "h^=external_own_late(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,0,(struct external_l){n},m,big,&x,(struct external_dv){n*0.75},k,k);"
    "h^=external_own_late(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,1,(long)n*17,om,big,(long)n,(struct external_dv){2.0},ok,oq);"
    "h^=mine(0,(struct external_l){n},m,big,&x,(struct external_dv){n*0.25},k,k)+mine(1,ol,om,big,(long)n*5,(struct external_dv){0.5},ok,oq);"
    "h^=mine_late(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,0,(struct external_l){n},m,big,&x,(struct external_dv){n*0.75},k,k);"
    "h^=external_own_apply(mine,mine_late,n);return (int)(h%1000003);}";

// The register-classified unions of a slot of their own as unnamed arguments, which pass as their members, and va_arg's
// reading of them from the save area and the overflow area: the program's calls of the harness's variadic function and
// of its own, and the harness's call of the program's through a pointer.
static const char check_test_program_transparent_own_variadic[] =
    BUSTER_CHECK_TEST_TRANSPARENT_UNIONS
    BUSTER_CHECK_TEST_TRANSPARENT_OWN_UNIONS
    "unsigned long external_own_variadic(int,...);unsigned long external_own_variadic_apply(unsigned long(*)(int,...),int);"
    "static unsigned long fold(unsigned long h,unsigned long v){return (h^v)*1099511628211UL;}"
    "unsigned long mine_variadic(int count,...){__builtin_va_list a;__builtin_va_start(a,count);unsigned long h=(unsigned long)count;"
    "for(int i=0;i<count;i++){ExternalOL l=__builtin_va_arg(a,ExternalOL);ExternalOM m=__builtin_va_arg(a,ExternalOM);ExternalOP p=__builtin_va_arg(a,ExternalOP);"
    "ExternalOD d=__builtin_va_arg(a,ExternalOD);h=fold(h,(unsigned long)l.s.a);h=fold(h,(unsigned long)(m.m.a+m.m.b*3+(long)(m.m.c*4)+(long)(m.m.d*8)));"
    "h=fold(h,(unsigned long)p.l);h=fold(h,(unsigned long)(long)(d.s.v*4));}__builtin_va_end(a);return h;}"
    "int tested(int n){ExternalOL l={.s={n}};ExternalOM m={.m={n,3,1.5f,n*0.5f}};ExternalOP p={.l=n*9L};ExternalOD d={.s={n*0.5}};"
    "unsigned long h=external_own_variadic(3,l,m,p,d,l,m,p,d,l,m,p,d);h^=mine_variadic(3,l,m,p,d,l,m,p,d,l,m,p,d);"
    "h^=external_own_variadic_apply(mine_variadic,n);return (int)(h%1000003);}";

// Unions of a stack slot of their own of a pair's and a vector's members (#1844, BUSTER_CHECK_TEST_TRANSPARENT_OWN_UNIONS):
// a 128-bit integer's in a union of 32 and in a packed one, a 16-byte vector's struct and a complex long, passed to the
// harness's clang functions with the members' and the unions' values, in registers and after 5 longs and 8 doubles, and
// the program's own definitions called by the harness through pointers; and as unnamed arguments, read with va_arg.
static const char check_test_program_transparent_own_pairs[] =
    BUSTER_CHECK_TEST_TRANSPARENT_UNIONS
    BUSTER_CHECK_TEST_TRANSPARENT_OWN_UNIONS
    "unsigned long external_own_pairs(int,ExternalOW,ExternalOV,ExternalOC,ExternalOX);"
    "unsigned long external_own_pairs_late(long,long,long,long,long,double,double,double,double,double,double,double,double,int,ExternalOW,ExternalOV,"
    "ExternalOC,ExternalOX);"
    "unsigned long external_own_pairs_apply(unsigned long(*)(int,ExternalOW,ExternalOV,ExternalOC,ExternalOX),"
    "unsigned long(*)(long,long,long,long,long,double,double,double,double,double,double,double,double,int,ExternalOW,ExternalOV,ExternalOC,ExternalOX),int);"
    "unsigned long external_own_pairs_variadic(int,...);unsigned long external_own_pairs_variadic_apply(unsigned long(*)(int,...),int);"
    "static unsigned long fold(unsigned long h,unsigned long v){return (h^v)*1099511628211UL;}"
    "unsigned long mine(int k,ExternalOW w,ExternalOV v,ExternalOC c,ExternalOX x){"
    "return external_own_pairs(k,w,v,c,x)*3+(unsigned long)w.w+(unsigned long)v.v.x[1]+(unsigned long)__imag__ c.c+(unsigned long)(x.w>>64);}"
    "unsigned long mine_late(long l0,long l1,long l2,long l3,long l4,double d0,double d1,double d2,double d3,double d4,double d5,double d6,double d7,"
    "int k,ExternalOW w,ExternalOV v,ExternalOC c,ExternalOX x){"
    "return external_own_pairs_late(l0,l1,l2,l3,l4,d0,d1,d2,d3,d4,d5,d6,d7,k,w,v,c,x)+(unsigned long)(w.w>>64)*7+(unsigned long)v.v.x[2]+(unsigned long)x.w;}"
    "unsigned long mine_variadic(int count,...){__builtin_va_list a;__builtin_va_start(a,count);unsigned long h=(unsigned long)count;"
    "for(int i=0;i<count;i++){ExternalOW w=__builtin_va_arg(a,ExternalOW);ExternalOV v=__builtin_va_arg(a,ExternalOV);ExternalOC c=__builtin_va_arg(a,ExternalOC);"
    "h=fold(h,(unsigned long)w.w^(unsigned long)(w.w>>64));h=fold(h,(unsigned long)(v.v.x[0]+v.v.x[3]*7));"
    "h=fold(h,(unsigned long)(__real__ c.c+__imag__ c.c*9));}__builtin_va_end(a);return h;}"
    "int tested(int n){__int128 w=((__int128)n<<64)|(unsigned)(n*5+1);struct external_v v={{n,-3,n*n,9}};_Complex long c=n*3;__imag__ c=-n;"
    "ExternalOW ow={.w=w};ExternalOV ov={.v=v};ExternalOC oc={.c=c};ExternalOX ox={.w=w+1};"
    "unsigned long h=external_own_pairs(0,w,v,c,w)^external_own_pairs(1,ow,ov,oc,ox);"
    "h^=external_own_pairs_late(1,2,3,4,n,0.5,1,2,3,4,5,6,n*0.5,0,w,v,c,w)^external_own_pairs_late(1,2,3,4,n,0.5,1,2,3,4,5,6,n*0.5,1,ow,ov,oc,ox);"
    "h^=mine(0,w,v,c,w)+mine_late(1,2,3,4,n,0.5,1,2,3,4,5,6,n*0.5,1,ow,ov,oc,ox);h^=external_own_pairs_apply(mine,mine_late,n);"
    "h^=external_own_pairs_variadic(3,ow,ov,oc,ow,ov,oc,ow,ov,oc)^mine_variadic(3,ow,ov,oc,ow,ov,oc,ow,ov,oc);"
    "h^=external_own_pairs_variadic_apply(mine_variadic,n);return (int)(h%1000003);}";

// The reason the lowering refuses a union made transparent after clang's code may have classified a function's type.
static const char check_test_program_transparent_late[] =
    "unions made transparent after a function definition or an initializer naming a function, whose first members pass otherwise";

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
        "transparent-union-made-after-a-definition-apart", true, false, 1, 0, check_test_program_transparent_late);
    // The same after a static object's initializer that names a function, whose address clang's code takes where it emits
    // the object, before any definition (#801's review): clang 23.1.4 passes `external`'s and `saved`'s arguments as the
    // union, in a general register, and an aggregate's initializer names it as well. Refused by name, with 0 to 17
    // declarations before them, which put the initializer's declaration in each lane of the vector path's scan and in its
    // tail; the same without a function in the initializers is taken (check_test_program_transparent_first runs it).
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t references;
        bool late;
    } initializers[] =
    {
        { "transparent-union-made-after-an-initializer", "int(*saved)(union U)=external;", 2, true },
        { "transparent-union-made-after-an-aggregate-initializer", "struct S{int(*f)(union U);}s={&external};int(*saved)(union U);", 2, true },
        { "transparent-union-made-after-other-initializers", "int x=1;int*saved_address=&x;int(*saved)(union U);", 2, false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(initializers); i += 1)
    {
        for (uint32_t padding = 0; padding < 18; padding += 1)
        {
            char source[1024];
            char name[160];
            int length = 0;
            for (uint32_t p = 0; p < padding; p += 1)
            {
                length += snprintf(source + length, sizeof(source) - (size_t)length, "int p%u;", p);
            }
            snprintf(source + length, sizeof(source) - (size_t)length,
                     "union U{struct{float x,y;}p;struct{int a,b;}q;};int external(union U);%stypedef union U T __attribute__((transparent_union));"
                     "int tested(int n){union U u={.q={n,2}};return external(u)+saved(u);}",
                     initializers[i].source);
            snprintf(name, sizeof(name), "%s after %u declarations", initializers[i].name, padding);
            check_test_program_boundary_reason(test, source, name, true, !initializers[i].late, initializers[i].references, 1,
                                               initializers[i].late ? check_test_program_transparent_late : NULL);
        }
    }
    check_test_program_source(test, check_test_program_transparent_first, "transparent-union-made-between-initializers", clang);
    // A union aligned or packed otherwise than its first member: clang's code takes its registers from the member and,
    // where none is left, a stack slot of the union's size and alignment, as an argument, a parameter, through a pointer
    // and as va_arg's, which reads the member from the union's slot.
    check_test_program_source(test,
        "typedef union __attribute__((transparent_union,aligned(16))){int*p;long*q;}A;"
        "typedef union __attribute__((transparent_union,aligned(16))){int i;unsigned u;}I;"
        "typedef union __attribute__((transparent_union,packed)){int*p;long*q;}P;"
        "static long take(A a){return *a.p;}"
        "static long take_late(long l0,long l1,long l2,long l3,long l4,long l5,A a,I i,P p,long g){return l0+l5*3+*a.p*5+i.i*7+*p.p*11+g*13;}"
        "static long(*volatile through)(long,long,long,long,long,long,A,I,P,long)=take_late;"
        "static long pick(int n,...){__builtin_va_list v;__builtin_va_start(v,n);long s=0;"
        "for(int k=0;k<n;k++){A x=__builtin_va_arg(v,A);I y=__builtin_va_arg(v,I);s=s*31+*x.p+y.i;}__builtin_va_end(v);return s;}"
        "int tested(int n){int z=n+1;A a={.p=&z};"
        "long h=take(&n)+take_late(1,2,3,4,5,6,&z,n,&z,9)*3+through(n,2,3,4,5,6,a,n*2,&n,n)*5;"
        "I i1={.i=n},i2={.i=2};A b={.p=&n};h+=pick(5,a,i1,b,i2,a,i1,b,i2,a,i1);return (int)(h%1000003);}",
        "transparent-union-aligned", clang);
    // Past two eightbytes, of a first member clang passes as an aggregate, in registers or in memory, a pair or a 16-byte
    // vector, of any size (#1844): the harness's clang code calls and is called with each, in registers, on the stack, through pointers and
    // as unnamed arguments, and a member's value converted to the union passes from a zeroed object of the union's size.
    check_test_program_source(test, check_test_program_transparent_own, "transparent-union-own-slots", clang);
    check_test_program_source(test, check_test_program_transparent_own_variadic, "transparent-union-own-slots-variadic", clang);
    check_test_program_source(test, check_test_program_transparent_own_pairs, "transparent-union-own-slots-pairs", clang);
    // A member of an _Atomic type, which clang passes uncounted, keeps its refusal.
    check_test_program_boundary_reason(test,
        "typedef union __attribute__((transparent_union,aligned(32))){_Atomic long w;_Atomic long v;}A;int take(A);int tested(int n){return take(n);}",
        "transparent-union-aligned-atomic", true, false, 1, 0,
        "transparent unions sized or aligned otherwise than their first member, a vector wider than 16 bytes or an _Atomic value");
}
