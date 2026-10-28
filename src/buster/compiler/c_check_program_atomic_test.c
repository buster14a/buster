// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's atomic lowering against clang's code of the same source: every read/modify/write kind at each width through the
// leaves, the legacy __sync forms, floating loads, stores and exchanges, which cross the helpers as bits, and objects
// reached through members, elements and casts (c_checker.c's check_graph_atomic_alignment). Then the accesses clang calls
// libatomic for, which the lowering refuses as misaligned, and no others. Include after c_check_program_test.c's runners.

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
            "h=fold(h,__c11_atomic_is_lock_free(0));h=fold(h,__c11_atomic_is_lock_free(4));h=fold(h,(unsigned)parameter(n));return (int)h;}",
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        char source[8192];
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
