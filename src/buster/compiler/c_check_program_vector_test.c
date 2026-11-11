// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's vectors in the lowering, against clang -O0's code of the same source for x86-64-v4 (AVX-512F, BW, DQ and VL):
// every operator at each element type and width, compound assignments, calls passing and returning vectors in
// registers, va_arg of them, elements of lvalues and of values, literals, splats, casts, conditionals, globals, arrays and
// members of vector type. Most of their types are aligned to 16, and one program's to their sizes, 32 and 64, whose locals
// realign its frame (#552); one program's are vectors of 8 and 4 bytes (MMX's __m64 and the narrow vectors), their
// operators, calls, members, arrays, globals, casts to and from integers of their bytes and nontemporal stores, and their
// divisions' floating-point flags under FENV_ACCESS; and _Atomic vectors of 4 to 64 bytes, their atomic loads, stores,
// exchanges and compare-exchanges and calls both ways. #775 also covers SSE2/v2/v3 and AVX-only function targets, and
// #1193 their vectors wider than the targets' registers: results split across them, with clang's code both ways, and
// operators split into pieces they hold.
// Unsupported operations and widths are refused by name. Include after c_check_program_test.c's runners.

static const char check_test_vector_types[] =
    "typedef signed char v64qi __attribute__((vector_size(64),aligned(16)));typedef unsigned char v16qu __attribute__((vector_size(16)));"
    "typedef short v16hi __attribute__((vector_size(32),aligned(16)));typedef unsigned short v8hu __attribute__((vector_size(16)));"
    "typedef int v4si __attribute__((vector_size(16)));typedef int v8si __attribute__((vector_size(32),aligned(16)));"
    "typedef unsigned v16su __attribute__((vector_size(64),aligned(16)));typedef long long v2di __attribute__((vector_size(16)));"
    "typedef unsigned long long v8du __attribute__((vector_size(64),aligned(16)));typedef long long v4di __attribute__((vector_size(32),aligned(16)));"
    "typedef float v4sf __attribute__((vector_size(16)));typedef float v8sf __attribute__((vector_size(32),aligned(16)));"
    "typedef float v16sf __attribute__((vector_size(64),aligned(16)));typedef double v2df __attribute__((vector_size(16)));"
    "typedef double v8df __attribute__((vector_size(64),aligned(16)));\n"
    "static unsigned fold(unsigned h,const void*p,unsigned long n){const unsigned char*b=p;for(unsigned long i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}\n"
    "#define FOLD(v) h=fold(h,&(v),sizeof(v))\n"
    "static void fill(void*p,unsigned long n,unsigned s){unsigned char*b=p;for(unsigned long i=0;i<n;i++){s=s*1103515245u+12345u;b[i]=(unsigned char)(s>>16);}}\n"
    "static void fillf(float*f,int n,int s){for(int i=0;i<n;i++)f[i]=(float)((i*37+s*11)%201-100)/7.0f;f[0]=-0.0f;}\n"
    "static void filld(double*f,int n,int s){for(int i=0;i<n;i++)f[i]=(double)((i*53+s*13)%301-150)/3.0;f[1]=0.0;}\n";

// Computing with the vectors Tier 0 has no class for (#137, c_check_program_transport.c), against clang -O0's code at
// every level: arithmetic, compares, shifts and compound assignments of vectors of 1 and 2 bytes (of chars, shorts and a
// _Float16), of more than 64 bytes (of each element size, floats, doubles and AMX's 1024-byte tile) and of bool (of 4,
// 8, 12, 32, 64, 100, 128 and 1024 elements, their padding bits past the elements unfolded); casts, splats, constants and
// conversions of each, __builtin_convertvector both ways, an extended vector's compare converted to one of bool; a
// vector of bool's elements read and written; and the reductions and shuffles. What the ordinary vectors' rows refuse
// at a level (variable shifts, 64-bit compares) is left to the levels that have it.
static const char check_test_vector_transport[] =
    "typedef signed char t1 __attribute__((vector_size(1)));typedef char t2 __attribute__((vector_size(2)));typedef unsigned char u2 __attribute__((vector_size(2)));\n"
    "typedef short s1 __attribute__((vector_size(2)));typedef unsigned char e2 __attribute__((ext_vector_type(2)));typedef _Float16 h1 __attribute__((vector_size(2)));\n"
    "typedef int w32 __attribute__((vector_size(128)));typedef unsigned char wq __attribute__((vector_size(256)));typedef float wf __attribute__((vector_size(128)));\n"
    "typedef double wd __attribute__((vector_size(256)));typedef short wh __attribute__((vector_size(128)));typedef long long wl __attribute__((vector_size(128)));\n"
    "typedef int Tile __attribute__((vector_size(1024),aligned(64)));\n"
    "typedef _Bool b2 __attribute__((ext_vector_type(2)));typedef _Bool b4 __attribute__((ext_vector_type(4)));typedef _Bool b8 __attribute__((ext_vector_type(8)));\n"
    "typedef _Bool b12 __attribute__((ext_vector_type(12)));typedef _Bool b32 __attribute__((ext_vector_type(32)));typedef _Bool b64 __attribute__((ext_vector_type(64)));\n"
    "typedef _Bool b100 __attribute__((ext_vector_type(100)));typedef _Bool b128 __attribute__((ext_vector_type(128)));typedef _Bool b1024 __attribute__((ext_vector_type(1024)));\n"
    "typedef int i2v __attribute__((ext_vector_type(2)));typedef int i4v __attribute__((ext_vector_type(4)));typedef int i8v __attribute__((ext_vector_type(8)));\n"
    "typedef short s8v __attribute__((ext_vector_type(8)));typedef float f8v __attribute__((ext_vector_type(8)));typedef double d2v __attribute__((ext_vector_type(2)));\n"
    "typedef unsigned char q16 __attribute__((ext_vector_type(16)));typedef unsigned char q128 __attribute__((ext_vector_type(128)));typedef int x32 __attribute__((ext_vector_type(32)));\n"
    "/* A vector of bool's bits past its elements are padding, which clang's code leaves either way: only its elements fold. */\n"
    "static unsigned foldb(unsigned h,const void*p,unsigned long count,unsigned long lanes,unsigned long size){const unsigned char*b=p;for(unsigned long k=0;k<count;k++)for(unsigned long i=0;i<lanes;i++)h=(h^((b[k*size+i/8]>>(i%8))&1))*16777619u;return h;}\n"
    "#define FOLDB(v,lanes) h=foldb(h,&(v),sizeof(v)/sizeof((v)[0]),lanes,sizeof((v)[0]))\n"
    "static t2 make2(short x){return (t2)x;}static b8 notb(b8 a){return ~a;}static b8 less8(i8v a,i8v b){return a<b;}static b8 truth8(s8v a){return a;}\n"
    "int tested(int n){unsigned h=2166136261u;\n"
    "t2 a,b;fill(&a,2,n);fill(&b,2,n+1);b|=1;\n"
    "t2 x[]={a+b,a-b,a*b,a/b,a%b,a&b,a|b,a^b,~a,-a,a<<(b&7),a>>(b&3),a<b,a<=b,a>b,a>=b,a==b,a!=b,a+3,a*(char)n,a<<2};FOLD(x);\n"
    "u2 c,d;fill(&c,2,n+2);fill(&d,2,n+3);d|=1;u2 y[]={c+d,c/d,c%d,c>>(d&7),c*d,-c,(u2)(c<d),(u2)(c>=d),c-5};FOLD(y);\n"
    "t1 o,p;fill(&o,1,n+4);fill(&p,1,n+5);p|=1;t1 z[]={o+p,o*p,o/p,-o,~o,o<p,o!=p};FOLD(z);\n"
    "s1 s,t;fill(&s,2,n+6);fill(&t,2,n+7);t|=1;s1 q[]={s+t,s*t,s/t,s%t,s<<(t&15),s>>(t&15),s<t,-s};FOLD(q);\n"
    "e2 e,f;fill(&e,2,n+8);fill(&f,2,n+9);e2 ee[]={e+f,e*f,e-f,(e2)(e>f),e^f};FOLD(ee);\n"
    "h1 ha={(_Float16)(n*0.75f+0.1f)},hb={(_Float16)(1.5f-n)};h1 hr[]={ha+hb,ha-hb,ha*hb,ha/hb,-ha};FOLD(hr);short hc[]={(ha<hb)[0],(ha==hb)[0]};FOLD(hc);\n"
    "a+=b;a-=3;a*=b;a^=b;a<<=1;FOLD(a);c|=d;c&=(unsigned char)n;FOLD(c);\n"
    "w32 wa,wb;fill(&wa,128,n+10);fill(&wb,128,n+11);w32 wr[]={wa+wb,wa-wb,wa*wb,wa&wb,wa|wb,wa^wb,~wa,-wa,wa<wb,wa==wb,wa>=wb,wa+n};FOLD(wr);\n"
    "#ifdef __AVX2__\n"
    "w32 wv[]={wa<<(wb&31),wa>>(wb&31),wa<<3,wa>>n%32};FOLD(wv);\n"
    "#endif\n"
    "wa+=wb;wa*=3;FOLD(wa);\n"
    "wq qa,qb;fill(&qa,256,n+12);fill(&qb,256,n+13);wq qr[]={qa+qb,qa-qb,qa&qb,(wq)(qa<qb),(wq)(qa==qb),qa+(unsigned char)n,~qb};FOLD(qr);\n"
    "wf fa,fb;fillf((float*)&fa,32,n);fillf((float*)&fb,32,n+1);fb+=100.0f;wf fr[]={fa+fb,fa-fb,fa*fb,fa/fb,-fa,fa*2.5f,(wf)(fa<fb),(wf)(fa>=fb)};FOLD(fr);\n"
    "wd da,db;filld((double*)&da,32,n);filld((double*)&db,32,n+2);db+=500.0;wd dr[]={da+db,da*db,da/db,-db,da-1.5,(wd)(da<db),(wd)(da!=db)};FOLD(dr);\n"
    "wh sa,sb;fill(&sa,128,n+14);fill(&sb,128,n+15);wh sr[]={sa+sb,sa*sb,sa<sb,-sa};FOLD(sr);\n"
    "wl la,lb;fill(&la,128,n+16);fill(&lb,128,n+17);wl lr[]={la+lb,la-lb,-la,la^lb};FOLD(lr);\n"
    "#ifdef __SSE4_2__\n"
    "wl lc[]={la<lb,la==lb};FOLD(lc);\n"
    "#endif\n"
    "Tile ta,tb;fill(&ta,1024,n+18);fill(&tb,1024,n+19);Tile tr[]={ta+tb,ta^tb,~ta,ta*3,ta==tb};FOLD(tr);ta+=tb;FOLD(ta);\n"
    "b8 m,k;fill(&m,1,n+20);fill(&k,1,n+21);b8 mr[]={m&k,m|k,m^k,~m,!m,m==k,m!=k,m<k,m<=k,m>k,m>=k,m&(_Bool)(n&1),m|1};FOLD(mr);\n"
    "b4 m4,k4;fill(&m4,1,n+22);fill(&k4,1,n+23);m4&=(b4){1,1,1,1};k4=k4&(b4){1,1,1,1};b4 r4[]={m4&k4,m4|k4,~m4,!m4,m4==k4,m4<=k4,m4>k4};FOLDB(r4,4);\n"
    "b12 m12,k12;fill(&m12,2,n+24);fill(&k12,2,n+25);m12=m12&m12;k12=k12|k12;b12 r12[]={~m12,m12^k12,m12==k12,m12>=k12};FOLDB(r12,12);\n"
    "b32 m32,k32;fill(&m32,4,n+26);fill(&k32,4,n+27);b32 r32[]={m32&k32,~m32,m32<k32,m32==k32};FOLD(r32);\n"
    "b64 m64,k64;fill(&m64,8,n+28);fill(&k64,8,n+29);b64 r64[]={m64|k64,~m64,m64>=k64,m64!=k64};FOLD(r64);\n"
    "b128 g,l;fill(&g,16,n+30);fill(&l,16,n+31);b128 r128[]={g&l,~g,g<l,g==l,g^1};FOLD(r128);\n"
    "b100 g1,l1;fill(&g1,16,n+32);fill(&l1,16,n+33);g1=g1&g1;l1=l1|l1;b100 r100[]={~g1,g1==l1,g1<=l1};FOLDB(r100,100);\n"
    "b1024 gg,ll;fill(&gg,128,n+34);fill(&ll,128,n+35);b1024 rr[]={gg&ll,~gg,gg>ll};FOLD(rr);m^=k;m|=k;FOLD(m);g&=l;FOLD(g);\n"
    "s1 sv=(s1)a;FOLD(sv);short sh=(short)a;FOLD(sh);t2 back=(t2)(short)(n*257+3);FOLD(back);t2 mk=make2((short)n);FOLD(mk);\n"
    "e2 es=(e2)n;FOLD(es);e2 ea=(unsigned char)7;FOLD(ea);e2 eb=es+(unsigned char)n;FOLD(eb);\n"
    "b8 bs=(_Bool)(n&1);FOLD(bs);b8 bt=(b8)(n&2);FOLD(bt);b128 bw=(_Bool)(n&1);FOLD(bw);b4 b4s=1;FOLDB(b4s,4);x32 xs=n;FOLD(xs);\n"
    "i8v iv={n,0,-1,2,0,5,0,n-3};b8 cb=__builtin_convertvector(iv,b8);FOLD(cb);i8v ib=__builtin_convertvector(cb,i8v);FOLD(ib);\n"
    "f8v fv=__builtin_convertvector(cb,f8v);FOLD(fv);f8v ff;fillf((float*)&ff,8,n);b8 fc=__builtin_convertvector(ff,b8);FOLD(fc);\n"
    "i2v two={n,-n};t2 nar=__builtin_convertvector(two,t2);FOLD(nar);i2v wid=__builtin_convertvector(a,i2v);FOLD(wid);\n"
    "e2 ce=__builtin_convertvector(a,e2);FOLD(ce);d2v dd=__builtin_convertvector(a,d2v);FOLD(dd);b2 bb=__builtin_convertvector(a,b2);FOLDB(bb,2);\n"
    "wf wfv=__builtin_convertvector(wa,wf);FOLD(wfv);wd wdv=__builtin_convertvector(wa,wd);FOLD(wdv);\n"
    "x32 xx;fill(&xx,128,n+36);xx&=3;xx-=1;b32 xb=__builtin_convertvector(xx,b32);FOLD(xb);x32 xb2=__builtin_convertvector(xb,x32);FOLD(xb2);\n"
    "q128 qq;fill(&qq,128,n+37);b128 qbb=__builtin_convertvector(qq&1,b128);FOLD(qbb);\n"
    "b8 lit=(b8){1,0,(_Bool)n,1,0,0,0,1};FOLD(lit);t2 lt=(t2){1,(char)n};FOLD(lt);b8 ne=notb((b8){1,0,1,1,0,0,0,1});FOLD(ne);\n"
    "int el=0;for(int i=0;i<8;i++)el=el*3+cb[i]+lit[(i+n)&7];FOLD(el);\n"
    "b8 st=cb;st[n&7]=1;st[(n+3)&7]=0;st[2]=(_Bool)n;FOLD(st);_Bool sv5=(st[5]=n&4);FOLD(sv5);int pq=(st[2]=0)+(st[3]=n)+(st[4]=0.5);FOLD(st);FOLD(pq);\n"
    "b128 bigb;fill(&bigb,16,n+38);bigb[100]=!bigb[100];bigb[127]=1;int bigsum=0;for(int i=0;i<128;i++)bigsum+=bigb[i]*(i+1);FOLD(bigb);FOLD(bigsum);\n"
    "_Bool rv=notb(cb)[n&7];FOLD(rv);\n"
    "i8v ia={n,1,2,3,4,5,6,7},ic={7,6,5,4,3,2,1,n};b8 lm=less8(ia,ic);FOLD(lm);b8 lk=ia;FOLD(lk);lk=ia==ic;FOLD(lk);\n"
    "s8v s8={0,(short)n,-1,0,4,0,0,(short)(n-2)};b8 tc=truth8(s8);FOLD(tc);f8v f8a={0.5f,(float)n,-1,2,3,4,5,6},f8b={1,2,3,4,(float)n,0,0,0};b8 fl=f8a<f8b;FOLD(fl);\n"
    "int rd[]={__builtin_reduce_or(cb),__builtin_reduce_and(cb),__builtin_reduce_xor(cb),__builtin_reduce_add(cb),__builtin_reduce_max(cb),__builtin_reduce_min(cb),\n"
    "__builtin_reduce_and(bw),__builtin_reduce_or(qbb),__builtin_reduce_xor(qbb)};FOLD(rd);\n"
    "char ra[]={__builtin_reduce_add(a),__builtin_reduce_mul(a),__builtin_reduce_max(a),__builtin_reduce_min(a),__builtin_reduce_xor(a)};FOLD(ra);\n"
    "int rw[]={__builtin_reduce_add(wb),__builtin_reduce_max(wb),__builtin_reduce_min(wb),__builtin_reduce_and(wb),__builtin_reduce_or(wb),__builtin_reduce_mul(wb)};FOLD(rw);\n"
    "unsigned char rq[]={__builtin_reduce_max(qq),__builtin_reduce_min(qq),__builtin_reduce_add(qq)};FOLD(rq);\n"
    "t2 sh1=__builtin_shufflevector(a,mk,3,0);FOLD(sh1);t2 sh2=__builtin_shufflevector(a,a,1,1);FOLD(sh2);\n"
    "i4v sw=__builtin_shufflevector(two,two,1,0,3,-1);sw[3]=0;FOLD(sw);b8 sbb=__builtin_shufflevector(cb,lit,0,8,1,9,2,10,3,15);FOLD(sbb);\n"
    "b4 sb4=__builtin_shufflevector(cb,cb,7,6,5,4);FOLDB(sb4,4);q16 lo=__builtin_shufflevector(qq,qq,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);FOLD(lo);\n"
    "w32 rev=__builtin_shufflevector(wb,wb,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);FOLD(rev);\n"
    "return (int)h;}\n";

// The accepted extended-vector logical operators at ordinary, tiny and transport widths. Truth and selection differ:
// every nonzero lane is true for !/&&/||, but ?: selects by its condition's sign bit. The source also observes both
// arms' evaluation and keeps a transport condition's value across an arm that overwrites its original object.
static const char check_test_vector_logical[] =
    "typedef int I4 __attribute__((ext_vector_type(4)));typedef unsigned U4 __attribute__((ext_vector_type(4)));"
    "typedef float F4 __attribute__((ext_vector_type(4)));typedef int I32 __attribute__((ext_vector_type(32)));"
    "typedef float F32 __attribute__((ext_vector_type(32)));typedef char C2 __attribute__((ext_vector_type(2)));"
    "typedef short S1 __attribute__((ext_vector_type(1)));typedef _Float16 H1 __attribute__((ext_vector_type(1)));"
    "typedef _Bool B4 __attribute__((ext_vector_type(4)));typedef _Bool B32 __attribute__((ext_vector_type(32)));\n"
    "static int seen;static I32 held;"
    "static I4 mark4(int k){seen=seen*10+k;return (I4){0,1,-1,2};}"
    "static int marks(int k){seen=seen*10+k;return k==1?0:1;}"
    "static I32 mark32(int k){seen=seen*10+k;I32 v;for(int i=0;i<32;i++)v[i]=(i%4==0?0:i%4==1?1:i%4==2?-1:k);return v;}"
    "static I32 overwrite(void){held=(I32)7;return (I32)11;}\n"
    "int tested(int n){unsigned h=2166136261u;I4 a={n-3,0,1,-1},b={0,n,0,-7};"
    "I4 r[]={!a,a&&b,a||b,a&&n,n&&b,a||n,n||b,a?b:17,a?19:b,a?b:a,a?:b};FOLD(r);"
    "U4 u={0u,1u,0x80000000u,0xffffffffu};I4 ur=u?a:b;FOLD(ur);"
    "unsigned bits[]={0,0x80000000u,0x7fc12345u,0xbf800000u};F4 f;__builtin_memcpy(&f,bits,16);"
    "F4 g={(float)n,0.0f,-0.0f,2.0f};I4 ft[]={!f,f&&g,f||g,f&&1.0f,0.0f||f};FOLD(ft);"
    "F4 fr[]={a?f:g,a?f:1.5f,n?f:g};FOLD(fr);B4 bc={1,0,(_Bool)(n&1),0};F4 br=bc?f:g;FOLD(br);"
    "B4 ba={0,1,0,1},bb={1,0,1,0},bo=bc?ba:bb;unsigned packed=0;for(int i=0;i<4;i++)packed|=(unsigned)bo[i]<<i;FOLD(packed);"
    "struct __attribute__((packed)) guard{unsigned char pre;C2 v;unsigned char post;};struct guard guard={0x31,{(signed char)n,0},0xd7};"
    "C2 c={(signed char)(n-4),-1};C2 cr[]={!guard.v,guard.v&&c,guard.v||c,guard.v?c:(C2){9,8}};FOLD(cr);"
    "guard.v=!guard.v;FOLD(guard);S1 s={(short)(n-3)},t={-1};S1 sr[]={!s,s&&t,s||t,s?t:(S1){7}};FOLD(sr);"
    "unsigned short hb=(n&1)?0x8000u:0x7e11u;H1 hv,hs=(H1)(_Float16)n;__builtin_memcpy(&hv,&hb,2);S1 hm[]={!hv,hv&&hv,hv||(_Float16)0,!hs};FOLD(hm);"
    "I32 x,y;F32 p,q;for(int i=0;i<32;i++){x[i]=(i%4==0?0:i%4==1?1:i%4==2?-1:n);y[i]=i-n;"
    "p[i]=(i%4==0?0.0f:i%4==1?-0.0f:(float)(i-n));q[i]=(float)(n-i);}"
    "I32 wr[]={!x,x&&y,x||y,x&&n,n||y,x?y:13,x?:y,!p,p&&q,p||q};FOLD(wr);"
    "F32 wf[]={x?p:q,x?2.5f:q};FOLD(wf);B32 mask;for(int i=0;i<32;i++)mask[i]=(i+n)%3==0;F32 bw=mask?p:q;FOLD(bw);"
    "held=x;I32 saved=held?overwrite():y;FOLD(saved);FOLD(held);"
    "seen=0;I4 e1=mark4(1)&&mark4(2);FOLD(e1);FOLD(seen);seen=0;I4 e2=marks(1)&&mark4(2);FOLD(e2);FOLD(seen);"
    "seen=0;I4 e3=marks(2)||mark4(3);FOLD(e3);FOLD(seen);seen=0;I4 e4=mark4(1)?mark4(2):mark4(3);FOLD(e4);FOLD(seen);"
    "seen=0;I4 e5=mark4(1)?:mark4(2);FOLD(e5);FOLD(seen);seen=0;I32 e6=mark32(1)?mark32(2):mark32(3);FOLD(e6);FOLD(seen);\n"
    "#ifdef __SSE4_2__\n"
    "typedef double D16 __attribute__((ext_vector_type(16)));typedef long L16 __attribute__((ext_vector_type(16)));"
    "D16 da,db;L16 dc;for(int i=0;i<16;i++){da[i]=i%3?(double)(i-n):-0.0;db[i]=(double)(n-i);dc[i]=i%3?1:-1;}"
    "L16 dl[]={!da,da&&db,da||db};D16 ds=dc?da:db;FOLD(dl);FOLD(ds);\n"
    "#endif\n"
    "return (int)h;}\n";

