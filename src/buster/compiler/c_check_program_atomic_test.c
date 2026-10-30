// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's atomic lowering against clang's code of the same source: every read/modify/write kind at each width through
// the leaves, the legacy __sync forms (their sized forms too), floating loads, stores and exchanges, which cross the
// helpers as bits, objects reached through members, elements and casts (c_checker.c's check_graph_atomic_alignment),
// the wrapping increment and decrement, the byte flag, the lock-free queries, GNU's generic forms, the scoped families
// and the IEEE maximum and minimum. Then the accesses clang calls libatomic for, which the lowering refuses as
// misaligned, and no others. Include after c_check_program_test.c's runners.

static const char check_test_atomic_fold[] =
    "static unsigned fold(unsigned h,unsigned long long v){return (h^(unsigned)v^(unsigned)(v>>32))*16777619u;}\n";

// The programs: each `tested(n)` runs atomic operations on objects it sets from n, and folds every result and object.
static void check_test_program_atomic_sources(CheckTest* test, const char* clang)
{
    static const char* const programs[][2] = {
        {
            "atomic-read-modify-write",
            "static signed char c8;static unsigned short u16;static int i32;static unsigned long u64v;static long s64;"
            "static _Bool flag;static char buffer[32];static char*pointer;\n"
            "int tested(int n){unsigned h=2166136261u;"
            "c8=(signed char)(n*37);u16=(unsigned short)(n*4099);i32=n*100003;u64v=(unsigned long)n*0x9e3779b97f4a7c15ul;s64=-(long)n*7;"
            "h=fold(h,(unsigned long long)__atomic_fetch_add(&c8,(signed char)n,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_add_fetch(&u16,77,__ATOMIC_RELAXED));"
            "h=fold(h,(unsigned long long)__atomic_fetch_sub(&i32,n,__ATOMIC_ACQ_REL));"
            "h=fold(h,__atomic_sub_fetch(&u64v,3,__ATOMIC_RELEASE));"
            "h=fold(h,(unsigned long long)__atomic_fetch_and(&s64,~(long)n,__ATOMIC_ACQUIRE));"
            "h=fold(h,(unsigned long long)__atomic_or_fetch(&c8,0x41,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_fetch_xor(&u16,(unsigned short)n,__ATOMIC_SEQ_CST));"
            "h=fold(h,(unsigned long long)__atomic_nand_fetch(&i32,0x5555,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_fetch_nand(&u64v,0xff00ff,__ATOMIC_SEQ_CST));"
            "h=fold(h,(unsigned long long)__atomic_exchange_n(&s64,(long)n<<40,__ATOMIC_SEQ_CST));"
            "h=fold(h,(unsigned long long)__atomic_fetch_min(&c8,(signed char)(n-8),__ATOMIC_SEQ_CST));"
            "h=fold(h,(unsigned long long)__atomic_max_fetch(&i32,n*-3,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_min_fetch(&u16,(unsigned short)(n*1000),__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_fetch_max(&u64v,(unsigned long)n<<60,__ATOMIC_SEQ_CST));"
            "h=fold(h,(unsigned long long)__atomic_fetch_max(&s64,-(long)n,__ATOMIC_SEQ_CST));"
            "int e=i32;h=fold(h,__atomic_compare_exchange_n(&i32,&e,n,n&1,__ATOMIC_SEQ_CST,__ATOMIC_RELAXED));"
            "h=fold(h,(unsigned)e);h=fold(h,(unsigned)i32);"
            "int wrong=i32+1;h=fold(h,__atomic_compare_exchange_n(&i32,&wrong,5,0,__ATOMIC_ACQUIRE,__ATOMIC_ACQUIRE));"
            "h=fold(h,(unsigned)wrong);h=fold(h,(unsigned)i32);"
            "unsigned short half=u16;h=fold(h,__atomic_compare_exchange_n(&u16,&half,(unsigned short)~half,1,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));"
            "flag=n&1;h=fold(h,__atomic_exchange_n(&flag,!flag,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_fetch_or(&flag,1,__ATOMIC_SEQ_CST));h=fold(h,__atomic_and_fetch(&flag,n&2,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_xor_fetch(&flag,1,__ATOMIC_SEQ_CST));"
            "_Bool b=flag;h=fold(h,__atomic_compare_exchange_n(&flag,&b,0,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));h=fold(h,flag);"
            "pointer=buffer+(n&7);h=fold(h,(unsigned long long)(__atomic_fetch_add(&pointer,5,__ATOMIC_SEQ_CST)-buffer));"
            "h=fold(h,(unsigned long long)(__atomic_sub_fetch(&pointer,2,__ATOMIC_SEQ_CST)-buffer));"
            "h=fold(h,(unsigned long long)(__atomic_exchange_n(&pointer,buffer+n%31,__ATOMIC_SEQ_CST)-buffer));"
            "char*old=pointer;h=fold(h,__atomic_compare_exchange_n(&pointer,&old,buffer,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));"
            "h=fold(h,(unsigned long long)(pointer-buffer));"
            "h=fold(h,(unsigned long long)c8);h=fold(h,u16);h=fold(h,(unsigned)i32);h=fold(h,u64v);h=fold(h,(unsigned long long)s64);"
            "return (int)h;}",
        },
        {
            "legacy-atomics",
            "#pragma clang diagnostic ignored \"-Wsync-fetch-and-nand-semantics-changed\"\n"
            "static int counter;static unsigned char byte;static long cell;static unsigned short lock;\n"
            "int tested(int n){unsigned h=2166136261u;counter=n*13;byte=(unsigned char)(n*91);cell=(long)n<<33;lock=(unsigned short)n;"
            "h=fold(h,(unsigned)__sync_fetch_and_add(&counter,n));h=fold(h,__sync_fetch_and_sub(&byte,7));"
            "h=fold(h,(unsigned long long)__sync_fetch_and_or(&cell,n));h=fold(h,(unsigned)__sync_fetch_and_and(&counter,0x3c3c));"
            "h=fold(h,__sync_fetch_and_xor(&byte,(unsigned char)n));h=fold(h,(unsigned long long)__sync_fetch_and_nand(&cell,0xf0f0));"
            "h=fold(h,(unsigned)__sync_add_and_fetch(&counter,1000));h=fold(h,__sync_sub_and_fetch(&byte,1));"
            "h=fold(h,(unsigned long long)__sync_or_and_fetch(&cell,1));h=fold(h,(unsigned)__sync_and_and_fetch(&counter,~n));"
            "h=fold(h,__sync_xor_and_fetch(&byte,0x80));h=fold(h,(unsigned long long)__sync_nand_and_fetch(&cell,n));"
            "h=fold(h,__sync_bool_compare_and_swap(&counter,counter,n));h=fold(h,__sync_bool_compare_and_swap(&counter,n+1,7));"
            "h=fold(h,(unsigned long long)__sync_val_compare_and_swap(&cell,cell,-1L));h=fold(h,(unsigned long long)__sync_val_compare_and_swap(&cell,3,4));"
            "h=fold(h,__sync_lock_test_and_set(&lock,(unsigned short)(n+500)));__sync_lock_release(&lock);"
            "h=fold(h,(unsigned)counter);h=fold(h,byte);h=fold(h,(unsigned long long)cell);h=fold(h,lock);"
            "return (int)h;}",
        },
        {
            "floating-atomics",
            "static float f;static double d;\n"
            "static unsigned bits(float x){union{float f;unsigned u;}v={x};return v.u;}\n"
            "static unsigned long long dbits(double x){union{double d;unsigned long long u;}v={x};return v.u;}\n"
            "int tested(int n){unsigned h=2166136261u;f=(float)n*0.75f;d=n==3?-0.0:(double)n/3.0;"
            "float old=__atomic_exchange_n(&f,-(float)n,__ATOMIC_SEQ_CST);double oldd=__atomic_exchange_n(&d,(double)n*1e300,__ATOMIC_ACQ_REL);"
            "h=fold(h,bits(old));h=fold(h,dbits(oldd));h=fold(h,bits(f));h=fold(h,dbits(d));"
            "__atomic_store_n(&f,old*2.0f,__ATOMIC_RELEASE);__atomic_store_n(&d,n==5?__builtin_nan(\"7\"):oldd-1.0,__ATOMIC_SEQ_CST);"
            "float now=__atomic_load_n(&f,__ATOMIC_ACQUIRE);double nowd=__atomic_load_n(&d,__ATOMIC_RELAXED);"
            "h=fold(h,bits(now));h=fold(h,dbits(nowd));return (int)h;}",
        },
        {
            "atomic-object-paths",
            "typedef struct{char tag;unsigned short half;unsigned word;unsigned long long wide;}Cell;\n"
            "typedef struct{Cell cells[4];Cell*next;}Table;\n"
            "static Table table;static long words[8];\n"
            "int tested(int n){unsigned h=2166136261u;Table*t=&table;Cell*c=&t->cells[n&3];"
            "for(int i=0;i<4;i++){table.cells[i]=(Cell){(char)i,(unsigned short)(i*7),(unsigned)(i*n),(unsigned long long)i<<40};}"
            "for(int i=0;i<8;i++){words[i]=i*n;}"
            "__atomic_store_n(&c->half,(unsigned short)(n*3),__ATOMIC_RELAXED);"
            "h=fold(h,__atomic_fetch_add(&t->cells[(n+1)&3].word,(unsigned)n,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_fetch_or(&table.cells[2].wide,1ull<<(n&63),__ATOMIC_SEQ_CST));"
            "void*v=&words[n&7];h=fold(h,(unsigned long long)__atomic_fetch_add((long*)v,n,__ATOMIC_SEQ_CST));"
            "char*bytes=(char*)words;h=fold(h,(unsigned long long)__atomic_fetch_xor((long*)(bytes+8*((n+3)&7)),3,__ATOMIC_SEQ_CST));"
            "t->next=c;h=fold(h,__atomic_exchange_n(&t->next->word,9u,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_load_n(&(*c).wide,__ATOMIC_ACQUIRE));h=fold(h,__atomic_load_n(&words[(n*5)&7],__ATOMIC_ACQUIRE));"
            "h=fold(h,__atomic_load_n((unsigned char*)&c->tag,__ATOMIC_RELAXED));"
            "for(int i=0;i<4;i++){h=fold(h,table.cells[i].half);h=fold(h,table.cells[i].word);h=fold(h,table.cells[i].wide);}"
            "for(int i=0;i<8;i++){h=fold(h,(unsigned long long)words[i]);}return (int)h;}",
        },
        {
            "c11-atomic-builtins",
            "static _Atomic int a;static _Atomic unsigned short b;static _Atomic signed char c;static _Atomic unsigned long wide;"
            "static _Atomic float f;static int array[32];static _Atomic(int*)p;"
            "int tested(int n){unsigned h=2166136261u;__c11_atomic_init(&a,n);__c11_atomic_init(&b,(unsigned short)(n*73));"
            "h=fold(h,(unsigned)__c11_atomic_fetch_add(&a,3,0));h=fold(h,(unsigned)__c11_atomic_fetch_sub(&a,2,5));"
            "h=fold(h,(unsigned)__c11_atomic_fetch_and(&a,31,5));h=fold(h,(unsigned)__c11_atomic_fetch_or(&a,4,5));"
            "h=fold(h,(unsigned)__c11_atomic_fetch_xor(&a,7,5));h=fold(h,__c11_atomic_fetch_add(&b,(unsigned short)n,5));"
            "__c11_atomic_init(&c,(signed char)n);h=fold(h,(unsigned)__c11_atomic_fetch_min(&c,3,5));"
            "h=fold(h,(unsigned)__c11_atomic_fetch_max(&c,7,5));h=fold(h,(unsigned)__c11_atomic_fetch_nand(&c,5,5));"
            "__c11_atomic_init(&wide,(unsigned long)n<<40);h=fold(h,__c11_atomic_fetch_add(&wide,17,5));"
            "h=fold(h,__c11_atomic_fetch_max(&wide,29,5));h=fold(h,__c11_atomic_fetch_min(&wide,31,5));"
            "int e=__c11_atomic_load(&a,2);h=fold(h,__c11_atomic_compare_exchange_strong(&a,&e,n*2,5,0));"
            "e=999;h=fold(h,__c11_atomic_compare_exchange_weak(&a,&e,n,5,0));h=fold(h,(unsigned)e);"
            "h=fold(h,(unsigned)__c11_atomic_exchange(&a,n+9,5));__c11_atomic_store(&a,n+3,3);h=fold(h,(unsigned)__c11_atomic_load(&a,2));"
            "__c11_atomic_init(&f,(float)n+0.5f);h=fold(h,(unsigned)__c11_atomic_exchange(&f,(float)n+2.0f,5));"
            "float ef=(float)n+2.0f;h=fold(h,__c11_atomic_compare_exchange_strong(&f,&ef,(float)n+3.0f,5,5));"
            "h=fold(h,(unsigned)(int)__c11_atomic_fetch_add(&f,2.0f,5));h=fold(h,(unsigned)(int)__c11_atomic_fetch_sub(&f,1.0f,5));"
            "h=fold(h,(unsigned)(int)__c11_atomic_fetch_min(&f,4.0f,5));h=fold(h,(unsigned)(int)__c11_atomic_fetch_max(&f,6.0f,5));"
            "__c11_atomic_init(&p,array);h=fold(h,(unsigned long)(__c11_atomic_fetch_add(&p,3,5)-array));"
            "h=fold(h,(unsigned long)(__c11_atomic_fetch_sub(&p,2,5)-array));h=fold(h,(unsigned long)(__c11_atomic_load(&p,5)-array));"
            "__c11_atomic_thread_fence(5);__c11_atomic_signal_fence(5);return (int)h;}",
        },
        {
            "c11-atomic-strict-floating-min-max",
            "#pragma STDC FENV_ACCESS ON\n"
            "int change_daz(int);int clear_fp(void);int fp_state(void);unsigned fp_bitsf(float);unsigned long fp_bits(double);"
            "static _Atomic float f;static _Atomic double d;static int addresses,rhs_calls,orders;"
            "static _Atomic float*af(void){addresses++;return &f;}static _Atomic double*ad(void){addresses++;return &d;}"
            "static float rf(float x){rhs_calls++;return x;}static double rd(double x){rhs_calls++;return x;}"
            "static int order(void){orders++;return 5;}"
            "int tested(int n){const unsigned fs[]={1,0x80000001u,0x3f800000u,0xbf800000u,0};"
            "const unsigned long ds[]={1,0x8000000000000001ul,0x3ff0000000000000ul,0xbff0000000000000ul,0};"
            "union{unsigned u;float f;}fv={fs[(unsigned)n%5]};union{unsigned long u;double d;}dv={ds[(unsigned)n%5]};"
            "unsigned h=2166136261u;int old_daz=change_daz(n);addresses=rhs_calls=orders=0;"
            "__c11_atomic_init(&f,fv.f);clear_fp();float a=__c11_atomic_fetch_min(af(),rf(1.0f),order());"
            "h=fold(h,fp_state());h=fold(h,fp_bitsf(a));h=fold(h,fp_bitsf(__c11_atomic_load(&f,5)));"
            "__c11_atomic_init(&f,fv.f);clear_fp();a=__c11_atomic_fetch_max(af(),rf(-1.0f),order());"
            "h=fold(h,fp_state());h=fold(h,fp_bitsf(a));h=fold(h,fp_bitsf(__c11_atomic_load(&f,5)));"
            "__c11_atomic_init(&d,dv.d);clear_fp();double b=__c11_atomic_fetch_min(ad(),rd(1.0),order());"
            "h=fold(h,fp_state());h=fold(h,fp_bits(b));h=fold(h,fp_bits(__c11_atomic_load(&d,5)));"
            "__c11_atomic_init(&d,dv.d);clear_fp();b=__c11_atomic_fetch_max(ad(),rd(-1.0),order());"
            "h=fold(h,fp_state());h=fold(h,fp_bits(b));h=fold(h,fp_bits(__c11_atomic_load(&d,5)));"
            "h=fold(h,addresses);h=fold(h,rhs_calls);h=fold(h,orders);change_daz(old_daz);return (int)h;}",
        },
        {
            "c11-atomic-strict-floating-min-max-exceptions",
            "#pragma STDC FENV_ACCESS ON\n"
            "int clear_fp(void);int fp_state(void);unsigned fp_bitsf(float);unsigned long fp_bits(double);"
            "int tested(int n){union{unsigned u;float f;}fv={(n&1)?0x7f800001u:0x7fc00001u};"
            "union{unsigned long u;double d;}dv={(n&1)?0x7ff0000000000001ul:0x7ff8000000000001ul};"
            "_Atomic float f;_Atomic double d;unsigned h=2166136261u;float a;double b;"
            "__c11_atomic_init(&f,fv.f);clear_fp();a=__c11_atomic_fetch_min(&f,1.0f,5);"
            "h=fold(h,fp_state());h=fold(h,fp_bitsf(a));h=fold(h,fp_bitsf(__c11_atomic_load(&f,5)));"
            "__c11_atomic_init(&f,1.0f);clear_fp();a=__c11_atomic_fetch_max(&f,fv.f,5);"
            "h=fold(h,fp_state());h=fold(h,fp_bitsf(a));h=fold(h,fp_bitsf(__c11_atomic_load(&f,5)));"
            "__c11_atomic_init(&d,dv.d);clear_fp();b=__c11_atomic_fetch_min(&d,1.0,5);"
            "h=fold(h,fp_state());h=fold(h,fp_bits(b));h=fold(h,fp_bits(__c11_atomic_load(&d,5)));"
            "__c11_atomic_init(&d,1.0);clear_fp();b=__c11_atomic_fetch_max(&d,dv.d,5);"
            "h=fold(h,fp_state());h=fold(h,fp_bits(b));h=fold(h,fp_bits(__c11_atomic_load(&d,5)));return (int)h;}",
        },
        {
            "atomic-language-operations",
            "static _Atomic int a[4];static _Atomic signed char byte;static _Atomic unsigned short half;static _Atomic float f;"
            "static _Atomic int*last;static int calls;static _Atomic int*address(void){calls++;return last;}"
            "int tested(int n){unsigned h=2166136261u;_Atomic int local=n;last=&local;calls=0;"
            "h=fold(h,(unsigned)(*address()+=3));h=fold(h,(unsigned)(*address())++);h=fold(h,(unsigned)++*address());"
            "h=fold(h,(unsigned)(*address()-=2));h=fold(h,(unsigned)(*address())--);h=fold(h,(unsigned)--*address());"
            "h=fold(h,(unsigned)(*address()*=2));h=fold(h,(unsigned)(*address()^=n));h=fold(h,(unsigned)(*address()/=2));"
            "h=fold(h,(unsigned)(*address()%=7));h=fold(h,(unsigned)(*address()<<=1));h=fold(h,(unsigned)(*address()>>=1));"
            "h=fold(h,(unsigned)(*address()&=15));h=fold(h,(unsigned)(*address()|=2));h=fold(h,(unsigned)calls);"
            "byte=(signed char)n;half=(unsigned short)(n*19);h=fold(h,(unsigned)++byte);h=fold(h,(unsigned)half++);"
            "f=(float)n+0.5f;h=fold(h,(unsigned)(f+=1.5f));h=fold(h,(unsigned)(f*=2.0f));"
            "for(int i=0;i<4;i++){a[i]=n+i;h=fold(h,(unsigned)++a[i]);}return (int)h;}",
        },
        {
            "atomic-variable-stride-pointer",
            "int tested(int n){int k=(unsigned)n%5+1;int values[3][k];_Atomic(int(*)[k]) pointer=values;"
            "++pointer;++pointer;return (int)(((char*)pointer-(char*)values)/(k*sizeof(int)));}",
        },
        {
            "atomic-function-values",
            "struct Bytes{unsigned char a,b,c;};struct Floats{float a,b;};"
            "static int bytes(_Atomic(struct Bytes) x){struct Bytes v=x;return v.a+v.b*3+v.c*7;}"
            "static int floats(_Atomic(struct Floats) x){struct Floats v=x;return (int)(v.a*3+v.b*7);}"
            "static int complex_parameter(_Atomic(_Complex double) x){_Complex double v=x;return (int)(__real__ v*3+__imag__ v*7);}"
            "static _Atomic(struct Bytes) make(struct Bytes x){return x;}"
            "static _Atomic(struct Bytes) forward(struct Bytes x){return make(x);}"
            "static _Atomic(struct Bytes) choose(int n,struct Bytes x){if(n)return x;else return x;}"
            "static _Atomic int scalar(int n){return n;}"
            "int tested(int n){unsigned h=2166136261u;struct Bytes v={(unsigned char)n,5,7};struct Floats f={(float)n+0.5f,2.0f};"
            "_Atomic(struct Bytes) a=make(v);a=forward(v);a=choose(n,v);struct Bytes b=a;h=fold(h,b.a+b.c*11u);"
            "h=fold(h,(unsigned)bytes(v));h=fold(h,(unsigned)bytes(make(v)));h=fold(h,(unsigned)floats(f));"
            "h=fold(h,(unsigned)complex_parameter((double)n+2.0i));_Atomic int q=scalar(n);q=scalar(n+1);h=fold(h,(unsigned)q);"
            "unsigned __int128 k=(unsigned)n&3;_Atomic unsigned long word=8;word<<=k;h=fold(h,word);word>>=k;h=fold(h,word);"
            "_Atomic unsigned __int128 wide=3;wide<<=k;h=fold(h,(unsigned)wide);return (int)h;}",
        },
        {
            "atomic-representation-classes",
            "struct Tiny{unsigned char a,b,c;};struct Big{unsigned long a,b,c;};"
            "static _Atomic(struct Tiny) tiny=(struct Tiny){1,2,3};static _Atomic(struct Big) big;"
            "static _Atomic(unsigned __int128) wide;static _Atomic long double extended;static _Atomic(_Complex double) z;"
            "static _Atomic double real;static _Atomic _Bool bit;"
            "struct __attribute__((packed)) Packed{char tag;_Atomic short x;};static struct Packed packed;"
            "_Static_assert(__c11_atomic_is_lock_free(0),\"zero size query\");"
            "_Static_assert(__c11_atomic_is_lock_free(4),\"native word query\");"
            "static int parameter(_Atomic int x){x+=2;return x;}"
            "int tested(int n){unsigned h=2166136261u;struct Tiny a={(unsigned char)n,5,7},b={(unsigned char)(n+1),9,10};"
            "tiny=a;a=tiny;h=fold(h,a.a+a.b*3u+a.c*11u);a=__c11_atomic_exchange(&tiny,b,5);h=fold(h,a.a+a.b*13u);"
            "a=b;h=fold(h,__c11_atomic_compare_exchange_strong(&tiny,&a,b,5,5));a.a=99;"
            "h=fold(h,__c11_atomic_compare_exchange_weak(&tiny,&a,b,5,0));h=fold(h,a.a+a.c*17u);"
            "struct Big first={(unsigned)n,7,8},second={9,(unsigned)n,10};__c11_atomic_init(&big,first);"
            "second=__c11_atomic_exchange(&big,second,5);h=fold(h,second.a+second.b*19+second.c*23);"
            "wide=((unsigned __int128)(unsigned)n<<80)+123;h=fold(h,(unsigned)__c11_atomic_fetch_add(&wide,31,5));"
            "h=fold(h,(unsigned)++wide);wide/=3;wide%=17;h=fold(h,(unsigned)wide);"
            "extended=(long double)n+0.5L;h=fold(h,(unsigned)(int)(extended+=2.5L));"
            "h=fold(h,(unsigned)(int)__c11_atomic_exchange(&extended,(long double)n*2.0L,5));"
            "volatile _Complex double rhs=2.0+1.0i;z=(double)n+2.0i;z+=rhs;z*=rhs;_Complex double q=z;"
            "h=fold(h,(unsigned)(int)(__real__ q*13+__imag__ q*7));real=n;real+=rhs;h=fold(h,(unsigned)(int)real);"
            "packed.x=(short)n;h=fold(h,(unsigned)__c11_atomic_fetch_add(&packed.x,1,5));h=fold(h,(unsigned)packed.x);"
            "bit=0;for(int i=0;i<300;i++){h=fold(h,__c11_atomic_fetch_add(&bit,1,5));}h=fold(h,bit);"
            "bit=0;__c11_atomic_fetch_add(&bit,1,5);__c11_atomic_fetch_add(&bit,1,5);__c11_atomic_fetch_max(&bit,0,5);"
            "for(int i=0;i<254;i++){__c11_atomic_fetch_add(&bit,1,5);}h=fold(h,bit);"
            "h=fold(h,__c11_atomic_is_lock_free(0));h=fold(h,__c11_atomic_is_lock_free(4));h=fold(h,(unsigned)parameter(n));return (int)h;}",
        },
        {
            // clang's sized legacy forms are their families' builtins whatever their suffixes say, __sync_swap is
            // __sync_lock_test_and_set's exchange, and its legacy minimum and maximum take their prototypes' int and
            // unsigned int through any pointer converting to them (void * among them).
            "sized-legacy-atomics",
            "#pragma clang diagnostic ignored \"-Wsync-fetch-and-nand-semantics-changed\"\n"
            "static signed char c8;static unsigned short u16;static int i32;static unsigned u32;static long s64;static unsigned long u64;"
            "static _Bool flag;static char buffer[32];static char*pointer;\n"
            "int tested(int n){unsigned h=2166136261u;c8=(signed char)(n*37);u16=(unsigned short)(n*4099);i32=n*100003;u32=(unsigned)n*2654435761u;"
            "s64=-(long)n*7;u64=(unsigned long)n*0x9e3779b97f4a7c15ul;"
            "h=fold(h,(unsigned long long)__sync_fetch_and_add_1(&c8,(signed char)n));h=fold(h,__sync_add_and_fetch_2(&u16,77));"
            "h=fold(h,(unsigned long long)__sync_fetch_and_sub_4(&i32,n));h=fold(h,__sync_sub_and_fetch_8(&u64,3));"
            "h=fold(h,(unsigned long long)__sync_fetch_and_and_16(&s64,~(long)n));h=fold(h,(unsigned long long)__sync_or_and_fetch_1(&c8,0x41));"
            "h=fold(h,__sync_fetch_and_xor_2(&u16,(unsigned short)n));h=fold(h,(unsigned long long)__sync_nand_and_fetch_4(&i32,0x5555));"
            "h=fold(h,__sync_fetch_and_nand_8(&u64,0xff00ff));h=fold(h,(unsigned long long)__sync_lock_test_and_set_8(&s64,(long)n<<40));"
            "h=fold(h,__sync_bool_compare_and_swap_4(&i32,i32,n));h=fold(h,(unsigned long long)__sync_val_compare_and_swap_8(&s64,3,4));"
            "h=fold(h,__sync_fetch_and_or_4(&u16,0x100));h=fold(h,__sync_xor_and_fetch_8(&u16,7));h=fold(h,(unsigned long long)__sync_and_and_fetch_1(&s64,-2L));"
            "h=fold(h,(unsigned long long)__sync_swap(&c8,(signed char)(n-9)));h=fold(h,__sync_swap_2(&u16,(unsigned short)(n*3)));"
            "h=fold(h,__sync_swap_16(&flag,n&1));__sync_lock_release_1(&u16);__sync_lock_release_4(&flag);"
            "h=fold(h,(unsigned)__sync_fetch_and_max(&i32,n*-3+5));h=fold(h,(unsigned)__sync_fetch_and_min(&i32,n-2));"
            "h=fold(h,__sync_fetch_and_umax(&u32,(unsigned)n<<28));h=fold(h,__sync_fetch_and_umin(&u32,(unsigned)n*1000));"
            "h=fold(h,(unsigned)__sync_fetch_and_max((int*)&u32,-5));h=fold(h,__sync_fetch_and_umin((unsigned*)&i32,7u));"
            "void*v=&u32;h=fold(h,__sync_fetch_and_umax(v,99u));"
            "pointer=buffer+(n&7);h=fold(h,(unsigned long long)(__sync_fetch_and_add_2(&pointer,(char*)5)-buffer));"
            "h=fold(h,(unsigned long long)(__sync_swap_8(&pointer,buffer+3)-buffer));"
            "h=fold(h,(unsigned long long)c8);h=fold(h,u16);h=fold(h,(unsigned)i32);h=fold(h,u32);h=fold(h,(unsigned long long)s64);h=fold(h,u64);"
            "h=fold(h,flag);h=fold(h,(unsigned long long)(pointer-buffer));return (int)h;}",
        },
        {
            // clang's wrapping increment and decrement, GNU's byte flag of any object, and the lock-free queries, constant
            // where clang's evaluator answers them (their arguments then never evaluated) and libatomic's otherwise.
            "wrapping-flags-and-lock-free-atomics",
            "static unsigned char u8;static signed char s8;static unsigned short u16;static int i32;static unsigned u32;static long s64;"
            "static unsigned long u64;static _Bool flag;static char byte;static int word;struct S{char a,b;}pair;struct Incomplete;static long cell;\n"
            "_Static_assert(__atomic_always_lock_free(sizeof(long),0),\"long\");_Static_assert(!__atomic_always_lock_free(16,0),\"16\");"
            "_Static_assert(__atomic_always_lock_free(4,&i32),\"int\");_Static_assert(__atomic_is_lock_free(2,0),\"short\");"
            "enum{E=__atomic_always_lock_free(8,&cell)*10+__atomic_always_lock_free(8,&i32)+__c11_atomic_is_lock_free(4)*100};"
            "static int calls;static int*counted(int*p){calls+=1;return p;}\n"
            "int tested(int n){unsigned h=2166136261u;u8=(unsigned char)(n*37);s8=(signed char)(n*-11);u16=(unsigned short)(n*4099);"
            "i32=n*3-7;u32=(unsigned)n*5;s64=-(long)n;u64=(unsigned long)n*9;"
            "for(int i=0;i<4;i+=1){h=fold(h,__atomic_fetch_uinc(&u8,(unsigned char)(n*13),__ATOMIC_SEQ_CST));"
            "h=fold(h,(unsigned long long)__atomic_fetch_udec(&s8,(signed char)(n-3),__ATOMIC_RELAXED));"
            "h=fold(h,__atomic_fetch_uinc(&u16,5,__ATOMIC_ACQUIRE));h=fold(h,(unsigned)__atomic_fetch_udec(&i32,9,__ATOMIC_RELEASE));"
            "h=fold(h,__atomic_fetch_uinc(&u32,(unsigned)n,__ATOMIC_ACQ_REL));h=fold(h,(unsigned long long)__atomic_fetch_udec(&s64,n,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_fetch_uinc(&u64,2,__ATOMIC_SEQ_CST));h=fold(h,__atomic_fetch_udec(&u64,0,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_fetch_uinc(&flag,n&1,__ATOMIC_SEQ_CST));h=fold(h,__atomic_fetch_udec(&flag,1,__ATOMIC_SEQ_CST));}"
            "byte=(char)(n&3);word=n<<8;h=fold(h,__atomic_test_and_set(&byte,__ATOMIC_SEQ_CST));h=fold(h,__atomic_test_and_set(&byte,__ATOMIC_ACQUIRE));"
            "__atomic_clear(&byte,__ATOMIC_RELEASE);h=fold(h,__atomic_test_and_set(&word,__ATOMIC_RELAXED));h=fold(h,(unsigned)word);"
            "__atomic_clear(&word,__ATOMIC_SEQ_CST);void*pv=&pair.b;h=fold(h,__atomic_test_and_set(pv,__ATOMIC_SEQ_CST));"
            "h=fold(h,__atomic_test_and_set((struct Incomplete*)&pair,n));__atomic_clear((volatile void*)&pair.b,n&1?0:5);"
            "h=fold(h,(unsigned)pair.a+256u*(unsigned)pair.b);calls=0;h=fold(h,__atomic_is_lock_free(4,counted(&i32)));"
            "h=fold(h,__atomic_is_lock_free(8,counted(&i32)));h=fold(h,__atomic_is_lock_free(16,0));h=fold(h,__atomic_is_lock_free(3,0));"
            "h=fold(h,__atomic_is_lock_free((unsigned)n&7,&cell));h=fold(h,__atomic_always_lock_free(8,counted(&word)));h=fold(h,(unsigned)calls);"
            "h=fold(h,E);h=fold(h,u8);h=fold(h,(unsigned long long)s8);h=fold(h,u16);h=fold(h,(unsigned)i32);h=fold(h,u32);"
            "h=fold(h,(unsigned long long)s64);h=fold(h,u64);h=fold(h,flag);h=fold(h,(unsigned)byte);return (int)h;}",
        },
        {
            // GNU's generic forms, whose values are where their arguments point: native objects through the leaves, and
            // records, long double and packed members through libatomic's generic functions, as clang's code calls them.
            "generic-atomics",
            "static unsigned fold_bytes(unsigned h,const void*p,unsigned long n){const unsigned char*b=p;for(unsigned long i=0;i<n;i+=1){h=fold(h,b[i]);}return h;}"
            "struct S3{char c[3];};struct S8{int a,b;};struct S16{long a,b;};struct S24{long a,b,c;};struct __attribute__((packed)) P{char tag;int x;};"
            "static int i32;static long s64;static short s16;static double d;static float f;static long double ld;static char bytes4[4];"
            "static struct S3 s3;static struct S8 s8;static struct S16 s16b;static struct S24 s24;static struct P packed;\n"
            "int tested(int n){unsigned h=2166136261u;i32=n*7;s64=(long)n<<33;s16=(short)(n*13);d=n*0.5;f=(float)n*0.25f;ld=(long double)n/3;"
            "for(int i=0;i<4;i+=1){bytes4[i]=(char)n;}s3=(struct S3){{(char)n,(char)(n+1),(char)(n+2)}};s8=(struct S8){n,-n};s16b=(struct S16){n,n*3};"
            "s24=(struct S24){n,n+1,n+2};packed.x=n*5;"
            "int ri;long rl;short rs;double rd;float rf;long double rld;struct S3 r3;struct S8 r8;struct S16 r16;struct S24 r24;char rb[4];"
            "__atomic_load(&i32,&ri,__ATOMIC_SEQ_CST);h=fold(h,(unsigned)ri);__atomic_load(&s64,&rl,__ATOMIC_ACQUIRE);h=fold(h,(unsigned long long)rl);"
            "__atomic_load(&s16,&rs,__ATOMIC_RELAXED);h=fold(h,(unsigned)rs);__atomic_load(&d,&rd,__ATOMIC_SEQ_CST);h=fold_bytes(h,&rd,sizeof rd);"
            "__atomic_load(&f,&rf,__ATOMIC_SEQ_CST);h=fold_bytes(h,&rf,sizeof rf);__atomic_load(&ld,&rld,__ATOMIC_SEQ_CST);h=fold_bytes(h,&rld,10);"
            "__atomic_load(&s3,&r3,__ATOMIC_SEQ_CST);h=fold_bytes(h,&r3,sizeof r3);__atomic_load(&s8,&r8,__ATOMIC_SEQ_CST);h=fold_bytes(h,&r8,sizeof r8);"
            "__atomic_load(&s16b,&r16,__ATOMIC_SEQ_CST);h=fold_bytes(h,&r16,sizeof r16);__atomic_load(&s24,&r24,__ATOMIC_SEQ_CST);h=fold_bytes(h,&r24,sizeof r24);"
            "__atomic_load(&bytes4,&rb,__ATOMIC_SEQ_CST);h=fold_bytes(h,rb,4);"
            "int vi=n*100;long vl=-(long)n;struct S3 v3={{'a',(char)n,'c'}};struct S8 v8={n*2,n*4};struct S24 v24={7,(long)n,9};long double vld=1.5L*n;"
            "__atomic_store(&i32,&vi,__ATOMIC_SEQ_CST);h=fold(h,(unsigned)i32);__atomic_store(&s64,&vl,__ATOMIC_RELEASE);h=fold(h,(unsigned long long)s64);"
            "__atomic_store(&s3,&v3,__ATOMIC_SEQ_CST);h=fold_bytes(h,&s3,sizeof s3);__atomic_store(&s8,&v8,__ATOMIC_RELAXED);h=fold_bytes(h,&s8,sizeof s8);"
            "__atomic_store(&s24,&v24,__ATOMIC_SEQ_CST);h=fold_bytes(h,&s24,sizeof s24);__atomic_store(&ld,&vld,__ATOMIC_SEQ_CST);h=fold_bytes(h,&ld,10);"
            "__atomic_store(&packed.x,&vi,__ATOMIC_SEQ_CST);h=fold(h,(unsigned)packed.x);"
            "vi=n-1;__atomic_exchange(&i32,&vi,&ri,__ATOMIC_SEQ_CST);h=fold(h,(unsigned)ri);h=fold(h,(unsigned)i32);"
            "v8.a+=1;__atomic_exchange(&s8,&v8,&r8,__ATOMIC_ACQ_REL);h=fold_bytes(h,&r8,sizeof r8);h=fold_bytes(h,&s8,sizeof s8);"
            "v3.c[0]='z';__atomic_exchange(&s3,&v3,&r3,__ATOMIC_SEQ_CST);h=fold_bytes(h,&r3,sizeof r3);"
            "v24.c=n*11;__atomic_exchange(&s24,&v24,&r24,__ATOMIC_SEQ_CST);h=fold_bytes(h,&r24,sizeof r24);h=fold_bytes(h,&s24,sizeof s24);"
            "__atomic_exchange(&packed.x,&vi,&ri,__ATOMIC_SEQ_CST);h=fold(h,(unsigned)ri);"
            "int ei=i32,di=n*3;h=fold(h,__atomic_compare_exchange(&i32,&ei,&di,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));h=fold(h,(unsigned)ei);h=fold(h,(unsigned)i32);"
            "ei=12345;h=fold(h,__atomic_compare_exchange(&i32,&ei,&di,n&1,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED));h=fold(h,(unsigned)ei);"
            "struct S8 e8=s8,d8={5,(int)n};h=fold(h,__atomic_compare_exchange(&s8,&e8,&d8,1,__ATOMIC_SEQ_CST,__ATOMIC_ACQUIRE));h=fold_bytes(h,&s8,sizeof s8);"
            "e8.a=-77;h=fold(h,__atomic_compare_exchange(&s8,&e8,&d8,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));h=fold_bytes(h,&e8,sizeof e8);"
            "struct S24 e24=s24,d24={1,2,(long)n};h=fold(h,__atomic_compare_exchange(&s24,&e24,&d24,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));"
            "h=fold_bytes(h,&s24,sizeof s24);e24.a=99;h=fold(h,__atomic_compare_exchange(&s24,&e24,&d24,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));"
            "h=fold_bytes(h,&e24,sizeof e24);struct S3 e3=s3,d3={{1,2,3}};h=fold(h,__atomic_compare_exchange(&s3,&e3,&d3,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));"
            "h=fold_bytes(h,&s3,sizeof s3);double ed=d,dd=n*1.25;h=fold(h,__atomic_compare_exchange(&d,&ed,&dd,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));"
            "h=fold_bytes(h,&d,sizeof d);return (int)h;}",
        },
        {
            // The scoped families: GNU's operations with a scope (__scoped_atomic_*), C11's on plain objects (__hip_atomic_*,
            // whose pointer arithmetic scales as C11's) and on _Atomic ones (__opencl_atomic_*), each scope evaluated once.
            "scoped-atomics",
            "static unsigned fold_bytes(unsigned h,const void*p,unsigned long n){const unsigned char*b=p;for(unsigned long i=0;i<n;i+=1){h=fold(h,b[i]);}return h;}"
            "struct S8{int a,b;};struct T3{char c[3];};static int i32;static unsigned u32;static long s64;static unsigned char u8;static int arr[8];"
            "static int*pointer;static float f;static double d;static struct S8 s8;static _Atomic int ai;static _Atomic long al;static _Atomic(int*)ap;"
            "static _Atomic(struct S8)as8;static _Atomic(struct T3)at3;static _Atomic float af;static int scopes;static int scope(int s){scopes+=1;return s;}\n"
            "int tested(int n){unsigned h=2166136261u;i32=n*7;u32=(unsigned)n*3;s64=(long)n<<20;u8=(unsigned char)n;pointer=arr+(n&3);f=(float)n;d=n*0.5;"
            "s8=(struct S8){n,n+1};scopes=0;"
            "h=fold(h,(unsigned)__scoped_atomic_load_n(&i32,__ATOMIC_SEQ_CST,__MEMORY_SCOPE_SYSTEM));"
            "__scoped_atomic_store_n(&i32,n*9,__ATOMIC_RELEASE,scope(__MEMORY_SCOPE_DEVICE));"
            "h=fold(h,(unsigned)__scoped_atomic_exchange_n(&i32,n,__ATOMIC_SEQ_CST,__MEMORY_SCOPE_WRKGRP));int e=i32;"
            "h=fold(h,__scoped_atomic_compare_exchange_n(&i32,&e,5,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST,scope(n&3)));"
            "h=fold(h,(unsigned)__scoped_atomic_fetch_add(&i32,3,__ATOMIC_SEQ_CST,__MEMORY_SCOPE_SINGLE));"
            "h=fold(h,(unsigned)__scoped_atomic_fetch_sub(&i32,1,__ATOMIC_SEQ_CST,__MEMORY_SCOPE_CLUSTR));"
            "h=fold(h,__scoped_atomic_fetch_and(&u32,0xff,__ATOMIC_SEQ_CST,0));h=fold(h,__scoped_atomic_fetch_or(&u32,0x100,__ATOMIC_SEQ_CST,1));"
            "h=fold(h,__scoped_atomic_fetch_xor(&u32,0x55,__ATOMIC_SEQ_CST,2));h=fold(h,__scoped_atomic_fetch_nand(&u32,0x3c,__ATOMIC_SEQ_CST,3));"
            "h=fold(h,(unsigned)__scoped_atomic_fetch_max(&i32,n*2,__ATOMIC_SEQ_CST,4));h=fold(h,(unsigned)__scoped_atomic_fetch_min(&i32,-n,__ATOMIC_SEQ_CST,5));"
            "h=fold(h,__scoped_atomic_fetch_uinc(&u8,7,__ATOMIC_SEQ_CST,0));h=fold(h,__scoped_atomic_fetch_udec(&u8,2,__ATOMIC_SEQ_CST,0));"
            "h=fold(h,(unsigned long long)__scoped_atomic_add_fetch(&s64,3,__ATOMIC_SEQ_CST,0));h=fold(h,(unsigned long long)__scoped_atomic_sub_fetch(&s64,4,__ATOMIC_SEQ_CST,0));"
            "h=fold(h,__scoped_atomic_and_fetch(&u32,0x7f,__ATOMIC_SEQ_CST,0));h=fold(h,__scoped_atomic_or_fetch(&u32,0x80,__ATOMIC_SEQ_CST,0));"
            "h=fold(h,__scoped_atomic_xor_fetch(&u32,0x11,__ATOMIC_SEQ_CST,0));h=fold(h,__scoped_atomic_nand_fetch(&u32,0x22,__ATOMIC_SEQ_CST,0));"
            "h=fold(h,(unsigned long long)__scoped_atomic_max_fetch(&s64,100,__ATOMIC_SEQ_CST,0));h=fold(h,(unsigned long long)__scoped_atomic_min_fetch(&s64,50,__ATOMIC_SEQ_CST,0));"
            "h=fold(h,(unsigned long long)(__scoped_atomic_fetch_add(&pointer,4,__ATOMIC_SEQ_CST,0)-arr));int r;int v=n+40;"
            "__scoped_atomic_load(&i32,&r,__ATOMIC_SEQ_CST,0);h=fold(h,(unsigned)r);__scoped_atomic_store(&i32,&v,__ATOMIC_SEQ_CST,scope(1));h=fold(h,(unsigned)i32);"
            "v+=1;__scoped_atomic_exchange(&i32,&v,&r,__ATOMIC_SEQ_CST,1);h=fold(h,(unsigned)r);"
            "e=i32;v=77;h=fold(h,__scoped_atomic_compare_exchange(&i32,&e,&v,1,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST,2));h=fold(h,(unsigned)i32);"
            "__scoped_atomic_thread_fence(__ATOMIC_SEQ_CST,scope(9));"
            "h=fold(h,(unsigned)__hip_atomic_load(&i32,__ATOMIC_SEQ_CST,1));__hip_atomic_store(&i32,n*3,__ATOMIC_SEQ_CST,scope(2));"
            "h=fold(h,(unsigned)__hip_atomic_exchange(&i32,n+2,__ATOMIC_SEQ_CST,3));e=i32;h=fold(h,__hip_atomic_compare_exchange_strong(&i32,&e,8,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST,4));"
            "e=9999;h=fold(h,__hip_atomic_compare_exchange_weak(&i32,&e,8,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST,5));h=fold(h,(unsigned)e);"
            "h=fold(h,(unsigned)__hip_atomic_fetch_add(&i32,2,__ATOMIC_SEQ_CST,6));h=fold(h,(unsigned)__hip_atomic_fetch_sub(&i32,1,__ATOMIC_SEQ_CST,1));"
            "h=fold(h,__hip_atomic_fetch_and(&u32,0xf0,__ATOMIC_SEQ_CST,1));h=fold(h,__hip_atomic_fetch_or(&u32,0x3,__ATOMIC_SEQ_CST,1));"
            "h=fold(h,__hip_atomic_fetch_xor(&u32,0x5,__ATOMIC_SEQ_CST,1));h=fold(h,(unsigned)__hip_atomic_fetch_max(&i32,n,__ATOMIC_SEQ_CST,1));"
            "h=fold(h,(unsigned)__hip_atomic_fetch_min(&i32,n-5,__ATOMIC_SEQ_CST,1));"
            "h=fold(h,(unsigned long long)(__hip_atomic_fetch_add(&pointer,1,__ATOMIC_SEQ_CST,1)-arr));h=fold(h,(unsigned long long)(pointer-arr));"
            "struct S8 r8=__hip_atomic_load(&s8,__ATOMIC_SEQ_CST,1);h=fold_bytes(h,&r8,sizeof r8);"
            "__hip_atomic_store(&s8,((struct S8){3,n}),__ATOMIC_SEQ_CST,1);h=fold_bytes(h,&s8,sizeof s8);"
            "h=fold(h,(unsigned)(int)__hip_atomic_fetch_add(&f,1.5f,__ATOMIC_SEQ_CST,1));h=fold(h,(unsigned)(int)(f*4));"
            "h=fold(h,(unsigned)(int)__hip_atomic_fetch_max(&d,2.0,__ATOMIC_SEQ_CST,1));h=fold(h,(unsigned)(int)(d*4));"
            "__opencl_atomic_init(&ai,n);h=fold(h,(unsigned)__opencl_atomic_load(&ai,__ATOMIC_SEQ_CST,__OPENCL_MEMORY_SCOPE_DEVICE));"
            "__opencl_atomic_store(&ai,n*5,__ATOMIC_SEQ_CST,scope(2));h=fold(h,(unsigned)__opencl_atomic_exchange(&ai,n,__ATOMIC_SEQ_CST,1));"
            "e=n;h=fold(h,__opencl_atomic_compare_exchange_strong(&ai,&e,4,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST,3));"
            "e=-1;h=fold(h,__opencl_atomic_compare_exchange_weak(&ai,&e,4,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST,4));h=fold(h,(unsigned)e);"
            "h=fold(h,(unsigned)__opencl_atomic_fetch_add(&ai,10,__ATOMIC_SEQ_CST,2));h=fold(h,(unsigned)__opencl_atomic_fetch_sub(&ai,3,__ATOMIC_SEQ_CST,2));"
            "h=fold(h,(unsigned)__opencl_atomic_fetch_and(&ai,0xff,__ATOMIC_SEQ_CST,2));h=fold(h,(unsigned)__opencl_atomic_fetch_or(&ai,0x100,__ATOMIC_SEQ_CST,2));"
            "h=fold(h,(unsigned)__opencl_atomic_fetch_xor(&ai,0x7,__ATOMIC_SEQ_CST,2));h=fold(h,(unsigned)__opencl_atomic_fetch_max(&ai,n*50,__ATOMIC_SEQ_CST,2));"
            "h=fold(h,(unsigned)__opencl_atomic_fetch_min(&ai,n*10,__ATOMIC_SEQ_CST,2));"
            "__opencl_atomic_init(&al,(long)n<<40);h=fold(h,(unsigned long long)__opencl_atomic_fetch_add(&al,1,__ATOMIC_SEQ_CST,2));"
            "__opencl_atomic_init(&ap,arr);h=fold(h,(unsigned long long)(__opencl_atomic_fetch_add(&ap,2,__ATOMIC_SEQ_CST,2)-arr));"
            "h=fold(h,(unsigned long long)(__opencl_atomic_load(&ap,__ATOMIC_SEQ_CST,2)-arr));"
            "__opencl_atomic_init(&as8,((struct S8){n,2}));struct S8 o8=__opencl_atomic_load(&as8,__ATOMIC_SEQ_CST,2);h=fold_bytes(h,&o8,sizeof o8);"
            "__opencl_atomic_init(&at3,((struct T3){{1,2,(char)n}}));struct T3 o3=__opencl_atomic_exchange(&at3,((struct T3){{4,5,6}}),__ATOMIC_SEQ_CST,2);"
            "h=fold_bytes(h,&o3,sizeof o3);__opencl_atomic_init(&af,(float)n);h=fold(h,(unsigned)(int)__opencl_atomic_fetch_add(&af,0.5f,__ATOMIC_SEQ_CST,2));"
            "h=fold(h,(unsigned)scopes);h=fold(h,(unsigned)i32);h=fold(h,u32);h=fold(h,(unsigned long long)s64);h=fold(h,u8);return (int)h;}",
        },
        {
            // clang's atomic IEEE maximum and minimum of floats and doubles, GNU's and the scoped forms, every pair of 13
            // values by their bits: both zeros, ones, both infinities, quiet NaNs of each sign and of a payload, signaling
            // NaNs and both signs' smallest denormals.
            "ieee-maximum-minimum-atomics",
            "static float f;static double d;"
            "static unsigned fb(float x){union{float f;unsigned u;}v={x};return v.u;}static float bf(unsigned u){union{unsigned u;float f;}v={u};return v.f;}"
            "static unsigned long long db(double x){union{double d;unsigned long long u;}v={x};return v.u;}"
            "static double bd(unsigned long long u){union{unsigned long long u;double d;}v={u};return v.d;}\n"
            "int tested(int n){static const unsigned fs[]={0x00000000u,0x80000000u,0x3f800000u,0xbf800000u,0x7f800000u,0xff800000u,0x7fc00000u,"
            "0xffc00000u,0x7fc00123u,0x7f800001u,0xff800123u,0x00000001u,0x80000001u};"
            "static const unsigned long long ds[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,0x7ff0000000000000ull,"
            "0xfff0000000000000ull,0x7ff8000000000000ull,0xfff8000000000000ull,0x7ff8000000000123ull,0x7ff0000000000001ull,0xfff0000000000123ull,1,"
            "0x8000000000000001ull};"
            "unsigned h=2166136261u;const unsigned count=sizeof fs/sizeof fs[0];const unsigned i=(unsigned)n%count;for(unsigned j=0;j<count;j+=1){"
            "f=bf(fs[i]);h=fold(h,fb(__atomic_fetch_fmaximum(&f,bf(fs[j]),__ATOMIC_SEQ_CST)));h=fold(h,fb(f));"
            "f=bf(fs[i]);h=fold(h,fb(__atomic_fetch_fminimum(&f,bf(fs[j]),__ATOMIC_RELAXED)));h=fold(h,fb(f));"
            "f=bf(fs[i]);h=fold(h,fb(__atomic_fetch_fmaximum_num(&f,bf(fs[j]),__ATOMIC_ACQUIRE)));h=fold(h,fb(f));"
            "f=bf(fs[i]);h=fold(h,fb(__atomic_fetch_fminimum_num(&f,bf(fs[j]),__ATOMIC_RELEASE)));h=fold(h,fb(f));"
            "d=bd(ds[i]);h=fold(h,db(__atomic_fetch_fmaximum(&d,bd(ds[j]),__ATOMIC_SEQ_CST)));h=fold(h,db(d));"
            "d=bd(ds[i]);h=fold(h,db(__atomic_fetch_fminimum(&d,bd(ds[j]),__ATOMIC_ACQ_REL)));h=fold(h,db(d));"
            "d=bd(ds[i]);h=fold(h,db(__scoped_atomic_fetch_fmaximum_num(&d,bd(ds[j]),__ATOMIC_SEQ_CST,__MEMORY_SCOPE_DEVICE)));h=fold(h,db(d));"
            "d=bd(ds[i]);h=fold(h,db(__scoped_atomic_fetch_fminimum_num(&d,bd(ds[j]),__ATOMIC_SEQ_CST,0)));h=fold(h,db(d));"
            "f=bf(fs[j]);h=fold(h,fb(__scoped_atomic_fetch_fmaximum(&f,(float)n,__ATOMIC_SEQ_CST,1)));h=fold(h,fb(f));"
            "f=bf(fs[j]);h=fold(h,fb(__scoped_atomic_fetch_fminimum(&f,(float)-n,__ATOMIC_SEQ_CST,2)));h=fold(h,fb(f));}"
            "return (int)h;}",
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        char source[16384];
        const int length = snprintf(source, sizeof(source), "%s%s", check_test_atomic_fold, programs[i][1]);
        check_test_expect(test, length > 0 && (size_t)length < sizeof(source), "atomic program source fits");
        check_test_program_source(test, source, programs[i][0], clang);
    }
}

// The lowering's verdict on a unit: lowered on both paths alike, or refused as one atomic object that may be misaligned.
static bool check_test_program_atomic_lowered(CheckTest* test, const char* source, const char* name, bool* misaligned)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = true,
    };
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers);
    preprocessed = preprocessed && pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source));
    bool verdicts[2] = { 0 };
    bool decided[2] = { 0 };
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parse_options = { .scalar = path == 0 };
        const CheckOptions options = { .scalar = path == 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parse_options, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0;
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers initializers = { 0 };
        CheckAsms asms = { 0 };
        CheckDataPrepared data = { 0 };
        CheckProgram program = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &options, &stream) && stream.report_count == 0 &&
                check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch) &&
                check_data_prepare(&data, &input, &stream, &initializers, &types.table, parser.options.scalar, &input_arena, &scratch);
        const CheckProgramOptions program_options = {
            .initializers = &initializers,
            .asms = &asms,
            .identifiers = &identifiers,
            .initializer_references = data.references,
            .initializer_reference_count = data.reference_count,
            .scalar = path == 0,
        };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, program_options, &input_arena, &scratch);
        CheckProgramLowered lowered = { 0 };
        const bool done = ready && check_program_lower(&lowered, &program, true, &output, &scratch);
        decided[path] = ready && (done || (lowered.status == CHECK_PROGRAM_UNSUPPORTED && lowered.reason != NULL &&
                                           strcmp(lowered.reason, "a misaligned atomic object, which clang calls libatomic for") == 0));
        verdicts[path] = !done;
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    char description[256];
    snprintf(description, sizeof(description), "%s: lowered, or refused as misaligned, alike on both paths", name);
    const bool valid = decided[0] && decided[1] && verdicts[0] == verdicts[1];
    check_test_expect(test, valid, description);
    *misaligned = verdicts[1];
    return valid;
}

