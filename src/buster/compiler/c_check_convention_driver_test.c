// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The calling conventions other than System V's (#137, #147) by the driver against clang, on original fixtures. clang
// 23 compiles ms_abi, vectorcall, regcall and preserve_none on x86-64 Linux with argument registers of their own, and
// preserve_most and preserve_all with System V's arguments but more registers kept by the callee; the backend compiles
// System V's, and preserve_most's and preserve_all's definitions with Tier 0's preserving frame (#1447). So -c must
// refuse, by both paths and naming the convention, a definition and a call of each of the first four, directly and
// through a pointer, where clang compiles every one of them; a definition of a preserve_most or preserve_all function
// must compile, and so must a call of one, which keeps whatever a System V caller expects, and run: the callees built by
// clang, the callers by each path, values live across the calls, as clang's build of both runs. The preserving frames
// themselves are check_test_driver_preserving_frames'. Include after c_check_common_driver_test.c.

static void check_test_driver_conventions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const conventions[] = { "ms_abi", "vectorcall", "regcall", "preserve_none", "preserve_most", "preserve_all" };
    char path[512];
    char object[512];
    snprintf(path, sizeof(path), "%s/convention.c", directory);
    snprintf(object, sizeof(object), "%s/convention.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(conventions); c += 1)
    {
        // The definition, a direct call and a call through a pointer, each a unit of its own.
        const bool callable = c >= 4;
        char sources[3][512];
        snprintf(sources[0], sizeof(sources[0]), "int __attribute__((%s)) f(int x, long y) { return x - (int)y; }\n", conventions[c]);
        snprintf(sources[1], sizeof(sources[1]), "int __attribute__((%s)) f(int x, long y);\nint g(int n) { return f(n, 2) + n; }\n", conventions[c]);
        snprintf(sources[2], sizeof(sources[2]), "int g(int (__attribute__((%s)) *p)(int, long), int n) { return p(n, 3) * 2; }\n", conventions[c]);
        static const char* const shapes[] = { "a definition", "a direct call", "a call through a pointer" };
        for (uint32_t s = 0; s < 3; s += 1)
        {
            const bool written = parse_test_write(path, sources[s], strlen(sources[s]));
            char* text = NULL;
            char* reference[] = { (char*)clang, "-std=gnu17", "-O0", "-c", path, "-o", object, NULL };
            const int clang_status = written ? check_test_driver_spawn(reference, &text) : -1;
            free(text);
            char description[512];
            snprintf(description, sizeof(description), "conventions: clang compiles %s of %s", shapes[s], conventions[c]);
            check_test_expect(test, clang_status == 0, description);
            const bool refused = !callable;
            char expected[128];
            snprintf(expected, sizeof(expected), "unsupported: functions of the %s calling convention", conventions[c]);
            for (uint32_t p = 0; written && p < 2; p += 1)
            {
                char* arguments[] = { (char*)executable, "-std=gnu17", "-c", path, "-o", object, p == 1 ? "--scalar" : NULL, NULL };
                const int status = check_test_driver_spawn(arguments, &text);
                const bool named = text != NULL && strstr(text, expected) != NULL;
                snprintf(description, sizeof(description), "conventions: %s of %s by the %s path %s (status %d: %s)", shapes[s], conventions[c],
                         p == 1 ? "scalar" : "vector", refused ? "is refused by name" : "compiles", status, text != NULL ? text : "");
                check_test_expect(test, refused ? status == 1 && named : status == 0 && !named, description);
                free(text);
            }
        }
    }
    // preserve_most's and preserve_all's callees by clang, called by each path's code with values live across the calls.
    static const char callees[] =
        "int printf(const char*, ...);\n"
        "static long mix(long a, long b) { return a * 31 + b; }\n"
        "long __attribute__((preserve_most)) most(long a, int b, double c) { printf(\"most %ld %d %.1f\\n\", a, b, c); return mix(a, b) + (long)c; }\n"
        "double __attribute__((preserve_all)) all(double a, long b) { printf(\"all %.1f %ld\\n\", a, b); return a * 2 + (double)mix(b, 3); }\n";
    static const char callers[] =
        "#include <stdio.h>\n"
        "long __attribute__((preserve_most)) most(long a, int b, double c);\n"
        "double __attribute__((preserve_all)) all(double a, long b);\n"
        "int main(void) {\n"
        "    long (__attribute__((preserve_most)) *p)(long, int, double) = most;\n"
        "    long total = 0; double sum = 0;\n"
        "    for (int i = 1; i < 6; i++) { long kept = i * 7; double held = i * 0.5; total += most(kept, i, held) + p(i, -i, 1.5) + kept;\n"
        "        sum += all(held, kept) + held; }\n"
        "    printf(\"%ld %.1f\\n\", total, sum); return 0; }\n";
    char callee_path[512];
    char caller_path[512];
    char callee_object[512];
    char reference[512];
    snprintf(callee_path, sizeof(callee_path), "%s/convention-callees.c", directory);
    snprintf(caller_path, sizeof(caller_path), "%s/convention-callers.c", directory);
    snprintf(callee_object, sizeof(callee_object), "%s/convention-callees.o", directory);
    snprintf(reference, sizeof(reference), "%s/convention-clang", directory);
    const bool written = parse_test_write(callee_path, callees, sizeof(callees) - 1) && parse_test_write(caller_path, callers, sizeof(callers) - 1);
    char* text = NULL;
    char* compile[] = { (char*)clang, "-std=gnu17", "-O0", "-c", callee_path, "-o", callee_object, NULL };
    const bool callee_built = written && check_test_driver_spawn(compile, &text) == 0;
    free(text);
    char* build[] = { (char*)clang, "-std=gnu17", "-O0", "-w", caller_path, callee_object, "-o", reference, NULL };
    const bool built = callee_built && check_test_driver_spawn(build, &text) == 0;
    free(text);
    char expected[1024] = { 0 };
    check_test_expect(test, built && check_test_driver_capture(reference, expected, sizeof(expected)), "conventions: clang's build of the preserved callers runs");
    for (uint32_t p = 0; built && p < 2; p += 1)
    {
        char caller_object[512];
        char linked[512];
        snprintf(caller_object, sizeof(caller_object), "%s/convention-callers-%u.o", directory, p);
        snprintf(linked, sizeof(linked), "%s/convention-ours-%u", directory, p);
        char* arguments[] = { (char*)executable, "-std=gnu17", "-w", "-c", caller_path, "-o", caller_object, p == 1 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        char* link[] = { (char*)clang, caller_object, callee_object, "-o", linked, NULL };
        const bool linked_ok = compiled && check_test_driver_spawn(link, &text) == 0;
        free(text);
        char ran[1024] = { 0 };
        char description[2400];
        const bool running = linked_ok && check_test_driver_capture(linked, ran, sizeof(ran));
        snprintf(description, sizeof(description), "conventions: the %s path's calls of clang's preserve_most and preserve_all callees run as clang's build: %s against %s",
                 p == 1 ? "scalar" : "vector", running ? ran : "(nothing)", expected);
        check_test_expect(test, running && strcmp(ran, expected) == 0, description);
        unlink(caller_object);
        unlink(linked);
    }
    unlink(callee_path);
    unlink(caller_path);
    unlink(callee_object);
    unlink(reference);
    unlink(path);
    unlink(object);
}

// Tier 0's preserving frames (#147, #1447: Tier0Function.preserve_general and the rest), by each path: preserve_most's
// and preserve_all's definitions, of a value, void, two registers' and a double's results, calls, a variable-length
// array, an object aligned to 64, variadic arguments and a backtrace from a callee, called from a harness clang builds
// (inline assembly that sets every general register and xmm0 to xmm15, or ymm0 to ymm15 with AVX, calls through r11 and
// reads them all back): every register the convention keeps but the result's must come back, rsp too, the results
// right and the backtraces through the frame complete. On x86-64; with probes, frame pointers and debug information;
// and, where the host has AVX2, for x86-64-v3, whose preserve_all keeps the YMMs whole. And the frames Tier 0's
// preserving frame doesn't take are refused by name: a musttail call's, __builtin_eh_return's and __builtin_setjmp's,
// which clang compiles.
static void check_test_driver_preserving_frames(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char harness[] =
        "#include <execinfo.h>\n"
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "#include <unwind.h>\n"
        "// probe(state): every general register but rsp from state->in (rbp's too), zmm0 to zmm31 and k0 to k7 with AVX-512 (ymm0\n"
        "// to ymm15 with AVX, xmm0 to xmm15 without) from state->vin and state->kin, a call of state->fn through r11 (r11 holds it),\n"
        "// then every register into state->out, state->vout and state->kout.\n"
        "typedef struct State { uint64_t in[16]; uint64_t out[16]; uint8_t vin[32][64]; uint8_t vout[32][64]; uint64_t kin[8]; uint64_t kout[8]; void* fn; uint64_t sp; } State;\n"
        "_Static_assert(__builtin_offsetof(State, vin) == 256 && __builtin_offsetof(State, vout) == 2304 && __builtin_offsetof(State, kin) == 4352 && __builtin_offsetof(State, kout) == 4416 && __builtin_offsetof(State, fn) == 4480 && __builtin_offsetof(State, sp) == 4488, \"layout\");\n"
        "void probe(State* s);\n"
        "#define LOW \"0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\"\n"
        "#define ALL LOW \",16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\"\n"
        "__asm__(\".text\\n.globl probe\\n.type probe,@function\\nprobe:\\n\"\n"
        "        \"push %rbx\\npush %rbp\\npush %r12\\npush %r13\\npush %r14\\npush %r15\\npush %rdi\\nmov %rdi, %rax\\n\"\n"
        "#if defined(__AVX512F__)\n"
        "        \".irp n,\" ALL \"\\nvmovdqu64 256+64*\\\\n(%rax), %zmm\\\\n\\n.endr\\n.irp n,0,1,2,3,4,5,6,7\\nkmovq 4352+8*\\\\n(%rax), %k\\\\n\\n.endr\\n\"\n"
        "#elif defined(__AVX__)\n"
        "        \".irp n,\" LOW \"\\nvmovdqu 256+64*\\\\n(%rax), %ymm\\\\n\\n.endr\\n\"\n"
        "#else\n"
        "        \".irp n,\" LOW \"\\nmovdqu 256+64*\\\\n(%rax), %xmm\\\\n\\n.endr\\n\"\n"
        "#endif\n"
        "        \"mov %rsp, 4488(%rax)\\nmov 8(%rax), %rcx\\nmov 16(%rax), %rdx\\nmov 24(%rax), %rbx\\nmov 40(%rax), %rbp\\nmov 48(%rax), %rsi\\nmov 56(%rax), %rdi\\n\"\n"
        "        \"mov 64(%rax), %r8\\nmov 72(%rax), %r9\\nmov 80(%rax), %r10\\nmov 96(%rax), %r12\\nmov 104(%rax), %r13\\nmov 112(%rax), %r14\\nmov 120(%rax), %r15\\n\"\n"
        "        \"mov 4480(%rax), %r11\\nmov (%rax), %rax\\ncall *%r11\\npush %rax\\nmov 8(%rsp), %rax\\npop 128(%rax)\\n\"\n"
        "        \"mov %rcx, 136(%rax)\\nmov %rdx, 144(%rax)\\nmov %rbx, 152(%rax)\\nmov %rsp, 160(%rax)\\nmov %rbp, 168(%rax)\\nmov %rsi, 176(%rax)\\nmov %rdi, 184(%rax)\\n\"\n"
        "        \"mov %r8, 192(%rax)\\nmov %r9, 200(%rax)\\nmov %r10, 208(%rax)\\nmov %r11, 216(%rax)\\nmov %r12, 224(%rax)\\nmov %r13, 232(%rax)\\nmov %r14, 240(%rax)\\nmov %r15, 248(%rax)\\n\"\n"
        "#if defined(__AVX512F__)\n"
        "        \".irp n,\" ALL \"\\nvmovdqu64 %zmm\\\\n, 2304+64*\\\\n(%rax)\\n.endr\\n.irp n,0,1,2,3,4,5,6,7\\nkmovq %k\\\\n, 4416+8*\\\\n(%rax)\\n.endr\\nvzeroupper\\n\"\n"
        "#elif defined(__AVX__)\n"
        "        \".irp n,\" LOW \"\\nvmovdqu %ymm\\\\n, 2304+64*\\\\n(%rax)\\n.endr\\nvzeroupper\\n\"\n"
        "#else\n"
        "        \".irp n,\" LOW \"\\nmovdqu %xmm\\\\n, 2304+64*\\\\n(%rax)\\n.endr\\n\"\n"
        "#endif\n"
        "        \"add $8, %rsp\\npop %r15\\npop %r14\\npop %r13\\npop %r12\\npop %rbp\\npop %rbx\\nret\\n.globl probe_end\\nprobe_end:\\n\");\n"
        "// What each convention keeps of a vector register: preserve_all's whole YMMs with AVX (xmm without), and\n"
        "// no_caller_saved_registers' whole ZMMs, zmm16 to zmm31 and the masks with AVX-512 (each mask's 8 bytes, BW's).\n"
        "#ifdef __AVX__\n"
        "#define ALL_WIDTH 32\n"
        "#else\n"
        "#define ALL_WIDTH 16\n"
        "#endif\n"
        "#ifdef __AVX512F__\n"
        "#define EVERY_WIDTH 64\n"
        "#define EVERY_VECTORS 0xffffffffu\n"
        "#define EVERY_MASKS 0xffu\n"
        "#else\n"
        "#define EVERY_WIDTH ALL_WIDTH\n"
        "#define EVERY_VECTORS 0xffffu\n"
        "#define EVERY_MASKS 0u\n"
        "#endif\n"
        "static State last;\n"
        "static const char* names[16] = { \"rax\", \"rcx\", \"rdx\", \"rbx\", \"rsp\", \"rbp\", \"rsi\", \"rdi\", \"r8\", \"r9\", \"r10\", \"r11\", \"r12\", \"r13\", \"r14\", \"r15\" };\n"
        "// The value a general register has when probe calls: s->in's, but r11's, which holds the function.\n"
        "static uint64_t given(const State* s, uint32_t r) { return r == 11 ? (uint64_t)s->fn : s->in[r]; }\n"
        "// A call of `fn` with a and b in rdi and rsi: the general registers `kept`, the vector ones `vkept` (their first `width`\n"
        "// bytes) and the masks `kkept` must come back.\n"
        "static int check(const char* what, void* fn, uint32_t kept, uint32_t vkept, size_t width, uint32_t kkept, long a, long b, long* result)\n"
        "{\n"
        "    State s;\n"
        "    memset(&s, 0, sizeof(s));\n"
        "    for (uint32_t r = 0; r < 16; r += 1) { s.in[r] = 0x1111111111111111ull * (r + 1) ^ 0x0123456789abcdefull; }\n"
        "    s.in[7] = (uint64_t)a;\n"
        "    s.in[6] = (uint64_t)b;\n"
        "    for (uint32_t v = 0; v < 32; v += 1) { for (uint32_t k = 0; k < 64; k += 1) { s.vin[v][k] = (uint8_t)(v * 37 + k * 11 + 5); } }\n"
        "    for (uint32_t k = 0; k < 8; k += 1) { s.kin[k] = 0x0f1e2d3c4b5a6978ull * (k + 3); }\n"
        "    s.fn = fn;\n"
        "    probe(&s);\n"
        "    last = s;\n"
        "    int bad = s.out[4] != s.sp;\n"
        "    for (uint32_t r = 0; r < 16; r += 1)\n"
        "    {\n"
        "        if ((kept >> r & 1) && r != 4 && s.out[r] != given(&s, r)) { printf(\"%s: %s changed\\n\", what, names[r]); bad = 1; }\n"
        "    }\n"
        "    for (uint32_t v = 0; v < 32; v += 1)\n"
        "    {\n"
        "        if ((vkept >> v & 1) && memcmp(s.vin[v], s.vout[v], width) != 0) { printf(\"%s: vector register %u changed\\n\", what, v); bad = 1; }\n"
        "    }\n"
        "    for (uint32_t k = 0; k < 8; k += 1)\n"
        "    {\n"
        "        if ((kkept >> k & 1) && s.kout[k] != s.kin[k]) { printf(\"%s: k%u changed\\n\", what, k); bad = 1; }\n"
        "    }\n"
        "    *result = (long)s.out[0];\n"
        "    return bad;\n"
        "}\n"
        "// A backtrace from a callee of the preserving frame (observe): each general register of `unwound_kept` (but rsp, the CFA)\n"
        "// libgcc's unwinder recovers in the frame of its call from probe, which the frame's rules give, into `unwound`. libgcc has\n"
        "// no place of a register no frame on the way saves, so the others aren't read.\n"
        "extern char probe_end[];\n"
        "static uint64_t unwound[16];\n"
        "static uint32_t unwound_kept;\n"
        "static int unwound_found;\n"
        "static _Unwind_Reason_Code unwound_frame(struct _Unwind_Context* context, void* argument)\n"
        "{\n"
        "    static const int dwarf[16] = { 0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15 };\n"
        "    const uintptr_t ip = _Unwind_GetIP(context);\n"
        "    (void)argument;\n"
        "    if (ip > (uintptr_t)probe && ip <= (uintptr_t)probe_end && !unwound_found)\n"
        "    {\n"
        "        for (int r = 0; r < 16; r += 1) { unwound[r] = r == 4 || (unwound_kept >> r & 1) == 0 ? 0 : _Unwind_GetGR(context, dwarf[r]); }\n"
        "        unwound_found = 1;\n"
        "    }\n"
        "    return _URC_NO_REASON;\n"
        "}\n"
        "void observe(void) { _Unwind_Backtrace(unwound_frame, 0); }\n"
        "// The registers `kept` an unwinder recovers in probe's frame are those probe called with.\n"
        "static int recovered(const char* what, uint32_t kept)\n"
        "{\n"
        "    int bad = !unwound_found;\n"
        "    for (uint32_t r = 0; unwound_found && r < 16; r += 1)\n"
        "    {\n"
        "        if ((kept >> r & 1) && r != 4 && unwound[r] != given(&last, r)) { printf(\"%s: %s unwound as %llx\\n\", what, names[r], (unsigned long long)unwound[r]); bad = 1; }\n"
        "    }\n"
        "    unwound_found = 0;\n"
        "    return bad;\n"
        "}\n"
        "typedef struct Pair { long a, b; } Pair;\n"
        "long most(long a, long b);\n"
        "long most_calls(long a, long b);\n"
        "void most_void(long a, long b);\n"
        "Pair most_pair(long a, long b);\n"
        "long most_var(long n, ...);\n"
        "long all(long a, long b);\n"
        "long all_calls(long a, long b);\n"
        "double all_double(double a, double b);\n"
        "long all_vla(long a, long b);\n"
        "long all_aligned(long a, long b);\n"
        "long all_trace(long a, long b);\n"
        "void all_observe(long a, long b);\n"
        "long most_observe(long a, long b);\n"
        "long every(long a, long b);\n"
        "long every_calls(long a, long b);\n"
        "void every_void(long a, long b);\n"
        "double every_double(double a, double b);\n"
        "long every_aligned(long a, long b);\n"
        "long every_var(long n, ...);\n"
        "void every_observe(long a, long b);\n"
        "long every_cdecl(long a, long b);\n"
        "extern int depth_seen;\n"
        "int main(void)\n"
        "{\n"
        "    // rbx, rbp and r12 to r15, and preserve_most's rax, rcx, rdx, rsi, rdi, r8, r9 and r10, but a result's.\n"
        "    const uint32_t most_set = 1u << 3 | 1u << 5 | 0xf000u | 1u << 0 | 1u << 1 | 1u << 2 | 1u << 6 | 1u << 7 | 1u << 8 | 1u << 9 | 1u << 10;\n"
        "    long r = 0;\n"
        "    int bad = 0;\n"
        "    bad |= check(\"most\", (void*)most, most_set & ~1u, 0, 16, 0, 40, 2, &r) || r != 42;\n"
        "    bad |= check(\"most_calls\", (void*)most_calls, most_set & ~1u, 0, 16, 0, 40, 2, &r) || r != 126;\n"
        "    bad |= check(\"most_void\", (void*)most_void, most_set, 0, 16, 0, 40, 2, &r);\n"
        "    bad |= check(\"most_pair\", (void*)most_pair, most_set & ~5u, 0, 16, 0, 40, 2, &r) || r != 42;\n"
        "    bad |= check(\"all\", (void*)all, most_set & ~1u, 0xffff, ALL_WIDTH, 0, 40, 2, &r) || r != 42;\n"
        "    bad |= check(\"all_calls\", (void*)all_calls, most_set & ~1u, 0xffff, ALL_WIDTH, 0, 40, 2, &r) || r != 126;\n"
        "    bad |= check(\"all_double\", (void*)all_double, most_set & ~1u, 0xfffe, ALL_WIDTH, 0, 1, 2, &r);\n"
        "    bad |= check(\"all_vla\", (void*)all_vla, most_set & ~1u, 0xffff, ALL_WIDTH, 0, 40, 2, &r) || r != 42;\n"
        "    bad |= check(\"all_aligned\", (void*)all_aligned, most_set & ~1u, 0xffff, ALL_WIDTH, 0, 40, 2, &r) || r != 42;\n"
        "    // A backtrace from a callee reaches through the frame: to probe, which has no unwind tables, and directly to main and on.\n"
        "    bad |= check(\"all_trace\", (void*)all_trace, most_set & ~1u, 0xffff, ALL_WIDTH, 0, 40, 2, &r) || r != 42 || depth_seen != 3;\n"
        "    depth_seen = 0;\n"
        "    bad |= all_trace(1, 2) != 3 || depth_seen < 5;\n"
        "    // The caller's registers through the frame's rules, every one the convention keeps.\n"
        "    unwound_kept = most_set;\n"
        "    bad |= check(\"all_observe\", (void*)all_observe, most_set, 0xffff, ALL_WIDTH, 0, 40, 2, &r) || recovered(\"all_observe\", most_set);\n"
        "    unwound_kept = most_set & ~1u;\n"
        "    bad |= check(\"most_observe\", (void*)most_observe, most_set & ~1u, 0, 16, 0, 40, 2, &r) || r != 42 || recovered(\"most_observe\", most_set & ~1u);\n"
        "    bad |= most_var(3, 10L, 20L, 12L) != 42;\n"
        "    // no_caller_saved_registers' every register: preserve_most's general ones and r11, and every vector and mask one.\n"
        "    const uint32_t every_set = most_set | 1u << 11;\n"
        "    bad |= check(\"every\", (void*)every, every_set & ~1u, EVERY_VECTORS, EVERY_WIDTH, EVERY_MASKS, 40, 2, &r) || r != 42;\n"
        "    bad |= check(\"every_calls\", (void*)every_calls, every_set & ~1u, EVERY_VECTORS, EVERY_WIDTH, EVERY_MASKS, 40, 2, &r) || r != 126;\n"
        "    bad |= check(\"every_void\", (void*)every_void, every_set, EVERY_VECTORS, EVERY_WIDTH, EVERY_MASKS, 40, 2, &r);\n"
        "    bad |= check(\"every_double\", (void*)every_double, every_set & ~1u, EVERY_VECTORS & ~1u, EVERY_WIDTH, EVERY_MASKS, 1, 2, &r);\n"
        "    bad |= check(\"every_aligned\", (void*)every_aligned, every_set & ~1u, EVERY_VECTORS, EVERY_WIDTH, EVERY_MASKS, 40, 2, &r) || r != 42;\n"
        "    unwound_kept = every_set;\n"
        "    bad |= check(\"every_observe\", (void*)every_observe, every_set, EVERY_VECTORS, EVERY_WIDTH, EVERY_MASKS, 40, 2, &r) || recovered(\"every_observe\", every_set);\n"
        "    bad |= every_var(3, 10L, 20L, 12L) != 42 || every_double(2.0, 3.0) != 7.0;\n"
        "    // cdecl before the bit, which is still its type (#1639's review).\n"
        "    bad |= check(\"every_cdecl\", (void*)every_cdecl, every_set & ~1u, EVERY_VECTORS, EVERY_WIDTH, EVERY_MASKS, 40, 2, &r) || r != 42;\n"
        "    Pair p = most_pair(5, 3);\n"
        "    bad |= p.a != 8 || p.b != 2 || all_double(2.0, 3.0) != 7.0;\n"
        "    printf(bad ? \"FAILED\\n\" : \"ok\\n\");\n"
        "    return bad;\n"
        "}\n";
    static const char callees[] =
        "#include <execinfo.h>\n"
        "#include <stdarg.h>\n"
        "int printf(const char*, ...);\n"
        "typedef struct Pair { long a, b; } Pair;\n"
        "int depth_seen;\n"
        "__attribute__((noinline)) static void trace(void) { void* frames[32]; depth_seen = backtrace(frames, 32); }\n"
        "static long helper(long a, long b) { return (a + b) * 3; }\n"
        "__attribute__((preserve_most)) long most(long a, long b) { return a + b; }\n"
        "__attribute__((preserve_most)) long most_calls(long a, long b) { volatile double d = 1.5; d *= a; printf(\"\"); return helper(a, b) + (long)(d - d); }\n"
        "__attribute__((preserve_most)) void most_void(long a, long b) { volatile long x = a * b; (void)x; }\n"
        "__attribute__((preserve_most)) Pair most_pair(long a, long b) { Pair p = { a + b, a - b }; return p; }\n"
        "__attribute__((preserve_most)) long most_var(long n, ...) { va_list ap; va_start(ap, n); long s = 0; for (long i = 0; i < n; i++) s += va_arg(ap, long); va_end(ap); return s; }\n"
        "__attribute__((preserve_all)) long all(long a, long b) { double x = (double)a * 0.5; return (long)(x * 2) + b; }\n"
        "__attribute__((preserve_all)) long all_calls(long a, long b) { char buf[64]; __builtin_snprintf(buf, sizeof buf, \"%f %ld\", 1.5, a); return helper(a, b); }\n"
        "__attribute__((preserve_all)) double all_double(double a, double b) { return a * b + 1.0; }\n"
        "__attribute__((preserve_all)) long all_vla(long a, long b) { char buf[a]; __builtin_memset(buf, 1, a); return buf[a - 1] + b + 39; }\n"
        "__attribute__((preserve_all)) long all_aligned(long a, long b) { _Alignas(64) char x[64]; __builtin_memset(x, (int)a, 64); printf(\"\"); return x[63] + b; }\n"
        "__attribute__((preserve_all)) long all_trace(long a, long b) { trace(); return a + b; }\n"
        "void observe(void);\n"
        "__attribute__((preserve_all)) void all_observe(long a, long b) { (void)a; (void)b; observe(); }\n"
        "__attribute__((preserve_most)) long most_observe(long a, long b) { observe(); return a + b; }\n"
        "__attribute__((no_caller_saved_registers)) long every(long a, long b) { return a + b; }\n"
        "__attribute__((no_caller_saved_registers)) long every_calls(long a, long b) { char buf[64]; __builtin_snprintf(buf, sizeof buf, \"%f %ld\", 1.5, a); return helper(a, b); }\n"
        "__attribute__((no_caller_saved_registers)) void every_void(long a, long b) { volatile long x = a * b; (void)x; }\n"
        "__attribute__((no_caller_saved_registers, sysv_abi)) double every_double(double a, double b) { return a * b + 1.0; }\n"
        "__attribute__((no_caller_saved_registers)) long every_aligned(long a, long b) { _Alignas(64) char x[64]; __builtin_memset(x, (int)a, 64); printf(\"\"); return x[63] + b; }\n"
        "__attribute__((no_caller_saved_registers)) long every_var(long n, ...) { va_list ap; va_start(ap, n); long s = 0; for (long i = 0; i < n; i++) s += va_arg(ap, long); va_end(ap); return s; }\n"
        "__attribute__((no_caller_saved_registers)) void every_observe(long a, long b) { (void)a; (void)b; observe(); }\n"
        "__attribute__((cdecl, no_caller_saved_registers)) long every_cdecl(long a, long b) { char buf[64]; __builtin_snprintf(buf, sizeof buf, \"%f\", 2.5); return a + b; }\n";
    static const struct
    {
        const char* name;
        const char* source;
        const char* reason;
    } refusals[] = {
        { "musttail", "__attribute__((preserve_all)) long g(long a);\n__attribute__((preserve_all)) long f(long a) { __attribute__((musttail)) return g(a); }\n",
          "unsupported: musttail calls in a function of the preserve_most or preserve_all calling convention" },
        { "__builtin_eh_return", "__attribute__((preserve_most)) void f(long o, void *h) { __builtin_eh_return(o, h); }\n",
          "unsupported: __builtin_eh_return in a function of the preserve_most or preserve_all calling convention" },
        { "__builtin_setjmp", "void *buf[5];\n__attribute__((preserve_all)) int f(void) { return __builtin_setjmp(buf); }\n",
          "unsupported: __builtin_setjmp in a function of the preserve_most or preserve_all calling convention" },
        { "no_caller_saved_registers' __builtin_eh_return", "__attribute__((no_caller_saved_registers)) void f(long o, void *h) { __builtin_eh_return(o, h); }\n",
          "unsupported: __builtin_eh_return in a function with no_caller_saved_registers" },
        { "no_caller_saved_registers' __builtin_setjmp", "void *buf[5];\n__attribute__((no_caller_saved_registers)) int f(void) { return __builtin_setjmp(buf); }\n",
          "unsupported: __builtin_setjmp in a function with no_caller_saved_registers" },
    };
    char harness_path[512];
    char callee_path[512];
    char object[512];
    char linked[512];
    snprintf(harness_path, sizeof(harness_path), "%s/preserving-harness.c", directory);
    snprintf(callee_path, sizeof(callee_path), "%s/preserving-callees.c", directory);
    snprintf(object, sizeof(object), "%s/preserving.o", directory);
    snprintf(linked, sizeof(linked), "%s/preserving", directory);
    const bool written = parse_test_write(harness_path, harness, sizeof(harness) - 1) && parse_test_write(callee_path, callees, sizeof(callees) - 1);
    check_test_expect(test, written, "preserving frames: fixtures written");
    static const char* const configurations[][4] = {
        { "-march=x86-64", NULL, NULL, NULL },
        { "-march=x86-64", "-fstack-clash-protection", "-fno-omit-frame-pointer", "-g" },
        { "-march=x86-64-v3", NULL, NULL, NULL },
        { "-march=x86-64-v4", NULL, NULL, NULL },
    };
    for (uint32_t c = 0; written && c < BUSTER_ARRAY_LENGTH(configurations); c += 1)
    {
        // x86-64-v3's code runs where the host has AVX2, and x86-64-v4's (no_caller_saved_registers' ZMMs and masks) where it
        // has AVX-512's F, BW, DQ and VL.
        const bool runs = c < 2 || (c == 2 && __builtin_cpu_supports("avx2")) ||
                          (c == 3 && __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512dq") &&
                           __builtin_cpu_supports("avx512vl"));
        for (uint32_t p = 0; runs && p < 2; p += 1)
        {
            char* text = NULL;
            char* arguments[] = { (char*)executable, "--verify", "-std=gnu17", "-w", (char*)configurations[c][0], "-c", callee_path, "-o", object, NULL, NULL, NULL, NULL, NULL };
            uint32_t at = 9;
            for (uint32_t k = 1; k < 4 && configurations[c][k] != NULL; k += 1)
            {
                arguments[at++] = (char*)configurations[c][k];
            }
            arguments[at] = p == 1 ? "--scalar" : NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            char description[2048];
            snprintf(description, sizeof(description), "preserving frames: %s%s by the %s path compile (status %d: %s)", configurations[c][0],
                     configurations[c][1] != NULL ? " with probes, frame pointers and -g" : "", p == 1 ? "scalar" : "vector", status, text != NULL ? text : "");
            check_test_expect(test, status == 0, description);
            free(text);
            text = NULL;
            char* link[] = { (char*)clang, (char*)configurations[c][0], "-O0", "-w", harness_path, object, "-o", linked, NULL };
            const bool built = status == 0 && check_test_driver_spawn(link, &text) == 0;
            free(text);
            char ran[4096] = { 0 };
            const bool running = built && check_test_driver_capture(linked, ran, sizeof(ran));
            snprintf(description, sizeof(description), "preserving frames: %s%s by the %s path keep their registers: %s", configurations[c][0],
                     configurations[c][1] != NULL ? " with probes, frame pointers and -g" : "", p == 1 ? "scalar" : "vector", running ? ran : "(nothing ran)");
            check_test_expect(test, running && strcmp(ran, "ok\n") == 0, description);
            unlink(object);
            unlink(linked);
        }
    }
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(refusals); r += 1)
    {
        const bool made = parse_test_write(callee_path, refusals[r].source, strlen(refusals[r].source));
        char* text = NULL;
        char* reference[] = { (char*)clang, "-std=gnu17", "-O0", "-c", callee_path, "-o", object, NULL };
        const int oracle = made ? check_test_driver_spawn(reference, &text) : -1;
        free(text);
        for (uint32_t p = 0; made && p < 2; p += 1)
        {
            text = NULL;
            char* arguments[] = { (char*)executable, "-std=gnu17", "-c", callee_path, "-o", object, p == 1 ? "--scalar" : NULL, NULL };
            const int status = check_test_driver_spawn(arguments, &text);
            char description[1024];
            snprintf(description, sizeof(description), "preserving frames: %s by the %s path is refused by name where clang compiles it (status %d, clang %d: %s)",
                     refusals[r].name, p == 1 ? "scalar" : "vector", status, oracle, text != NULL ? text : "");
            check_test_expect(test, oracle == 0 && status == 1 && text != NULL && strstr(text, refusals[r].reason) != NULL, description);
            free(text);
        }
    }
    unlink(harness_path);
    unlink(callee_path);
    unlink(object);
}

