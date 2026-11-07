// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original default-warning sources. Clang supplies the external diagnostic oracle, including source policy.
// Include after the checker driver's spawn, count and file-comparison helpers. Initializer-only families have
// separate fixtures. These cases compare complete no-caret diagnostics through both kernels and lane requests.

typedef struct CheckDriverDefaultWarningFixture CheckDriverDefaultWarningFixture;
struct CheckDriverDefaultWarningFixture
{
    const char* name;
    const char* source;
    const char* options[4];
    uint32_t warnings;
    uint32_t errors;
    uint32_t notes;
    bool c23;
    bool compare_warning_text;
};

static const char check_driver_format_plain_source[] =
    "typedef struct _IO_FILE FILE;\n"
    "int printf(const char*,...);\nint fprintf(FILE*,const char*,...);\n"
    "int sprintf(char*,const char*,...);\nint snprintf(char*,unsigned long,const char*,...);\n"
    "void f(FILE*p,char*b){printf(\"%d\",\"str\");fprintf(p,\"%d\",\"str\");"
    "sprintf(b,\"%d\",\"str\");snprintf(b,16,\"%d\",\"str\");snprintf(b,16,\"%s.mo\\0\",\"x\");}\n";

// The first attribute survives a bare redeclaration; the second is attached after a bare declaration.
// A non-library name with a different format index keeps its source contract under the same options.
static const char check_driver_format_attributed_source[] =
    "int printf(const char*,...)__attribute__((format(printf,1,2)));\nint printf(const char*,...);\n"
    "int snprintf(char*,unsigned long,const char*,...);\n"
    "int snprintf(char*,unsigned long,const char*,...)__attribute__((format(printf,3,4)));\n"
    "int trace(int,const char*,...)__attribute__((format(printf,2,3)));\n"
    "void f(char*b){printf(\"%d\",\"str\");snprintf(b,16,\"%d\",\"str\");trace(0,\"%d\",\"str\");}\n";

