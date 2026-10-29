// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #792: unnamed parameters retain their signature ordinals and ABI slots. The shared native fixture runner compares
// scalar/vector reader and lowering paths, then executes the generated object beside Clang's independent build.
// Include after c_check_program_test.c's source helpers.

static void check_test_program_unnamed_parameters(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        "int second(int,int b){return b;}int third(int,int,int c){return c;}int middle(int a,int,int c){return a*10+c;}"
        "double fp(double,double b){return b;}int mixed(double,int,long c){return (int)c;}"
        "int tail(int a,int){return a;}int all(int,int){return 9;}int none(void){return 5;}"
        "int tested(int n){int k=n&31;return second(1,k)+third(1,2,k)*3+middle(k,2,3)+(int)(fp(1.5,2.5)*2)"
        "+mixed(1.0,2,k)*7+tail(k,99)+all(1,2)+none();}",
        "unnamed-scalar-leading-middle-trailing-and-all", clang);
    check_test_program_source(test,
        "long integers(long,long,long,long,long,long,long g,long,long i){return g*3+i;}"
        "double floating(double,double,double,double,double,double,double,double,double i,double j){return i*3+j;}"
        "long memory(long double,long double b,int,int c){return (long)b*3+c;}"
        "long wide(__int128,__int128 b,long c){return (long)(b>>64)+(long)b+c;}"
        "int tested(int n){int k=n&31;return (int)integers(1,2,3,4,5,6,k,8,k+2)"
        "+(int)floating(1,2,3,4,5,6,7,8,k+0.5,k+1.5)+(int)memory(1.0L,k+2.0L,3,k)"
        "+(int)wide(((__int128)1<<64)+2,((__int128)3<<64)+k,17);}",
        "unnamed-integer-sse-stack-and-memory-classes", clang);
    check_test_program_source(test,
        "struct I{long a,b;};struct D{double a,b;};struct M{double a;long b;};struct L{long a,b,c;};"
        "long ints(struct I,struct I v,long k){return v.a*3+v.b+k;}"
        "double doubles(struct D,struct D v,double k){return v.a*3+v.b+k;}"
        "long mixed(struct M,struct M v,long k){return (long)v.a*3+v.b+k;}"
        "long large(struct L,struct L v,long k){return v.a*3+v.b+v.c+k;}"
        "struct L hidden(long,struct I,struct L v,int,long k){v.a+=k;return v;}"
        "int tested(int n){int k=n&31;struct I i={1,2},j={k,7};struct D d={1.5,2.5},e={k+0.5,7.5};"
        "struct M m={1.5,2},q={k+0.5,7};struct L a={1,2,3},b={k,7,9},r=hidden(11,i,b,13,17);"
        "return (int)(ints(i,j,19)+doubles(d,e,19)+mixed(m,q,19)+large(a,b,19)+r.a+r.b+r.c);}",
        "unnamed-aggregate-register-memory-and-hidden-result", clang);
    check_test_program_source(test,
        "struct I{long a,b;};struct D{double a,b;};struct L{long a,b,c;};"
        "long integer_rollback(long,long,long,long,long,struct I,long k,long s){return k*3+s;}"
        "double sse_rollback(double,double,double,double,double,double,double,struct D,double k,double s){return k*3+s;}"
        "long stacked(long,long,long,long,long,long,struct L,long k,long,long s){return k*3+s;}"
        "int tested(int n){int k=n&31;struct I i={11,12};struct D d={13,14};struct L l={15,16,17};"
        "return (int)(integer_rollback(1,2,3,4,5,i,k,19)+sse_rollback(1,2,3,4,5,6,7,d,k,23)"
        "+stacked(1,2,3,4,5,6,l,k,29,31));}",
        "unnamed-aggregate-register-exhaustion-and-stack-offsets", clang);
    check_test_program_source(test,
        "int add(int a,int b){return a+b;}"
        "int adjusted(int[3],int a[3],int(int,int),int(*f)(int left,int right),int k){return f(a[1],k);}"
        "int vla(int n,int a[n],int[n],int k){return a[n-1]+k;}"
        "int addressed(int,volatile int b,int,int c __attribute__((aligned(32)))){int*p=&c;*p+=2;b+=3;"
        "return b+c+(int)((unsigned long)p&31)*100;}"
        "int tested(int n){int k=n&31;int a[3]={1,k,3},b[3]={4,5,6};"
        "return adjusted(b,a,add,add,7)+vla(3,a,b,k)+addressed(11,k,13,17);}",
        "unnamed-adjusted-nested-declarators-and-addressed-locals", clang);
    check_test_program_source(test,
        "long mixed(int,double,int count,...){__builtin_va_list ap;__builtin_va_start(ap,count);"
        "long a=__builtin_va_arg(ap,long);double b=__builtin_va_arg(ap,double);__builtin_va_end(ap);return count+a+(long)b;}"
        "long stack(int,int,int,int,int,int,int,int count,...){__builtin_va_list ap;__builtin_va_start(ap,count);"
        "long a=__builtin_va_arg(ap,long);double b=__builtin_va_arg(ap,double);__builtin_va_end(ap);return count+a+(long)b;}"
        "long sse(double,double,double,double,double,double,double,double,double,int count,...){"
        "__builtin_va_list ap;__builtin_va_start(ap,count);double a=__builtin_va_arg(ap,double);"
        "long b=__builtin_va_arg(ap,long);__builtin_va_end(ap);return count+(long)a+b;}"
        "int tested(int n){int k=n&31;return (int)(mixed(11,12.5,k,17L,19.5)+stack(1,2,3,4,5,6,7,k,23L,29.5)"
        "+sse(1,2,3,4,5,6,7,8,9,k,31.5,37L));}",
        "unnamed-fixed-parameters-before-varargs", clang);
    // The harness is compiled by Clang and calls this buster-compiled definition through a function pointer, so the
    // caller and callee do not share lowering code that could hide the same parameter-index error on both sides.
    check_test_program_source(test,
        "int external_unnamed_apply(long(*)(int,double,int,double,long),int);"
        "long callback(int,double,int i,double d,long tail){return i*3+(long)(d*2)+tail;}"
        "int tested(int n){return external_unnamed_apply(callback,n&31);}",
        "unnamed-parameters-from-clang-caller", clang);
}
