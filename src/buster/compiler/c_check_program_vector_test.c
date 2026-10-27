// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's vectors in the lowering, against clang -O0's code of the same source for x86-64-v4 (AVX-512F, BW, DQ and VL):
// every operator at each element type and width, calls passing and returning vectors in registers, elements, literals,
// splats, casts, conditionals, globals, arrays and members of vector type. Their types are aligned to 16 at most, as
// Tier 0's frame objects are until it realigns a frame. Then what the lowering refuses by name: vectors on x86-64,
// operators AVX-512 has no instruction for, compound assignments, other sizes and over-aligned locals. Include after
// c_check_program_test.c's runners.

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

static void check_test_program_vector_sources(CheckTest* test, const char* clang, const X86Target* target)
{
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
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        const size_t length = strlen(check_test_vector_types) + strlen(programs[i][1]) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "vector program source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s", check_test_vector_types, programs[i][1]);
            check_test_program_source_char(test, source, programs[i][0], clang, false, target);
            free(source);
        }
    }
}

// What the lowering refuses by name, with vectors of each kind it can't take yet.
static void check_test_program_vector_refusals(CheckTest* test, const X86Target* target)
{
    static const char* const refused[][3] = {
        {
            "vector-multiply-bytes",
            "typedef char v16qi __attribute__((vector_size(16)));int tested(int n){v16qi a={(char)n},b={2};a=a*b;return a[0];}",
            "vector multiplication of 8-bit elements",
        },
        {
            "vector-divide-integers",
            "typedef int v4si __attribute__((vector_size(16)));int tested(int n){v4si a={n},b={2};a=a/b;return a[0];}",
            "vector division of integers",
        },
        {
            "vector-shift-bytes",
            "typedef unsigned char v16qu __attribute__((vector_size(16)));int tested(int n){v16qu a={(unsigned char)n},b={1};a=a<<b;return a[0];}",
            "vector shifts of 8-bit elements",
        },
        {
            "vector-compound-assignment",
            "typedef int v4si __attribute__((vector_size(16)));int tested(int n){v4si a={n},b={2};a+=b;return a[0];}",
            "compound assignments of vectors",
        },
        {
            "vector-of-8-bytes",
            "typedef int v2si __attribute__((vector_size(8)));int tested(int n){v2si a={n},b={2};a=b;return a[0];}",
            "vector types of other than 16, 32 or 64 bytes",
        },
        {
            "vector-aligned-local",
            "typedef int v16si __attribute__((vector_size(64)));int tested(int n){v16si a={n},b={2};a=b;return a[0];}",
            "local objects aligned past 16 bytes or larger than 1 GB",
        },
        {
            "vector-integer-cast",
            "typedef int v4si __attribute__((vector_size(16)));int tested(int n){v4si a={n};__int128 x=(__int128)a;return (int)x;}",
            "casts between vectors and integers or vectors of other sizes",
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
    {
        check_test_program_boundary_target(test, refused[i][1], refused[i][0], true, false, 0, 2, refused[i][2], target);
    }
    // Without AVX-512F and AVX-512VL, a vector value of any kind.
    check_test_program_boundary_reason(test, "typedef int v4si __attribute__((vector_size(16)));int tested(int n){v4si a={n},b={2};a=b;return a[0];}",
                                       "vector-without-avx512", true, false, 0, 2, "vector types without AVX-512F and AVX-512VL");
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
    }
}
