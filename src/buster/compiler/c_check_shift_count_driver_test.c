// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original runtime shift-count sources (#1394). Independent counts and the shared warning harness compare the
// pinned Clang's complete diagnostics, warning policy and object preservation through both kernels and requested lanes.

static const CheckDriverDefaultWarningFixture check_driver_shift_count_fixtures[] =
{
    {
        .name = "nonconstant int operands retain left and right shift count warnings",
        .source = "int left(int x){return x<<40;}int right(int x){return x>>32;}\n",
        .warnings = 2,
    },
    {
        .name = "negative counts do not require a constant left operand",
        .source = "int left(int x){return x<<-1;}int right(int x){return x>>(-2L);}\n",
        .warnings = 2,
    },
    {
        .name = "ordinary integer widths use the promoted left type",
        .source = "unsigned int a(unsigned int x){return x<<31;}long b(long x){return x>>63;}"
                  "long c(long x){return x<<64;}unsigned long d(unsigned long x){return x>>64;}\n",
        .warnings = 2,
    },
    {
        .name = "narrow integer operands promote before count validation",
        .source = "int a(unsigned char x){return x<<8;}int b(unsigned short x){return x>>16;}"
                  "int c(unsigned char x){return x<<31;}int d(unsigned short x){return x>>32;}\n",
        .warnings = 1,
    },
    {
        .name = "128-bit values and counts retain their complete widths",
        .source = "unsigned __int128 a(unsigned __int128 x){return x<<127;}"
                  "unsigned __int128 b(unsigned __int128 x){return x>>128;}"
                  "int c(int x){return x<<((unsigned __int128)1<<100);}\n",
        .warnings = 2,
    },
    {
        .name = "bit-precise operands use their value width rather than storage bytes",
        .source = "_BitInt(9) a(_BitInt(9) x){return x<<8;}_BitInt(9) b(_BitInt(9) x){return x<<9;}"
                  "unsigned _BitInt(1) c(unsigned _BitInt(1) x){return x>>1;}\n",
        .warnings = 2,
        .c23 = true,
    },
    {
        .name = "narrow bit-fields use their integer promotion",
        .source = "struct S{unsigned long n:7;};int a(struct S x){return x.n<<31;}int b(struct S x){return x.n>>32;}\n",
        .warnings = 1,
    },
    {
        .name = "compound shifts use the original lvalue width",
        .source = "void f(int*x,long*y,unsigned char*z){*x<<=32;*y>>=64;*z<<=8;}\n",
        .warnings = 3,
    },
    {
        .name = "compound shifts retain negative count diagnostics",
        .source = "void f(unsigned int*x){*x>>=-1;}\n",
        .warnings = 1,
    },
    {
        .name = "atomic compound shifts use the underlying integer width",
        .source = "void f(_Atomic(int)*x){*x<<=32;}\n",
        .warnings = 1,
    },
    {
        .name = "Boolean shifts distinguish promoted ordinary width from compound storage width",
        .source = "int a(_Bool x){return x<<31;}int b(_Bool x){return x>>32;}int c(_Bool x){return x<<32;}"
                  "void d(_Bool x){x<<=0;x<<=1;x<<=7;x>>=0;x>>=1;x>>=7;}void e(_Bool x){x<<=8;x>>=8;}\n",
        .warnings = 4,
    },
    {
        .name = "Boolean dereferences atomic lvalues and bit-fields retain the compound byte boundary",
        .source = "struct S{_Bool n:1;};void a(_Bool*p){*p<<=1;*p>>=7;}void b(_Bool*p){*p<<=8;*p>>=8;}"
                  "void c(_Atomic(_Bool)*p){*p<<=1;*p>>=7;}void d(_Atomic(_Bool)*p){*p<<=8;*p>>=8;}"
                  "void e(struct S*p){p->n<<=1;p->n>>=7;}void f(struct S*p){p->n<<=8;p->n>>=8;}\n",
        .warnings = 6,
    },
    {
        .name = "Boolean count width refinement does not change negative warnings in any shift form",
        .source = "int a(_Bool x){return x<<-1;}int b(_Bool x){return x>>-1;}void c(_Bool x){x<<=-1;x>>=-1;}\n",
        .warnings = 4,
    },
    {
        .name = "non-Boolean compound storage and bit-precise value-width boundaries remain distinct",
        .source = "void a(unsigned char x){x<<=7;x>>=7;x<<=8;x>>=8;}"
                  "void b(unsigned short x){x<<=15;x>>=15;x<<=16;x>>=16;}"
                  "unsigned _BitInt(1) c(unsigned _BitInt(1) x){return (x<<0)+(x>>0)+(x<<1)+(x>>1);}"
                  "void d(unsigned _BitInt(1) x){x<<=0;x>>=0;x<<=1;x>>=1;}"
                  "unsigned _BitInt(3) e(unsigned _BitInt(3) x){return (x<<2)+(x>>2)+(x<<3)+(x>>3);}"
                  "void f(unsigned _BitInt(3) x){x<<=2;x>>=2;x<<=3;x>>=3;}\n",
        .warnings = 12,
        .c23 = true,
    },
    {
        .name = "compound bit-field counts use the declared integer type",
        .source = "struct S{unsigned long n:7;};void f(struct S*x){x->n<<=7;x->n>>=64;}\n",
        .warnings = 1,
    },
    {
        .name = "computed and explicitly converted count constants are checked independently",
        .source = "int a(int x){return x<<(16+16);}int b(int x){return x>>((int)33.0);}"
                  "int c(int x){return x>>(1?-1:0);}\n",
        .warnings = 3,
    },
    {
        .name = "signed and unsigned count conversions determine the warning family",
        .source = "int a(int x){return x<<(unsigned)-1;}int b(int x){return x>>(signed char)255;}"
                  "int c(int x){return x<<(unsigned char)256;}\n",
        .warnings = 2,
    },
    {
        .name = "bit-precise count signedness is retained",
        .source = "int a(int x){return x<<(_BitInt(8))-1;}int b(int x){return x>>(unsigned _BitInt(8))255;}\n",
        .warnings = 2,
        .c23 = true,
    },
    {
        .name = "unknown and in-range counts remain quiet",
        .source = "int a(int x,int n){return x<<n;}int b(int x){return x>>0;}\n",
    },
    {
        .name = "a known payload with side effects is not a constant count",
        .source = "int tick(void);int a(int x){return x<<(tick(),32);}int b(int x,int*p){return x>>(*p=32);}\n",
    },
    {
        .name = "effects in the left operand do not suppress a constant count warning",
        .source = "int tick(void);int a(int*p){return (*p)++<<32;}int b(void){return tick()>>-1;}\n",
        .warnings = 2,
    },
    {
        .name = "fully constant shifts retain one diagnostic each",
        .source = "int a(void){return 1<<32;}int b(void){return 1>>-1;}\n",
        .warnings = 2,
    },
    {
        .name = "constant and runtime shift diagnostics share expression order",
        .source = "int f(int x){return (1<<32)+(x>>32);}\n",
        .warnings = 2,
    },
    {
        .name = "runtime shift diagnostics can precede retained constant diagnostics",
        .source = "int f(int x){return (x<<32)+(1>>-1);}\n",
        .warnings = 2,
    },
    {
        .name = "retained shifts share statement order with runtime and unused-value diagnostics",
        .source = "void f(int x){x<<32;1>>-1;}\n",
        .warnings = 4,
    },
    {
        .name = "a failed program retains its already collected constant shift diagnostic",
        .source = "int f(void){return 1<<32;break;}\n",
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "sizeof operands do not diagnose runtime shift counts",
        .source = "unsigned long f(int x){return sizeof(x<<32);}\n",
    },
    {
        .name = "typeof and alignof operands do not diagnose runtime shift counts",
        .source = "unsigned long f(int x){return _Alignof(__typeof__(x>>-1));}\n",
    },
    {
        .name = "generic controlling expressions do not diagnose runtime shift counts",
        .source = "int f(int x){return _Generic(x<<32,int:7,default:0);}\n",
    },
    {
        .name = "unselected generic arms retain queued shift-count diagnostics",
        .source = "int f(int x){return _Generic(x,int:7,default:x>>-1);}\n",
        .warnings = 1,
    },
    {
        .name = "a selected generic arm before an unselected arm keeps diagnostic order",
        .source = "int f(int x){return _Generic(x,int:x<<32,default:x>>-1);}\n",
        .warnings = 2,
    },
    {
        .name = "a retained constant diagnostic in an unselected generic arm precedes the selected arm",
        .source = "int f(int x){return _Generic(x,default:1>>-1,int:x<<32);}\n",
        .warnings = 2,
    },
    {
        .name = "an unreachable generic selection still diagnoses its detached arm",
        .source = "int f(int x){if(0)return _Generic(x,int:x<<32,default:x>>-1);return x;}\n",
        .warnings = 1,
    },
    {
        .name = "logical and inside a detached generic arm retains its queued count warning",
        .source = "int f(int x){return _Generic(x,int:x,default:0&&(x>>-1));}\n",
        .warnings = 1,
    },
    {
        .name = "logical or inside a detached generic arm retains its queued count warning",
        .source = "int f(int x){return _Generic(x,int:x,default:1||(x<<32));}\n",
        .warnings = 1,
    },
    {
        .name = "an ordinary conditional inside a detached generic arm retains its queued count warning",
        .source = "int f(int x){return _Generic(x,int:x,default:0?(x<<32):x);}\n",
        .warnings = 1,
    },
    {
        .name = "sizeof suppresses counts inside and around detached generic arms",
        .source = "int a(int x){return _Generic(x,int:x,default:sizeof(x<<32));}"
                  "unsigned long b(int x){return sizeof(_Generic(x,int:x<<32,default:x>>-1));}\n",
    },
    {
        .name = "typeof suppresses counts inside and around detached generic arms",
        .source = "int a(int x){return _Generic(x,int:x,default:_Alignof(__typeof__(x>>-1)));}"
                  "unsigned long b(int x){return _Alignof(__typeof__(_Generic(x,int:x<<32,default:x>>-1)));}\n",
    },
    {
        .name = "a detached arm precedes a later statement's count diagnostic",
        .source = "int f(int x){if(0)x=_Generic(x,int:0,default:x<<32);return x>>-1;}\n",
        .warnings = 2,
    },
    {
        .name = "a detached arm under a conditional precedes the next operand's count diagnostic",
        .source = "int f(int x){return (0?_Generic(x,int:0,default:x<<32):0)+(x>>-1);}\n",
        .warnings = 2,
    },
    {
        .name = "unselected ordinary conditional arms do not diagnose runtime shift counts",
        .source = "int f(int x){return 0?(x<<32):x;}\n",
    },
    {
        .name = "short-circuited logical operands do not diagnose runtime shift counts",
        .source = "int a(int x){return 0&&(x>>-1);}int b(int x){return 1||(x<<40);}\n",
    },
    {
        .name = "unselected choose arms retain queued shift-count diagnostics",
        .source = "int f(int x){return __builtin_choose_expr(1,x,x<<32);}\n",
        .warnings = 1,
    },
    {
        .name = "unselected generic statement values publish each shift form exactly once",
        .source = "int a(int x){return _Generic(x,int:0,default:({x<<32;}));}"
                  "int b(int x){return _Generic(x,int:0,default:({x>>32;}));}"
                  "int c(int x){return _Generic(x,int:0,default:({x<<=32;}));}"
                  "int d(int x){return _Generic(x,int:0,default:({x>>=32;}));}\n",
        .warnings = 4,
    },
    {
        .name = "unselected choose statement values publish each shift form exactly once",
        .source = "int a(int x){return __builtin_choose_expr(0,({x<<32;}),0);}"
                  "int b(int x){return __builtin_choose_expr(0,({x>>32;}),0);}"
                  "int c(int x){return __builtin_choose_expr(0,({x<<=32;}),0);}"
                  "int d(int x){return __builtin_choose_expr(0,({x>>=32;}),0);}\n",
        .warnings = 4,
    },
    {
        .name = "detached statement initializers retain their single shift diagnostic",
        .source = "int a(int x){return _Generic(x,int:0,default:({int y=x<<32;y;}));}"
                  "int b(int x){return __builtin_choose_expr(0,({int y=x>>32;y;}),0);}\n",
        .warnings = 2,
    },
    {
        .name = "standalone and selected statement values retain one legitimate count diagnostic",
        .source = "int a(int x){return ({x<<32;});}int b(int x){return ({int y=x>>32;y;});}"
                  "int c(int x){return _Generic(x,int:({x<<32;}),default:0);}"
                  "int d(int x){return __builtin_choose_expr(1,({x>>32;}),0);}\n",
        .warnings = 4,
    },
    {
        .name = "nested detached statement selections share count ownership without dropping warnings",
        .source = "int a(int x){return _Generic(x,int:0,default:({__builtin_choose_expr(0,({x<<32;}),0);}));}"
                  "int b(int x){return __builtin_choose_expr(0,({_Generic(x,int:0,default:({x>>32;}));}),0);}\n",
        .warnings = 2,
    },
    {
        .name = "detached negative statement counts publish once in both static selections",
        .source = "int a(int x){return _Generic(x,int:0,default:({x<<-1;}));}"
                  "int b(int x){return __builtin_choose_expr(0,({x>>-1;}),0);}\n",
        .warnings = 2,
    },
    {
        .name = "unreachable owners still publish their detached statement count once",
        .source = "int a(int x){if(0)return _Generic(x,int:0,default:({x<<32;}));return 0;}"
                  "int b(int x){if(0)return __builtin_choose_expr(0,({x>>32;}),0);return 0;}\n",
        .warnings = 2,
    },
    {
        .name = "count ownership retains adjacent unused-value diagnostics and source order",
        .source = "int f(int x){x<<32;(void)_Generic(x,int:0,default:({x<<32;}));x>>32;return 0;}\n",
        .warnings = 5,
    },
    {
        .name = "unevaluated and in-range detached statement values remain quiet",
        .source = "int a(int x){return _Generic(x,int:0,default:({x<<31;}));}"
                  "int b(int x){return __builtin_choose_expr(0,({x>>31;}),0);}"
                  "unsigned long c(int x){return _Generic(x,int:0,default:sizeof(({x<<32;})));}"
                  "unsigned long d(int x){return __builtin_choose_expr(0,sizeof(({x>>32;})),0);}\n",
    },
    {
        .name = "distinct detached macro expansions retain one count group and note each",
        .source = "#define BAD(x) ({(x)<<32;})\nint a(int x){return _Generic(x,int:0,default:BAD(x));}"
                  "int b(int x){return __builtin_choose_expr(0,BAD(x),0);}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "a selected choose arm before an unselected arm keeps diagnostic order",
        .source = "int f(int x){return __builtin_choose_expr(1,x<<32,x>>-1);}\n",
        .warnings = 2,
    },
    {
        .name = "a selected choose arm after an unselected arm keeps diagnostic order",
        .source = "int f(int x){return __builtin_choose_expr(0,x>>-1,x<<32);}\n",
        .warnings = 2,
    },
    {
        .name = "an unreachable choose selection still diagnoses its detached arm",
        .source = "int f(int x){if(0)return __builtin_choose_expr(1,x<<32,x>>-1);return x;}\n",
        .warnings = 1,
    },
    {
        .name = "logical and inside a detached choose arm retains its queued count warning",
        .source = "int f(int x){return __builtin_choose_expr(1,x,0&&(x>>-1));}\n",
        .warnings = 1,
    },
    {
        .name = "logical or inside a detached choose arm retains its queued count warning",
        .source = "int f(int x){return __builtin_choose_expr(1,x,1||(x<<32));}\n",
        .warnings = 1,
    },
    {
        .name = "an ordinary conditional inside a detached choose arm retains its queued count warning",
        .source = "int f(int x){return __builtin_choose_expr(1,x,0?(x<<32):x);}\n",
        .warnings = 1,
    },
    {
        .name = "sizeof suppresses counts inside and around detached choose arms",
        .source = "int a(int x){return __builtin_choose_expr(1,x,sizeof(x<<32));}"
                  "unsigned long b(int x){return sizeof(__builtin_choose_expr(1,x<<32,x>>-1));}\n",
    },
    {
        .name = "typeof suppresses counts inside and around detached choose arms",
        .source = "int a(int x){return __builtin_choose_expr(1,x,_Alignof(__typeof__(x>>-1)));}"
                  "unsigned long b(int x){return _Alignof(__typeof__(__builtin_choose_expr(1,x<<32,x>>-1)));}\n",
    },
    {
        .name = "generic controllers suppress counts throughout nested static selections",
        .source = "int a(int x){return _Generic(_Generic(x,int:x<<32,default:x>>-1),int:0,default:0);}"
                  "int b(int x){return _Generic(__builtin_choose_expr(1,x<<32,x>>-1),int:0,default:0);}\n",
    },
    {
        .name = "generic controllers nested inside detached arms still suppress counts",
        .source = "int a(int x){return _Generic(x,int:x,default:_Generic(x<<32,int:0,default:0));}"
                  "int b(int x){return __builtin_choose_expr(1,x,_Generic(x>>-1,int:0,default:0));}\n",
    },
    {
        .name = "static assertions suppress count diagnostics in detached static-selection arms",
        .source = "void f(int x){_Static_assert(_Generic(x,int:1,default:x<<32),\"generic\");"
                  "_Static_assert(__builtin_choose_expr(1,1,x>>-1),\"choose\");}\n",
    },
    {
        .name = "case values suppress count diagnostics in detached static-selection arms",
        .source = "int f(int x){switch(x){case _Generic(x,int:0,default:x<<32):return 1;"
                  "case __builtin_choose_expr(1,1,x>>-1):return 2;}return 0;}\n",
    },
    {
        .name = "a file-scope constexpr initializer suppresses detached count diagnostics",
        .source = "extern int x;constexpr int y=_Generic(x,int:0,default:x<<32);\n",
        .c23 = true,
    },
    {
        .name = "a local constexpr initializer retains detached count diagnostics",
        .source = "int f(int x){constexpr int y=_Generic(x,int:0,default:x>>-1);return y;}\n",
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "an omitted-middle conditional diagnoses its shared condition once",
        .source = "int f(int x,int y){return (x<<32)?:y;}\n",
        .warnings = 1,
    },
    {
        .name = "an omitted-middle conditional in a detached arm diagnoses its shared condition once",
        .source = "int f(int x){return _Generic(x,int:0,default:(x<<32)?:0);}\n",
        .warnings = 1,
    },
    {
        .name = "a selected statement expression retains its dead shift's suppression",
        .source = "int f(int x){return _Generic(x,int:({if(0)(void)(x<<32);0;}),default:0);}\n",
    },
    {
        .name = "selected conditional operands still diagnose their counts",
        .source = "int a(int x){return x?x<<32:x;}int b(int x){return 1?x>>-1:x;}\n",
        .warnings = 2,
    },
    {
        .name = "dead statements do not diagnose runtime shift counts",
        .source = "int a(int x){if(0){return x<<32;}return x;}int b(int x){return x;x>>=-1;}"
                  "int c(int x){if(1)return x;return x>>32;}\n",
    },
    {
        .name = "reachable statement arms diagnose their counts",
        .source = "int a(int x){if(x)return x<<32;return x;}int b(int x){if(1)return x>>-1;return x;}\n",
        .warnings = 2,
    },
    {
        .name = "vector shifts retain their separate count semantics",
        .source = "typedef int V __attribute__((vector_size(16)));V f(V x){return x<<32;}\n",
        // Baseline x86-64 cannot lower this vector shift; the ordinary expression still checks warning exclusion.
        .syntax_only = true,
    },
    {
        .name = "macro shift counts retain expansion notes",
        .source = "#define SHIFT(x) ((x)<<32)\nint f(int x){return SHIFT(x);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "overflow count suppression preserves the negative count family",
        .source = "int a(int x){return x<<32;}int b(int x){return x>>-1;}\n",
        .options = { "-Wno-shift-count-overflow" },
        .warnings = 1,
    },
    {
        .name = "negative count suppression preserves the overflow count family",
        .source = "int a(int x){return x<<32;}int b(int x){return x>>-1;}\n",
        .options = { "-Wno-shift-count-negative" },
        .warnings = 1,
    },
    {
        .name = "specific count warning promotion rejects object publication",
        .source = "int f(int x){return x<<32;}\n",
        .options = { "-Werror=shift-count-overflow" },
        .errors = 1,
    },
    {
        .name = "source diagnostic push and pop preserve shift count policy",
        .source = "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wshift-count-overflow\"\n"
                  "int a(int x){return x<<32;}\n#pragma clang diagnostic pop\nint b(int x){return x<<32;}\n",
        .warnings = 1,
    },
};

static void check_test_driver_shift_counts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_shift_count_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_shift_count_fixtures));
}