// Packed one-bit builtins use the same scalar/byte/vector algebra at tiny, padded and multi-register widths. Check
// every defined lane, independent guards around a one-byte result, and evaluation of a funnel's unused operands.
// Floating max, min and abs (#1834): llvm.maxnum, minnum and fabs, as clang's code computes them, of NaN payloads (quiet
// and signaling), signed zeros and infinities; of floats and doubles, scalars and vectors of 8 to 128 bytes and an odd
// length. The baseline programs and the vector builtin programs (znver5's rows) run it.
static const char check_test_vector_floating_extremes[] =
    "typedef float f2 __attribute__((vector_size(8)));typedef float f8 __attribute__((vector_size(32),aligned(16)));"
    "typedef double d8 __attribute__((vector_size(64),aligned(16)));typedef float f32 __attribute__((vector_size(128),aligned(16)));"
    "typedef float e3 __attribute__((ext_vector_type(3)));typedef float f4 __attribute__((vector_size(16)));typedef double d2 __attribute__((vector_size(16)));\n"
    "static unsigned float_bits(unsigned i){static const unsigned b[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc00001u,0xffc00002u,0x7f800001u,1,"
    "0x3fc00000u,0xbf800000u};return b[i%10];}\n"
    "static unsigned long long double_bits(unsigned i){static const unsigned long long b[]={0,0x8000000000000000ull,0x7ff0000000000000ull,"
    "0xfff0000000000000ull,0x7ff8000000000001ull,0xfff8000000000002ull,0x7ff0000000000001ull,1,0x3ff8000000000000ull,0xbff0000000000000ull};return b[i%10];}\n"
    "#define E(T,k) {T a,b;for(unsigned i_=0;i_<__builtin_vectorelements(T);i_++){unsigned long long x_=sizeof(a[0])==4?float_bits(n+k+i_):double_bits(n+k+i_),"
    "y_=sizeof(a[0])==4?float_bits(n*3+i_*7):double_bits(n*3+i_*7);__typeof__(a[0]) p_,q_;__builtin_memcpy(&p_,&x_,sizeof p_);__builtin_memcpy(&q_,&y_,sizeof q_);"
    "a[i_]=p_;b[i_]=q_;}T r_[]={__builtin_elementwise_max(a,b),__builtin_elementwise_min(a,b),__builtin_elementwise_abs(a)};"
    "for(unsigned j_=0;j_<3;j_++)for(unsigned i_=0;i_<__builtin_vectorelements(T);i_++){__typeof__(a[0]) e_=r_[j_][i_];FOLD(e_);}}\n"
    "int tested(int n){unsigned h=2166136261u;E(f4,1)E(d2,2)E(f2,3)E(f8,4)E(d8,5)E(f32,6)E(e3,7)\n"
    "for(unsigned i=0;i<10;i++){unsigned long long x_=double_bits(n+i),y_=double_bits(i*3);unsigned xf=float_bits(n+i),yf=float_bits(i*7);double a,b;float c,d;"
    "__builtin_memcpy(&a,&x_,8);__builtin_memcpy(&b,&y_,8);__builtin_memcpy(&c,&xf,4);__builtin_memcpy(&d,&yf,4);"
    "double r[]={__builtin_elementwise_max(a,b),__builtin_elementwise_min(a,b),__builtin_elementwise_abs(a)};"
    "float s[]={__builtin_elementwise_max(c,d),__builtin_elementwise_min(c,d),__builtin_elementwise_abs(c)};FOLD(r);FOLD(s);}return (int)h;}\n";

static const char check_test_vector_boolean_builtins[] =
    "#define CASE(N) static unsigned bits##N(unsigned h,int n){typedef _Bool V __attribute__((ext_vector_type(N)));V a,b,c;"
    "fill(&a,sizeof a,n);fill(&b,sizeof b,n+1);fill(&c,sizeof c,n+2);V r[]={"
    "__builtin_elementwise_max(a,b),__builtin_elementwise_min(a,b),__builtin_elementwise_add_sat(a,b),__builtin_elementwise_sub_sat(a,b),"
    "__builtin_elementwise_popcount(a),__builtin_elementwise_bitreverse(a),"
    "__builtin_elementwise_fshl(a,b,c),__builtin_elementwise_fshr(a,b,c),__builtin_elementwise_pdep(a,b),__builtin_elementwise_pext(a,b),"
    "__builtin_elementwise_clmul(a,b)};"
    "for(unsigned j=0;j<sizeof r/sizeof r[0];j++)for(unsigned i=0;i<N;i++)h=(h^(unsigned)r[j][i])*16777619u;return h;}\n"
    "CASE(1)CASE(3)CASE(8)CASE(12)CASE(32)CASE(64)CASE(65)CASE(128)CASE(1024)\n"
    "int tested(int n){unsigned h=2166136261u;h=bits1(h,n);h=bits3(h,n+1);h=bits8(h,n+2);h=bits12(h,n+3);h=bits32(h,n+4);"
    "h=bits64(h,n+5);h=bits65(h,n+6);h=bits128(h,n+7);h=bits1024(h,n+8);"
    "typedef _Bool B3 __attribute__((ext_vector_type(3)));struct __attribute__((packed)) guard{unsigned char pre;B3 value;unsigned char post;};"
    "struct guard guarded={0x5a,{1,0,1},0xc7};B3 b={0,1,(_Bool)(n&1)};guarded.value=__builtin_elementwise_add_sat(guarded.value,b);"
    "FOLD(guarded.pre);FOLD(guarded.post);for(int i=0;i<3;i++)h=(h^(unsigned)guarded.value[i])*16777619u;"
    "unsigned calls[3]={0};B3 once=__builtin_elementwise_fshl((calls[0]++,guarded.value),(calls[1]++,b),(calls[2]++,b));"
    "FOLD(calls);for(int i=0;i<3;i++)h=(h^(unsigned)once[i])*16777619u;"
    "once=__builtin_elementwise_fshr((calls[0]++,guarded.value),(calls[1]++,b),(calls[2]++,b));"
    "FOLD(calls);for(int i=0;i<3;i++)h=(h^(unsigned)once[i])*16777619u;return (int)h;}\n";

