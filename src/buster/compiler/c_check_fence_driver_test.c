// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1059's concurrent controls through the C driver: a store-buffering litmus (each of two threads stores 1 to its own
// variable, fences, and loads the other's) whose outcome of both loads reading 0 x86-64's store buffers allow without a
// fence that orders a store before a later load, and a seq_cst thread fence forbids. The program runs it twice, with a
// signal fence (a compiler barrier alone: the control, which may show the outcome) and with a thread fence of seq_cst
// (the typed stream's FENCE, Tier 0's or Tier 1's mfence: which must never show it), and prints how many of its
// iterations showed the outcome. Each build is checked: by both paths, by Tier 0 and by --tier1 (every function Tier
// 1's), its four fences taken directly (--helper-report), its fenced threads' objects with one mfence each and the
// control's with none. A sequential interpreter shows no store buffer: the native runs are the evidence. A process
// with one CPU prints that it skipped the runs. Include after c_check_tier1_driver_test.c, whose decoder of a
// function's object and Tier 1 report readers this test takes.

static const char check_test_fence_litmus_source[] =
    "#define _GNU_SOURCE\n"
    "#include <pthread.h>\n"
    "#include <sched.h>\n"
    "#include <stdio.h>\n"
    "#include <unistd.h>\n"
    "#define ITERATIONS 100000\n"
    "static volatile int x, y, r0, r1;\n"
    "static int go, done0, done1;\n"
    // A spin that yields past its first spins, so that the threads go on where they share CPUs with others.
    "static void wait_for(int* flag, int value)\n"
    "{\n"
    "    for (int spins = 0; __atomic_load_n(flag, __ATOMIC_ACQUIRE) < value; spins++)\n"
    "    {\n"
    "        if (spins > 4096) sched_yield();\n"
    "    }\n"
    "}\n"
    "void* fenced0(void* unused)\n"
    "{\n"
    "    for (int i = 1; i <= ITERATIONS; i++) { wait_for(&go, i); x = 1; __atomic_thread_fence(__ATOMIC_SEQ_CST); r0 = y; __atomic_store_n(&done0, i, __ATOMIC_RELEASE); }\n"
    "    return unused;\n"
    "}\n"
    "void* fenced1(void* unused)\n"
    "{\n"
    "    for (int i = 1; i <= ITERATIONS; i++) { wait_for(&go, i); y = 1; __atomic_thread_fence(__ATOMIC_SEQ_CST); r1 = x; __atomic_store_n(&done1, i, __ATOMIC_RELEASE); }\n"
    "    return unused;\n"
    "}\n"
    "void* plain0(void* unused)\n"
    "{\n"
    "    for (int i = 1; i <= ITERATIONS; i++) { wait_for(&go, i); x = 1; __atomic_signal_fence(__ATOMIC_SEQ_CST); r0 = y; __atomic_store_n(&done0, i, __ATOMIC_RELEASE); }\n"
    "    return unused;\n"
    "}\n"
    "void* plain1(void* unused)\n"
    "{\n"
    "    for (int i = 1; i <= ITERATIONS; i++) { wait_for(&go, i); y = 1; __atomic_signal_fence(__ATOMIC_SEQ_CST); r1 = x; __atomic_store_n(&done1, i, __ATOMIC_RELEASE); }\n"
    "    return unused;\n"
    "}\n"
    "static int run(void* (*first)(void*), void* (*second)(void*))\n"
    "{\n"
    "    go = 0;\n"
    "    done0 = 0;\n"
    "    done1 = 0;\n"
    "    pthread_t a, b;\n"
    "    if (pthread_create(&a, 0, first, 0) != 0 || pthread_create(&b, 0, second, 0) != 0) return -1;\n"
    "    int both = 0;\n"
    "    for (int i = 1; i <= ITERATIONS; i++)\n"
    "    {\n"
    "        x = 0;\n"
    "        y = 0;\n"
    "        __atomic_store_n(&go, i, __ATOMIC_RELEASE);\n"
    "        wait_for(&done0, i);\n"
    "        wait_for(&done1, i);\n"
    "        both += r0 == 0 && r1 == 0;\n"
    "    }\n"
    "    pthread_join(a, 0);\n"
    "    pthread_join(b, 0);\n"
    "    return both;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    cpu_set_t set;\n"
    "    alarm(120);\n"
    "    if (sched_getaffinity(0, sizeof(set), &set) != 0 || CPU_COUNT(&set) < 2) { printf(\"skipped\\n\"); return 0; }\n"
    "    const int control = run(plain0, plain1);\n"
    "    const int fenced = run(fenced0, fenced1);\n"
    "    printf(\"fenced=%d control=%d iterations=%d\\n\", fenced, control, ITERATIONS);\n"
    "    return fenced < 0 || control < 0;\n"
    "}\n";

