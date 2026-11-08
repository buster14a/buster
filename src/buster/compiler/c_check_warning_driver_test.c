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
                        check_test_expect(test, status == oracle && diagnostics_match, description);
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