static const CheckDriverDefaultWarningFixture check_driver_default_warning_fixtures[] =
{
    {
        .name = "return flow conservative no-fallthrough and reachable fallthrough",
        .source = "int a(int c){if(c)return 1;return 2;}int b(int c){if(c)return 1;}int d(void){while(1){}}_Noreturn void r(void){while(1){}}\n",
        .warnings = 1,
    },
    {
        .name = "floating literal file and local initializers",
        .source = "int a=1.5;\nint f(void){int b=2.5;return b;}\n",
        .warnings = 2,
    },
    {
        .name = "floating literal assignment return and call conversions",
        .source = "int f(void){int a;a=1.5;return -2.5;}\nvoid g(int);void h(void){g(3.5);}\n",
        .warnings = 3,
    },
    {
        .name = "floating literal source formats",
        .source = "int a=1.1f;int b=1.1;int c=1.1L;\n",
        .warnings = 3,
    },
    {
        .name = "floating literal array and record initializer elements",
        .source = "int values[2]={1.5,2.5};\nstruct S{int n;}object={1.5};\n",
        .warnings = 3,
    },
    {
        .name = "floating literal local brace initializer order",
        .source = "int f(void){int a[2]={1.5,2.5};int b={3.5};return a[0]+b;}\n",
        .warnings = 3,
    },
    {
        .name = "floating literal scalar and aggregate declaration order",
        .source = "int f(void){int x=4.5;int a[2]={1.5,2.5};int y=3.5;return x+a[0]+y;}\n",
        .warnings = 4,
    },
    {
        .name = "floating literal range initializer conversions",
        .source = "int values[4]={[0 ... 3]=1.5};\n",
        .warnings = 4,
    },
    {
        .name = "floating literal decimal and hexadecimal spelling",
        .source = "int a=0.9999999999999999;int b=1e-30;int c=0x1.8p0;\n",
        .warnings = 3,
    },
    {
        .name = "floating literal adjacent integer precision",
        .source = "int a=1.9999999999999998;int b=1.0000000000000002;\n",
        .warnings = 2,
    },
    {
        .name = "floating literal call argument subtrees follow right to left conversion order",
        .source = "void f(int,int);int g(int);void h(void){f(g(1.5),2.5);f(3.5,g(4.5));f(5.5,6.5);}\n",
        .warnings = 6,
    },
    {
        .name = "floating literal nested call and comma conversions are emitted once",
        .source = "void f(int,int);int g(int);void h(void){f(g(1.5),g(2.5));f(3.5,4.5),f(5.5,6.5);}\n",
        .warnings = 6,
    },
    {
        .name = "binary operand calls visit literal conversions right to left",
        .source = "int f(int);int h(void){return f(1.5)+f(2.5);}\n",
        .warnings = 2,
    },
    {
        .name = "comma assignment conversions visit the right operand first",
        .source = "int h(void){int a,b;a=1.5,b=2.5;return a+b;}\n",
        .warnings = 2,
    },
    {
        .name = "reverse conversions precede source ordered bounds and unused results",
        .source = "int f(int);void h(void){int a[1];(f(1.5)+a[2]),(f(2.5)+a[3]);}\n",
        .warnings = 6,
        .notes = 2,
    },
    {
        .name = "reverse conversions preserve each inner comma marker before its bounds warning",
        .source = "void f(int);void h(void){int a[1];(a[2],f(1.5)),(a[3],f(2.5));}\n",
        .warnings = 6,
        .notes = 2,
    },
    {
        .name = "comma integer call conversions follow the same reverse walk",
        .source = "short f(short);void h(void){f(65536),f(65537);}\n",
        .warnings = 2,
    },
    {
        .name = "brace initializer conversions precede nested call conversions",
        .source = "int f(int);int h(void){int a[2]={(f(1.5),2.5),(f(3.5),4.5)};return a[0];}\n",
        .warnings = 4,
    },
    {
        .name = "unreachable return and local initializer retain literal conversion warnings",
        .source = "int f(void){if(0)return 1.5;if(0){int a=2.5;(void)a;}return 0;}\n",
        .warnings = 2,
    },
    {
        .name = "unreachable call conversion precedes the later reachable conversion",
        .source = "void f(int);void h(void){if(0)f(1.5);f(2.5);}\n",
        .warnings = 2,
    },
    {
        .name = "unreachable literal conversions do not enable runtime warning families",
        .source = "int f(void){int a[1];if(0){int x=1.5;(void)x;1;a[2];return 1/0;}return 0;}\n",
        .warnings = 1,
    },
    {
        .name = "short-circuited and conditional call literals remain source checks",
        .source = "int g(int);int f(void){return 0&&g(1.5);}int h(void){return 1?0:g(2.5);}\n",
        .warnings = 2,
    },
    {
        .name = "conditional call arms retain literal conversions",
        .source = "int f(int); void h(int c){c ? f(1.5) : f(2.5);}\n",
        .warnings = 2,
    },
    {
        .name = "comma conditional arms do not acquire outer conversion",
        .source = "int side(void);void take(int);int f(int c){int x=c?(side(),2.5):3.0;x=c?3.0:(side(),2.5);take(c?(side(),2.5):3.0);return c?(side(),2.5):3.0;}\n",
        .options = { "-Werror=literal-conversion" },
        .c23 = true,
    },
    {
        .name = "nested comma and conditional comma results do not inherit literal conversion",
        .source = "int side(void);int f(int c){return (side(),(side(),2.5));}int g(int c){return (side(),(c?2.5:2.5));}\n",
        .options = { "-Werror=literal-conversion" },
        .c23 = true,
    },
    {
        .name = "integer conversion of comma results uses the whole result expression",
        .source = "int side(void);short f(int c){return (side(),(c?65536:65537));}short g(void){return (side(),(1?65536:65537));}short h(void){return (side(),(side(),65536));}short i(int c){return c?(side(),65536):65537;}\n",
        .warnings = 4,
    },
    {
        .name = "top level comma still receives outer conversion",
        .source = "int side(void);int f(void){return (side(),2.5);}\n",
        .warnings = 1,
    },
    {
        .name = "unevaluated sizeof and generic arms suppress literal conversions",
        .source = "int f(void){return sizeof((int){1.5})+_Generic(0,int:0,default:(int){2.5});}\n",
    },
    {
        .name = "conditional literal arms inherit initializer and return targets",
        .source = "int a=0?1.5:2.5;int b=1?3.5:4.5;int f(int c){return c?5.5:6.5;}\n",
        .warnings = 6,
    },
    {
        .name = "conditional brace initializer and unreachable argument targets",
        .source = "int a[1]={0?1.5:2.5};void f(int);void h(int c){if(0)f(c?3.5:4.5);}\n",
        .warnings = 4,
    },
    {
        .name = "conditional integer conversions retain selected-arm pruning",
        .source = "short a=0?65536:65537;short b=1?65538:65539;\n",
        .warnings = 2,
    },
    {
        .name = "nested integer argument conversions follow right to left order",
        .source = "void f(short,short);short g(short);void h(void){f(g(65536),65537);}\n",
        .warnings = 2,
    },
    {
        .name = "unreachable integer conversion families remain suppressed",
        .source = "void f(short);short g(void){if(0)return 65536;if(0){short a=65536;(void)a;}return 0;}"
                  "void h(void){if(0)f(65536);f(65537);}int i(void){return 0&&(f(65536),1);}int j(void){return 1?0:(f(65536),1);}\n",
        .warnings = 1,
    },
    {
        .name = "initializers after return retain only literal source conversions",
        .source = "int h(void){return 0;short a=65536;int b=1.5;(void)a;(void)b;}\n",
        .warnings = 1,
    },
    {
        .name = "floating literal boolean conversions",
        .source = "_Bool a=1.5;_Bool b=2.0;_Bool c=1.0;\n",
        .warnings = 2,
    },
    {
        .name = "floating literal out of range conversions",
        .source = "unsigned char a=1000.5;unsigned b=-1.5;int c=1e30;\n",
        .warnings = 3,
    },
    {
        .name = "floating literal macro expansion note",
        .source = "#define VALUE 1.5\nint a=VALUE;\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "floating literal parentheses retain the literal warning",
        .source = "int a=((1.5));int b=+2.5;int c=-3.5;\n",
        .warnings = 3,
    },
    {
        .name = "explicit and nonliteral floating conversions stay quiet by default",
        .source = "int a=(double)1.5f;int b=(int)1.5;int c=2.5-1.0;int d=2.5+0.0;int e=1.0;\n"
                  "int f(double a){return a;}int g(void){double a=1.5;return a;}\nint h=__builtin_nan(\"\");int i=__builtin_inf();\n",
    },
    {
        .name = "floating literal conversion group disabled",
        .source = "int a=1.5;\n",
        .options = { "-Wno-literal-conversion" },
    },
    {
        .name = "floating literal conversion parent group disabled",
        .source = "int a=1.5;\n",
        .options = { "-Wno-conversion" },
    },
    {
        .name = "floating literal conversion source policy",
        .source = "#pragma clang diagnostic error \"-Wliteral-conversion\"\nint a=1.5;\n",
        .errors = 1,
    },
    {
        .name = "float conversion error does not promote literal conversion",
        .source = "int a=1.5;\n",
        .options = { "-Werror=float-conversion" },
        .warnings = 1,
    },
    {
        .name = "float conversion disabled does not suppress literal conversion",
        .source = "int a=1.5;\n",
        .options = { "-Wno-float-conversion" },
        .warnings = 1,
    },
    {
        .name = "literal disabled before float conversion error",
        .source = "int a=1.5;\n",
        .options = { "-Wno-literal-conversion", "-Werror=float-conversion" },
    },
    {
        .name = "literal error after float conversion disabled",
        .source = "int a=1.5;\n",
        .options = { "-Wno-float-conversion", "-Werror=literal-conversion" },
        .errors = 1,
    },
    {
        .name = "float conversion source policy does not promote literal conversion",
        .source = "#pragma clang diagnostic error \"-Wfloat-conversion\"\nint a=1.5;\n",
        .warnings = 1,
    },
    {
        .name = "literal source policy remains disabled across float conversion error",
        .source = "#pragma clang diagnostic ignored \"-Wliteral-conversion\"\n#pragma clang diagnostic error \"-Wfloat-conversion\"\nint a=1.5;\n",
    },
    {
        .name = "file initializer discards pointer qualifiers at its declaration",
        .source = "const int b=1;\nint *c=&b;\n",
        .warnings = 1,
    },
    {
        .name = "local initializer discards pointer qualifiers",
        .source = "int f(void){const int b=1;int *c=&b;return *c;}\n",
        .warnings = 1,
    },
    {
        .name = "assignment discards pointer qualifiers",
        .source = "void f(const int *a,int *b){b=a;}\n",
        .warnings = 1,
    },
    {
        .name = "return discards pointer qualifiers at its expression",
        .source = "const int b=1;\nint *f(void){return &b;}\n",
        .warnings = 1,
    },
    {
        .name = "discarded pointer qualifiers follow named policy",
        .source = "const int b=1;\nint *c=&b;\n",
        .options = { "-Wno-incompatible-pointer-types-discards-qualifiers" },
    },
    {
        .name = "discarded pointer qualifiers honor source severity",
        .source = "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\nconst int b=1;\nint *c=&b;\n",
        .errors = 1,
    },
    {
        .name = "discarded pointer qualifiers retain typedef spellings",
        .source = "typedef const int CI;CI b=1;typedef int *P;P c=&b;\n",
        .warnings = 1,
    },
    {
        .name = "discarded array qualifiers retain the source array type",
        .source = "const int b[2]={1,2};int *c=b;\n",
        .warnings = 1,
    },
    {
        .name = "discarded pointer qualifiers retain outer const spelling",
        .source = "const int b=1;int *const c=&b;\n"
                  "void f(const int *const a){int *const b=a;(void)b;}\n"
                  "int *g(const int *const a){return a;}\n",
        .warnings = 3,
    },
    {
        .name = "plain char pointer initializer and return conversion",
        .source = "unsigned char a[2];char *b=a;\nchar *f(void){return a;}\n",
        .warnings = 2,
    },
    {
        .name = "direct call qualifier warnings retain named and unnamed parameter notes",
        .source = "void f(int *,int *b);void g(const int *p){f(p,p);}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "direct call parameter note uses the visible redeclaration",
        .source = "void f(int *first);void f(int *second);void g(const int *p){f(p);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "empty-list redeclaration retains conversion checks without a parameter note",
        .source = "void f(int *first);void f();void g(const int *p){f(p);}\n",
        .options = { "-Wno-deprecated-non-prototype" },
        .warnings = 1,
    },
    {
        .name = "closed-block prototype supplies no parameter note to an unprototyped call",
        .source = "void earlier(void){void f(int *hidden);}void f();void g(const int *p){f(p);}\n",
        .options = { "-Wno-deprecated-non-prototype" },
    },
    {
        .name = "function pointer variable call has no parameter declaration note",
        .source = "void (*f)(int *parameter);void g(const int *p){f(p);}\n",
        .warnings = 1,
    },
    {
        .name = "function pointer typedef and parameter calls have no declaration notes",
        .source = "typedef void (*F)(int *parameter);F f;void g(const int *p){f(p);}\n"
                  "void h(void (*callback)(int *parameter),const int *p){callback(p);}\n",
        .warnings = 2,
    },
    {
        .name = "dereferenced and cast callees have no parameter declaration notes",
        .source = "void f(int *parameter);void g(const int *p){(*f)(p);((void(*)(int *))f)(p);}\n",
        .warnings = 2,
    },
    {
        .name = "pointer argument warning is located at the expression beginning",
        .source = "void f(int *parameter);void g(const int *p){f(p+1);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "nested pointer qualifier wording includes the parameter note",
        .source = "void f(int **parameter);void g(const int **p){f(p);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "pointer call macro expansion precedes its parameter note",
        .source = "#define F f\n#define P p\nvoid f(int *parameter);void g(const int *p){F(P);}\n",
        .warnings = 1,
        .notes = 2,
    },
    {
        .name = "mixed pointer conversion groups follow argument order",
        .source = "void f(int *a,int *b);void g(const int *p,unsigned int *q){f(q,p);f(p,q);}\n",
        .warnings = 4,
        .notes = 4,
    },
    {
        .name = "plain char signedness overrides an array qualifier discard",
        .source = "void f(char *p);void g(void){const unsigned char a[2]={1,2};f(a);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "pointer parameter notes skip intervening scalar and callback parameters",
        .source = "void f(int *,int count,void callback(int *),int *last);\n"
                  "void g(const int *p){f(p,0,0,p);}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "pointer parameter type spelling belongs to its own specifiers",
        .source = "typedef int *P;void f(P first,int *second);void g(const int *p){f(p,p);}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "pointer call qualifiers and signedness obey independent group policy",
        .source = "void f(int *a,int *b);void g(const int *p,unsigned int *q){f(p,q);}\n",
        .options = { "-Wno-incompatible-pointer-types-discards-qualifiers" },
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "pointer call warning promotion retains its parameter note",
        .source = "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\n"
                  "void f(int *parameter);void g(const int *p){f(p);}\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "nested pointer initialization assignment and return wording",
        .source = "void f(const int **p,int **q){int **r=p;q=p;(void)r;}\nint **g(const int **p){return p;}\n",
        .warnings = 3,
    },
    {
        .name = "nonconstant file initializer",
        .source = "int a;\nint b=a;\n",
        .errors = 1,
    },
    {
        .name = "nonconstant aggregate initializer element",
        .source = "int a;\nint values[2]={1,a};\n",
        .errors = 1,
    },
    {
        .name = "nonconstant static local initializer",
        .source = "int read(void);\nint f(void){static int value=read();return value;}\n",
        .errors = 1,
    },
    {
        .name = "duplicate qualifiers and declaration specifiers",
        .source = "const const int a;\nvolatile volatile int b;\n_Atomic _Atomic int c;\nextern extern int d;\n"
                  "static static int e;\ntypedef typedef int T;\nunsigned unsigned int f;\n",
        .warnings = 7,
    },
    {
        .name = "duplicate function specifiers",
        .source = "inline inline void f(void){}\n_Noreturn _Noreturn void g(void){for(;;){}}\n",
        .warnings = 2,
    },
    {
        .name = "duplicate separated noreturn specifiers",
        .source = "_Noreturn void _Noreturn f(void){for(;;){}}\n_Noreturn extern _Noreturn void g(void);\n",
        .warnings = 2,
    },
    {
        .name = "duplicate parameter and pointer qualifiers",
        .source = "int f(const const int x,int *volatile volatile p){return x+*p;}\n",
        .warnings = 2,
    },
    {
        .name = "duplicate declaration specifier group disabled",
        .source = "const const int value;\n",
        .options = { "-Wno-duplicate-decl-specifier" },
    },
    {
        .name = "duplicate declaration specifier source policy",
        .source = "#pragma clang diagnostic ignored \"-Wduplicate-decl-specifier\"\nconst const int value;\n",
    },
    {
        .name = "noreturn fall off",
        .source = "_Noreturn void f(void){}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn explicit return",
        .source = "_Noreturn void f(void){return;}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn valued return",
        .source = "__attribute__((noreturn)) int f(void){return 1;}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn dead explicit return",
        .source = "_Noreturn void f(void){if(0)return;for(;;){}}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn ordinary call can return",
        .source = "void g(void);_Noreturn void f(void){g();}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn call ends the path",
        .source = "_Noreturn void g(void);_Noreturn void f(void){g();}\n",
    },
    {
        .name = "noreturn partial terminal path",
        .source = "_Noreturn void g(void);_Noreturn void f(int x){if(x)g();}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn trap ends the path",
        .source = "_Noreturn void f(void){__builtin_trap();}\n",
    },
    {
        .name = "noreturn cast terminal path",
        .source = "_Noreturn int g(void);_Noreturn void f(void){(void)g();}\n",
    },
    {
        .name = "noreturn partial cast terminal path",
        .source = "_Noreturn int g(void);_Noreturn void f(int x){if(x)(void)g();}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn partial trap path",
        .source = "_Noreturn void f(int x){if(x)__builtin_trap();}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn independent of return type warning policy",
        .source = "_Noreturn void f(void){}\n",
        .options = { "-Wno-return-type" },
        .warnings = 1,
    },
    {
        .name = "noreturn group disabled",
        .source = "_Noreturn void f(void){return;}\n",
        .options = { "-Wno-invalid-noreturn" },
    },
    {
        .name = "noreturn source policy",
        .source = "#pragma clang diagnostic ignored \"-Winvalid-noreturn\"\n_Noreturn void f(void){}\n",
    },
    {
        .name = "scalar return fall off after ordinary call",
        .source = "void g(void);int f(void){g();}\n",
        .warnings = 1,
    },
    {
        .name = "scalar return partial terminal path",
        .source = "_Noreturn void g(void);int f(int x){if(x)g();}\n",
        .warnings = 1,
    },
    {
        .name = "scalar return terminal call",
        .source = "_Noreturn int g(void);int f(void){(void)g();}\n",
    },
    {
        .name = "scalar return partial trap path",
        .source = "int f(int x){if(x)__builtin_trap();}\n",
        .warnings = 1,
    },
    {
        .name = "private extern declarations",
        .source = "__private_extern__ int x;\n__private_extern__ int f(void);\nvoid g(void){__private_extern__ int y;}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "private extern definitions",
        .source = "__private_extern__ int x=7;\n__private_extern__ int f(void){return x;}\n",
    },
    {
        .name = "private extern direct initializer after attribute",
        .source = "__private_extern__ int x __attribute__((unused)) (3);\n",
    },
    {
        .name = "private extern thread local declaration",
        .source = "__private_extern__ __thread int x;\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "private extern redeclarations",
        .source = "__private_extern__ int x;int x;\nint y;__private_extern__ int y;\n__private_extern__ int z;int z=3;\n",
        .warnings = 3,
        .notes = 3,
    },
    {
        .name = "private extern multiple declarators",
        .source = "__private_extern__ int x,y=2,z;\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "private extern duplicate specifier",
        .source = "__private_extern__ __private_extern__ int x;\n",
        .warnings = 2,
        .notes = 1,
    },
    {
        .name = "private extern group disabled",
        .source = "__private_extern__ int x;\n",
        .options = { "-Wno-private-extern" },
    },
    {
        .name = "private extern source policy",
        .source = "#pragma clang diagnostic ignored \"-Wprivate-extern\"\n__private_extern__ int x;\n",
    },
    {
        .name = "first block annotation survives its own later bare declaration",
        .source = "void declare(void){extern int printf(const char*,...)__attribute__((format(printf,1,2)));"
                  "extern int printf(const char*,...);}\nint printf(const char*,...);\n"
                  "void call(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
        .warnings = 1,
    },
    {
        .name = "first bare block extern does not export a later block annotation",
        .source = "void declare(void){extern int printf(const char*,...);"
                  "extern int printf(const char*,...)__attribute__((format(printf,1,2)));"
                  "extern int printf(const char*,...);}\nint printf(const char*,...);\n"
                  "void call(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
    },
    {
        .name = "all supported explicit format builtins keep their source argument indexes",
        .source = "typedef struct _IO_FILE FILE;\nvoid f(FILE*p,char*b){"
                  "__builtin_printf(\"%d\",\"str\");__builtin_fprintf(p,\"%d\",\"str\");"
                  "__builtin_sprintf(b,\"%d\",\"str\");__builtin_snprintf(b,16,\"%d\",\"str\");"
                  "__builtin_snprintf(b,16,\"%s.mo\\0\",\"x\");}\n",
        .options = { "-ffreestanding", "-fno-builtin" },
        .warnings = 5,
    },
    {
        .name = "explicit format receipts follow interleaved unrelated intrinsic calls",
        .source = "void f(const char*s){(void)__builtin_strlen(s);__builtin_printf(\"%d\",\"str\");(void)__builtin_strlen(s);}\n",
        .options = { "-fno-builtin" },
        .warnings = 1,
    },
    {
        .name = "first block extern format contract reaches the first file declaration",
        .source = "void declare(void){extern int printf(const char*,...)__attribute__((format(printf,1,2)));}\n"
                  "int printf(const char*,...);\nvoid call(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
        .warnings = 1,
    },
    {
        .name = "later file declaration does not import a nested outer redeclaration annotation",
        .source = "int printf(const char*,...);\n"
                  "void declare(void){extern int printf(const char*,...)__attribute__((format(printf,1,2)));}\n"
                  "int printf(const char*,...);\nvoid call(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
    },
    {
        .name = "middle format annotation survives a final bare declaration",
        .source = "int printf(const char*,...);\nint printf(const char*,...)__attribute__((format(printf,1,2)));\n"
                  "int printf(const char*,...);\nvoid f(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
        .warnings = 1,
    },
    {
        .name = "later format annotation does not change an earlier call",
        .source = "int report(const char*,...);\nvoid before(void){report(\"%d\",\"str\");}\n"
                  "int report(const char*,...)__attribute__((format(printf,1,2)));\nint report(const char*,...);\n"
                  "void after(void){report(\"%d\",\"str\");}\n",
        .warnings = 1,
    },
    {
        .name = "block format inheritance follows the visible declaration",
        .source = "int report(const char*,...);\nvoid f(void){{"
                  "int report(const char*,...)__attribute__((format(printf,1,2)));"
                  "int report(const char*,...);report(\"%d\",\"str\");}"
                  "report(\"%d\",\"str\");}\n",
        .warnings = 1,
    },
    {
        .name = "bare format inheritance preserves unrelated declaration attributes",
        .source = "int report(const char*,...)__attribute__((format(printf,1,2)));\n"
                  "int report(const char*,...)__attribute__((noinline));\nvoid f(void){report(\"%d\",\"str\");}\n",
        .warnings = 1,
    },
    {
        .name = "hosted plain format names retain inferred checks",
        .source = check_driver_format_plain_source,
        .warnings = 5,
    },
    {
        .name = "freestanding plain format names have no inferred contract",
        .source = check_driver_format_plain_source,
        .options = { "-ffreestanding" },
    },
    {
        .name = "global no builtin disables plain format inference",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin" },
    },
    {
        .name = "per name exclusions disable all four supported format names",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin-printf", "-fno-builtin-fprintf", "-fno-builtin-sprintf", "-fno-builtin-snprintf" },
    },
    {
        .name = "printf exclusion preserves the other names",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin-printf" },
        .warnings = 4,
    },
    {
        .name = "snprintf exclusion disables its embedded NUL check too",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin-snprintf" },
        .warnings = 3,
    },
    {
        .name = "builtin exclusions match the whole name",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin-print" },
        .warnings = 5,
    },
    {
        .name = "later builtin option restores plain format inference",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin", "-fbuiltin" },
        .warnings = 5,
    },
    {
        .name = "later no builtin option disables plain format inference",
        .source = check_driver_format_plain_source,
        .options = { "-fbuiltin", "-fno-builtin" },
    },
    {
        .name = "freestanding preserves explicit and redeclared format contracts",
        .source = check_driver_format_attributed_source,
        .options = { "-ffreestanding" },
        .warnings = 3,
    },
    {
        .name = "global no builtin preserves explicit and redeclared format contracts",
        .source = check_driver_format_attributed_source,
        .options = { "-fno-builtin" },
        .warnings = 3,
    },
    {
        .name = "per name exclusions preserve explicit and redeclared format contracts",
        .source = check_driver_format_attributed_source,
        .options = { "-fno-builtin-printf", "-fno-builtin-snprintf" },
        .warnings = 3,
    },
    {
        .name = "plain dprintf needs an explicit format contract",
        .source = "int dprintf(int,const char*,...);\nvoid f(void){dprintf(1,\"%d\",\"str\");}\n",
    },
    {
        .name = "attributed dprintf remains checked when builtins are disabled",
        .source = "int dprintf(int,const char*,...)__attribute__((format(printf,2,3)));\nvoid f(void){dprintf(1,\"%d\",\"str\");}\n",
        .options = { "-ffreestanding", "-fno-builtin", "-fno-builtin-dprintf" },
        .warnings = 1,
    },
    {
        .name = "explicit builtin printf remains checked when builtins are disabled",
        .source = "void f(void){__builtin_printf(\"%d\",\"str\");}\n",
        .options = { "-ffreestanding", "-fno-builtin", "-fno-builtin-printf" },
        .warnings = 1,
    },
    {
        .name = "local printf function pointer has no inferred format contract",
        .source = "void f(void(*printf)(const char*,...)){printf(\"%d\",\"str\");}\n",
    },
    {
        .name = "valid plain format calls retain a quiet twin",
        .source = "int printf(const char*,...);\nint snprintf(char*,unsigned long,const char*,...);\nvoid f(char*b){printf(\"%d\",7);snprintf(b,16,\"%s.mo\",\"x\");}\n",
    },
    {
        .name = "one parenthesized comparison does not hide the other warning",
        .source = "int f(int a,int b,int c,int d){return ((a<b) & c<d)+(a==b ^ (c!=d));}\n",
        .warnings = 2,
        .notes = 4,
    },
    {
        .name = "parenthesized eager chain leaves outer precedence visible",
        .source = "int f(int a,int b,int c,int d,int e,int g){return (a<b & c<d) ^ e<g;}\n",
        .warnings = 1,
        .notes = 2,
    },
    {
        .name = "constant initializer comparison pairs",
        .source = "int a=1<2 & 3<4; static int b=1==2 ^ 3!=4; int f(void){return a+b;}\n",
    },
    {
        .name = "constant initializer single comparisons",
        .source = "int a=1<2 & 3; static int b=1 ^ 2!=3; int f(void){return a+b;}\n",
        .warnings = 2,
        .notes = 4,
    },
    {
        .name = "enum comparison pairs",
        .source = "enum E{A=1<2 & 3<4,B=1==2 ^ 3!=4};int f(void){return A+B;}\n",
    },
    {
        .name = "enum single comparisons",
        .source = "enum E{A=1<2 & 3,B=1 ^ 2!=3};int f(void){return A+B;}\n",
        .warnings = 2,
        .notes = 4,
    },
    {
        .name = "unevaluated comparison pairs",
        .source = "unsigned long f(int a,int b,int c,int d){return sizeof(a<b & c<d);}\n",
    },
    {
        .name = "unevaluated single comparison",
        .source = "unsigned long f(int a,int b,int c){return sizeof(a<b & c);}\n",
        .warnings = 1,
        .notes = 2,
    },
    {
        .name = "unselected generic comparison pair",
        .source = "int f(int a,int b,int c,int d){return _Generic(0,int:1,default:a<b ^ c<d);}\n",
    },
    {
        .name = "unselected generic single comparison",
        .source = "int f(int a,int b,int c){return _Generic(0,int:1,default:a<b ^ c);}\n",
        .warnings = 1,
        .notes = 2,
    },
    {
        .name = "two relational operands of each bitwise operator",
        .source = "int f(int a,int b,int c,int d){return (a<b & c<d)+(a>b | c>d)+(a<=b ^ c>=d);}\n",
    },
    {
        .name = "two equality operands of each bitwise operator",
        .source = "int f(int a,int b,int c,int d){return (a==b & c!=d)+(a!=b | c==d)+(a==b ^ c!=d);}\n",
    },
    {
        .name = "mixed equality and relational operands",
        .source = "int f(int a,int b,int c,int d){return (a==b & c<d)+(a>=b | c!=d)+(a!=b ^ c<=d);}\n",
    },
    {
        .name = "eager chains of three comparisons",
        .source = "int f(int a,int b,int c,int d,int e,int g){return (a<b ^ c<d ^ e<g)+(a==b & c==d & e==g)+(a!=b | c!=d | e!=g);}\n",
    },
    {
        .name = "mixed eager bitwise chains",
        .source = "int f(int a,int b,int c,int d,int e,int g){return (a<b & c<d ^ e<g)+(a==b ^ c==d | e==g);}\n",
    },
    {
        .name = "bare bitwise operand suppresses comparison precedence",
        .source = "int f(int a,int b,int c,int d){return (a&b ^ c<d)+(a<b | c^d);}\n",
    },
    {
        .name = "parenthesized comparisons are explicit",
        .source = "int f(int a,int b,int c,int d){return ((a<b) & (c<d))+((a==b)^c)+(a|(b!=c));}\n",
    },
    {
        .name = "relational left operand still warns once per operator",
        .source = "int f(int a,int b,int c){return (a<b & c)+(a<=b | c)+(a>b ^ c);}\n",
        .warnings = 3,
        .notes = 6,
    },
    {
        .name = "relational right operand still warns once per operator",
        .source = "int f(int a,int b,int c){return (a & b>=c)+(a | b<c)+(a ^ b<=c);}\n",
        .warnings = 3,
        .notes = 6,
    },
    {
        .name = "equality left operand still warns once per operator",
        .source = "int f(int a,int b,int c){return (a==b & c)+(a!=b | c)+(a==b ^ c);}\n",
        .warnings = 3,
        .notes = 6,
    },
    {
        .name = "equality right operand still warns once per operator",
        .source = "int f(int a,int b,int c){return (a & b!=c)+(a | b==c)+(a ^ b!=c);}\n",
        .warnings = 3,
        .notes = 6,
    },
    {
        .name = "parenthesized bitwise operand does not suppress warning",
        .source = "int f(int a,int b,int c,int d){return ((a&b) ^ c<d)+(a<b | (c^d));}\n",
        .warnings = 2,
        .notes = 4,
    },
    {
        .name = "nondecimal literal conversion bit patterns and lossy controls",
        .source = "#define X 0xe2\n"
                  "signed char take(signed char x) { return x; }\n"
                  "signed char small(void) { return (0xff); }\n"
                  "signed char large(void) { return 0x100; }\n"
                  "signed char single=0xff, overflow=0x1ff;\n"
                  "signed char values[8]; short s;\n"
                  "void f(void) { values[0]=0xe2; values[1]=(0342); values[2]=0b11100010; values[3]=X; s=0xffff; take(0x80); }\n"
                  "void hex_loss(void) { values[4]=0x100; }\n"
                  "void dec_loss(void) { values[5]=226; }\n"
                  "void calc_loss(void) { values[6]=0xe2+0; }\n"
                  "void unary_loss(void) { values[7]=+0xe2; }\n"
                  "void short_loss(void) { s=65535; }\n"
                  "void call_loss(void) { take(0x180); }\n",
        .warnings = 8,
        .c23 = true,
    },
    {
        .name = "nondecimal conversion with unsigned plain char",
        .source = "char c; signed char s; short h; void f(void) { c=0xe2; s=(0Xff); h=0177777; s=0x100; }\n",
        .options = { "-funsigned-char" },
        .warnings = 1,
    },
    {
        // Clang points at the cast, while the existing checker points at its literal. Compare the complete
        // warning text and severity, apart from that independent location discrepancy.
        .name = "explicit cast does not receive literal exemption",
        .source = "signed char s; void f(void) { s=(int)0xff; }\n",
        .options = { "-funsigned-char" },
        .warnings = 1,
        .compare_warning_text = true,
    },
    {
        .name = "nonliteral integer zero pointer conversion",
        .source = "int*p=1-1;\n",
        .warnings = 1,
    },
    {
        .name = "overflowing double literal",
        .source = "double value=1e400;\n",
        .warnings = 1,
    },
    {
        .name = "underflowing float literal",
        .source = "float value=1e-99f;\n",
        .warnings = 1,
    },
    {
        .name = "ordered pointer integer comparison",
        .source = "int f(int*p){return p<1;}\n",
        .warnings = 1,
    },
    {
        .name = "ordinary declaration declares nothing",
        .source = "int;\n",
        .warnings = 1,
    },
    {
        .name = "untagged record declares nothing",
        .source = "struct{int x;};\n",
        .warnings = 1,
    },
    {
        .name = "plain complex has an inferred element type",
        .source = "_Complex value;\n",
        .warnings = 1,
    },
    {
        .name = "member declaration declares nothing",
        .source = "struct S{int;int x;};\n",
        .warnings = 1,
    },
    {
        .name = "untagged enum member declares its enumerators",
        .source = "struct S{enum{E};};union U{enum{A};};struct T{struct{enum{B};};};int main(void){return E+A+B;}\n",
    },
    {
        .name = "anonymous union member declares a member",
        .source = "struct S{union{int x;};int y;};int main(void){struct S s;s.x=1;return s.x;}\n",
    },
    {
        .name = "named tag member declares nothing",
        .source = "struct S{struct Named{int x;};};\n",
        .warnings = 1,
    },
    {
        .name = "typedef without a declarator requires a name",
        .source = "typedef int;\n",
        .warnings = 1,
    },
    {
        .name = "typedef of a named record without a declarator requires a name",
        .source = "typedef struct S{int x;};\n",
        .warnings = 1,
    },
    {
        .name = "typedef of a named enum without a declarator requires a name",
        .source = "typedef enum E{A};\n",
        .warnings = 1,
    },
    {
        .name = "extern on a tag declaration is ignored",
        .source = "extern struct S;\n",
        .warnings = 1,
    },
    {
        .name = "static on a tag declaration is ignored",
        .source = "static union U;\n",
        .warnings = 1,
    },
    {
        .name = "const on a tag declaration is ignored",
        .source = "const struct S;\n",
        .warnings = 1,
    },
    {
        .name = "qualified ordinary declaration declares nothing",
        .source = "const int;\n",
        .warnings = 1,
    },
    {
        .name = "flexible record member is not last",
        .source = "struct S{int n;int a[];};struct P{struct S s;int after;};\n",
        .warnings = 1,
    },
    {
        .name = "external inline contains modifiable static local",
        .source = "inline int f(void){static int value;return value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "leading attribute keeps the inline note at the declaration",
        .source = "__attribute__((unused)) inline int g(void){static int value;return value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "leading extension keeps the inline note after it",
        .source = "__extension__ inline int g(void){static int value;return value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "trailing attribute keeps the inline note at its first declaration",
        .source = "inline int g(void) __attribute__((unused));\ninline int g(void){static int value;return value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "used internal function has no definition",
        .source = "static int missing(void);int f(void){return missing();}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated sizeof VLA bound use precedes a later call",
        .source = "static int m(void);void f(void){int a[sizeof(int[m()])];a[0]=m();(void)a;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated sizeof VLA bound use is a use alone",
        .source = "static int m(void);void f(void){int a[sizeof(int[m()])];(void)a;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated sizeof VLA bound use in an addition",
        .source = "static int m(void);int f(void){return sizeof(int[m()])+1;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated alignof VLA bound use",
        .source = "static int m(void);int f(void){return _Alignof(int[m()]);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated conditional VLA bound use",
        .source = "static int m(void);int f(void){return sizeof(int[1?m():2]);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated comma VLA bound use",
        .source = "static int m(void);int f(int n){return sizeof(int[(m(),n)]);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "undefined batch follows first use and survives its own promotion",
        .source = "static int first(void);static int second(void);int f(void){return second()+first();}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "earlier promoted literal error suppresses final undefined batch",
        .source = "static int missing(void);int f(void){return missing();}double value=1e400;\n",
        .warnings = 2,
        .notes = 1,
    },
    {
        .name = "mapped sparse error retains same body return analysis",
        .source = "struct S{long a,b;};struct S f(int n){int*p=1-1;(void)p;if(n)return(struct S){1,2};}\n",
        .options = { "-Werror=non-literal-null-conversion" },
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "pre C23 definition omits parameter name",
        .source = "int f(int){return 0;}\n",
        .warnings = 1,
    },
    {
        .name = "literal zero pointer conversion is quiet",
        .source = "int*p=0;\n",
    },
    {
        .name = "nonzero subnormal literal is quiet",
        .source = "float value=1e-45f;\n",
    },
    {
        .name = "exact literal zero with a small exponent is quiet",
        .source = "float value=0e-9999f;\n",
    },
    {
        .name = "explicit complex element type is quiet",
        .source = "_Complex double value;\n",
    },
    {
        .name = "terminal flexible record member is quiet",
        .source = "struct S{int n;int a[];};struct P{int before;struct S s;};\n",
    },
    {
        .name = "external inline constant static local is quiet",
        .source = "inline int f(void){static const int value=1;return value;}\n",
    },
    {
        .name = "later internal definition discharges the warning",
        .source = "static int missing(void);int f(void){return missing();}static int missing(void){return 1;}\n",
    },
    {
        .name = "unevaluated internal function reference is quiet",
        .source = "static int missing(void);unsigned long value=sizeof missing;\n",
    },
    {
        .name = "C23 definition may omit parameter name",
        .source = "int f(int){return 0;}\n",
        .c23 = true,
    },
    {
        .name = "null conversion source push pop retains named policy",
        .source = "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wnon-literal-null-conversion\"\n"
                  "int*quiet=1-1;\n#pragma clang diagnostic pop\nint*loud=2-2;\n",
        .warnings = 1,
    },
    {
        .name = "empty declaration source error is promoted",
        .source = "#pragma clang diagnostic error \"-Wmissing-declarations\"\nint;\n",
        .errors = 1,
    },
    {
        .name = "unnamed parameter source suppression is scoped",
        .source = "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wc23-extensions\"\n"
                  "int quiet(int){return 0;}\n#pragma clang diagnostic pop\nint loud(int){return 0;}\n",
        .warnings = 1,
    },
    {
        .name = "named no error overrides global warning promotion",
        .source = "int*p=1-1;\n",
        .options = { "-Werror", "-Wno-error=non-literal-null-conversion" },
        .warnings = 1,
    },
    {
        .name = "everything does not restore an explicitly disabled diagnostic",
        .source = "int*p=1-1;\n",
        .options = { "-Wno-everything", "-Weverything", "-Wno-missing-variable-declarations" },
    },
    {
        .name = "later broad suppression disables a named diagnostic",
        .source = "int*p=1-1;\n",
        .options = { "-Wnon-literal-null-conversion", "-Wno-everything" },
    },
    {
        .name = "later named diagnostic overrides broad suppression",
        .source = "int*p=1-1;\n",
        .options = { "-Wno-everything", "-Wnon-literal-null-conversion" },
        .warnings = 1,
    },
    {
        .name = "everything retains a named suppression",
        .source = "int*p=1-1;\n",
        .options = { "-Wno-non-literal-null-conversion", "-Weverything", "-Wno-missing-variable-declarations" },
    },
    {
        .name = "system warning families are suppressed by default",
        .source = "# 1 \"system-warning-705.h\" 3\nint*p=1-1;\ndouble value=1e400;\nint;\nint f(int){return 0;}\n"
                  "# 10 \"user-warning-705.c\"\nint ordinary;\n",
    },
    {
        .name = "system warning opt in enables sparse families",
        .source = "# 1 \"system-warning-705.h\" 3\nint*p=1-1;\ndouble value=1e400;\nint;\nint f(int){return 0;}\n"
                  "# 10 \"user-warning-705.c\"\nint ordinary;\n",
        .options = { "-Wsystem-headers" },
        .warnings = 4,
    },
    {
        .name = "user expansion of a system macro retains diagnostic notes",
        .source = "# 1 \"system-warning-705.h\" 3\n#define ZERO (1-1)\n# 10 \"user-warning-705.c\"\nint*p=ZERO;\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "independent bodies retain source order at requested lanes",
        .source = "int*f0(void){return 1-1;}\nint*f1(void){return 2-2;}\nint*f2(void){return 3-3;}\n",
        .warnings = 3,
    },
};

