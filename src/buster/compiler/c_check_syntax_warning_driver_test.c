// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source-shape boundaries for #1394. The shared runner compares complete diagnostics, source positions and
// notes with Clang under normal, promoted and suppressed policy, both kernels and multiple lane requests.

static void check_test_driver_syntax_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const CheckDriverDefaultWarningFixture fixtures[] =
    {
        {
            .name = "grouped equality in each truth condition",
            .source = "int a(int x){if((x==1))return 1;return 0;}\n"
                      "int b(int x){if(((x==1)))return 1;return 0;}\n"
                      "int c(int x){while((x==1))x++;return x;}\n"
                      "int d(int x){for(;(x==1);x++){}return x;}\n"
                      "int e(int x){do{x++;}while((x==1));return x;}\n",
            .warnings = 5,
            .notes = 10,
        },
        {
            .name = "equality grouping requires an assignable left side and a truth condition",
            .source = "int a(int x){if(x==1)return 1;return 0;}\n"
                      "int b(int x){if((1==x))return 1;return 0;}\n"
                      "int c(int x){if(((int)x==1))return 1;return 0;}\n"
                      "int d(const int x){if((x==1))return 1;return 0;}\n"
                      "int e(int x){if((x!=1))return 1;return 0;}\n"
                      "int f(int x){if((x<1))return 1;return 0;}\n"
                      "int g(int x){return (x==1);}\n"
                      "int h(int x){return (x==1)?2:3;}\n",
        },
        {
            .name = "macro supplied equality grouping is intentional",
            .source = "#define GROUP(e) (e)\n"
                      "#define EQUAL(x) ((x)==1)\n"
                      "int a(int x){if(GROUP(x==1))return 1;return 0;}\n"
                      "int b(int x){if(EQUAL(x))return 1;return 0;}\n",
        },
        {
            .name = "logical not before comparison and bitwise and",
            .source = "int a(int x){return !x&1;}\n"
                      "int b(int x,int y){return !x==y;}\n"
                      "int c(int x,int y){return !x<y;}\n"
                      "int d(int x){return sizeof(!x&1);}\n",
            .warnings = 4,
            .notes = 8,
        },
        {
            .name = "logical not respects parentheses and boolean operands",
            .source = "int a(int x){return !x|1;}\n"
                      "int b(int x){return !x^1;}\n"
                      "int c(int x,int y){return x&!y;}\n"
                      "int d(int x,int y){return !x&!y;}\n"
                      "int e(int x,_Bool y){return !x&y;}\n"
                      "int f(_Bool x){return !x&1;}\n"
                      "int g(int x){return (!x)&1;}\n"
                      "int h(int x){return !(!x)&1;}\n"
                      "int i(int x,int y,int z){return !(x==y)<z;}\n"
                      "int j(int x,int y){return !x<(_Bool)y;}\n"
                      "int k(int x,_Bool y){return !x&+y;}\n"
                      "int l(int x,int y){return !x&(y=(x==1));}\n",
        },
        {
            .name = "boolean switch expressions before integer promotion",
            .source = "int a(_Bool b){switch(b){case 0:return 1;default:return 0;}}\n"
                      "int b(int x){switch(x==1){case 0:return 1;default:return 0;}}\n"
                      "int c(int x){switch(!x){case 0:return 1;default:return 0;}}\n"
                      "int d(int x,int y){switch(x&&y){case 0:return 1;default:return 0;}}\n"
                      "int e(int x,int y){switch((x==1)|(y==2)){case 0:return 1;default:return 0;}}\n"
                      "int f(int x,int y){switch(x?y==1:y==2){case 0:return 1;default:return 0;}}\n"
                      "int g(int x,int y){switch((x++,y==1)){case 0:return 1;default:return 0;}}\n"
                      "int h(int x,int y){switch(x=(y==1)){case 0:return 1;default:return 0;}}\n"
                      "int i(_Bool x){switch(+x){case 0:return 1;default:return 0;}}\n",
            .warnings = 9,
        },
        {
            .name = "ordinary switch values and explicit integer casts",
            .source = "int a(_Bool b){switch((int)b){case 0:return 1;default:return 0;}}\n"
                      "int b(int x){switch(x){case 0:return 1;default:return 0;}}\n"
                      "int c(void){switch(1){case 0:return 1;default:return 0;}}\n"
                      "int d(int x,int y){switch((x==1)|y){case 0:return 1;default:return 0;}}\n"
                      "int e(int x,int y){switch(x|=(y==1)){case 0:return 1;default:return 0;}}\n",
        },
        {
            .name = "syntax families obey parent group and individual disabling",
            .source = "int a(int x){if((x==1))return !x&1;return 0;}\n"
                      "int b(_Bool x){switch(x){case 0:return 1;default:return 0;}}\n",
            .options = { "-Wno-parentheses", "-Wno-switch-bool" },
        },
        {
            .name = "syntax warning source policy restores after pop",
            .source = "#pragma clang diagnostic push\n"
                      "#pragma clang diagnostic ignored \"-Wparentheses\"\n"
                      "#pragma clang diagnostic ignored \"-Wswitch-bool\"\n"
                      "int a(int x){if((x==1))return !x&1;return 0;}\n"
                      "int b(_Bool x){switch(x){case 0:return 1;default:return 0;}}\n"
                      "#pragma clang diagnostic pop\n"
                      "int c(int x){if((x==1))return !x&1;return 0;}\n"
                      "int d(_Bool x){switch(x){case 0:return 1;default:return 0;}}\n",
            .warnings = 3,
            .notes = 4,
        },
        {
            .name = "C23 boolean switch value",
            .source = "int f(bool x){switch(x){case false:return 1;default:return 0;}}\n",
            .warnings = 1,
            .c23 = true,
        },
    };
    check_test_driver_warning_fixtures(test, clang, executable, directory, fixtures, BUSTER_ARRAY_LENGTH(fixtures));
}
