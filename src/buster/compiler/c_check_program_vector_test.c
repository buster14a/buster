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
// exchanges and compare-exchanges and calls both ways. #775 also covers SSE2/v2/v3 and AVX-only function targets.
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
            "vector-of-2-bytes",
            "typedef char v2qi __attribute__((vector_size(2)));int tested(int n){v2qi a={(char)n},b={2};a=b;return a[0];}",
            "vector types of other than 4, 8, 16, 32 or 64 bytes",
            2,
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
            "vector-logical-not",
            "typedef int V __attribute__((ext_vector_type(4)));int tested(int n){V a={n,0,1,2};return (!a)[0];}",
            "vector logical operators and conditions",
            1,
        },
        {
            "vector-logical-and",
            "typedef int V __attribute__((ext_vector_type(4)));int tested(int n){V a={n,0,1,2},b={1,n,0,1};return (a&&b)[0];}",
            "vector logical operators and conditions",
            2,
        },
        {
            "vector-logical-or",
            "typedef int V __attribute__((ext_vector_type(4)));int tested(int n){V a={n,0,1,2},b={1,n,0,1};return (a||b)[0];}",
            "vector logical operators and conditions",
            2,
        },
        {
            "vector-condition",
            "typedef int V __attribute__((ext_vector_type(4)));int tested(int n){V a={n,0,1,2},b={1,n,0,1};return (a?a:b)[0];}",
            "vector logical operators and conditions",
            2,
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
          "int tested(int n){unsigned h=2166136261u;struct S{C v;unsigned guard;} s={{1,2,3,(unsigned char)n},0xdeadbeef};\n"
          "C t=s.v+2;s.v=t;FOLD(s);V a={n,-n},b=narrow(a);V out[2]={a,b};out[0]=out[1];FOLD(out);\n"
          "v16su x;fill(&x,64,n);wide(x);v16su z=hold;FOLD(z);return (int)h;}\n"
        },
        { "baseline-floating-pressure",
          "static double consume(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j,double k,double l,double m,double o,double p,double q,v4si v){return a+b+c+d+e+f+g+h+i+j+k+l+m+o+p+q+v[1];}\n"
          "int tested(int n){v4si a={n,1,2,3},b={4,n,5,6};return (int)consume(n,n+1,n+2,n+3,n+4,n+5,n+6,n+7,n+8,n+9,n+10,n+11,n+12,n+13,n+14,n+15,a*b+a);}\n"
        },
        { "baseline-sse-builtins",
          "#include <emmintrin.h>\n"
          "int tested(int n){unsigned h=2166136261u;__m128i a,b;__m128 f,g;fill(&a,16,n);fill(&b,16,n+1);fill(&f,16,n+2);fill(&g,16,n+3);\n"
          "__m128i r[]={_mm_add_epi32(a,b),_mm_shuffle_epi32(a,0x1b),_mm_shufflehi_epi16(a,0x4e),_mm_shufflelo_epi16(b,0xb1),_mm_slli_si128(a,3),_mm_srli_si128(a,15),_mm_srli_si128(b,16),_mm_packs_epi32(a,b),_mm_packus_epi16(a,b),_mm_madd_epi16(a,b),_mm_mulhi_epi16(a,b),_mm_mul_epu32(a,b),_mm_insert_epi16(a,n*31,6),_mm_cvtps_epi32(f),_mm_slli_epi16(a,n&15),_mm_srli_epi16(b,n&15),_mm_srai_epi16(a,n&15),_mm_slli_epi32(a,n&31),_mm_srli_epi32(b,n&31),_mm_srai_epi32(a,n&31),_mm_slli_epi64(a,n&63),_mm_srli_epi64(b,n&63)};FOLD(r);\n"
          "__m128 m[]={_mm_shuffle_ps(f,g,0x1b),_mm_max_ps(f,g),_mm_min_ps(f,g)};FOLD(m);int e[]={_mm_extract_epi16(a,5),_mm_extract_epi16(b,0)};FOLD(e);return (int)h;}\n"
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
                check_test_program_includes = i + 1 == BUSTER_ARRAY_LENGTH(programs);
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
            check_test_program_boundary_target(test, "typedef int V __attribute__((vector_size(64)));int tested(V a,V b){V c;c=a+b;return c[0];}",
                                               "baseline-wide-vector-operator", true, false, 0, 0,
                                               "vector operations wider than the target's registers", &target);
            check_test_program_boundary_target(test, "typedef int V __attribute__((vector_size(64)));V tested(V a){return a;}",
                                               "baseline-wide-vector-result", true, false, 0, 0,
                                               "vector results wider than the target's registers", &target);
            check_test_program_boundary_target(test, "typedef int V __attribute__((vector_size(16)));V tested(V a,V b){return a/b;}",
                                               "baseline-integer-vector-division", true, false, 0, 0, "vector division of integers", &target);
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