// Whether clang's object of the unit calls libatomic: its string table names an __atomic_ function.
static bool check_test_program_atomic_libcall(const char* source, const char* clang, bool* libcall)
{
    char directory[] = "/tmp/buster-check-atomic-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    char path[160];
    char object[160];
    char out[160];
    char err[160];
    snprintf(path, sizeof(path), "%s/unit.c", directory);
    snprintf(object, sizeof(object), "%s/unit.o", directory);
    snprintf(out, sizeof(out), "%s/out", directory);
    snprintf(err, sizeof(err), "%s/err", directory);
    char* compile[] = {
        (char*)clang,
        "-std=gnu17",
        "-O0",
        "-march=x86-64",
        "-fno-stack-protector",
        "-w",
        "-c",
        path,
        "-o",
        object,
        0,
    };
    bool valid = created && parse_test_write(path, source, strlen(source)) && object_test_run(compile, out, err) == 0;
    FILE* file = valid ? fopen(object, "rb") : 0;
    uint8_t bytes[65536];
    const size_t size = file != 0 ? fread(bytes, 1, sizeof(bytes), file) : 0;
    valid = file != 0 && size != 0 && size < sizeof(bytes);
    if (file != 0)
    {
        fclose(file);
    }
    *libcall = valid && memmem(bytes, size, "__atomic_", 9) != 0;
    if (created)
    {
        unlink(path);
        unlink(object);
        unlink(out);
        unlink(err);
        rmdir(directory);
    }
    return valid;
}