// The programs run where the host has the target's features and saves their state (x86_target_native, CPUID and
// XCR0, whatever ISA the host build selected); elsewhere they are compiled and linked, as clang's builds are, and not
// run.
static void check_test_program_vector_sources(CheckTest* test, const char* clang, const X86Target* target)
{
    const X86Target native = x86_target_native();
    const bool run_native = (native.features & target->features) == target->features;
    if (!run_native)
    {
        printf("  vectors: native execution skipped (the host lacks x86-64-v4's features or their saved state); the programs compile and link\n");
    }
    static const char* const programs[][2] = {
        {
            "vector-integer-operators",
            "int tested(int n){unsigned h=2166136261u;v64qi qa,qb;v16qu ua,ub;v16hi ha,hb;v8hu wa,wb;v8si sa,sb;v16su da,db;v2di la,lb;v8du ea,eb;"
            "fill(&qa,64,n);fill(&qb,64,n+1);fill(&ua,16,n+2);fill(&ub,16,n+3);fill(&ha,32,n+4);fill(&hb,32,n+5);fill(&wa,16,n+6);fill(&wb,16,n+7);"
            "fill(&sa,32,n+8);fill(&sb,32,n+9);fill(&da,64,n+10);fill(&db,64,n+11);fill(&la,16,n+12);fill(&lb,16,n+13);fill(&ea,64,n+14);fill(&eb,64,n+15);"
            "if(n&1){qb=qa;sb=sa;}"
            "v64qi q[]={qa+qb,qa-qb,qa&qb,qa|qb,qa^qb,~qa,-qb,qa<qb,qa==qb,qa>=3,qa+5,qa!=qb};FOLD(q);"
            "v16qu u[]={ua+ub,ua-ub,(v16qu)(ua<ub),(v16qu)(ua>ub),(v16qu)(ua<=ub),(v16qu)(ua==7)};FOLD(u);"
            "v16hi hh[]={ha*hb,ha<<(hb&15),ha>>(hb&15),ha>hb,ha+(short)n,-ha,ha<<3,ha>>2};FOLD(hh);"
            "v8hu w[]={wa*wb,wa<<(wb&15),wa>>(wb&15),(v8hu)(wa>wb),(v8hu)(wa!=wb),wa-(unsigned short)n,wa<<3};FOLD(w);"
            "v8si s[]={sa*sb,sa>>(sb&31),sa<<(sb&31),sa<=sb,n*sa,-sa,sa>>2,sa+sb};FOLD(s);"
            "v16su d[]={da>>(db&31),(v16su)(da<db),da*7u,(da^db)+1,(v16su)(da>=db)};FOLD(d);"
            "v2di l[]={la*lb,la>>(lb&63),la>lb,la+(long long)n<<20,-la,~lb};FOLD(l);"
            "v8du e[]={ea>>(eb&63),(v8du)(ea>=eb),ea*eb,ea<<1,ea-eb};FOLD(e);"
            "return (int)h;}",
        },
        {
            "vector-floating-operators",
            "int tested(int n){unsigned h=2166136261u;v4sf fa,fb;v8sf ga,gb;v16sf za,zb;v2df da,db;v8df ea,eb;"
            "fillf((float*)&fa,4,n);fillf((float*)&fb,4,n+1);fillf((float*)&ga,8,n+2);fillf((float*)&gb,8,n+3);fillf((float*)&za,16,n+4);"
            "fillf((float*)&zb,16,n+5);filld((double*)&da,2,n+6);filld((double*)&db,2,n+7);filld((double*)&ea,8,n+8);filld((double*)&eb,8,n+9);"
            "if(n&2){gb=ga;eb=ea;}float s=(float)n/4.0f;double t=(double)n*1.5;"
            "v4sf f[]={fa+fb,fa-fb,fa*fb,fa/fb,-fa,fa*s,2.5f-fa};FOLD(f);"
            "v8sf g[]={ga+gb,ga/gb,-gb,(v8sf)(ga<gb),(v8sf)(ga==gb),(v8sf)(ga!=gb),(v8sf)(ga>=gb)};FOLD(g);"
            "v16sf z[]={za*zb-za,za/(zb+1.0f),(v16sf)(za>zb),(v16sf)(za<=zb),-za};FOLD(z);"
            "v2df d[]={da+db,da/db,-db,da*t,(v2df)(da<db)};FOLD(d);"
            "v8df e[]={ea+eb,ea/eb,-eb,ea*t,(v8df)(ea>eb),(v8df)(ea<=eb),(v8df)(ea>=eb),(v8df)(ea==eb)};FOLD(e);"
            "return (int)h;}",
        },
        {
            "vector-calls-and-values",
            "static v4si add4(v4si a,v4si b,int k){return a+b*k;}static v8sf scale(v8sf a,float s){return a*s;}"
            "static v16su mix(v16su a,v16su b){return (a^b)+(a&b);}static v8df pick(int c,v8df a,v8df b){return c?a:b;}"
            "static v2di many(v2di a,v2di b,v2di c,v2di d,v2di e,v2di f,v2di g,v2di h,v2di i,v2di j){return a+b+c+d+e+f+g+h+i+j;}"
            "int tested(int n){unsigned h=2166136261u;v4si a={n,2,-3,4};v4si b=(v4si){5,n*6,7,8};v4si c=add4(a,b,n);FOLD(c);"
            "int e=c[n&3]+a[0];c[1]=e;c[(n+2)&3]+=3;FOLD(c);"
            "v8sf f;fillf((float*)&f,8,n);v8sf g=scale(f,0.5f);g[7]=(float)e;FOLD(g);"
            "v16su x,y;fill(&x,64,n);fill(&y,64,n*3);v16su z=mix(x,y);z[0]=(unsigned)(z[1]+z[15]);FOLD(z);"
            "v8df p,q;filld((double*)&p,8,n);filld((double*)&q,8,n+5);v8df r=pick(n&1,p,q);double w=r[n&7];FOLD(r);FOLD(w);"
            "v2di l={n,-n};v2di m=many(l,l,l,l,l,l,l,l,l,l);FOLD(m);"
            "v4si s=n>2?a:b;v4si t=(a,b);v4si u=({v4si k=a+1;k*2;});FOLD(s);FOLD(t);FOLD(u);"
            "v8si wide=(v8si){1,2,3,4,5,6,7,n};v4di cast=(v4di)wide;cast[1]^=(long long)n<<40;FOLD(cast);"
            "v16qu bytes=(v16qu)a;bytes[3]=(unsigned char)n;FOLD(bytes);"
            "return (int)h;}",
        },
        {
            // Vectors Tier 0 has no class for (#137), which the producer passes, returns, copies and stores by their bytes
            // (check_lower_transport_type): of 1 and 2 bytes, of bools (an initializer's element at run time among them),
            // clang's extended ones of 2 bytes and one of 128, as parameters, results, va_arg's, members, an array's
            // elements, globals and an _Atomic's value, and elements of the ones whose elements are bytes.
            "vector-transport",
            "typedef signed char t1 __attribute__((vector_size(1)));typedef char t2 __attribute__((vector_size(2)));typedef short t2s __attribute__((vector_size(2)));"
            "typedef _Bool b8 __attribute__((ext_vector_type(8)));typedef _Bool b16 __attribute__((ext_vector_type(16)));typedef _Bool b64 __attribute__((ext_vector_type(64)));"
            "typedef unsigned char e2 __attribute__((ext_vector_type(2)));typedef int w32 __attribute__((vector_size(128)));"
            "struct tr{t2 b[2];t2s d;b8 c;t1 a;};"
            "static t2 pick2(t2 a,t2 b,int k){return k?a:b;}static t1 back1(t1 a){t1 r=a;return r;}"
            "static b8 back8(b8 a,b16 b,b64 c,b64*out){*out=c;(void)b;return a;}static e2 backe(e2 a){return a;}"
            "static w32 backw(w32 a,int k){w32 r=a;r[k&31]=k;return r;}static struct tr backs(struct tr s,t1 x){s.a=x;s.b[1]=s.b[0];return s;}"
            "static t2 variadic(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);t2 a=__builtin_va_arg(ap,t2);b8 b=__builtin_va_arg(ap,b8);"
            "t2s c=__builtin_va_arg(ap,t2s);b64 d=__builtin_va_arg(ap,b64);__builtin_va_end(ap);a[0]^=*(char*)&b^*(char*)&c^*(char*)&d;return a;}"
            "static _Atomic(t2) atomic2;static t2 global2={3,4};static b8 globalb={1,0,1,1,0,0,0,1};"
            "int tested(int n){unsigned h=2166136261u;"
            "t2 a={(char)n,2},b;fill(&b,2,n);t2 c=pick2(a,b,n&1);FOLD(c);a[1]=(char)(n*3);FOLD(a);"
            "t1 o;fill(&o,1,n+1);t1 p=back1(o);FOLD(p);"
            "b8 m={1,0,(_Bool)(n&1),1,0,1,0,0};b16 q;fill(&q,2,n+2);b64 r,s;fill(&r,8,n+3);b8 k=back8(m,q,r,&s);FOLD(k);FOLD(s);"
            "e2 e={(unsigned char)n,0x20};e2 f=backe(e);FOLD(f);"
            "w32 w;fill(&w,128,n+4);w32 x=backw(w,n);FOLD(x);int y=x[n&31];FOLD(y);"
            "struct tr t;fill(&t,sizeof(t),n+5);struct tr u=backs(t,o);FOLD(u);"
            "t2s z;fill(&z,2,n+6);t2 v=variadic(4,c,m,z,r);FOLD(v);"
            "atomic2=c;t2 g=atomic2;FOLD(g);"
            "global2=n&2?global2:c;FOLD(global2);FOLD(globalb);"
            "t2 arr[3]={a,b,c};t2 el=arr[(unsigned)n%3];FOLD(el);char ch=el[1];FOLD(ch);"
            "return (int)h;}",
        },
        {
            // Shuffles of bytes at x86-64-v4, which hasn't AVX-512 VBMI's VPERMT2B, element by element (#1834): of 8, 16,
            // 32 and 64 bytes, of both operands, with an undefined index, and the MMX unpacks of bytes and words
            // (_mm_unpackhi_pi8's, _mm_unpacklo_pi8's and _mm_unpackhi_pi16's shuffles).
            "vector-shuffles-without-vbmi",
            "typedef short s4 __attribute__((vector_size(8)));typedef char c8 __attribute__((vector_size(8)));typedef char c16 __attribute__((vector_size(16)));"
            "typedef char c32 __attribute__((vector_size(32)));typedef char c64 __attribute__((vector_size(64)));\n"
            "int tested(int n){unsigned h=2166136261u;c64 a,b;fill(&a,64,n);fill(&b,64,n+1);c8 a8=*(c8*)&a,b8=*(c8*)&b;c16 a16=*(c16*)&a,b16=*(c16*)&b;"
            "c32 a32=*(c32*)&a,b32=*(c32*)&b;"
            "c8 w=__builtin_shufflevector(a8,b8,15,0,14,1,13,2,12,3);FOLD(w);"
            "c16 x=__builtin_shufflevector(a16,b16,31,0,30,1,29,2,28,3,27,4,26,5,25,6,24,7);FOLD(x);"
            "c32 y=__builtin_shufflevector(a32,b32,63,0,62,1,61,2,60,3,59,4,58,5,57,6,56,7,33,34,35,36,1,2,3,4,40,41,42,43,9,9,9,-1);y[31]=0;FOLD(y);"
            "c64 z=__builtin_shufflevector(a,b,127,0,126,1,125,2,124,3,123,4,122,5,121,6,120,7,64,65,66,67,1,2,3,4,40,41,42,43,9,9,9,9,"
            "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115);FOLD(z);"
            "c8 m[]={__builtin_shufflevector(a8,b8,4,12,5,13,6,14,7,15),__builtin_shufflevector(a8,b8,0,8,1,9,2,10,3,11)};"
            "s4 u=__builtin_shufflevector((s4)a8,(s4)b8,2,6,3,7);FOLD(m);FOLD(u);return (int)h;}",
        },
        {
            "vector-transport-compute",
            check_test_vector_transport,
        },
        {
            "vector-logical-and-conditions",
            check_test_vector_logical,
        },
        {
            // The lowering's room for an element-wise reduction (check_lower_transport_room), in a unit with nothing
            // else: a 1024-element vector of bool's maximum and minimum, its most entries an element.
            "vector-transport-reduce-room",
            "typedef _Bool b1024 __attribute__((ext_vector_type(1024)));"
            "static int most(b1024 v){return __builtin_reduce_max(v);}static int least(b1024 v){return __builtin_reduce_min(v);}"
            "int tested(int n){b1024 a,b;fill(&a,128,n);__builtin_memset(&b,n&1?0xff:0,128);b[(unsigned)n*37%1024]=n&2;"
            "return most(a)+2*least(a)+4*most(b)+8*least(b);}",
        },
        {
            "vector-memory",
            "static v8si global_vector={1,2,3,4,5,6,7,8};static v4sf global_array[3];struct holder{int tag;v4si v;v8df d;};static struct holder global_holder;"
            "static v16su through(const v16su*p,int i){return p[i]+p[i+1];}"
            "int tested(int n){unsigned h=2166136261u;global_vector=global_vector+n;global_vector[n&7]=-n;FOLD(global_vector);"
            "for(int i=0;i<3;i++)global_array[i]=(v4sf){(float)i,(float)n,0.5f,-1.0f}*(float)(i+1);FOLD(global_array);"
            "struct holder local={n,{1,2,3,n},{0}};local.d=local.d+(double)n;local.v[2]=local.tag*3;global_holder=local;"
            "global_holder.v=global_holder.v*local.v;FOLD(global_holder.v);FOLD(global_holder.d);"
            "v16su array[4];fill(array,sizeof(array),n);v16su*pointer=array+1;v16su sum=through(array,n&1)+*pointer+pointer[1];"
            "pointer[-1]=sum;FOLD(array);"
            "return (int)h;}",
        },
        {
            "vector-compound-assignments",
            "static v8si global_sum;int tested(int n){unsigned h=2166136261u;v8si a={n,2,3,4,5,6,7,8},b={1,n,1,n,1,n,1,n};"
            "v8hu w={1,2,3,4,5,6,7,(unsigned short)n};v8df d={0.5,1.5,2.5,3.5,4.5,5.5,6.5,(double)n};v8si x[2];fill(x,sizeof(x),n);"
            "a+=b;a-=3;a*=b;a<<=2;a>>=b&3;a&=~b;a|=0x100;a^=b;FOLD(a);w*=w;w>>=1;w+=(unsigned short)n;FOLD(w);"
            "d*=2.0;d/=d+1.0;d-=0.25;FOLD(d);global_sum=a;global_sum+=a;*x+=global_sum;x[1]^=a;FOLD(global_sum);FOLD(x);"
            "return (int)h;}",
        },
        {
            "vector-rvalue-elements",
            "static v4si twice(v4si a){return a+a;}static v8df halve(v8df a){return a*0.5;}"
            "int tested(int n){unsigned h=2166136261u;v4si a={n,2,-3,4},b={5,n*6,7,8};v8sf f;fillf((float*)&f,8,n);v8df d;filld((double*)&d,8,n);"
            "v16su x;fill(&x,64,n);int out[4]={0};"
            "long long l=((v2di)a)[n&1];int s=(a+b)[n&3]+(a-b)[1];int c=twice(b)[2];float g=(f*2.0f)[n&7];double e=(n&1?d:-d)[3];"
            "unsigned char q=((v16qu)b)[n&15];unsigned z=(x>>1)[n&15]+(~x)[15];double r=halve(d)[n&7];int k=({v4si t=a*b;t;})[0];"
            "out[n&3]=(a^b)[1];out[(n+1)&3]+=(b<<1)[3];FOLD(l);FOLD(s);FOLD(c);FOLD(g);FOLD(e);FOLD(q);FOLD(z);FOLD(r);FOLD(k);FOLD(out);"
            "return (int)h+(a*3)[n&3];}",
        },
        {
            "atomic-vector-transport",
            "typedef int A4 __attribute__((vector_size(16)));"
            "_Atomic(A4) external_atomic4(_Atomic(A4));int external_apply_atomic4(_Atomic(A4)(*)(_Atomic(A4)),int);"
            "static _Atomic(A4) local_atomic4(_Atomic(A4) x){A4 v=x;return v+1;}"
            "typedef int A8 __attribute__((vector_size(32)));"
            "_Atomic(A8) external_atomic8(_Atomic(A8));int external_apply_atomic8(_Atomic(A8)(*)(_Atomic(A8)),int);"
            "static _Atomic(A8) local_atomic8(_Atomic(A8) x){A8 v=x;return v+1;}"
            "typedef int A16 __attribute__((vector_size(64)));"
            "_Atomic(A16) external_atomic16(_Atomic(A16));int external_apply_atomic16(_Atomic(A16)(*)(_Atomic(A16)),int);"
            "static _Atomic(A16) local_atomic16(_Atomic(A16) x){A16 v=x;return v+1;}"
            "int tested(int n){unsigned h=2166136261u;"
            "A4 a4={0};for(int i=0;i<4;i++)a4[i]=n+i;_Atomic(A4) s4=a4;A4 r4=s4;FOLD(r4);"
            "s4=a4+1;A4 e4=a4+1;int ok4=__c11_atomic_compare_exchange_strong(&s4,&e4,a4+2,5,0);FOLD(ok4);"
            "ok4=__c11_atomic_compare_exchange_strong(&s4,&e4,a4+3,5,0);FOLD(ok4);FOLD(e4);"
            "r4=__c11_atomic_exchange(&s4,a4+4,5);FOLD(r4);__c11_atomic_store(&s4,a4+5,5);r4=__c11_atomic_load(&s4,5);FOLD(r4);"
            "_Atomic(A4) z4=external_atomic4(a4);r4=z4;FOLD(r4);int q4=external_apply_atomic4(local_atomic4,n);FOLD(q4);"
            "A8 a8={0};for(int i=0;i<8;i++)a8[i]=n+i;_Atomic(A8) s8=a8;A8 r8=s8;FOLD(r8);"
            "s8=a8+1;A8 e8=a8+1;int ok8=__c11_atomic_compare_exchange_strong(&s8,&e8,a8+2,5,0);FOLD(ok8);"
            "ok8=__c11_atomic_compare_exchange_strong(&s8,&e8,a8+3,5,0);FOLD(ok8);FOLD(e8);"
            "r8=__c11_atomic_exchange(&s8,a8+4,5);FOLD(r8);__c11_atomic_store(&s8,a8+5,5);r8=__c11_atomic_load(&s8,5);FOLD(r8);"
            "_Atomic(A8) z8=external_atomic8(a8);r8=z8;FOLD(r8);int q8=external_apply_atomic8(local_atomic8,n);FOLD(q8);"
            "A16 a16={0};for(int i=0;i<16;i++)a16[i]=n+i;_Atomic(A16) s16=a16;A16 r16=s16;FOLD(r16);"
            "s16=a16+1;A16 e16=a16+1;int ok16=__c11_atomic_compare_exchange_strong(&s16,&e16,a16+2,5,0);FOLD(ok16);"
            "ok16=__c11_atomic_compare_exchange_strong(&s16,&e16,a16+3,5,0);FOLD(ok16);FOLD(e16);"
            "r16=__c11_atomic_exchange(&s16,a16+4,5);FOLD(r16);__c11_atomic_store(&s16,a16+5,5);r16=__c11_atomic_load(&s16,5);FOLD(r16);"
            "_Atomic(A16) z16=external_atomic16(a16);r16=z16;FOLD(r16);int q16=external_apply_atomic16(local_atomic16,n);FOLD(q16);"
            "return (int)h;}",
        },
        {
            // Atomic vectors of 8 and 4 bytes (#137): their atomic loads and stores, which go through their buffers'
            // addresses at their own bytes (check_lower_atomic_narrow_vector), exchanges and compare-exchanges, globals,
            // and parameters and results in XMM registers (a 4-byte one's value alone in xmm0), with calls both ways
            // with clang's code.
            "atomic-vector-narrow-transport",
            "typedef signed char Q8 __attribute__((vector_size(8)));typedef short H2 __attribute__((vector_size(4)));"
            "typedef float F2 __attribute__((vector_size(8)));typedef unsigned char U4 __attribute__((vector_size(4)));"
            "_Atomic(Q8) external_atomic_q8(_Atomic(Q8),int);int external_apply_atomic_q8(_Atomic(Q8)(*)(_Atomic(Q8),int),int);"
            "_Atomic(H2) external_atomic_h2(_Atomic(H2),int);int external_apply_atomic_h2(_Atomic(H2)(*)(_Atomic(H2),int),int);"
            "static _Atomic(Q8) local_q8(_Atomic(Q8) x,int k){Q8 v=x;return v-(signed char)k;}"
            "static _Atomic(H2) local_h2(_Atomic(H2) x,int k){H2 v=x;return v+(short)k;}"
            "static __attribute__((noinline)) _Atomic(F2) pass_f2(_Atomic(F2) x,_Atomic(U4) y,_Atomic(F2)*out){F2 v=x;U4 u=y;*out=v*2.0f;"
            "return v+(float)u[1];}"
            "static _Atomic(Q8) global_q8;static _Atomic(U4) global_u4;"
            "int tested(int n){unsigned h=2166136261u;"
            "Q8 a={(signed char)n,2,-3,4,5,(signed char)(n*7),-128,127};_Atomic(Q8) s=a;Q8 r=s;FOLD(r);"
            "s=a+1;Q8 e=a+1;int ok=__c11_atomic_compare_exchange_strong(&s,&e,a+2,5,0);FOLD(ok);"
            "ok=__c11_atomic_compare_exchange_strong(&s,&e,a+3,5,0);FOLD(ok);FOLD(e);"
            "r=__c11_atomic_exchange(&s,a+4,5);FOLD(r);__c11_atomic_store(&s,a+5,5);r=__c11_atomic_load(&s,5);FOLD(r);"
            "_Atomic(Q8) z=external_atomic_q8(a,n);r=z;FOLD(r);int q=external_apply_atomic_q8(local_q8,n);FOLD(q);"
            "H2 b={(short)(n*1000),-2};_Atomic(H2) t=b;H2 rb=t;FOLD(rb);H2 eb=b;ok=__c11_atomic_compare_exchange_strong(&t,&eb,b*2,5,0);FOLD(ok);"
            "rb=__c11_atomic_exchange(&t,b+3,5);FOLD(rb);rb=t;FOLD(rb);"
            "_Atomic(H2) w=external_atomic_h2(b,n);rb=w;FOLD(rb);q=external_apply_atomic_h2(local_h2,n);FOLD(q);"
            "F2 f={1.5f*(float)n,-0.25f};U4 u={1,(unsigned char)n,3,4};_Atomic(F2) out;_Atomic(F2) g=pass_f2(f,u,&out);F2 gv=g,ov=out;FOLD(gv);"
            "FOLD(ov);global_q8=a;global_u4=u;Q8 gq=global_q8;U4 gu=global_u4;FOLD(gq);FOLD(gu);"
            "return (int)h;}",
        },
        {
            "volatile-vector-values",
            "int tested(int n){unsigned h=2166136261u;volatile v4si a={n,2,3,4};v4si b=a;a=b+1;"
            "int x=((v4si)a)[n&3];v4si c=(0,a);int y=c[(n+1)&3];FOLD(b);FOLD(x);FOLD(y);return (int)h;}",
        },
        {
            // Vectors of 8 and 4 bytes, in XMM registers whose bytes past them are zero: operators at each element, a
            // scalar's splat (5LL into two ints, a constant that fits, and 5 into four chars) as either operand, a shift's
            // count or shifted value, an arm of ?: (with and without its middle operand) and the right of a compound
            // assignment, calls passing and returning them as clang's ABI classes them (8 bytes in XMM registers, 4 in
            // general ones, a vector of one double in memory), members and arrays, globals, ?:, casts to and from
            // integers of their bytes (bit casts, as GNU C's vectors cast, a constant's too; an ext_vector_type's cast
            // splats), elements, nontemporal stores and loads.
            "vector-narrow",
            "typedef int v2si __attribute__((vector_size(8)));typedef unsigned v2su __attribute__((vector_size(8)));"
            "typedef short v4hi __attribute__((vector_size(8)));typedef signed char v8qi __attribute__((vector_size(8)));"
            "typedef signed char v4qi __attribute__((vector_size(4)));typedef unsigned char v4qu __attribute__((vector_size(4)));"
            "typedef short v2hi __attribute__((vector_size(4)));typedef float v2sf __attribute__((vector_size(8)));"
            "typedef long long v1di __attribute__((vector_size(8)));typedef float f2 __attribute__((ext_vector_type(2)));"
            "typedef double v1df __attribute__((vector_size(8)));"
            "struct holder{char tag;v2si pair;v4qi quad;};static v2si global2={7,-9};static v4qi global4={1,2,3,4};"
            "static __attribute__((noinline)) v2si mix(v2si a,v4qi b,v2sf c){v2si r=a+(v2si){b[0],b[3]};r[1]+=(int)c[1];return r;}"
            "static __attribute__((noinline)) v4qi quad(v4qi a,int k){return (a+(v4qi){(signed char)k,2,3,4})^a;}"
            "static __attribute__((noinline)) v1df scale(v1df a,v1di b,v4qu c){return a*(double)b[0]+(double)c[2];}"
            "int tested(int n){unsigned h=2166136261u;v2si a={n,n*3},b={-n,11};v2su u={(unsigned)n,0xffffffffu};"
            "v4hi w4={(short)n,-2,300,(short)(n*1000)};v8qi q={1,2,3,(signed char)n,5,6,7,-8};v4qi s={(signed char)n,-1,64,9};"
            "v4qu t={200,(unsigned char)n,3,255};v2hi w={(short)-n,1234};v2sf f={1.5f,(float)n};"
            "v2si r1[]={a+b,a*b-(a>>1),a==b,a<(v2si){n,100},a+5LL,(v2si)(v4hi)a};FOLD(r1);"
            "v2si r9[]={5LL+a,(n&1)?a:5LL,(n&2)?7LL:a,a<<2LL,3LL<<(a&7),(v2si)5LL,(n&4)?:a,5LL?:a};v2si c2=a;c2+=5LL;c2<<=1LL;FOLD(r9);FOLD(c2);"
            "v4hi r10=w4==3LL;v4qi r11[]={(n&1)?9:s,s};r11[1]+=1;FOLD(r10);FOLD(r11);"
            "v2su r2=u>>3;v4hi r3[]={w4<<2,-w4,w4*(short)3};v8qi r4[]={q+q,~q&(v8qi){15,15,15,15,127,127,127,127}};FOLD(r2);FOLD(r3);FOLD(r4);"
            "v4qi r5=s^(v4qi){85,85,85,85};v4qu r6=t+5;v2hi r7=w*(short)3;v2sf r8[]={f*2.0f+f,-f};FOLD(r5);FOLD(r6);FOLD(r7);FOLD(r8);"
            "long long bits=(long long)a;v2si back=(v2si)(bits+1);int four=(int)s;v4qi from4=(v4qi)(four^0x01010101);v1di one=(v1di)(bits*3);"
            "FOLD(bits);FOLD(back);FOLD(four);FOLD(from4);FOLD(one);v1df sc=scale((v1df){1.5*n},one,t);FOLD(sc);"
            "a[1]=77;s[2]+=3;int e=q[3]+w4[2]+t[1];FOLD(a);FOLD(s);FOLD(e);"
            "struct holder hold={'x',a,s};hold.pair+=b;hold.quad[1]=(signed char)n;FOLD(hold.pair);FOLD(hold.quad);"
            "v2si array[3]={a,b,{1,2}};array[(unsigned)n%3u]=array[((unsigned)n+1)%3u]*2;v2si*p=&array[2];*p=*p+1;FOLD(array);"
            "global2=global2-a;global4=global4+s;FOLD(global2);FOLD(global4);"
            "v2si pick=(n&1)?a:b;v2si called=mix(a,s,f);v4qi q2=quad(s,n);f2 ext=(f2)3.0f;FOLD(pick);FOLD(called);FOLD(q2);FOLD(ext);"
            "__builtin_nontemporal_store(a,&array[0]);v2si nt=__builtin_nontemporal_load(&array[1]);v4qi nt4;__builtin_nontemporal_store(s,&nt4);"
            "FOLD(array);FOLD(nt);FOLD(nt4);"
            "return (int)h;}",
        },
        {
            // Divisions of vectors of 8 and 4 bytes under FENV_ACCESS: each divides its own elements alone, so that the
            // zeros past them in their registers raise no FE_INVALID (clang's code divides them one at a time), and
            // their own elements' 0/0 and division by zero raise theirs. The flags of each are folded with its value.
            "vector-narrow-division-flags",
            "\n#pragma STDC FENV_ACCESS ON\n"
            "typedef float v2sf __attribute__((vector_size(8)));typedef float v1sf __attribute__((vector_size(4)));"
            "typedef double v1df __attribute__((vector_size(8)));int clear_fp(void);int fp_state(void);"
            "int tested(int n){unsigned h=2166136261u;volatile float one=1.0f;v2sf a={one,2.0f*(float)n},b={2.0f,4.0f};"
            "clear_fp();v2sf c=a/b;int f1=fp_state();FOLD(c);FOLD(f1);"
            "v1sf d={3.0f*(float)n},e={one+1.0f};clear_fp();v1sf g=d/e;int f2=fp_state();FOLD(g);FOLD(f2);"
            "v1df p={3.0*n},q={4.0};clear_fp();v1df r=p/q;int f3=fp_state();FOLD(r);FOLD(f3);"
            "v2sf z={0.0f*(float)n,one};clear_fp();v2sf s=z/(v2sf){0.0f,0.0f};int f4=fp_state();FOLD(s);FOLD(f4);"
            "v2sf w=a;clear_fp();w/=2.0f*(float)(n|1);int f5=fp_state();FOLD(w);FOLD(f5);"
            "return (int)h;}",
        },
        {
            "vector-aligned-locals",
            "typedef int v16si __attribute__((vector_size(64)));typedef double v4df __attribute__((vector_size(32)));"
            "int tested(int n){unsigned h=2166136261u;v16si a={n},b={2};a=a*b+a;FOLD(a);v4df d={(double)n,1.0};d=d*d;FOLD(d);"
            "v16si array[2]={a,b};array[1]+=array[0];FOLD(array);int t=(a*b)[n&15]+(d+d)[n&3];FOLD(t);return (int)h;}",
        },
        {
            // A transparent union (#147) whose first member, a struct of four floats, passes in xmm0 and xmm1: a vector
            // its conversion selects goes through a temporary of the vector's bytes, and the union's own value.
            "vector-transparent-union-member",
            "typedef union __attribute__((transparent_union)){struct{_Alignas(16) float a[4];}s;v4sf v;}TV;"
            "static unsigned sum(int k,TV x){unsigned h=2166136261u+(unsigned)k;FOLD(x.s);return h;}"
            "int tested(int n){v4sf v;fillf((float*)&v,4,n);TV t={.v=v+1};unsigned h=sum(1,v)^sum(2,t)^sum(3,v*v);return (int)h;}",
        },
        {
            // Unnamed vectors (#137): 16-byte ones from the save area's vector slots until the eight registers are taken,
            // then from the overflow area, also through a va_copy; and the wider ones, which a variadic call passes in
            // memory, from the overflow area at their alignments, 16 (the aligned typedefs) and 32 and 64.
            "vector-varargs",
            "typedef int v16si __attribute__((vector_size(64)));typedef double v4df __attribute__((vector_size(32)));"
            "static unsigned narrow(int n,...){unsigned h=2166136261u;__builtin_va_list ap,copy;__builtin_va_start(ap,n);"
            "for(int i=0;i<n;i++){v4si a=__builtin_va_arg(ap,v4si);v2df d=__builtin_va_arg(ap,v2df);FOLD(a);FOLD(d);if(i==2)__builtin_va_copy(copy,ap);}"
            "double e=__builtin_va_arg(ap,double);v4si t=__builtin_va_arg(copy,v4si);FOLD(e);FOLD(t);__builtin_va_end(copy);__builtin_va_end(ap);return h;}"
            "static unsigned wide(int n,...){unsigned h=2166136261u;__builtin_va_list ap;__builtin_va_start(ap,n);"
            "for(int i=0;i<n;i++){v8sf b=__builtin_va_arg(ap,v8sf);v16su c=__builtin_va_arg(ap,v16su);v16si f=__builtin_va_arg(ap,v16si);"
            "v4df g=__builtin_va_arg(ap,v4df);FOLD(b);FOLD(c);FOLD(f);FOLD(g);}__builtin_va_end(ap);return h;}"
            "int tested(int n){v4si a={n,2,-3,4};v8sf b;fillf((float*)&b,8,n);v16su c;fill(&c,64,n);v2df d={(double)n,0.25};"
            "v16si f={n,n*n,-n};v4df g={0.5,(double)n};"
            "unsigned x=narrow(5,a,d,a+1,d+d,a*3,d,a-n,d*d,a,d,1.5);unsigned y=wide(2,b,c,f,g,b*2.0f,c^1u,f+n,g+g);return (int)(x^y);}",
        },
        {
            // Past the integer depth table (#780): calls of 16 arguments that return vectors of 16, 32 and 64 bytes (the
            // first as _mm_set1_epi8 calls _mm_set_epi8), one of 16 and two vectors computed past it, and an addition
            // nested 18 deep beside a vector.
            "vectors-past-the-depth-tables",
            "v16qu vector_spread(unsigned char a0,unsigned char a1,unsigned char a2,unsigned char a3,unsigned char a4,unsigned char a5,unsigned char a6,"
            "unsigned char a7,unsigned char a8,unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13,unsigned char a14,"
            "unsigned char a15){v16qu v={a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15};return v;}"
            "v8si vector_pairs(int a0,int a1,int a2,int a3,int a4,int a5,int a6,int a7,int a8,int a9,int a10,int a11,int a12,int a13,int a14,int a15)"
            "{v8si v={a0+a8,a1+a9,a2+a10,a3+a11,a4+a12,a5+a13,a6+a14,a7+a15};return v;}"
            "v16su vector_wide(unsigned a0,unsigned a1,unsigned a2,unsigned a3,unsigned a4,unsigned a5,unsigned a6,unsigned a7,unsigned a8,unsigned a9,"
            "unsigned a10,unsigned a11,unsigned a12,unsigned a13,unsigned a14,unsigned a15)"
            "{v16su v={a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15};return v*3u;}"
            "int vector_taken(int a0,int a1,int a2,int a3,int a4,int a5,int a6,int a7,int a8,int a9,int a10,int a11,int a12,int a13,int a14,int a15,"
            "v8si v,v16qu q){return a0+a15+v[a1&7]+q[a2&15];}"
            "int tested(int n){unsigned h=2166136261u;unsigned char b=(unsigned char)n;"
            "v16qu q=vector_spread(b,b,b,b,b,b,b,b,b,b,b,b,b,b,b,b);FOLD(q);"
            "v8si s=vector_pairs(n+0,n+1,n+2,n+3,n+4,n+5,n+6,n+7,n+8,n+9,n+10,n+11,n+12,n+13,n+14,n+15);FOLD(s);"
            "v16su d=vector_wide(n*1,n*2,n*3,n*4,n*5,n*6,n*7,n*8,n*9,n*10,n*11,n*12,n*13,n*14,n*15,n*16);FOLD(d);"
            "int t=vector_taken(n+0,n+1,n+2,n+3,n+4,n+5,n+6,n+7,n+8,n+9,n+10,n+11,n+12,n+13,n+14,n+15,s*s+s,q+q);FOLD(t);"
            "v8si u=s+(n+0+(n+1+(n+2+(n+3+(n+4+(n+5+(n+6+(n+7+(n+8+(n+9+(n+10+(n+11+(n+12+(n+13+(n+14+(n+15+(n+16+(n+17+(n)))))))))))))))))));"
            "FOLD(u);return (int)h;}",
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        const size_t length = strlen(check_test_vector_types) + strlen(programs[i][1]) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "vector program source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s", check_test_vector_types, programs[i][1]);
            check_test_program_source_native_limits(test, source, programs[i][0], clang, false, -1, 0, target, run_native);
            free(source);
        }
    }

    // Clang 23.1.4 -O0 for x86-64-v4 miscompiles clmul's last defined lane of a 65-element bool vector: at
    // tested(16), lane 64 returns true for true * false. Its -O2 build and the lower target levels agree with the
    // one-bit product, a & b. Keep every builtin in Buster's source and use that independent operation for this
    // oracle alone; the baseline fixtures below still compare clang's clmul at SSE2/v2/v3.
    static const char clmul_oracle[] = "#define __builtin_elementwise_clmul(a,b) ((a)&(b))\n";
    char source[sizeof(check_test_vector_types) + sizeof(check_test_vector_boolean_builtins) - 1];
    char oracle[sizeof(clmul_oracle) + sizeof(source) - 1];
    snprintf(source, sizeof(source), "%s%s", check_test_vector_types, check_test_vector_boolean_builtins);
    snprintf(oracle, sizeof(oracle), "%s%s", clmul_oracle, source);
    check_test_program_source_native_bounds(test, source, "vector-boolean-builtins", clang, false, -1, 0, target, run_native, 0, oracle, false,
                                            2017, true, "-std=gnu17");
}

