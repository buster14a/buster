// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1106 regression: deep expressions and nested calls through the public driver, linked and executed.
// Clang is an external oracle; none of its implementation or fixtures is incorporated.

#include <sys/resource.h>

static bool check_test_deep_source(const char* path)
{
    static const uint32_t depths[] = { 239, 240, 255, 256, 257, 300, 1000 };
    static const char* const types[] = { "long", "double", "long double", "__int128", "V" };
    FILE* file = fopen(path, "wb");
    bool written = file != NULL;
    if (written)
    {
        fputs("typedef unsigned V __attribute__((vector_size(16)));\n"
              "long add(long x,long y){return x+y;}\n"
              "double addd(double x,double y){return x+y;}\n"
              "volatile long effects; long next(void){effects+=1;return effects;}\n", file);
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(types); c += 1)
        {
            for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(depths); d += 1)
            {
                fprintf(file, "%s expr_%u_%u(%s x){return ", types[c], c, d, types[c]);
                for (uint32_t n = 0; n < depths[d]; n += 1)
                {
                    fputs("x+(", file);
                }
                fputs("x", file);
                for (uint32_t n = 0; n < depths[d]; n += 1)
                {
                    fputc(')', file);
                }
                fputs(";}\n", file);
            }
        }
        // A wide call after 300 live values: its negative effect wraps into a positive signed byte, and the
        // first selector's modulo depth would otherwise underflow the ordinary call-save area.
        fputs("long wide(", file);
        for (uint32_t a = 0; a < 239; a += 1)
        {
            fprintf(file, "%slong a%u", a == 0 ? "" : ",", a);
        }
        fputs("){return ", file);
        for (uint32_t a = 0; a < 239; a += 1)
        {
            fprintf(file, "%sa%u", a == 0 ? "" : "+", a);
        }
        fputs(";}\n", file);
        static const char* const headers[] = {
            "long calls(long x){return ", "long indirect(long x,long (*f)(long,long)){return ",
            "double floating(double x){return ", "long side_effects(void){return ",
            "long conditional(long x){return ", "long wide_prefix(long x){return ",
        };
        static const char* const opens[] = { "add(x,", "f(x,", "addd(x,", "add(next(),", "x+(", "x+(" };
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(headers); c += 1)
        {
            const uint32_t depth = c == 5 ? 300 : 1000;
            fputs(headers[c], file);
            for (uint32_t n = 0; n < depth; n += 1)
            {
                fputs(opens[c], file);
            }
            if (c == 5)
            {
                fputs("wide(", file);
                for (uint32_t a = 0; a < 239; a += 1)
                {
                    fputs(a == 0 ? "x" : ",x", file);
                }
                fputc(')', file);
            }
            else
            {
                fputs(c == 3 ? "0" : c == 4 ? "(x ? 7 : 11)" : "x", file);
            }
            for (uint32_t n = 0; n < depth; n += 1)
            {
                fputc(')', file);
            }
            fputs(";}\n", file);
        }
        fputs("int main(void){V v={1,2,3,4},r;\n", file);
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(types); c += 1)
        {
            for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(depths); d += 1)
            {
                const uint32_t expected = depths[d] + 1;
                if (c == 4)
                {
                    fprintf(file, "r=expr_%u_%u(v);if(r[0]!=%u||r[1]!=%u||r[2]!=%u||r[3]!=%u)return %u;\n",
                            c, d, expected, 2 * expected, 3 * expected, 4 * expected, 1 + c * 7 + d);
                }
                else
                {
                    fprintf(file, "if(expr_%u_%u(1)!=%u)return %u;\n", c, d, expected, 1 + c * 7 + d);
                }
            }
        }
        fputs("if(calls(1)!=1001)return 40;\n"
              "if(indirect(1,add)!=1001)return 41;\n"
              "if(floating(1)!=1001)return 42;\n"
              "if(side_effects()!=500500||effects!=1000)return 43;\n"
              "if(conditional(0)!=11||conditional(1)!=1007)return 44;\n"
              "if(wide_prefix(1)!=539)return 45;return 0;}\n", file);
        written = ferror(file) == 0;
        const bool closed = fclose(file) == 0;
        written = written && closed;
    }
    return written;
}

// Clang's recursive parser needs more host stack for the deepest oracle on some releases. Raise only its child's
// soft limit, never Buster's: Buster must compile the same source under the test process's original stack limit.
static int check_test_deep_oracle(char* const* arguments)
{
    fflush(NULL);
    const pid_t child = fork();
    if (child == 0)
    {
        struct rlimit stack;
        if (getrlimit(RLIMIT_STACK, &stack) != 0)
        {
            _exit(126);
        }
        const rlim_t wanted = (rlim_t)64 * 1024 * 1024;
        if (stack.rlim_cur != RLIM_INFINITY && stack.rlim_cur < wanted)
        {
            stack.rlim_cur = stack.rlim_max == RLIM_INFINITY || stack.rlim_max >= wanted ? wanted : stack.rlim_max;
            if (setrlimit(RLIMIT_STACK, &stack) != 0)
            {
                _exit(126);
            }
        }
        char* text = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "deep-value Clang oracle: %s", text);
        }
        free(text);
        _exit(status >= 0 ? status : 126);
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && child > 0 && errno == EINTR);
    return child > 0 && waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void check_test_driver_deep(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512], object[512], binary[512];
    snprintf(source, sizeof(source), "%s/deep-values.c", directory);
    snprintf(object, sizeof(object), "%s/deep-values.o", directory);
    snprintf(binary, sizeof(binary), "%s/deep-values", directory);
    const bool written = check_test_deep_source(source);
    check_test_expect(test, written, "write the deep-value driver regression");
    for (uint32_t route = 0; written && route < 5; route += 1)
    {
        unlink(object);
        unlink(binary);
        char* text = NULL;
        int compiled = -1;
        if (route == 0)
        {
            char* arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-fbracket-depth=8192", "-c", source, "-o", object, NULL };
            compiled = check_test_deep_oracle(arguments);
        }
        else
        {
            const char* extra[] = { "--tier1" };
            CheckCorpusOracle outcome;
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run_with(executable, source, object, route == 2 || route == 4, false, false, false,
                                                        extra, route >= 3 ? 1 : 0, &outcome, &warnings);
            compiled = ran && !outcome.rejected ? 0 : -1;
        }
        free(text);
        text = NULL;
        char* link[] = { (char*)clang, object, "-o", binary, NULL };
        const int linked = compiled == 0 ? check_test_driver_spawn(link, &text) : -1;
        free(text);
        text = NULL;
        char* run[] = { binary, NULL };
        const int status = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
        char message[192];
        snprintf(message, sizeof(message), "deep values, route %u (clang, default, scalar, tier1, scalar tier1): compile %d, link %d, run %d",
                 route, compiled, linked, status);
        check_test_expect(test, compiled == 0 && linked == 0 && status == 0, message);
        free(text);
    }
    unlink(binary);
    unlink(object);
    unlink(source);
}