// Each unit's atomic access is misaligned exactly when clang calls libatomic for it (EmitPointerWithAlignment's alignment
// below the object's size), and the lowering refuses exactly those.
static void check_test_program_atomic_alignment(CheckTest* test, const char* clang)
{
    typedef struct CheckAtomicAlignment CheckAtomicAlignment;
    struct CheckAtomicAlignment
    {
        const char* name;
        const char* source;
        bool misaligned;
    };
    static const CheckAtomicAlignment cases[] = {
        { "a pointer's pointee", "unsigned f(unsigned*p){return __atomic_load_n(p,__ATOMIC_RELAXED);}", false },
        { "a packed record's member", "struct __attribute__((packed)) P{char c;unsigned w;};unsigned f(struct P*p){return __atomic_load_n(&p->w,__ATOMIC_RELAXED);}", true },
        { "#pragma pack's member", "#pragma pack(2)\nstruct P{char c;unsigned w;};\n#pragma pack()\nunsigned f(struct P*p){return __atomic_fetch_add(&p->w,1,__ATOMIC_RELAXED);}", true },
        { "a packed member", "struct P{char c;unsigned w __attribute__((packed));long l;};unsigned f(struct P*p){return __atomic_exchange_n(&p->w,1,__ATOMIC_RELAXED);}", true },
        { "a member at its offset", "struct S{unsigned a;unsigned b;unsigned long q;};unsigned long f(struct S*s){return __atomic_load_n(&s->q,__ATOMIC_RELAXED);}", false },
        { "a cast of a member at offset 0", "struct S{unsigned a;unsigned b;unsigned long q;};unsigned long f(struct S*s){return __atomic_load_n((unsigned long*)&s->a,__ATOMIC_RELAXED);}", false },
        { "a cast of a member at offset 4", "struct S{unsigned a;unsigned b;unsigned long q;};unsigned long f(struct S*s){return __atomic_load_n((unsigned long*)&s->b,__ATOMIC_RELAXED);}", true },
        { "a cast of a char pointer", "unsigned long f(char*p){return __atomic_fetch_or((unsigned long*)p,1,__ATOMIC_RELAXED);}", false },
        { "a cast of a void pointer", "unsigned long f(void*p){return __atomic_load_n((unsigned long*)p,__ATOMIC_RELAXED);}", false },
        { "a cast of a char object's address", "char c;unsigned f(void){return __atomic_load_n((unsigned*)&c,__ATOMIC_RELAXED);}", true },
        { "a cast of a char array's element", "char buffer[8];unsigned f(void){return __atomic_load_n((unsigned*)&buffer[1],__ATOMIC_RELAXED);}", true },
        { "a cast of an array plus 1", "char buffer[8];unsigned f(void){return __atomic_load_n((unsigned*)(buffer+1),__ATOMIC_RELAXED);}", true },
        { "a cast of a pointer plus 1", "unsigned long f(unsigned*p){return __atomic_load_n((unsigned long*)(p+1),__ATOMIC_RELAXED);}", false },
        { "a pointer element's value, of a pointer type aligned to 1", "typedef unsigned*__attribute__((aligned(1)))UP;unsigned f(UP*p,int i){return __atomic_load_n(p[i],__ATOMIC_RELAXED);}", false },
        { "a variable aligned to 1", "int x __attribute__((aligned(1)));int f(void){return __atomic_load_n(&x,__ATOMIC_RELAXED);}", true },
        { "a member of a variable aligned to 2", "struct S{int a;int b;}s __attribute__((aligned(2)));int f(void){return __atomic_load_n(&s.b,__ATOMIC_RELAXED);}", true },
        { "an element of an array of 16 bytes", "char buffer[16];unsigned f(void){return __atomic_load_n((unsigned*)&buffer[4],__ATOMIC_RELAXED);}", false },
        { "an element of an array of 15 bytes", "char buffer[15];unsigned f(void){return __atomic_load_n((unsigned*)&buffer[4],__ATOMIC_RELAXED);}", true },
        { "an element of a local array of 16 bytes", "unsigned f(void){char buffer[16];return __atomic_load_n((unsigned*)&buffer[4],__ATOMIC_RELAXED);}", false },
        { "an element of an array of 16 bytes aligned to 2", "char buffer[16] __attribute__((aligned(2)));unsigned f(void){return __atomic_load_n((unsigned*)&buffer[4],__ATOMIC_RELAXED);}", true },
        { "an element of an array aligned to 32", "_Alignas(32) char buffer[8];unsigned long f(void){return __atomic_load_n((unsigned long*)&buffer[8 - 8],__ATOMIC_RELAXED);}", false },
        { "an element of a record's array", "typedef struct{char c[16];}B;B b;unsigned f(void){return __atomic_load_n((unsigned*)&b.c[4],__ATOMIC_RELAXED);}", true },
        { "an element of an array of 40 bytes at a variable index", "char buffer[40];unsigned long f(int i){return __atomic_load_n((unsigned long*)&buffer[i],__ATOMIC_RELAXED);}", true },
        { "an array of 40 bytes plus 8", "char buffer[40];unsigned long f(void){return __atomic_load_n((unsigned long*)(buffer+8),__ATOMIC_RELAXED);}", false },
        { "an aligned typedef below its type's alignment", "typedef unsigned __attribute__((aligned(1))) U;unsigned f(U*p){return __atomic_load_n(p,__ATOMIC_RELAXED);}", true },
        { "an element of a record of alignment 4", "struct __attribute__((packed,aligned(4))) R{unsigned a;unsigned long q;};unsigned long f(struct R*r,int i){return __atomic_fetch_add(&r[i].q,1,__ATOMIC_RELAXED);}", true },
        { "a nested member", "struct I{char c;short s;};struct O{int x;struct I in;};short f(struct O*o){return __atomic_load_n(&o->in.s,__ATOMIC_RELAXED);}", false },
        { "a constant element of a packed array", "struct __attribute__((packed)) P{char c;unsigned w[2];};unsigned f(struct P*p){return __atomic_load_n(&p->w[1],__ATOMIC_RELAXED);}", true },
        { "a dereference's address", "unsigned long f(unsigned long*p){return __atomic_load_n(&*p,__ATOMIC_RELAXED);}", false },
        { "a store through a member", "struct S{char c;_Bool b;};void f(struct S*s){__atomic_store_n(&s->b,1,__ATOMIC_RELEASE);}", false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        bool libcall = false;
        bool misaligned = false;
        const bool compiled = check_test_program_atomic_libcall(cases[i].source, clang, &libcall);
        char description[256];
        snprintf(description, sizeof(description), "%s: clang %s libatomic", cases[i].name, cases[i].misaligned ? "calls" : "doesn't call");
        check_test_expect(test, compiled && libcall == cases[i].misaligned, description);
        if (check_test_program_atomic_lowered(test, cases[i].source, cases[i].name, &misaligned))
        {
            snprintf(description, sizeof(description), "%s: %s", cases[i].name, cases[i].misaligned ? "refused as misaligned" : "lowered");
            check_test_expect(test, misaligned == cases[i].misaligned, description);
        }
    }
}

static void check_test_program_atomics(CheckTest* test, const char* clang)
{
    check_test_program_atomic_sources(test, clang);
    check_test_program_atomic_alignment(test, clang);
}