// The mfence records of function `name` in object `path`, or UINT32_MAX where it doesn't decode.
static uint32_t check_test_fence_records(const char* path, const char* name)
{
    enum
    {
        CHECK_TEST_FENCE_RECORDS = 1024,
    };
    X86Decoded* records = calloc(CHECK_TEST_FENCE_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_FENCE_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_FENCE_RECORDS, sizeof(uint32_t));
    uint32_t relocations = 0;
    const uint32_t count = records != NULL && offsets != NULL && types != NULL ? check_test_tier1_function(path, name, records, CHECK_TEST_FENCE_RECORDS, offsets, types, &relocations) : 0;
    uint32_t fences = count != 0 ? 0 : UINT32_MAX;
    for (uint32_t i = 0; i < count; i += 1)
    {
        fences += (records[i].instruction.form & BUSTER_X86_FORM_NUMBER) == X86_FORM_MFENCE;
    }
    free(records);
    free(offsets);
    free(types);
    return fences;
}

static void check_test_driver_fences(CheckTest* test, const char* executable, const char* directory)
{
    char source[512];
    snprintf(source, sizeof(source), "%s/fence-litmus.c", directory);
    const bool written = parse_test_write(source, check_test_fence_litmus_source, sizeof(check_test_fence_litmus_source) - 1);
    check_test_expect(test, written, "fences: the litmus's source is written");
    const bool ready = written && x86_disassembler_initialize();
    static const char* const names[4] = { "fenced0", "fenced1", "plain0", "plain1" };
    for (uint32_t build = 0; ready && build < 4; build += 1)
    {
        const bool tier1 = build >= 2;
        const bool scalar = (build & 1) != 0;
        const char* const way = tier1 ? scalar ? "Tier 1, scalar path" : "Tier 1, vector path" : scalar ? "Tier 0, scalar path" : "Tier 0, vector path";
        char output[512];
        char object[512];
        snprintf(output, sizeof(output), "%s/fence-litmus-%u", directory, build);
        snprintf(object, sizeof(object), "%s/fence-litmus-%u.o", directory, build);
        bool built = true;
        for (uint32_t kind = 0; kind < 2; kind += 1)
        {
            char* arguments[16] = { (char*)executable, "--helper-report", "--verify" };
            uint32_t count = 3;
            if (tier1)
            {
                arguments[count++] = "--tier1";
                arguments[count++] = "--tier1-refusals";
            }
            if (scalar)
            {
                arguments[count++] = "--scalar";
            }
            if (kind == 1)
            {
                arguments[count++] = "-c";
            }
            arguments[count++] = "-o";
            arguments[count++] = kind == 0 ? output : object;
            arguments[count++] = source;
            arguments[count] = NULL;
            char* text = NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            // Its four fences are the typed stream's, none a helper's call; its atomic accesses keep theirs.
            const bool reported = text != NULL && strstr(text, "helpers.fence\tdirect=4\thelper=0\tplanned=0\n") != NULL &&
                                  strstr(text, "helpers\tunits=1\trefused=0\tdirect=4\t") != NULL;
            const bool taken = !tier1 || check_test_tier1_taken(text, names, BUSTER_ARRAY_LENGTH(names));
            if (status != 0 && text != NULL)
            {
                fprintf(stderr, "%s", text);
            }
            free(text);
            built &= status == 0 && reported && taken;
        }
        char message[256];
        snprintf(message, sizeof(message), "fences (%s): the litmus builds, its four fences taken directly%s", way, tier1 ? " and its threads Tier 1's" : "");
        check_test_expect(test, built, message);
        // One mfence in each fenced thread, between its store and its load; none in the control's.
        const uint32_t fences[4] = {
            built ? check_test_fence_records(object, "fenced0") : UINT32_MAX,
            built ? check_test_fence_records(object, "fenced1") : UINT32_MAX,
            built ? check_test_fence_records(object, "plain0") : UINT32_MAX,
            built ? check_test_fence_records(object, "plain1") : UINT32_MAX,
        };
        snprintf(message, sizeof(message), "fences (%s): the fenced threads have one mfence each and the control's none (%u, %u, %u, %u)", way, fences[0], fences[1], fences[2],
                 fences[3]);
        check_test_expect(test, fences[0] == 1 && fences[1] == 1 && fences[2] == 0 && fences[3] == 0, message);
        char captured[256] = { 0 };
        const bool ran = built && check_test_driver_capture(output, captured, sizeof(captured));
        int fenced = -1;
        int control = -1;
        int iterations = 0;
        const bool skipped = ran && strcmp(captured, "skipped\n") == 0;
        const bool read = ran && sscanf(captured, "fenced=%d control=%d iterations=%d", &fenced, &control, &iterations) == 3;
        snprintf(message, sizeof(message), "fences (%s): the fenced litmus never shows both loads reading 0 (%d of %d iterations)", way, fenced, iterations);
        check_test_expect(test, skipped || (read && fenced == 0 && control >= 0 && iterations > 0), message);
        if (read)
        {
            // The control's count is the evidence that the machine shows the reordering the fence forbids; it may be 0
            // on a run where the threads never overlapped, which leaves the fenced run's 0 weaker, not wrong.
            printf("fences (%s): without a thread fence, %d of %d iterations showed both loads reading 0; with one, %d\n", way, control, iterations, fenced);
        }
        else if (skipped)
        {
            printf("fences (%s): the litmus skipped its runs: the process may run on one CPU\n", way);
        }
        unlink(output);
        unlink(object);
    }
    unlink(source);
}
