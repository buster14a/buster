// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #803 fixtures. Clang and system libc are external oracles, not incorporated implementations.
// Exercise the actual source driver, emitted ABI, linker symbols and libc's failure behavior on both paths.
#include <signal.h>
#include <sys/resource.h>

static int check_test_fortify_execute(const char* executable)
{
    int result = -1;
    const pid_t child = fork();
    if (child == 0)
    {
        const struct rlimit limit = { 0, 0 };
        setrlimit(RLIMIT_CORE, &limit);
        const int quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (quiet >= 0)
        {
            dup2(quiet, STDERR_FILENO);
            dup2(quiet, STDOUT_FILENO);
            close(quiet);
        }
        execl(executable, executable, (char*)NULL);
        _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && child > 0 && errno == EINTR);
    if (waited == child && child > 0)
    {
        result = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
    }
    return result;
}

static void check_test_driver_fortify(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        int result;
    } cases[] =
    {
        {
            "query-selected fortified native calls",
            "#if !(__has_builtin(__builtin___memcpy_chk) && __has_builtin(__builtin___memmove_chk) && __has_builtin(__builtin___mempcpy_chk) && "
            "__has_builtin(__builtin___memset_chk) && __has_builtin(__builtin___strcpy_chk) && __has_builtin(__builtin___stpcpy_chk) && "
            "__has_builtin(__builtin___strncpy_chk) && __has_builtin(__builtin___stpncpy_chk) && __has_builtin(__builtin___strcat_chk) && "
            "__has_builtin(__builtin___strncat_chk) && __has_builtin(__builtin___sprintf_chk) && __has_builtin(__builtin___snprintf_chk) && "
            "__has_builtin(__builtin___vsprintf_chk) && __has_builtin(__builtin___vsnprintf_chk) && __has_builtin(__builtin_dynamic_object_size))\n"
            "#error implemented fortified calls must be available\n#endif\n"
            "static volatile unsigned long limit=32;\n"
            "int main(void){char b[32],format[]=\"ab%n\";int n=0,e=0;"
            "if(__builtin_dynamic_object_size(b,0)!=32)return 1;"
            "if(__builtin___memcpy_chk((e++,b),\"xy\",3,limit)!=b||e!=1||__builtin_memcmp(b,\"xy\",3))return 2;"
            "int r=__builtin___snprintf_chk(b,32,0,limit,format,&n);return r!=2||n!=2||__builtin_memcmp(b,\"ab\",3);}",
            0
        },
        {
            "checked memory/string results",
            "static volatile unsigned long limit=64;\n"
            "int main(void){char a[64],b[64];if(__builtin___memset_chk(a,0,64,limit)!=a)return 1;if(__builtin___memcpy_chk(a,\"abcd\",5,limit)!=a)return 2;"
            "if(__builtin___memmove_chk(a+1,a,5,limit)!=a+1)return 3;if(__builtin___mempcpy_chk(b,a,6,limit)!=b+6||__builtin_memcmp(b,\"aabcd\",6))return 4;"
            "if(__builtin___stpcpy_chk(a,\"xy\",limit)!=a+2)return 5;if(__builtin___strcat_chk(a,\"z\",limit)!=a)return 6;"
            "if(__builtin___strncat_chk(a,\"12\",1,limit)!=a)return 7;if(__builtin___strcpy_chk(b,a,limit)!=b||__builtin_memcmp(b,\"xyz1\",5))return 8;"
            "if(__builtin___strncpy_chk(b,\"rs\",5,limit)!=b||b[2]||b[3]||b[4])return 9;if(__builtin___stpncpy_chk(b,\"uvw\",5,limit)!=b+3||b[3]||b[4])return 10;"
            "if(__builtin___stpncpy_chk(b,\"uvwxy\",3,limit)!=b+3||__builtin_memcmp(b,\"uvw\",3))return 11;return 0;}"
            , 0
        },
        {
            "plain memory/string results",
            "static volatile unsigned long limit=64;\n"
            "int main(void){char a[64],b[64];if(__builtin___memset_chk(a,0,64,(unsigned long)-1)!=a)return 1;"
            "if(__builtin___memcpy_chk(a,\"abcd\",5,(unsigned long)-1)!=a)return 2;if(__builtin___memmove_chk(a+1,a,5,(unsigned long)-1)!=a+1)return 3;"
            "if(__builtin___mempcpy_chk(b,a,6,(unsigned long)-1)!=b+6||__builtin_memcmp(b,\"aabcd\",6))return 4;"
            "if(__builtin___stpcpy_chk(a,\"xy\",(unsigned long)-1)!=a+2)return 5;if(__builtin___strcat_chk(a,\"z\",(unsigned long)-1)!=a)return 6;"
            "if(__builtin___strncat_chk(a,\"12\",1,(unsigned long)-1)!=a)return 7;"
            "if(__builtin___strcpy_chk(b,a,(unsigned long)-1)!=b||__builtin_memcmp(b,\"xyz1\",5))return 8;"
            "if(__builtin___strncpy_chk(b,\"rs\",5,(unsigned long)-1)!=b||b[2]||b[3]||b[4])return 9;"
            "if(__builtin___stpncpy_chk(b,\"uvw\",5,(unsigned long)-1)!=b+3||b[3]||b[4])return 10;"
            "if(__builtin___stpncpy_chk(b,\"uvwxy\",3,(unsigned long)-1)!=b+3||__builtin_memcmp(b,\"uvw\",3))return 11;return 0;}"
            , 0
        },
        {
            "formatted ABI flag 0",
            "static int v(char*b,const char*f,...){__builtin_va_list a;__builtin_va_start(a,f);int r=__builtin___vsprintf_chk(b,0,(unsigned long)-1,f,a);"
            "__builtin_va_end(a);return r;}\n"
            "static int vn(char*b,const char*f,...){__builtin_va_list a;__builtin_va_start(a,f);int r=__builtin___vsnprintf_chk(b,64,0,(unsigned long)-1,f,a);"
            "__builtin_va_end(a);return r;}\n"
            "int main(void){char b[64];const char*f=\"%s:%d:%.1f:%.1Lf\";const char*expected=\"x:-7:2.5:3.5\";\n"
            "int r=__builtin___sprintf_chk(b,0,(unsigned long)-1,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);"
            "if(r!=12||__builtin_memcmp(b,expected,13))return 1;\n"
            "r=__builtin___snprintf_chk(b,64,0,(unsigned long)-1,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);"
            "if(r!=12||__builtin_memcmp(b,expected,13))return 2;\n"
            "r=v(b,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);if(r!=12||__builtin_memcmp(b,expected,13))return 3;\n"
            "r=vn(b,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);return r!=12||__builtin_memcmp(b,expected,13);}"
            , 0
        },
        {
            "formatted ABI flag 1",
            "static int v(char*b,const char*f,...){__builtin_va_list a;__builtin_va_start(a,f);int r=__builtin___vsprintf_chk(b,1,64,f,a);__builtin_va_end(a);"
            "return r;}\n"
            "static int vn(char*b,const char*f,...){__builtin_va_list a;__builtin_va_start(a,f);int r=__builtin___vsnprintf_chk(b,64,1,64,f,a);__builtin_va_end(a);"
            "return r;}\n"
            "int main(void){char b[64];const char*f=\"%s:%d:%.1f:%.1Lf\";const char*expected=\"x:-7:2.5:3.5\";\n"
            "int r=__builtin___sprintf_chk(b,1,64,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);if(r!=12||__builtin_memcmp(b,expected,13))return 1;\n"
            "r=__builtin___snprintf_chk(b,64,1,64,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);if(r!=12||__builtin_memcmp(b,expected,13))return 2;\n"
            "r=v(b,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);if(r!=12||__builtin_memcmp(b,expected,13))return 3;\n"
            "r=vn(b,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);return r!=12||__builtin_memcmp(b,expected,13);}"
            , 0
        },
        {
            "formatted ABI flag 2",
            "static int v(char*b,const char*f,...){__builtin_va_list a;__builtin_va_start(a,f);int r=__builtin___vsprintf_chk(b,2,64,f,a);__builtin_va_end(a);"
            "return r;}\n"
            "static int vn(char*b,const char*f,...){__builtin_va_list a;__builtin_va_start(a,f);int r=__builtin___vsnprintf_chk(b,64,2,64,f,a);__builtin_va_end(a);"
            "return r;}\n"
            "int main(void){char b[64];const char*f=\"%s:%d:%.1f:%.1Lf\";const char*expected=\"x:-7:2.5:3.5\";\n"
            "int r=__builtin___sprintf_chk(b,2,64,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);if(r!=12||__builtin_memcmp(b,expected,13))return 1;\n"
            "r=__builtin___snprintf_chk(b,64,2,64,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);if(r!=12||__builtin_memcmp(b,expected,13))return 2;\n"
            "r=v(b,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);if(r!=12||__builtin_memcmp(b,expected,13))return 3;\n"
            "r=vn(b,f,\"x\",(signed char)-7,(float)2.5,(long double)3.5);return r!=12||__builtin_memcmp(b,expected,13);}"
            , 0
        },
        {
            "each argument once",
            "int main(void){char b[8];int e[4]={0};__builtin___memcpy_chk((e[0]++,b),(e[1]++,\"ab\"),(e[2]++,3),(e[3]++,8));"
            "return e[0]!=1||e[1]!=1||e[2]!=1||e[3]!=1||__builtin_memcmp(b,\"ab\",3);}"
            , 0
        },
        {
            "object-size queries",
            "__attribute__((noinline)) unsigned long f(char*p){return __builtin_dynamic_object_size(p,0);} int main(void){char b[8];int e=0;"
            "__builtin___memcpy_chk(b,\"abc\",4,__builtin_object_size(b,0));"
            "return __builtin_memcmp(b,\"abc\",4)||__builtin_dynamic_object_size(b,1)!=8||__builtin_dynamic_object_size(b+3,0)!=__builtin_object_size(b+3,0)||__builtin_dynamic_object_size((e++"
            ",b),0)==0||e||f(b)!=(unsigned long)-1;}"
            , 0
        },
        {
            "70 variadic operands flag 0",
            "int main(void){char b[128];"
            "int n=__builtin___snprintf_chk(b,128,0,(unsigned long)-1,\"%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d"
            "%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d\",1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,"
            "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1);if(n!=70)return 1;for(int i=0;i<70;i++)if(b[i]!=49)return 2;return b[70]!=0;}"
            , 0
        },
        {
            "70 variadic operands flag 1",
            "int main(void){char b[128];"
            "int n=__builtin___snprintf_chk(b,128,1,(unsigned long)-1,\"%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d"
            "%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d\",1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,"
            "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1);if(n!=70)return 1;for(int i=0;i<70;i++)if(b[i]!=49)return 2;return b[70]!=0;}"
            , 0
        },
        {
            "memcpy overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___memcpy_chk(b,\"abcdef\",n,4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "memmove overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___memmove_chk(b,\"abcdef\",n,4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "mempcpy overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___mempcpy_chk(b,\"abcdef\",n,4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "memset overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___memset_chk(b,1,n,4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "strncpy overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___strncpy_chk(b,\"abcdef\",n,4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "stpncpy overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___stpncpy_chk(b,\"abcdef\",n,4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "strcpy overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___strcpy_chk(b,\"abcdef\",4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "stpcpy overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___stpcpy_chk(b,\"abcdef\",4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "strcat overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___strcat_chk(b,\"abcdef\",4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "strncat overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___strncat_chk(b,\"abcdef\",n,4);return 0;}"
            , 128 + SIGABRT
        },
        {
            "sprintf overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___sprintf_chk(b,0,4,\"%s\",\"abcdef\");return 0;}"
            , 128 + SIGABRT
        },
        {
            "snprintf overflow",
            "int main(void){char b[32]={0};volatile unsigned long n=5;__builtin___snprintf_chk(b,n,0,4,\"%s\",\"abcdef\");return 0;}"
            , 128 + SIGABRT
        },
        {
            "flag 1 writable %n unknown size",
            "int main(void){char b[32],f[]=\"ab%n\";int n;__builtin___snprintf_chk(b,32,1,(unsigned long)-1,f,&n);return 0;}"
            , 128 + SIGABRT
        },
        {
            "flag 2 writable %n unknown size",
            "int main(void){char b[32],f[]=\"ab%n\";int n;__builtin___snprintf_chk(b,32,2,(unsigned long)-1,f,&n);return 0;}"
            , 128 + SIGABRT
        },
        {
            "zero flag writable %n",
            "int main(void){char b[32],f[]=\"ab%n\";int n=0;int r=__builtin___snprintf_chk(b,32,0,(unsigned long)-1,f,&n);return n!=2||r!=2;}"
            , 0
        },
    };
    char source[512];
    char binary[512];
    char object[512];
    char names_file[512];
    snprintf(source, sizeof(source), "%s/fortify.c", directory);
    snprintf(binary, sizeof(binary), "%s/fortify", directory);
    snprintf(object, sizeof(object), "%s/fortify.o", directory);
    snprintf(names_file, sizeof(names_file), "%s/fortify.names", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const bool written = parse_test_write(source, cases[c].source, strlen(cases[c].source));
        check_test_expect(test, written, "fortify runtime fixture written");
        for (uint32_t optimization = 0; written && optimization < 2; optimization += 1)
        {
            for (uint32_t path = 0; path < 3; path += 1)
            {
                char* text = NULL;
                char* arguments[] = { (char*)(path == 0 ? clang : executable), optimization == 0 ? "-O0" : "-O2",
                                      "-Wno-fortify-source", source, "-o", binary, path == 1 ? "--scalar" : NULL, NULL };
                unlink(binary);
                const int compiled = check_test_driver_spawn(arguments, &text);
                const int ran = compiled == 0 ? check_test_fortify_execute(binary) : -1;
                char description[256];
                snprintf(description, sizeof(description), "%s: %s %s compiles and exits %d (got compile %d, run %d)",
                         cases[c].name, path == 0 ? "clang" : path == 1 ? "scalar" : "vector", optimization == 0 ? "-O0" : "-O2",
                         cases[c].result, compiled, ran);
                check_test_expect(test, compiled == 0 && ran == cases[c].result, description);
                if (compiled != 0 && text != NULL)
                {
                    fprintf(stderr, "%s", text);
                }
                free(text);
            }
        }
    }
#if defined(__GLIBC__) && !__GLIBC_PREREQ(2, 40)
    // Actual optimized system headers, not hand-written substitutes for their fortify wrappers.
    static const char headers[] =
        "#include <stdio.h>\n#include <string.h>\n#include <strings.h>\n"
        "int main(int argc,char**argv){char buf[32],dst[32];memcpy(buf,\"hello world\",12);strcpy(dst,buf);strcat(dst,\"!\");"
        "snprintf(buf,sizeof buf,\"%s %d\",dst,argc);puts(buf);return strcmp(buf,\"hello world! 1\")!=0;}\n";
    const bool headers_written = parse_test_write(source, headers, sizeof(headers) - 1);
    check_test_expect(test, headers_written, "fortified system-header reproducer written");
    for (uint32_t level = 2; headers_written && level <= 3; level += 1)
    {
        for (uint32_t path = 0; path < 3; path += 1)
        {
            char* text = NULL;
            char* arguments[] = { (char*)(path == 0 ? clang : executable), "-O2", level == 2 ? "-D_FORTIFY_SOURCE=2" : "-D_FORTIFY_SOURCE=3",
                                  source, "-o", binary, path == 1 ? "--scalar" : NULL, NULL };
            unlink(binary);
            const int compiled = check_test_driver_spawn(arguments, &text);
            check_test_expect(test, compiled == 0 && check_test_fortify_execute(binary) == 0, "FORTIFY_SOURCE=2/3 real headers compile, link and run");
            if (compiled != 0 && text != NULL)
            {
                fprintf(stderr, "%s", text);
            }
            free(text);
        }
    }
#else
    fprintf(stderr, "checker-fortify: system-header case requires pre-2.40 glibc; newer Clang wrappers need #147's pass_object_size ABI\n");
#endif
    static const char* const invalid[] =
    {
        "void f(char*d,char*s){__builtin___memcpy_chk(d,s,4);}",
        "void f(char*d,char*s){__builtin___strcpy_chk(d,s,4,5);}",
        "void f(char*d){__builtin___snprintf_chk(d,4,0,4);}",
        "void f(char*d){__builtin___vsprintf_chk(d,0,4,\"%d\",1);}",
        "void f(char*d,int m){__builtin_dynamic_object_size(d,m);}",
        "void f(char*d){__builtin_dynamic_object_size(d,4);}",
        "void f(char*d){__builtin___memcpy_chkk(d,d,0,0);}",
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(invalid); c += 1)
    {
        const bool written = parse_test_write(source, invalid[c], strlen(invalid[c]));
        check_test_expect(test, written, "fortify diagnostic fixture written");
        for (uint32_t path = 0; written && path < 3; path += 1)
        {
            char* text = NULL;
            char* arguments[] = { (char*)(path == 0 ? clang : executable), "-std=gnu17", "-Werror", "-c", source, "-o", object,
                                  path == 1 ? "--scalar" : NULL, NULL };
            unlink(object);
            const int compiled = check_test_driver_spawn(arguments, &text);
            check_test_expect(test, compiled > 0 && access(object, F_OK) != 0, "bad fortify signature/mode/spelling is rejected without an object");
            free(text);
        }
    }
    // All fourteen checked symbols and unknown-bound reductions. Formatted calls keep a zero security flag here;
    // their nonzero flag is independently checked by the %n failure fixtures above.
    static const struct
    {
        const char* name;
        const char* expression;
    } symbols[] =
    {
        { "memcpy", "(void*)__builtin___memcpy_chk(d,s,n,BOUND)" },
        { "memmove", "(void*)__builtin___memmove_chk(d,s,n,BOUND)" },
        { "mempcpy", "(void*)__builtin___mempcpy_chk(d,s,n,BOUND)" },
        { "memset", "(void*)__builtin___memset_chk(d,7,n,BOUND)" },
        { "strcpy", "(void*)__builtin___strcpy_chk(d,s,BOUND)" },
        { "stpcpy", "(void*)__builtin___stpcpy_chk(d,s,BOUND)" },
        { "strncpy", "(void*)__builtin___strncpy_chk(d,s,n,BOUND)" },
        { "stpncpy", "(void*)__builtin___stpncpy_chk(d,s,n,BOUND)" },
        { "strcat", "(void*)__builtin___strcat_chk(d,s,BOUND)" },
        { "strncat", "(void*)__builtin___strncat_chk(d,s,n,BOUND)" },
        { "sprintf", "(void*)(long)__builtin___sprintf_chk(d,0,BOUND,s,n)" },
        { "snprintf", "(void*)(long)__builtin___snprintf_chk(d,n,0,BOUND,s,n)" },
        { "vsprintf", "(void*)(long)__builtin___vsprintf_chk(d,0,BOUND,s,a)" },
        { "vsnprintf", "(void*)(long)__builtin___vsnprintf_chk(d,n,0,BOUND,s,a)" },
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(symbols); c += 1)
    {
        for (uint32_t plain = 0; plain < 2; plain += 1)
        {
            char fixture[1024];
            snprintf(fixture, sizeof(fixture), "#define BOUND %s\nvoid*f(char*d,const char*s,unsigned long n,unsigned long z,__builtin_va_list a){return %s;}\n",
                     plain ? "((unsigned long)-1)" : "z", symbols[c].expression);
            const bool written = parse_test_write(source, fixture, strlen(fixture));
            check_test_expect(test, written, "fortify symbol fixture written");
            for (uint32_t path = 0; written && path < 2; path += 1)
            {
                char* text = NULL;
                char* arguments[] = { (char*)executable, "-O0", "-c", source, "-o", object, path == 0 ? "--scalar" : NULL, NULL };
                unlink(object);
                const int compiled = check_test_driver_spawn(arguments, &text);
                free(text);
                text = NULL;
                char* names[] = { "nm", "-u", object, NULL };
                const int listed = compiled == 0 ? object_test_run(names, names_file, "/dev/null") : -1;
                FILE* listing = listed == 0 ? fopen(names_file, "r") : NULL;
                size_t bytes = 0;
                const bool read = listing != NULL && pp_test_read_output(listing, &text, &bytes);
                if (listing != NULL)
                {
                    fclose(listing);
                }
                char expected[80];
                snprintf(expected, sizeof(expected), " U %s%s%s\n", plain ? "" : "__", symbols[c].name, plain ? "" : "_chk");
                char description[128];
                snprintf(description, sizeof(description), "%s uses the %s symbol", symbols[c].name, plain ? "plain" : "checking");
                check_test_expect(test, compiled == 0 && listed == 0 && read && text != NULL && strstr(text, expected) != NULL, description);
                free(text);
            }
        }
    }
    unlink(names_file);
    unlink(object);
    unlink(binary);
    unlink(source);
}

static int run_check_fortify_tests(int argc, char** argv)
{
    CheckTest test = { 0 };
    const char* clang = "clang";
    const char* executable = "/proc/self/exe";
    bool options = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
        }
        else if (strcmp(argv[i], "--executable") == 0 && i + 1 < argc)
        {
            executable = argv[++i];
        }
        else
        {
            options = false;
        }
    }
    char directory[] = "/tmp/buster-fortify-XXXXXX";
    const bool ready = options && mkdtemp(directory) != NULL;
    check_test_expect(&test, ready, "fortify test options and temporary directory");
    if (ready)
    {
        check_test_driver_fortify(&test, clang, executable, directory);
        rmdir(directory);
    }
    printf("checker-fortify: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    return test.failures != 0;
}
