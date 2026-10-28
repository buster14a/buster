// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's x86 builtins in the lowering (c_check_program_x86.c), against clang -O0's code of the same source for znver5:
// each family's builtins called directly, at their prototypes' types, on inputs from the call's argument, with every
// result folded into the program's hash. The programs run where the host has the features their rows take (AVX-512F,
// BW, CD, DQ, VL, VBMI, VBMI2, BITALG and VNNI, GFNI, VAES, AVX2 and BMI2, by x86_target_native's CPUID and XCR0);
// elsewhere they compile and link, as clang's builds do, and don't run. Include after c_check_program_vector_test.c.

static const char check_test_x86_types[] =
    "typedef char v16qi __attribute__((vector_size(16)));typedef char v32qi __attribute__((vector_size(32)));"
    "typedef char v64qi __attribute__((vector_size(64)));typedef unsigned char v64qu __attribute__((vector_size(64)));"
    "typedef short v8hi __attribute__((vector_size(16)));typedef short v16hi __attribute__((vector_size(32)));"
    "typedef short v32hi __attribute__((vector_size(64)));typedef int v4si __attribute__((vector_size(16)));"
    "typedef int v8si __attribute__((vector_size(32)));typedef int v16si __attribute__((vector_size(64)));"
    "typedef long long v2di __attribute__((vector_size(16)));typedef long long v4di __attribute__((vector_size(32)));"
    "typedef long long v8di __attribute__((vector_size(64)));typedef float v4sf __attribute__((vector_size(16)));"
    "typedef float v8sf __attribute__((vector_size(32)));typedef double v2df __attribute__((vector_size(16)));"
    "typedef double v4df __attribute__((vector_size(32)));\n"
    "static unsigned fold(unsigned h,const void*p,unsigned long n){const unsigned char*b=p;for(unsigned long i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}\n"
    "#define FOLD(v) h=fold(h,&(v),sizeof(v))\n"
    "static void fill(void*p,unsigned long n,unsigned s){unsigned char*b=p;for(unsigned long i=0;i<n;i++){s=s*1103515245u+12345u;b[i]=(unsigned char)(s>>16);}}\n"
    "static unsigned long long mask(int n){return (unsigned long long)((unsigned)n*2654435761u)<<32|((unsigned)n*40503u+5u);}\n";

