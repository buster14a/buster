// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original aggregate-return fixtures; Clang supplies the independent diagnostic oracle.

static void check_test_driver_returns(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* source;
        const char* first;
        const char* second;
    } cases[] = {
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;else return s;}\n", NULL, NULL },
        { "struct S{long a,b,c,d;};struct S f(int n,struct S s){if(n>0)return s;else if(n<0)return s;else return s;}\n", NULL, NULL },
        { "struct S{double a,b;};struct S f(int n,struct S s){switch(n){case 0:return s;default:return s;}}\n", NULL, NULL },
        { "struct S{long a,b;};struct S f(int n,struct S s){while(1){if(n)return s;else return s;}}\n", NULL, NULL },
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", NULL, NULL },
        { "struct S{long a,b,c,d;};struct S f(int n,struct S s){if(n)return s;}\n", NULL, NULL },
        { "struct S{long a,b;};struct S f(void){}\n", NULL, NULL },
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", "-w", NULL },
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", "-Wno-return-type", NULL },
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", "-Werror", "-Wno-error=return-type" },
        { "struct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", "-Wno-error=return-type", "-Werror" },
        { "#pragma clang diagnostic ignored \"-Wreturn-type\"\nstruct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", NULL, NULL },
        { "#pragma clang diagnostic error \"-Wreturn-type\"\nstruct S{long a,b;};struct S f(int n,struct S s){if(n)return s;}\n", NULL, NULL },
        { "struct S{long a,b;};struct S f(void){__builtin_trap();}\n", NULL, NULL },
        { "struct S{long a,b;};struct S f(void){(__builtin_trap(),0);}\n", NULL, NULL },
        { "struct S{long a,b;};struct S f(void){if(0)__builtin_trap();}\n", NULL, NULL },
        { "struct S{long a,b;};struct S f(struct S s){return s;s.a=1;}\n", NULL, NULL },
        { "struct S{long a,b,c,d;};struct S f(struct S s){return s;if(s.a)s.b=1;else s.c=2;}\n", NULL, NULL },
        { "struct S{long a,b;};struct S f(struct S s){switch(1){case 1:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b,c,d;};struct S f(struct S s){switch(1){case 1:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(struct S s){switch(2){case 1:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(struct S s){switch(2){case 1:break;default:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(struct S s){switch(1){case 1:break;default:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(struct S s){switch((unsigned)-1){case -1:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(struct S s){switch(-2){case -4 ... -1:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(struct S s){switch((unsigned)-1){case 4294967290U ... 4294967295U:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(struct S s){int n;switch(n=1){case 1:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(struct S s){switch(__builtin_constant_p(s.a)){case 0:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};enum E{A,B};struct S f(enum E e,struct S s){switch(e){case A:return s;case B:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};enum E{A,B};struct S f(struct S s){switch(A){case B:return s;}}\n", "-Werror=return-type", NULL },
        { "struct S{long a,b;};struct S f(_Bool n,struct S s){switch(n){case 0:return s;case 1:return s;}}\n", "-Werror=return-type", "-Wno-switch-bool" },
    };
    char source[512];
    char objects[2][512];
    snprintf(source, sizeof(source), "%s/aggregate-return.c", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const bool written = parse_test_write(source, cases[c].source, strlen(cases[c].source));
        check_test_expect(test, written, "aggregate return diagnostic fixture is written");
        for (uint32_t mode = 0; written && mode < 2; mode += 1)
        {
            char* expected = NULL;
            char* arguments[16] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", mode == 0 ? "-fsyntax-only" : "-c", source, "-o", objects[0] };
            snprintf(objects[0], sizeof(objects[0]), "%s/aggregate-return-0.o", directory);
            snprintf(objects[1], sizeof(objects[1]), "%s/aggregate-return-1.o", directory);
            uint32_t count = 7;
            if (cases[c].first != NULL)
            {
                arguments[count++] = (char*)cases[c].first;
            }
            if (cases[c].second != NULL)
            {
                arguments[count++] = (char*)cases[c].second;
            }
            arguments[count] = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[6] = objects[path];
                arguments[count] = path == 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                unlink(objects[path]);
                const int status = check_test_driver_spawn(arguments, &actual);
                const char* oracle_message = expected != NULL ? strstr(expected, "non-void function") : NULL;
                const char* actual_message = actual != NULL ? strstr(actual, "non-void function") : NULL;
                const bool warnings = (oracle_message != NULL) == (actual_message != NULL);
                const bool mixed = oracle_message != NULL && strstr(oracle_message, "in all control paths") != NULL;
                const bool same = warnings && (actual_message == NULL || ((strstr(actual_message, "in all control paths") != NULL) == mixed &&
                                  (uint64_t)(actual_message - actual) == (uint64_t)(oracle_message - expected) &&
                                  memcmp(actual, expected, (size_t)(actual_message - actual)) == 0));
                char description[160];
                snprintf(description, sizeof(description), "aggregate return case %u, %s %s: Clang status, location and warning kind", c,
                         path == 0 ? "scalar" : "vector", mode == 0 ? "syntax" : "object");
                check_test_expect(test, oracle >= 0 && status == oracle && same, description);
                check_test_expect(test, mode == 0 || (access(objects[path], F_OK) == 0) == (status == 0),
                                  "return-type errors prevent object publication");
                free(actual);
            }
            if (mode != 0 && oracle == 0)
            {
                check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]), "aggregate return scalar/vector objects are identical");
            }
            free(expected);
            unlink(objects[0]);
            unlink(objects[1]);
        }
    }
    unlink(source);
}