static void check_test_driver_warning_fixtures(CheckTest* test, const char* clang, const char* executable, const char* directory,
                                               const CheckDriverDefaultWarningFixture* fixtures, uint32_t fixture_count)
{
    static const char* const policies[] = { NULL, "-Werror", "-w" };
    static const char* const lanes[] = { "1", "3" };
    char source[PATH_MAX];
    char objects[4][PATH_MAX];
    snprintf(source, sizeof(source), "%s/default-warning.c", directory);
    for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
    {
        snprintf(objects[route], sizeof(objects[route]), "%s/default-warning-%u.o", directory, route);
    }
    for (uint32_t f = 0; f < fixture_count; f += 1)
    {
        const CheckDriverDefaultWarningFixture* fixture = fixtures + f;
        const bool written = parse_test_write(source, fixture->source, strlen(fixture->source));
        bool mismatch_printed = false;
        check_test_expect(test, written, "original default-warning fixture is written");
        for (uint32_t policy = 0; written && policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
        {
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                char* arguments[20] = { (char*)clang, fixture->c23 ? "-std=c23" : "-std=gnu17", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                        mode == 0 ? "-fsyntax-only" : "-c", source, "-o", objects[2] };
                uint32_t count = 8;
                for (uint32_t option = 0; option < BUSTER_ARRAY_LENGTH(fixture->options); option += 1)
                {
                    if (fixture->options[option] != NULL)
                    {
                        arguments[count++] = (char*)fixture->options[option];
                    }
                }
                if (policies[policy] != NULL)
                {
                    arguments[count++] = (char*)policies[policy];
                }
                unlink(objects[2]);
                char* expected = NULL;
                unlink(objects[3]);
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(expected);
                const bool oracle_ready = (oracle == 0 || oracle == 1) && expected != NULL;
                char description[256];
                snprintf(description, sizeof(description), "%s policy=%u mode=%u: independent Clang oracle", fixture->name, policy, mode);
                check_test_expect(test, oracle_ready && (policy != 0 || (oracle == (fixture->errors != 0) && counts.warnings == fixture->warnings &&
                                       counts.errors == fixture->errors && counts.notes == fixture->notes)), description);
                for (uint32_t lane = 0; oracle_ready && lane < BUSTER_ARRAY_LENGTH(lanes); lane += 1)
                {
                    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
                    {
                        arguments[0] = (char*)(executable != NULL ? executable : "/proc/self/exe");
                        arguments[7] = objects[scalar];
                        arguments[count] = "--lanes";
                        arguments[count + 1] = (char*)lanes[lane];
                        arguments[count + 2] = scalar != 0 ? "--scalar" : NULL;
                        arguments[count + 3] = NULL;
                        unlink(objects[scalar]);
                        static const char sentinel[] = "preserved-warning-output";
                        const bool preserve = mode != 0 && oracle == 1;
                        const bool prior = !preserve || parse_test_write(objects[scalar], sentinel, sizeof(sentinel));
                        char* actual = NULL;
                        const int status = check_test_driver_spawn(arguments, &actual);
                        snprintf(description, sizeof(description), "%s policy=%u mode=%u lanes=%s %s: complete Clang diagnostics",
                                 fixture->name, policy, mode, lanes[lane], scalar != 0 ? "scalar" : "vector");
                        const char* expected_text = fixture->compare_warning_text ? strstr(expected, "warning: ") : NULL;
                        expected_text = fixture->compare_warning_text && expected_text == NULL ? strstr(expected, "error: ") : expected_text;
                        const char* actual_text = fixture->compare_warning_text && expected_text != NULL ?
                                                  strstr(actual != NULL ? actual : "", policy == 1 ? "error: " : "warning: ") : NULL;
                        const bool diagnostics_match = expected_text != NULL ? actual_text != NULL && strcmp(actual_text, expected_text) == 0 :
                                                       actual != NULL && strcmp(actual, expected) == 0;
                        const bool same = status == oracle && diagnostics_match;
                        check_test_expect(test, same, description);
                        if (!same && !mismatch_printed)
                        {
                            fprintf(stderr, "%s: status %d (clang %d)\nBuster:\n%s\nClang:\n%s\n", description,
                                    status, oracle, actual != NULL ? actual : "<no output>\n", expected);
                            mismatch_printed = true;
                        }
                        if (preserve)
                        {
                            FILE* retained = fopen(objects[scalar], "rb");
                            char bytes[sizeof(sentinel)] = { 0 };
                            const bool unchanged = retained != NULL && fread(bytes, 1, sizeof(bytes), retained) == sizeof(bytes) &&
                                                   memcmp(bytes, sentinel, sizeof(bytes)) == 0 && fgetc(retained) == EOF;
                            check_test_expect(test, prior && unchanged, "promoted default warnings preserve an existing output file");
                            if (retained != NULL)
                            {
                                fclose(retained);
                            }
                        }
                        else
                        {
                            check_test_expect(test, (access(objects[scalar], F_OK) == 0) == (mode != 0 && status == 0),
                                              "default warning policy controls object publication");
                        }
                        free(actual);
                    }
                    if (mode != 0 && oracle == 0)
                    {
                        check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]),
                                          "default warning source has identical scalar/vector objects at each lane request");
                        if (lane == 0)
                        {
                            check_test_expect(test, link(objects[0], objects[3]) == 0, "the first lane request retains a frozen object twin");
                        }
                        else
                        {
                            check_test_expect(test, check_test_driver_same_files(objects[0], objects[3]),
                                              "default warning source has identical objects across requested lane counts");
                        }
                    }
                }
                free(expected);
                for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
                {
                    unlink(objects[route]);
                }
            }
        }
    }
    unlink(source);
}

