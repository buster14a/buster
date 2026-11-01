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
        {
            // The compares of floats at every predicate and past them, where clang's code takes the immediate's low 5 bits
            // (or a scalar one's whole byte, which the instruction takes so): into vectors, into masks under masks of
            // none, some and all (the 64-byte ones with a rounding control that clang's code ignores, the scalar ones with
            // the exceptions' suppression and without), and into ints (vcomiss, vcomisd), of zeros of each sign, equal
            // values, infinities and NaNs.
            "x86-rest-float-compares",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "#define P8(F,o,...) F(o+0,__VA_ARGS__)F(o+1,__VA_ARGS__)F(o+2,__VA_ARGS__)F(o+3,__VA_ARGS__)F(o+4,__VA_ARGS__)F(o+5,__VA_ARGS__)F(o+6,"
            "__VA_ARGS__)F(o+7,__VA_ARGS__)\n"
            "#define P32(F,...) P8(F,0,__VA_ARGS__)P8(F,8,__VA_ARGS__)P8(F,16,__VA_ARGS__)P8(F,24,__VA_ARGS__)F(32,__VA_ARGS__)F(0x3b,__VA_ARGS__)F(-1,"
            "__VA_ARGS__)\n"
            "#define VA(p,N,A,B) __builtin_ia32_##N(A,B,p),\n"
            "#define MA(p,N,A,B,K) __builtin_ia32_##N(A,B,p,K),\n"
            "#define MS(p,N,A,B,K,S) __builtin_ia32_##N(A,B,p,K,S),\n"
            "#define V(N,T,A,B) {T r[]={P32(VA,N,A,B)};FOLD(r);}\n"
            "#define M(N,A,B,K) {unsigned short r[]={P32(MA,N,A,B,K)};FOLD(r);}\n"
            "#define MR(N,A,B,K,S) {unsigned short r[]={P32(MS,N,A,B,K,S)};FOLD(r);}\n"
            "#define C(N,A,B,S) {int r[]={P32(MA,N,A,B,S)};FOLD(r);}\n"
            "static const float fs[8]={1.5f,-0.0f,0.0f,__builtin_nanf(\"\"),__builtin_inff(),-2.5f,1.5f,-__builtin_inff()};\n"
            "static const double ds[8]={1.5,-0.0,0.0,__builtin_nan(\"\"),__builtin_inf(),-2.5,1.5,-__builtin_inf()};\n"
            "int tested(int n){unsigned h=2166136261u;v4sf a4,b4;v8sf a8,b8;v16sf a16,b16;v2df p2,q2;v4df p4,q4;v8df p8,q8;\n"
            "for(int i=0;i<16;i++){float f=fs[(i+n)&7],g=fs[(i*3+n*5+1)&7];double d=ds[(i+n*3)&7],e=ds[(i*5+n+2)&7];if(i<4){a4[i]=f;b4[i]=g;}if(i<2)"
            "{p2[i]=d;q2[i]=e;}\n"
            "if(i<8){a8[i]=g;b8[i]=f;p8[i]=d;q8[i]=e;}if(i<4){p4[i]=e;q4[i]=d;}a16[i]=f;b16[i]=g;}\n"
            "unsigned long long m=mask(n);unsigned char k8=(unsigned char)m;unsigned short k16=(unsigned short)(m>>8);\n"
            "V(cmpps,v4sf,a4,b4)V(cmppd,v2df,p2,q2)V(cmpps256,v8sf,a8,b8)V(cmppd256,v4df,p4,q4)V(cmpss,v4sf,a4,b4)V(cmpsd,v2df,p2,q2)\n"
            "M(cmpps128_mask,a4,b4,k8)M(cmpps128_mask,a4,b4,0xff)M(cmpps128_mask,a4,b4,0)M(cmppd128_mask,p2,q2,k8)M(cmppd128_mask,p2,q2,0xff)\n"
            "M(cmpps256_mask,a8,b8,k8)M(cmpps256_mask,a8,b8,0xff)M(cmppd256_mask,p4,q4,k8)\n"
            "MR(cmpps512_mask,a16,b16,k16,4)MR(cmpps512_mask,a16,b16,0xffff,8)MR(cmppd512_mask,p8,q8,k8,8)MR(cmppd512_mask,p8,q8,0xff,4)\n"
            "MR(cmpss_mask,a4,b4,k8,4)MR(cmpss_mask,a4,b4,0xff,8)MR(cmpsd_mask,p2,q2,k8,12)MR(cmpsd_mask,p2,q2,1,4)\n"
            "C(vcomiss,a4,b4,4)C(vcomiss,a4,b4,8)C(vcomisd,p2,q2,4)C(vcomisd,p2,q2,8)C(vcomisd,q2,p2,4)\n"
            "return (int)h;}",
        },
        {
            // Strict compares preserve their signaling bit and masked exception suppression. Uniform raw QNaN/SNaN
            // inputs keep a quiet comparison from inheriting another lane's invalid flag. The nested function's
            // compare is outside its empty ON block: clang constrains that entire function too.
            "x86-rest-float-compares-fenv",
            "int clear_fp(void);int fp_state(void);\n"
            "static __attribute__((noinline)) unsigned nested(v4sf a,v4sf b){{\n#pragma STDC FENV_ACCESS ON\n}\n"
            "clear_fp();unsigned r=__builtin_ia32_cmpps128_mask(a,b,16,15);return r|((unsigned)fp_state()<<8);}\n"
            "#pragma STDC FENV_ACCESS ON\n"
            "#define P8(F,o,...) F(o+0,__VA_ARGS__)F(o+1,__VA_ARGS__)F(o+2,__VA_ARGS__)F(o+3,__VA_ARGS__)"
            "F(o+4,__VA_ARGS__)F(o+5,__VA_ARGS__)F(o+6,__VA_ARGS__)F(o+7,__VA_ARGS__)\n"
            "#define P32(F,...) P8(F,0,__VA_ARGS__)P8(F,8,__VA_ARGS__)P8(F,16,__VA_ARGS__)P8(F,24,__VA_ARGS__)\n"
            "#define V(P,N,T,A,B) {clear_fp();T r=(T)__builtin_ia32_##N(A,B,P);FOLD(r);unsigned f=(unsigned)fp_state();FOLD(f);}\n"
            "#define M(P,N,A,B,K) {clear_fp();unsigned r=__builtin_ia32_##N(A,B,P,K);FOLD(r);unsigned f=(unsigned)fp_state();FOLD(f);}\n"
            "#define R(P,N,A,B,K,S) {clear_fp();unsigned r=__builtin_ia32_##N(A,B,P,K,S);FOLD(r);unsigned f=(unsigned)fp_state();FOLD(f);}\n"
            "int tested(int n){unsigned h=2166136261u;const unsigned fs[]={0x3fc00000u,0x7fc00001u,0x7f800001u,0x80000000u};"
            "const unsigned long long ds[]={0x3ff8000000000000ull,0x7ff8000000000001ull,0x7ff0000000000001ull,0x8000000000000000ull};"
            "unsigned ab[16],bb[16];unsigned long long db[8],eb[8];unsigned k=(unsigned)n&3u;\n"
            "for(int i=0;i<16;i++){ab[i]=((unsigned)n&4u)?0x3f800000u:fs[k];bb[i]=((unsigned)n&4u)?fs[k]:0x3f800000u;"
            "if(i<8){db[i]=((unsigned)n&4u)?0x3ff0000000000000ull:ds[k];eb[i]=((unsigned)n&4u)?ds[k]:0x3ff0000000000000ull;}}\n"
            "v4sf a4,b4;v8sf a8,b8;v16sf a16,b16;v2df d2,e2;v4df d4,e4;v8df d8,e8;"
            "__builtin_memcpy(&a4,ab,16);__builtin_memcpy(&b4,bb,16);__builtin_memcpy(&a8,ab,32);__builtin_memcpy(&b8,bb,32);"
            "__builtin_memcpy(&a16,ab,64);__builtin_memcpy(&b16,bb,64);__builtin_memcpy(&d2,db,16);__builtin_memcpy(&e2,eb,16);"
            "__builtin_memcpy(&d4,db,32);__builtin_memcpy(&e4,eb,32);__builtin_memcpy(&d8,db,64);__builtin_memcpy(&e8,eb,64);\n"
            "unsigned m=nested(a4,b4);FOLD(m);unsigned char k8=(unsigned char)mask(n);unsigned short k16=(unsigned short)mask(n);\n"
            "P32(V,cmpps,v4sf,a4,b4)P32(V,cmppd,v2df,d2,e2)P32(V,cmpps256,v8sf,a8,b8)P32(V,cmppd256,v4df,d4,e4)\n"
            "P32(M,cmpps128_mask,a4,b4,k8)P32(M,cmpps128_mask,a4,b4,0)P32(M,cmppd128_mask,d2,e2,0xff)"
            "P32(M,cmpps256_mask,a8,b8,0xff)P32(M,cmppd256_mask,d4,e4,k8)\n"
            "P32(R,cmpps512_mask,a16,b16,k16,4)P32(R,cmpps512_mask,a16,b16,0xffff,8)P32(R,cmpps512_mask,a16,b16,0xffff,12)"
            "P32(R,cmppd512_mask,d8,e8,0xff,4)P32(R,cmppd512_mask,d8,e8,0xff,8)P32(R,cmppd512_mask,d8,e8,0xff,12)"
            "P32(R,cmppd512_mask,d8,e8,0,4)return (int)h;}",
        },
        {
            // FP16's strict predicate and SAE are compiled/linked under the explicit target. They are not executed
            // on a host without that ISA; the strict row controls cover all predicates and both SAE values as well.
            "x86-rest-half-compares-fenv",
            "#pragma STDC FENV_ACCESS ON\n"
            "typedef _Float16 v8hf __attribute__((vector_size(16)));typedef _Float16 v16hf __attribute__((vector_size(32)));"
            "typedef _Float16 v32hf __attribute__((vector_size(64)));\n"
            "__attribute__((target(\"avx512fp16,avx512vl\"))) static unsigned strict_half(v8hf a,v8hf b,v16hf c,v16hf d,v32hf e,v32hf f)"
            "{return __builtin_ia32_cmpph128_mask(a,b,16,0xff)+__builtin_ia32_cmpph128_mask(b,a,9,0)"
            "+__builtin_ia32_cmpph256_mask(c,d,17,0xffff)+__builtin_ia32_cmpph512_mask(e,f,31,~0u,4)"
            "+__builtin_ia32_cmpph512_mask(e,f,16,~0u,8)+__builtin_ia32_cmpph512_mask(e,f,16,~0u,12);}\n"
            "int tested(int n){v8hf a,b;v16hf c,d;v32hf e,f;fill(&a,16,n);fill(&b,16,n+1);fill(&c,32,n+2);fill(&d,32,n+3);"
            "fill(&e,64,n+4);fill(&f,64,n+5);return (int)strict_half(a,b,c,d,e,f);}",
            0,
            true,
        },
        {
            // AVX512-FP16's compares of halves into masks, at predicates that swap their operands, that clang's code makes
            // constants and past them, and the scalar one's with the exceptions' suppression: compiled and linked against
            // clang's build of the same, as no host of the tests runs them; test-x86-intrinsics compares their code.
            "x86-rest-half-compares",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "typedef _Float16 v8hf __attribute__((vector_size(16)));typedef _Float16 v16hf __attribute__((vector_size(32)));"
            "typedef _Float16 v32hf __attribute__((vector_size(64)));\n"
            "__attribute__((target(\"avx512fp16,avx512vl\"))) static unsigned compares(v8hf a,v8hf b,v16hf c,v16hf d,v32hf e,v32hf f,unsigned k)"
            "{return __builtin_ia32_cmpph128_mask(a,b,0x00,(unsigned char)k)+__builtin_ia32_cmpph128_mask(a,b,0x1e,(unsigned char)k)"
            "+__builtin_ia32_cmpph256_mask(c,d,0x09,(unsigned short)k)+__builtin_ia32_cmpph256_mask(c,d,0x14,0xffff)"
            "+__builtin_ia32_cmpph512_mask(e,f,0x0d,k,8)+__builtin_ia32_cmpph512_mask(e,f,0x1f,k,4)+__builtin_ia32_cmpph512_mask(e,f,0x0b,k,4)"
            "+__builtin_ia32_cmpsh_mask(a,b,0x3b,(unsigned char)k,8)+__builtin_ia32_cmpsh_mask(a,b,-1,(unsigned char)k,4);}\n"
            "int tested(int n){unsigned h=2166136261u;v8hf a,b;v16hf c,d;v32hf e,f;fill(&a,16,n);fill(&b,16,n+1);fill(&c,32,n+2);fill(&d,32,n+3);"
            "fill(&e,64,n+4);fill(&f,64,n+5);unsigned r=compares(a,b,c,d,e,f,(unsigned)mask(n));FOLD(r);return (int)h;}",
            0,
            true,
        },
        {
            // AVX-512's arithmetic by the rounding its last argument asks for (#148): the 64-byte vectors' sums, differences,
            // products, quotients and square roots, the scalar ones under masks of some and all, the conversions of integers
            // to floats and of floats to doubles under masks, the scalar conversions to integers and from them, and the FMAs merged into a,
            // zeroed and merged into c, each at the current mode (4) and at each embedded rounding (8 to 11; the widening
            // conversion's exceptions' suppression, 8), of inexact values.
            "x86-rest-rounded-arithmetic",
            "#define R5(F,...) F(4,__VA_ARGS__)F(8,__VA_ARGS__)F(9,__VA_ARGS__)F(10,__VA_ARGS__)F(11,__VA_ARGS__)\n"
            "#define A2(r,N,a,b) __builtin_ia32_##N(a,b,r),\n"
            "#define A1(r,N,a) __builtin_ia32_##N(a,r),\n"
            "#define A4(r,N,a,b,c,k) __builtin_ia32_##N(a,b,c,k,r),\n"
            "#define A3(r,N,a,b,k) __builtin_ia32_##N(a,b,k,r),\n"
            "#define B(N,T,a,b) {T r[]={R5(A2,N,a,b)};FOLD(r);}\n"
            "#define U(N,T,a) {T r[]={R5(A1,N,a)};FOLD(r);}\n"
            "#define S(N,T,a,b,w,k) {T r[]={R5(A4,N,a,b,w,k)};FOLD(r);}\n"
            "#define C(N,T,a,w,k) {T r[]={R5(A3,N,a,w,k)};FOLD(r);}\n"
            "#define I(N,T,a) {T r[]={R5(A1,N,a)};FOLD(r);}\n"
            "#define F(N,T,a,b,c,k) {T r[]={R5(A4,N,a,b,c,k)};FOLD(r);}\n"
            "int tested(int n){unsigned h=2166136261u;v16sf a16,b16,c16;v8df p8,q8,s8;v4sf a4,b4,w4;v2df p2,q2,w2;v16si i16;v8di l8;v8sf f8;\n"
            "for(int i=0;i<16;i++){a16[i]=(float)(n*7+i)/3.0f;b16[i]=(float)(i*13-n)/7.0f;c16[i]=(float)(n+i*i)/11.0f;i16[i]=(n+i)*0x3fffff7+i;"
            "if(i<8){p8[i]=(double)(n*5+i)/3.0;q8[i]=(double)(i*11-n)/7.0;s8[i]=(double)(n-i*3)/13.0;l8[i]=(long long)(n+i)*0x1fffffffffff3ll;"
            "f8[i]=(float)(n*3+i)/9.0f;}if(i<4){a4[i]=(float)(n+i)/3.0f;b4[i]=(float)(i-n)/7.0f;w4[i]=(float)i;}if(i<2){p2[i]=(double)(n+i)/3.0;"
            "q2[i]=(double)(i-n*3)/7.0;w2[i]=(double)i+0.5;}}\n"
            "unsigned long long m=mask(n);unsigned char k8=(unsigned char)m;unsigned short k16=(unsigned short)(m>>8);\n"
            "B(addpd512,v8df,p8,q8)B(addps512,v16sf,a16,b16)B(subpd512,v8df,p8,q8)B(subps512,v16sf,a16,b16)B(mulpd512,v8df,p8,q8)"
            "B(mulps512,v16sf,a16,b16)B(divpd512,v8df,p8,q8)B(divps512,v16sf,a16,b16)U(sqrtpd512,v8df,p8)U(sqrtps512,v16sf,a16)\n"
            "S(addsd_round_mask,v2df,p2,q2,w2,k8)S(addss_round_mask,v4sf,a4,b4,w4,k8)S(subsd_round_mask,v2df,p2,q2,w2,0xff)"
            "S(subss_round_mask,v4sf,a4,b4,w4,0xff)S(mulsd_round_mask,v2df,p2,q2,w2,k8)S(mulss_round_mask,v4sf,a4,b4,w4,k8)"
            "S(divsd_round_mask,v2df,p2,q2,w2,0xff)S(divss_round_mask,v4sf,a4,b4,w4,k8)S(divss_round_mask,v4sf,a4,b4,w4,0)\n"
            "C(cvtdq2ps512_mask,v16sf,i16,a16,k16)C(cvtdq2ps512_mask,v16sf,i16,a16,0xffff)C(cvtudq2ps512_mask,v16sf,i16,b16,k16)"
            "C(cvtqq2pd512_mask,v8df,l8,p8,k8)C(cvtqq2pd512_mask,v8df,l8,p8,0xff)C(cvtuqq2pd512_mask,v8df,l8,q8,k8)\n"
            "{v8df r[]={__builtin_ia32_cvtps2pd512_mask(f8,p8,k8,4),__builtin_ia32_cvtps2pd512_mask(f8,q8,0xff,8)};FOLD(r);}\n"
            "I(vcvtsd2si32,int,p2)I(vcvtsd2si64,long long,q2)I(vcvtss2si32,int,a4)I(vcvtss2si64,long long,b4)\n"
            "F(vfmaddpd512_mask,v8df,p8,q8,s8,k8)F(vfmaddpd512_maskz,v8df,p8,q8,s8,k8)F(vfmaddpd512_mask3,v8df,p8,q8,s8,k8)"
            "F(vfmsubpd512_mask3,v8df,p8,q8,s8,k8)F(vfmaddps512_mask,v16sf,a16,b16,c16,k16)F(vfmaddps512_maskz,v16sf,a16,b16,c16,0xffff)"
            "F(vfmaddps512_mask3,v16sf,a16,b16,c16,k16)F(vfmsubps512_mask3,v16sf,a16,b16,c16,k16)\n"
            "long long ll=(long long)n*0x1fffffffffff7ll+3;unsigned long long ul=0x8000000000000001ull*(unsigned)(n|1)+0xfff;int ii=n*0x3fffff7+5;"
            "unsigned ui=0x80000001u*(unsigned)n+0x1ff;B(cvtsi2sd64,v2df,p2,ll)B(cvtsi2ss32,v4sf,a4,ii)B(cvtsi2ss64,v4sf,a4,ll)"
            "B(cvtusi2sd64,v2df,q2,ul)B(cvtusi2ss32,v4sf,b4,ui)B(cvtusi2ss64,v4sf,b4,ul)\n"
            "return (int)h;}",
            0,
            false,
        },
        {
            // The rest of the gathers and scatters (#148): AVX-512's of floats and doubles at 64 bytes and of every element at
            // 16 and 32 bytes, under masks of some lanes and all, at the elements' scales and by byte and word offsets at
            // scales 1 and 2; the scatters into tables whose bytes are hashed after them; and AVX2's gathers under vectors'
            // signs.
            "x86-rest-gathers",
            "#define G(N,T,...) {T r=__builtin_ia32_##N(__VA_ARGS__);FOLD(r);}\n"
            "#define S(N,...) __builtin_ia32_##N(__VA_ARGS__);\n"
            "int tested(int n){unsigned h=2166136261u;float tf[64];double td[64];int ti[64];long long tq[64];float sf[64]={0};double sd[64]={0};"
            "int si[64]={0};long long sq[64]={0};\n"
            "for(int i=0;i<64;i++){tf[i]=(float)i*1.5f+(float)n;td[i]=(double)i*2.25+(double)n;ti[i]=i*7+n;tq[i]=(long long)i*0x100000001ll+n;}\n"
            "v4sf a4,f4;v8sf f8;v16sf a16;v2df p2;v4df p4;v8df p8;v4si i4,x4;v8si i8,x8;v16si x16;v2di q2,y2;v4di q4,y4;v8di y8;fill(&a4,16,n);"
            "fill(&f4,16,n+1);fill(&f8,32,n+2);fill(&a16,64,n+3);fill(&p2,16,n+4);fill(&p4,32,n+5);fill(&p8,64,n+6);fill(&i4,16,n+7);fill(&i8,32,n+8);"
            "fill(&q2,16,n+9);fill(&q4,32,n+10);\n"
            "for(int i=0;i<16;i++){x16[i]=(i*3+n)&63;if(i<8){x8[i]=(i*5+n*3)&63;y8[i]=(i*7+n)&63;}if(i<4){x4[i]=(i*11+n)&63;y4[i]=(i*13+n*5)&63;}"
            "if(i<2)y2[i]=(i*17+n*7)&63;}\n"
            "unsigned long long m=mask(n);unsigned char k8=(unsigned char)m;unsigned short k16=(unsigned short)(m>>8);\n"
            "G(gathersiv16sf,v16sf,a16,tf,x16,k16,4)G(gathersiv16sf,v16sf,a16,tf,x16,0xffff,4)G(gathersiv8df,v8df,p8,td,x8,k8,8)"
            "G(gatherdiv16sf,v8sf,f8,tf,y8,k8,4)G(gatherdiv16si,v8si,i8,ti,y8,k8,4)G(gatherdiv8df,v8df,p8,td,y8,0xff,8)\n"
            "G(gather3div2df,v2df,p2,td,y2,k8,8)G(gather3div2di,v2di,q2,tq,y2,k8,8)G(gather3div4df,v4df,p4,td,y4,k8,8)"
            "G(gather3div4di,v4di,q4,tq,y4,0xff,8)G(gather3div4sf,v4sf,a4,tf,y2,k8,4)G(gather3div4si,v4si,i4,ti,y2,0xff,4)"
            "G(gather3div8sf,v4sf,f4,tf,y4,k8,4)G(gather3div8si,v4si,i4,ti,y4,k8,4)G(gather3siv2df,v2df,p2,td,x4,k8,8)"
            "G(gather3siv2di,v2di,q2,tq,x4,0xff,8)G(gather3siv4df,v4df,p4,td,x4,k8,8)G(gather3siv4di,v4di,q4,tq,x4,k8,8)"
            "G(gather3siv4sf,v4sf,a4,tf,x4,k8,4)G(gather3siv4si,v4si,i4,ti,x4,k8,4)G(gather3siv8sf,v8sf,f8,tf,x8,k8,4)"
            "G(gather3siv8si,v8si,i8,ti,x8,0xff,4)G(gather3siv4si,v4si,i4,ti,x4*4,0xff,1)G(gather3siv4si,v4si,i4,ti,x4*2,k8,2)\n"
            "S(scatterdiv16sf,sf,k8,y8,f8,4)S(scatterdiv8df,sd,k8,y8,p8,8)S(scattersiv16sf,sf,k16,x16,a16,4)S(scattersiv8df,sd,0xff,x8,p8,8)"
            "S(scattersiv8di,sq,k8,x8,(v8di)p8,8)S(scatterdiv2df,sd,k8,y2,p2,8)S(scatterdiv2di,sq,0xff,y2,q2,8)S(scatterdiv4df,sd,k8,y4,p4,8)"
            "S(scatterdiv4di,sq,k8,y4,q4,8)S(scatterdiv4sf,sf,k8,y2,a4,4)S(scatterdiv4si,si,k8,y2,i4,4)S(scatterdiv8sf,sf,0xff,y4,f4,4)"
            "S(scatterdiv8si,si,k8,y4,i4,4)S(scattersiv2df,sd,k8,x4,p2,8)S(scattersiv2di,sq,k8,x4,q2,8)S(scattersiv4df,sd,0xff,x4,p4,8)"
            "S(scattersiv4di,sq,k8,x4,q4,8)S(scattersiv4sf,sf,k8,x4,a4,4)S(scattersiv4si,si,k8,x4,i4,4)S(scattersiv8sf,sf,k8,x8,f8,4)"
            "S(scattersiv8si,si,k8,x8,i8,4)S(scattersiv4si,si,0xff,x4*2,i4,2)FOLD(sf);FOLD(sd);FOLD(si);FOLD(sq);\n"
            "v4si m4={-1,0,-(n&1),-1};v8si m8={-1,0,-1,-(n&1),0,-1,-1,-(n&2)};v2di m2={-1,-(long long)(n&1)};v4di m4q={0,-1,-1,-(long long)(n&1)};\n"
            "G(gatherd_d,v4si,i4,ti,x4,m4,4)G(gatherd_d256,v8si,i8,ti,x8,m8,4)G(gatherd_pd,v2df,p2,td,x4,(v2df)m2,8)"
            "G(gatherd_pd256,v4df,p4,td,x4,(v4df)m4q,8)G(gatherd_ps,v4sf,a4,tf,x4,(v4sf)m4,4)G(gatherd_ps256,v8sf,f8,tf,x8,(v8sf)m8,4)"
            "G(gatherd_q,v2di,q2,tq,x4,m2,8)G(gatherd_q256,v4di,q4,tq,x4,m4q,8)G(gatherq_d,v4si,i4,ti,y2,m4,4)G(gatherq_d256,v4si,i4,ti,y4,m4,4)"
            "G(gatherq_pd,v2df,p2,td,y2,(v2df)m2,8)G(gatherq_pd256,v4df,p4,td,y4,(v4df)m4q,8)G(gatherq_ps,v4sf,a4,tf,y2,(v4sf)m4,4)"
            "G(gatherq_ps256,v4sf,f4,tf,y4,(v4sf)m4,4)G(gatherq_q,v2di,q2,tq,y2,m2,8)G(gatherq_q256,v4di,q4,tq,y4,m4q,8)"
            "G(gatherd_d,v4si,i4,ti,x4*4,m4,1)\n"
            "return (int)h;}",
            0,
            false,
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
