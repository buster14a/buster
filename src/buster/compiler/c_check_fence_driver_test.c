// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1059's concurrent controls through the C driver: a store-buffering litmus (each of two threads stores 1 to its own
// variable, orders, and loads the other's) whose outcome of both loads reading 0 x86-64's store buffers allow without a
// fence that orders a store before a later load, and a seq_cst thread fence, or a seq_cst store before a seq_cst load,
// forbids. The program runs it four times: with a signal fence (a compiler barrier alone: a control, which may show the
// outcome), with a thread fence of seq_cst (the typed stream's FENCE, Tier 0's or Tier 1's mfence), with relaxed atomic
// accesses (the other control) and with seq_cst ones (the typed stream's marked SET and LOAD, and the store's FENCE),
// the second and the fourth of which must never show it, and prints how many of its iterations showed the outcome. Each
// build is checked: by both paths, by Tier 0 and by --tier1 (every function Tier 1's), its fences and atomic accesses
// taken directly (--helper-report), its fenced and seq_cst threads' objects with one mfence each and the others with
// none. A sequential interpreter shows no store buffer: the native runs are the evidence. A process with one CPU prints
// that it skipped the runs. A concurrent counter litmus of the direct read/modify/writes (check_test_driver_counters),
// built and checked the same four ways. And --helper-report's counts of a unit with every kind of helper call
// (check_test_driver_helper_report). Include after c_check_tier1_driver_test.c, whose decoder of a function's object
// and Tier 1 report readers this test takes.

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
    // #1059's direct atomic accesses: a seq_cst store and a seq_cst load, whose store's fence forbids the outcome, and
    // relaxed ones, which allow it.
    "void* sequential0(void* unused)\n"
    "{\n"
    "    for (int i = 1; i <= ITERATIONS; i++) { wait_for(&go, i); __atomic_store_n(&x, 1, __ATOMIC_SEQ_CST); r0 = __atomic_load_n(&y, __ATOMIC_SEQ_CST); __atomic_store_n(&done0, i, __ATOMIC_RELEASE); }\n"
    "    return unused;\n"
    "}\n"
    "void* sequential1(void* unused)\n"
    "{\n"
    "    for (int i = 1; i <= ITERATIONS; i++) { wait_for(&go, i); __atomic_store_n(&y, 1, __ATOMIC_SEQ_CST); r1 = __atomic_load_n(&x, __ATOMIC_SEQ_CST); __atomic_store_n(&done1, i, __ATOMIC_RELEASE); }\n"
    "    return unused;\n"
    "}\n"
    "void* relaxed0(void* unused)\n"
    "{\n"
    "    for (int i = 1; i <= ITERATIONS; i++) { wait_for(&go, i); __atomic_store_n(&x, 1, __ATOMIC_RELAXED); r0 = __atomic_load_n(&y, __ATOMIC_RELAXED); __atomic_store_n(&done0, i, __ATOMIC_RELEASE); }\n"
    "    return unused;\n"
    "}\n"
    "void* relaxed1(void* unused)\n"
    "{\n"
    "    for (int i = 1; i <= ITERATIONS; i++) { wait_for(&go, i); __atomic_store_n(&y, 1, __ATOMIC_RELAXED); r1 = __atomic_load_n(&x, __ATOMIC_RELAXED); __atomic_store_n(&done1, i, __ATOMIC_RELEASE); }\n"
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
    "    const int relaxed = run(relaxed0, relaxed1);\n"
    "    const int sequential = run(sequential0, sequential1);\n"
    "    printf(\"fenced=%d control=%d sequential=%d relaxed=%d iterations=%d\\n\", fenced, control, sequential, relaxed, ITERATIONS);\n"
    "    return fenced < 0 || control < 0 || sequential < 0 || relaxed < 0;\n"
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
    check_test_expect(test, ready, "fences: the disassembler that reads the builds' objects initializes");
    static const char* const names[8] = { "fenced0", "fenced1", "plain0", "plain1", "sequential0", "sequential1", "relaxed0", "relaxed1" };
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
            // Its four fences and its eighteen atomic accesses (the seq_cst and relaxed threads' stores and loads, the eight
            // threads' done flags' stores, the go flag's store and the wait's load) are the typed stream's, none a helper's call.
            const bool reported = text != NULL && strstr(text, "helpers.fence\tdirect=4\thelper=0\tplanned=0\n") != NULL &&
                                  strstr(text, "helpers.atomic-access\tdirect=18\thelper=0\tplanned=0\n") != NULL &&
                                  strstr(text, "helpers\tunits=1\trefused=0\tdirect=22\thelper=0\tplanned=0\n") != NULL;
            const bool taken = !tier1 || check_test_tier1_taken(text, names, BUSTER_ARRAY_LENGTH(names));
            if (status != 0 && text != NULL)
            {
                fprintf(stderr, "%s", text);
            }
            free(text);
            built &= status == 0 && reported && taken;
        }
        char message[256];
        snprintf(message, sizeof(message), "fences (%s): the litmus builds, its fences and atomic accesses taken directly%s", way, tier1 ? " and its threads Tier 1's" : "");
        check_test_expect(test, built, message);
        // One mfence in each fenced thread, between its store and its load, and in each seq_cst one, after its store; none in
        // the control's or the relaxed ones.
        uint32_t fences[8];
        for (uint32_t t = 0; t < 8; t += 1)
        {
            fences[t] = built ? check_test_fence_records(object, names[t]) : UINT32_MAX;
        }
        snprintf(message, sizeof(message), "fences (%s): the fenced and seq_cst threads have one mfence each, the others none (%u %u %u %u %u %u %u %u)", way, fences[0],
                 fences[1], fences[2], fences[3], fences[4], fences[5], fences[6], fences[7]);
        check_test_expect(test, fences[0] == 1 && fences[1] == 1 && fences[2] == 0 && fences[3] == 0 && fences[4] == 1 && fences[5] == 1 && fences[6] == 0 && fences[7] == 0,
                          message);
        char captured[256] = { 0 };
        const bool ran = built && check_test_driver_capture(output, captured, sizeof(captured));
        int fenced = -1;
        int control = -1;
        int sequential = -1;
        int relaxed = -1;
        int iterations = 0;
        const bool skipped = ran && strcmp(captured, "skipped\n") == 0;
        const bool read = ran && sscanf(captured, "fenced=%d control=%d sequential=%d relaxed=%d iterations=%d", &fenced, &control, &sequential, &relaxed, &iterations) == 5;
        snprintf(message, sizeof(message), "fences (%s): the fenced and seq_cst litmus never show both loads reading 0 (%d and %d of %d iterations)", way, fenced, sequential,
                 iterations);
        check_test_expect(test, skipped || (read && fenced == 0 && sequential == 0 && control >= 0 && relaxed >= 0 && iterations > 0), message);
        if (read)
        {
            // The controls' counts are the evidence that the machine shows the reordering the fence and the seq_cst store
            // forbid; one may be 0 on a run where the threads never overlapped, which leaves the 0 beside it weaker, not wrong.
            printf("fences (%s): of %d iterations, both loads read 0 in %d without a thread fence and %d with one; in %d with relaxed accesses and %d with "
                   "seq_cst ones\n",
                   way, iterations, control, fenced, relaxed, sequential);
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

// #1059's concurrent read/modify/writes: four threads add to shared counters by every direct form (GNU's fetch_add, a
// __sync_ one, an implicit _Atomic object's increment, C11's fetch_sub of a short and GNU's add_fetch of a byte, which
// wrap) and by compare-exchange loops of each direct form (GNU's weak compare-exchange of a long, __sync's val form of a
// short and bool form of a byte, which wrap, C11's weak one of an int, and an implicit _Atomic double's +=, its plan's
// loop), and take a spin lock by GNU's exchange and release it by a store, around a plain increment; then four threads
// increment a volatile counter ten times as often, the control, which may lose updates. Every atomic count must be
// exact. Each run's threads start together, past a barrier of an add_fetch and an acquire load, so that they overlap
// where they have CPUs of their own.
static const char check_test_counter_litmus_source[] =
    "#define _GNU_SOURCE\n"
    "#include <pthread.h>\n"
    "#include <sched.h>\n"
    "#include <stdio.h>\n"
    "#include <unistd.h>\n"
    "#define THREADS 4\n"
    "#define ITERATIONS 100000\n"
    "#define CONTROL_ITERATIONS 1000000\n"
    "static long gnu;\n"
    "static int legacy;\n"
    "static _Atomic long implicit;\n"
    "static _Atomic short down;\n"
    "static unsigned char bytes;\n"
    "static long swapped;\n"
    "static short valued;\n"
    "static unsigned char flagged;\n"
    "static _Atomic int c11;\n"
    "static _Atomic double floating;\n"
    "static int lock;\n"
    "static long guarded;\n"
    "static volatile long plain;\n"
    "static int ready;\n"
    // A spin that yields past its first spins, so that the threads go on where they share CPUs with others.
    "static void start(void)\n"
    "{\n"
    "    __atomic_add_fetch(&ready, 1, __ATOMIC_ACQ_REL);\n"
    "    for (int spins = 0; __atomic_load_n(&ready, __ATOMIC_ACQUIRE) < THREADS; spins++)\n"
    "    {\n"
    "        if (spins > 64) sched_yield();\n"
    "    }\n"
    "}\n"
    "void* worker(void* unused)\n"
    "{\n"
    "    start();\n"
    "    for (int i = 0; i < ITERATIONS; i++)\n"
    "    {\n"
    "        __atomic_fetch_add(&gnu, 1, __ATOMIC_RELAXED);\n"
    "        __sync_fetch_and_add(&legacy, 2);\n"
    "        implicit++;\n"
    "        __c11_atomic_fetch_sub(&down, 1, __ATOMIC_ACQ_REL);\n"
    "        __atomic_add_fetch(&bytes, 3, __ATOMIC_SEQ_CST);\n"
    "        long expected = __atomic_load_n(&swapped, __ATOMIC_RELAXED);\n"
    "        while (!__atomic_compare_exchange_n(&swapped, &expected, expected + 1, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}\n"
    "        short old;\n"
    "        do old = __atomic_load_n(&valued, __ATOMIC_RELAXED); while (__sync_val_compare_and_swap(&valued, old, (short)(old - 1)) != old);\n"
    "        unsigned char seen;\n"
    "        do seen = __atomic_load_n(&flagged, __ATOMIC_RELAXED); while (!__sync_bool_compare_and_swap(&flagged, seen, (unsigned char)(seen + 5)));\n"
    "        int current = __c11_atomic_load(&c11, __ATOMIC_RELAXED);\n"
    "        while (!__c11_atomic_compare_exchange_weak(&c11, &current, current + 2, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {}\n"
    "        floating += 1.0;\n"
    "        for (int spins = 0; __atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE) != 0; spins++)\n"
    "        {\n"
    "            if (spins > 64) sched_yield();\n"
    "        }\n"
    "        guarded++;\n"
    "        __atomic_store_n(&lock, 0, __ATOMIC_RELEASE);\n"
    "    }\n"
    "    return unused;\n"
    "}\n"
    "void* control(void* unused)\n"
    "{\n"
    "    start();\n"
    "    for (int i = 0; i < CONTROL_ITERATIONS; i++) plain++;\n"
    "    return unused;\n"
    "}\n"
    "static int run(void* (*body)(void*))\n"
    "{\n"
    "    pthread_t threads[THREADS];\n"
    "    int created = 0;\n"
    "    ready = 0;\n"
    "    while (created < THREADS && pthread_create(&threads[created], 0, body, 0) == 0) created++;\n"
    "    for (int t = 0; t < created; t++) pthread_join(threads[t], 0);\n"
    "    return created == THREADS;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    cpu_set_t set;\n"
    "    alarm(120);\n"
    "    if (sched_getaffinity(0, sizeof(set), &set) != 0 || CPU_COUNT(&set) < 2) { printf(\"skipped\\n\"); return 0; }\n"
    "    if (!run(worker) || !run(control)) return 1;\n"
    "    printf(\"gnu=%ld legacy=%d implicit=%ld down=%d bytes=%d guarded=%ld plain=%ld swapped=%ld valued=%d flagged=%d c11=%d floating=%ld\\n\", gnu, legacy,\n"
    "           (long)implicit, (int)down, (int)bytes, guarded, (long)plain, swapped, (int)valued, (int)flagged, (int)c11, (long)floating);\n"
    "    return 0;\n"
    "}\n";

static void check_test_driver_counters(CheckTest* test, const char* executable, const char* directory)
{
    char source[512];
    snprintf(source, sizeof(source), "%s/counter-litmus.c", directory);
    const bool written = parse_test_write(source, check_test_counter_litmus_source, sizeof(check_test_counter_litmus_source) - 1);
    check_test_expect(test, written, "counters: the litmus's source is written");
    static const char* const names[3] = { "start", "worker", "control" };
    // The program's totals: four threads of 100000 iterations each, a short's and a byte's wrapped, and the control's of
    // 1000000 each.
    const long total = 4 * 100000L;
    const long control = 4 * 1000000L;
    for (uint32_t build = 0; written && build < 4; build += 1)
    {
        const bool tier1 = build >= 2;
        const bool scalar = (build & 1) != 0;
        const char* const way = tier1 ? scalar ? "Tier 1, scalar path" : "Tier 1, vector path" : scalar ? "Tier 0, scalar path" : "Tier 0, vector path";
        char output[512];
        snprintf(output, sizeof(output), "%s/counter-litmus-%u", directory, build);
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
        arguments[count++] = "-o";
        arguments[count++] = output;
        arguments[count++] = source;
        arguments[count] = NULL;
        char* text = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        // The worker's six read/modify/writes and five compare-exchanges and the barrier's addition, and eleven atomic
        // accesses (the lock's release, the barrier's load, the compare-exchange loops' first loads, and main's loads of the
        // four _Atomic counters), are the typed stream's, none a helper's call.
        const bool reported = text != NULL && strstr(text, "helpers.atomic-rmw\tdirect=12\thelper=0\tplanned=0\n") != NULL &&
                              strstr(text, "helpers.atomic-access\tdirect=11\thelper=0\tplanned=0\n") != NULL &&
                              strstr(text, "helpers\tunits=1\trefused=0\tdirect=23\thelper=0\tplanned=0\n") != NULL;
        const bool taken = !tier1 || check_test_tier1_taken(text, names, BUSTER_ARRAY_LENGTH(names));
        if ((status != 0 || !reported) && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        const bool built = status == 0 && reported && taken;
        char message[256];
        snprintf(message, sizeof(message), "counters (%s): the litmus builds, its read/modify/writes taken directly%s", way, tier1 ? " and its threads Tier 1's" : "");
        check_test_expect(test, built, message);
        char captured[256] = { 0 };
        const bool ran = built && check_test_driver_capture(output, captured, sizeof(captured));
        long gnu = -1;
        int legacy = -1;
        long implicit = -1;
        int down = -1;
        int bytes = -1;
        long guarded = -1;
        long plain = -1;
        long swapped = -1;
        int valued = -1;
        int flagged = -1;
        int c11 = -1;
        long floating = -1;
        const bool skipped = ran && strcmp(captured, "skipped\n") == 0;
        const bool read = ran && sscanf(captured, "gnu=%ld legacy=%d implicit=%ld down=%d bytes=%d guarded=%ld plain=%ld swapped=%ld valued=%d flagged=%d c11=%d floating=%ld", &gnu,
                                        &legacy, &implicit, &down, &bytes, &guarded, &plain, &swapped, &valued, &flagged, &c11, &floating) == 12;
        const bool exact = read && gnu == total && legacy == 2 * total && implicit == total && down == (int16_t)-total && bytes == (uint8_t)(3 * total) && guarded == total &&
                           plain >= 1 && plain <= control && swapped == total && valued == (int16_t)-total && flagged == (uint8_t)(5 * total) && c11 == 2 * total &&
                           floating == total;
        char message_counts[200];
        snprintf(message_counts, sizeof(message_counts), "gnu=%ld legacy=%d implicit=%ld down=%d bytes=%d guarded=%ld swapped=%ld valued=%d flagged=%d c11=%d floating=%ld", gnu,
                 legacy, implicit, down, bytes, guarded, swapped, valued, flagged, c11, floating);
        snprintf(message, sizeof(message), "counters (%s): every atomic count is exact (%s)", way, message_counts);
        check_test_expect(test, skipped || exact, message);
        if (read)
        {
            // The control's count is the evidence that the threads overlapped: a volatile increment loses updates the locked
            // ones keep. It may be exact on a run where the threads never did (a loaded machine's, whose threads take turns
            // on one CPU), which leaves the exact counts beside it weaker, not wrong.
            printf("counters (%s): %ld of %ld volatile increments kept, every atomic one\n", way, plain, control);
        }
        else if (skipped)
        {
            printf("counters (%s): the litmus skipped its runs: the process may run on one CPU\n", way);
        }
        unlink(output);
    }
    unlink(source);
}

// --helper-report's counts (#1059) of a unit with every kind of direct form and helper call: an implicit _Atomic store,
// load and compound assignment (its lock xadd, the typed stream's ATOMIC_RMW), an explicit store, compare-exchange (its
// ATOMIC_CAS) and load, a fence, and an asm statement with instructions. Its `direct` columns are the lowered code's
// direct forms (four accesses, two read/modify/writes, one fence: a seq_cst store's own isn't one), its `helper` columns
// its call sites of each family's helpers, every call in the object (the statement's), and `planned` the signatures (the
// statement's helper alone: neither read/modify/write takes a leaf). By both paths.
static void check_test_driver_helper_report(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "_Atomic int a;\n"
                                  "int counter;\n"
                                  "int f(void)\n"
                                  "{\n"
                                  "    a = 1;\n"
                                  "    a += 2;\n"
                                  "    __atomic_store_n(&counter, 3, __ATOMIC_RELEASE);\n"
                                  "    int expected = 3;\n"
                                  "    __atomic_compare_exchange_n(&counter, &expected, 4, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);\n"
                                  "    __atomic_thread_fence(__ATOMIC_SEQ_CST);\n"
                                  "    __asm__ volatile(\"nop\" ::: \"memory\");\n"
                                  "    return a + __atomic_load_n(&counter, __ATOMIC_ACQUIRE);\n"
                                  "}\n";
    static const char* const lines[] = {
        "helpers\tunits=1\trefused=0\tdirect=7\thelper=1\tplanned=1\n",
        "helpers.atomic-access\tdirect=4\thelper=0\tplanned=0\n",
        "helpers.atomic-rmw\tdirect=2\thelper=0\tplanned=0\n",
        "helpers.fence\tdirect=1\thelper=0\tplanned=0\n",
        "helpers.asm\tdirect=0\thelper=1\tplanned=1\n",
        "helpers.unmodeled\tdirect=0\thelper=0\tplanned=0\n",
    };
    char source[512];
    char object[512];
    snprintf(source, sizeof(source), "%s/helper-report.c", directory);
    snprintf(object, sizeof(object), "%s/helper-report.o", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "helper report: the source is written");
    for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
    {
        char* vector_arguments[] = { (char*)executable, "--helper-report", "-c", "-o", object, source, NULL };
        char* scalar_arguments[] = { (char*)executable, "--scalar", "--helper-report", "-c", "-o", object, source, NULL };
        char* text = NULL;
        const int status = check_test_driver_spawn(scalar != 0 ? scalar_arguments : vector_arguments, &text);
        bool reported = status == 0 && text != NULL;
        for (uint32_t l = 0; reported && l < BUSTER_ARRAY_LENGTH(lines); l += 1)
        {
            reported = strstr(text, lines[l]) != NULL;
        }
        if (!reported && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        check_test_expect(test, reported, scalar != 0 ? "helper report (scalar path): every direct form and helper call counted, an implicit update's and an asm statement's among them"
                                                      : "helper report (vector path): every direct form and helper call counted, an implicit update's and an asm statement's among them");
        unlink(object);
    }
    unlink(source);
}
