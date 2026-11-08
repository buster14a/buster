// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's DiagnoseIntInBoolContext (#1447): a `<<` and a conditional of integer constants converted
// to _Bool, through truth contexts and conversions, with -Wint-in-bool-context's and -Wtautological-constant-compare's
// groups. The shared warning harness compares the pinned Clang's complete diagnostics, warning policy and object
// preservation through both kernels and requested lanes.

static const CheckDriverDefaultWarningFixture check_driver_int_in_bool_fixtures[] =
{
    {
        .name = "a literal conversion and a shift in one logical expression keep clang's order",
        .source = "int f(int x){return 1.5&&(x<<35);}\n",
        .warnings = 2,
    },
    {
        .name = "a shift in a boolean context warns under -Wall",
        .source = "int f(int x){return 1.5&&(x<<35);}\n",
        .options = { "-Wall" },
        .warnings = 3,
    },
    {
        .name = "shifts converted to _Bool through conditions, logical operators, assignments, calls and returns",
        .source = "_Bool b;void take(_Bool);_Bool r(int x){return x<<1;}\n"
                  "int f(int x,int y){if(x<<2)return 1;while(x<<3)x--;for(;x<<4;)x--;do x--;while(x<<5);y=!(x<<6);y=(x<<7)&&y;b=x<<8;take(x<<9);"
                  "b=(y,x<<10);b=y?(x<<11):0;return y;}\n",
        .options = { "-Wall" },
        .warnings = 12,
    },
    {
        .name = "literal shifts converted to _Bool always evaluate to one truth by default",
        .source = "int a(void){return (1<<2)?1:0;}int b(int x){return !(0<<x);}int c(void){return !(2<<32);}int d(void){return !(3<<32);}"
                  "int f(void){return !(1<<-1);}int g(int x){return !(-0<<x);}\n",
        .warnings = 9,
    },
    {
        .name = "parenthesized literals, unsigned shifts, casts and conditional arms take no warning of their own",
        .source = "int f(int x,int y,unsigned u){if((1)<<2)return 1;if(1<<(2))return 2;if(1u<<31)return 3;if(u<<2)return 4;"
                  "if((char)x<<2)return 5;if((_Bool)(x<<2))return 6;if(y?(x<<2):0)return 7;if((y,x<<2))return 8;return 0;}\n",
        .options = { "-Wall" },
        .warnings = 4,
    },
    {
        .name = "conditionals of integer constants always true, except the 0 and 1 idioms, casts and parentheses",
        .source = "int f(int x,int y){if(y?2:3)return 1;if(y?2:0)return 2;if(y?0:1)return 3;if(y?-1:3)return 4;if(y?(2):3)return 5;"
                  "if(y?2u:3u)return 6;if(y?2:3u)return 7;if(y?:3)return 8;if(y?1:3)return 9;return !(y?6:7);}\n",
        .warnings = 5,
    },
    {
        .name = "file-scope and brace initializers of _Bool warn at once in clang's order",
        .source = "_Bool g1=1<<2,g2=0?2:3;_Bool arr[2]={1<<3,1?8:9};\nvoid f(int x,int y){_Bool a[2]={x<<10,y?8:9};(void)a;}\n",
        .options = { "-Wall" },
        .warnings = 6,
    },
    {
        .name = "macro operands, unreachable code, short circuits and static assertions warn too",
        .source = "#define SH(x) ((x)<<1)\n#define CO(y) ((y)?2:3)\n"
                  "int f(int x,int y){if(SH(x))return 1;if(CO(y))return 2;return 0;if(x<<3)return 3;y=0&&(x<<4);y=1||(y?6:7);"
                  "_Static_assert(!(1<<3)||1,\"\");return y;}\n",
        .options = { "-Wall" },
        .warnings = 6,
        .notes = 2,
    },
    {
        .name = "_Atomic _Bool targets take no int-in-bool warning, unlike a plain _Bool",
        .source = "_Atomic _Bool fret(int x){return x<<1;}void take(_Atomic _Bool);void g(int x,int y){_Atomic _Bool l;l=x<<1;(void)l;_Atomic _Bool m=x<<2;"
                  "_Atomic _Bool n=y?2:3;_Bool p=y?2:3;take(x<<3);(void)m;(void)n;(void)p;}\n",
        .options = { "-Wall" },
        .warnings = 1,
    },
    {
        .name = "int-in-bool-context suppression keeps the tautological forms",
        .source = "int f(int x){if(x<<2)return 1;if(1<<2)return 2;if(x?2:3)return 3;return x<<40?1:0;}\n",
        .options = { "-Wall", "-Wno-int-in-bool-context" },
        .warnings = 2,
    },
    {
        .name = "tautological-compare suppression keeps int-in-bool-context",
        .source = "int f(int x){if(x<<2)return 1;if(1<<2)return 2;if(x?2:3)return 3;return x<<40?1:0;}\n",
        .options = { "-Wall", "-Wno-tautological-compare" },
        .warnings = 2,
    },
    {
        .name = "-Wno-most drops both groups",
        .source = "int f(int x){if(x<<2)return 1;if(1<<2)return 2;if(x?2:3)return 3;return x<<40?1:0;}\n",
        .options = { "-Wall", "-Wno-most" },
    },
    {
        .name = "tautological constant compare promotion rejects object publication",
        .source = "int f(int x){return (x?2:3)?1:0;}\n",
        .options = { "-Werror=tautological-constant-compare" },
        .errors = 1,
    },
};

static void check_test_driver_int_in_bool(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_int_in_bool_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_int_in_bool_fixtures));
}
