// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1390 logical-expression discard regressions. Include after c_check_warning_driver_test.c.
// The shared harness compares complete Clang diagnostics, status and output publication for both kernels,
// lane requests, syntax/object modes, and default, -Werror and -w policy.

static void check_test_driver_logical_unused(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const CheckDriverDefaultWarningFixture fixtures[] =
    {
        {
            .name = "logical discard reported call idioms",
            .source = "int work(int);void f(int x){x&&work(x);work(x)!=-38||work(x+1);}\n",
        },
        {
            .name = "logical discard reported assignment idioms",
            .source = "char* next(void);void f(int x,int*p){x&&(*p=1);char*v;(v=next())&&*v||(v=\"C\");}\n",
        },
        {
            .name = "logical discard value-only controls",
            .source = "void f(int a,int b){a&&b;a||b;}\n",
            .warnings = 2,
        },
        {
            .name = "logical discard left operands accept their own effects",
            .source = "int work(int);void f(int a,int b){work(a)&&b;work(a)||b;(a=1)&&b;(a=1)||b;a++&&b;--a||b;}\n",
        },
        {
            .name = "logical discard right writes and increments",
            .source = "void f(int a,int b,int*p){a&&(b+=1);a||(b-=1);a&&b++;a||--b;a&&(*p=1);}\n",
        },
        {
            .name = "logical discard nested value paths",
            .source = "int work(int);void f(int a,int b){a&&(b||work(a));a||(b&&work(a));(work(a)&&b)||a;(work(a)||b)&&a;}\n",
        },
        {
            .name = "logical discard off-path calls still warn",
            .source = "int work(int);void f(int a,int b,int*p){a&&(work(b)+1);(work(b)+1)||a;a&&(work(b)==b);a&&p[work(b)];}\n",
            .warnings = 4,
        },
        {
            .name = "logical discard comma result and literal-zero paths",
            .source = "int work(int);void f(int a){a&&(work(a),work(a));a&&(work(a),0);(a&&work(a),work(a));(work(a),a&&work(a));}\n",
        },
        {
            .name = "logical discard comma nonresult effects are insufficient",
            .source = "int work(int);void f(int a,int b){a&&(work(a),b);a&&(work(a),+0);}\n",
            .warnings = 2,
        },
        {
            .name = "logical discard conditional value arms",
            .source = "int work(int);void f(int a,int b,int c){a&&(b?work(a):c);a&&(b?c:work(a));a&&(b?:work(a));b?(a&&work(a)):a;}\n",
        },
        {
            .name = "logical discard conditional condition is insufficient",
            .source = "int work(int);void f(int a,int b,int c){a&&(work(a)?b:c);a&&(work(a)?:b);}\n",
            .warnings = 2,
        },
        {
            .name = "logical discard explicit nonvoid casts and unary values still warn",
            .source = "int work(int);void f(int a){a&&(int)work(a);a&&+work(a);(void)(a&&work(a));}\n",
            .warnings = 2,
        },
        {
            .name = "logical discard statement expression follows final result",
            .source = "int work(int);void f(int a,int b){a&&({work(a);});a&&({work(a);b;});}\n",
            .warnings = 1,
        },
        {
            .name = "logical discard selected value paths",
            .source = "int work(int);void f(int a){a&&_Generic(a,int:work(a),default:0);a&&__builtin_choose_expr(1,work(a),0);}\n",
        },
        {
            .name = "logical discard direct volatile reads",
            .source = "struct S{volatile int value;};void f(int a,volatile int*p,struct S*s){a&&*p;*p||a;a&&s->value;}\n",
        },
        {
            .name = "logical discard values computed from volatile reads still warn",
            .source = "void f(int a,volatile int*p){a&&(*p+1);a&&(int)*p;}\n",
            .warnings = 2,
        },
        {
            .name = "logical discard short-circuited effects remain syntactically quiet",
            .source = "int work(int);void f(int a){0&&work(a);1||work(a);}\n",
        },
        {
            .name = "logical discard attributed calls keep their result obligation",
            .source = "int pure_fn(int)__attribute__((pure));int const_fn(int)__attribute__((const));"
                      "int required(int)__attribute__((warn_unused_result));\n"
                      "void f(int a){a&&pure_fn(a);const_fn(a)||a;a&&required(a);a&&pure_fn(a++);}\n",
            .warnings = 4,
        },
        {
            .name = "logical discard inherited contracts survive a bare redeclaration",
            .source = "int pure_fn(int)__attribute__((pure));int pure_fn(int);"
                      "int const_fn(int)__attribute__((const));int const_fn(int);"
                      "int required(int)__attribute__((warn_unused_result));int required(int);\n"
                      "void f(int a){a&&pure_fn(a);a&&const_fn(a);a&&required(a);}\n",
            .warnings = 3,
        },
        {
            .name = "logical discard contracts follow source-visible declaration snapshots",
            .source = "int work(int);void before(int a){a&&work(a);}\n"
                      "int work(int)__attribute__((pure));int work(int);void after(int a){a&&work(a);}\n",
            .warnings = 1,
        },
        {
            .name = "logical discard a block contract does not escape its scope",
            .source = "int work(int);void f(int a){{extern int work(int)__attribute__((pure));a&&work(a);}a&&work(a);}\n",
            .warnings = 1,
        },
        {
            .name = "logical discard each contract kind inherits independently",
            .source = "int trace(const char*,...)__attribute__((pure));"
                      "int trace(const char*,...)__attribute__((format(printf,1,2)));"
                      "int required(int)__attribute__((warn_unused_result));int required(int)__attribute__((const));int required(int);\n"
                      "void f(int a){a&&trace(\"%d\",a);a&&required(a);}\n",
            .warnings = 2,
        },
        {
            .name = "logical discard direct function designator wrappers",
            .source = "int work(int);void f(int a){a&&(work)(a);a&&(&work)(a);a&&(*work)(a);}\n",
        },
        {
            .name = "logical discard typedef result contracts retain baseline warnings",
            .source = "typedef int Required __attribute__((warn_unused_result));Required work(int);void f(int a){a&&work(a);}\n",
            .warnings = 1,
        },
        {
            .name = "logical discard nested typedef result contracts retain baseline warnings",
            .source = "typedef int Required __attribute__((warn_unused_result));typedef Required Alias;Alias work(int);void f(int a){a&&work(a);}\n",
            .warnings = 1,
        },
        {
            .name = "logical discard typeof result contracts retain baseline warnings",
            .source = "typedef int Required __attribute__((warn_unused_result));typeof((Required)0) work(int);void f(int a){a&&work(a);}\n",
            .warnings = 1,
        },
        {
            .name = "logical discard a body-local typedef contract survives requested lanes",
            .source = "void f(int a){typedef int Required __attribute__((warn_unused_result));extern Required work(void);a&&work();}\n"
                      "int anchor(int a){return a+1;}\n",
            .warnings = 1,
        },
        {
            .name = "logical discard warning-specific source policy",
            .source = "int work(int);void f(int a,int b){\n"
                      "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wunused-value\"\n"
                      "a&&b;\n#pragma clang diagnostic pop\na&&work(a);a&&b;}\n",
            .warnings = 1,
        },
        {
            .name = "logical discard command-line warning suppression",
            .source = "int work(int);void f(int a,int b){a&&b;a&&work(a);}\n",
            .options = { "-Wno-unused-value" },
        },
        {
            .name = "logical discard ordinary library calls accept discard",
            .source = "typedef __SIZE_TYPE__ size_t;int printf(const char*,...);void*memcpy(void*,const void*,size_t);"
                      "void*memmove(void*,const void*,size_t);void*memset(void*,int,size_t);void*malloc(size_t);"
                      "void*calloc(size_t,size_t);void*realloc(void*,size_t);int snprintf(char*,size_t,const char*,...);"
                      "int strcmp(const char*,const char*);size_t strlen(const char*);char*strdup(const char*);\n"
                      "void f(int a,char*d,const char*s,size_t n){a&&printf(\"%s\",s);a&&memcpy(d,s,n);a&&memmove(d,s,n);"
                      "a&&memset(d,0,n);a&&malloc(n);a&&calloc(n,1);a&&realloc(d,n);a&&snprintf(d,n,\"%s\",s);"
                      "a&&strcmp(d,s);a&&strlen(s);a&&strdup(s);}\n",
        },
        {
            .name = "logical discard implicit builtin contracts retain warnings",
            .source = "int abs(int);long labs(long);long long llabs(long long);double fabs(double);double floor(double);"
                      "int isalpha(int);int toupper(int);\n"
                      "void f(int a,double x){a&&abs(a);a&&labs(a);a&&llabs(a);a&&fabs(x);a&&floor(x);a&&isalpha(a);a&&toupper(a);}\n",
            .warnings = 7,
        },
        {
            .name = "logical discard disabling builtins drops implicit contracts",
            .source = "int abs(int);long labs(long);long long llabs(long long);double fabs(double);double floor(double);"
                      "int isalpha(int);int toupper(int);\n"
                      "void f(int a,double x){a&&abs(a);a&&labs(a);a&&llabs(a);a&&fabs(x);a&&floor(x);a&&isalpha(a);a&&toupper(a);}\n",
            .options = { "-fno-builtin" },
        },
        {
            .name = "logical discard per-name builtin disabling retains other contracts",
            .source = "int abs(int);long labs(long);void f(int a){a&&abs(a);a&&labs(a);}\n",
            .options = { "-fno-builtin-abs" },
            .warnings = 1,
        },
        {
            .name = "logical discard freestanding declarations have no implicit contracts",
            .source = "int abs(int);int isalpha(int);void f(int a){a&&abs(a);a&&isalpha(a);}\n",
            .options = { "-ffreestanding" },
        },
        {
            .name = "logical discard internal library spellings remain ordinary functions",
            .source = "static int abs(int a){return a;}static int isalpha(int a){return a;}void f(int a){a&&abs(a);a&&isalpha(a);}\n",
        },
        {
            .name = "logical discard explicit library contracts remain effective",
            .source = "typedef __SIZE_TYPE__ size_t;void*malloc(size_t)__attribute__((warn_unused_result));"
                      "int printf(const char*,...)__attribute__((pure));\n"
                      "void f(int a,size_t n){a&&malloc(n);a&&printf(\"%d\",a);}\n",
            .warnings = 2,
        },
        {
            .name = "logical discard explicit contracts survive builtin disabling",
            .source = "typedef __SIZE_TYPE__ size_t;void*malloc(size_t)__attribute__((warn_unused_result));"
                      "int printf(const char*,...)__attribute__((pure));\n"
                      "void f(int a,size_t n){a&&malloc(n);a&&printf(\"%d\",a);}\n",
            .options = { "-fno-builtin" },
            .warnings = 2,
        },
        {
            .name = "logical discard ordinary calls retain their policy when freestanding",
            .source = "int work(int);void f(int a){a&&work(a);}\n",
            .options = { "-ffreestanding" },
        },
    };
    check_test_driver_warning_fixtures(test, clang, executable, directory, fixtures, BUSTER_ARRAY_LENGTH(fixtures));
}
