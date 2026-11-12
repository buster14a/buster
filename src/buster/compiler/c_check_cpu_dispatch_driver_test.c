// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's cpu_specific and cpu_dispatch multiversioning (#1447, c_check_cpu_dispatch.c and
// c_check_program_cpu_dispatch.c), by the driver against clang 23, on original fixtures:
//
// - Diagnostics: each unit's whole output (diagnostics, their places, their order, their notes) must be clang's by both
//   paths, in GNU C17: the CPU names, the lists' duplicates (-Wfunction-multiversion), the redeclaration rules (the
//   missing attribute, mixed multiversioning kinds, a dispatcher's other CPUs, two versions of one CPU, the return type,
//   inline specification and linkage, the prototype, main) and a dispatcher's body; then the warnings' options.
// - Objects: -c of a unit of versions (external ones of several CPUs, and a static group with its dispatcher) and of a
//   unit with the external dispatcher and calls, by clang and both paths: each symbol clang has (f.A, f.O, f.P, f.V,
//   f.ifunc, f, f.resolver, s.A, s.a, s.ifunc, s, s.resolver) with clang's binding, type and definedness; the scalar and
//   vector objects the same bytes; and every mix of clang's and this executable's objects linked by cc, and the direct
//   path's executables of the unit of both, and -g's objects of it, run as clang's build.
// - The resolver's choice: a unit that defines libgcc's CPU model itself (statically initialized, as an IFUNC's resolver
//   runs at load), with versions of eleven CPUs of distinct masks and two of none (no CPU whose code this host may not run,
//   as diamondrapids's prologue may have APX's instructions), built by clang and both paths for
//   each of a set of models (none, each CPU's mask alone, each but one of its bits, every bit), must run as clang's.
// Include after the driver test's ELF reader (CheckTestElf) and check_test_fortify_execute.

// A symbol's binding, type and definedness in the object at `path` (a byte each, the section index's being 0 or 1), or
// false where it has none of `name`.
static bool check_test_cpu_dispatch_symbol(const char* path, const char* name, uint32_t* facts)
{
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    CheckTestElf elf = { 0 };
    bool found = false;
    if (arena.base != 0 && check_test_elf_open(&elf, object_test_read_file(&arena, path)))
    {
        for (uint64_t i = 1; !found && i < elf.symbol_count; i += 1)
        {
            const char* spelling = check_test_elf_string(elf.symbol_names, elf.symbol_names_size, elf.symbols[i].st_name);
            found = spelling != NULL && strcmp(spelling, name) == 0;
            *facts = found ? (uint32_t)ELF64_ST_BIND(elf.symbols[i].st_info) << 16 | (uint32_t)ELF64_ST_TYPE(elf.symbols[i].st_info) << 8 |
                                 (elf.symbols[i].st_shndx != SHN_UNDEF ? 1u : 0u) : *facts;
        }
    }
    arena_release(&arena);
    return found;
}