static void check_test_program_x86_sources(CheckTest* test, const char* clang, const X86Target* target, bool run_native)
{
    static const char* const programs[][2] = {
        {
            "x86-opmasks",
            "int tested(int n){unsigned h=2166136261u;unsigned short a=(unsigned short)(n*40503u+7),b=(unsigned short)(n*7919u+3);"
            "unsigned c=(unsigned)n*2654435761u+1,d=c^0x5555aaaau;unsigned long long e=(unsigned long long)c<<32|d,f=e*0x9e3779b97f4a7c15ull+(unsigned)n;"
            "unsigned short r[]={__builtin_ia32_kandhi(a,b),__builtin_ia32_kaddhi(a,b),__builtin_ia32_kunpckhi(a,b),__builtin_ia32_kshiftlihi(a,3),"
            "__builtin_ia32_kshiftlihi(b,16)};FOLD(r);unsigned s=__builtin_ia32_kunpcksi(c,d);FOLD(s);"
            "unsigned long long q[]={__builtin_ia32_kunpckdi(e,f),__builtin_ia32_kordi(e,f),__builtin_ia32_kxordi(e,f),__builtin_ia32_kshiftlidi(e,5),"
            "__builtin_ia32_kshiftlidi(f,63),__builtin_ia32_kmovq(f)};FOLD(q);"
            "int z[]={__builtin_ia32_ktestzdi(e,f),__builtin_ia32_ktestzdi(e,~e),__builtin_ia32_ktestzdi(0,f)};FOLD(z);"
            "return (int)h;}",
        },
        {
            "x86-compress-expand",
            "int tested(int n){unsigned h=2166136261u;v64qi qa,qw;v32hi ha,hw;v16si da,dw;v8di ea,ew;v8hi xa,xw;v16qi ba,bw;v8si ya,yw;"
            "fill(&qa,64,n);fill(&qw,64,n+1);fill(&ha,64,n+2);fill(&hw,64,n+3);fill(&da,64,n+4);fill(&dw,64,n+5);fill(&ea,64,n+6);fill(&ew,64,n+7);"
            "fill(&xa,16,n+8);fill(&xw,16,n+9);fill(&ba,16,n+10);fill(&bw,16,n+11);fill(&ya,32,n+12);fill(&yw,32,n+13);unsigned long long m=mask(n);"
            "v64qi q[]={__builtin_ia32_compressqi512_mask(qa,qw,m),__builtin_ia32_expandqi512_mask(qa,qw,m)};FOLD(q);"
            "v32hi hh[]={__builtin_ia32_compresshi512_mask(ha,hw,(unsigned)m),__builtin_ia32_expandhi512_mask(ha,hw,(unsigned)(m>>9))};FOLD(hh);"
            "v16si d[]={__builtin_ia32_compresssi512_mask(da,dw,(unsigned short)m),__builtin_ia32_expandsi512_mask(da,dw,(unsigned short)(m>>7))};FOLD(d);"
            "v8di e[]={__builtin_ia32_compressdi512_mask(ea,ew,(unsigned char)m),__builtin_ia32_expanddi512_mask(ea,ew,(unsigned char)(m>>3))};FOLD(e);"
            "v8hi x=__builtin_ia32_compresshi128_mask(xa,xw,(unsigned char)(m>>11));FOLD(x);"
            "v16qi bq=__builtin_ia32_compressqi128_mask(ba,bw,(unsigned short)(m>>13));FOLD(bq);"
            "v8si y=__builtin_ia32_compresssi256_mask(ya,yw,(unsigned char)(m>>17));FOLD(y);"
            "v16si out[2];fill(out,sizeof(out),n+14);__builtin_ia32_compressstoresi512_mask(out,da,(unsigned short)(m>>21));FOLD(out);"
            "v64qi bytes[2];fill(bytes,sizeof(bytes),n+15);__builtin_ia32_compressstoreqi512_mask(bytes,qa,m>>1);FOLD(bytes);"
            "return (int)h;}",
        },
        {
            "x86-mask-vectors",
            "int tested(int n){unsigned h=2166136261u;v64qi a;v16qi b;v32hi w;fill(&a,64,n);fill(&b,16,n+1);fill(&w,64,n+2);unsigned long long m=mask(n);"
            "unsigned long long k=__builtin_ia32_cvtb2mask512(a);unsigned short k2=__builtin_ia32_cvtb2mask128(b);unsigned k3=__builtin_ia32_cvtw2mask512(w);"
            "FOLD(k);FOLD(k2);FOLD(k3);v64qi v=__builtin_ia32_cvtmask2b512(m);v16si vd=__builtin_ia32_cvtmask2d512((unsigned short)(m>>5));FOLD(v);FOLD(vd);"
            "return (int)h;}",
        },
        {
            "x86-convert",
            "int tested(int n){unsigned h=2166136261u;union{unsigned u;float f;}q={0x7fc00000u};v2df d={(double)n*1.75-3.5,0};"
            "v4sf f={(float)n*-2.25f+1.0f,0,0,0};v2df big={1e30*(n+1),0};v4sf nan={q.f,0,0,0};"
            "int a[]={__builtin_ia32_cvttsd2si(d),__builtin_ia32_cvttss2si(f),__builtin_ia32_cvttsd2si(big),__builtin_ia32_cvttss2si(nan)};FOLD(a);"
            "long long b[]={__builtin_ia32_cvttsd2si64(d),__builtin_ia32_cvttss2si64(f),__builtin_ia32_cvttsd2si64(big)};FOLD(b);"
            "return (int)h;}",
        },
        {
            "x86-lanes",
            "int tested(int n){unsigned h=2166136261u;v16si a,b;v8di c,d;v4si x;v4di y;fill(&a,64,n);fill(&b,64,n+1);fill(&c,64,n+2);fill(&d,64,n+3);"
            "fill(&x,16,n+4);fill(&y,32,n+5);unsigned char m=(unsigned char)(n*37+1);"
            "v4si e[]={__builtin_ia32_extracti32x4_mask(a,2,x,m),__builtin_ia32_extracti32x4_mask(b,3,x,0xff)};FOLD(e);"
            "v4di e2=__builtin_ia32_extracti64x4_mask(c,1,y,m);FOLD(e2);"
            "v16si i1=__builtin_ia32_inserti32x4(a,x,3);v8di i2=__builtin_ia32_inserti64x4(c,y,1);v4di i3=__builtin_ia32_insert128i256(y,(v2di)x,1);"
            "FOLD(i1);FOLD(i2);FOLD(i3);v16si al=__builtin_ia32_alignd512(a,b,5);v8di aq=__builtin_ia32_alignq512(c,d,3);FOLD(al);FOLD(aq);"
            "v16si s1=__builtin_ia32_shuf_i32x4(a,b,0x4e);v8di s2=__builtin_ia32_shuf_i64x2(c,d,0xb1);FOLD(s1);FOLD(s2);"
            "return (int)h;}",
        },
        {
            "x86-immediates",
            "int tested(int n){unsigned h=2166136261u;v16si a;v16qi b;v64qi c,d;fill(&a,64,n);fill(&b,16,n+1);fill(&c,64,n+2);fill(&d,64,n+3);"
            "v16si p=__builtin_ia32_pshufd512(a,0x1b);v16si r=__builtin_ia32_prold512(a,7);FOLD(p);FOLD(r);"
            "v16qi s[]={__builtin_ia32_pslldqi128_byteshift(b,3),__builtin_ia32_pslldqi128_byteshift(b,15)};FOLD(s);"
            "v64qi g=__builtin_ia32_vgf2p8affineqb_v64qi(c,d,0x5a);FOLD(g);"
            "return (int)h;}",
        },
        {
            "x86-ternary",
            "int tested(int n){unsigned h=2166136261u;v16si a,b,c;v8di d,e,f;v64qu u;v64qi s;v32hi w1,w2,w3;v64qi q1,q2,q3;v32qi y1,y2,y3;"
            "fill(&a,64,n);fill(&b,64,n+1);fill(&c,64,n+2);fill(&d,64,n+3);fill(&e,64,n+4);fill(&f,64,n+5);fill(&u,64,n+6);fill(&s,64,n+7);"
            "fill(&w1,64,n+8);fill(&w2,64,n+9);fill(&w3,64,n+10);fill(&q1,64,n+11);fill(&q2,64,n+12);fill(&q3,64,n+13);fill(&y1,32,n+14);"
            "fill(&y2,32,n+15);fill(&y3,32,n+16);unsigned long long m=mask(n);"
            "v16si t1=__builtin_ia32_pternlogd512_mask(a,b,c,0x96,(unsigned short)m);v8di t2=__builtin_ia32_pternlogq512_mask(d,e,f,0xe8,(unsigned char)m);"
            "FOLD(t1);FOLD(t2);v16si dp=__builtin_ia32_vpdpbusd512(a,u,s);FOLD(dp);"
            "v16si p1=__builtin_ia32_vpermi2vard512(a,b,c);v8di p2=__builtin_ia32_vpermi2varq512(d,e,f);v32hi p3=__builtin_ia32_vpermi2varhi512(w1,w2,w3);"
            "v64qi p4=__builtin_ia32_vpermi2varqi512(q1,q2,q3);v32qi p5=__builtin_ia32_vpermi2varqi256(y1,y2,y3);"
            "FOLD(p1);FOLD(p2);FOLD(p3);FOLD(p4);FOLD(p5);"
            "return (int)h;}",
        },
        {
            "x86-truncations",
            "int tested(int n){unsigned h=2166136261u;v16si a;v8di b;v32hi c;v16qi w1;v16hi w2;v8si w3;v8hi w4;v32qi w5;fill(&a,64,n);fill(&b,64,n+1);"
            "fill(&c,64,n+2);fill(&w1,16,n+3);fill(&w2,32,n+4);fill(&w3,32,n+5);fill(&w4,16,n+6);fill(&w5,32,n+7);unsigned long long m=mask(n);"
            "v16qi r1=__builtin_ia32_pmovdb512_mask(a,w1,(unsigned short)m);v16hi r2=__builtin_ia32_pmovdw512_mask(a,w2,(unsigned short)(m>>3));"
            "v16qi r3=__builtin_ia32_pmovqb512_mask(b,w1,(unsigned char)m);v8si r4=__builtin_ia32_pmovqd512_mask(b,w3,(unsigned char)(m>>5));"
            "v8hi r5=__builtin_ia32_pmovqw512_mask(b,w4,(unsigned char)(m>>9));v32qi r6=__builtin_ia32_pmovuswb512_mask(c,w5,(unsigned)m);"
            "v32qi r7=__builtin_ia32_pmovwb512_mask(c,w5,(unsigned)(m>>2));FOLD(r1);FOLD(r2);FOLD(r3);FOLD(r4);FOLD(r5);FOLD(r6);FOLD(r7);"
            "return (int)h;}",
        },
        {
            "x86-gathers-scatters",
            "int tested(int n){unsigned h=2166136261u;int table[64];long long wide[64];for(int i=0;i<64;i++){table[i]=i*7+n;wide[i]=(long long)i*i-n;}"
            "v16si idx;for(int i=0;i<16;i++)idx[i]=(i*5+n)&63;v8si idx8;for(int i=0;i<8;i++)idx8[i]=(i*3+n)&31;"
            "v8di idx64;for(int i=0;i<8;i++)idx64[i]=(i*11+n)&63;v16si src;fill(&src,64,n);v8di src2;fill(&src2,64,n+1);unsigned long long m=mask(n);"
            "v16si g1=__builtin_ia32_gathersiv16si(src,table,idx,(unsigned short)m,4);v8di g2=__builtin_ia32_gathersiv8di(src2,wide,idx8,(unsigned char)m,8);"
            "v8di g3=__builtin_ia32_gatherdiv8di(src2,wide,idx64,(unsigned char)(m>>3),8);v16si g4=__builtin_ia32_gathersiv16si(src,(char*)table+4,idx,0xffff,2);"
            "FOLD(g1);FOLD(g2);FOLD(g3);FOLD(g4);int dst[128];long long dst2[64];fill(dst,sizeof(dst),n+2);fill(dst2,sizeof(dst2),n+3);"
            "__builtin_ia32_scatterdiv16si(dst,(unsigned char)m,idx64,idx8,4);__builtin_ia32_scatterdiv8di(dst2,(unsigned char)(m>>1),idx64,src2,8);"
            "__builtin_ia32_scattersiv16si(dst,(unsigned short)(m>>4),idx,src,4);FOLD(dst);FOLD(dst2);"
            "return (int)h;}",
        },
        {
            "x86-permutes-shifts-misc",
            "int tested(int n){unsigned h=2166136261u;v32qi a,b,c;v16qi d,e,f;v64qi x,y;v16si p,q;v8di r,s;v32hi w,z;v8si t,u;v8sf ff;v4sf fs;v2df fd;v4df fd4;"
            "v4di vq;fill(&a,32,n);fill(&b,32,n+1);fill(&c,32,n+2);fill(&d,16,n+3);fill(&e,16,n+4);fill(&f,16,n+5);fill(&x,64,n+6);fill(&y,64,n+7);"
            "fill(&p,64,n+8);fill(&q,64,n+9);fill(&r,64,n+10);fill(&s,64,n+11);fill(&w,64,n+12);fill(&z,64,n+13);fill(&t,32,n+14);fill(&u,32,n+15);"
            "fill(&vq,32,n+16);for(int i=0;i<8;i++)ff[i]=(float)(i*3-n);for(int i=0;i<4;i++)fs[i]=(float)(n-i);fd[0]=-1.5*n;fd[1]=2.0;"
            "for(int i=0;i<4;i++)fd4[i]=(double)(i-2)*n;unsigned long long m=mask(n);"
            "v32qi bl=__builtin_ia32_pblendvb256(a,b,c);v16qi bl2=__builtin_ia32_pblendvb128(d,e,f);FOLD(bl);FOLD(bl2);"
            "unsigned long long sb=__builtin_ia32_vpshufbitqmb512_mask(x,y,m);unsigned short sb2=__builtin_ia32_vpshufbitqmb128_mask(d,e,(unsigned short)m);"
            "FOLD(sb);FOLD(sb2);v16si cf=__builtin_ia32_vpconflictsi_512(p&7);v8di cq=__builtin_ia32_vpconflictdi_512(r&3);FOLD(cf);FOLD(cq);"
            "v8di pv=__builtin_ia32_permvardi512(r,s);v16si pv2=__builtin_ia32_permvarsi512(p,q);v8si pv3=__builtin_ia32_permvarsi256(t,u);"
            "v32hi pv4=__builtin_ia32_permvarhi512(w,z);FOLD(pv);FOLD(pv2);FOLD(pv3);FOLD(pv4);"
            "v16si sh[]={__builtin_ia32_psllv16si(p,q&63),__builtin_ia32_psrlv16si(p,q&63),__builtin_ia32_psrav16si(p,q&63)};FOLD(sh);"
            "v8di shq[]={__builtin_ia32_psllv8di(r,s&127),__builtin_ia32_psrlv8di(r,s&127),__builtin_ia32_psrav8di(r,s&127)};FOLD(shq);"
            "v8di mu=__builtin_ia32_pmuludq512(p,q);v8di sa=__builtin_ia32_psadbw512((v64qu)x,(v64qu)y);v64qi ms=__builtin_ia32_vpmultishiftqb512(x,y);"
            "FOLD(mu);FOLD(sa);FOLD(ms);v8di ae=__builtin_ia32_aesenc512(r,s);v8di al=__builtin_ia32_aesenclast512(r,s);v4di ae2=__builtin_ia32_aesenc256(vq,vq);"
            "FOLD(ae);FOLD(al);FOLD(ae2);v8di sr=__builtin_ia32_psraqi512(r,n&63);v2di sr2=__builtin_ia32_psraqi128((v2di){n,-n},70);FOLD(sr);FOLD(sr2);"
            "int mm[]={__builtin_ia32_movmskps(fs),__builtin_ia32_movmskps256(ff),__builtin_ia32_movmskpd(fd),__builtin_ia32_movmskpd256(fd4),"
            "__builtin_ia32_pmovmskb256(a)};FOLD(mm);long long ex[]={__builtin_ia32_vec_ext_v4di(vq,0),__builtin_ia32_vec_ext_v4di(vq,3)};FOLD(ex);"
            "return (int)h;}",
        },
        {
            "x86-compares-selects-moves",
            "int tested(int n){unsigned h=2166136261u;v64qi a,b;v16si c,d;v8di e,f;v32hi g,i;v16qi j,k;fill(&a,64,n);fill(&b,64,n+1);fill(&c,64,n+2);"
            "fill(&d,64,n+3);fill(&e,64,n+4);fill(&f,64,n+5);fill(&g,64,n+6);fill(&i,64,n+7);fill(&j,16,n+8);fill(&k,16,n+9);unsigned long long m=mask(n);"
            "unsigned long long k1=__builtin_ia32_cmpb512_mask(a,b,1,m),k2=__builtin_ia32_ucmpb512_mask(a,b,6,~m);"
            "unsigned short k3=__builtin_ia32_cmpd512_mask(c,d,2,(unsigned short)m),k4=__builtin_ia32_ucmpd512_mask(c,d,5,0xffff);"
            "unsigned char k5=__builtin_ia32_cmpq512_mask(e,f,4,(unsigned char)m);unsigned k6=__builtin_ia32_cmpw512_mask(g,i,0,(unsigned)m);"
            "unsigned short k7=__builtin_ia32_cmpb128_mask(j,k,1,0xffff);FOLD(k1);FOLD(k2);FOLD(k3);FOLD(k4);FOLD(k5);FOLD(k6);FOLD(k7);"
            "v64qi s1=__builtin_ia32_selectb_512(m,a,b);v16si s2=__builtin_ia32_selectd_512((unsigned short)m,c,d);"
            "v8di s3=__builtin_ia32_selectq_512((unsigned char)m,e,f);v32hi s4=__builtin_ia32_selectw_512((unsigned)m,g,i);"
            "v16qi s5=__builtin_ia32_selectb_128((unsigned short)m,j,k);FOLD(s1);FOLD(s2);FOLD(s3);FOLD(s4);FOLD(s5);"
            "char buf[200];fill(buf,200,n);v64qi l1=__builtin_ia32_loaddquqi512_mask((const v64qi*)(buf+(n&7)),a,m);"
            "v16si l2=__builtin_ia32_loaddqusi512_mask((const int*)(buf+3),c,(unsigned short)m);FOLD(l1);FOLD(l2);"
            "__builtin_ia32_storedquqi512_mask((v64qi*)(buf+5),b,m>>3);__builtin_ia32_storedqusi512_mask((int*)(buf+64),d,(unsigned short)(m>>9));FOLD(buf);"
            "return (int)h;}",
        },
        {
            "x86-elements-bits",
            "int tested(int n){unsigned h=2166136261u;v64qi a,b;v16qi x,y;v32hi w,w2;v16si d;v8di q;v2di l,l2;v4si v,v2;v32qi c,c2;fill(&a,64,n);"
            "fill(&b,64,n+1);fill(&x,16,n+2);fill(&y,16,n+3);fill(&w,64,n+4);fill(&w2,64,n+5);fill(&d,64,n+6);fill(&q,64,n+7);fill(&l,16,n+8);"
            "fill(&l2,16,n+9);fill(&v,16,n+10);fill(&v2,16,n+11);fill(&c,32,n+12);fill(&c2,32,n+13);"
            "v64qi sh=__builtin_ia32_pshufb512(a,b);v16qi sh2=__builtin_ia32_pshufb128(x,y);v32qi sh3=__builtin_ia32_pshufb256(c,c2);FOLD(sh);FOLD(sh2);FOLD(sh3);"
            "v16si ma=__builtin_ia32_pmaddwd512(w,w2);v32hi mb=__builtin_ia32_pmaddubsw512(a,b);v8hi pk=__builtin_ia32_packusdw128(v,v2);FOLD(ma);FOLD(mb);FOLD(pk);"
            "v64qi pq=__builtin_ia32_permvarqi512(a,b);v16qi pq2=__builtin_ia32_permvarqi128(x,y);FOLD(pq);FOLD(pq2);"
            "v2di ae=__builtin_ia32_aesenc128(l,l2);v2di ael=__builtin_ia32_aesenclast128(l,l2);FOLD(ae);FOLD(ael);"
            "v32hi s1=__builtin_ia32_psllwi512(w,n&15);v16si s2=__builtin_ia32_pslldi512(d,n&31);v8di s3=__builtin_ia32_psrlqi512(q,(n&63)+1);"
            "v16si s4=__builtin_ia32_psradi512(d,40);FOLD(s1);FOLD(s2);FOLD(s3);FOLD(s4);"
            "int e[]={__builtin_ia32_vec_ext_v16qi(x,5),__builtin_ia32_vec_ext_v4si(v,3),__builtin_ia32_pmovmskb128(x)};FOLD(e);"
            "v4si vs=__builtin_ia32_vec_set_v4si(v,n,2);FOLD(vs);unsigned u=(unsigned)n*2654435761u;unsigned long long uu=mask(n);"
            "unsigned bits[]={__builtin_ia32_pdep_si(u,0x0f0f0f0fu),__builtin_ia32_pext_si(u,0x00ff00f0u),__builtin_ia32_bzhi_si(u,(unsigned)n&31)};FOLD(bits);"
            "unsigned long long wide[]={__builtin_ia32_pdep_di(uu,0x00ff00ff00ff00ffull),__builtin_ia32_pext_di(uu,0xf0f0f0f0f0f0f0f0ull),"
            "__builtin_ia32_bzhi_di(uu,(unsigned long long)(n&63)+1)};FOLD(wide);"
            "v8di z=(v8di)__builtin_ia32_undef512();for(int i=0;i<8;i++)z[i]=i;FOLD(z);"
            "return (int)h;}",
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        const size_t length = strlen(check_test_x86_types) + strlen(programs[i][1]) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "x86 builtin program source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s", check_test_x86_types, programs[i][1]);
            check_test_program_source_native_limits(test, source, programs[i][0], clang, false, -1, 0, target, run_native);
            free(source);
        }
    }
}

