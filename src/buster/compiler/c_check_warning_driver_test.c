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
    bool syntax_only;
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
        // An enumeration or an _Atomic target takes neither -Wliteral-conversion nor -Wconstant-conversion in clang (#1447,
        // review on #1453): only a builtin type, qualified or not, does.
        .name = "conversion warnings skip enumeration and atomic targets",
        .source = "enum E { A, B };\nvoid arg(enum E);\nvoid argi(int);\nenum E fret(void) { return 3.5; }\n_Atomic int gy = 7.5;\n"
                  "_Atomic char gc = 300;\nvoid f(void) {\n  int x = 1.5; enum E e = 2.5; _Atomic int y = 3.5; _Atomic char z = 4.5;\n"
                  "  arg(4.5); argi(4.5); e = 5.5; y = 6.5; const int c = 8.5; volatile int v = 9.5;\n"
                  "  _Bool b = 0.5; _Atomic _Bool ab = 0.5; enum E e2 = 300; _Atomic char k = 300; char d = 300; z = 500;\n"
                  "  (void)x; (void)e; (void)y; (void)z; (void)c; (void)v; (void)b; (void)ab; (void)e2; (void)k; (void)d;\n}\n",
        .warnings = 6,
    },
    {
        // A brace element's destination is _Atomic by its declared type, which its plan part records stripped (#1447).
        .name = "conversion warnings skip atomic initializer elements",
        .source = "_Atomic char ga[1] = { 1.5 };\nstruct S { _Atomic char c; int i; };\nstruct S gs = { 1.5, 2.5 };\nvoid f(void) {\n"
                  "  _Atomic char a[2] = { 1.5, 2 };\n  struct S s = { 3.5, 4.5 };\n  struct { _Atomic int x; double d; } t = { .x = 5.5, .d = 1 };\n"
                  "  (void)a; (void)s; (void)t;\n}\n",
        .warnings = 2,
    },
    {
        // A return's or a prototype argument's _Atomic destination too, for either warning, beside the int, char and
        // plain parameter ones that still warn (review on #1453).
        .name = "conversion warnings skip atomic returns and arguments",
        .source = "_Atomic int f(void) { return 3.5; }\n_Atomic int gpar(_Atomic int);\nvoid h(void) { gpar(3.5); }\n"
                  "_Atomic char g(void) { return 300; }\nvoid gc(_Atomic char);\nvoid kc(void) { gc(300); }\n"
                  "struct S { int i; _Atomic int m; };\nvoid k(void) { struct S t; t.m = 3.5; t.m = 5000000000LL; (void)t; }\n"
                  "int fi(void) { return 2.5; }\nvoid pi(int);\nvoid hp(void) { pi(2.5); }\nchar fc(void) { return 300; }\n",
        .warnings = 3,
    },
    {
        // An enumeration takes -Wconstant-conversion by its integer type's range, though not -Wliteral-conversion, in
        // every position (review on #1453). (A brace element takes neither yet: #1759.)
        .name = "constant conversions to enumerations",
        .source = "enum Small : signed char { X };\nenum Small e = 300;\nenum Small rs(void) { return 300; }\n"
                  "void ca(enum Small);\nvoid cc(void) { ca(300); }\n"
                  "void as(void) { enum Small s; s = 300; (void)s; }\nenum FB : _Bool { FA };\nenum FB fb = 2;\n"
                  "enum E { A, B };\nenum E ee = 5000000000LL;\nenum Small lit = 2.5;\n",
        .warnings = 6,
        .c23 = true,
    },
    {
        .name = "floating literal assignment return and call conversions",
        .source = "int f(void){int a;a=1.5;return -2.5;}\nvoid g(int);void h(void){g(3.5);}\n",
        .warnings = 3,
    },
    {
        // A truth context converts a floating literal to _Bool (#147): conditions, `!`, `&&`, `||` and a conditional's first
        // operand, in unreachable code and short-circuited operands too; never a conditional's arms, a comma's result or
        // sizeof's operand.
        .name = "floating literal truth contexts",
        .source = "int f(int x){if(0.25)x=1;while(0.5)break;for(;0.75;)break;do{}while(1.5);return (2.5&&x)+(x||-3.5)+!4.5+(5.5?x:1)+(1.0&&x)+(0.0||x);}\n"
                  "int g(int x){return (x?1.5:2.5)&&x;}int h(int x){return (x,1.5)&&x;}unsigned long k(int x){return sizeof(1.5&&x);}\n"
                  "int u(int x){if(0)return 6.5&&x;return 0&&7.5;}\n_Bool b=1.5;_Bool c(void){return 2.5;}\n",
        .warnings = 13,
    },
    {
        // A static assertion's condition is a truth context too, warned where clang's parser reads it: between a declaration's
        // initializer and a later statement's, at file scope, and in unreachable code.
        .name = "floating literal static assertions",
        .source = "int o(int x){int y=2.5;_Static_assert(1.5,\"\");_Static_assert(0.0==0,\"\");return y+(x&&3.5);}\n_Static_assert(-0.5,\"\");\n"
                  "_Static_assert(1.5&&2.5,\"\");int z(int x){if(0){_Static_assert(0.5,\"\");}return x;}\n",
        .warnings = 7,
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
        // An unreachable initializer's barriers outlive its body's scratch (#1447): the next body's marks and queue
        // must not reuse them, which made a later body's initialized variables "uninitialized when used here".
        .name = "unreachable initializer barriers outlive their body",
        .source = "int f(int n) {\n  return n;\n  int dead = n;\n  return dead;\n}\n"
                  "int g(int n) {\n  int a = n + 1;\n  if (n > 1) n = a;\n  int b = n + 2;\n  if (n > 2) n = b;\n"
                  "  int c = n + 3;\n  if (n > 3) n = c;\n  int d = n + 4;\n  if (n > 4) n = d;\n  return n;\n}\n",
        .options = { "-Wall" },
    },
    {
        .name = "initializers after return retain only literal source conversions",
        .source = "int h(void){return 0;short a=65536;int b=1.5;(void)a;(void)b;}\n",
        .warnings = 1,
    },
    // Clang reports a literal conversion at once, as its full expression completes (Sema::Diag), while constant
    // conversions, shift counts and division by zero wait for their function's end (DiagRuntimeBehavior); at file scope a
    // declarator's constant conversion follows its literal ones. The whole output is clang's, in clang's order.
    {
        .name = "a truth context's literal conversion precedes the body's shift counts",
        .source = "int f(int x){return (x<<40)+(x>>-1)+(1.5?x<<33:0);}\n",
        .warnings = 4,
    },
    {
        .name = "literal conversions report at once before the body's deferred conversions and shift counts",
        .source = "char c=1<<9;\n"
                  "void g(int x){char d=1<<9;char e=x<<40;int k=2.5;if(0.5){x<<=64;}x=!1.5?x>>99:x;(void)d;(void)e;(void)k;}\n",
        .warnings = 7,
    },
    {
        .name = "literal conversions precede selection arms' shift counts and division by zero",
        .source = "int h(int x){int a=_Generic(x,int:1,long:x<<70);int b=1.5&&(x+(x<<35));int c=__builtin_choose_expr(1,x,x>>40);short s=3.7;"
                  "return a+b+c+s+(x<<-3)/0;}\n",
        .warnings = 7,
    },
    {
        .name = "argument and conditional-arm literal conversions precede deferred constant conversions",
        .source = "void take(char);int m(int x){take(1<<9);take(2.5);unsigned char u=(x?300:1.5);(void)u;return x?x<<32:(0?x<<50:1);}\n",
        .warnings = 5,
    },
    {
        .name = "literal conversions in unreachable and short-circuited code precede deferred shift counts",
        .source = "int g1(int x){if(0){int a=2.5;x<<=40;(void)a;}int b=sizeof(x<<40)+sizeof((char)3.5);int c=0&&(x<<40)&&1.5;"
                  "int d=_Generic(1.5,double:x<<1,default:x<<99);int e=(char)(x>>70)+4;int f[2]={1.5,x<<33};return b+c+d+e+f[0];}\n"
                  "_Static_assert(1.5,\"x\");struct S{int m;}s={2.5};\n",
        .warnings = 8,
    },
    {
        .name = "file-scope literal conversions keep each declarator's order with constant conversions",
        .source = "char c=1<<9;int a=2.5;_Bool b=0.5;char d=1.5?300:2;char w=300,x=5.5;int v[3]={300,4.5,7};_Static_assert(1.5,\"\");"
                  "struct T{int i;double e;}t={6.5,1.5};\n",
        .warnings = 10,
    },
    {
        .name = "unreachable code keeps literal conversions in each full expression's conversion order",
        .source = "int c0(int);int u(int x){return x;x=x<<40;int a=2.5;char c=1.5?300:0;x/=0;return a+c+c0(3.5)+(x&&4.5);}\n"
                  "int w(int x){if(0){x=x<<40;x=5.5;}while(0){x=x/0+6.5;}return x;}\n",
        .warnings = 5,
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
        // clang's extra `;` of attribute lists alone (#1447): only the C23 lists warn, whatever the GNU lists' order.
        .name = "attribute-only declaration warns only for its C23 lists",
        .source = "[[gnu::unused]] __attribute__((unused));\n"
                  "[[gnu::unused]] __attribute__((unused)) [[gnu::unused]] __attribute__((unused));\n"
                  "__attribute__((unused)) [[gnu::unused]] __attribute__((aligned(8)));\n"
                  "[[gnu::unused]] __attribute__((deprecated, packed)) [[gnu::unused]];\n"
                  "[[gnu::unused]] __attribute__((unknown_gnu));\n"
                  "[[unknown_c23]] __attribute__((unknown_gnu));\n",
        .warnings = 8,
        .c23 = true,
    },
    {
        .name = "attribute-only declaration's GNU lists stay quiet without ignored-attribute warnings",
        .source = "[[gnu::unused]] __attribute__((unused));\n[[unknown_c23]] __attribute__((unknown_gnu));\n",
        .options = { "-Wno-ignored-attributes" },
        .warnings = 1,
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
        // An indirection through a pointer to void (#147, -Wvoid-ptr-dereference): its operand's type as written (a typedef
        // kept, top-level qualifiers dropped), in dead code too; not right after a unary `&` nor in sizeof's or alignof's
        // operand.
        .name = "void pointer dereferences",
        .source = "typedef void *VP;\nvoid f(void *p, VP v, const void *c, void *const k) {\n"
                  "  (void)*p; (void)*v; (void)*c; (void)*k; void *q = &*p; q = &(*p); q = & *p;\n"
                  "  (void)sizeof(*p); (void)_Alignof(*p); (void)__alignof__ *p; (void)(1 ? *p : *p);\n"
                  "  if (0) (void)*p; (void)**(void **)p; (void)q;\n}\n",
        .warnings = 10,
    },
    {
        // A dereference of a null pointer constant (#1447, -Wnull-dereference): read or assigned (a compound assignment's
        // too, a cast to void's operand too), through casts (a void pointer's, a constant expression's), with clang's note;
        // not its address, a member of it, its increment, a volatile one, a pointer variable's, a subscript, sizeof's
        // operand, nor unreachable code.
        .name = "null pointer dereferences",
        .source = "struct S { int a; };\nint g(int);\nint f(int n) {\n"
                  "  *(int *)0 = 1; *(int *)0 += 2; int x = *(int *)0; g(*(int *)0); int k = n ? *(int *)0 : 1;\n"
                  "  int c = *(const int *)(0); int l = *(int *)(1 - 1); int t = *(char *)(void *)0L;\n"
                  "  int m = (*(struct S *)0).a; int *q = &*(int *)0; (*(int *)0)++; *(volatile int *)0 = 3;\n"
                  "  int *p = 0; *p = 4; int y = ((int *)0)[0]; int s = sizeof(*(int *)0); ((struct S *)0)->a = 2;\n"
                  "  if (0) { int z = *(int *)0; (void)z; }\n"
                  "  (void)*(int *)0;\n"
                  "  return x + k + c + l + t + m + y + s + (q != 0);\n  *(int *)0 = 9;\n}\n",
        .warnings = 9,
        .notes = 9,
    },
    {
        // A nullptr_t operand is a null pointer constant too (#1447, C23): `nullptr` and a nullptr_t variable through casts,
        // not a volatile target.
        .name = "null pointer dereferences of nullptr",
        .source = "typedef typeof(nullptr) np;\nint g(np n) {\n"
                  "  int a = *(int *)n; int b = *(int *)(nullptr); int c = *(volatile int *)nullptr; int d = *(int *)(void *)nullptr;\n"
                  "  *(int *)nullptr = 1;\n"
                  "  return a + b + c + d;\n}\n",
        .c23 = true,
        .warnings = 4,
        .notes = 4,
    },
    {
        // Unreachable initializers (#1447): after a return, in a constant false branch and in a `while (0)`'s body,
        // clang's runtime behavior's warnings (array bounds, division by zero, null dereference) stay silent.
        .name = "runtime warnings skip unreachable initializers",
        .source = "int a[2];\nint f(int n) {\n"
                  "  if (0) { int d = a[5]; int e = n / 0; int z = *(int *)0; (void)d; (void)e; (void)z; }\n"
                  "  while (0) { int d = a[5]; int e = n / 0; int z = *(int *)0; (void)d; (void)e; (void)z; }\n"
                  "  return n;\n"
                  "  { int d = a[5]; int e = n / 0; int z = *(int *)0; (void)d; (void)e; (void)z; }\n}\n",
    },
    {
        // An unreachable initializer's barriers outlive its body's scratch (#1447): the next body's marks and queue
        // must not reuse them, which made a later body's initialized variables "uninitialized when used here".
        .name = "unreachable initializer barriers outlive their body",
        .source = "int f(int n) {\n  return n;\n  int dead = n;\n  return dead;\n}\n"
                  "int g(int n) {\n  int a = n + 1;\n  if (n > 1) n = a;\n  int b = n + 2;\n  if (n > 2) n = b;\n"
                  "  int c = n + 3;\n  if (n > 3) n = c;\n  int d = n + 4;\n  if (n > 4) n = d;\n  return n;\n}\n",
        .options = { "-Wall" },
    },
    {
        // A compound literal's unnamed object is no variable for -Wuninitialized (#1447): its own value, at its closing
        // `}`, is no read within its own initialization. Scalar, pointer, array and struct literals in initializers and
        // assignments, at file scope, in loops, under `&` and nested, say nothing.
        .name = "compound literals are no variables for -Wuninitialized",
        .source = "struct S { int x; int y; };\nstruct T { int n; int *p; };\n"
                  "int *gp = &(int){ 4 };\nstruct S *gs = &(struct S){ .x = 3 };\nint *gq = (int[]){ 5, 6 };\n"
                  "int scalars(void) {\n  int e = (int){ 4 };\n  long l;\n  l = (long){ 5 };\n  int *q;\n  q = (int *){ 0 };\n"
                  "  return e + (int)l + (q != 0) + *gp + gs->x + gq[1];\n}\n"
                  "int aggregates(void) {\n  int a[2] = { (int){ 1 }, (int){ 2 } };\n  int z = { (int){ 3 } };\n"
                  "  struct S s = (struct S){ .x = 1 };\n  struct S m = { .x = (int){ 1 } };\n  int *p = (int[2]){ 1, 2 };\n"
                  "  s = (struct S){ .y = 2 };\n  return a[0] + z + s.x + m.x + p[1];\n}\n"
                  "int loops(int n) {\n  int total = 0;\n"
                  "  for (int i = 0; i < n; i += 1) { int e = (int){ i }; total += e + (int){ 2 }; }\n"
                  "  while (n > 0) { int d = (int){ n }; total += d; n -= 1; }\n  return total;\n}\n"
                  "int addresses(void) {\n  int *p = &(int){ 4 };\n  int **pp = &(int *){ &(int){ 5 } };\n  return *p + **pp;\n}\n"
                  "int nested(void) {\n  int e = (int){ (int){ 4 } };\n  struct T t = (struct T){ .n = (int){ 1 }, .p = &(int){ 2 } };\n"
                  "  int a = (int){ (int){ (int){ 3 } } + 1 };\n  return e + t.n + *t.p + a;\n}\n",
        .options = { "-Wall" },
    },
    {
        // The variables a compound literal's initializer reads keep their warnings (#1447): an uninitialized one is used
        // here, directly or through a nested literal or an assignment's, and the declared variable itself inside its
        // own initializer's literal, braced or not, is used within its own initialization.
        .name = "compound literal initializers keep their variables' uninitialized reads",
        .source = "int reads(void) {\n  int u;\n  int e = (int){ u };\n  return e;\n}\n"
                  "int self(void) {\n  int x = (int){ x };\n  return x;\n}\n"
                  "int nested(void) {\n  int u;\n  int v;\n  int e = (int){ (int){ u } + v };\n  return e;\n}\n"
                  "int assigned(void) {\n  int x;\n  x = (int){ x };\n  return x;\n}\n"
                  "int braced(void) {\n  int y = { (int){ y } };\n  return y;\n}\n"
                  "int pointer(void) {\n  int *p = (int *){ p };\n  return p != 0;\n}\n",
        .options = { "-Wall" },
        .warnings = 7,
        .notes = 4,
    },
    {
        // A logical operator one operand decides though the other isn't known (#1447, clang's CFG tryEvaluateBool):
        // `x && 0` and `x || 1`, through `!` and casts, prune the arm they leave out for the runtime warnings and for
        // -Wreturn-type alike; `x && (x || 1)` and `x && 1` decide nothing.
        .name = "short-circuit conditions one operand decides",
        .source = "int a[2];\nint f(int n) {\n"
                  "  if (n && 0) { n = n / 0; }\n"
                  "  if (!(n || 1)) { n = a[5]; }\n"
                  "  if ((long)(n || 1)) { n += 1; } else { n = n % 0; }\n"
                  "  while (n && 0) { n = *(int *)0; }\n"
                  "  if (n && (n || 1)) { n = n / 0; }\n"
                  "  return n;\n}\n"
                  "int g(int n) {\n  if (n || 1) return 1;\n}\n"
                  "int h(int n) {\n  while (n || 1) { if (n) return 2; }\n}\n"
                  "int k(int n) {\n  if (n && 1) return 1;\n}\n",
        .warnings = 2,
    },
    {
        // The same conditions prune a conditional's arm and a logical operator's right operand (#1447, clang's CFG builder
        // in VisitConditionalOperator and VisitLogicalOperator): `n && 1` and `n || 0` decide nothing.
        .name = "short-circuit operands one operand decides",
        .source = "int a[2];\nint f(int n) {\n"
                  "  int r = (n && 0) ? n / 0 : (n || 1) ? 1 : *(int *)0;\n"
                  "  r += ((n && 0) && n % 0) + ((n || 1) || a[5]) + (!(n || 1) ? a[5] : 0) + ((long)(n && 0) ? *(int *)0 : 1);\n"
                  "  r += (n && 1) ? n / 0 : 0;\n"
                  "  r += (n || 0) || *(int *)0;\n"
                  "  return r;\n}\n",
        .warnings = 2,
        .notes = 1,
    },
    {
        // Clang decides by the context its parser is in (#147): an unselected _Generic association or choose_expr operand in
        // an alignof's operand is unevaluated with it, as are a _Generic's controlling expression and a statement
        // expression in sizeof's; an array declarator's bound, in a sizeof's type name too, is evaluated; and a sizeof of a
        // variable array's element evaluates its operand.
        .name = "void pointer dereferences by context",
        .source = "void *p; int i;\nunsigned long f(int n) {\n  char v[n][n]; int w[(*p, 3)];\n"
                  "  return _Alignof(_Generic(i, int: i, default: *p)) + _Alignof(__builtin_choose_expr(0, i, *p)) + _Generic(*p, void: 1, default: 2)\n"
                  "    + sizeof(({ (void)*p; 1; })) + sizeof(char (*)[(*p, n)]) + sizeof(int[(*p, n)]) + sizeof(v[(*p, 0)]) + sizeof(w);\n}\n"
                  "void g(int a[(*p, 3)]) { (void)a; }\n",
        .warnings = 12,
    },
    {
        // A file-scope declaration of attribute lists and `__extension__`s alone is clang's extra `;` (#1447), which says
        // nothing but its C23 lists' ignored-attribute warnings; GNU attributes after an `__extension__` are specifiers,
        // which declare nothing. A declaration's leading attribute lists, C23's and GNU's in any order, go on past its
        // `__extension__`s.
        .name = "file-scope extension markers after attributes",
        .source = "__extension__;\n__attribute__((unused)) __extension__;\nint a; __extension__ __extension__;\n"
                  "__extension__ __attribute__((unused));\n__attribute__((unused)) [[gnu::unused]] __extension__ int b;\n"
                  "__attribute__((unused)) [[gnu::unused]] __extension__;\n[[gnu::unused]] __extension__ __extension__;\n",
        .warnings = 3,
        .c23 = true,
    },
    {
        // `__extension__`s before a static assertion (#1447), at file scope and in a block: the assertion, checked.
        .name = "extension markers before static assertions",
        .source = "__extension__ _Static_assert(1, \"\");\n__extension__ _Static_assert(0, \"file\");\n"
                  "void f(void) { __extension__ __extension__ _Static_assert(0, \"block\"); }\n",
        .errors = 2,
    },
    {
        // An assignment to a const variable, through an array's subscripts or a record's members to it, or through a
        // pointer to a const record (#147): the variable named with its declared type and the "declared const here" note at
        // its name; a pointer's target is the generic error.
        .name = "const variable assignments",
        .source = "typedef const int CI;\nstruct s { int a; };\nvoid f(const int p, const struct s *q) {\n  const int x = 1; x = 2; x++; p = 3;\n"
                  "  const int a[2] = {0}; a[0] = 1; const int m[2][2] = {0}; m[0][1] = 1;\n"
                  "  CI y = 1; y = 2; const struct s v = {0}; v.a = 1; q->a = 2;\n}\nvoid g(const int *r) { r[0] = 1; *r = 2; }\n",
        .errors = 10,
        .notes = 8,
    },
    {
        // Arithmetic on a pointer to an incomplete type (#147): clang's message of the pointee as written, a subscript's at
        // its `[`, and the "forward declaration" note at the tag's declaring name.
        .name = "incomplete pointer arithmetic",
        .source = "struct s; union u; enum e; typedef struct s S;\nvoid f(struct s *p, struct s *q, union u *w, enum e *n, S *t) {\n"
                  "  (void)(p + 1); (void)(1 + p); (void)(p - q); p++; --p; p += 2; (void)p[1]; (void)1[p];\n"
                  "  (void)(w + 1); (void)(n - 1); (void)(t + 1);\n}\n",
        .errors = 11,
        .notes = 11,
    },
    {
        .name = "independent bodies retain source order at requested lanes",
        .source = "int*f0(void){return 1-1;}\nint*f1(void){return 2-2;}\nint*f2(void){return 3-3;}\n",
        .warnings = 3,
    },
    {
        // A GNU statement expression in an unevaluated operand (#147): its statements are the graph's roots, but clang drops
        // the unused-result and runtime-behavior warnings in sizeof's, _Alignof's, __alignof__'s and typeof's operands
        // (check_common_unevaluated). None of these statement expressions has a side effect, which clang's
        // -Wunevaluated-expression would report.
        .name = "statement expression results in sizeof operands",
        .source = "int *q;\nunsigned long g(void) { return sizeof(({ *q; 1; })); }\nunsigned long k(void) { return sizeof(({ 1 + 2; 1; })); }\n",
    },
    {
        .name = "unused results in unevaluated statement expressions",
        .source = "int *q;\nstruct S { int m; } *ps, vs;\nunsigned long f(int x) {\n"
                  "    return sizeof(({ x == 1; x != 1; x < 1; 1; })) + sizeof(({ (0, x); (void *)q; ps->m; vs.m; 1; })) +\n"
                  "           sizeof(({ x ? 1 : 2; ({ 3; 4; }); 5; })) + sizeof(x ? ({ 1; 2; }) : 3) + sizeof (({ *q; 1; })) +\n"
                  "           __alignof__(({ *q; x == 1; (void *)q; 1; })) + _Alignof(({ 1; 2; }));\n}\n",
        .warnings = 1,
    },
    {
        .name = "runtime behavior warnings in unevaluated statement expressions",
        .source = "int a[2];\nunsigned u;\nint s;\nunsigned long f(int x) {\n"
                  "    return sizeof(({ x / 0; 1; })) + sizeof(({ x % 0; 1; })) + sizeof(({ a[5]; 1; })) + sizeof(({ char c = 300; c; })) +\n"
                  "           sizeof(({ u < s; 1; })) + sizeof(({ int b[2]; b[2]; })) + __alignof__(({ a[2]; x / 0; 1; }));\n}\n",
        .options = { "-Wsign-compare" },
    },
    {
        .name = "typeof statement expressions are unevaluated",
        .source = "int *q;\nvoid f(int x) {\n    __typeof__(({ *q; x == 1; x = x; x / 0; 1; })) t = 0;\n"
                  "    typeof(({ (0, x); (void *)q; 1; })) v = 0;\n    (void)t;\n    (void)v;\n}\n",
        .options = { "-Wself-assign" },
    },
    {
        // The evaluated controls: a statement expression outside the operand, sizeof's own discarded result, and array
        // bounds, which clang evaluates as constant expressions even in a sizeof's type name or in a declaration inside its
        // statement expression, until a nested sizeof makes them unevaluated again.
        .name = "evaluated statement expressions and array bounds keep their warnings",
        .source = "int *q;\nunsigned long f(int x) {\n    ({ *q; 1; });\n    sizeof(({ 1; 2; }));\n"
                  "    unsigned long n = sizeof(int[({ x == 1; 2; })]) + sizeof(({ int w[({ 3; 4; })]; 5; }));\n"
                  "    return n + ({ x < 1; 6; }) + sizeof(int[sizeof(({ 7; 8; }))]);\n}\n",
        .warnings = 6,
        .notes = 1,
    },
    {
        // A sizeof of an expression of variable array type evaluates it after all (clang's TransformToPotentiallyEvaluated),
        // which the contexts mark TRANSFORMED; a statement expression whose value decays to a pointer is no variable array,
        // and stays unevaluated. (A statement expression inside a variable array's operand isn't supported yet.)
        .name = "a statement expression decaying from a variable array stays unevaluated",
        .source = "void g(int n) {\n    char vv[n][n];\n    (void)sizeof(({ 3; vv[0]; }));\n    (void)sizeof(({ 4; vv; }));\n}\n",
    },
    {
        // -Wsign-compare's operands keep their declared typedef's spelling (#1100): a parameter's typedef, a cast's,
        // and sizeof's `__size_t`, each with clang's (aka) of what the sugar hides; the same sugar survives the
        // operands' shared-type arithmetic.
        .name = "sign-compare operands keep their typedef spelling",
        .source = "typedef int wchar_t;\ntypedef unsigned long size_t;\ntypedef int myint;\ntypedef signed char tiny;\n"
                  "int s1(wchar_t wc) { return wc < 0xffU; }\n"
                  "int s2(int n) { int a[4]; return n >= sizeof a / sizeof a[0]; }\n"
                  "int s3(int n, size_t m) { return n < m; }\n"
                  "int s4(myint n, unsigned u) { return n < u; }\n"
                  "int s5(int n, size_t m) { return n < m + 1; }\n"
                  "int s6(int n, size_t m) { return n < 1 + m; }\n"
                  "int s7(int n, size_t m) { return n < (m >> 1); }\n"
                  "int s8(int n, size_t m, unsigned long u) { return n < m + u; }\n"
                  "int s9(int n) { return n < (size_t)1; }\n"
                  "int s10(tiny x, tiny y, unsigned u) { return x + y < u; }\n",
        .options = { "-Wsign-compare" },
        .warnings = 10,
    },
    {
        // A format argument keeps its declared typedef's spelling (#1100), with the (aka) of what it hides.
        .name = "format arguments keep their typedef spelling",
        .source = "typedef unsigned long size_t;\ntypedef long ptrdiff_t;\ntypedef int wchar_t;\ntypedef int myint;\n"
                  "int printf(const char*,...);\n"
                  "void f(size_t m, ptrdiff_t d, wchar_t *w, myint n) { printf(\"%u\", m); printf(\"%u\", d); printf(\"%d\", w); printf(\"%lu\", -n); printf(\"%lu\", +n); printf(\"%lu\", ~n); }\n",
        .options = { "-Wformat" },
        .warnings = 6,
    },
    {
        // A fixed-parameter conversion must not transfer the source cast's typedef to the converted pointer.
        .name = "format fixed conversion does not borrow source typedef",
        .source = "typedef _BitInt(32) T;\nvoid f(const char*, _BitInt(32)*) __attribute__((format(printf,1,2)));\n"
                  "void g(void) { f(\"%d\", (T)0); }\n",
        .options = { "-Wformat", "-Wno-gcc-compat", "-Wno-non-literal-null-conversion" },
        .warnings = 1,
    },
    {
        // A for clause's value in parentheses is the clause's (#1447): clang's IgnoreParens finds the discarded comma,
        // its operand of no effect and its literal 0 idiom through them, in the init and the increment; the condition's
        // value is read.
        .name = "parenthesized comma for clauses discard their values",
        .source = "int g(int);\nvoid f(int n, int *p) {\n    int i = 0, j = 0;\n    for ((i, j); i < n; ++i) {}\n"
                  "    for (; ; (i++, j)) { break; }\n    for (((i, j)); ; ) { break; }\n    for ((j, i++); ; ((i, 2))) { break; }\n"
                  "    for (; (i, j); ) { break; }\n    for ((i == 1, j == 2); ; (i < 2)) { break; }\n"
                  "    for ((i == 1); ; ((i < 2))) { break; }\n    for ((g(1), j); ; (j, (void)0)) { break; }\n"
                  "    for ((i, 0); ; (j, 0)) { break; }\n    for ((void)(i, j); ; ) { break; }\n    for ((p[0], (j)); ; ) { break; }\n}\n",
        .warnings = 14,
        .notes = 1,
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
            for (uint32_t mode = 0; mode < (fixture->syntax_only ? 1u : 2u); mode += 1)
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
                const bool oracle_matches = oracle_ready && (policy != 0 || (oracle == (fixture->errors != 0) && counts.warnings == fixture->warnings &&
                                             counts.errors == fixture->errors && counts.notes == fixture->notes));
                if (policy == 0 && !oracle_matches)
                {
                    fprintf(stderr, "warning fixture oracle mismatch: %s mode=%u\nexpected status=%d warnings=%u errors=%u notes=%u; "
                                    "actual status=%d warnings=%u errors=%u notes=%u\n%s\n",
                            fixture->name, mode, fixture->errors != 0, fixture->warnings, fixture->errors, fixture->notes,
                            oracle, counts.warnings, counts.errors, counts.notes, expected != NULL ? expected : "(no diagnostic output)");
                }
                check_test_expect(test, oracle_matches, description);
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
