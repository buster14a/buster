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
            "vector-multiply-bytes",
            "typedef char v16qi __attribute__((vector_size(16)));int tested(int n){v16qi a={(char)n},b={2};a=a*b;return a[0];}",
            "vector multiplication of 8-bit elements",
            2,
        },
        {
            "vector-divide-integers",
            "typedef int v4si __attribute__((vector_size(16)));int tested(int n){v4si a={n},b={2};a=a/b;return a[0];}",
            "vector division of integers",
            2,
        },
        {
            "vector-shift-bytes",
            "typedef unsigned char v16qu __attribute__((vector_size(16)));int tested(int n){v16qu a={(unsigned char)n},b={1};a=a<<b;return a[0];}",
            "vector shifts of 8-bit elements",
            2,
        },
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
            "vector-divide-integers-past-64-bytes",
            "typedef int w32 __attribute__((vector_size(128)));int tested(int n){w32 a={n},b={2};a=a/b;return a[0];}",
            "vector division of integers",
            2,
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
        // and of three elements and one, unpacks, PSHUFLW's and PSHUFHW's, and with SSSE3 PSHUFB's of any bytes; narrow
        // results and operands.
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
          "__builtin_shufflevector(w,x,11,10,9,8,15,14,13,12),__builtin_shufflevector(w,x,2,3,0,1,6,7,4,5)\n"
          "#ifdef __SSSE3__\n,__builtin_shufflevector(w,x,7,8,1,15,4,4,9,0)\n#endif\n};FOLD(m);\n"
          "v16qi o[]={__builtin_shufflevector(y,z,0,16,1,17,2,18,3,19,4,20,5,21,6,22,7,23),__builtin_shufflevector(z,y,8,24,9,25,10,26,11,27,12,28,13,29,14,30,15,31),"
          "__builtin_shufflevector(y,z,2,3,0,1,6,7,4,5,10,11,8,9,14,15,12,13)\n"
          "#ifdef __SSSE3__\n,__builtin_shufflevector(y,z,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0),__builtin_shufflevector(y,z,0,17,2,19,31,5,6,7,16,9,3,3,12,30,14,1)\n"
          "#endif\n};FOLD(o);\n"
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
        // __builtin_elementwise_min, _max and _abs of integers without their instructions (#1834): SSE2's compares and
        // blends, unsigned elements biased by their sign bits, unsigned words' saturating subtractions, bytes' and words'
        // absolute values of each and its negation and the others' by their signs; qwords' minima and maxima with SSE4.2's
        // PCMPGTQ, 32 bytes with AVX2. Each pair has equal elements and the most negative one.
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
          "#ifdef __SSE4_2__\n"
          "M(v2di,a7,b7);M(uq,a8,b8);\n"
          "#endif\n"
          "#ifdef __AVX2__\n"
          "V(sb32,12)V(uw16,13)V(ud8,14)V(v4di,15)V(uq4,16)V(ub32,17)\n"
          "M(sb32,a12,b12);M(uw16,a13,b13);M(ud8,a14,b14);M(v4di,a15,b15);M(uq4,a16,b16);M(ub32,a17,b17);A(sb32,a12);A(v4di,a15);\n"
          "#endif\n"
          "return (int)h;}\n"
        },
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
            // A wide operator's pieces are refused by their own reason (#1193).
            check_test_program_boundary_target(test, "typedef int V __attribute__((vector_size(64)));V tested(V a,V b){return a/b;}",
                                               "baseline-wide-integer-vector-division", true, false, 0, 0, "vector division of integers", &target);
            check_test_program_boundary_target(test, "typedef int V __attribute__((vector_size(16)));V tested(V a,V b){return a/b;}",
                                               "baseline-integer-vector-division", true, false, 0, 0, "vector division of integers", &target);
            // Without AVX-512, what the baseline's rows refuse by name (#775), at each level that hasn't the instructions.
            static const struct
            {
                uint32_t levels;
                const char* source;
                const char* reason;
            } refused[] = {
                { 1, "typedef char V __attribute__((vector_size(16)));V tested(V a,V b){return __builtin_shufflevector(a,b,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);}",
                  "vector shuffles of 8-bit elements without SSSE3" },
                { 1, "typedef short V __attribute__((vector_size(16)));V tested(V a,V b){return __builtin_shufflevector(a,b,7,8,1,15,4,4,9,0);}",
                  "vector shuffles of 16-bit elements without SSSE3" },
                { 1, "float tested(float a,float b,float c){return __builtin_elementwise_fma(a,b,c);}", "fused multiply-adds of scalars without FMA or AVX-512F" },
                { 1, "typedef long long V __attribute__((vector_size(16)));V tested(V a,V b){return __builtin_elementwise_max(a,b);}",
                  "vector minimum and maximum of 64-bit elements without SSE4.2" },
                { 3, "typedef int V __attribute__((vector_size(32)));typedef short N __attribute__((vector_size(16)));N tested(V a){return __builtin_convertvector(a,N);}",
                  "vector conversions wider than the target's registers" },
                { 4, "typedef char V __attribute__((vector_size(32)));V tested(V a,V b){return __builtin_shufflevector(a,b,31,30,29,28,27,26,25,24,23,22,21,20,19,"
                     "18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);}",
                  "vector shuffles of 32-byte vectors of 8- and 16-bit elements without AVX-512" },
            };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
            {
                if ((refused[i].levels >> level & 1) != 0)
                {
                    check_test_program_boundary_target(test, refused[i].source, "baseline-refusal", true, false, 0, 0, refused[i].reason, &target);
                }
            }
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
        "I a=(I)f,b=(I)g;I k[]={a&b,a|b,a^b};FOLD(k);return (int)h;}";
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
        check_test_program_atomic_vector_constraints(test, clang);
        check_test_program_vector_baselines(test, clang);
    }
}
