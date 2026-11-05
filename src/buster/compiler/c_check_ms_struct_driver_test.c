// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original Microsoft record-layout fixtures. Clang supplies diagnostics and independently compiled ABI peers.

static void check_test_driver_ms_diagnostics(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const sources[] = {
        "#pragma ms_struct unknown\nint value;\n",
        "#pragma ms_struct\nint value;\n",
        "#pragma ms_struct on extra\nint value;\n",
        "struct S{\n#pragma ms_struct on\nunsigned a:3;};\n",
        "#pragma ms_struct on\nstruct S{\n#pragma ms_struct off\nunsigned a:3;};\n",
        "struct __attribute__((ms_struct,gcc_struct)) S{unsigned a:3;};\n",
        "struct __attribute__((gcc_struct,ms_struct)) S{unsigned a:3;};\n",
        "int value __attribute__((ms_struct));\n",
        "struct __attribute__((ms_struct(1))) S{unsigned a:3;};\n",
        "struct __attribute__((gcc_struct(1))) S{unsigned a:3;};\n",
        "#pragma clang diagnostic ignored \"-Wignored-pragmas\"\n#pragma ms_struct unknown\nint value;\n",
        "#pragma clang diagnostic error \"-Wignored-pragmas\"\n#pragma ms_struct unknown\nint value;\n",
        "#define STATE unknown\n#pragma ms_struct STATE\nint value;\n",
        "#pragma ms_struct on\n#pragma ms_struct unknown\nstruct S{char head;unsigned a:3,b:5;char tail;};\n_Static_assert(sizeof(struct S)==12,\"state\");\n",
        "int value;\n#pragma ms_struct\n",
        "#pragma ms_struct unknown\nint value=;\n",
        "#pragma weak missing\n#pragma ms_struct unknown\nint value;\n",
        "#pragma float_control(pop)\n#pragma ms_struct unknown\nint value;\n",
        "enum __attribute__((ms_struct)) E{ZERO};\n",
    };
    char path[512];
    snprintf(path, sizeof(path), "%s/ms-struct-diagnostic.c", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(sources); c += 1)
    {
        const bool written = parse_test_write(path, sources[c], strlen(sources[c]));
        check_test_expect(test, written, "Microsoft layout diagnostic fixture is written");
        for (uint32_t mode = 0; written && mode < 3; mode += 1)
        {
            char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", path,
                                  mode == 1 ? "-Werror" : mode == 2 ? "-w" : NULL, NULL, NULL };
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                arguments[0] = (char*)executable;
                arguments[mode != 0 ? 6 : 5] = scalar != 0 ? "--scalar" : NULL;
                char* actual = NULL;
                const int result = check_test_driver_spawn(arguments, &actual);
                const uint64_t length = expected != NULL ? strcspn(expected, "\n") : 0;
                const uint64_t observed = actual != NULL ? strcspn(actual, "\n") : 0;
                const bool same = oracle == result && length == observed && (length == 0 || memcmp(actual, expected, length) == 0);
                check_test_expect(test, same, "Microsoft layout diagnostics match Clang's location, wording and warning policy");
                if (!same)
                {
                    fprintf(stderr, "ms_struct diagnostic %u mode %u scalar %u: Clang %d (%s), checker %d (%s)\n", c, mode, scalar,
                            oracle, expected != NULL ? expected : "", result, actual != NULL ? actual : "");
                }
                free(actual);
            }
            free(expected);
        }
    }
    static const char refused[] = "#pragma ms_struct on\nstruct S{unsigned _BitInt(129) a:32;};\n";
    char object[512];
    char marker[512];
    snprintf(object, sizeof(object), "%s/ms-struct-refused.o", directory);
    snprintf(marker, sizeof(marker), "%s/ms-struct-marker", directory);
    const bool written = parse_test_write(path, refused, sizeof(refused) - 1) && parse_test_write(marker, "preserve", 8);
    for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
    {
        const bool seeded = parse_test_write(object, "preserve", 8);
        char* arguments[] = { (char*)executable, "-std=gnu17", "-w", "-c", path, "-o", object, scalar != 0 ? "--scalar" : NULL, NULL };
        char* text = NULL;
        const int status = seeded ? check_test_driver_spawn(arguments, &text) : 0;
        check_test_expect(test, status != 0 && text != NULL && strstr(text, "ms_struct bit-field storage with non-power-of-two size is not supported") != NULL &&
                               check_test_driver_same_files(object, marker),
                          "unsupported Microsoft unit alignment is named, cannot be suppressed and preserves the output");
        free(text);
    }
    unlink(marker);
    unlink(object);
    unlink(path);
}