// What the lowering refuses by name: a builtin the table doesn't name, and one whose instructions the target hasn't.
static void check_test_program_x86_refusals(CheckTest* test)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &v4);
    check_test_expect(test, parsed, "the x86 builtins' refusals' target, x86-64-v4");
    if (parsed)
    {
        check_test_program_boundary_target(test, "typedef char v64qi __attribute__((vector_size(64)));int tested(int n){v64qi a={(char)n},b={1};"
                                                 "v64qi c=__builtin_ia32_vpmultishiftqb512(a,b);return c[0];}",
                                           "x86-builtin-without-its-features", true, false, 0, 3, "x86 builtins whose instructions the target hasn't", &v4);
    }
}

static __attribute__((noinline)) void check_test_program_x86(CheckTest* test, const char* clang)
{
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("znver5", strlen("znver5"), &target);
    check_test_expect(test, parsed, "the x86 builtin programs' target, znver5");
    if (parsed)
    {
        // The features the programs' rows take, which the host must have and save to run them.
        const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) |
                                  BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512VBMI) |
                                  BUSTER_X86_FEATURE(AVX512VBMI2) | BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512VNNI) |
                                  BUSTER_X86_FEATURE(GFNI) | BUSTER_X86_FEATURE(VAES) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(BMI2);
        const X86Target native = x86_target_native();
        const bool run_native = (native.features & required) == required;
        if (!run_native)
        {
            printf("  x86 builtins: native execution skipped (the host lacks their features or their saved state); the programs compile and link\n");
        }
        check_test_program_build_release();
        check_test_program_march = "-march=znver5";
        check_test_program_x86_sources(test, clang, &target, run_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
        check_test_program_x86_refusals(test);
    }
}
