// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1394 call-warning fixtures. The common harness checks the complete independent Clang diagnostic stream,
// status and object publication, each warning policy, scalar/vector kernels and one/three requested lanes.

static void check_test_driver_call_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const CheckDriverDefaultWarningFixture fixtures[] =
    {
        {
            .name = "nonnull index lists, all pointers, inherited unions and evaluated calls",
            .source = "void nn(int*)__attribute__((nonnull(1)));void all(int*,int*)__attribute__((nonnull));"
                      "void repeated(int*,int*)__attribute__((nonnull(1)));void repeated(int*,int*)__attribute__((nonnull(2)));"
                      "void normal(int*);\n"
                      "void f(void){nn(0);nn((void*)0);all(0,0);repeated(0,0);normal(0);if(0)nn(0);(void)sizeof(nn(0));}\n"
                      "void g(void){{extern void normal(int*)__attribute__((nonnull(1)));normal(0);}normal(0);}\n",
            .warnings = 7,
        },
        {
            .name = "nonnull visible predecessor and bare redeclaration",
            .source = "void work(int*);void before(void){work(0);}\n"
                      "void work(int*)__attribute__((nonnull(1)));void work(int*);void after(int*p){work(p);work(0);}\n",
            .warnings = 1,
        },
        {
            .name = "nonnull explicit contracts remain without builtins",
            .source = "void work(int*)__attribute__((nonnull(1)));void f(void){work(0);}\n",
            .options = { "-fno-builtin" },
            .warnings = 1,
        },
        {
            .name = "nonnull source and command policy",
            .source = "void work(int*)__attribute__((nonnull(1)));void f(void){\n"
                      "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wnonnull\"\nwork(0);\n"
                      "#pragma clang diagnostic pop\nwork(0);}\n",
            .warnings = 1,
        },
        {
            .name = "nonnull command suppression",
            .source = "void work(int*)__attribute__((nonnull(1)));void f(void){work(0);}\n",
            .options = { "-Wno-nonnull" },
        },
        {
            .name = "sizeof pointer memory access, source identity and evaluated context",
            .source = "typedef __SIZE_TYPE__ size_t;void*memcpy(void*,const void*,size_t);void*memset(void*,int,size_t);\n"
                      "void f(int*p,int*q,char*s){memset(p,0,sizeof(p));memcpy(p,q,sizeof(p));memcpy(p,q,sizeof(q));"
                      "memset(p,0,sizeof(*p));memset(s,0,sizeof(s));memset((void*)p,0,sizeof(p));"
                      "if(0)memset(p,0,sizeof(p));(void)sizeof(memset(p,0,sizeof(p)));}\n",
            .warnings = 5,
            .notes = 5,
        },
        {
            .name = "sizeof pointer memory access survives no-builtin",
            .source = "typedef __SIZE_TYPE__ size_t;void*memset(void*,int,size_t);void f(int*p){memset(p,0,sizeof(p));}\n",
            .options = { "-fno-builtin" },
            .warnings = 1,
            .notes = 1,
        },
        {
            .name = "sizeof pointer memory access survives freestanding",
            .source = "typedef __SIZE_TYPE__ size_t;void*memset(void*,int,size_t);void f(int*p){memset(p,0,sizeof(p));}\n",
            .options = { "-ffreestanding" },
            .warnings = 1,
            .notes = 1,
        },
        {
            .name = "sizeof pointer memory access explicit suppression",
            .source = "typedef __SIZE_TYPE__ size_t;void*memset(void*,int,size_t);void f(int*p){memset(p,0,sizeof(p));}\n",
            .options = { "-Wno-sizeof-pointer-memaccess" },
        },
        {
            .name = "fortify object extents, string lengths, offsets and explicit builtin receipts",
            .source = "typedef __SIZE_TYPE__ size_t;void*memcpy(void*,const void*,size_t);void*memset(void*,int,size_t);"
                      "char*strcpy(char*,const char*);char*strncpy(char*,const char*,size_t);\n"
                      "void f(void){char b[4];char c[8];memcpy(b,\"123456789\",9);strcpy(c,\"123456789\");strncpy(b,\"x\",9);"
                      "__builtin_memcpy(b,\"123456789\",9);__builtin_strcpy(c,\"123456789\");memset(b+1,0,4);"
                      "memcpy(b,\"123\",4);if(0)memcpy(b,\"123456789\",9);(void)sizeof(memcpy(b,\"123456789\",9));}\n",
            .warnings = 6,
        },
        {
            .name = "fortify builtin policy leaves explicit spellings enabled",
            .source = "typedef __SIZE_TYPE__ size_t;void*memcpy(void*,const void*,size_t);char*strcpy(char*,const char*);"
                      "void f(void){char b[4];memcpy(b,\"123456789\",9);strcpy(b,\"123456789\");"
                      "__builtin_memcpy(b,\"123456789\",9);__builtin_strcpy(b,\"123456789\");}\n",
            .options = { "-fno-builtin" },
            .warnings = 2,
        },
        {
            .name = "fortify freestanding explicit spellings",
            .source = "typedef __SIZE_TYPE__ size_t;void*memcpy(void*,const void*,size_t);"
                      "void f(void){char b[4];memcpy(b,\"123456789\",9);__builtin_memcpy(b,\"123456789\",9);}\n",
            .options = { "-ffreestanding" },
            .warnings = 1,
        },
        {
            .name = "fortify command suppression",
            .source = "void f(void){char b[4];__builtin_memcpy(b,\"123456789\",9);}\n",
            .options = { "-Wno-fortify-source" },
        },
        {
            .name = "absolute value category, truncation, unsigned and original builtin arguments",
            .source = "int abs(int);long labs(long);double fabs(double);float fabsf(float);\n"
                      "int f(double d,long l,unsigned u){int x=abs(d);x+=abs(l);x+=abs(u);x+=fabs(1);"
                      "x+=fabsf(d);x+=__builtin_abs(d);return x;}\n"
                      "int g(double d){if(0)return abs(d);return sizeof(abs(d));}\n",
            .warnings = 8,
            .notes = 8,
        },
        {
            .name = "absolute value no-builtin policy preserves explicit builtins",
            .source = "int abs(int);double fabs(double);int f(double d){return abs(d)+__builtin_abs(d);}\n",
            .options = { "-fno-builtin" },
            .warnings = 1,
            .notes = 1,
        },
        {
            .name = "absolute value freestanding policy preserves explicit builtins",
            .source = "int abs(int);double fabs(double);int f(double d){return abs(d)+__builtin_abs(d);}\n",
            .options = { "-ffreestanding" },
            .warnings = 1,
            .notes = 1,
        },
        {
            .name = "absolute value command suppression",
            .source = "int abs(int);int f(double d){return abs(d);}\n",
            .options = { "-Wno-absolute-value" },
        },
        {
            .name = "GNU required result remains checked with call warnings",
            .source = "int work(void)__attribute__((warn_unused_result));void f(void){work();(void)work();}\n",
            .warnings = 1,
        },
        {
            .name = "C23 nodiscard remains checked with call warnings",
            .source = "[[nodiscard]] int work(void);void f(void){work();(void)work();}\n",
            .c23 = true,
            .warnings = 1,
        },
    };
    check_test_driver_warning_fixtures_dealt(test, clang, executable, directory, fixtures, BUSTER_ARRAY_LENGTH(fixtures), true);
}