static void check_test_driver_ms_abi(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char header[] =
        "#define MS_MODE on\n#pragma ms_struct MS_MODE\n"
        "typedef struct{char head;unsigned a:3,b:5;char tail;} M;\n"
        "typedef struct __attribute__((packed)){char head;unsigned a:3,b:5;char tail;} P;\n"
        "typedef struct{unsigned char a:3;unsigned :0;unsigned char b:3;char tail;} Z;\n"
        "typedef union{unsigned a:3 __attribute__((aligned(16)));unsigned b:5;char tail;} U;\n"
        "typedef struct{char head;unsigned __int128 a:70,b:30;char tail;} Wide;\n"
        "typedef struct{char head;unsigned _BitInt(65) a:40,b:20;char tail;} Bits;\n"
        "typedef struct{unsigned a:3,b:5;char tail;int flexible[];} Flex;\n"
        "typedef unsigned Small __attribute__((aligned(1)));\n"
        "typedef struct{char head;Small a,b;char tail;} Plain;\n"
        "typedef struct{char head;Small a[3];unsigned b:2;char tail;} Array;\n"
        "typedef struct{char head;_Complex double a;unsigned b:2;char tail;} Complex;\n"
        "enum __attribute__((packed)) E{ZERO,ONE,TWO};\n"
        "typedef struct{enum E a:2,b:2;char tail;} Enum;\n"
        "typedef struct __attribute__((gcc_struct)){char head;unsigned a:3,b:5;char tail;} G;\n"
        "struct Forward;\n#pragma ms_struct off\nstruct Forward{char head;unsigned a:3,b:5;char tail;};\n"
        "struct __attribute__((ms_struct)) Explicit;\nstruct Explicit{char head;unsigned a:3,b:5;char tail;};\n"
        "struct __attribute__((ms_struct)) BothA;\nstruct __attribute__((gcc_struct)) BothA{char head;unsigned a:3,b:5;char tail;};\n"
        "struct __attribute__((gcc_struct)) BothB;\nstruct __attribute__((ms_struct)) BothB{char head;unsigned a:3,b:5;char tail;};\n"
        "#pragma ms_struct on\n#pragma pack(push,2)\n"
        "typedef struct{char head;unsigned char a:3;unsigned b:5;char tail;} Pack2;\n#pragma pack(pop)\n"
        "#pragma ms_struct reset\ntypedef struct{char head;unsigned a:3,b:5;char tail;} Reset;\n"
        "#define LAYOUT(T,S,A,O) _Static_assert(sizeof(T)==S&&_Alignof(T)==A&&__builtin_offsetof(T,tail)==O,\"layout \" #T)\n"
        "LAYOUT(M,12,4,8);LAYOUT(P,6,1,5);LAYOUT(Z,8,4,5);LAYOUT(U,4,1,0);\n"
        "LAYOUT(Wide,48,16,32);LAYOUT(Bits,48,16,32);LAYOUT(Flex,8,4,4);\n"
        "LAYOUT(Plain,16,4,12);LAYOUT(Array,24,4,20);LAYOUT(Complex,32,8,28);LAYOUT(Enum,2,1,1);\n"
        "LAYOUT(G,4,4,2);LAYOUT(struct Forward,4,4,2);LAYOUT(struct Explicit,12,4,8);\n"
        "LAYOUT(struct BothA,4,4,2);LAYOUT(struct BothB,4,4,2);LAYOUT(Pack2,8,2,6);LAYOUT(Reset,4,4,2);\n"
        "M change_m(M);P change_p(P);Z change_z(Z);U change_u(U);\n"
        "int inspect(const M*,const P*,const Z*,const U*);\n";
    static const char library[] =
        "#include \"ms-struct-abi.h\"\n"
        "M change_m(M v){v.a+=3;v.b+=5;v.head++;v.tail+=2;return v;}\n"
        "P change_p(P v){v.a+=3;v.b+=5;v.head++;v.tail+=2;return v;}\n"
        "Z change_z(Z v){v.a+=3;v.b+=5;v.tail+=2;return v;}\n"
        "U change_u(U v){v.a+=3;return v;}\n"
        "int inspect(const M*m,const P*p,const Z*z,const U*u){\n"
        "return m->head!=11||m->a!=1||m->b!=2||m->tail!=13||p->head!=21||p->a!=1||p->b!=2||p->tail!=23||\n"
        "z->a!=1||z->b!=2||z->tail!=31||u->b!=2||((const unsigned char*)m)[4]!=17||((const unsigned char*)p)[1]!=17||\n"
        "(((const unsigned char*)z)[4]&7)!=2||(((const unsigned char*)u)[0]&31)!=2;}\n";
    static const char caller[] =
        "#include \"ms-struct-abi.h\"\n"
        "static void zero(void*p,unsigned n){unsigned char*b=p;for(unsigned i=0;i<n;i++)b[i]=0;}\n"
        "int main(void){M m;P p;Z z;U u;zero(&m,sizeof m);zero(&p,sizeof p);zero(&z,sizeof z);zero(&u,sizeof u);\n"
        "m.head=11;m.a=1;m.b=2;m.tail=13;p.head=21;p.a=1;p.b=2;p.tail=23;z.a=1;z.b=2;z.tail=31;u.b=2;\n"
        "if(inspect(&m,&p,&z,&u))return 1;m=change_m(m);p=change_p(p);z=change_z(z);u=change_u(u);\n"
        "return m.head!=12||m.a!=4||m.b!=7||m.tail!=15||p.head!=22||p.a!=4||p.b!=7||p.tail!=25||z.a!=4||z.b!=7||z.tail!=33||u.a!=5;}\n";
    char header_path[512];
    char sources[2][512];
    char objects[3][2][512];
    char binary[512];
    snprintf(header_path, sizeof(header_path), "%s/ms-struct-abi.h", directory);
    snprintf(sources[0], sizeof(sources[0]), "%s/ms-struct-library.c", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/ms-struct-caller.c", directory);
    snprintf(binary, sizeof(binary), "%s/ms-struct-run", directory);
    const bool written = parse_test_write(header_path, header, sizeof(header) - 1) && parse_test_write(sources[0], library, sizeof(library) - 1) &&
                         parse_test_write(sources[1], caller, sizeof(caller) - 1);
    check_test_expect(test, written, "Microsoft layout ABI source fixtures are written");
    for (uint32_t tier = 0; written && tier < 2; tier += 1)
    {
        bool compiled[3][2] = { { false } };
        for (uint32_t producer = 0; producer < 3; producer += 1)
        {
            for (uint32_t part = 0; part < 2; part += 1)
            {
                snprintf(objects[producer][part], sizeof(objects[producer][part]), "%s/ms-struct-%u-%u.o", directory, producer, part);
                char* arguments[] = { (char*)(producer == 0 ? clang : executable), "-std=gnu17", tier != 0 ? "-O2" : "-O0", "-c", sources[part], "-o",
                                      objects[producer][part], producer == 2 ? "--scalar" : NULL, NULL };
                char* text = NULL;
                compiled[producer][part] = check_test_driver_spawn(arguments, &text) == 0;
                check_test_expect(test, compiled[producer][part], "Microsoft record definitions and fixed layout assertions compile");
                if (!compiled[producer][part])
                {
                    fprintf(stderr, "ms_struct ABI tier %u producer %u part %u: %s\n", tier, producer, part, text != NULL ? text : "");
                }
                free(text);
            }
        }
        for (uint32_t callee = 0; callee < 3; callee += 1)
        {
            for (uint32_t caller_index = 0; caller_index < 3; caller_index += 1)
            {
                char* arguments[] = { (char*)clang, objects[callee][0], objects[caller_index][1], "-o", binary, NULL };
                char* text = NULL;
                const bool linked = compiled[callee][0] && compiled[caller_index][1] && check_test_driver_spawn(arguments, &text) == 0;
                free(text);
                char* run[] = { binary, NULL };
                text = NULL;
                const int status = linked ? check_test_driver_spawn(run, &text) : -1;
                check_test_expect(test, linked && status == 0, "Clang and both Buster paths exchange Microsoft records and bit-field values in both directions");
                if (!linked || status != 0)
                {
                    fprintf(stderr, "ms_struct ABI tier %u callee %u caller %u: linked %u status %d %s\n", tier, callee, caller_index, linked, status, text != NULL ? text : "");
                }
                free(text);
            }
        }
        for (uint32_t part = 0; part < 2; part += 1)
        {
            check_test_expect(test, compiled[1][part] && compiled[2][part] && check_test_driver_same_files(objects[1][part], objects[2][part]),
                              "Microsoft layout scalar and vector paths emit identical objects");
        }
        for (uint32_t producer = 0; producer < 3; producer += 1)
        {
            for (uint32_t part = 0; part < 2; part += 1)
            {
                unlink(objects[producer][part]);
            }
        }
    }
    unlink(binary);
    unlink(sources[0]);
    unlink(sources[1]);
    unlink(header_path);
}