// What the lowering refuses by name, with vectors of each kind it can't take yet.
static void check_test_program_vector_refusals(CheckTest* test, const X86Target* target)
{
    static const struct
    {
        const char* name;
        const char* source;
        const char* reason;
        uint32_t initializations;
    } refused[] = {
        {
            // Vectors passed by their bytes (#137, check_lower_transport_type) compute as clang -O0's code does
            // (c_check_program_transport.c), but where clang's own code doesn't, and as the ordinary vectors refuse.
            "vector-of-bool-element-update",
            "typedef _Bool b8 __attribute__((ext_vector_type(8)));int tested(int n){b8 a={(_Bool)n,1};a[n&7]|=1;return a[0];}",
            "compound assignments and increments of elements of vectors of bool, which clang 23 miscompiles",
            1,
        },
        {
            "vector-of-bool-element-increment",
            "typedef _Bool b8 __attribute__((ext_vector_type(8)));int tested(int n){b8 a={(_Bool)n,1};a[1]++;return a[1];}",
            "compound assignments and increments of elements of vectors of bool, which clang 23 miscompiles",
            1,
        },
        {
            "vector-of-bool-leading-zero-count",
            "typedef _Bool V __attribute__((ext_vector_type(8)));void tested(V*p,V*q){*p=__builtin_elementwise_clzg(*p,*q);}",
            "elementwise leading/trailing zero counts of boolean vectors, which clang 23 miscompiles",
            0,
        },
        {
            "vector-of-bool-trailing-zero-count",
            "typedef _Bool V __attribute__((ext_vector_type(8)));void tested(V*p){*p=__builtin_elementwise_ctzg(*p);}",
            "elementwise leading/trailing zero counts of boolean vectors, which clang 23 miscompiles",
            0,
        },
        {
            "vector-bf16-arithmetic-past-64-bytes",
            "typedef __bf16 w64 __attribute__((vector_size(128)));void tested(w64*p,w64*q){*p=*p+*q;}",
            "vector arithmetic of bf16 elements without AVX512-BF16",
            0,
        },
        {
            "vector-logical-resource-bound",
            "typedef int V __attribute__((ext_vector_type(1025)));void tested(V*p,V*q){*p=*p&&*q;}",
            "logical operations of vectors of more than 1024 elements",
            0,
        },
        {
            "volatile-vector-element-read",
            "typedef int V __attribute__((vector_size(16)));int tested(volatile V*p,int i){return (*p)[i&3];}",
            "element access through volatile vectors",
            0,
        },
        {
            "volatile-vector-element-write",
            "typedef int V __attribute__((vector_size(16)));void tested(volatile V*p,int i){(*p)[i&3]=i;}",
            "element access through volatile vectors",
            0,
        },
        {
            "volatile-vector-element-update",
            "typedef int V __attribute__((vector_size(16)));int tested(volatile V*p,int i){return (*p)[i&3]+=i;}",
            "element access through volatile vectors",
            0,
        },
        {
            "volatile-vector-element-member",
            "typedef int V __attribute__((vector_size(16)));struct S{volatile V v;};int tested(struct S*p,int i){return p->v[i&3];}",
            "element access through volatile vectors",
            0,
        },
        {
            "volatile-vector-element-side-effect-base",
            "typedef int V __attribute__((vector_size(16)));int tested(volatile V*p,int*i){return p[(*i)++][0];}",
            "element access through volatile vectors",
            0,
        },
        {
            "atomic-vector-update",
            "typedef int V __attribute__((vector_size(16)));V tested(_Atomic(V)*p,V v){return *p+=v;}",
            "updates of atomic vector values",
            0,
        },
        {
            "vector-bf16-arithmetic",
            "typedef __bf16 v8bf __attribute__((vector_size(16)));void tested(v8bf*p,v8bf*q){*p=*p+*q;}",
            "vector arithmetic of bf16 elements without AVX512-BF16",
            0,
        },
        {
            "vector-integer-cast",
            "typedef int v4si __attribute__((vector_size(16)));int tested(int n){v4si a={n};__int128 x=(__int128)a;return (int)x;}",
            "casts between vectors and integers or vectors of other sizes",
            2,
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
    {
        check_test_program_boundary_target(test, refused[i].source, refused[i].name, true, false, 0, refused[i].initializations, refused[i].reason, target);
    }

}

// Accesses in a segment's space (#1447) that the lowering refuses by name, which compiled without the segment before: a
// bit of a vector of bool, an atomic builtin's object (which its pointer reaches), and an asm statement's register output
// (stored through its address). Clang's code has %gs or %fs on each.
static void check_test_program_segment_refusals(CheckTest* test, const X86Target* target)
{
    static const struct
    {
        const char* name;
        const char* source;
        const char* reason;
    } refused[] = {
        // A vector's element takes its vector's override (check_lower_element_segment; check_test_program_segments runs
        // those), but an element of a volatile vector.
        { "segment-volatile-vector-element", "typedef int V __attribute__((vector_size(16)));int tested(volatile V __seg_fs*p){return (*p)[1];}",
          "element access through volatile vectors" },
        // An atomic builtin's object in a segment's space takes the override where a direct plan takes the builtin
        // (check_test_program_segments runs those); a generic one by pointer, __sync_lock_release and __atomic_clear,
        // which take another plan, are refused. (A generic exchange whose result pointer is in another space than the
        // object's is clang's error, #2233: c_check_address_space_driver_test.c.)
        { "segment-atomic-generic-load", "int tested(int __seg_fs*p,int*r){__atomic_load(p,r,5);return *r;}",
          "a load or store in a segment's address space (256, 257 or 258: %gs, %fs or %ss)" },
        { "segment-sync-lock-release", "void tested(int __seg_gs*p){__sync_lock_release(p);}",
          "a load or store in a segment's address space (256, 257 or 258: %gs, %fs or %ss)" },
        { "segment-atomic-clear", "void tested(char __seg_gs*p){__atomic_clear(p,5);}",
          "a load or store in a segment's address space (256, 257 or 258: %gs, %fs or %ss)" },
        { "segment-atomic-expected", "_Bool tested(int*p,int __seg_gs*e){return __atomic_compare_exchange_n(p,e,1,0,5,5);}",
          "a load or store in a segment's address space (256, 257 or 258: %gs, %fs or %ss)" },
        { "segment-c11-atomic-expected", "_Bool tested(_Atomic int*p,int __seg_fs*e){return __c11_atomic_compare_exchange_strong(p,e,1,5,5);}",
          "a load or store in a segment's address space (256, 257 or 258: %gs, %fs or %ss)" },
        { "segment-atomic-generic-result", "void tested(int*p,int __seg_gs*r){__atomic_load(p,r,5);}",
          "a load or store in a segment's address space (256, 257 or 258: %gs, %fs or %ss)" },
        { "segment-asm-register-output", "void tested(int __seg_gs*p){__asm__(\"movl $1,%0\":\"=r\"(*p));}",
          "an asm output in a segment's address space (256, 257 or 258: %gs, %fs or %ss) but a memory one" },
        { "segment-asm-read-write-output", "void tested(int __seg_gs*p){__asm__(\"incl %0\":\"+r\"(*p));}",
          "an asm output in a segment's address space (256, 257 or 258: %gs, %fs or %ss) but a memory one" },
        // An aggregate's copy past 8 of the target's widest vector registers, which clang stops on too ("cannot lower
        // memory intrinsic in address space"): from the segment and into it.
        { "segment-struct-copy-past-limit", "struct S{char c[1025];};void tested(struct S*q,struct S __seg_fs*p){*q=*p;}",
          "an aggregate copy of more than 8 vector registers in a segment's address space (256, 257 or 258: %gs, %fs or %ss), which clang can't lower either" },
        { "segment-struct-store-past-limit", "struct S{char c[1025];};void tested(struct S __seg_gs*p,struct S*q){*p=*q;}",
          "an aggregate copy of more than 8 vector registers in a segment's address space (256, 257 or 258: %gs, %fs or %ss), which clang can't lower either" },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
    {
        check_test_program_boundary_target(test, refused[i].source, refused[i].name, true, false, 0, 0, refused[i].reason, target);
    }
}

// Loads and stores in __seg_fs and __seg_gs (#1447), run and compared with clang's code: thread-locals reached through
// %fs (their address less the thread pointer, __builtin_thread_pointer's), and globals in __seg_gs through %gs, whose
// base arch_prctl sets to a window over them and restores to 0. Indirection, subscripts, members, compound assignments,
// increments and decrements, of integers, floating values, long double, __int128 and vectors, at -O0 (Tier 0) and
// through Tier 1's fallback to Tier 0.
static void check_test_program_segments(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        "struct S{long a;int b;char c[8];};"
        "static _Thread_local long tl[4]={1,2,3,4};static _Thread_local struct S ts={5,6,\"abcdefg\"};"
        "static _Thread_local double td=2.5;static _Thread_local long double tq=1.75L;static _Thread_local __int128 ti=77;"
        "static long rd(long __seg_fs*p,int i){return p[i];}static void wr(long __seg_fs*p,int i,long v){p[i]=v;}"
        "static int member(struct S __seg_fs*s,int n){s->b+=n;s->c[n&7]=(char)('A'+n);return s->b+s->c[(n+1)&7]+(int)s->a;}"
        "int tested(int n){char*tp=__builtin_thread_pointer();"
        "long __seg_fs*p=(long __seg_fs*)((char*)tl-tp);struct S __seg_fs*s=(struct S __seg_fs*)((char*)&ts-tp);"
        "double __seg_fs*d=(double __seg_fs*)((char*)&td-tp);long double __seg_fs*q=(long double __seg_fs*)((char*)&tq-tp);"
        "__int128 __seg_fs*w=(__int128 __seg_fs*)((char*)&ti-tp);"
        "wr(p,n&3,n*7L);*p+=n;p[1]++;--p[2];long r=rd(p,(n+1)&3)+p[1]*3+(p[3]<<=1);"
        "r+=member(s,n);*d+=n;*d*=1.5;*q-=n;*w+=n;*w<<=3;"
        "return (int)(r+tl[0]+tl[1]+tl[2]+tl[3]+ts.b+(long)td+(long)tq+(long)ti+(long)(ti>>64));}",
        "segment-thread-locals", clang);
    check_test_program_source(test,
        "typedef int v4si __attribute__((vector_size(16)));static _Thread_local v4si tv={9,8,7,6};"
        "int tested(int n){char*tp=__builtin_thread_pointer();v4si __seg_fs*p=(v4si __seg_fs*)((char*)&tv-tp);"
        "v4si v=*p;v4si k={n,n+1,n+2,n+3};*p=v*k;*p+=k;v=*p;return v[0]+v[1]*3+v[2]*5+v[3]*7+tv[n&3];}",
        "segment-vectors", clang);
    check_test_program_source(test,
        "long syscall(long,...);__seg_gs int gs_count;__seg_gs long gs_wide[4];__seg_gs double gs_d;"
        "static char window[1<<16] __attribute__((aligned(64)));"
        "int tested(int n){unsigned long a=(unsigned long)&gs_count,b=(unsigned long)gs_wide,c=(unsigned long)&gs_d;"
        "unsigned long lo=a<b?a:b;lo=lo<c?lo:c;unsigned long hi=a>b?a:b;hi=hi>c?hi:c;"
        "if(hi-lo+64>sizeof window)return -1;"
        "for(unsigned i=0;i<sizeof window;i++)window[i]=(char)i;"
        "if(syscall(158,0x1001,(unsigned long)window-lo)!=0)return -2;"
        "gs_count=n;gs_count++;gs_count*=3;gs_wide[n&3]=gs_count;gs_wide[0]+=gs_wide[1];gs_d=n*0.5;gs_d+=gs_count;"
        "long r=gs_count+gs_wide[0]+gs_wide[n&3]+(long)gs_d+--gs_count;"
        "if(syscall(158,0x1001,0UL)!=0)return -3;"
        "return (int)r+window[a-lo]+window[b-lo+8];}",
        "segment-gs-globals", clang);
    // The atomic builtins a direct plan takes (#1059): loads and stores (a seq_cst store's xchg), lock xadd, xchg,
    // lock cmpxchg, a bitwise or a minimum's loop of a LOAD and lock cmpxchg, __sync's, C11's, test-and-set's xchg.
    check_test_program_source(test,
        "static _Thread_local int ta=5;static _Thread_local long tb=100;static _Thread_local short tc=3;static _Thread_local char td;"
        "static _Thread_local _Atomic int te=11;"
        "int tested(int n){char*tp=__builtin_thread_pointer();int __seg_fs*a=(int __seg_fs*)((char*)&ta-tp);"
        "long __seg_fs*b=(long __seg_fs*)((char*)&tb-tp);short __seg_fs*c=(short __seg_fs*)((char*)&tc-tp);"
        "char __seg_fs*d=(char __seg_fs*)((char*)&td-tp);_Atomic int __seg_fs*e=(_Atomic int __seg_fs*)((char*)&te-tp);"
        "int r=__atomic_load_n(a,5);__atomic_store_n(a,r+n,5);__atomic_store_n(a,__atomic_load_n(a,0)+1,3);"
        "r+=__atomic_fetch_add(a,3,5)+__atomic_sub_fetch(a,1,2)+__atomic_exchange_n(a,7,4);int x=7;"
        "r+=__atomic_compare_exchange_n(a,&x,n,0,5,5)+x;x=-1;r+=__atomic_compare_exchange_n(a,&x,2,1,2,0)+x;"
        "r+=(int)__atomic_fetch_or(b,n,5)+(int)__atomic_and_fetch(b,~1L,0)+(int)__atomic_fetch_xor(b,6,5)+(int)__atomic_fetch_nand(b,3,5);"
        "r+=(int)__sync_fetch_and_sub(b,1)+(int)__sync_val_compare_and_swap(b,tb,9)+__sync_bool_compare_and_swap(b,9,n)+(int)__sync_lock_test_and_set(b,4);"
        "r+=__atomic_fetch_max(c,(short)n,5)+__atomic_fetch_min(c,(short)-n,5)+__atomic_exchange_n(c,(short)(n*3),5);"
        "r+=__atomic_test_and_set(d,5)+__atomic_test_and_set(d,5);"
        "r+=__c11_atomic_load(e,5)+__c11_atomic_fetch_add(e,n,5);__c11_atomic_store(e,4,5);"
        "return r+ta+(int)tb+tc+td+te;}",
        "segment-atomics", clang);
    // Bit-fields: their unit's pieces read and written through %fs (check_lower_bit_unit, check_lower_bit_store), a unit of
    // 1, 2, 4 and 8 bytes and one of 3 bytes' pieces, signed and unsigned, by assignments, updates and increments.
    check_test_program_source(test,
        "struct B{unsigned a:3;int b:5;unsigned c:12;long d:33;unsigned char e:2;};"
        "struct __attribute__((packed)) P{char x;unsigned y:20;};"
        "static _Thread_local struct B tb={5,-3,100,-7,1};static _Thread_local struct P tp_={1,0x12345};"
        "int tested(int n){char*tp=__builtin_thread_pointer();struct B __seg_fs*b=(struct B __seg_fs*)((char*)&tb-tp);"
        "struct P __seg_fs*p=(struct P __seg_fs*)((char*)&tp_-tp);"
        "int r=b->a+b->b+b->c+(int)b->d+b->e+p->y;b->a=n;b->b+=n;b->c++;--b->d;b->e=b->a;r+=b->b++;"
        "p->y^=n*977;r+=p->y+(p->y=n);"
        "return r+tb.a+tb.b+tb.c+(int)tb.d+tb.e+(int)tp_.y+tp_.x;}",
        "segment-bit-fields", clang);
    // A vector's elements through the vector's override (check_lower_element_segment), which their own type has lost: read,
    // assigned, compound-assigned, incremented and decremented, of ints, floats and shorts, at constant and variable indices,
    // through `(*p)[i]` and `p[0][i]`.
    check_test_program_source(test,
        "typedef int v4si __attribute__((vector_size(16)));typedef float v8sf __attribute__((vector_size(32)));"
        "typedef short v8hi __attribute__((vector_size(16)));"
        "static _Thread_local v4si tv={9,8,7,6};static _Thread_local v8sf tf={1,2,3,4,5,6,7,8};static _Thread_local v8hi th={1,-2,3,-4,5,-6,7,-8};"
        "int tested(int n){char*tp=__builtin_thread_pointer();v4si __seg_fs*v=(v4si __seg_fs*)((char*)&tv-tp);"
        "v8sf __seg_fs*f=(v8sf __seg_fs*)((char*)&tf-tp);v8hi __seg_fs*h=(v8hi __seg_fs*)((char*)&th-tp);"
        "long r=(*v)[n&3]+(*v)[0];(*v)[(n+1)&3]=n*5;(*v)[2]+=n;(*v)[3]++;r+=--(*v)[1];r+=(*v)[0]--;"
        "(*f)[n&7]*=1.5f;(*f)[0]=(*f)[7]+n;r+=(long)(*f)[n&7]+(long)(*f)[0];"
        "(*h)[n&7]-=n;r+=(*h)[n&7]+(*h)[(n*3)&7];v[0][1]^=7;r+=v[0][1];"
        "return (int)(r+tv[0]+tv[1]+tv[2]+tv[3]+(long)tf[n&7]+th[n&7]);}",
        "segment-vector-elements", clang);
    // Half-precision values, whose loads and stores are a U16's (c_check_program_half.c): _Float16's and __bf16's, read,
    // assigned, compound-assigned, incremented and decremented, through %fs.
    check_test_program_source(test,
        "static _Thread_local _Float16 th[4]={1.5f16,2.25f16,-3.0f16,0.5f16};static _Thread_local __bf16 tb[2]={1.0f,2.5f};"
        "int tested(int n){char*tp=__builtin_thread_pointer();_Float16 __seg_fs*h=(_Float16 __seg_fs*)((char*)th-tp);"
        "__bf16 __seg_fs*b=(__bf16 __seg_fs*)((char*)tb-tp);"
        "float r=h[n&3]+(float)*h;h[1]=(_Float16)(n*0.5f);h[2]+=h[1];h[3]*=2;h[0]++;--h[1];r+=h[0]+h[1]+h[2]+h[3];"
        "b[0]=b[1]+n;b[1]-=1.0f;r+=(float)b[0]+(float)b[1];"
        "return (int)(4*(r+th[0]+th[1]+th[2]+th[3]+(float)tb[0]+(float)tb[1]));}",
        "segment-half-precision", clang);
    // %ss's space (258), whose base 64-bit mode ignores, so that a flat address cast to it reaches its object: clang's 36 on
    // each access, of integers, floating values, long double, __int128, vectors and their elements, members and bit-fields,
    // the direct atomic plans, and globals in the space (RIP-relative, `ss:x(%rip)`).
    check_test_program_source(test,
        "#define SS __attribute__((address_space(258)))\n"
        "typedef int v4si __attribute__((vector_size(16)));struct S{long a;int b;char c[8];unsigned u:5;int s:11;};"
        "static SS long gl[4]={1,2,3,4};static SS struct S gs_={5,6,\"abcdefg\",3,-4};static SS double gd=2.5;"
        "static long tl[4]={9,8,7,6};static long double tq=1.75L;static __int128 ti=77;static v4si tv={4,5,6,7};"
        "static _Float16 th=1.5f16;static int ta=5;static short tc=3;"
        "int tested(int n){long SS*p=(long SS*)tl;long double SS*q=(long double SS*)&tq;__int128 SS*w=(__int128 SS*)&ti;"
        "v4si SS*v=(v4si SS*)&tv;_Float16 SS*h=(_Float16 SS*)&th;int SS*a=(int SS*)&ta;short SS*c=(short SS*)&tc;"
        "p[n&3]=n*7L;*p+=n;p[1]++;--p[2];long r=p[(n+1)&3]+(p[3]<<=1);gl[n&3]+=gl[0];gd*=1.5;"
        "gs_.b+=n;gs_.c[n&7]=(char)('A'+n);gs_.u=n;gs_.s-=n;r+=gl[n&3]+gs_.b+gs_.c[(n+1)&7]+gs_.u+gs_.s+(long)gd;"
        "*q-=n;*w+=n;*w<<=3;v4si k={n,1,2,3};*v+=k;(*v)[n&3]*=3;r+=(*v)[0]+(*v)[(n+1)&3];*h+=(_Float16)n;r+=(long)(float)*h;"
        "r+=__atomic_fetch_add(a,3,5)+__atomic_exchange_n(a,n,5)+__atomic_load_n(a,2);int x=n;"
        "r+=__atomic_compare_exchange_n(a,&x,9,0,5,5)+__atomic_fetch_max(c,(short)n,5)+__sync_fetch_and_or(c,6);"
        "return (int)(r+tl[0]+tl[1]+tl[2]+tl[3]+(long)tq+(long)ti+tv[n&3]+(long)(float)th+ta+tc);}",
        "segment-ss", clang);
    // Aggregates (#1447): a read of a struct's, a union's or a complex value's object in a segment is a copy of its bytes
    // through the override to a frame object (check_lower_segment_read), whose address its consumers take (a copy, a call's
    // argument, a register return, a conditional's forwarding); a struct's assignment into one copies through it
    // (check_lower_copy_segmented), and its value is the stored bytes. Sizes of 8, 13 and 24 bytes, in %fs and %ss.
    check_test_program_source(test,
        "#define SS __attribute__((address_space(258)))\n"
        "struct S{long a,b,c;};struct P{int x,y;};struct O{char c[13];};union U{double d;char c[12];};"
        "static struct S gs_={1,2,3};static struct P gp={4,5};static struct O go={\"abcdefghijkl\"};static union U gu={.c=\"uvwxyzabcde\"};"
        "static _Thread_local struct S ts={7,8,9};static _Thread_local _Complex double tc=1.5+2.5i;static _Complex float gc=3.0f-1.0if;"
        "static long sum(struct S s){return s.a*100+s.b*10+s.c;}static int psum(struct P p){return p.x*7+p.y;}"
        "static struct S cp(struct S SS*p){return *p;}static struct P cpp(struct P SS*p){return *p;}static void st(struct S SS*p,struct S s){*p=s;}"
        "int tested(int n){char*tp=__builtin_thread_pointer();struct S SS*ps=(struct S SS*)&gs_;struct P SS*pp=(struct P SS*)&gp;"
        "struct O SS*po=(struct O SS*)&go;union U SS*pu=(union U SS*)&gu;struct S __seg_fs*fs=(struct S __seg_fs*)((char*)&ts-tp);"
        "_Complex double __seg_fs*fc=(_Complex double __seg_fs*)((char*)&tc-tp);_Complex float SS*sc=(_Complex float SS*)&gc;"
        "long r=sum(*ps)+psum(*pp)+cp(ps).b+cpp(pp).y;struct S t=*fs;r+=t.a+t.c;struct S u={11,12,n};st(ps,u);*fs=u;fs->b=99;"
        "struct O o=*po;r+=o.c[n&7]+o.c[12];struct O o2={\"zyxwvutsrqpo\"};*po=o2;union U v=*pu;r+=v.c[n&3];"
        "struct S w;w=(*ps=t);r+=w.a;_Complex double z=*fc;r+=(long)(__real__ z*10)+(long)(__imag__ z*10);"
        "_Complex float zf=*sc*2.0f;r+=(long)__real__ zf+(long)__imag__ zf;r+=sum(*fs)+(n?*ps:*fs).b;"
        "return (int)(r+gs_.a+gs_.b+gs_.c+ts.b+ts.c+gp.x+go.c[0]+go.c[11]+gu.c[1]);}",
        "segment-aggregates", clang);
    // Complex values, __float128s and _BitInts (#1447): a complex value's store (a copy through the override), its updates
    // and a real object's update by a complex value (check_lower_segment_update: the object copied to a frame object,
    // updated there and copied back), its parts as lvalues (check_lower_element_segment: `__real__ *p` read, stored and
    // updated), a __float128's and a wide _BitInt's updates the same way, and a narrow _BitInt's read, store and update,
    // whose LOAD and STORE take the override as an integer's do. In %fs and %ss.
    check_test_program_source(test,
        "#define SS __attribute__((address_space(258)))\n"
        "static _Thread_local _Complex double tc=1.5+2.5i;static _Thread_local _BitInt(40) tb=-12345;static _Thread_local unsigned _BitInt(200) tw;"
        "static _Complex float gc=3.0f-1.0if;static _BitInt(13) gb=100;static __float128 gq=2.5;static _BitInt(130) gw;static double gd=4.0;"
        "int tested(int n){char*tp=__builtin_thread_pointer();tw=77+n;gw=-5-n;"
        "_Complex double __seg_fs*fc=(_Complex double __seg_fs*)((char*)&tc-tp);_BitInt(40) __seg_fs*fb=(_BitInt(40) __seg_fs*)((char*)&tb-tp);"
        "unsigned _BitInt(200) __seg_fs*fw=(unsigned _BitInt(200) __seg_fs*)((char*)&tw-tp);_Complex float SS*sc=(_Complex float SS*)&gc;"
        "_BitInt(13) SS*sb=(_BitInt(13) SS*)&gb;__float128 SS*sq=(__float128 SS*)&gq;_BitInt(130) SS*sw=(_BitInt(130) SS*)&gw;double SS*sd=(double SS*)&gd;"
        "long r=0;*fc=3.0-4.0i;*fc+=n;*fc*=2.0i;_Complex double z=(*fc)++;r+=(long)__real__ z+(long)__imag__ *fc*3;"
        "*sc=*sc/2.0f;*sc-=1.0if;r+=(long)(__real__ *sc*10)+(long)(__imag__ *sc*10);__real__ *fc=n;__imag__ *sc+=1.5f;--__imag__ *fc;"
        "r+=(long)__real__ *fc+(long)__imag__ *fc+(long)(__imag__ *sc*4);"
        "*fb=*fb*3+n;*fb+=100;(*fb)--;r+=(long)*fb;*sb=4000;*sb+=200;++*sb;r+=(long)*sb+(*sb>>2);"
        "*fw=*fw*1000000007;*fw+=5;++*fw;r+=(long)(*fw%1000003);*sw=*sw*3;*sw-=1000;(*sw)++;r+=(long)*sw;"
        "*sq=*sq*2;*sq+=n;(*sq)++;r+=(long)*sq;*sd+=1.0i;r+=(long)*sd;"
        "return (int)(r+(long)__real__ tc+(long)(__imag__ gc*4)+(long)tb+(long)gb+(long)(tw%1000)+(long)gw+(long)gq);}",
        "segment-complex-bit-int", clang);
    // _Atomic lvalues (#1447): their plans' direct accesses take the override (CheckLowerAtomic.segment), as clang's code has
    // it: a load's mov, a store's xchg, an increment's or an addition's lock xadd, another update's loop of a load and lock
    // cmpxchg (a double's too), of 1, 2, 4 and 8 bytes; a struct's library calls (__atomic_load, __atomic_store) take its
    // address as it is, as clang's do, which in %ss (whose base is 0) is the object's.
    check_test_program_source(test,
        "#define SS __attribute__((address_space(258)))\n"
        "struct B{long a,b,c;};static _Thread_local _Atomic int ta=5;static _Thread_local _Atomic long tl=100;static _Thread_local _Atomic double td=1.5;"
        "static _Atomic short gs_=7;static _Atomic struct B gb;static _Atomic char gc=3;"
        "int tested(int n){char*tp=__builtin_thread_pointer();{struct B i0={1,2,n};gb=i0;}"
        "_Atomic int __seg_fs*a=(_Atomic int __seg_fs*)((char*)&ta-tp);_Atomic long __seg_fs*l=(_Atomic long __seg_fs*)((char*)&tl-tp);"
        "_Atomic double __seg_fs*d=(_Atomic double __seg_fs*)((char*)&td-tp);_Atomic short SS*s=(_Atomic short SS*)&gs_;"
        "_Atomic struct B SS*b=(_Atomic struct B SS*)&gb;_Atomic char SS*c=(_Atomic char SS*)&gc;"
        "long r=*a;*a=n;r+=++*a;r+=*a+=3;r+=(*a)--;*l*=3;r+=*l;r+=*l|=5;r+=*l<<=1;*d+=1.25;r+=(long)(*d*4);"
        "*s-=2;r+=*s;r+=++*s;*c='x';r+=*c;(*c)++;struct B t=*b;r+=t.a+t.c;struct B u={9,8,7};*b=u;"
        "struct B v=gb;return (int)(r+ta+tl+(long)td+gs_+v.b+gc);}",
        "segment-atomic-lvalues", clang);
    // A vector of bool's elements (#1447): the byte that holds each bit read, and read and written for an assignment,
    // through the vector's override (check_lower_transport_bit_read, check_lower_transport_bit_store), of 8 and 64 bits, in
    // %fs and %ss.
    check_test_program_source(test,
        "#define SS __attribute__((address_space(258)))\n"
        "typedef _Bool B8 __attribute__((ext_vector_type(8)));typedef _Bool B64 __attribute__((ext_vector_type(64)));"
        "static _Thread_local B8 tb={1,0,1,1,0,0,1,0};static B64 gb;"
        "int tested(int n){B8 __seg_fs*p=(B8 __seg_fs*)((char*)&tb-(char*)__builtin_thread_pointer());B64 SS*q=(B64 SS*)&gb;int r=0;"
        "for(int i=0;i<8;i++)r=r*2+(*p)[i];(*p)[1]=n&1;(*p)[2]=0;(*q)[40]=1;(*q)[n&63]=(*p)[3];"
        "for(int i=0;i<8;i++)r=r*3+(*p)[i];r+=(*q)[40]*5+(*q)[n&63]*7+(*q)[0];return r+tb[1]+gb[40];}",
        "segment-bool-vectors", clang);
}

// Initializer validation keeps atomic rvalue identity while storage uses the underlying vector bytes.
static void check_test_program_atomic_vector_constraints(CheckTest* test, const char* clang)
{
    static const CheckSourceFixture fixtures[] = {
        {
            "atomic vector initializers keep the declared destination",
            "typedef int V __attribute__((vector_size(16)));_Atomic(V) make(V);"
            "int f(void){_Atomic(V) a=(V){1,2,3,4};V b=a;_Atomic(V) c=make(b);V d=c;return b[0]+d[0];}",
            false,
            false,
        },
        {
            "atomic vector rvalues do not initialize plain vectors",
            "typedef int V __attribute__((vector_size(16)));_Atomic(V) make(V);"
            "V f(void){V x=make((V){1,2,3,4});return x;}",
            true,
            false,
        },
    };
    char directory[] = "/tmp/buster-atomic-vector-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    check_test_expect(test, created, "atomic vector constraint directory");
    const char* arguments[] = { "-std=gnu17" };
    for (uint32_t i = 0; created && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char path[128];
        snprintf(path, sizeof(path), "%s/%u.c", directory, i);
        char* paths[] = { path };
        ParseTestClang oracle = { 0 };
        const bool ready = parse_test_write(path, fixtures[i].source, strlen(fixtures[i].source)) &&
                           parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &oracle);
        check_test_expect(test, ready, "atomic vector constraint oracle");
        if (ready)
        {
            check_test_source(test, fixtures + i, path, &oracle);
        }
        unlink(path);
    }
    if (created)
    {
        rmdir(directory);
    }
}

// Differential baseline coverage: exact source and -march shared with Clang, both lowering and Tier 0 paths.
static void check_test_program_vector_baselines(CheckTest* test, const char* clang)
{
    static const char* const levels[] = { "x86-64", "x86-64-v2", "x86-64-v3" };
    static const char* const marches[] = { "-march=x86-64", "-march=x86-64-v2", "-march=x86-64-v3" };
    static const char* const programs[][2] = {
        { "baseline-integers",
          "typedef unsigned U __attribute__((vector_size(16)));typedef short H __attribute__((vector_size(16)));\n"
          "static v4si calc(v4si a,v4si b){return a*b+a-b;}static v4si global;\n"
          "int tested(int n){unsigned h=2166136261u;v4si a,b;U u,v;v16qu q,r;v8hu w,x;H k,l;v2di d,e;\n"
          "fill(&a,16,n);fill(&b,16,n+1);fill(&u,16,n+2);fill(&v,16,n+3);fill(&q,16,n+4);fill(&r,16,n+5);fill(&w,16,n+6);fill(&x,16,n+7);fill(&k,16,n+8);fill(&l,16,n+9);fill(&d,16,n+10);fill(&e,16,n+11);\n"
          "v4si z[]={calc(a,b),a+(int)n,-b,~a,a&b,a|b,a^b,a<b,a<=b,a>b,a>=b,a==b,a!=b};FOLD(z);\n"
          "U t[]={u+v,u-v,u*v,u<v,u<=v,u>v,u>=v,u==v,u!=v};FOLD(t);\n"
          "v16qu c[]={q+r,q-r,~q,(v16qu)(q<r),(v16qu)(q<=r),(v16qu)(q>r),(v16qu)(q>=r),(v16qu)(q==r),(v16qu)(q!=r),q+(unsigned char)n};FOLD(c);\n"
          "v8hu j[]={w+x,w*x,(v8hu)(w<x),(v8hu)(w<=x),(v8hu)(w>x),(v8hu)(w>=x),w+(unsigned short)n};FOLD(j);\n"
          "H m[]={k+l,k*l,k<l,k<=l,k>l,k>=l,k==l,k!=l};FOLD(m);v2di ll[]={d+e,d-e,-e,~d,d+(long long)n};FOLD(ll);\n"
          "global=calc(a,b);v4si mem[3]={a,b,{0}};mem[(unsigned)n%3]=global;v4si out=*mem;out[2]=n;mem[1]+=out;FOLD(mem);FOLD(global);return (int)h;}\n"
        },
        { "baseline-floating",
          "int tested(int n){unsigned h=2166136261u;v4sf a,b;v2df d,e;fillf((float*)&a,4,n);fillf((float*)&b,4,n+1);filld((double*)&d,2,n);filld((double*)&e,2,n+2);b+=1.0f;e+=1.0;\n"
          "v4sf r[]={a+b,a-b,a*b,a/b,-a,a*1.5f,2.5f-a};FOLD(r);v4si c[]={a<b,a<=b,a>b,a>=b,a==b,a!=b};FOLD(c);\n"
          "v2df z[]={d+e,d-e,d*e,d/e,-d,d*2.0};FOLD(z);v2di k[]={d<e,d<=e,d>e,d>=e,d==e,d!=e};FOLD(k);\n"
          "union{unsigned u[4];v4sf f;}q={{0x7fc01234u,0x80000000u,0,0x3f800000u}},p={{0x3f800000u,0,0x80000000u,0xffc05678u}};\n"
          "v4si cmp[]={q.f<p.f,q.f<=p.f,q.f>p.f,q.f>=p.f,q.f==p.f,q.f!=p.f};FOLD(cmp);return (int)h;}\n"
        },
        { "baseline-narrow-and-wide-transport",
          "typedef int V __attribute__((vector_size(8)));typedef unsigned char C __attribute__((vector_size(4)));\n"
          "static v16su hold;static void wide(v16su a){v16su b=a;hold=b;}static V narrow(V a){return a+3;}\n"
          "static v16su back(v16su a){return a;}static v8sf half(v8sf a){return a;}static v8df pair(v8df a,v16su b){hold=b;return a;}\n"
          "int tested(int n){unsigned h=2166136261u;struct S{C v;unsigned guard;} s={{1,2,3,(unsigned char)n},0xdeadbeef};\n"
          "C t=s.v+2;s.v=t;FOLD(s);V a={n,-n},b=narrow(a);V out[2]={a,b};out[0]=out[1];FOLD(out);\n"
          "v16su x;fill(&x,64,n);wide(x);v16su z=hold;FOLD(z);v16su y=back(x);FOLD(y);v8sf f;fillf((float*)&f,8,n);v8sf g=half(f);FOLD(g);\n"
          "v8df d;fill(&d,64,n+3);v8df e=pair(d,y);FOLD(e);FOLD(hold);return (int)h;}\n"
        },
        { "baseline-transport",
          "typedef char t2 __attribute__((vector_size(2)));typedef _Bool b8 __attribute__((ext_vector_type(8)));typedef _Bool b64 __attribute__((ext_vector_type(64)));\n"
          "typedef int w32 __attribute__((vector_size(128)));static t2 pick2(t2 a,t2 b,int k){return k?a:b;}static b64 back64(b8 a,b64 b,b8*out){*out=a;return b;}\n"
          "static w32 backw(w32 a,int k){w32 r=a;r[k&31]=k;return r;}\n"
          "int tested(int n){unsigned h=2166136261u;t2 a={(char)n,2},b;fill(&b,2,n);t2 c=pick2(a,b,n&1);FOLD(c);b8 m={1,0,(_Bool)(n&1),1,0,1,0,0},k;\n"
          "b64 r;fill(&r,8,n+3);b64 s=back64(m,r,&k);FOLD(k);FOLD(s);w32 w;fill(&w,128,n+4);w32 x=backw(w,n);FOLD(x);return (int)h;}\n"
        },
        { "baseline-transport-compute", check_test_vector_transport },
        { "baseline-logical-and-conditions", check_test_vector_logical },
        { "baseline-boolean-builtins", check_test_vector_boolean_builtins },
        { "baseline-transport-builtins",
          "typedef unsigned char B1 __attribute__((vector_size(1)));typedef unsigned char B2 __attribute__((vector_size(2)));"
          "typedef short H1 __attribute__((vector_size(2)));typedef unsigned char B128 __attribute__((vector_size(128)));"
          "typedef short H64 __attribute__((vector_size(128)));typedef float F32 __attribute__((vector_size(128)));"
          "typedef double D16 __attribute__((vector_size(128)));typedef float F31 __attribute__((ext_vector_type(31)));"
          "int tested(int n){unsigned h=2166136261u;B1 b1={(unsigned char)n},c1={73};B2 b2={(unsigned char)n,219},c2={87,36};H1 w1={(short)n},x1={-97};"
          "B1 r1[]={__builtin_elementwise_max(b1,c1),__builtin_elementwise_add_sat(b1,c1)};"
          "B2 r2[]={__builtin_elementwise_min(b2,c2),__builtin_elementwise_sub_sat(b2,c2)};"
          "H1 t1[]={__builtin_elementwise_min(w1,x1),__builtin_elementwise_add_sat(w1,x1)};FOLD(r1);FOLD(r2);FOLD(t1);"
          "B128 b,c;H64 w,x;fill(&b,128,n);fill(&c,128,n+1);fill(&w,128,n+2);fill(&x,128,n+3);"
          "B128 br[]={__builtin_elementwise_min(b,c),__builtin_elementwise_max(b,c),__builtin_elementwise_add_sat(b,c)};"
          "H64 wr[]={__builtin_elementwise_min(w,x),__builtin_elementwise_max(w,x),__builtin_elementwise_sub_sat(w,x)};FOLD(br);FOLD(wr);"
          "F32 f,g;D16 d,e;F31 p;for(int i=0;i<32;i++){f[i]=(float)(i+n)/17.0f;g[i]=(float)(i%3-1);if(i<31)p[i]=f[i];"
          "if(i<16){d[i]=(double)(i+n)/23.0;e[i]=(double)(i%3-1);}}"
          "F32 fr[]={__builtin_elementwise_sin(f),__builtin_elementwise_pow(f,g)};D16 dr[]={__builtin_elementwise_sin(d),__builtin_elementwise_pow(d,e)};"
          "FOLD(fr);FOLD(dr);float fs=__builtin_reduce_in_order_fadd(f,-0.0f),ps=__builtin_reduce_in_order_fadd(p,1.25f);"
          "double ds=__builtin_reduce_in_order_fadd(d,-0.0);FOLD(fs);FOLD(ps);FOLD(ds);return (int)h;}"
        },
        // Three doubles' and three longs' extended vectors, whose elements LLVM returns a register each at 16 bytes (xmm0,
        // xmm1 and st0; rax, rdx and rcx: tier0_element_parts, #137), wider vectors' registers at the other levels.
        { "baseline-element-results",
          "typedef double d3 __attribute__((ext_vector_type(3)));typedef long l3 __attribute__((ext_vector_type(3)));\n"
          "static d3 pick(d3 a,d3 b,int k){return k?a:b;}static l3 back(l3 a){l3 r=a;return r;}\n"
          "int tested(int n){unsigned h=2166136261u;double v[4]={1.5*n,-2.25,3.125,0};d3 a,b;__builtin_memcpy(&a,v,32);b=a;b[2]=(double)n;\n"
          "long double guard=n;for(int i=0;i<3;i++){d3 c=pick(a,b,(n+i)&1);__builtin_memcpy(v,&c,24);FOLD(v);}\n"
          "long w[4]={7,-8,n,0};l3 d;__builtin_memcpy(&d,w,32);l3 e=back(d);__builtin_memcpy(w,&e,24);FOLD(w);guard*=2;double g=(double)guard;FOLD(g);return (int)h;}\n"
        },
        // #1193: vectors of 32 and 64 bytes returned split across the target's registers (xmm0 and xmm1, xmm0 to xmm3,
        // ymm0 and ymm1) or whole, by the program's functions, by the harness's (clang's) to the program and by the
        // program's to the harness through pointers.
        { "baseline-wide-results",
          "typedef int W8 __attribute__((vector_size(32)));typedef double W16 __attribute__((vector_size(64)));typedef unsigned char W64 __attribute__((vector_size(64)));\n"
          "W8 external_wide8(W8,int);W16 external_wide16(W16,int);int external_apply_wide8(W8(*)(W8,int),int);int external_apply_wide16(W16(*)(W16,int),int);\n"
          "static W8 own8(W8 a,int k){a[k&7]=k*5;W8 b=a;return b;}static W16 own16(W16 a,int k){a[k&7]=k*0.5;return a;}\n"
          "static W64 bytes64(int n){W64 v;fill(&v,64,n);v[(unsigned)n%64]=7;return v;}static W8 chain8(W8 a,int k){return own8(external_wide8(a,k),k+1);}\n"
          "int tested(int n){unsigned h=2166136261u;W8 a;W16 d;fill(&a,32,n);filld((double*)&d,8,n);\n"
          "W8 r[]={own8(a,n),external_wide8(a,n),chain8(a,n)};FOLD(r);W16 s[]={own16(d,n),external_wide16(d,n),own16(external_wide16(d,n+1),n)};FOLD(s);\n"
          "W64 b=bytes64(n);FOLD(b);int e[]={external_apply_wide8(own8,n),external_apply_wide16(own16,n),external_apply_wide8(external_wide8,n)};FOLD(e);return (int)h;}\n"
        },
        // #1193: operators on vectors wider than the target's registers, split into pieces they hold as LLVM splits them:
        // integers of each element size, unsigned compares, splats, negations (a float's through its sign bit) and floats.
        { "baseline-wide-operators",
          "typedef long long v8di __attribute__((vector_size(64)));\n"
          "int tested(int n){unsigned h=2166136261u;v8si a,b;v16su u,v;v8sf f,g;v8df d,e;v64qi c,q;v16hi k,l;v4di x,y;\n"
          "fill(&a,32,n);fill(&b,32,n+1);fill(&u,64,n+2);fill(&v,64,n+3);fillf((float*)&f,8,n);fillf((float*)&g,8,n+1);filld((double*)&d,8,n);filld((double*)&e,8,n+2);\n"
          "fill(&c,64,n+4);fill(&q,64,n+5);fill(&k,32,n+6);fill(&l,32,n+7);fill(&x,32,n+8);fill(&y,32,n+9);g+=1.0f;e+=1.0;\n"
          "v8si r[]={a+b,a-b,a*b,a&b,a|b,a^b,~a,-b,a<b,a==b,a>=b,a+n};FOLD(r);v16su t[]={u+v,u-v,u*v,u<v,u>=v,u!=v,u+(unsigned)n,~u};FOLD(t);\n"
          "v8sf w[]={f+g,f-g,f*g,f/g,-f,f+2.5f};FOLD(w);v8si fc[]={f<g,f>=g,f==g,f!=g};FOLD(fc);v8df z[]={d+e,d-e,d*e,d/e,-d,d*1.5};FOLD(z);v8di dc[]={d<e,d>=e};FOLD(dc);\n"
          "v64qi m[]={c+q,c-q,c<q,c==q,~c,c+(signed char)n};FOLD(m);v16hi o[]={k+l,k*l,k<l,-k};FOLD(o);v4di p[]={x+y,x-y,-x,x^y,x+(long long)n};FOLD(p);\n"
          "v16su mem[2]={u,v};mem[(unsigned)n&1]+=u;FOLD(mem);return (int)h;}\n"
        },
        { "baseline-floating-pressure",
          "static double consume(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j,double k,double l,double m,double o,double p,double q,v4si v){return a+b+c+d+e+f+g+h+i+j+k+l+m+o+p+q+v[1];}\n"
          "int tested(int n){v4si a={n,1,2,3},b={4,n,5,6};return (int)consume(n,n+1,n+2,n+3,n+4,n+5,n+6,n+7,n+8,n+9,n+10,n+11,n+12,n+13,n+14,n+15,a*b+a);}\n"
        },
        { "baseline-sse-builtins",
          "#include <emmintrin.h>\n"
          "int tested(int n){unsigned h=2166136261u;__m128i a,b;__m128 f,g;fill(&a,16,n);fill(&b,16,n+1);fill(&f,16,n+2);fill(&g,16,n+3);\n"
          "__m128i r[]={_mm_add_epi32(a,b),_mm_shuffle_epi32(a,0x1b),_mm_shufflehi_epi16(a,0x4e),_mm_shufflelo_epi16(b,0xb1),_mm_slli_si128(a,3),_mm_srli_si128(a,15),_mm_srli_si128(b,16),_mm_packs_epi32(a,b),_mm_packus_epi16(a,b),_mm_madd_epi16(a,b),_mm_mulhi_epi16(a,b),_mm_mul_epu32(a,b),_mm_insert_epi16(a,n*31,6),_mm_cvtps_epi32(f),_mm_slli_epi16(a,n&15),_mm_srli_epi16(b,n&15),_mm_srai_epi16(a,n&15),_mm_slli_epi32(a,n&31),_mm_srli_epi32(b,n&31),_mm_srai_epi32(a,n&31),_mm_slli_epi64(a,n&63),_mm_srli_epi64(b,n&63)};FOLD(r);\n"
          "__m128 m[]={_mm_shuffle_ps(f,g,0x1b),_mm_max_ps(f,g),_mm_min_ps(f,g)};FOLD(m);int e[]={_mm_extract_epi16(a,5),_mm_extract_epi16(b,0)};FOLD(e);\n"
          // #1193: the sign masks and the scalar conversions to integers, of NaNs and values past int's range too.
          "__m128d c=_mm_castps_pd(f),k=_mm_castps_pd(g);int s[]={_mm_movemask_epi8(a),_mm_movemask_ps(f),_mm_movemask_pd(c),_mm_cvttss_si32(f),_mm_cvtss_si32(g),"
          "_mm_cvttsd_si32(c),_mm_cvtsd_si32(k)};FOLD(s);long long l[]={_mm_cvttss_si64(f),_mm_cvtss_si64(g),_mm_cvttsd_si64(k),_mm_cvtsd_si64(c)};FOLD(l);return (int)h;}\n"
        },
        // The rest of SSE and SSE2's intrinsics (#775's survey): averages, sums of differences, minima and maxima,
        // saturating arithmetic, unpacks, conversions, square roots, approximations, scalar operations, compares into
        // vectors and into flags (a NaN among their operands), moves of halves and non-temporal stores.
        { "baseline-sse2-survey",
          "#include <emmintrin.h>\n"
          "int tested(int n){unsigned h=2166136261u;__m128i a,b;__m128 f,g;__m128d d,e;fill(&a,16,n);fill(&b,16,n+1);fillf((float*)&f,4,n+2);fillf((float*)&g,4,n+3);"
          "filld((double*)&d,2,n+4);filld((double*)&e,2,n+5);union{unsigned u[4];__m128 v;}nan={{0x7fc01234u,0,0x80000000u,0x3f800000u}};\n"
          "__m128i r[]={_mm_avg_epu8(a,b),_mm_avg_epu16(a,b),_mm_sad_epu8(a,b),_mm_min_epu8(a,b),_mm_max_epu8(a,b),_mm_min_epi16(a,b),_mm_max_epi16(a,b),"
          "_mm_adds_epi8(a,b),_mm_adds_epu8(a,b),_mm_adds_epi16(a,b),_mm_adds_epu16(a,b),_mm_subs_epi8(a,b),_mm_subs_epu8(a,b),_mm_subs_epi16(a,b),_mm_subs_epu16(a,b),"
          "_mm_unpacklo_epi8(a,b),_mm_unpackhi_epi8(a,b),_mm_unpacklo_epi16(a,b),_mm_unpackhi_epi16(b,a),_mm_unpacklo_epi32(a,b),_mm_unpackhi_epi32(a,b),"
          "_mm_unpacklo_epi64(a,b),_mm_unpackhi_epi64(b,a),_mm_move_epi64(a),_mm_cvtpd_epi32(d),_mm_cvttpd_epi32(e),_mm_castps_si128(_mm_cmplt_ps(f,g)),"
          "_mm_castpd_si128(_mm_cmpneq_pd(d,e))};FOLD(r);\n"
          "__m128 s[]={_mm_sqrt_ps(_mm_mul_ps(f,f)),_mm_sqrt_ss(g),_mm_rcp_ps(g),_mm_rsqrt_ps(_mm_mul_ps(g,g)),_mm_max_ss(f,g),_mm_min_ss(f,nan.v),_mm_cvtpd_ps(d),"
          "_mm_cvtsd_ss(f,e),_mm_unpacklo_ps(f,g),_mm_unpackhi_ps(f,g),_mm_movehl_ps(f,g),_mm_movelh_ps(f,g),_mm_move_ss(f,g),_mm_cmpgt_ss(f,g),"
          "_mm_cmpord_ps(f,nan.v),_mm_cmpunord_ss(nan.v,g),_mm_shuffle_ps(f,g,0x4e)};FOLD(s);\n"
          "__m128d t[]={_mm_sqrt_pd(_mm_mul_pd(d,d)),_mm_sqrt_sd(d,_mm_mul_pd(e,e)),_mm_cvtps_pd(f),_mm_cvtepi32_pd(a),_mm_max_sd(d,e),_mm_min_sd(d,e),_mm_cvtss_sd(d,g),"
          "_mm_unpacklo_pd(d,e),_mm_unpackhi_pd(d,e),_mm_move_sd(d,e),_mm_cmple_sd(d,e),_mm_shuffle_pd(d,e,1)};FOLD(t);\n"
          "int c[]={_mm_comieq_ss(f,g),_mm_comilt_ss(f,g),_mm_comige_ss(g,f),_mm_ucomineq_ss(f,nan.v),_mm_comieq_ss(nan.v,nan.v),_mm_comile_sd(d,e),"
          "_mm_ucomigt_sd(e,d),_mm_cvtsi128_si32(_mm_srli_si128(a,4)),_mm_extract_epi16(b,7)};FOLD(c);\n"
          "__m64 m[2]={0};_mm_storeh_pi(&m[0],f);_mm_storel_pi(&m[1],g);FOLD(m);__m128 l[]={_mm_loadh_pi(f,&m[1]),_mm_loadl_pi(g,&m[0])};FOLD(l);"
          "_Alignas(16) float stream[4];_mm_stream_ps(stream,f);FOLD(stream);return (int)h;}\n"
        },
        // Shuffles of every element size by the baseline's sequences: PSHUFD's and SHUFPD's, SHUFPS's of halves, of pairs
        // and of three elements and one, unpacks, PSHUFLW's and PSHUFHW's, and with SSSE3 PSHUFB's of any bytes, without it
        // element by element (#1834); narrow results and operands. 32 bytes of bytes and words element by element without
        // AVX-512 (#1834).
        { "baseline-shuffles",
          "typedef short v8hi __attribute__((vector_size(16)));typedef char v16qi __attribute__((vector_size(16)));"
          "typedef float v2sf __attribute__((vector_size(8)));typedef int v2si __attribute__((vector_size(8)));\n"
          "int tested(int n){unsigned h=2166136261u;v4si a,b;v2di q,r;v4sf f,g;v2df d,e;fill(&a,16,n);fill(&b,16,n+1);fill(&q,16,n+2);fill(&r,16,n+3);"
          "fillf((float*)&f,4,n);fillf((float*)&g,4,n+1);filld((double*)&d,2,n);filld((double*)&e,2,n+1);v8hi w=(v8hi)a,x=(v8hi)b;v16qi y=(v16qi)a,z=(v16qi)b;\n"
          "v4si s[]={__builtin_shufflevector(a,b,3,2,1,0),__builtin_shufflevector(a,b,4,1,2,3),__builtin_shufflevector(a,b,0,1,2,7),__builtin_shufflevector(a,b,5,0,7,2),"
          "__builtin_shufflevector(a,b,6,7,0,1),__builtin_shufflevector(a,b,3,4,1,2),__builtin_shufflevector(a,b,1,6,3,0),__builtin_shufflevector(a,b,4,5,6,1),"
          "__builtin_shufflevector(a,b,7,6,5,4),__builtin_shufflevector(a,b,4,4,4,0),__builtin_shufflevector(a,b,1,2,3,4),__builtin_shufflevector(b,a,0,4,1,5)};FOLD(s);\n"
          "v2di t[]={__builtin_shufflevector(q,r,1,0),__builtin_shufflevector(q,r,3,0),__builtin_shufflevector(q,r,2,3),__builtin_shufflevector(q,r,1,2),"
          "__builtin_shufflevector(q,r,3,3)};FOLD(t);\n"
          "v4sf u[]={__builtin_shufflevector(f,g,0,4,1,5),__builtin_shufflevector(f,g,6,7,2,3),__builtin_shufflevector(f,f,3,2,1,0),__builtin_shufflevector(f,g,7,0,6,1)};FOLD(u);"
          "v2df k[]={__builtin_shufflevector(d,e,1,2),__builtin_shufflevector(d,e,3,1)};FOLD(k);\n"
          "v8hi m[]={__builtin_shufflevector(w,x,0,8,1,9,2,10,3,11),__builtin_shufflevector(w,x,12,4,13,5,14,6,15,7),__builtin_shufflevector(w,w,3,2,1,0,7,7,4,5),"
          "__builtin_shufflevector(w,x,11,10,9,8,15,14,13,12),__builtin_shufflevector(w,x,2,3,0,1,6,7,4,5),__builtin_shufflevector(w,x,7,8,1,15,4,4,9,0),"
          "__builtin_shufflevector(w,x,0,0,0,0,0,0,0,0),__builtin_shufflevector(w,x,1,2,3,4,5,6,7,8)};FOLD(m);\n"
          "v16qi o[]={__builtin_shufflevector(y,z,0,16,1,17,2,18,3,19,4,20,5,21,6,22,7,23),__builtin_shufflevector(z,y,8,24,9,25,10,26,11,27,12,28,13,29,14,30,15,31),"
          "__builtin_shufflevector(y,z,2,3,0,1,6,7,4,5,10,11,8,9,14,15,12,13),"
          "__builtin_shufflevector(y,z,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0),__builtin_shufflevector(y,z,0,17,2,19,31,5,6,7,16,9,3,3,12,30,14,1),"
          "__builtin_shufflevector(y,z,1,0,3,2,5,4,7,6,9,8,11,10,13,12,15,14),__builtin_shufflevector(y,z,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5)};FOLD(o);\n"
          "typedef char v32qi __attribute__((vector_size(32),aligned(16)));typedef short v16hi __attribute__((vector_size(32),aligned(16)));"
          "v32qi y2=__builtin_shufflevector(y,z,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31),z2=y2+(char)n;"
          "v16hi w2=(v16hi)y2,x2=(v16hi)z2;"
          "v32qi o2[]={__builtin_shufflevector(y2,z2,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0),"
          "__builtin_shufflevector(y2,z2,0,33,2,35,63,5,6,7,48,9,3,3,12,62,14,1,16,40,18,19,20,21,22,23,24,25,26,27,28,29,30,32)};FOLD(o2);"
          "v16hi m2[]={__builtin_shufflevector(w2,x2,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0),__builtin_shufflevector(w2,x2,0,16,1,17,8,24,9,25,31,30,2,2,3,7,4,5)};FOLD(m2);"
          "v16qi o3=__builtin_shufflevector(y2,z2,31,0,30,1,29,2,28,3,63,32,62,33,61,34,60,35);FOLD(o3);\n"
          "v2sf p=__builtin_shufflevector(f,f,2,3);v4sf pp=__builtin_shufflevector(p,(v2sf){1.0f,2.0f},0,2,1,3);FOLD(p);FOLD(pp);"
          "v2si i2=__builtin_shufflevector((v2si){n,7},(v2si){-3,n*5},3,0);FOLD(i2);return (int)h;}\n"
        },
        // SSE3's, SSSE3's and SSE4.1's (x86-64-v2's): blends by a vector's signs and by immediates, extensions, elements'
        // extractions and insertions, packs, minima and maxima, products, horizontal sums, alignments, signs, roundings,
        // dot products and tests.
        { "baseline-sse41",
          "#include <smmintrin.h>\n"
          "int tested(int n){unsigned h=2166136261u;\n"
          "#ifdef __SSE4_1__\n"
          "__m128i a,b,m;__m128 f,g;__m128d d,e;fill(&a,16,n);fill(&b,16,n+1);fill(&m,16,n+2);fillf((float*)&f,4,n);fillf((float*)&g,4,n+3);filld((double*)&d,2,n);"
          "filld((double*)&e,2,n+1);\n"
          "__m128i r[]={_mm_blendv_epi8(a,b,m),_mm_blend_epi16(a,b,0x5a),_mm_cvtepi8_epi16(a),_mm_cvtepu8_epi32(b),_mm_cvtepi16_epi64(a),_mm_cvtepu32_epi64(b),"
          "_mm_insert_epi8(a,n,5),_mm_insert_epi32(b,n*3,2),_mm_insert_epi64(a,(long long)n<<33,1),_mm_packus_epi32(a,b),_mm_min_epi8(a,b),_mm_max_epu16(a,b),"
          "_mm_min_epi32(a,b),_mm_max_epu32(a,b),_mm_mullo_epi32(a,b),_mm_alignr_epi8(a,b,5),_mm_hadd_epi16(a,b),_mm_hsub_epi32(a,b),_mm_sign_epi8(a,b),"
          "_mm_maddubs_epi16(a,b),_mm_mulhrs_epi16(a,b),_mm_abs_epi16(a),_mm_shuffle_epi8(a,b),_mm_minpos_epu16(a)};FOLD(r);\n"
          "__m128 s[]={_mm_blendv_ps(f,g,_mm_castsi128_ps(m)),_mm_blend_ps(f,g,5),_mm_round_ps(f,1),_mm_floor_ps(g),_mm_dp_ps(f,g,0xf1),_mm_hadd_ps(f,g),"
          "_mm_addsub_ps(f,g)};FOLD(s);\n"
          "__m128d t[]={_mm_blendv_pd(d,e,_mm_castsi128_pd(m)),_mm_blend_pd(d,e,2),_mm_round_pd(d,2),_mm_ceil_sd(d,e),_mm_dp_pd(d,e,0x31),_mm_hsub_pd(d,e),"
          "_mm_addsub_pd(d,e)};FOLD(t);\n"
          "int c[]={_mm_extract_epi8(a,9),_mm_extract_epi32(b,3),_mm_extract_ps(f,2),_mm_testz_si128(a,b),_mm_testc_si128(a,m),_mm_testnzc_si128(b,m)};FOLD(c);"
          "long long l=_mm_extract_epi64(a,1);FOLD(l);\n"
          "#endif\n"
          "return (int)h;}\n"
        },
        // __builtin_convertvector's integer extensions without SSE4.1 (SSE2's unpacks and arithmetic shifts) and truncations
        // without AVX-512 (PSHUFD, shifts and saturating packs; with AVX2 of 32 bytes too), at each element size and
        // signedness, and between 8- and 16-bit integers and floats or doubles, through dwords (#1834).
        { "baseline-integer-conversions",
          "typedef signed char sb8 __attribute__((vector_size(8)));typedef unsigned char ub8 __attribute__((vector_size(8)));"
          "typedef signed char sb4 __attribute__((vector_size(4)));typedef unsigned char ub4 __attribute__((vector_size(4)));"
          "typedef short sw8 __attribute__((vector_size(16)));typedef unsigned short uw8 __attribute__((vector_size(16)));"
          "typedef short sw4 __attribute__((vector_size(8)));typedef unsigned short uw4 __attribute__((vector_size(8)));"
          "typedef short sw2 __attribute__((vector_size(4)));typedef int sd2 __attribute__((vector_size(8)));typedef unsigned ud2 __attribute__((vector_size(8)));"
          "typedef unsigned ud4 __attribute__((vector_size(16)));typedef unsigned long long uq2 __attribute__((vector_size(16)));"
          "typedef char c2 __attribute__((vector_size(2)));typedef signed char sb16 __attribute__((vector_size(16)));"
          "typedef double v4df __attribute__((vector_size(32),aligned(16)));\n"
          "#define C(T,v) do{T c_=__builtin_convertvector(v,T);FOLD(c_);}while(0)\n"
          "int tested(int n){unsigned h=2166136261u;sb8 c;ub8 d;sb4 e;ub4 f;sw8 g;uw8 i;sw4 j;uw4 k;v4si l;ud4 m;sd2 o;ud2 p;v2di q;uq2 r;sw2 s;v4sf x;v2df y;"
          "fill(&c,8,n);fill(&d,8,n+1);fill(&e,4,n+2);fill(&f,4,n+3);fill(&g,16,n+4);fill(&i,16,n+5);fill(&j,8,n+6);fill(&k,8,n+7);fill(&l,16,n+8);"
          "fill(&m,16,n+9);fill(&o,8,n+10);fill(&p,8,n+11);fill(&q,16,n+12);fill(&r,16,n+13);fill(&s,4,n+14);fillf((float*)&x,4,n);filld((double*)&y,2,n);\n"
          "C(sw8,c);C(uw8,c);C(sw8,d);C(uw8,d);C(v4si,e);C(ud4,e);C(v4si,f);C(ud4,f);C(v4si,j);C(v4si,k);C(v2di,o);C(v2di,p);C(uq2,o);C(v2di,s);C(uq2,s);\n"
          "C(sb8,g);C(ub8,i);C(sw4,l);C(uw4,m);C(sb4,l);C(ub4,m);C(sd2,q);C(ud2,r);C(sw2,q);C(c2,r);\n"
          "C(sb4,x);C(ub4,x+15.0f);C(sw4,x);C(sw2,y);C(c2,y);C(v4sf,e);C(v4sf,f);C(v4sf,j);C(v4sf,k);C(v2df,s);\n"
          "#ifdef __AVX2__\n"
          "v8si a;v16hi b;v4di t;v8sf u;v4df w;fill(&a,32,n+15);fill(&b,32,n+16);fill(&t,32,n+17);fillf((float*)&u,8,n+1);filld((double*)&w,4,n+1);\n"
          "C(sw8,a);C(sb8,a);C(sb16,b);C(v16qu,b);C(v4si,t);C(sw4,t);C(ub4,t);C(sw8,u);C(sb8,u);C(sw4,w);C(ub4,w+151.0);\n"
          "#endif\n"
          "return (int)h;}\n"
        },
        // #1834: shifts by a scalar count without AVX-512, by the count in an XMM register as clang's code shifts by a
        // splat: words', dwords' and qwords' PSLL, PSRL and PSRA (a qword's arithmetic one by the logical one and its sign
        // bit), bytes' through words (masked, or unpacked into words' high bytes), of 8, 16 and 32 bytes, and 32 bytes
        // split in two without AVX; counts of each type, constant and not.
        { "baseline-scalar-shifts",
          "#define T(name,ct,W) typedef ct name __attribute__((vector_size(W)));\n"
          "T(sb,signed char,16)T(ub,unsigned char,16)T(sw,short,16)T(uw,unsigned short,16)T(sd,int,16)T(ud,unsigned,16)T(sq,long long,16)"
          "T(uq,unsigned long long,16)T(sb8,signed char,8)T(ud2,unsigned,8)T(sw4,short,8)T(SB,signed char,32)T(UB,unsigned char,32)T(SW,short,32)"
          "T(SD,int,32)T(UQ,unsigned long long,32)T(SQ,long long,32)\n"
          "#define S(T,bits) do{T x_;fill(&x_,sizeof(T),n+k+(int)sizeof(T)*bits);T r_[]={x_<<(k%bits),x_>>(k%bits),x_<<3,x_>>(bits-1),x_>>0,"
          "(T)(x_<<(unsigned char)(bits/2))};FOLD(r_);}while(0)\n"
          "int tested(int n){unsigned h=2166136261u;for(int k=0;k<70;k+=3){\n"
          "S(sb,8);S(ub,8);S(sw,16);S(uw,16);S(sd,32);S(ud,32);S(sq,64);S(uq,64);S(sb8,8);S(ud2,32);S(sw4,16);\n"
          "#ifndef __AVX512F__\n"
          "S(SB,8);S(UB,8);\n"
          "#endif\n"
          "S(SW,16);S(SD,32);S(UQ,64);S(SQ,64);long long kk=k%64;v2di z;fill(&z,16,n);v2di zz=z>>kk;FOLD(zz);}\n"
          "return (int)h;}\n"
        },
        // #1834: the generic builtins of vectors wider than the target's registers (32 bytes without AVX, 64 without
        // AVX-512) on pieces of the registers' width through memory, as a transport vector's: shuffles of each element
        // size, a 16-byte vector's two into one of 32 and 32-byte ones' into 16, conversions widening and between integers
        // and floats, and elementwise minima, maxima, saturating arithmetic and square roots.
        { "baseline-wide-builtins",
          "typedef int I8 __attribute__((vector_size(32),aligned(16)));typedef unsigned U8 __attribute__((vector_size(32),aligned(16)));typedef float F8 __attribute__((vector_size(32),aligned(16)));\n"
          "typedef short S16 __attribute__((vector_size(32),aligned(16)));typedef unsigned char B32 __attribute__((vector_size(32),aligned(16)));typedef double D4 __attribute__((vector_size(32),aligned(16)));\n"
          "typedef long long L4 __attribute__((vector_size(32),aligned(16)));typedef short S8 __attribute__((vector_size(16)));typedef int I4 __attribute__((vector_size(16)));\n"
          "typedef unsigned char B64 __attribute__((vector_size(64),aligned(16)));typedef int I16 __attribute__((vector_size(64),aligned(16)));typedef float F16 __attribute__((vector_size(64),aligned(16)));\n"
          "typedef double D8 __attribute__((vector_size(64),aligned(16)));typedef unsigned short W32 __attribute__((vector_size(64),aligned(16)));\n"
          "int tested(int n){unsigned h=2166136261u;I8 a,b;U8 u,v;S16 s,t;B32 c,d;L4 l,m;I16 w,x;B64 e,g;W32 y,z;F8 f,k;D4 q,r;F16 fa;D8 da;\n"
          "fill(&a,32,n);fill(&b,32,n+1);fill(&u,32,n+2);fill(&v,32,n+3);fill(&s,32,n+4);fill(&t,32,n+5);fill(&c,32,n+6);fill(&d,32,n+7);fill(&l,32,n+8);fill(&m,32,n+9);\n"
          "fill(&w,64,n+10);fill(&x,64,n+11);fill(&e,64,n+12);fill(&g,64,n+13);fill(&y,64,n+14);fill(&z,64,n+15);\n"
          "for(int i=0;i<8;i++){f[i]=(float)((n*13+i*7)%50-25)*0.75f;k[i]=(float)((n*5+i*3)%40-20)*0.5f;}for(int i=0;i<4;i++){q[i]=f[i]*1.5;r[i]=k[i+4];}\n"
          "for(int i=0;i<16;i++)fa[i]=(float)((i*7+n)%30-15)*0.25f;for(int i=0;i<8;i++)da[i]=fa[i*2];\n"
          "I8 s1[]={__builtin_shufflevector(a,b,7,6,5,4,3,2,1,0),__builtin_shufflevector(a,b,0,8,1,9,2,10,3,11),__builtin_shufflevector(a,b,15,0,14,1,13,2,12,3)};FOLD(s1);\n"
          "#if defined(__SSSE3__)&&!defined(__AVX2__)\n"
          "B32 s2r=__builtin_shufflevector(c,d,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);FOLD(s2r);\n"
          "#endif\n"
          "#ifndef __AVX2__\n"
          "B32 s2[]={__builtin_shufflevector(c,d,0,32,1,33,2,34,3,35,4,36,5,37,6,38,7,39,8,40,9,41,10,42,11,43,12,44,13,45,14,46,15,47)};FOLD(s2);\n"
          "#endif\n"
          "L4 s3=__builtin_shufflevector(l,m,3,4,1,6);FOLD(s3);I4 s4=__builtin_shufflevector(a,b,1,9,7,15);FOLD(s4);I8 s5=__builtin_shufflevector((I4){n,2,3,4},(I4){5,6,7,n},7,6,5,4,3,2,1,0);FOLD(s5);\n"
          "I16 s6=__builtin_shufflevector(w,x,31,30,29,28,27,26,25,24,0,1,2,3,4,5,6,7);FOLD(s6);\n"
          "F8 c3=__builtin_convertvector(a,F8);FOLD(c3);I8 c4=__builtin_convertvector(f,I8);FOLD(c4);\n"
          "D4 c5=__builtin_convertvector(s4,D4);FOLD(c5);L4 c6=__builtin_convertvector(s4,L4);FOLD(c6);typedef unsigned char B16 __attribute__((vector_size(16)));B16 cc;fill(&cc,16,n);I16 c7=__builtin_convertvector(cc,I16);FOLD(c7);F16 c8=__builtin_convertvector(w,F16);FOLD(c8);\n"
          "#ifdef __SSE4_1__\n"
          "U8 e2[]={__builtin_elementwise_min(u,v),__builtin_elementwise_max(u,v)};FOLD(e2);\n"
          "#endif\n"
          "I8 e1[]={a+b};FOLD(e1);\n"
          "S16 e3[]={__builtin_elementwise_min(s,t),__builtin_elementwise_add_sat(s,t),__builtin_elementwise_sub_sat(s,t)};FOLD(e3);\n"
          "B32 e4[]={__builtin_elementwise_min(c,d),__builtin_elementwise_max(c,d),__builtin_elementwise_add_sat(c,d),__builtin_elementwise_sub_sat(c,d)};FOLD(e4);\n"
          "B64 e5[]={__builtin_elementwise_min(e,g),__builtin_elementwise_add_sat(e,g)};FOLD(e5);W32 e6[]={__builtin_elementwise_sub_sat(y,z)};FOLD(e6);\n"
          "F8 e7[]={__builtin_elementwise_sqrt(f*f)};FOLD(e7);\n"
          "D4 e8[]={__builtin_elementwise_sqrt(q*q)};FOLD(e8);\n"
          "F16 e9[]={__builtin_elementwise_sqrt(fa*fa)};FOLD(e9);D8 e10[]={__builtin_elementwise_sqrt(da*da)};FOLD(e10);\n"
          "return (int)h;}\n"
        },
        // Vectors' floating elementwise builtins without AVX-512 (#1834), of 8, 16, 32 and 64 bytes of floats and 16, 32
        // and 64 of doubles: SSE4.1's ROUNDPS and ROUNDPD (the C library's ceilf ... round an element without them), and
        // FMA's VFMADD213PS and PD (fmaf and fma an element without it); canonicalize, copysign, maxnum and minnum by SSE's
        // forms, and the IEEE maxima and minima, with AVX's blends or, without AVX, a chain of rows; of NaNs (their
        // payloads and signs), signed zeros, infinities, a denormal, halves and values past 2^23 and 2^52.
        { "baseline-vector-floating-builtins",
          "typedef float v2sf __attribute__((vector_size(8)));typedef double v4df __attribute__((vector_size(32),aligned(16)));\n"
          "#define U(w) do{__typeof__(w) v=w,r_[]={__builtin_elementwise_ceil(v),__builtin_elementwise_floor(v),__builtin_elementwise_trunc(v),"
          "__builtin_elementwise_rint(v),__builtin_elementwise_nearbyint(v),__builtin_elementwise_roundeven(v),__builtin_elementwise_round(v),"
          "__builtin_elementwise_canonicalize(v)};FOLD(r_);}while(0)\n"
          "#define B(w,z) do{__typeof__(w) a=w,b=z,r_[]={__builtin_elementwise_copysign(a,b),__builtin_elementwise_maxnum(a,b),__builtin_elementwise_minnum(a,b),"
          "__builtin_elementwise_maximum(a,b),__builtin_elementwise_minimum(a,b),__builtin_elementwise_maximumnum(a,b),__builtin_elementwise_minimumnum(a,b),"
          "__builtin_elementwise_fma(a,b,a)};FOLD(r_);}while(0)\n"
          "union F{unsigned u[16];v16sf v;v8sf h[2];v4sf q[4];v2sf e[8];};union D{unsigned long long u[8];v8df v;v4df h[2];v2df q[4];};\n"
          "int tested(int n){unsigned h=2166136261u;\n"
          "static const unsigned fb[]={0x7fc01234u,0xffc05678u,0x80000000u,0,0x3f800000u,0xbf800000u,0x7f800000u,0xff800000u,1,0x40490fdbu,0x3f000000u,"
          "0xbf000000u,0x3fc00000u,0xc0200000u,0x3effffffu,0x4b7fffffu,0x7f812345u};\n"
          "static const unsigned long long db[]={0x7ff8000000001234ull,0xfff8000000005678ull,0x8000000000000000ull,0,0x3ff0000000000000ull,"
          "0xbff0000000000000ull,0x7ff0000000000000ull,0xfff0000000000000ull,1,0x400921fb54442d18ull,0x3fe0000000000000ull,0xbfe0000000000000ull,"
          "0x3ff8000000000000ull,0xc004000000000000ull,0x3fdfffffffffffffull,0x432fffffffffffffull,0x7ff0000000012345ull};\n"
          "for(unsigned i=0;i<17;i++){union F x,y;union D p,q;\n"
          "for(unsigned j=0;j<16;j++){x.u[j]=fb[(i+j+(unsigned)n)%17];y.u[j]=fb[(i*3+j*5+(unsigned)n)%17];}\n"
          "for(unsigned j=0;j<8;j++){p.u[j]=db[(i+j+(unsigned)n)%17];q.u[j]=db[(i*3+j*5+(unsigned)n)%17];}\n"
          "U(x.v);U(x.h[0]);U(x.q[1]);U(x.e[2]);U(p.v);U(p.h[1]);U(p.q[0]);\n"
          "B(x.v,y.v);B(x.h[1],y.h[1]);B(x.q[0],y.q[0]);B(x.e[3],y.e[3]);B(p.v,q.v);B(p.h[0],q.h[0]);B(p.q[1],q.q[1]);}\n"
          "return (int)h;}\n"
        },
        // A generic vector builtin of a member as an array's first element, before the member itself (#2111): the
        // inferred extent took the last typed token's type, the struct's, for the call's, so the array had one element.
        // Elementwise builtins of one and two operands, integer and floating, of struct, union and pointed-to members;
        // and a reduction's scalar, which takes one element of a vector array's first vector.
        { "member-operand-builtins",
          "struct S{v4si v;v4sf f;};union M{v4si v;v4sf f;};\n"
          "int tested(int n){unsigned h=2166136261u;struct S x;union M u;struct S*p=&x;\n"
          "x.v=(v4si){1,-2,3,n};x.f=(v4sf){1.0f,4.0f,9.0f,(float)(n*n)};\n"
          "v4si r[]={__builtin_elementwise_abs(x.v),x.v};v4sf q[]={__builtin_elementwise_sqrt(x.f),x.f};\n"
          "v4si m[]={__builtin_elementwise_add_sat(x.v,x.v),x.v};u.v=x.v;v4si w[]={__builtin_elementwise_max(u.v,(v4si){0}),u.v};\n"
          "u.f=x.f;v4sf z[]={__builtin_elementwise_minnum(p->f,u.f),p->f,__builtin_elementwise_maxnum(u.f,x.f)};\n"
          "int s[]={__builtin_reduce_add(x.v),x.v[3]};v4si t[]={__builtin_reduce_add(p->v),7,8,9,x.v};\n"
          "_Static_assert(sizeof r==32&&sizeof q==32&&sizeof m==32&&sizeof w==32&&sizeof z==48&&sizeof s==8&&sizeof t==32,\"extents\");\n"
          "FOLD(r);FOLD(q);FOLD(m);FOLD(w);FOLD(z);FOLD(s);FOLD(t);return (int)h;}\n"
        },
        // Reductions without AVX-512 (#1834): every integer one of 8- to 64-bit elements of 4 to 64 bytes, folded element
        // by element; and the floating maximum, minimum, max, min and associative sums of 8 to 64 bytes of floats and 16 to
        // 64 of doubles in clang's halving order (pieces of the registers' bytes paired first), of NaNs (their payloads and
        // signs), signed zeros, infinities and values whose sums round by their order.
        { "baseline-reductions",
          "typedef float v2sf __attribute__((vector_size(8)));typedef double v4df __attribute__((vector_size(32),aligned(16)));\n"
          "#define RI(T,E,W) do{typedef E V __attribute__((vector_size(W),aligned(16)));V v;fill(&v,W,n*7+W);"
          "E r_[]={__builtin_reduce_add(v),__builtin_reduce_mul(v),__builtin_reduce_and(v),__builtin_reduce_or(v),__builtin_reduce_xor(v),__builtin_reduce_max(v),__builtin_reduce_min(v)};FOLD(r_);}while(0)\n"
          "#define RIS(E) RI(T,E,4);RI(T,E,8);RI(T,E,16);RI(T,E,32);RI(T,E,64)\n"
          "#define RF(w) do{__typeof__(w) v=w;"
          "__typeof__(v[0]) r_[]={__builtin_reduce_maximum(v),__builtin_reduce_minimum(v),__builtin_reduce_max(v),__builtin_reduce_min(v),__builtin_reduce_assoc_fadd(v),__builtin_reduce_assoc_fadd(v,(__typeof__(v[0]))n)};FOLD(r_);}while(0)\n"
          "union F{unsigned u[16];v16sf v;v8sf h[2];v4sf q[4];v2sf e[8];};union D{unsigned long long u[8];v8df v;v4df h[2];v2df q[4];};\n"
          "int tested(int n){unsigned h=2166136261u;\n"
          "RIS(signed char);RIS(unsigned char);RIS(short);RIS(unsigned short);RI(T,int,8);RI(T,int,16);RI(T,int,32);RI(T,int,64);RI(T,unsigned,8);"
          "RI(T,unsigned,16);RI(T,unsigned,32);RI(T,unsigned,64);RI(T,long long,16);RI(T,long long,32);RI(T,long long,64);RI(T,unsigned long long,16);"
          "RI(T,unsigned long long,32);RI(T,unsigned long long,64);\n"
          "static const unsigned fb[]={0x7fc01234u,0xffc05678u,0x80000000u,0,0x3f800000u,0xbf800000u,0x7f800000u,0xff800000u,1,0x40490fdbu,0x3f000000u,0xbf000000u,0x3fc00000u,0xc0200000u,0x3effffffu,0x4b7fffffu,0x7f812345u,0x3dcccccdu,0x4cbebc20u,0xb3d6bf95u};\n"
          "static const unsigned long long db[]={0x7ff8000000001234ull,0xfff8000000005678ull,0x8000000000000000ull,0,0x3ff0000000000000ull,0xbff0000000000000ull,0x7ff0000000000000ull,0xfff0000000000000ull,1,0x400921fb54442d18ull,0x3fe0000000000000ull,0xbfe0000000000000ull,0x3ff8000000000000ull,0xc004000000000000ull,0x3fdfffffffffffffull,0x432fffffffffffffull,0x7ff0000000012345ull,0x3fb999999999999aull,0x4415af1d78b58c40ull,0xbcb0000000000000ull};\n"
          "for(unsigned i=0;i<60;i++){union F x;union D p;unsigned k=i%3==0?20:4;\n"
          "for(unsigned j=0;j<16;j++)x.u[j]=fb[(i*7+j*(i%5+1)+(unsigned)n)%k+(k==4?16:0)];\n"
          "for(unsigned j=0;j<8;j++)p.u[j]=db[(i*7+j*(i%5+1)+(unsigned)n)%k+(k==4?16:0)];\n"
          "RF(x.v);RF(x.h[1]);RF(x.q[2]);RF(x.e[1]);RF(p.v);RF(p.h[0]);RF(p.q[1]);}\n"
          "return (int)h;}\n"
        },
        // Masked memory builtins without AVX-512 (#1834), element by element: loads, stores, expanding loads, compressing
        // stores, gathers and scatters (a scatter's indices counted in the indices' elements, as clang's code counts them)
        // of 8- to 64-bit elements and 2 to 16 lanes; and at a page's end before an inaccessible one, lanes whose bits are
        // clear past it, which must touch nothing there.
        { "baseline-masked-memory",
          "#include <sys/mman.h>\n"
          "#define M(E,L) do{typedef E X __attribute__((ext_vector_type(L)));typedef _Bool B __attribute__((ext_vector_type(L)));"
          "typedef int I __attribute__((ext_vector_type(L)));E mem[4*L];X w,v;I ix;unsigned bits;fill(mem,sizeof mem,n+L);fill(&w,sizeof w,n*3+L);"
          "fill(&v,sizeof v,n*5+L);fill(&bits,4,n*7+L);B m;for(int i_=0;i_<L;i_++){m[i_]=(bits>>i_)&1;ix[i_]=(int)((bits>>(i_+3))%(sizeof(E)<4?L:4*L));"
          "}X r1=__builtin_masked_load(m,mem,w);X r2=__builtin_masked_expand_load(m,mem+1,w);X r3=__builtin_masked_gather(m,ix,mem,w);FOLD(r1);FOLD(r2);FOLD(r3);"
          "__builtin_masked_store(m,v,mem+L);__builtin_masked_compress_store(m,v,mem+2*L+1);__builtin_masked_scatter(m,ix,v,mem);FOLD(mem);"
          "E*edge=(E*)(page+4096)-L/2;B lo;for(int i_=0;i_<L;i_++)lo[i_]=i_<L/2;X r4=__builtin_masked_load(lo,edge,w);FOLD(r4);__builtin_masked_store(lo,v,edge);"
          "X r5=__builtin_masked_expand_load(lo,edge,w);FOLD(r5);__builtin_masked_compress_store(lo,v,edge);FOLD(*(X*)(edge-L/2));}while(0)\n"
          "#define ALL(E) M(E,2);M(E,4);if(sizeof(E)<=8)M(E,8);if(sizeof(E)<=4)M(E,16)\n"
          "int tested(int n){unsigned h=2166136261u;\n"
          "char*page=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);mprotect(page+4096,4096,PROT_NONE);\n"
          "ALL(int);ALL(unsigned);ALL(long long);ALL(float);ALL(double);M(signed char,4);M(signed char,16);M(short,8);M(unsigned short,4);\n"
          "munmap(page,8192);return (int)h;}\n"
        },
        // The bit builtins without AVX-512 (#1834), element by element: popcount, bitreverse, clzg and ctzg (with a zero's
        // operand, and without one of nonzero elements alone, which clang leaves undefined for zero), fshl and fshr, of
        // signed and unsigned 8- to 64-bit elements of 4 to 64 bytes, zeros and single bits among them.
        { "baseline-bit-builtins",
          "#define T(E,W) do{typedef E V __attribute__((vector_size(W)));V a,b,c;fill(&a,W,n*11+W);fill(&b,W,n*13+W+1);fill(&c,W,n*17+W+2);for(unsigned k_=0;"
          "k_<W/sizeof(E);k_+=3)a[k_]=0;for(unsigned k_=1;k_<W/sizeof(E);k_+=5)a[k_]=(E)1<<(k_%(8*sizeof(E)));"
          "V r_[]={__builtin_elementwise_popcount(a),__builtin_elementwise_bitreverse(a),__builtin_elementwise_clzg(a,b),__builtin_elementwise_ctzg(a,b),__builtin_elementwise_fshl(a,b,c),__builtin_elementwise_fshr(a,b,c)};FOLD(r_);V s_={0};for(unsigned k_=0;k_<W/sizeof(E);k_++)if(a[k_]){V t_=__builtin_elementwise_clzg(a);V u_=__builtin_elementwise_ctzg(a);s_[k_]=t_[k_]+u_[k_];}FOLD(s_);}while(0)\n"
          "#define ALL(E) T(E,4);T(E,8);T(E,16);T(E,32);T(E,64)\n"
          "int tested(int n){unsigned h=2166136261u;\n"
          "ALL(signed char);ALL(unsigned char);ALL(short);ALL(unsigned short);T(int,8);T(int,16);T(int,32);T(int,64);T(unsigned,16);T(unsigned,64);\n"
          "T(long long,16);T(long long,32);T(long long,64);T(unsigned long long,16);T(unsigned long long,64);\n"
          "return (int)h;}\n"
        },
        // _mm_maskmoveu_si128 and _mm_maskmove_si64 without AVX-512BW (#2240), byte by byte: random masks, none and every
        // byte selected, at every offset of a buffer, and _mm_maskmove_si64 at a page's end before an inaccessible one (its
        // header backs the 16-byte store up there; MASKMOVDQU itself may fault on bytes it doesn't store, so
        // _mm_maskmoveu_si128 isn't tested there).
        { "baseline-maskmove",
          "#include <immintrin.h>\n"
          "#include <sys/mman.h>\n"
          "int tested(int n){unsigned h=2166136261u;char buf[64];\n"
          "char*page=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);mprotect(page+4096,4096,PROT_NONE);\n"
          "for(int k=0;k<8;k++){__m128i d,m;fill(&d,16,n*3+k);fill(&m,16,n*5+k);if(k==0)m=_mm_setzero_si128();if(k==1)m=_mm_set1_epi8(-1);\n"
          "fill(buf,64,n+k);_mm_maskmoveu_si128(d,m,buf+k);h=fold(h,buf,64);\n"
          "__m64 e,q;fill(&e,8,n*7+k);fill(&q,8,n*11+k);fill(buf,64,n+k+1);_mm_maskmove_si64(e,q,buf+k*3);h=fold(h,buf,64);\n"
          "char*edge=page+4096-8;for(int i=0;i<8;i++)edge[i]=(char)(i+n);\n"
          "_mm_maskmove_si64(e,_mm_set1_pi8(-1),page+4096-8);h=fold(h,page+4096-8,8);}\n"
          "munmap(page,8192);return (int)h;}\n"
        },
        // Scalars' elementwise builtins without AVX-512F (#1834): the C library's ceilf ... roundf, fmaf and fma without
        // SSE4.1 and FMA, and ldexpf and ldexp; SSE4.1's ROUNDSS and ROUNDSD (round's of the value plus the largest value
        // below 0.5 of its sign) and FMA's VFMADD213SS and SD where the target has them; halves, values near them and past
        // 2^23 and 2^52. copysign, canonicalize, maxnum and minnum by SSE's forms, the IEEE maxima and minima (two rows
        // without AVX, #1834), and the fused multiply-add (b's NaN the result's of two, as clang's VFMADD213SS has b's register its destination),
        // of every pair of NaNs (their payloads), signed zeros, infinities, a denormal and others.
        { "baseline-scalar-builtins",
          "#define R(v) do{__typeof__(v) r_[]={__builtin_elementwise_ceil(v),__builtin_elementwise_floor(v),__builtin_elementwise_trunc(v),"
          "__builtin_elementwise_rint(v),__builtin_elementwise_nearbyint(v),__builtin_elementwise_roundeven(v),__builtin_elementwise_round(v)};FOLD(r_);}while(0)\n"
          "int tested(int n){unsigned h=2166136261u;\n"
          "static const float fs[]={0.5f,-0.5f,1.5f,2.5f,-2.5f,0.49999997f,-0.49999997f,8388607.5f,-0.0f,1e30f,-3.75f,0.0f};\n"
          "static const double ds[]={0.5,-0.5,1.5,2.5,-2.5,0.49999999999999994,-0.49999999999999994,4503599627370495.5,-0.0,1e300,-3.75,0.0};\n"
          "for(int i=0;i<12;i++){float x=fs[i]*(float)(n+1);double y=ds[i]*(double)(n+1);R(x);R(y);"
          "float f=__builtin_elementwise_fma(x,1.0f/3.0f,(float)n);double g=__builtin_elementwise_fma(y,1.0/3.0,-y);FOLD(f);FOLD(g);}\n"
          "#define B(x,y) do{__typeof__(x) r_[]={__builtin_elementwise_copysign(x,y),__builtin_elementwise_maxnum(x,y),__builtin_elementwise_minnum(x,y),"
          "__builtin_elementwise_canonicalize(x),__builtin_elementwise_fma(x,y,y)IEEE(x,y)};FOLD(r_);}while(0)\n"
          "#define IEEE(x,y) ,__builtin_elementwise_maximum(x,y),__builtin_elementwise_minimum(x,y),__builtin_elementwise_maximumnum(x,y),__builtin_elementwise_minimumnum(x,y)\n"
          "union{unsigned u;float f;}fu[]={{0x7fc01234u},{0xffc05678u},{0x80000000u},{0},{0x3f800000u},{0xbf800000u},{0x7f800000u},{0xff800000u},{1},{0x40490fdbu}};\n"
          "union{unsigned long long u;double f;}du[]={{0x7ff8000000001234ull},{0xfff8000000005678ull},{0x8000000000000000ull},{0},{0x3ff0000000000000ull},"
          "{0xbff0000000000000ull},{0x7ff0000000000000ull},{0xfff0000000000000ull},{1},{0x400921fb54442d18ull}};\n"
          "for(int i=0;i<10;i++)for(int j=0;j<10;j++){float x=fu[i].f,y=fu[(unsigned)(j+n+40)%10].f;double a=du[i].f,b=du[(unsigned)(j+n+40)%10].f;B(x,y);B(a,b);}\n"
          "float l[]={__builtin_elementwise_ldexp(1.5f,n),__builtin_elementwise_ldexp(3.0f,(long)n-100),__builtin_elementwise_ldexp(1.0f,(short)(n*7)),"
          "__builtin_elementwise_ldexp(2.0f,(long)n+0x100000000L)};FOLD(l);\n"
          "double k[]={__builtin_elementwise_ldexp(1.5,n),__builtin_elementwise_ldexp(3.0,(long)n-1000),__builtin_elementwise_ldexp(1.0,(unsigned)n*3)};FOLD(k);\n"
          "return (int)h;}\n"
        },
        // __builtin_elementwise_min, _max and _abs of integers without their instructions (#1834): SSE2's compares and
        // blends, unsigned elements biased by their sign bits, unsigned words' saturating subtractions, bytes' and words'
        // absolute values of each and its negation and the others' by their signs; qwords' minima and maxima with SSE4.2's
        // PCMPGTQ, without it a compare's mask and a blend's row; 32 bytes with AVX2. Each pair has equal elements and the
        // most negative one.
        { "baseline-min-max-abs",
          "typedef signed char sb __attribute__((vector_size(16)));typedef short sw __attribute__((vector_size(16)));"
          "typedef unsigned ud __attribute__((vector_size(16)));typedef unsigned long long uq __attribute__((vector_size(16)));"
          "typedef signed char sb8 __attribute__((vector_size(8)));typedef unsigned short uw4 __attribute__((vector_size(8)));typedef int sd2 __attribute__((vector_size(8)));"
          "typedef unsigned char ub32 __attribute__((vector_size(32),aligned(16)));typedef unsigned short uw16 __attribute__((vector_size(32),aligned(16)));"
          "typedef unsigned ud8 __attribute__((vector_size(32),aligned(16)));typedef unsigned long long uq4 __attribute__((vector_size(32),aligned(16)));"
          "typedef signed char sb32 __attribute__((vector_size(32),aligned(16)));\n"
          "#define M(T,a,b) do{T x_=__builtin_elementwise_min(a,b),y_=__builtin_elementwise_max(a,b);FOLD(x_);FOLD(y_);}while(0)\n"
          "#define A(T,a) do{T x_=__builtin_elementwise_abs(a);FOLD(x_);}while(0)\n"
          "#define V(T,k) T a##k,b##k;fill(&a##k,sizeof(T),n+k);fill(&b##k,sizeof(T),n+k+50);b##k[1]=a##k[1];a##k[0]=(__typeof__(a##k[0]))(1ull<<(8*sizeof(a##k[0])-1));\n"
          "int tested(int n){unsigned h=2166136261u;V(sb,1)V(v16qu,2)V(sw,3)V(v8hu,4)V(v4si,5)V(ud,6)V(v2di,7)V(uq,8)V(sb8,9)V(uw4,10)V(sd2,11)\n"
          "M(sb,a1,b1);M(v16qu,a2,b2);M(sw,a3,b3);M(v8hu,a4,b4);M(v4si,a5,b5);M(ud,a6,b6);M(sb8,a9,b9);M(uw4,a10,b10);M(sd2,a11,b11);\n"
          "A(sb,a1);A(sw,a3);A(v4si,a5);A(v2di,a7);A(sb8,b9);A(sd2,b11);\n"
          "M(v2di,a7,b7);M(uq,a8,b8);\n"
          "#ifdef __AVX2__\n"
          "V(sb32,12)V(uw16,13)V(ud8,14)V(v4di,15)V(uq4,16)V(ub32,17)\n"
          "M(sb32,a12,b12);M(uw16,a13,b13);M(ud8,a14,b14);M(v4di,a15,b15);M(uq4,a16,b16);M(ub32,a17,b17);A(sb32,a12);A(v4di,a15);\n"
          "#endif\n"
          "return (int)h;}\n"
        },
        // Compares of qwords without SSE4.1's PCMPEQQ and SSE4.2's PCMPGTQ (#1834): PCMPEQD's halves ANDed, the sign of
        // the difference or of an operand, and `<=` and `>=` as complements; of 16, 32 and 64 bytes, and their uses in
        // `?:`, `!` and `&&`. The pairs have equal elements, equal high halves, the extremes and the sign bits.
        { "baseline-qword-compares",
          "typedef long long sq __attribute__((vector_size(16)));typedef unsigned long long uq __attribute__((vector_size(16)));"
          "typedef long long sq4 __attribute__((vector_size(32)));typedef unsigned long long uq4 __attribute__((vector_size(32)));"
          "typedef long long sq8 __attribute__((vector_size(64)));typedef unsigned long long uq8 __attribute__((vector_size(64)));"
          "typedef long long ex2 __attribute__((ext_vector_type(2)));typedef unsigned long long ux4 __attribute__((ext_vector_type(4)));\n"
          "#define C(T,a,b) do{__typeof__(a<b) r_[]={a<b,a<=b,a>b,a>=b,a==b,a!=b};FOLD(r_);}while(0)\n"
          "#define V(T,k) T a##k,b##k;fill(&a##k,sizeof(T),n+k);fill(&b##k,sizeof(T),n+k+50);b##k[1]=a##k[1];a##k[0]=(__typeof__(a##k[0]))(1ull<<63);"
          "b##k[0]=(__typeof__(a##k[0]))((unsigned long long)a##k[0]+1);"
          "if(sizeof(T)>16){a##k[2]=(__typeof__(a##k[0]))0x7fffffffffffffffull;b##k[2]=(__typeof__(a##k[0]))0x7fffffff00000000ull;a##k[3]=(__typeof__(a##k[0]))-1;b##k[3]=0;}\n"
          "int tested(int n){unsigned h=2166136261u;V(sq,1)V(uq,2)V(sq4,3)V(uq4,4)V(sq8,5)V(uq8,6)V(ex2,7)V(ux4,8)\n"
          "C(sq,a1,b1);C(uq,a2,b2);C(sq4,a3,b3);C(uq4,a4,b4);C(sq8,a5,b5);C(uq8,a6,b6);C(ex2,a7,b7);C(ux4,a8,b8);\n"
          "a7[1]=0;a8[2]=0;b8[3]=0;ex2 c7=a7?b7:-b7;ux4 c8=a8?b8:~b8;__typeof__(!a7) d7=!a7,e7=a7&&b7;__typeof__(!a8) d8=!a8,e8=a8&&b8;"
          "FOLD(c7);FOLD(c8);FOLD(d7);FOLD(e7);FOLD(d8);FOLD(e8);\n"
          "return (int)h;}\n"
        },
        // Divisions of floating vectors narrower than a register (#1834): the divisor's lanes past its own 1.0f.
        { "baseline-narrow-division",
          "typedef float f2 __attribute__((vector_size(8)));typedef float f1 __attribute__((vector_size(4)));typedef double d1 __attribute__((vector_size(8)));\n"
          "int tested(int n){unsigned h=2166136261u;f2 a={(float)n,7.5f},b={3.0f,-0.25f};f1 c={(float)n*1.5f},e={-2.0f};d1 g={(double)n/3},k={0.1};"
          "f2 x=a/b;f1 y=c/e;d1 z=g/k;f2 w=b/(f2){(float)(n*n+1),2.0f};FOLD(x);FOLD(y);FOLD(z);FOLD(w);return (int)h;}\n"
        },
        // Products of qwords without AVX-512DQ (#1834): PMULUDQ's of the low dwords and of each high one by the other's
        // low one, the latter two summed and shifted up; of 16, 32 and 64 bytes, by a splat and of an odd length.
        { "baseline-qword-multiply",
          "typedef unsigned long long uq4 __attribute__((vector_size(32),aligned(16)));typedef long long sq8 __attribute__((vector_size(64),aligned(16)));"
          "typedef long long e3 __attribute__((ext_vector_type(3)));\n"
          "int tested(int n){unsigned h=2166136261u;v2di a,b;uq4 c,d;sq8 e,f;e3 g,k;fill(&a,16,n);fill(&b,16,n+1);fill(&c,32,n+2);fill(&d,32,n+3);fill(&e,64,n+4);"
          "fill(&f,64,n+5);fill(&g,sizeof g,n+6);fill(&k,sizeof k,n+7);a[1]=-1;b[0]=(long long)(1ull<<63);"
          "v2di x[]={a*b,a*(long long)n,b*b};uq4 y[]={c*d,d*3u};sq8 z[]={e*f,f*-7};e3 w[]={g*k,g*g};FOLD(x);FOLD(y);FOLD(z);FOLD(w);return (int)h;}\n"
        },
        // Nontemporal loads and stores (#1834): an ordinary load without SSE4.1, and vectors wider than the target's
        // nontemporal moves a piece at a time; of 8, 16, 32 and 64 bytes.
        { "baseline-nontemporal",
          "typedef int d2 __attribute__((vector_size(8),aligned(8)));typedef float f8 __attribute__((vector_size(32),aligned(32)));"
          "typedef short w32 __attribute__((vector_size(64),aligned(64)));typedef long long q4 __attribute__((vector_size(32),aligned(32)));\n"
          "int tested(int n){unsigned h=2166136261u;d2 a,a2;v4si b,b2;f8 c,c2;w32 e,e2;q4 g,g2;fill(&a,8,n);fill(&b,16,n+1);fillf((float*)&c,8,n+2);fill(&e,64,n+3);"
          "fill(&g,32,n+4);a2=__builtin_nontemporal_load(&a);b2=__builtin_nontemporal_load(&b);c2=__builtin_nontemporal_load(&c);e2=__builtin_nontemporal_load(&e);"
          "g2=__builtin_nontemporal_load(&g);__builtin_nontemporal_store(a2+1,&a);__builtin_nontemporal_store(b2*3,&b);__builtin_nontemporal_store(c2,&c);"
          "__builtin_nontemporal_store(e2,&e);__builtin_nontemporal_store(g2^7,&g);FOLD(a);FOLD(b);FOLD(c);FOLD(e);FOLD(g);FOLD(a2);FOLD(e2);return (int)h;}\n"
        },
        // Conversions between unsigned or 64-bit integers and floats or doubles without AVX-512 (#1834), element by
        // element as C converts each: in range, the most negative and the largest values and halfway cases among them.
        { "baseline-integer-floating-conversions",
          "typedef unsigned u4 __attribute__((vector_size(16)));typedef unsigned u2 __attribute__((vector_size(8)));typedef unsigned long long uq2 __attribute__((vector_size(16)));"
          "typedef double d4 __attribute__((vector_size(32),aligned(16)));typedef float f2 __attribute__((vector_size(8)));typedef long long q4 __attribute__((vector_size(32),aligned(16)));\n"
          "int tested(int n){unsigned h=2166136261u;u4 a;uq2 b;v2di c;fill(&a,16,n);fill(&b,16,n+1);fill(&c,16,n+2);a[0]=0xffffffffu;a[1]=0x80000001u;b[0]=~0ull;"
          "b[1]=(1ull<<63)+1025;c[0]=(long long)(1ull<<63);c[1]=(1ll<<53)+1;"
          "v4sf fa=__builtin_convertvector(a,v4sf);d4 da=__builtin_convertvector(a,d4);v2df db=__builtin_convertvector(b,v2df);f2 fb=__builtin_convertvector(b,f2);"
          "v2df dc=__builtin_convertvector(c,v2df);f2 fc=__builtin_convertvector(c,f2);FOLD(fa);FOLD(da);FOLD(db);FOLD(fb);FOLD(dc);FOLD(fc);\n"
          "v4sf g={3e9f,1.5f,4294967040.0f,(float)(n*n)};v2df k={1.8e19,(double)(n*n)*1e15};v2df m={-9.2e18,(double)n*-3e14};v2df k2={4e9,(double)(n*n)+0.5};"
          "u4 ga=__builtin_convertvector(g,u4);uq2 kb=__builtin_convertvector(k,uq2);v2di mc=__builtin_convertvector(m,v2di);u2 gk=__builtin_convertvector(k2,u2);"
          "q4 gq=__builtin_convertvector(g,q4);FOLD(ga);FOLD(kb);FOLD(mc);FOLD(gk);FOLD(gq);return (int)h;}\n"
        },
        // Integer vectors' division and remainder, element by element at every target (#2216): of each element size and
        // signedness, of 8 to 128 bytes and an odd length, by a splat and in place; no divisor is 0, nor INT_MIN's -1.
        { "baseline-integer-division",
          "typedef signed char sb8 __attribute__((vector_size(8)));typedef unsigned short uw __attribute__((vector_size(16)));"
          "typedef unsigned ud8 __attribute__((vector_size(32),aligned(16)));typedef long long sq8 __attribute__((vector_size(64),aligned(16)));"
          "typedef int sd32 __attribute__((vector_size(128),aligned(16)));typedef short e3 __attribute__((ext_vector_type(3)));\n"
          "#define D(T,k) T a##k,b##k;fill(&a##k,sizeof(T),n+k);fill(&b##k,sizeof(T),n+k+50);"
          "for(unsigned i_=0;i_<sizeof(T)/sizeof(a##k[0]);i_++){if(b##k[i_]==0||b##k[i_]==(__typeof__(b##k[0]))-1)b##k[i_]=3;"
          "if(a##k[i_]==(__typeof__(a##k[0]))(1ull<<(8*sizeof(a##k[0])-1)))a##k[i_]=1;}"
          "{T q_[]={a##k/b##k,a##k%b##k,a##k/(__typeof__(a##k[0]))7,a##k%(__typeof__(a##k[0]))5};a##k/=b##k;FOLD(q_);FOLD(a##k);}\n"
          "int tested(int n){unsigned h=2166136261u;D(sb8,1)D(v16qu,2)D(uw,3)D(v4si,4)D(ud8,5)D(v2di,6)D(sq8,7)D(sd32,8)\n"
          "e3 c={(short)n,-7,300},d={3,(short)(n|1),-9};e3 r[]={c/d,c%d};short o[]={r[0][0],r[0][1],r[0][2],r[1][0],r[1][1],r[1][2]};FOLD(o);return (int)h;}\n"
        },
        // Products and shifts no row takes, element by element (#2216): bytes' at every target, and shifts by a vector's
        // counts without AVX2; of 8 to 128 bytes, by vectors, splats and constants, and in place.
        { "baseline-lanewise-operators",
          "typedef signed char sb __attribute__((vector_size(16)));typedef unsigned char ub32 __attribute__((vector_size(32),aligned(16)));"
          "typedef signed char sb64 __attribute__((vector_size(64),aligned(16)));typedef unsigned short uw __attribute__((vector_size(16)));"
          "typedef long long sq4 __attribute__((vector_size(32),aligned(16)));typedef int sd32 __attribute__((vector_size(128),aligned(16)));\n"
          "#define S(T,k,bits) T a##k,b##k;fill(&a##k,sizeof(T),n+k);fill(&b##k,sizeof(T),n+k+50);"
          "{T c_=b##k&(__typeof__(b##k[0]))(bits-1);T q_[]={a##k*b##k,a##k<<c_,a##k>>c_,a##k<<(n&(bits-1)),a##k>>3,a##k*(__typeof__(a##k[0]))n};a##k*=b##k;a##k<<=c_;"
          "FOLD(q_);FOLD(a##k);}\n"
          "int tested(int n){unsigned h=2166136261u;S(sb,1,8)S(v16qu,2,8)S(ub32,3,8)S(sb64,4,8)S(uw,5,16)S(v4si,6,32)S(sq4,7,64)S(sd32,8,32)return (int)h;}\n"
        },
        // Saturating sums and differences of 32- and 64-bit elements, element by element at every target (#2216): of 8 to 128
        // bytes and an odd length, at the extremes; and the integer elementwise builtins of scalars, of each width and
        // signedness, plain char's too: add_sat, sub_sat, abs, max, min, popcount, bitreverse, clzg and ctzg with their
        // fallbacks, fshl and fshr.
        { "baseline-integer-elements",
          "typedef unsigned ud8 __attribute__((vector_size(32),aligned(16)));typedef long long sq8 __attribute__((vector_size(64),aligned(16)));"
          "typedef int sd2 __attribute__((vector_size(8)));typedef int sd32 __attribute__((vector_size(128),aligned(16)));"
          "typedef unsigned long long ux3 __attribute__((ext_vector_type(3)));\n"
          "#define V(T,k) {T a,b;fill(&a,sizeof(T),n+k);fill(&b,sizeof(T),n+k+50);a[0]=(__typeof__(a[0]))(1ull<<(8*sizeof(a[0])-1));b[0]=a[0];"
          "a[1]=(__typeof__(a[0]))-1;b[1]=1;a[sizeof(T)/sizeof(a[0])-1]=(__typeof__(a[0]))~a[0];b[sizeof(T)/sizeof(a[0])-1]=(__typeof__(a[0]))~a[0];"
          "T q_[]={__builtin_elementwise_add_sat(a,b),__builtin_elementwise_sub_sat(a,b),__builtin_elementwise_sub_sat(b,a)};FOLD(q_);}\n"
          "#define S(T,k) {T a=(T)((unsigned long long)n*0x9e3779b97f4a7c15ull>>k),b=(T)(1ull<<(8*sizeof(T)-1)),c=(T)-1;"
          "T q_[]={__builtin_elementwise_add_sat(a,b),__builtin_elementwise_add_sat(b,b),__builtin_elementwise_add_sat(c,a),__builtin_elementwise_sub_sat(a,c),"
          "__builtin_elementwise_sub_sat(b,a),__builtin_elementwise_max(a,b),__builtin_elementwise_min(a,c),__builtin_elementwise_popcount(a),"
          "__builtin_elementwise_bitreverse(a),__builtin_elementwise_clzg(a,b),__builtin_elementwise_ctzg((T)(a&~a),c),__builtin_elementwise_ctzg(b),"
          "__builtin_elementwise_fshl(a,c,(T)k),__builtin_elementwise_fshr(a,b,(T)(k*3))};FOLD(q_);}\n"
          "#define A(T,k) S(T,k) {T a=(T)((unsigned long long)n*0x9e37ull>>k),b=(T)(1ull<<(8*sizeof(T)-1));"
          "T q_[]={__builtin_elementwise_abs(a),__builtin_elementwise_abs(b),__builtin_elementwise_abs((T)-a)};FOLD(q_);}\n"
          "int tested(int n){unsigned h=2166136261u;V(v4si,1)V(ud8,2)V(v2di,3)V(sq8,4)V(sd2,5)V(sd32,7)\n"
          "{ux3 a={~0ull,1ull<<63,(unsigned long long)n},b={1,1ull<<63,7};ux3 s=__builtin_elementwise_add_sat(a,b),d=__builtin_elementwise_sub_sat(b,a);"
          "unsigned long long o[]={s[0],s[1],s[2],d[0],d[1],d[2]};FOLD(o);}\n"
          "A(signed char,1)S(unsigned char,2)A(short,3)S(unsigned short,4)A(int,5)S(unsigned,6)A(long long,7)S(unsigned long,8)S(char,9)return (int)h;}\n"
        },
        { "baseline-floating-extremes", check_test_vector_floating_extremes },
        // AVX's and AVX2's (x86-64-v3's): 256-bit unpacks, broadcasts, permutes, variable shifts, lanes' extractions and
        // insertions, extensions, gathers, FMA's (of no NaN) and F16C's conversions, and shuffles of dwords and qwords.
        { "baseline-avx2",
          "#include <immintrin.h>\n"
          "int tested(int n){unsigned h=2166136261u;\n"
          "#ifdef __AVX2__\n"
          "__m256i a,b;__m256 f,g,k;__m256d d,e;__m128i x;__m128 y;fill(&a,32,n);fill(&b,32,n+1);fill(&x,16,n+2);fillf((float*)&f,8,n);fillf((float*)&g,8,n+1);"
          "fillf((float*)&k,8,n+2);filld((double*)&d,4,n);filld((double*)&e,4,n+3);fillf((float*)&y,4,n+4);"
          "int idx[16];float tab[16];for(int i=0;i<16;i++){idx[i]=(i*7+n)&15;tab[i]=(float)i*1.5f;}__m256i lane=_mm256_and_si256(a,_mm256_set1_epi32(15));\n"
          "__m128i z;fill(&z,16,n+5);__v4di wide[]={__builtin_shufflevector((__v2di)x,(__v2di)z,0,1,0,3),__builtin_shufflevector((__v2di)x,(__v2di)z,0,1,2,1),"
          "__builtin_shufflevector((__v2di)x,(__v2di)z,0,1,2,3),__builtin_shufflevector((__v2di)x,(__v2di)z,0,1,0,1),"
          "__builtin_shufflevector((__v2di)x,(__v2di)z,0,1,3,2)};FOLD(wide);__v8si dw=__builtin_shufflevector((__v4si)x,(__v4si)z,0,1,2,3,4,5,2,7);FOLD(dw);"
          "__v4di sparse=__builtin_shufflevector((__v2di)x,(__v2di)z,0,1,-1,3),sparse2=__builtin_shufflevector((__v2di)x,(__v2di)z,0,1,2,-1);"
          "long long kept[]={sparse[0],sparse[1],sparse[3],sparse2[0],sparse2[1],sparse2[2]};FOLD(kept);\n"
          "__m256i r[]={_mm256_unpacklo_epi8(a,b),_mm256_unpackhi_epi16(a,b),_mm256_unpacklo_epi32(a,b),_mm256_unpackhi_epi64(a,b),_mm256_broadcastb_epi8(x),"
          "_mm256_broadcastw_epi16(x),_mm256_broadcastd_epi32(x),_mm256_broadcastq_epi64(x),_mm256_broadcastsi128_si256(x),_mm256_permutevar8x32_epi32(a,b),"
          "_mm256_sllv_epi32(a,lane),_mm256_srlv_epi64(a,_mm256_and_si256(b,_mm256_set1_epi64x(63))),_mm256_srav_epi32(a,b),_mm256_permute4x64_epi64(a,0x1b),"
          "_mm256_inserti128_si256(a,x,1),_mm256_cvtepi8_epi16(x),_mm256_cvtepu16_epi64(x),_mm256_shuffle_epi32(a,0x1b),_mm256_shufflehi_epi16(a,0x4e),"
          "_mm256_slli_si256(a,3),_mm256_bsrli_epi128(b,5),_mm256_i32gather_epi32(idx,lane,4),_mm256_maddubs_epi16(a,b),_mm256_mulhrs_epi16(a,b),"
          "_mm256_packus_epi32(a,b),(__m256i)__builtin_shufflevector((__v8si)a,(__v8si)b,0,9,2,11,12,5,14,7),"
          "(__m256i)__builtin_shufflevector((__v4di)a,(__v4di)b,3,6,1,4)};FOLD(r);\n"
          "__m256 s[]={_mm256_fmadd_ps(f,g,k),_mm256_fmsub_ps(f,g,k),_mm256_fmaddsub_ps(f,g,k),_mm256_permutevar_ps(f,a),_mm256_permute_ps(f,0x1b),"
          "_mm256_shuffle_ps(f,g,0x4e),_mm256_permutevar8x32_ps(f,b),_mm256_cvtph_ps(x),_mm256_insertf128_ps(f,y,0),_mm256_i32gather_ps(tab,lane,4)};FOLD(s);\n"
          "__m256d t[]={_mm256_fmadd_pd(d,e,d),_mm256_permute4x64_pd(d,0x4e),_mm256_shuffle_pd(d,e,5),_mm256_permute_pd(d,5)};FOLD(t);\n"
          "__m128 u[]={_mm256_extractf128_ps(f,1),_mm_fmadd_ps(y,y,y),_mm_cvtph_ps(x),_mm_permutevar_ps(y,x)};FOLD(u);"
          "__m128i v[]={_mm256_extracti128_si256(a,1),_mm_sllv_epi32(x,_mm_and_si128(x,_mm_set1_epi32(31)))};FOLD(v);"
          "int c[]={_mm256_extract_epi32(a,5),_mm256_extract_epi8(b,30),_mm256_testz_si256(a,b)};FOLD(c);\n"
          "#endif\n"
          "return (int)h;}\n"
        },
    };
    const X86Target native = x86_target_native();
    for (uint32_t level = 0; level < BUSTER_ARRAY_LENGTH(levels); level += 1)
    {
        X86Target target = { 0 };
        const bool parsed = x86_target_parse(levels[level], strlen(levels[level]), &target);
        check_test_expect(test, parsed, "baseline vector target");
        check_test_program_build_release();
        check_test_program_march = marches[level];
        for (uint32_t i = 0; parsed && i < BUSTER_ARRAY_LENGTH(programs); i += 1)
        {
            const size_t length = strlen(check_test_vector_types) + strlen(programs[i][1]) + 1;
            char* source = malloc(length);
            check_test_expect(test, source != NULL, "baseline vector source");
            if (source != NULL)
            {
                snprintf(source, length, "%s%s", check_test_vector_types, programs[i][1]);
                check_test_program_includes = strstr(programs[i][1], "#include") != NULL;
                check_test_program_source_native_limits(test, source, programs[i][0], clang, false, -1, 0, &target,
                                                         (native.features & target.features) == target.features);
                check_test_program_includes = false;
                free(source);
            }
        }
        if (parsed && level == 2)
        {
            const char* wide =
                "typedef unsigned U __attribute__((vector_size(32)));typedef int I __attribute__((vector_size(32)));"
                "typedef unsigned long long Q __attribute__((vector_size(32)));typedef float F __attribute__((vector_size(32)));"
                "int tested(int n){U a,b;Q q,r;F f,g;unsigned h=2166136261u;fill(&a,32,n);fill(&b,32,n+1);fill(&q,32,n+2);fill(&r,32,n+3);fillf((float*)&f,8,n);fillf((float*)&g,8,n+2);g+=1.0f;"
                "U z[]={a+b,a-b,a*b,a^b,~a,a<<((U)b&31),a>>((U)b&31),(U)(a<b),(U)(a>=b),a+3u};FOLD(z);"
                "Q k[]={q+r,q-r,(Q)(q<r),(Q)(q>=r),q+(unsigned long long)n,q>>(r&63)};FOLD(k);"
                "F v[]={f+g,f-g,f*g,f/g,-f,f+2.0f};FOLD(v);I c[]={f<g,f>=g};FOLD(c);return (int)h;}";
            char source[4096];
            snprintf(source, sizeof(source), "%s%s", check_test_vector_types, wide);
            check_test_program_source_native_limits(test, source, "baseline-avx2-256", clang, false, -1, 0, &target,
                                                     (native.features & target.features) == target.features);
        }
        if (parsed)
        {
            // The builtins the baseline's rows still refuse by name (#775) are c_check_program_vector_builtins_test.c's
            // refusals; the operators' and shuffles' sequences take every one (#1834).
        }
    }
    check_test_program_build_release();
    check_test_program_march = "-march=x86-64";
    X86Target baseline = { 0 };
    x86_target_parse("x86-64", strlen("x86-64"), &baseline);
    const char* avx =
        "typedef float F __attribute__((vector_size(32)));typedef int I __attribute__((vector_size(32)));"
        "__attribute__((target(\"avx,no-avx2\"))) int tested(int n){F f,g;unsigned h=2166136261u;fillf((float*)&f,8,n);fillf((float*)&g,8,n+1);g+=1.0f;"
        "F v[]={f+g,f-g,f*g,f/g,-f,f+2.5f};FOLD(v);I c[]={f<g,f>=g,f==g,f!=g};FOLD(c);"
        "I a=(I)f,b=(I)g;I k[]={a&b,a|b,a^b};FOLD(k);"
        "F s[]={__builtin_shufflevector(f,g,7,6,5,4,3,2,1,0),__builtin_shufflevector(f,g,0,8,1,9,12,13,2,15)};FOLD(s);"
        "typedef char C __attribute__((vector_size(32)));C x=(C)a,y=(C)b;"
        "C t[]={__builtin_shufflevector(x,y,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)};FOLD(t);return (int)h;}";
    char source[4096];
    snprintf(source, sizeof(source), "%s%s", check_test_vector_types, avx);
    check_test_program_source_native_limits(test, source, "baseline-avx-without-avx2", clang, false, -1, 0, &baseline,
                                             (native.features & BUSTER_X86_FEATURE(AVX)) != 0);
    check_test_program_boundary_target(test,
        "typedef int V __attribute__((vector_size(32)));__attribute__((target(\"avx,no-avx2\"))) int tested(V a,V b){V c;c=a+b;return c[0];}",
        "baseline-avx-rejects-avx2-arithmetic", true, false, 0, 0, "vector operator unavailable at this target", &baseline);
    check_test_program_build_release();
}

static __attribute__((noinline)) void check_test_program_vectors(CheckTest* test, const char* clang)
{
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &target);
    check_test_expect(test, parsed, "the vector programs' target, x86-64-v4");
    if (parsed)
    {
        // The section's own -march for clang's builds, from a fresh build directory, and the default's after it.
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64-v4";
        check_test_program_vector_sources(test, clang, &target);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
        check_test_program_vector_refusals(test, &target);
        check_test_program_segment_refusals(test, &target);
        check_test_program_segments(test, clang);
        check_test_program_atomic_vector_constraints(test, clang);
        check_test_program_vector_baselines(test, clang);
    }
}
