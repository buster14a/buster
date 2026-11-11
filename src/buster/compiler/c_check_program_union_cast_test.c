// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GCC's cast to a union (#2301, clang's CK_ToUnion), run against clang -O0's build of the same programs: a value of a
// member's type initializes that member of a temporary union (check_lower_union_cast), and a static object's
// initializer's cast its member's bytes (check_init_value). The other bytes of a temporary are left as clang's code
// leaves them, so the programs read only the initialized member. Include after c_check_program_test.c's source and
// execution helpers.

// Each kind of member a cast selects: an integer's, a narrower one's, a pointer's, a floating value's, a wide one's, a
// complex one's, a vector's, a struct's, a function's pointer from its designator, a pointer from an array's decay, an
// _Atomic member passed over for a plain one, and a named bit-field; as an initializer, an assignment, an argument, a
// result, a member access and a conditional's operand.
static const char check_test_program_union_cast_values[] =
    "typedef int V4 __attribute__((vector_size(16)));struct S{int a,b,c;};"
    "union U{char c;short h;int*p;float f;long double ld;__int128 w;_Complex double z;V4 v;struct S s;int(*fn)(int);const int*cp;};"
    "union A{_Atomic int a;int b;};union B{int bits:5;long l;};"
    "static int twice(int x){return x*2;}"
    "static long take(union U u,int k){return k==0?u.s.b:k==1?(long)u.f:(long)u.ld;}"
    "static union U make(struct S s){return (union U)s;}"
    "int tested(int n){int x=n+3;int arr[2]={n,n*5};V4 v={n,2,3,4};struct S s={n,n+1,n+2};volatile int q=n*7;"
    "union U u=(union U)&x;long h=*u.p;u=(union U)(char)(n+1);h=h*31+u.c;u=(union U)(short)-n;h=h*31+u.h;"
    "u=(union U)(float)(n*0.5f);h=h*31+(long)(u.f*4);u=(union U)(long double)n;h=h*31+(long)(u.ld*3);"
    "u=(union U)((__int128)n<<70);h=h*31+(long)(u.w>>70);u=(union U)(_Complex double)(n+2.0);h=h*31+(long)__real__ u.z;"
    "u=(union U)v;h=h*31+u.v[0]+u.v[3];h=h*31+make(s).s.c+take((union U)s,0)+take((union U)(float)n,1)*3+take((union U)(long double)n,2)*5;"
    "u=(union U)twice;h=h*31+u.fn(n);u=(union U)(const int*)arr;h=h*31+u.cp[1];"
    "union A a=(union A)q;h=h*31+a.b;union B b=(union B)n;h=h*31+b.bits;"
    "h=h*31+(n>2?((union U)&x).p[0]:((union U)(char)n).c);h=h*31+((union U)s).s.a;"
    "return (int)(h%1000003);}";

// A static object's initializer: the union's own, an aggregate's member and an array's element, a block's static object,
// and an address constant.
static const char check_test_program_union_cast_static[] =
    "union U{int i;double d;int*p;struct{int x,y;}s;};int seed=5;"
    "union U g1=(union U)7;union U g2=(union U)2.5;union U g3=(union U)&seed;struct T{union U u;int k;}t={(union U)9,4};"
    "union U ga[3]={(union U)1,(union U)1.5,[2]=(union U)&seed};"
    "int tested(int n){static union U s1=(union U)11;"
    "return g1.i+(int)(g2.d*2)*3+*g3.p*5+t.u.i*7+t.k*11+ga[0].i*13+(int)(ga[1].d*2)*17+*ga[2].p*19+s1.i*23+n;}";

// A transparent union's cast, which selects its member as any union's does, passed to a function taking the union.
static const char check_test_program_union_cast_transparent[] =
    "typedef union __attribute__((transparent_union)){int*p;long*q;}P;static long peek(P x){return *x.p;}"
    "int tested(int n){int x=n*3;long y=n;return (int)(peek((P)&x)+peek((P)(long*)&y)*7+((P)&x).p[0]*11);}";

static void check_test_program_union_casts(CheckTest* test, const char* clang)
{
    check_test_program_source(test, check_test_program_union_cast_values, "union-cast-values", clang);
    check_test_program_source(test, check_test_program_union_cast_static, "union-cast-static", clang);
    check_test_program_source(test, check_test_program_union_cast_transparent, "union-cast-transparent", clang);
}