// Tier 0's interrupt service routines (#147, Tier0Function.interrupt), by each path, run in user mode as the processor would:
// a probe clang builds loads every register, zmm0 to zmm31 and k0 to k7 with AVX-512 (whole YMMs with AVX, XMMs
// without), sets the direction flag, pushes an interrupt's frame (ss, rsp, rflags, cs and its own resume address, and an
// error code for the second kind) and jumps to the handler, whose iretq returns to it at the same privilege level; it then
// reads every register back. Every register must come back, rsp too, the direction flag set again as the frame had it;
// and the handlers record what they saw: the frame's rip, rsp and cs, the error code, and a cleared direction flag. The
// handlers make calls (whose callees keep nothing for them), use vectors and floating point, and realign their frame. Both
// paths' code, with --verify, on x86-64 (with probes, frame pointers and -g too), x86-64-v3 where the host has AVX2 and
// x86-64-v4 where it has AVX-512.
static void check_test_driver_interrupt_handlers(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char harness[] =
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "typedef struct State { uint64_t in[16]; uint64_t out[16]; uint8_t vin[32][64]; uint8_t vout[32][64]; uint64_t kin[8]; uint64_t kout[8]; void* fn; uint64_t sp; uint64_t flags; } State;\n"
        "_Static_assert(__builtin_offsetof(State, vin) == 256 && __builtin_offsetof(State, vout) == 2304 && __builtin_offsetof(State, kin) == 4352 && __builtin_offsetof(State, kout) == 4416 && __builtin_offsetof(State, fn) == 4480 && __builtin_offsetof(State, sp) == 4488 && __builtin_offsetof(State, flags) == 4496, \"layout\");\n"
        "#define LOW \"0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\"\n"
        "#define ALL LOW \",16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\"\n"
        "#if defined(__AVX512F__)\n"
        "#define LOAD \".irp n,\" ALL \"\\nvmovdqu64 256+64*\\\\n(%rax), %zmm\\\\n\\n.endr\\n.irp n,0,1,2,3,4,5,6,7\\nkmovq 4352+8*\\\\n(%rax), %k\\\\n\\n.endr\\n\"\n"
        "#define STORE \".irp n,\" ALL \"\\nvmovdqu64 %zmm\\\\n, 2304+64*\\\\n(%rax)\\n.endr\\n.irp n,0,1,2,3,4,5,6,7\\nkmovq %k\\\\n, 4416+8*\\\\n(%rax)\\n.endr\\nvzeroupper\\n\"\n"
        "#define WIDTH 64\n"
        "#define VECTORS 0xffffffffu\n"
        "#define MASKS 0xffu\n"
        "#elif defined(__AVX__)\n"
        "#define LOAD \".irp n,\" LOW \"\\nvmovdqu 256+64*\\\\n(%rax), %ymm\\\\n\\n.endr\\n\"\n"
        "#define STORE \".irp n,\" LOW \"\\nvmovdqu %ymm\\\\n, 2304+64*\\\\n(%rax)\\n.endr\\nvzeroupper\\n\"\n"
        "#define WIDTH 32\n"
        "#define VECTORS 0xffffu\n"
        "#define MASKS 0u\n"
        "#else\n"
        "#define LOAD \".irp n,\" LOW \"\\nmovdqu 256+64*\\\\n(%rax), %xmm\\\\n\\n.endr\\n\"\n"
        "#define STORE \".irp n,\" LOW \"\\nmovdqu %xmm\\\\n, 2304+64*\\\\n(%rax)\\n.endr\\n\"\n"
        "#define WIDTH 16\n"
        "#define VECTORS 0xffffu\n"
        "#define MASKS 0u\n"
        "#endif\n"
        "// iprobe(state) and iprobe_error(state): every register from state (but rsp; r11 the handler, state->fn), the\n"
        "// direction flag set, then an interrupt's frame (and the error code 0x5a5a) and a jump to the handler, whose iretq comes\n"
        "// back to the resume label after the call that pushed it; then every register into state, and rflags.\n"
        "#define PROBE(name, error) \\\n"
        "    \".text\\n.globl \" name \"\\n.type \" name \",@function\\n\" name \":\\n\" \\\n"
        "    \"push %rbx\\npush %rbp\\npush %r12\\npush %r13\\npush %r14\\npush %r15\\npush %rdi\\nmov %rdi, %rax\\n\" LOAD \\\n"
        "    \"mov %rsp, 4488(%rax)\\nmov 8(%rax), %rcx\\nmov 16(%rax), %rdx\\nmov 24(%rax), %rbx\\nmov 40(%rax), %rbp\\nmov 48(%rax), %rsi\\nmov 56(%rax), %rdi\\n\" \\\n"
        "    \"mov 64(%rax), %r8\\nmov 72(%rax), %r9\\nmov 80(%rax), %r10\\nmov 96(%rax), %r12\\nmov 104(%rax), %r13\\nmov 112(%rax), %r14\\nmov 120(%rax), %r15\\n\" \\\n"
        "    \"mov 4480(%rax), %r11\\nmov (%rax), %rax\\nstd\\n\" \\\n"
        "    \"pushq $0\\nmov %ss, (%rsp)\\npush %rsp\\naddq $8, (%rsp)\\npushfq\\npushq $0\\nmov %cs, (%rsp)\\ncall 2f\\n\" \\\n"
        "    \"push %rax\\npushfq\\ncld\\nmov 16(%rsp), %rax\\npop 4496(%rax)\\npop 128(%rax)\\n\" \\\n"
        "    \"mov %rcx, 136(%rax)\\nmov %rdx, 144(%rax)\\nmov %rbx, 152(%rax)\\nmov %rsp, 160(%rax)\\nmov %rbp, 168(%rax)\\nmov %rsi, 176(%rax)\\nmov %rdi, 184(%rax)\\n\" \\\n"
        "    \"mov %r8, 192(%rax)\\nmov %r9, 200(%rax)\\nmov %r10, 208(%rax)\\nmov %r11, 216(%rax)\\nmov %r12, 224(%rax)\\nmov %r13, 232(%rax)\\nmov %r14, 240(%rax)\\nmov %r15, 248(%rax)\\n\" \\\n"
        "    STORE \"add $8, %rsp\\npop %r15\\npop %r14\\npop %r13\\npop %r12\\npop %rbp\\npop %rbx\\nret\\n2:\\n\" error \"jmp *%r11\\n\"\n"
        "void iprobe(State* s);\n"
        "void iprobe_error(State* s);\n"
        "__asm__(PROBE(\"iprobe\", \"\") PROBE(\"iprobe_error\", \"pushq $0x5a5a\\n\"));\n"
        "static const char* names[16] = { \"rax\", \"rcx\", \"rdx\", \"rbx\", \"rsp\", \"rbp\", \"rsi\", \"rdi\", \"r8\", \"r9\", \"r10\", \"r11\", \"r12\", \"r13\", \"r14\", \"r15\" };\n"
        "typedef struct Seen { uint64_t rip, cs, rsp, error, flags; int calls; } Seen;\n"
        "extern volatile Seen seen;\n"
        "void handler(void*);\n"
        "void handler_error(void*, unsigned long);\n"
        "void handler_calls(void*);\n"
        "void handler_aligned(void*);\n"
        "void handler_vla(void*, unsigned long);\n"
        "static int raise_one(const char* what, void* fn, int error, int expected)\n"
        "{\n"
        "    State s;\n"
        "    memset(&s, 0, sizeof(s));\n"
        "    for (uint32_t r = 0; r < 16; r += 1) { s.in[r] = 0x1111111111111111ull * (r + 1) ^ 0x0123456789abcdefull; }\n"
        "    for (uint32_t v = 0; v < 32; v += 1) { for (uint32_t k = 0; k < 64; k += 1) { s.vin[v][k] = (uint8_t)(v * 37 + k * 11 + 5); } }\n"
        "    for (uint32_t k = 0; k < 8; k += 1) { s.kin[k] = 0x0f1e2d3c4b5a6978ull * (k + 3); }\n"
        "    s.fn = fn;\n"
        "    memset((void*)&seen, 0, sizeof(seen));\n"
        "    if (error) { iprobe_error(&s); } else { iprobe(&s); }\n"
        "    int bad = s.out[4] != s.sp || (s.flags >> 10 & 1) != 1 || seen.calls != 1 || seen.rsp != s.sp || seen.cs == 0 || (seen.flags >> 10 & 1) != 0 ||\n"
        "              (error && seen.error != 0x5a5a) || seen.rip == 0;\n"
        "    if (bad) { printf(\"%s: frame or flags (rsp %llx/%llx, seen rsp %llx, calls %d, error %llx, flags %llx/%llx)\\n\", what, (unsigned long long)s.out[4], (unsigned long long)s.sp, (unsigned long long)seen.rsp, seen.calls, (unsigned long long)seen.error, (unsigned long long)s.flags, (unsigned long long)seen.flags); }\n"
        "    for (uint32_t r = 0; r < 16; r += 1)\n"
        "    {\n"
        "        const uint64_t given = r == 11 ? (uint64_t)fn : s.in[r];\n"
        "        if (r != 4 && s.out[r] != given) { printf(\"%s: %s changed\\n\", what, names[r]); bad = 1; }\n"
        "    }\n"
        "    for (uint32_t v = 0; v < 32; v += 1)\n"
        "    {\n"
        "        if ((VECTORS >> v & 1) && memcmp(s.vin[v], s.vout[v], WIDTH) != 0) { printf(\"%s: vector register %u changed\\n\", what, v); bad = 1; }\n"
        "    }\n"
        "    for (uint32_t k = 0; k < 8; k += 1)\n"
        "    {\n"
        "        if ((MASKS >> k & 1) && s.kout[k] != s.kin[k]) { printf(\"%s: k%u changed\\n\", what, k); bad = 1; }\n"
        "    }\n"
        "    if (expected >= 0 && (int)seen.rip != expected) { printf(\"%s: computed %d, not %d\\n\", what, (int)seen.rip, expected); bad = 1; }\n"
        "    return bad;\n"
        "}\n"
        "int main(void)\n"
        "{\n"
        "    int bad = 0;\n"
        "    bad |= raise_one(\"handler\", (void*)handler, 0, -1);\n"
        "    bad |= raise_one(\"handler_error\", (void*)handler_error, 1, -1);\n"
        "    bad |= raise_one(\"handler_calls\", (void*)handler_calls, 0, 5);\n"
        "    bad |= raise_one(\"handler_aligned\", (void*)handler_aligned, 0, 3);\n"
        "    bad |= raise_one(\"handler_vla\", (void*)handler_vla, 1, 0x5a);\n"
        "    printf(bad ? \"FAILED\\n\" : \"ok\\n\");\n"
        "    return bad;\n"
        "}\n";
    static const char handlers[] =
        "#include <stdint.h>\n"
        "typedef struct Frame { uint64_t rip, cs, flags, rsp, ss; } Frame;\n"
        "typedef struct Seen { uint64_t rip, cs, rsp, error, flags; int calls; } Seen;\n"
        "volatile Seen seen;\n"
        "int snprintf(char*, unsigned long, const char*, ...);\n"
        "static void record(Frame* f, unsigned long error) { seen.flags = __builtin_ia32_readeflags_u64(); seen.rip = f->rip; seen.cs = f->cs; seen.rsp = f->rsp; seen.error = error; seen.calls += 1; }\n"
        "__attribute__((interrupt)) void handler(Frame* f) { record(f, 0); }\n"
        "__attribute__((interrupt)) void handler_error(Frame* f, unsigned long e) { record(f, e); }\n"
        "__attribute__((interrupt)) void handler_calls(Frame* f) { record(f, 0); char buf[32]; snprintf(buf, sizeof buf, \"%.1f\", 2.5); double d = 2.0; seen.rip = (uint64_t)(d * (buf[0] - '0') + 1.0); }\n"
        "__attribute__((interrupt)) void handler_aligned(Frame* f) { record(f, 0); _Alignas(64) char x[64]; __builtin_memset(x, 3, 64); seen.rip = (uint64_t)x[63]; }\n"
        "__attribute__((interrupt)) void handler_vla(Frame* f, unsigned long e) { record(f, e); char x[e & 0xff]; __builtin_memset(x, (int)(e & 0xff), sizeof x); seen.rip = (uint64_t)(unsigned char)x[(e & 0xff) - 1]; }\n";
    char harness_path[512];
    char handler_path[512];
    char object[512];
    char linked[512];
    snprintf(harness_path, sizeof(harness_path), "%s/interrupt-harness.c", directory);
    snprintf(handler_path, sizeof(handler_path), "%s/interrupt-handlers.c", directory);
    snprintf(object, sizeof(object), "%s/interrupt.o", directory);
    snprintf(linked, sizeof(linked), "%s/interrupt", directory);
    const bool written = parse_test_write(harness_path, harness, sizeof(harness) - 1) && parse_test_write(handler_path, handlers, sizeof(handlers) - 1);
    check_test_expect(test, written, "interrupt handlers: fixtures written");
    static const char* const configurations[][4] = {
        { "-march=x86-64", NULL, NULL, NULL },
        { "-march=x86-64", "-fstack-clash-protection", "-fno-omit-frame-pointer", "-g" },
        { "-march=x86-64-v3", NULL, NULL, NULL },
        { "-march=x86-64-v4", NULL, NULL, NULL },
    };
    for (uint32_t c = 0; written && c < BUSTER_ARRAY_LENGTH(configurations); c += 1)
    {
        const bool runs = c < 2 || (c == 2 && __builtin_cpu_supports("avx2")) ||
                          (c == 3 && __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512dq") &&
                           __builtin_cpu_supports("avx512vl"));
        for (uint32_t p = 0; runs && p < 2; p += 1)
        {
            char* text = NULL;
            char* arguments[] = { (char*)executable, "--verify", "-std=gnu17", "-w", (char*)configurations[c][0], "-c", handler_path, "-o", object, NULL, NULL, NULL, NULL, NULL };
            uint32_t at = 9;
            for (uint32_t k = 1; k < 4 && configurations[c][k] != NULL; k += 1)
            {
                arguments[at++] = (char*)configurations[c][k];
            }
            arguments[at] = p == 1 ? "--scalar" : NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            char description[2048];
            snprintf(description, sizeof(description), "interrupt handlers: %s%s by the %s path compile (status %d: %s)", configurations[c][0],
                     configurations[c][1] != NULL ? " with probes, frame pointers and -g" : "", p == 1 ? "scalar" : "vector", status, text != NULL ? text : "");
            check_test_expect(test, status == 0, description);
            free(text);
            text = NULL;
            char* link[] = { (char*)clang, (char*)configurations[c][0], "-O0", "-w", harness_path, object, "-o", linked, NULL };
            const bool built = status == 0 && check_test_driver_spawn(link, &text) == 0;
            free(text);
            char ran[4096] = { 0 };
            const bool running = built && check_test_driver_capture(linked, ran, sizeof(ran));
            snprintf(description, sizeof(description), "interrupt handlers: %s%s by the %s path return to the interrupted code with every register: %s",
                     configurations[c][0], configurations[c][1] != NULL ? " with probes, frame pointers and -g" : "", p == 1 ? "scalar" : "vector",
                     running ? ran : "(nothing ran)");
            check_test_expect(test, running && strcmp(ran, "ok\n") == 0, description);
            unlink(object);
            unlink(linked);
        }
    }
    unlink(harness_path);
    unlink(handler_path);
}
