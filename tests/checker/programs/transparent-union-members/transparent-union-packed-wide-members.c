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

static void member_fill(void*p,unsigned bytes,int n){unsigned char*q=p;for(unsigned i=0;i<bytes;i++)q[i]=(unsigned char)((unsigned)n*7+i*13+3);}
static unsigned long member_fold(unsigned long h,const void*p,unsigned bytes){const unsigned char*q=p;for(unsigned i=0;i<bytes;i++)h=(h^q[i])*1099511628211UL;return h;}

unsigned long external_transparent_packed(ExternalTPacked32,ExternalTPacked64,long);

unsigned long external_transparent_packed_apply(unsigned long(*)(ExternalTPacked32,ExternalTPacked64,long),int);

static unsigned long mine(ExternalTPacked32 a,ExternalTPacked64 b,long z)
{
    return member_fold(member_fold(external_transparent_packed(a,b,z),&a,32),&b,64);
}

int tested(int n)
{
    struct external_twide32_member a;
    struct external_twide64_member b;
    member_fill(&a,32,n);
    member_fill(&b,64,n+1);
    ExternalTPacked32 x={.a=a};
    ExternalTPacked64 y={.a=b};
    unsigned long h=external_transparent_packed(a,b,n*3L)^external_transparent_packed(x,y,n*5L);
    h^=mine(a,b,n*7L)+mine(x,y,n*9L);
    h^=external_transparent_packed_apply(mine,n);
    return (int)(h%1000003);
}
