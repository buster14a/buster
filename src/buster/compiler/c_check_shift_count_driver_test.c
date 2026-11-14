// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original runtime shift-count sources (#1394). Independent counts and the shared warning harness compare the
// pinned Clang's complete diagnostics, warning policy and object preservation through both kernels and requested lanes.

// Under -fwrapv, or -fno-strict-overflow without -fno-wrapv, signed overflow is defined, and clang's DiagnoseBadShiftValues
// warns of neither a shift's result nor its negative left operand; its counts' warnings stay (#1447).
static const char check_driver_shift_wrapv_source[] =
    "int a = 1 << 40;\n"
                  "int b = -1 << 2;\n"
                  "int c = 3 << 31;\n"
                  "long d = -8L << 60;\n"
                  "int f(int x) { return (3 << 31) | (-4 << 1) | (x << 40) | (1 << -1); }\n"
                  "void g(void) { int v = (-1 << 1); (void)v; }\n";

static const CheckDriverDefaultWarningFixture check_driver_shift_count_fixtures[] =
{
    {
        .name = "shift values without -fwrapv",
        .source = check_driver_shift_wrapv_source,
        .warnings = 9,
    },
    {
        .name = "-fwrapv defines shift values",
        .source = check_driver_shift_wrapv_source,
        .options = { "-fwrapv" },
        .warnings = 3,
    },
    {
        .name = "-fno-strict-overflow defines shift values",
        .source = check_driver_shift_wrapv_source,
        .options = { "-fno-strict-overflow" },
        .warnings = 3,
    },
    {
        .name = "-fno-wrapv after -fno-strict-overflow keeps shift values' warnings",
        .source = check_driver_shift_wrapv_source,
        .options = { "-fno-strict-overflow", "-fno-wrapv" },
        .warnings = 9,
    },
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
        .name = "a comma's discarded left operand warns where clang builds it, before a later shift count",
        .source = "int f(int x){return (x,1.5)?x<<32:0;}\n",
        .warnings = 2,
    },
    {
        .name = "commas, counts and divisions keep their build order within one expression",
        .source = "int a(int x){return (x<<40,2)+(x,3);}int b(int x){return ((x,1),x<<40);}int c(int x){(x,2),x<<40;return 0;}"
                  "int d(int x){return (x,1/0);}int e(int x){return (x,1)+1/0;}\n",
        .warnings = 14,
    },
    {
        .name = "a comma's marker precedes the constant conversions its full expression completes",
        .source = "char c;int f(int x){return (x,(c=300)+1);}int g(int x){return (x,1)+1/0+(c=301);}\n",
        .warnings = 5,
    },
    {
        .name = "statement conditions and for increments warn before their bodies",
        .source = "int a(int x,int y){if((x,y))return x<<32;return 0;}int b(int x){while(x<<40){x=1/0;}return 0;}"
                  "int c(int x){switch(x<<40){case 1:x=1/0;}return 0;}int d(int x){for(;;x<<40){x=1/0;}}"
                  "int e(int x){if(x<<41)x=1/0;else if(x<<42)x=2/0;return 0;}int f(int x){if(x){if(x<<40)x=1/0;}else x=2/0;return 0;}\n",
        .warnings = 16,
    },
    {
        .name = "a declarator's initializer warns before the later declarators'",
        .source = "int f(int x){int a=(x,1),b=x<<40;return a+b;}\n",
        .warnings = 2,
    },
    {
        .name = "comma and count ordering under -Wall",
        .source = "int f(int x){return (x,1.5)?x<<32:0;}int g(int x,int y){if((x,y))return x<<32;return 0;}\n",
        .options = { "-Wall" },
        .warnings = 4,
    },
    // A statement expression's statements warn where clang builds them (#1447): between what its enclosing expression built
    // before it and after it, its last value once.
    {
        .name = "a statement expression's statements warn between its enclosing expression's operands",
        .source = "int f(int x){int a=x/0+({int t=1/0;x%0;})+(x<<33);int b=({x<<34;})+x/0;int c=({int y=({2/0;3;});y/0;})+(x<<35);"
                  "int d=({char ch=300;ch;})+x/0;int e=({x,1;})+1/0;x/0,({x<<36;});return a+b+c+d+e;}\n",
        .warnings = 18,
    },
    {
        .name = "a statement expression's last value warns once",
        .source = "int g(int x){return ({x/0;})+(1<<32);}int h(int x){return ({x/0;});}void k(int x){({x/0;});({1;x%0;});}\n",
        .warnings = 8,
    },
    {
        .name = "a statement expression's conditions warn before their bodies and its later statements",
        .source = "int f(int x){int c=({if(1/0)x<<34;for(;x/0;){}5;})+6/0;if(({x/0;})+8/0)return 1;return c+({x/0;});}\n",
        .warnings = 8,
    },
    {
        .name = "statement expressions' order under -Wall",
        .source = "unsigned u;int g(int);int f(int x){int j=({u<x;1;})+({g(1);x/0;});return j+x/0+({int t=1/0;x%0;});}\n",
        .options = { "-Wall" },
        .warnings = 6,
    },
    {
        .name = "unused-value suppression keeps the reordered counts",
        .source = "int f(int x){return (x,1.5)?x<<32:0;}int g(int x){if(1/0)return x<<32;return 0;}\n",
        .options = { "-Wno-unused-value" },
        .warnings = 3,
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
    // A folded shift's own warnings (#1447): clang's DiagnoseBadShiftValues words -Wshift-negative-value and
    // -Wshift-overflow, and only the former and the counts are runtime-behavior ones, deferred to the function's end
    // and dropped where no path reaches.
    {
        .name = "folded shifts word their overflow and negative left operands as clang does",
        .source = "typedef int I;\nint a=0x7fffffff<<2;int b=-1<<3;int c=1<<31;int d=3<<30;long e=0x7fffffffffffffffL<<60;\n"
                  "__int128 g=(__int128)0x7fffffffffffffffLL<<100;int h=(I)5<<30;int i=(short)1<<31;\n",
        .warnings = 5,
    },
    {
        .name = "a folded negative shift waits for the function's end while its overflow does not",
        .source = "int f(int x){int z=-3<<1;char c=300;x=0x7fffffff<<2;c=301;return z+c+x;}\n",
        .warnings = 4,
    },
    {
        .name = "a body's shift overflow precedes a later shift precedence warning",
        .source = "int f(int x)\n{\n    int v = 3 << 31;\n    int y = 1 << 3 + 1;\n    return v + y;\n}\n",
        .warnings = 2,
        .notes = 1,
    },
    {
        .name = "unreachable and unevaluated shift overflow keeps its source order",
        .source = "int f(void){int a=sizeof(3<<31);int b=1<<2+1;return a+b;int c=3<<31;int d=1<<3+1;}\n",
        .warnings = 4,
        .notes = 2,
    },
    {
        .name = "shift overflow warns before a deferred negative shift and a later precedence warning",
        .source = "int f(void){int a=-1<<2;int b=3<<31;int c=1<<3+1;return a+b+c;}\n",
        .warnings = 3,
        .notes = 1,
    },
    {
        .name = "source suppression of shift overflow preserves later precedence diagnostics",
        .source = "int f(void){\n#pragma clang diagnostic ignored \"-Wshift-overflow\"\n"
                  "int a=3<<31;int b=1<<3+1;return a+b;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "unreachable folded shifts drop their runtime warnings and keep their overflow",
        .source = "int a(int x){return x;x=1<<32;x=-1<<1;x=3<<31;x=-1<<2;return x;}\n"
                  "int b(int x){if(x)return 1;else return 2;x=1<<33;}\n"
                  "int c(int x){if(0){x=1<<40;x=-1<<2;x=0x7fffffff<<3;}while(0){x=1<<42;}for(;;){}x=-1<<4;}\n"
                  "void d(int x){goto e;x=-1<<9;e:x=-1<<10;while(1){}x=-1<<11;}\n",
        .warnings = 3,
    },
    {
        .name = "constant contexts leave a folded negative shift to the constant evaluation",
        .source = "enum{E=-1<<2};_Static_assert((-1<<1)==-2,\"\");int f(int x){switch(x){case -1<<3:return 1;}"
                  "int y=sizeof(-1<<4);y+=x?(-1<<5):0;y+=1?0:(-1<<6);return y;}\n",
        .warnings = 1,
    },
    {
        .name = "an unreachable promoted folded shift neither reports nor rejects the unit",
        .source = "int f(int x){if(x)return 1;else return 0;x=-1<<8;x=1<<40;}\n",
        .options = { "-Werror=shift-negative-value", "-Werror=shift-count-overflow" },
    },
    {
        .name = "a reachable promoted folded shift rejects the unit",
        .source = "int f(int x){x=-1<<8;return x;}\n",
        .options = { "-Werror=shift-negative-value" },
        .errors = 1,
    },
    {
        .name = "source policy reaches folded shifts' runtime and immediate warnings",
        .source = "int f(int x){\n#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wshift-negative-value\"\n"
                  "x=-1<<2;\n#pragma clang diagnostic pop\n#pragma clang diagnostic error \"-Wshift-count-overflow\"\n"
                  "x=1<<40;x=-2<<1;x=0x7fffffff<<4;if(x)return 0;return x;x=1<<41;}\n",
        .warnings = 2,
        .errors = 1,
    },
    {
        // Review on #1778: clang has no CFG block for an unselected static-selection arm, and its Sema evaluates a reachable
        // statement's folded condition, whose range our program drops.
        .name = "unselected static-selection arms keep their folded negative shifts",
        .source = "int f(long x){int y=_Generic(x,int:0,long:-1<<2);y+=__builtin_choose_expr(1,0,-1<<3);"
                  "if(x)y=_Generic(x,int:x<<40,long:1);return y;}\nvoid g(long x){(void)_Generic(x,int:0,long:-1<<4);"
                  "(void)__builtin_choose_expr(0,-1<<5,0);}\n",
        .warnings = 4,
    },
    {
        .name = "a reachable statement's folded condition keeps its shifts' warnings and an unreached one drops them",
        .source = "#define SW (1<<44)\nint a(int x){if((1<<40)!=0)x=1;if(1&&((1<<41)!=0))x=2;if(x)x=5;else if((-1<<2)<0)x=6;"
                  "else if((1<<46)!=0)x=7;if(x){if((1<<47)!=0)x=8;}switch(SW){default:x=10;}if((0<<48)!=0)x=11;return x;}\n"
                  "int b(int x){return x;if((1<<40)!=0)x=1;if((-1<<2)<0)x=6;switch(SW){default:x=10;}if((0<<48)!=0)x=11;else x=12;return x;}\n"
                  "int c(int x){if(x)return 1;else return 2;if((1<<49)!=0)x=1;}\nint d(int x){if((1<<50)!=0)return 1;return x;}\n",
        .warnings = 7,
        .notes = 1,
    },
    {
        // Re-review on #1778: a static selection's unselected roots never stand for a folded condition's statement.
        .name = "a folded condition before a static selection keeps its shifts' warnings in its place",
        .source = "int f1(int x){if((1<<40)!=0)return _Generic(x,int:0,long:0);return 0;}\n"
                  "int f2(int x){if((1<<41)!=0)return __builtin_choose_expr(1,0,0);return 0;}\n"
                  "int f3(int x){if((1<<42)!=0){}_Generic(x,int:0,long:0);return 0;}\n"
                  "int f4(int x){switch(1<<43){default:return _Generic(x,int:0,long:0);}return 0;}\n"
                  "int f5(int x){if((-1<<2)<0)return _Generic(x,int:0,long:0);return 0;}\n"
                  "int f6(int x){return x;if((1<<44)!=0)return _Generic(x,int:0,long:0);return 0;}\n",
        .warnings = 6,
    },
    {
        // Re-review on #1778: an unselected static-selection arm's shifts warn in the selection's order, not its root's.
        .name = "unselected static-selection arms' folded shifts warn in their order",
        .source = "int f(int x){return _Generic(x,int:1<<40,default:-1<<2);}\n"
                  "int g(int x){return _Generic(x,long:1<<41,short:-2<<2,default:x)+__builtin_choose_expr(0,-3<<1,1<<42);}\n",
        .warnings = 6,
    },
    {
        // Re-review on #1778: a folded condition's statement whose arm has no expression is reached where its place is.
        .name = "a folded condition with an empty arm keeps its shifts' warnings only where its place is reached",
        .source = "int d1(int x){return 0;if((1<<40)!=0){}}\n"
                  "int n1(int x){return _Generic(x,int:0,long:0);}\n"
                  "int d2(int x){goto out;if((1<<41)!=0){}out:return x;}\n"
                  "int d3(int x){while(x){return 1;if((1<<42)!=0){}}return 0;}\n"
                  "int d4(int x){switch(x){case 1:return 1;if((1<<43)!=0){}case 2:if((1<<44)!=0){}}return 0;}\n"
                  "int d5(int x){while(1){if(x)return 1;}if((1<<45)!=0){}}\n"
                  "int r1(int x){if(x)return 1;if((1<<46)!=0){}return 0;}\n"
                  "int r2(int x){goto l;return 1;l:if((1<<47)!=0){}return 0;}\n"
                  "int r3(int x){while(x){if(x>2)break;if((1<<48)!=0){}}return 0;}\n"
                  "int r4(int x){for(;;){if(x)break;}if((1<<49)!=0){}return 0;}\n"
                  "int r5(int x){if(x){x=2;}else{return 1;}if((1<<50)!=0){}return x;}\n"
                  "int r6(int x){if((1<<51)!=0){}}\n",
        .warnings = 8,
    },
    {
        .name = "a unit with an error keeps its folded shifts' runtime warnings",
        .source = "int f(int x){x=-1<<2;x=1<<40;x=undeclared;return x;x=-1<<3;}\n",
        .warnings = 3,
        .errors = 1,
    },
};

static void check_test_driver_shift_counts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_shift_count_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_shift_count_fixtures));
}