static void check_test_driver_default_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_default_warning_fixtures, BUSTER_ARRAY_LENGTH(check_driver_default_warning_fixtures));
}

// -flazy-system-headers defers only system-header bodies. A unit with no deferred body must still publish its
// translation-unit-final warnings, exactly as a run without the flag does.
static void check_test_driver_lazy_final_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source_text[] = "static int missing(void);int f(void){return missing();}\n";
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/lazy-final-warning.c", directory);
    const bool written = parse_test_write(source, source_text, sizeof(source_text) - 1);
    char* expected = NULL;
    char* arguments[] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", "-fno-color-diagnostics", "-fsyntax-only", source, NULL };
    const int oracle = written ? check_test_driver_spawn(arguments, &expected) : -1;
    const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(expected);
    check_test_expect(test, oracle == 0 && expected != NULL && counts.warnings == 1 && counts.notes == 1,
                      "a lazy-system-header run without a deferred body has Clang's final warning oracle");
    for (uint32_t route = 0; written && route < 2; route += 1)
    {
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char* run[] = { (char*)(executable != NULL && route != 0 ? executable : "/proc/self/exe"), "-std=gnu17",
                            "-fno-caret-diagnostics", "-fno-color-diagnostics", "-fsyntax-only", "-flazy-system-headers", source,
                            scalar != 0 ? "--scalar" : NULL, NULL };
            char* actual = NULL;
            const int status = check_test_driver_spawn(run, &actual);
            check_test_expect(test, status == oracle && actual != NULL && expected != NULL && strcmp(actual, expected) == 0,
                              scalar != 0 ? "lazy-system-headers scalar run publishes the same final warning" :
                              "lazy-system-headers vector run publishes the same final warning");
            free(actual);
        }
    }
    free(expected);
    unlink(source);
}
