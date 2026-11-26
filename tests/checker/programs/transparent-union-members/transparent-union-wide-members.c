// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

typedef unsigned char external_twide16 __attribute__((vector_size(16)));
typedef unsigned char external_twide32 __attribute__((vector_size(32)));
typedef unsigned char external_twide64 __attribute__((vector_size(64)));
struct external_twide32_member{external_twide32 v;};struct external_twide64_member{external_twide64 v;};
union __attribute__((packed)) external_twide24_member{external_twide16 v;unsigned char bytes[24];};
union __attribute__((packed)) external_twide40_member{external_twide16 v;unsigned char bytes[40];};
union __attribute__((packed)) external_twide48_member{external_twide16 v;unsigned char bytes[48];};
union __attribute__((packed)) external_twide56_member{external_twide16 v;unsigned char bytes[56];};
typedef union __attribute__((transparent_union,aligned(128))){struct external_twide32_member a,b;}ExternalTWide32;
typedef union __attribute__((transparent_union,aligned(128))){struct external_twide64_member a,b;}ExternalTWide64;
typedef union __attribute__((transparent_union,aligned(128))){union external_twide24_member a,b;}ExternalTWide24;
typedef union __attribute__((transparent_union,aligned(128))){union external_twide40_member a,b;}ExternalTWide40;
typedef union __attribute__((transparent_union,aligned(128))){union external_twide48_member a,b;}ExternalTWide48;
typedef union __attribute__((transparent_union,aligned(128))){union external_twide56_member a,b;}ExternalTWide56;
typedef union __attribute__((transparent_union,packed,aligned(2))){struct external_twide32_member a,b;}ExternalTPacked32;
typedef union __attribute__((transparent_union,packed,aligned(2))){struct external_twide64_member a,b;}ExternalTPacked64;

unsigned long external_transparent_wide(long,ExternalTWide32,ExternalTWide64,ExternalTWide24,ExternalTWide40,ExternalTWide48,ExternalTWide56,long);
unsigned long external_transparent_wide_stack(long,long,long,long,long,long,double,double,double,double,double,double,double,double,ExternalTWide32,ExternalTWide64,ExternalTWide24,ExternalTWide40,ExternalTWide48,ExternalTWide56,long);
unsigned long external_transparent_wide_named(ExternalTWide32,ExternalTWide64,ExternalTWide24,ExternalTWide40,ExternalTWide48,ExternalTWide56,long,...);
unsigned long external_transparent_wide_apply(unsigned long(*)(long,ExternalTWide32,ExternalTWide64,ExternalTWide24,ExternalTWide40,ExternalTWide48,ExternalTWide56,long),unsigned long(*)(long,long,long,long,long,long,double,double,double,double,double,double,double,double,ExternalTWide32,ExternalTWide64,ExternalTWide24,ExternalTWide40,ExternalTWide48,ExternalTWide56,long),unsigned long(*)(ExternalTWide32,ExternalTWide64,ExternalTWide24,ExternalTWide40,ExternalTWide48,ExternalTWide56,long,...),int);

static void member_fill(void*p,unsigned bytes,int n){unsigned char*q=p;for(unsigned i=0;i<bytes;i++)q[i]=(unsigned char)((unsigned)n*7+i*13+3);}
static unsigned long member_fold(unsigned long h,const void*p,unsigned bytes){const unsigned char*q=p;for(unsigned i=0;i<bytes;i++)h=(h^q[i])*1099511628211UL;return h;}

static unsigned long mine(long k,ExternalTWide32 a,ExternalTWide64 b,ExternalTWide24 c,ExternalTWide40 d,ExternalTWide48 e,ExternalTWide56 f,long z)
{
    unsigned long h=external_transparent_wide(k,a,b,c,d,e,f,z);
    h=member_fold(h,&a,32);h=member_fold(h,&b,64);h=member_fold(h,&c,24);h=member_fold(h,&d,40);h=member_fold(h,&e,48);h=member_fold(h,&f,56);
    return h;
}

static unsigned long mine_stack(long l0,long l1,long l2,long l3,long l4,long l5,double d0,double d1,double d2,double d3,double d4,double d5,double d6,double d7,ExternalTWide32 a,ExternalTWide64 b,ExternalTWide24 c,ExternalTWide40 d,ExternalTWide48 e,ExternalTWide56 f,long z)
{
    unsigned long h=external_transparent_wide_stack(l0,l1,l2,l3,l4,l5,d0,d1,d2,d3,d4,d5,d6,d7,a,b,c,d,e,f,z);
    h=member_fold(h,&a,32);h=member_fold(h,&b,64);h=member_fold(h,&c,24);h=member_fold(h,&d,40);h=member_fold(h,&e,48);h=member_fold(h,&f,56);
    return h;
}

static unsigned long mine_named(ExternalTWide32 a,ExternalTWide64 b,ExternalTWide24 c,ExternalTWide40 d,ExternalTWide48 e,ExternalTWide56 f,long z,...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap,z);
    long x=__builtin_va_arg(ap,long);
    double y=__builtin_va_arg(ap,double);
    __builtin_va_end(ap);
    unsigned long h=external_transparent_wide_named(a,b,c,d,e,f,z,x,y);
    h=member_fold(h,&a,32);h=member_fold(h,&b,64);h=member_fold(h,&c,24);h=member_fold(h,&d,40);h=member_fold(h,&e,48);h=member_fold(h,&f,56);
    return h;
}

int tested(int n)
{
    struct external_twide32_member s32;
    member_fill(&s32,32,n+0);
    ExternalTWide32 a={.a=s32};
    struct external_twide64_member s64;
    member_fill(&s64,64,n+1);
    ExternalTWide64 b={.a=s64};
    union external_twide24_member s24;
    member_fill(&s24,24,n+2);
    ExternalTWide24 c={.a=s24};
    union external_twide40_member s40;
    member_fill(&s40,40,n+3);
    ExternalTWide40 d={.a=s40};
    union external_twide48_member s48;
    member_fill(&s48,48,n+4);
    ExternalTWide48 e={.a=s48};
    union external_twide56_member s56;
    member_fill(&s56,56,n+5);
    ExternalTWide56 f={.a=s56};
    
    unsigned long h=external_transparent_wide(1,s32,s64,s24,s40,s48,s56,n*3L)^external_transparent_wide(2,a,b,c,d,e,f,n*5L);
    h^=external_transparent_wide_stack(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,a,b,c,d,e,f,n*7L);
    h^=mine(3,a,b,c,d,e,f,n*9L)+mine_stack(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,s32,s64,s24,s40,s48,s56,n*11L);
    h^=external_transparent_wide_named(a,b,c,d,e,f,n*13L,n*15L,n*0.25)+mine_named(s32,s64,s24,s40,s48,s56,n*17L,n*19L,n*0.5);
    h^=external_transparent_wide_apply(mine,mine_stack,mine_named,n);
    return (int)(h%1000003);
}