static void check_test_cpu_dispatch_diagnostics(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        // Valid uses: versions and their dispatcher, of several CPUs, of no mangling, a static group, a version's
        // declaration before its definition, a dispatcher alone, a plain declaration before the first version, and a
        // dispatcher's body, which clang ignores (its warning at its first statement, none for an empty body).
        "__attribute__((cpu_specific(generic))) int f(void) { return 1; }\n__attribute__((cpu_specific(core_2_duo_ssse3))) int f(void) { return 2; }\n"
        "__attribute__((cpu_dispatch(generic, core_2_duo_ssse3))) int f(void);\nint main(void) { return f(); }\n",
        "__attribute__((cpu_specific(atom, generic, core_i7_sse4_2))) int f(int x) { return x; }\nint g(void) { return f(1); }\n",
        // A version's redefinition is recovered and invalid (#2362): its version's later definition is quiet, another
        // version's redefinition is reported, and the errors after them too.
        "__attribute__((cpu_specific(atom))) int f(void) { return 0; }\n__attribute__((cpu_specific(atom))) int f(void) { return 1; }\n"
        "__attribute__((cpu_specific(atom))) int f(void) { return 2; }\n__attribute__((cpu_specific(ivybridge))) int f(void) { return 3; }\n"
        "__attribute__((cpu_specific(ivybridge))) int f(void) { return 4; }\nint main(void) { return undeclared; }\n",
        "__attribute__((cpu_specific(i386))) int f(void) { return 0; }\n",
        "static __attribute__((cpu_specific(generic))) int s(void) { return 1; }\nstatic __attribute__((cpu_dispatch(generic))) int s(void);\n"
        "int g(void) { return s(); }\n",
        "__attribute__((cpu_specific(atom))) int f(void);\n__attribute__((cpu_specific(atom))) int f(void) { return 0; }\n",
        "__attribute__((cpu_dispatch(atom, generic))) int f(void);\nint g(void) { return f(); }\n",
        "int f(void);\n__attribute__((cpu_specific(atom))) int f(void) { return 0; }\n__attribute__((cpu_specific(generic))) int f(void) { return 1; }\n",
        "__attribute__((cpu_dispatch(generic))) int f(void) { return 0; }\n",
        "__attribute__((cpu_dispatch(generic))) int f(void) {}\n",
        "__attribute__((cpu_dispatch(generic))) int f(void) { int y = 1; }\n",
        "__attribute__((cpu_specific(atom))) int g(void) { }\n",
        "void h(void) { __attribute__((cpu_specific(atom))) int f(void); }\n",
        // The CPUs' names: an unknown one, at it; one that is no identifier; none; one on a variable.
        "__attribute__((cpu_specific(foo))) int f(void) { return 1; }\n",
        "__attribute__((cpu_dispatch(foo))) int f(void);\n",
        "__attribute__((cpu_specific(atom, foo, bar))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific(\"atom\"))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific())) int f(void) { return 0; }\n",
        "int x __attribute__((cpu_specific(atom)));\n",
        // Duplicate entries: one name twice, two names of one mangling, two of none.
        "__attribute__((cpu_specific(generic, generic))) int f(void) { return 0; }\n",
        "__attribute__((cpu_dispatch(generic, generic))) int f(void);\n",
        "__attribute__((cpu_specific(atom, bonnell))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific(i386, znver5))) int f(void) { return 0; }\n",
        // Redeclarations.
        "__attribute__((cpu_specific(generic))) int f(void) { return 0; }\n__attribute__((cpu_specific(generic))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific(generic))) int f(void) { return 0; }\n__attribute__((cpu_specific(atom, generic))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific(atom))) int f(void);\n__attribute__((cpu_specific(generic))) int f(void);\n"
        "__attribute__((cpu_specific(generic, atom))) int f(void);\n",
        "__attribute__((cpu_specific(generic))) int f(void) { return 0; }\n__attribute__((cpu_specific(atom))) int f(void) { return 0; }\n"
        "__attribute__((cpu_dispatch(atom))) int f(void);\n__attribute__((cpu_dispatch(generic))) int f(void);\n",
        "__attribute__((cpu_dispatch(atom))) int f(void);\n__attribute__((cpu_specific(atom))) int f(void);\n__attribute__((cpu_dispatch(atom))) int f(void);\n"
        "__attribute__((cpu_dispatch(atom, generic))) int f(void);\n",
        "__attribute__((cpu_dispatch(atom))) int f(void) { return 0; }\n__attribute__((cpu_dispatch(atom))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific(generic))) int f(void) { return 0; }\nint f(void);\n",
        "__attribute__((cpu_specific(atom))) int f(void) { return 1; }\nvoid h(void) { int f(void); f(); }\n",
        "__attribute__((cpu_specific(generic))) int f(void) { return 0; }\n__attribute__((target(\"avx\"))) int f(void) { return 0; }\n",
        "__attribute__((target(\"default\"))) int f(void);\n__attribute__((cpu_specific(atom))) int f(void);\n",
        "__attribute__((cpu_specific(atom))) int f(void);\n__attribute__((target_clones(\"avx\", \"default\"))) int f(void);\n",
        "__attribute__((cpu_specific(generic))) int f(void) { return 0; }\n__attribute__((cpu_specific(atom))) long f(void) { return 0; }\n",
        "__attribute__((cpu_specific(generic))) int f(void) { return 0; }\n__attribute__((cpu_dispatch(atom))) long f(void);\n",
        "__attribute__((cpu_specific(generic))) int f(void) { return 0; }\n__attribute__((cpu_specific(atom))) long f(void);\n"
        "__attribute__((cpu_specific(pentium))) int f(void);\n",
        "static __attribute__((cpu_specific(generic))) int f(void) { return 0; }\n__attribute__((cpu_specific(atom))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific(generic))) int f(void) { return 0; }\nstatic __attribute__((cpu_specific(atom))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific(generic))) inline int f(void) { return 0; }\n__attribute__((cpu_specific(atom))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific(generic))) int f(int x) { return x; }\n__attribute__((cpu_specific(atom))) int f(long x) { return (int)x; }\n",
        // The prototype, main, and other attributes (#839's).
        "__attribute__((cpu_specific(generic))) int f();\n",
        "int f();\n__attribute__((cpu_specific(generic))) int f(void) { return 0; }\n",
        "__attribute__((cpu_specific(generic))) int main(void) { return 0; }\n",
        "__attribute__((cpu_specific(atom), cpu_dispatch(atom))) int f(void);\n",
        "__attribute__((cpu_specific(atom), always_inline)) int f(void);\n",
        "__attribute__((noinline)) int f(void);\n__attribute__((cpu_specific(atom))) int f(void);\n",
    };
    // Units whose warnings each option set below changes.
    static const char* const flagged[] = {
        "__attribute__((cpu_specific(generic, generic))) int f(void) { return 0; }\n",
        "__attribute__((cpu_dispatch(generic))) int f(void) { return 0; }\n",
    };
    static const char* const options[] = {
        "-Werror",
        "-w",
        "-Wno-function-multiversion",
        "-Werror=function-multiversion",
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/cpu-dispatch.c", directory);
    const uint32_t plain = BUSTER_ARRAY_LENGTH(units);
    const uint32_t total = plain + BUSTER_ARRAY_LENGTH(flagged) * BUSTER_ARRAY_LENGTH(options);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < total; unit += 1)
    {
        const uint32_t flag = unit - plain;
        const char* text = unit < plain ? units[unit] : flagged[flag % BUSTER_ARRAY_LENGTH(flagged)];
        char* option = unit < plain ? NULL : (char*)options[flag / BUSTER_ARRAY_LENGTH(flagged)];
        const bool made = parse_test_write(source, text, strlen(text));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", source, option, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "cpu_dispatch %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[option != NULL ? 6 : 5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "cpu_dispatch %u (%s%s%s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit,
                     option != NULL ? option : "", option != NULL ? " " : "", text, scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}

// Writes the program text of `source` to `path`, compiles it to `output` with `compiler` (and --scalar where `scalar`,
// -c where `object`), with `define` as a -D option where it isn't NULL. Returns whether it compiled.
static bool check_test_cpu_dispatch_build(const char* compiler, const char* path, const char* output, bool object, bool scalar, const char* define)
{
    char* arguments[12] = { (char*)compiler, "-std=gnu17", "-O0", "-w" };
    uint32_t count = 4;
    if (object)
    {
        arguments[count++] = "-c";
    }
    if (define != NULL)
    {
        arguments[count++] = (char*)define;
    }
    arguments[count++] = (char*)path;
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    arguments[count] = NULL;
    char* text = NULL;
    unlink(output);
    const bool built = check_test_driver_spawn(arguments, &text) == 0;
    free(text);
    return built;
}

// Links `first` and `second` with cc (clang) into `output` and runs it: its output in `captured`.
static bool check_test_cpu_dispatch_link_run(const char* clang, const char* first, const char* second, const char* output, char* captured, size_t capacity)
{
    char* arguments[] = { (char*)clang, (char*)first, (char*)second, "-o", (char*)output, NULL };
    char* text = NULL;
    const bool linked = check_test_driver_spawn(arguments, &text) == 0;
    free(text);
    captured[0] = 0;
    return linked && check_test_driver_capture(output, captured, capacity);
}

static void check_test_cpu_dispatch_objects(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char versions[] =
        "__attribute__((cpu_specific(atom, generic, core_i7_sse4_2))) int f(int x) { return x + 3; }\n"
        "__attribute__((cpu_specific(haswell))) int f(int x) { return x * 2; }\n"
        "static __attribute__((cpu_specific(generic))) int s(void) { return 10; }\n"
        "static __attribute__((cpu_specific(skylake_avx512))) int s(void) { return 20; }\n"
        "static __attribute__((cpu_dispatch(generic, skylake_avx512))) int s(void);\n"
        "int call_s(void) { return s(); }\n";
    static const char dispatcher[] =
        "#include <stdio.h>\n"
        "__attribute__((cpu_dispatch(atom, generic, core_i7_sse4_2, haswell))) int f(int x);\n"
        "int call_s(void);\n"
        "int (*address_f)(int) = f;\n"
        "int main(void) { printf(\"%d %d %d\\n\", f(5), call_s(), address_f(1)); return 0; }\n";
    static const char* const version_names[] = { "f.A", "f.O", "f.P", "f.V", "s.A", "s.a", "s.ifunc", "s", "s.resolver", "call_s", "__cpu_model" };
    static const char* const dispatcher_names[] = { "f.A", "f.O", "f.P", "f.V", "f.ifunc", "f", "f.resolver", "address_f", "__cpu_indicator_init" };
    char sources[2][PATH_MAX];
    char objects[2][3][PATH_MAX];
    snprintf(sources[0], sizeof(sources[0]), "%s/cpu-versions.c", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/cpu-dispatcher.c", directory);
    const bool written = parse_test_write(sources[0], versions, sizeof(versions) - 1) && parse_test_write(sources[1], dispatcher, sizeof(dispatcher) - 1);
    check_test_expect(test, written, "cpu_dispatch: the objects' fixtures are written");
    bool built[2][3] = { { false } };
    for (uint32_t u = 0; written && u < 2; u += 1)
    {
        for (uint32_t p = 0; p < 3; p += 1)
        {
            snprintf(objects[u][p], sizeof(objects[u][p]), "%s/cpu-%s-%u.o", directory, u == 0 ? "versions" : "dispatcher", p);
            built[u][p] = check_test_cpu_dispatch_build(p == 0 ? clang : executable, sources[u], objects[u][p], true, p == 2, NULL);
            char description[256];
            snprintf(description, sizeof(description), "cpu_dispatch: the %s unit compiles through %s", u == 0 ? "versions'" : "dispatcher's",
                     p == 0 ? "clang" : p == 1 ? "the vector path" : "the scalar path");
            check_test_expect(test, built[u][p], description);
        }
        const char* const* names = u == 0 ? version_names : dispatcher_names;
        const uint32_t name_count = u == 0 ? BUSTER_ARRAY_LENGTH(version_names) : BUSTER_ARRAY_LENGTH(dispatcher_names);
        for (uint32_t p = 1; p < 3; p += 1)
        {
            for (uint32_t n = 0; built[u][0] && built[u][p] && n < name_count; n += 1)
            {
                uint32_t theirs = UINT32_MAX;
                uint32_t ours = UINT32_MAX - 1;
                const bool found = check_test_cpu_dispatch_symbol(objects[u][0], names[n], &theirs) && check_test_cpu_dispatch_symbol(objects[u][p], names[n], &ours);
                char description[256];
                snprintf(description, sizeof(description), "cpu_dispatch: %s's binding, type and definedness are clang's (%06x, clang %06x), by the %s path", names[n], ours,
                         theirs, p == 1 ? "vector" : "scalar");
                check_test_expect(test, found && ours == theirs, description);
            }
        }
        check_test_expect(test, built[u][1] && built[u][2] && check_test_driver_same_files(objects[u][1], objects[u][2]),
                          "cpu_dispatch: the scalar and vector objects match byte for byte");
    }
    // Every mix of the objects, linked by cc: clang's, each path's, and each with clang's other.
    static const uint32_t mixes[][2] = { { 0, 0 }, { 1, 1 }, { 2, 2 }, { 1, 0 }, { 0, 1 }, { 2, 0 }, { 0, 2 } };
    char runs[BUSTER_ARRAY_LENGTH(mixes)][128];
    char program[PATH_MAX];
    snprintf(program, sizeof(program), "%s/cpu-mixed", directory);
    for (uint32_t m = 0; written && m < BUSTER_ARRAY_LENGTH(mixes); m += 1)
    {
        const bool ran = built[0][mixes[m][0]] && built[1][mixes[m][1]] &&
                         check_test_cpu_dispatch_link_run(clang, objects[0][mixes[m][0]], objects[1][mixes[m][1]], program, runs[m], sizeof(runs[m]));
        char description[384];
        snprintf(description, sizeof(description), "cpu_dispatch: the versions' object %u and the dispatcher's %u link and run as clang's: \"%s\" (clang \"%s\")",
                 mixes[m][0], mixes[m][1], ran ? runs[m] : "", runs[0]);
        check_test_expect(test, ran && strcmp(runs[m], runs[0]) == 0, description);
    }
    unlink(program);
    // The direct path's executables of the units as one, of both paths.
    char together[PATH_MAX];
    snprintf(together, sizeof(together), "%s/cpu-together.c", directory);
    char text[sizeof(versions) + sizeof(dispatcher)];
    snprintf(text, sizeof(text), "%s%s", versions, dispatcher);
    const bool joined = written && parse_test_write(together, text, strlen(text));
    for (uint32_t p = 0; joined && p < 2; p += 1)
    {
        char direct[PATH_MAX];
        char captured[128] = { 0 };
        snprintf(direct, sizeof(direct), "%s/cpu-direct-%u", directory, p);
        const bool ran = check_test_cpu_dispatch_build(executable, together, direct, false, p == 1, NULL) && check_test_driver_capture(direct, captured, sizeof(captured));
        char description[384];
        snprintf(description, sizeof(description), "cpu_dispatch: the direct path's executable, by the %s path, runs as clang's build: \"%s\" (clang \"%s\")",
                 p == 0 ? "vector" : "scalar", captured, runs[0]);
        check_test_expect(test, ran && strcmp(captured, runs[0]) == 0, description);
        unlink(direct);
    }
    // With -g: the versions' bodies' debug information is at their own symbols.
    for (uint32_t p = 0; joined && p < 2; p += 1)
    {
        char debug_object[PATH_MAX];
        char debug_program[PATH_MAX];
        char captured[128] = { 0 };
        snprintf(debug_object, sizeof(debug_object), "%s/cpu-debug-%u.o", directory, p);
        snprintf(debug_program, sizeof(debug_program), "%s/cpu-debug-%u", directory, p);
        char* arguments[] = { (char*)executable, "-std=gnu17", "-O0", "-w", "-g", "-c", together, "-o", debug_object, p == 1 ? "--scalar" : NULL, NULL };
        char* text = NULL;
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        text = NULL;
        char* link[] = { (char*)clang, debug_object, "-o", debug_program, NULL };
        const bool linked = compiled && check_test_driver_spawn(link, &text) == 0;
        free(text);
        const bool ran = linked && check_test_driver_capture(debug_program, captured, sizeof(captured));
        char description[384];
        snprintf(description, sizeof(description), "cpu_dispatch: -g's object, by the %s path, links and runs as clang's build: \"%s\" (clang \"%s\")",
                 p == 0 ? "vector" : "scalar", captured, runs[0]);
        check_test_expect(test, ran && strcmp(captured, runs[0]) == 0, description);
        unlink(debug_object);
        unlink(debug_program);
    }
    for (uint32_t u = 0; u < 2; u += 1)
    {
        for (uint32_t p = 0; p < 3; p += 1)
        {
            unlink(objects[u][p]);
        }
        unlink(sources[u]);
    }
    unlink(together);
}

// The resolver's choice over libgcc's model, which the unit defines: a version of each of thirteen CPUs, each returning
// its index, dispatched together; built and run by clang and both paths for each model.
static void check_test_cpu_dispatch_models(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const cpus[] = { "generic", "pentium", "atom", "core_2_duo_ssse3", "core_i7_sse4_2", "sandybridge", "haswell",
                                        "broadwell", "skylake_avx512", "cannonlake", "icelake_client", "knl", "core_aes_pclmulqdq" };
    enum { SOURCE = 8192 };
    char* source = malloc(SOURCE);
    int length = source != NULL ? snprintf(source, SOURCE, "int printf(const char*, ...);\n"
                                                           "struct M { unsigned vendor, type, subtype; unsigned features[1]; } __cpu_model = { 0, 0, 0, { W0 } };\n"
                                                           "unsigned __cpu_features2[3] = { W1, W2, W3 };\nint __cpu_indicator_init(void) { return 0; }\n") : -1;
    for (uint32_t c = 0; length > 0 && length < SOURCE && c < BUSTER_ARRAY_LENGTH(cpus); c += 1)
    {
        length += snprintf(source + length, SOURCE - (size_t)length, "__attribute__((cpu_specific(%s))) int f(void) { return %u; }\n", cpus[c], c);
    }
    length += length > 0 && length < SOURCE ? snprintf(source + length, SOURCE - (size_t)length, "__attribute__((cpu_dispatch(") : 0;
    for (uint32_t c = 0; length > 0 && length < SOURCE && c < BUSTER_ARRAY_LENGTH(cpus); c += 1)
    {
        length += snprintf(source + length, SOURCE - (size_t)length, "%s%s", c != 0 ? ", " : "", cpus[c]);
    }
    length += length > 0 && length < SOURCE ? snprintf(source + length, SOURCE - (size_t)length,
                                                       "))) int f(void);\nint main(void) { printf(\"%%d\\n\", f()); return 0; }\n") : 0;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/cpu-models.c", directory);
    const bool written = length > 0 && length < SOURCE && parse_test_write(path, source, (size_t)length);
    check_test_expect(test, written, "cpu_dispatch: the models' fixture is written");
    // The models: none, every bit, and for each CPU its mask alone and its mask without its highest bit.
    uint32_t models[2 + 2 * BUSTER_ARRAY_LENGTH(cpus)][4] = { { 0, 0, 0, 0 }, { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX } };
    uint32_t model_count = 2;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cpus); c += 1)
    {
        const int32_t row = check_cpu_dispatch_row((String8){ .pointer = (char*)cpus[c], .length = strlen(cpus[c]) });
        for (uint32_t w = 0; row >= 0 && w < 4; w += 1)
        {
            models[model_count][w] = check_cpu_dispatch_rows[row].mask[w];
            models[model_count + 1][w] = check_cpu_dispatch_rows[row].mask[w];
        }
        uint32_t w = 4;
        while (w > 0 && models[model_count + 1][w - 1] == 0)
        {
            w -= 1;
        }
        if (w > 0)
        {
            models[model_count + 1][w - 1] &= ~(UINT32_C(1) << (31 - __builtin_clz(models[model_count + 1][w - 1])));
        }
        model_count += 2;
    }
    for (uint32_t m = 0; written && m < model_count; m += 1)
    {
        char define[160];
        snprintf(define, sizeof(define), "-DW0=%uu,W1=%uu,W2=%uu,W3=%uu", models[m][0], models[m][1], models[m][2], models[m][3]);
        // (A -D of several macros: each is its own option.)
        char defines[4][48];
        for (uint32_t w = 0; w < 4; w += 1)
        {
            snprintf(defines[w], sizeof(defines[w]), "-DW%u=%uu", w, models[m][w]);
        }
        char captured[3][64] = { { 0 } };
        bool ran[3] = { false };
        for (uint32_t b = 0; b < 3; b += 1)
        {
            char built[PATH_MAX];
            snprintf(built, sizeof(built), "%s/cpu-model-%u", directory, b);
            char* arguments[16] = { (char*)(b == 0 ? clang : executable), "-std=gnu17", "-O0", "-w", defines[0], defines[1], defines[2], defines[3], path, "-o", built,
                                    b == 2 ? "--scalar" : NULL, NULL };
            char* text = NULL;
            unlink(built);
            const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
            free(text);
            ran[b] = compiled && check_test_driver_capture(built, captured[b], sizeof(captured[b]));
            unlink(built);
        }
        for (uint32_t b = 1; b < 3; b += 1)
        {
            char description[384];
            snprintf(description, sizeof(description), "cpu_dispatch: the resolver of the model %s chooses clang's version, by the %s path: \"%s\" (clang \"%s\")",
                     define, b == 1 ? "vector" : "scalar", captured[b], captured[0]);
            check_test_expect(test, ran[0] && ran[b] && strcmp(captured[b], captured[0]) == 0, description);
        }
    }
    free(source);
    unlink(path);
}

static void check_test_driver_cpu_dispatch(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_cpu_dispatch_diagnostics(test, clang, executable, directory);
    check_test_cpu_dispatch_objects(test, clang, executable, directory);
    check_test_cpu_dispatch_models(test, clang, executable, directory);
}
