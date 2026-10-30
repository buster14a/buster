// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's rest of clang's x86 builtins (c_builtin_supplementary_scope.inc) in the lowering, as c_check_program_x86_test.c
// tests the others: each family's builtins called directly at their prototypes' types against clang -O0's code of the same
// source for znver5, at their immediates' edges, every result folded into the program's hash, run where the host has the
// features clang's code takes and compiled and linked elsewhere. Where a program passes an immediate outside its range,
// clang's -Wargument-outside-range is turned off by its pragma, as code that clang's code generation still takes: its
// code masks the immediate as the instruction's field reads it (an index's low bits, a predicate's), and so does ours.
// Include after c_check_program_x86_test.c, whose types, helpers and harness it takes.

static void check_test_program_x86_rest_sources(CheckTest* test, const char* clang, const X86Target* target, bool run_native)
{
    static const CheckTestX86Program programs[] = {
        {
            // The unsigned compares of quadwords and words at 16 and 32 bytes, by every predicate under masks of none, one
            // lane, others and all, the second operands sharing some elements with the first; then a predicate past 7,
            // which clang's code and the instruction take modulo 8.
            "x86-rest-unsigned-compares",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "#define UC(P) {unsigned char k[]={__builtin_ia32_ucmpq128_mask(a,b,P,0xff),__builtin_ia32_ucmpq128_mask(a,b,P,(unsigned char)m),"
            "__builtin_ia32_ucmpq256_mask(c,d,P,0xff),__builtin_ia32_ucmpq256_mask(c,d,P,1),__builtin_ia32_ucmpw128_mask(e,f,P,(unsigned char)(m>>8)),"
            "__builtin_ia32_ucmpw128_mask(e,f,P,0)};unsigned short w[]={__builtin_ia32_ucmpw256_mask(g,i,P,0xffff),"
            "__builtin_ia32_ucmpw256_mask(g,i,P,(unsigned short)(m>>16))};FOLD(k);FOLD(w);}\n"
            "int tested(int n){unsigned h=2166136261u;v2di a,b;v4di c,d;v8hi e,f;v16hi g,i;fill(&a,16,n);fill(&b,16,n+1);fill(&c,32,n+2);"
            "fill(&d,32,n+3);fill(&e,16,n+4);fill(&f,16,n+5);fill(&g,32,n+6);fill(&i,32,n+7);b[1]=a[1];d[2]=c[2];for(int k=0;k<8;k+=3)f[k]=e[k];"
            "for(int k=0;k<16;k+=5)i[k]=g[k];unsigned long long m=mask(n);UC(0)UC(1)UC(2)UC(3)UC(4)UC(5)UC(6)UC(7)UC(9)UC(15)return (int)h;}",
        },
        {
            // A quadword's extract and the insertions of a byte and a quadword at every index, each index's edges, and
            // past them, where clang's code takes the index's low bits.
            "x86-rest-elements",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "int tested(int n){unsigned h=2166136261u;v2di a;v16qi b;fill(&a,16,n);fill(&b,16,n+1);"
            "long long e[]={__builtin_ia32_vec_ext_v2di(a,0),__builtin_ia32_vec_ext_v2di(a,1),__builtin_ia32_vec_ext_v2di(a,2),"
            "__builtin_ia32_vec_ext_v2di(a,-1)};FOLD(e);char c=(char)(n*37+5);"
            "v16qi s[]={__builtin_ia32_vec_set_v16qi(b,c,0),__builtin_ia32_vec_set_v16qi(b,c,1),__builtin_ia32_vec_set_v16qi(b,c,7),"
            "__builtin_ia32_vec_set_v16qi(b,c,8),__builtin_ia32_vec_set_v16qi(b,(char)-1,14),__builtin_ia32_vec_set_v16qi(b,c,15),"
            "__builtin_ia32_vec_set_v16qi(b,c,16),__builtin_ia32_vec_set_v16qi(b,c,-1)};FOLD(s);long long q=(long long)n*0x123456789ll-7;"
            "v2di t[]={__builtin_ia32_vec_set_v2di(a,q,0),__builtin_ia32_vec_set_v2di(a,q,1),__builtin_ia32_vec_set_v2di(a,-1,1),"
            "__builtin_ia32_vec_set_v2di(a,q,2),__builtin_ia32_vec_set_v2di(a,q,3)};FOLD(t);return (int)h;}",
        },
        {
            // The rotates of doublewords and quadwords left and right at each width, by counts around the elements' bits
            // and the immediate's whole byte, and past it.
            "x86-rest-rotates",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "#define ROT(name,T,v) {T r[]={__builtin_ia32_##name(v,0),__builtin_ia32_##name(v,1),__builtin_ia32_##name(v,7),__builtin_ia32_##name(v,31),"
            "__builtin_ia32_##name(v,32),__builtin_ia32_##name(v,33),__builtin_ia32_##name(v,63),__builtin_ia32_##name(v,64),__builtin_ia32_##name(v,65),"
            "__builtin_ia32_##name(v,200),__builtin_ia32_##name(v,255),__builtin_ia32_##name(v,256),__builtin_ia32_##name(v,-1)};FOLD(r);}\n"
            "int tested(int n){unsigned h=2166136261u;v4si a;v8si b;v16si c;v2di d;v4di e;v8di f;fill(&a,16,n);fill(&b,32,n+1);fill(&c,64,n+2);"
            "fill(&d,16,n+3);fill(&e,32,n+4);fill(&f,64,n+5);ROT(prold128,v4si,a)ROT(prold256,v8si,b)ROT(prold512,v16si,c)ROT(prord128,v4si,a)"
            "ROT(prord256,v8si,b)ROT(prord512,v16si,c)ROT(prolq128,v2di,d)ROT(prolq256,v4di,e)ROT(prolq512,v8di,f)ROT(prorq128,v2di,d)"
            "ROT(prorq256,v4di,e)ROT(prorq512,v8di,f)return (int)h;}",
        },
        {
            // One vector or two by an immediate at the widths the inventory lacked, each at immediates of every kind of
            // edge: 0, 1, the bits a field takes (3, 15) and one past them, the lanes' and elements' counts, mixed bits,
            // bits 3 and 7 (VPERM2F128's zeroing), 255 and past the byte; and with its operands the other way round.
            "x86-rest-shuffles",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "#define I2(name,T,a,b) {T r[]={__builtin_ia32_##name(a,b,0),__builtin_ia32_##name(a,b,1),__builtin_ia32_##name(a,b,2),__builtin_ia32_##name(a,b,3),"
            "__builtin_ia32_##name(a,b,4),__builtin_ia32_##name(a,b,7),__builtin_ia32_##name(a,b,8),__builtin_ia32_##name(a,b,15),__builtin_ia32_##name(a,b,16),"
            "__builtin_ia32_##name(a,b,17),__builtin_ia32_##name(a,b,31),__builtin_ia32_##name(a,b,32),__builtin_ia32_##name(a,b,0x5a),"
            "__builtin_ia32_##name(a,b,0x88),__builtin_ia32_##name(a,b,0xa5),__builtin_ia32_##name(a,b,255),__builtin_ia32_##name(a,b,256),"
            "__builtin_ia32_##name(b,a,0x1b),__builtin_ia32_##name(a,b,-1)};FOLD(r);}\n"
            "#define I1(name,T,a) {T r[]={__builtin_ia32_##name(a,0),__builtin_ia32_##name(a,1),__builtin_ia32_##name(a,2),__builtin_ia32_##name(a,3),"
            "__builtin_ia32_##name(a,4),__builtin_ia32_##name(a,7),__builtin_ia32_##name(a,8),__builtin_ia32_##name(a,15),__builtin_ia32_##name(a,16),"
            "__builtin_ia32_##name(a,17),__builtin_ia32_##name(a,31),__builtin_ia32_##name(a,32),__builtin_ia32_##name(a,0x5a),"
            "__builtin_ia32_##name(a,0x88),__builtin_ia32_##name(a,0xa5),__builtin_ia32_##name(a,255),__builtin_ia32_##name(a,256),"
            "__builtin_ia32_##name(a,-1)};FOLD(r);}\n"
            "int tested(int n){unsigned h=2166136261u;v4si a4,b4;v8si a8,b8;v16si a16,b16;v2di q2,r2;v4di q4,r4;v8di q8,r8;v4sf f4,g4;v8sf f8,g8;"
            "v16sf f16,g16;v2df d2,e2;v4df d4,e4;v8df d8,e8;v16hi w16,x16;v32hi w32,x32;v32qi c32,e32;v64qi c64,e64;"
            "fill(&a4,16,n);fill(&b4,16,n+1);fill(&a8,32,n+2);fill(&b8,32,n+3);fill(&a16,64,n+4);fill(&b16,64,n+5);fill(&q2,16,n+6);fill(&r2,16,n+7);"
            "fill(&q4,32,n+8);fill(&r4,32,n+9);fill(&q8,64,n+10);fill(&r8,64,n+11);fill(&w16,32,n+12);fill(&x16,32,n+13);fill(&w32,64,n+14);"
            "fill(&x32,64,n+15);fill(&c32,32,n+16);fill(&e32,32,n+17);fill(&c64,64,n+18);fill(&e64,64,n+19);"
            "for(int i=0;i<4;i++){f4[i]=(float)(i*3-n);g4[i]=(float)n*0.5f-i;d4[i]=(double)(i-2)*n;e4[i]=1.5*i+n;}"
            "for(int i=0;i<8;i++){f8[i]=(float)(i-n);g8[i]=(float)(n+i)*0.25f;d8[i]=(double)n/(i+1);e8[i]=-(double)i*n;}"
            "for(int i=0;i<16;i++){f16[i]=(float)(i*n);g16[i]=(float)(i+n)*-1.5f;}d2[0]=n;d2[1]=-0.5;e2[0]=3.25;e2[1]=-(double)n;"
            "I2(alignd128,v4si,a4,b4)I2(alignd256,v8si,a8,b8)I2(alignq128,v2di,q2,r2)I2(alignq256,v4di,q4,r4)"
            "I2(shuf_f32x4,v16sf,f16,g16)I2(shuf_f64x2,v8df,d8,e8)I2(shuf_f32x4_256,v8sf,f8,g8)I2(shuf_f64x2_256,v4df,d4,e4)"
            "I2(shuf_i32x4_256,v8si,a8,b8)I2(shuf_i64x2_256,v4di,q4,r4)I2(shufpd256,v4df,d4,e4)I2(shufpd512,v8df,d8,e8)"
            "I2(shufps256,v8sf,f8,g8)I2(shufps512,v16sf,f16,g16)I1(pshufhw256,v16hi,w16)I1(pshufhw512,v32hi,w32)I1(pshuflw256,v16hi,w16)"
            "I1(pshuflw512,v32hi,w32)I1(pslldqi256_byteshift,v32qi,c32)I1(pslldqi512_byteshift,v64qi,c64)I1(psrldqi256_byteshift,v32qi,c32)"
            "I1(psrldqi512_byteshift,v64qi,c64)I2(palignr512,v64qi,c64,e64)I1(permdf256,v4df,d4)I1(permdf512,v8df,d8)I1(permdi512,v8di,q8)"
            "I1(vpermilpd,v2df,d2)I1(vpermilpd256,v4df,d4)I1(vpermilpd512,v8df,d8)I1(vpermilps,v4sf,f4)I1(vpermilps256,v8sf,f8)"
            "I1(vpermilps512,v16sf,f16)I2(vperm2f128_pd256,v4df,d4,e4)I2(vperm2f128_ps256,v8sf,f8,g8)I2(vperm2f128_si256,v8si,a8,b8)"
            "I2(permti256,v4di,q4,r4)I2(blendpd,v2df,d2,e2)I2(blendpd256,v4df,d4,e4)I2(blendps,v4sf,f4,g4)I2(blendps256,v8sf,f8,g8)"
            "I2(pblendd128,v4si,a4,b4)I2(pblendd256,v8si,a8,b8)I2(pblendw256,v16hi,w16,x16)return (int)h;}",
        },
        {
            // The elements of 256-bit vectors and of 8-byte ones extracted and inserted at every index, a float's too, and
            // past the indices; the lanes of 128 and 256 bits inserted and extracted, under masks of none, some and all, at
            // every index and past them; and INSERTPS at immediates that pick each element of b, each place in a and each
            // zero mask.
            "x86-rest-elements-lanes",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "typedef short v4hi __attribute__((vector_size(8)));\n"
            "#define E4(name,v) __builtin_ia32_##name(v,0),__builtin_ia32_##name(v,1),__builtin_ia32_##name(v,2),__builtin_ia32_##name(v,3)\n"
            "#define E8(name,v) E4(name,v),__builtin_ia32_##name(v,4),__builtin_ia32_##name(v,5),__builtin_ia32_##name(v,6),__builtin_ia32_##name(v,7)\n"
            "#define E16(name,v) E8(name,v),__builtin_ia32_##name(v,8),__builtin_ia32_##name(v,9),__builtin_ia32_##name(v,10),__builtin_ia32_##name(v,11),"
            "__builtin_ia32_##name(v,12),__builtin_ia32_##name(v,13),__builtin_ia32_##name(v,14),__builtin_ia32_##name(v,15)\n"
            "#define E32(name,v) E16(name,v),__builtin_ia32_##name(v,16),__builtin_ia32_##name(v,17),__builtin_ia32_##name(v,18),__builtin_ia32_##name(v,19),"
            "__builtin_ia32_##name(v,20),__builtin_ia32_##name(v,21),__builtin_ia32_##name(v,22),__builtin_ia32_##name(v,23),__builtin_ia32_##name(v,24),"
            "__builtin_ia32_##name(v,25),__builtin_ia32_##name(v,26),__builtin_ia32_##name(v,27),__builtin_ia32_##name(v,28),__builtin_ia32_##name(v,29),"
            "__builtin_ia32_##name(v,30),__builtin_ia32_##name(v,31)\n"
            "#define S(name,v,x,i) __builtin_ia32_##name(v,x,i)\n"
            "#define S4(name,v,x) S(name,v,x,0),S(name,v,x,1),S(name,v,x,2),S(name,v,x,3)\n"
            "#define S8(name,v,x) S4(name,v,x),S(name,v,x,4),S(name,v,x,5),S(name,v,x,6),S(name,v,x,7)\n"
            "#define S16(name,v,x) S8(name,v,x),S(name,v,x,8),S(name,v,x,9),S(name,v,x,10),S(name,v,x,11),S(name,v,x,12),S(name,v,x,13),S(name,v,x,14),"
            "S(name,v,x,15)\n"
            "#define S32(name,v,x) S16(name,v,x),S(name,v,x,16),S(name,v,x,17),S(name,v,x,18),S(name,v,x,19),S(name,v,x,20),S(name,v,x,21),S(name,v,x,22),"
            "S(name,v,x,23),S(name,v,x,24),S(name,v,x,25),S(name,v,x,26),S(name,v,x,27),S(name,v,x,28),S(name,v,x,29),S(name,v,x,30),S(name,v,x,31)\n"
            "#define L2(name,T,a,b) {T r[]={__builtin_ia32_##name(a,b,0),__builtin_ia32_##name(a,b,1),__builtin_ia32_##name(a,b,2),"
            "__builtin_ia32_##name(a,b,3),__builtin_ia32_##name(a,b,4),__builtin_ia32_##name(a,b,-1)};FOLD(r);}\n"
            "#define X2(name,T,a,w) {T r[]={__builtin_ia32_##name(a,0,w,0xff),__builtin_ia32_##name(a,1,w,0xff),__builtin_ia32_##name(a,2,w,0xff),"
            "__builtin_ia32_##name(a,3,w,0xff),__builtin_ia32_##name(a,4,w,0xff),__builtin_ia32_##name(a,1,w,(unsigned char)m),"
            "__builtin_ia32_##name(a,0,w,(unsigned char)(m>>8)),__builtin_ia32_##name(a,3,w,0),__builtin_ia32_##name(a,-1,w,(unsigned char)(m>>16))};FOLD(r);}\n"
            "int tested(int n){unsigned h=2166136261u;v4sf f;v8si a;v16hi w;v32qi c;v4hi s;v4di q;v16sf f16;v8sf f8,g8;v4sf f4,g4;v8df d8;v4df d4,e4;"
            "v2df d2;v16si i16;v8si i8;v4si i4;v8di q8;v2di q2;fill(&a,32,n);fill(&w,32,n+1);fill(&c,32,n+2);fill(&s,8,n+3);fill(&q,32,n+4);"
            "fill(&i16,64,n+5);fill(&i8,32,n+6);fill(&i4,16,n+7);fill(&q8,64,n+8);fill(&q2,16,n+9);unsigned long long m=mask(n);"
            "for(int i=0;i<4;i++){f[i]=(float)(i*7-n)*0.5f;f4[i]=(float)(i+n);g4[i]=-(float)(i*n)-0.25f;d4[i]=(double)i*n;e4[i]=-1.5*i;}"
            "for(int i=0;i<8;i++){f8[i]=(float)(n-i)*1.25f;g8[i]=(float)(i*n);d8[i]=(double)(i-n)/3;}for(int i=0;i<16;i++)f16[i]=(float)(i*i-n);"
            "d2[0]=n*0.75;d2[1]=-2.5;"
            "float fe[]={E4(vec_ext_v4sf,f),__builtin_ia32_vec_ext_v4sf(f,4),__builtin_ia32_vec_ext_v4sf(f,-1)};FOLD(fe);"
            "int ie[]={E8(vec_ext_v8si,a),__builtin_ia32_vec_ext_v8si(a,8),__builtin_ia32_vec_ext_v8si(a,-1)};FOLD(ie);"
            "short we[]={E16(vec_ext_v16hi,w),__builtin_ia32_vec_ext_v16hi(w,16),__builtin_ia32_vec_ext_v4hi(s,0),__builtin_ia32_vec_ext_v4hi(s,1),"
            "__builtin_ia32_vec_ext_v4hi(s,2),__builtin_ia32_vec_ext_v4hi(s,3),__builtin_ia32_vec_ext_v4hi(s,4)};FOLD(we);"
            "char ce[]={E32(vec_ext_v32qi,c),__builtin_ia32_vec_ext_v32qi(c,32),__builtin_ia32_vec_ext_v32qi(c,-1)};FOLD(ce);"
            "short x=(short)(n*977-3);char y=(char)(n*31+7);long long z=(long long)n*0x0123456789ll-5;int t=n*-12345+1;"
            "v4hi ss[]={S4(vec_set_v4hi,s,x),S(vec_set_v4hi,s,x,4),S(vec_set_v4hi,s,x,-1)};FOLD(ss);"
            "v8si as[]={S8(vec_set_v8si,a,t),S(vec_set_v8si,a,t,8),S(vec_set_v8si,a,-1,7)};FOLD(as);"
            "v4di qs[]={S4(vec_set_v4di,q,z),S(vec_set_v4di,q,z,4),S(vec_set_v4di,q,-1,2)};FOLD(qs);"
            "v16hi ws[]={S16(vec_set_v16hi,w,x),S(vec_set_v16hi,w,x,16)};FOLD(ws);v32qi cs[]={S32(vec_set_v32qi,c,y),S(vec_set_v32qi,c,y,33)};FOLD(cs);"
            "L2(insertf32x4,v16sf,f16,f4)L2(insertf32x4_256,v8sf,f8,f4)L2(insertf32x8,v16sf,f16,g8)L2(insertf64x2_256,v4df,d4,d2)"
            "L2(insertf64x2_512,v8df,d8,d2)L2(insertf64x4,v8df,d8,e4)L2(inserti32x4_256,v8si,i8,i4)L2(inserti32x8,v16si,i16,i8)"
            "L2(inserti64x2_256,v4di,q,q2)L2(inserti64x2_512,v8di,q8,q2)L2(vinsertf128_pd256,v4df,d4,d2)L2(vinsertf128_ps256,v8sf,f8,g4)"
            "L2(vinsertf128_si256,v8si,i8,i4)"
            "X2(extractf32x4_mask,v4sf,f16,g4)X2(extractf32x4_256_mask,v4sf,f8,g4)X2(extractf32x8_mask,v8sf,f16,g8)X2(extractf64x2_256_mask,v2df,d4,d2)"
            "X2(extractf64x2_512_mask,v2df,d8,d2)X2(extractf64x4_mask,v4df,d8,e4)X2(extracti32x4_256_mask,v4si,i8,i4)X2(extracti32x8_mask,v8si,i16,i8)"
            "X2(extracti64x2_256_mask,v2di,q,q2)X2(extracti64x2_512_mask,v2di,q8,q2)"
            "v2di xl[]={__builtin_ia32_extract128i256(q,0),__builtin_ia32_extract128i256(q,1),__builtin_ia32_extract128i256(q,2),"
            "__builtin_ia32_extract128i256(q,-1)};FOLD(xl);"
            "v4sf ip[]={__builtin_ia32_insertps128(f4,g4,0),__builtin_ia32_insertps128(f4,g4,0x10),__builtin_ia32_insertps128(f4,g4,0x60),"
            "__builtin_ia32_insertps128(f4,g4,0xb0),__builtin_ia32_insertps128(f4,g4,0xc5),__builtin_ia32_insertps128(f4,g4,0x0f),"
            "__builtin_ia32_insertps128(g4,f4,0x5a),__builtin_ia32_insertps128(f4,g4,0xff)};FOLD(ip);return (int)h;}",
        },
        {
            // The ternary logic of 16 and 32 bytes merged under masks of none, some and all, and of every width zeroed, by
            // immediates of each kind (0, 0xff, a's, b's, c's, majority, xor and select); the concatenating shifts of words,
            // doublewords and quadwords at each width by counts around their bits; the sums of absolute differences, the
            // carry-less products (of each half of each lane by each other) and the affine transforms by mixed immediates;
            // and the masks' shifts of each width by counts around their bits and past them.
            "x86-rest-ternary-shifts-products",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "#define TL(name,T,a,b,c,k) {T r[]={__builtin_ia32_##name(a,b,c,0,k),__builtin_ia32_##name(a,b,c,0xff,k),__builtin_ia32_##name(a,b,c,0xf0,k),"
            "__builtin_ia32_##name(a,b,c,0xcc,k),__builtin_ia32_##name(a,b,c,0xaa,k),__builtin_ia32_##name(a,b,c,0xe8,k),__builtin_ia32_##name(a,b,c,0x96,k),"
            "__builtin_ia32_##name(a,b,c,0xca,k),__builtin_ia32_##name(a,b,c,0x1b,k),__builtin_ia32_##name(a,b,c,256,k),__builtin_ia32_##name(a,b,c,-1,k)};FOLD(r);}\n"
            "#define TM(name,T,a,b,c) TL(name,T,a,b,c,0)TL(name,T,a,b,c,0xff)TL(name,T,a,b,c,(unsigned char)m)\n"
            "#define TZ(name,T,a,b,c) TL(name,T,a,b,c,0)TL(name,T,a,b,c,0xffff)TL(name,T,a,b,c,(unsigned short)m)\n"
            "#define SH(name,T,a,b) {T r[]={__builtin_ia32_##name(a,b,0),__builtin_ia32_##name(a,b,1),__builtin_ia32_##name(a,b,7),__builtin_ia32_##name(a,b,15),"
            "__builtin_ia32_##name(a,b,16),__builtin_ia32_##name(a,b,17),__builtin_ia32_##name(a,b,31),__builtin_ia32_##name(a,b,32),"
            "__builtin_ia32_##name(a,b,33),__builtin_ia32_##name(a,b,63),__builtin_ia32_##name(a,b,64),__builtin_ia32_##name(a,b,0xa5),"
            "__builtin_ia32_##name(a,b,255),__builtin_ia32_##name(b,a,9),__builtin_ia32_##name(a,b,256)};FOLD(r);}\n"
            "#define SH3(name,a1,b1,a2,b2,a3,b3,T1,T2,T3) SH(name##128,T1,a1,b1)SH(name##256,T2,a2,b2)SH(name##512,T3,a3,b3)\n"
            "#define IM(name,T,a,b) {T r[]={__builtin_ia32_##name(a,b,0),__builtin_ia32_##name(a,b,1),__builtin_ia32_##name(a,b,2),__builtin_ia32_##name(a,b,3),"
            "__builtin_ia32_##name(a,b,4),__builtin_ia32_##name(a,b,5),__builtin_ia32_##name(a,b,7),__builtin_ia32_##name(a,b,0x10),__builtin_ia32_##name(a,b,0x11),"
            "__builtin_ia32_##name(a,b,0x1b),__builtin_ia32_##name(a,b,0x2d),__builtin_ia32_##name(a,b,0x3f),__builtin_ia32_##name(a,b,0x5a),"
            "__builtin_ia32_##name(a,b,0xa5),__builtin_ia32_##name(a,b,0xff),__builtin_ia32_##name(b,a,0x01)};FOLD(r);}\n"
            "#define KS(name,T,v) {T r[]={__builtin_ia32_##name(v,0),__builtin_ia32_##name(v,1),__builtin_ia32_##name(v,3),__builtin_ia32_##name(v,7),"
            "__builtin_ia32_##name(v,8),__builtin_ia32_##name(v,15),__builtin_ia32_##name(v,16),__builtin_ia32_##name(v,31),__builtin_ia32_##name(v,32),"
            "__builtin_ia32_##name(v,63),__builtin_ia32_##name(v,64),__builtin_ia32_##name(v,255),__builtin_ia32_##name(v,256)};FOLD(r);}\n"
            "int tested(int n){unsigned h=2166136261u;v4si a4,b4,c4;v8si a8,b8,c8;v16si a16,b16,c16;v2di q2,r2,s2;v4di q4,r4,s4;v8di q8,r8,s8;"
            "v8hi w8,x8;v16hi w16,x16;v32hi w32,x32;v16qi c16b,e16b;v32qi c32b,e32b;v64qi c64b,e64b;"
            "fill(&a4,16,n);fill(&b4,16,n+1);fill(&c4,16,n+2);fill(&a8,32,n+3);fill(&b8,32,n+4);fill(&c8,32,n+5);fill(&a16,64,n+6);fill(&b16,64,n+7);"
            "fill(&c16,64,n+8);fill(&q2,16,n+9);fill(&r2,16,n+10);fill(&s2,16,n+11);fill(&q4,32,n+12);fill(&r4,32,n+13);fill(&s4,32,n+14);"
            "fill(&q8,64,n+15);fill(&r8,64,n+16);fill(&s8,64,n+17);fill(&w8,16,n+18);fill(&x8,16,n+19);fill(&w16,32,n+20);fill(&x16,32,n+21);"
            "fill(&w32,64,n+22);fill(&x32,64,n+23);fill(&c16b,16,n+24);fill(&e16b,16,n+25);fill(&c32b,32,n+26);fill(&e32b,32,n+27);"
            "fill(&c64b,64,n+28);fill(&e64b,64,n+29);unsigned long long m=mask(n);"
            "TM(pternlogd128_mask,v4si,a4,b4,c4)TM(pternlogd128_maskz,v4si,a4,b4,c4)TM(pternlogd256_mask,v8si,a8,b8,c8)"
            "TM(pternlogd256_maskz,v8si,a8,b8,c8)TZ(pternlogd512_maskz,v16si,a16,b16,c16)TM(pternlogq128_mask,v2di,q2,r2,s2)"
            "TM(pternlogq128_maskz,v2di,q2,r2,s2)TM(pternlogq256_mask,v4di,q4,r4,s4)TM(pternlogq256_maskz,v4di,q4,r4,s4)"
            "TM(pternlogq512_maskz,v8di,q8,r8,s8)"
            "SH3(vpshldd,a4,b4,a8,b8,a16,b16,v4si,v8si,v16si)SH3(vpshldq,q2,r2,q4,r4,q8,r8,v2di,v4di,v8di)"
            "SH3(vpshldw,w8,x8,w16,x16,w32,x32,v8hi,v16hi,v32hi)SH3(vpshrdd,a4,b4,a8,b8,a16,b16,v4si,v8si,v16si)"
            "SH3(vpshrdq,q2,r2,q4,r4,q8,r8,v2di,v4di,v8di)SH3(vpshrdw,w8,x8,w16,x16,w32,x32,v8hi,v16hi,v32hi)"
            "IM(dbpsadbw128,v8hi,c16b,e16b)IM(dbpsadbw256,v16hi,c32b,e32b)IM(dbpsadbw512,v32hi,c64b,e64b)"
            "IM(mpsadbw128,v8hi,c16b,e16b)IM(mpsadbw256,v16hi,c32b,e32b)"
            "IM(pclmulqdq128,v2di,q2,r2)IM(pclmulqdq256,v4di,q4,r4)IM(pclmulqdq512,v8di,q8,r8)"
            "IM(vgf2p8affineqb_v16qi,v16qi,c16b,e16b)IM(vgf2p8affineqb_v32qi,v32qi,c32b,e32b)IM(vgf2p8affineinvqb_v16qi,v16qi,c16b,e16b)"
            "IM(vgf2p8affineinvqb_v32qi,v32qi,c32b,e32b)IM(vgf2p8affineinvqb_v64qi,v64qi,c64b,e64b)"
            "unsigned char kb=(unsigned char)(m*7);unsigned short kw=(unsigned short)(m>>5);unsigned kd=(unsigned)(m>>11);unsigned long long kq=m*0x9e3779b97f4a7c15ull;"
            "KS(kshiftliqi,unsigned char,kb)KS(kshiftriqi,unsigned char,kb)KS(kshiftrihi,unsigned short,kw)KS(kshiftlisi,unsigned,kd)"
            "KS(kshiftrisi,unsigned,kd)KS(kshiftridi,unsigned long long,kq)return (int)h;}",
            BUSTER_X86_FEATURE(AVX512VBMI2) | BUSTER_X86_FEATURE(GFNI) | BUSTER_X86_FEATURE(VPCLMULQDQ) | BUSTER_X86_FEATURE(PCLMUL),
        },
        {
            // AVX10.2's sums of absolute differences of 64 bytes' blocks, which no host of the tests runs: compiled and
            // linked against clang's build of the same, test-x86-intrinsics compares their code.
            "x86-rest-mpsadbw512",
            "__attribute__((target(\"avx10.2\"))) static v32hi sad(v64qi a,v64qi b){return __builtin_ia32_mpsadbw512(a,b,0x2d);}\n"
            "int tested(int n){unsigned h=2166136261u;v64qi a,b;fill(&a,64,n);fill(&b,64,n+1);v32hi r=sad(a,b);FOLD(r);return (int)h;}",
            0,
            true,
        },
        {
            // The rest of AVX-512's operations of floats by an immediate under masks of none, some and all: the mantissas,
            // the reductions and the roundings to a scale at each width, the ranges of two vectors (and the other way
            // round), the classes into masks, the fix-ups merged and zeroed by tables of every token, and the scalar ones,
            // each at its immediate's edges and past them, of zeros, infinities, NaNs, denormals and other values, with
            // the exceptions' suppression and without.
            "x86-rest-float-immediates",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "#define GM(name,T,a,w,k) {T r[]={__builtin_ia32_##name(a,0,w,k),__builtin_ia32_##name(a,1,w,k),__builtin_ia32_##name(a,2,w,k),"
            "__builtin_ia32_##name(a,3,w,k),__builtin_ia32_##name(a,4,w,k),__builtin_ia32_##name(a,6,w,k),__builtin_ia32_##name(a,8,w,k),"
            "__builtin_ia32_##name(a,11,w,k),__builtin_ia32_##name(a,15,w,k),__builtin_ia32_##name(a,16,w,k),__builtin_ia32_##name(a,255,w,k)};FOLD(r);}\n"
            "#define GMS(name,T,a,w,k,s) {T r[]={__builtin_ia32_##name(a,0,w,k,s),__builtin_ia32_##name(a,1,w,k,s),__builtin_ia32_##name(a,2,w,k,s),"
            "__builtin_ia32_##name(a,3,w,k,s),__builtin_ia32_##name(a,4,w,k,s),__builtin_ia32_##name(a,6,w,k,s),__builtin_ia32_##name(a,8,w,k,s),"
            "__builtin_ia32_##name(a,11,w,k,s),__builtin_ia32_##name(a,15,w,k,s),__builtin_ia32_##name(a,16,w,k,s),__builtin_ia32_##name(a,255,w,k,s)};"
            "FOLD(r);}\n"
            "#define RU(name,T,a,w,k) {T r[]={__builtin_ia32_##name(a,0,w,k),__builtin_ia32_##name(a,1,w,k),__builtin_ia32_##name(a,3,w,k),"
            "__builtin_ia32_##name(a,4,w,k),__builtin_ia32_##name(a,8,w,k),__builtin_ia32_##name(a,0x11,w,k),__builtin_ia32_##name(a,0x23,w,k),"
            "__builtin_ia32_##name(a,0x3b,w,k),__builtin_ia32_##name(a,0x5c,w,k),__builtin_ia32_##name(a,0x8f,w,k),__builtin_ia32_##name(a,0xf0,w,k),"
            "__builtin_ia32_##name(a,0xff,w,k),__builtin_ia32_##name(a,256,w,k)};FOLD(r);}\n"
            "#define RUS(name,T,a,w,k,s) {T r[]={__builtin_ia32_##name(a,0,w,k,s),__builtin_ia32_##name(a,1,w,k,s),__builtin_ia32_##name(a,3,w,k,s),"
            "__builtin_ia32_##name(a,4,w,k,s),__builtin_ia32_##name(a,8,w,k,s),__builtin_ia32_##name(a,0x11,w,k,s),__builtin_ia32_##name(a,0x23,w,k,s),"
            "__builtin_ia32_##name(a,0x3b,w,k,s),__builtin_ia32_##name(a,0x5c,w,k,s),__builtin_ia32_##name(a,0x8f,w,k,s),__builtin_ia32_##name(a,0xf0,w,"
            "k,s),__builtin_ia32_##name(a,0xff,w,k,s),__builtin_ia32_##name(a,256,w,k,s)};FOLD(r);}\n"
            "#define RG(name,T,a,b,w,k) {T r[]={__builtin_ia32_##name(a,b,0,w,k),__builtin_ia32_##name(a,b,1,w,k),__builtin_ia32_##name(a,b,2,w,k),"
            "__builtin_ia32_##name(a,b,3,w,k),__builtin_ia32_##name(a,b,4,w,k),__builtin_ia32_##name(a,b,6,w,k),__builtin_ia32_##name(a,b,9,w,k),"
            "__builtin_ia32_##name(a,b,13,w,k),__builtin_ia32_##name(a,b,15,w,k),__builtin_ia32_##name(a,b,16,w,k),__builtin_ia32_##name(b,a,7,w,k)};"
            "FOLD(r);}\n"
            "#define RGS(name,T,a,b,w,k,s) {T r[]={__builtin_ia32_##name(a,b,0,w,k,s),__builtin_ia32_##name(a,b,1,w,k,s),__builtin_ia32_##name(a,b,2,w,k,"
            "s),__builtin_ia32_##name(a,b,3,w,k,s),__builtin_ia32_##name(a,b,4,w,k,s),__builtin_ia32_##name(a,b,6,w,k,s),__builtin_ia32_##name(a,b,9,w,k,"
            "s),__builtin_ia32_##name(a,b,13,w,k,s),__builtin_ia32_##name(a,b,15,w,k,s),__builtin_ia32_##name(a,b,16,w,k,s),__builtin_ia32_##name(b,a,7,"
            "w,k,s)};FOLD(r);}\n"
            "#define FC(name,a,k) {unsigned short r[]={__builtin_ia32_##name(a,0,k),__builtin_ia32_##name(a,1,k),__builtin_ia32_##name(a,2,k),"
            "__builtin_ia32_##name(a,4,k),__builtin_ia32_##name(a,8,k),__builtin_ia32_##name(a,0x10,k),__builtin_ia32_##name(a,0x20,k),"
            "__builtin_ia32_##name(a,0x40,k),__builtin_ia32_##name(a,0x80,k),__builtin_ia32_##name(a,0x81,k),__builtin_ia32_##name(a,0x5a,k),"
            "__builtin_ia32_##name(a,0xff,k),__builtin_ia32_##name(a,256,k)};FOLD(r);}\n"
            "#define FX(name,T,a,b,c,k) {T r[]={__builtin_ia32_##name(a,b,c,0,k),__builtin_ia32_##name(a,b,c,1,k),__builtin_ia32_##name(a,b,c,0x22,k),"
            "__builtin_ia32_##name(a,b,c,0x5a,k),__builtin_ia32_##name(a,b,c,0x80,k),__builtin_ia32_##name(a,b,c,0xff,k),__builtin_ia32_##name(a,b,c,256,"
            "k)};FOLD(r);}\n"
            "#define FXS(name,T,a,b,c,k,s) {T r[]={__builtin_ia32_##name(a,b,c,0,k,s),__builtin_ia32_##name(a,b,c,1,k,s),__builtin_ia32_##name(a,b,c,"
            "0x22,k,s),__builtin_ia32_##name(a,b,c,0x5a,k,s),__builtin_ia32_##name(a,b,c,0x80,k,s),__builtin_ia32_##name(a,b,c,0xff,k,s),"
            "__builtin_ia32_##name(a,b,c,256,k,s)};FOLD(r);}\n"
            "#define SM(name,T,a,b,w,k,s) {T r[]={__builtin_ia32_##name(a,b,w,k,0,s),__builtin_ia32_##name(a,b,w,k,1,s),__builtin_ia32_##name(a,b,w,k,3,"
            "s),__builtin_ia32_##name(a,b,w,k,4,s),__builtin_ia32_##name(a,b,w,k,0x13,s),__builtin_ia32_##name(a,b,w,k,0x2c,s),__builtin_ia32_##name(a,b,"
            "w,k,0xf1,s),__builtin_ia32_##name(a,b,w,k,0xff,s),__builtin_ia32_##name(a,b,w,k,256,s)};FOLD(r);}\n"
            "#define SG(name,T,a,b,w,k,s) {T r[]={__builtin_ia32_##name(a,b,0,w,k,s),__builtin_ia32_##name(a,b,3,w,k,s),__builtin_ia32_##name(a,b,6,w,k,"
            "s),__builtin_ia32_##name(a,b,9,w,k,s),__builtin_ia32_##name(a,b,15,w,k,s),__builtin_ia32_##name(a,b,16,w,k,s)};FOLD(r);}\n"
            "#define K8(k,s) GM(getmantps128_mask,v4sf,a4,w4,k)GM(getmantps256_mask,v8sf,a8,w8,k)GM(getmantpd128_mask,v2df,p2,x2,k)GM(getmantpd256_mask,"
            "v4df,p4,x4,k)GMS(getmantpd512_mask,v8df,p8,x8,k,s)RU(reduceps128_mask,v4sf,a4,w4,k)RU(reduceps256_mask,v8sf,a8,w8,k)RU(reducepd128_mask,"
            "v2df,p2,x2,k)RU(reducepd256_mask,v4df,p4,x4,k)RUS(reducepd512_mask,v8df,p8,x8,k,s)RU(rndscaleps_128_mask,v4sf,a4,w4,k)"
            "RU(rndscaleps_256_mask,v8sf,a8,w8,k)RU(rndscalepd_128_mask,v2df,p2,x2,k)RU(rndscalepd_256_mask,v4df,p4,x4,k)RG(rangeps128_mask,v4sf,a4,b4,"
            "w4,k)RG(rangeps256_mask,v8sf,a8,b8,w8,k)RG(rangepd128_mask,v2df,p2,q2,x2,k)RG(rangepd256_mask,v4df,p4,q4,x4,k)RGS(rangepd512_mask,v8df,p8,"
            "q8,x8,k,s)FC(fpclassps128_mask,a4,k)FC(fpclassps256_mask,a8,k)FC(fpclasspd128_mask,p2,k)FC(fpclasspd256_mask,p4,k)FC(fpclasspd512_mask,p8,k)"
            "FC(fpclassss_mask,a4,k)FC(fpclasssd_mask,p2,k)FX(fixupimmps128_mask,v4sf,a4,b4,t4,k)FX(fixupimmps128_maskz,v4sf,a4,b4,t4,k)"
            "FX(fixupimmps256_mask,v8sf,a8,b8,t8,k)FX(fixupimmps256_maskz,v8sf,a8,b8,t8,k)FX(fixupimmpd128_mask,v2df,p2,q2,u2,k)FX(fixupimmpd128_maskz,"
            "v2df,p2,q2,u2,k)FX(fixupimmpd256_mask,v4df,p4,q4,u4,k)FX(fixupimmpd256_maskz,v4df,p4,q4,u4,k)FXS(fixupimmpd512_mask,v8df,p8,q8,u8,k,s)"
            "FXS(fixupimmpd512_maskz,v8df,p8,q8,u8,k,s)SM(rangess128_round_mask,v4sf,a4,b4,w4,k,s)SM(rangesd128_round_mask,v2df,p2,q2,x2,k,s)"
            "SM(reducess_mask,v4sf,a4,b4,w4,k,s)SM(reducesd_mask,v2df,p2,q2,x2,k,s)SM(rndscaless_round_mask,v4sf,a4,b4,w4,k,s)SM(rndscalesd_round_mask,"
            "v2df,p2,q2,x2,k,s)SG(getmantss_round_mask,v4sf,a4,b4,w4,k,s)SG(getmantsd_round_mask,v2df,p2,q2,x2,k,s)FXS(fixupimmss_mask,v4sf,a4,b4,t4,k,s)"
            "FXS(fixupimmss_maskz,v4sf,a4,b4,t4,k,s)FXS(fixupimmsd_mask,v2df,p2,q2,u2,k,s)FXS(fixupimmsd_maskz,v2df,p2,q2,u2,k,s)\n"
            "#define K16(k,s) GMS(getmantps512_mask,v16sf,a16,w16,k,s)RUS(reduceps512_mask,v16sf,a16,w16,k,s)RGS(rangeps512_mask,v16sf,a16,b16,w16,k,s)"
            "FC(fpclassps512_mask,a16,k)FXS(fixupimmps512_mask,v16sf,a16,b16,t16,k,s)FXS(fixupimmps512_maskz,v16sf,a16,b16,t16,k,s)\n"
            "static const float fs[16]={0.0f,-0.0f,1.5f,-2.75f,1e30f,-1e-30f,1e-40f,__builtin_inff(),-__builtin_inff(),__builtin_nanf(\"\"),3.14159f,"
            "-7.0f,123456.79f,0.1f,-0.5f,2.0f};\n"
            "static const double ds[16]={0.0,-0.0,1.5,-2.75,1e300,-1e-300,4e-320,__builtin_inf(),-__builtin_inf(),__builtin_nan(\"\"),3.14159,-7.0,"
            "123456.789,0.1,-0.5,2.0};\n"
            "int tested(int n){unsigned h=2166136261u;v4sf a4,b4,w4;v8sf a8,b8,w8;v16sf a16,b16,w16;v2df p2,q2,x2;v4df p4,q4,x4;v8df p8,q8,x8;v4si t4;"
            "v8si t8;v16si t16;v2di u2;v4di u4;v8di u8;\n"
            "for(int i=0;i<16;i++){float f=fs[(i*5+n)&15],g=fs[(i*3+n*7+1)&15];double d=ds[(i*7+n)&15],e=ds[(i*11+n*3+2)&15];if(i<4){a4[i]=f;b4[i]=g;"
            "w4[i]=(float)(i-n);}if(i<2){p2[i]=d;q2[i]=e;x2[i]=-d;}if(i<8){a8[i]=g;b8[i]=f;w8[i]=(float)(n*i);p8[i]=d;q8[i]=e;x8[i]=(double)(i+n);}"
            "if(i<4){p4[i]=e;q4[i]=d;x4[i]=(double)i;}a16[i]=f*(float)(i%3);b16[i]=g;w16[i]=-(float)i;}\n"
            "fill(&t4,16,n+1);fill(&t8,32,n+2);fill(&t16,64,n+3);fill(&u2,16,n+4);fill(&u4,32,n+5);fill(&u8,64,n+6);unsigned long long m=mask(n);"
            "unsigned char k8=(unsigned char)m;unsigned short k16=(unsigned short)(m>>8);\n"
            "K8(k8,4)K8(0,8)K8(0xff,12)K16(k16,8)K16(0,4)K16(0xffff,12)\n"
            "return (int)h;}",
        },
    };
    const X86Target native = x86_target_native();
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        const bool run = run_native && !programs[i].link_only && (native.features & programs[i].features) == programs[i].features;
        if (run_native && !programs[i].link_only && !run)
        {
            printf("  %s: native execution skipped (the host lacks features clang's code of it takes); the program compiles and links\n",
                   programs[i].name);
        }
        check_test_program_x86_source(test, clang, target, run, programs[i].name, programs[i].body);
    }
}

// The rest's programs at znver5's target, where the host runs what its features allow (check_test_program_x86's rule).
static __attribute__((noinline)) void check_test_program_x86_rest(CheckTest* test, const char* clang)
{
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("znver5", strlen("znver5"), &target);
    check_test_expect(test, parsed, "the rest of the x86 builtins' programs' target, znver5");
    if (parsed)
    {
        const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ) |
                                  BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(BMI2);
        const X86Target native = x86_target_native();
        const bool run_native = (native.features & required) == required;
        if (!run_native)
        {
            printf("  the rest of the x86 builtins: native execution skipped (the host lacks their features or their saved state); the programs compile and "
                   "link\n");
        }
        check_test_program_build_release();
        check_test_program_march = "-march=znver5";
        check_test_program_x86_rest_sources(test, clang, &target, run_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
    }
}
