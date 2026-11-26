// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

typedef union __attribute__((transparent_union,aligned(32))){_Atomic char a,b;}ExternalTAtomicChar;
typedef union __attribute__((transparent_union,aligned(32))){_Atomic long a,b;}ExternalTAtomicLong;
typedef union __attribute__((transparent_union,aligned(32))){_Atomic double a,b;}ExternalTAtomicDouble;
typedef union __attribute__((transparent_union,aligned(32))){_Atomic long double a,b;}ExternalTAtomicExtended;
typedef union __attribute__((transparent_union,aligned(32))){_Atomic __int128 a,b;}ExternalTAtomicWide;

unsigned long external_transparent_atomic(ExternalTAtomicChar,ExternalTAtomicLong,ExternalTAtomicDouble,ExternalTAtomicExtended,ExternalTAtomicWide,long);
unsigned long external_transparent_atomic_stack(long,long,long,long,long,long,double,double,double,double,double,double,double,double,ExternalTAtomicChar,ExternalTAtomicLong,ExternalTAtomicDouble,ExternalTAtomicExtended,ExternalTAtomicWide,long);
unsigned long external_transparent_atomic_apply(unsigned long(*)(ExternalTAtomicChar,ExternalTAtomicLong,ExternalTAtomicDouble,ExternalTAtomicExtended,ExternalTAtomicWide,long),unsigned long(*)(long,long,long,long,long,long,double,double,double,double,double,double,double,double,ExternalTAtomicChar,ExternalTAtomicLong,ExternalTAtomicDouble,ExternalTAtomicExtended,ExternalTAtomicWide,long),int);

static unsigned long member_fold(unsigned long h,const void*p,unsigned bytes){const unsigned char*q=p;for(unsigned i=0;i<bytes;i++)h=(h^q[i])*1099511628211UL;return h;}

static unsigned long mine(ExternalTAtomicChar s,ExternalTAtomicLong a,ExternalTAtomicDouble b,ExternalTAtomicExtended c,ExternalTAtomicWide d,long z)
{
    unsigned long h=external_transparent_atomic(s,a,b,c,d,z);
    h=member_fold(h,&s,1);h=member_fold(h,&a,8);h=member_fold(h,&b,8);h=member_fold(h,&c,10);h=member_fold(h,&d,16);
    return h;
}

static unsigned long mine_stack(long l0,long l1,long l2,long l3,long l4,long l5,double d0,double d1,double d2,double d3,double d4,double d5,double d6,double d7,ExternalTAtomicChar s,ExternalTAtomicLong a,ExternalTAtomicDouble b,ExternalTAtomicExtended c,ExternalTAtomicWide d,long z)
{
    unsigned long h=external_transparent_atomic_stack(l0,l1,l2,l3,l4,l5,d0,d1,d2,d3,d4,d5,d6,d7,s,a,b,c,d,z);
    h=member_fold(h,&s,1);h=member_fold(h,&a,8);h=member_fold(h,&b,8);h=member_fold(h,&c,10);h=member_fold(h,&d,16);
    return h;
}

int tested(int n)
{
    __int128 w=(__int128)(((unsigned __int128)(unsigned long)n<<64)|(unsigned)(n*5+1));
    ExternalTAtomicChar s={.a=(char)n};
    ExternalTAtomicLong a={.a=n*3L};
    ExternalTAtomicDouble b={.a=n*0.5};
    ExternalTAtomicExtended c={.a=(long double)n*0.25L};
    ExternalTAtomicWide d={.a=w};
    
    unsigned long h=external_transparent_atomic((char)n,n*3L,n*0.5,(long double)n*0.25L,w,n*7L)^external_transparent_atomic(s,a,b,c,d,n*9L);
    h^=external_transparent_atomic_stack(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,s,a,b,c,d,n*11L);
    h^=mine((char)n,n*3L,n*0.5,(long double)n*0.25L,w,n*13L)+mine_stack(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,s,a,b,c,d,n*15L);
    h^=external_transparent_atomic_apply(mine,mine_stack,n);
    return (int)(h%1000003);
}
