// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

static unsigned long member_fold(unsigned long h,const void*p,unsigned bytes){const unsigned char*q=p;for(unsigned i=0;i<bytes;i++)h=(h^q[i])*1099511628211UL;return h;}

static void member_fill(void*p,unsigned bytes,int n){unsigned char*q=p;for(unsigned i=0;i<bytes;i++)q[i]=(unsigned char)((unsigned)n*7+i*13+3);}

#if defined(__AVX__)
typedef unsigned char external_twide32 __attribute__((vector_size(32)));
typedef union __attribute__((transparent_union,aligned(128))){_Atomic external_twide32 a,b;}ExternalTAtomic32;
unsigned long external_transparent_atomic32(ExternalTAtomic32,long);
unsigned long external_transparent_atomic32_stack(long,long,long,long,long,long,double,double,double,double,double,double,double,double,ExternalTAtomic32,long);
unsigned long external_transparent_atomic32_named(ExternalTAtomic32,long,...);
unsigned long external_transparent_atomic32_apply(unsigned long(*)(ExternalTAtomic32,long),unsigned long(*)(long,long,long,long,long,long,double,double,double,double,double,double,double,double,ExternalTAtomic32,long),unsigned long(*)(ExternalTAtomic32,long,...),int);

static unsigned long mine32(ExternalTAtomic32 a,long z)
{
    return member_fold(external_transparent_atomic32(a,z),&a,32);
}

static unsigned long stack32(long l0,long l1,long l2,long l3,long l4,long l5,double d0,double d1,double d2,double d3,double d4,double d5,double d6,double d7,ExternalTAtomic32 a,long z)
{
    return member_fold(external_transparent_atomic32_stack(l0,l1,l2,l3,l4,l5,d0,d1,d2,d3,d4,d5,d6,d7,a,z),&a,32);
}

static unsigned long named32(ExternalTAtomic32 a,long z,...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap,z);
    long x=__builtin_va_arg(ap,long);
    double y=__builtin_va_arg(ap,double);
    __builtin_va_end(ap);
    return member_fold(external_transparent_atomic32_named(a,z,x,y),&a,32);
}

static unsigned long test32(int n)
{
    external_twide32 x;
    member_fill(&x,32,n);
    ExternalTAtomic32 a={.a=x};
    unsigned long h=external_transparent_atomic32(x,n*3L)^external_transparent_atomic32(a,n*5L);
    h^=external_transparent_atomic32_stack(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,a,n*7L)+stack32(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,x,n*9L);
    h^=external_transparent_atomic32_named(a,n*11L,n*13L,n*0.25)+named32(x,n*15L,n*17L,n*0.5);
    h^=mine32(x,n*19L)+external_transparent_atomic32_apply(mine32,stack32,named32,n);
    return h;
}
#endif

#if defined(__AVX512F__)
typedef unsigned char external_twide64 __attribute__((vector_size(64)));
typedef union __attribute__((transparent_union,aligned(128))){_Atomic external_twide64 a,b;}ExternalTAtomic64;
unsigned long external_transparent_atomic64(ExternalTAtomic64,long);
unsigned long external_transparent_atomic64_stack(long,long,long,long,long,long,double,double,double,double,double,double,double,double,ExternalTAtomic64,long);
unsigned long external_transparent_atomic64_named(ExternalTAtomic64,long,...);
unsigned long external_transparent_atomic64_apply(unsigned long(*)(ExternalTAtomic64,long),unsigned long(*)(long,long,long,long,long,long,double,double,double,double,double,double,double,double,ExternalTAtomic64,long),unsigned long(*)(ExternalTAtomic64,long,...),int);

static unsigned long mine64(ExternalTAtomic64 a,long z)
{
    return member_fold(external_transparent_atomic64(a,z),&a,64);
}

static unsigned long stack64(long l0,long l1,long l2,long l3,long l4,long l5,double d0,double d1,double d2,double d3,double d4,double d5,double d6,double d7,ExternalTAtomic64 a,long z)
{
    return member_fold(external_transparent_atomic64_stack(l0,l1,l2,l3,l4,l5,d0,d1,d2,d3,d4,d5,d6,d7,a,z),&a,64);
}

static unsigned long named64(ExternalTAtomic64 a,long z,...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap,z);
    long x=__builtin_va_arg(ap,long);
    double y=__builtin_va_arg(ap,double);
    __builtin_va_end(ap);
    return member_fold(external_transparent_atomic64_named(a,z,x,y),&a,64);
}

static unsigned long test64(int n)
{
    external_twide64 x;
    member_fill(&x,64,n);
    ExternalTAtomic64 a={.a=x};
    unsigned long h=external_transparent_atomic64(x,n*3L)^external_transparent_atomic64(a,n*5L);
    h^=external_transparent_atomic64_stack(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,a,n*7L)+stack64(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,x,n*9L);
    h^=external_transparent_atomic64_named(a,n*11L,n*13L,n*0.25)+named64(x,n*15L,n*17L,n*0.5);
    h^=mine64(x,n*19L)+external_transparent_atomic64_apply(mine64,stack64,named64,n);
    return h;
}
#endif

int tested(int n)
{
    unsigned long h=(unsigned long)n;
#if defined(__AVX__)
    h^=test32(n);
#endif
#if defined(__AVX512F__)
    h^=test64(n);
#endif
    return (int)(h%1000003);
}
