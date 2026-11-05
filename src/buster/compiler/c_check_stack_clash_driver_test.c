// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Stack-clash protection on request (#177), included by c_checker_driver_test.c after c_check_frame_pointer_driver_test.c,
// whose object decoder (check_test_tier1_function) and refusal lines (check_test_tier1_taken) it takes. With
// -fstack-clash-protection each frame of a page or more and each dynamic allocation touches every page below rsp before rsp
// moves past it (predefines_stack_clash, tier0_emit_probed_reservation, tier0_emit_probed_allocation, Tier 1's
// PROBE_STACK and tier1_emit_cfg_probed_allocation), so that no frame or allocation steps over a guard page.
//
// The guard fixture runs a function on a stack of its own: one page, a PROT_NONE page under it, and mapped pages under
// that, at a 64 KiB-aligned address, so that every build's realigned frames lie the same way against the guard
// whatever address the mapping gets. clang builds its references with -fno-stack-protector, as ours have none: a
// distribution's default stack protector (Arch's clang) writes a realigned frame's canary at its top, which the
// realignment can put in the guard page. Each of its cases makes three pages below the first and writes only their lowest byte: a frame, an alloca, a
// variable-length array, a frame realigned to 64 bytes and one realigned to two pages. Unprobed, the code steps over the
// guard and prints "skipped"; probed, it faults on the guard page, whose handler (on a stack of its own) prints
// "probed". Each build prints what clang -O0's does with the option and without it: by Tier 0 and by --tier1 (with and
// without rematerialization), by each path, with a frame pointer and without, under --verify, every function Tier 1's but
// the page-aligned one, which Tier 1's selection refuses with or without probes and Tier 0 compiles. The probes fixture
// computes through frames of pages, variable-length arrays and allocas in loops, an allocation of none, one of a page and
// one of a constant, recursion through two-page frames, a realigned frame and 24 allocations in a straight line (Tier 1's
// capacity for their probes, which its emission checks), printing clang -O0's line. A probed
// frame's function starts with the loop of tier0_emit_probed_reservation's records (its touch and its backward jne), an
// unprobed one with none; and the last of -fstack-clash-protection and -fno-stack-clash-protection wins: without the
// option, with it then -fno-, the objects are byte for byte the same.

static const char check_test_stack_clash_guard_source[] =
    "#define _GNU_SOURCE\n"
    "#include <alloca.h>\n"
    "#include <signal.h>\n"
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <sys/mman.h>\n"
    "#include <ucontext.h>\n"
    "#include <unistd.h>\n"
    "\n"
    "#define PAGE 4096\n"
    "\n"
    "static ucontext_t main_context;\n"
    "static ucontext_t run_context;\n"
    "static char* guard;\n"
    "static int which;\n"
    "\n"
    "__attribute__((noinline)) void frame(void)\n"
    "{\n"
    "    volatile char big[3 * PAGE];\n"
    "    big[0] = 1;\n"
    "}\n"
    "\n"
    "__attribute__((noinline)) void allocation(int bytes)\n"
    "{\n"
    "    volatile char* p = alloca(bytes);\n"
    "    p[0] = 1;\n"
    "}\n"
    "\n"
    "__attribute__((noinline)) void array(int bytes)\n"
    "{\n"
    "    volatile char p[bytes];\n"
    "    p[0] = 1;\n"
    "}\n"
    "\n"
    "__attribute__((noinline)) void aligned(void)\n"
    "{\n"
    "    _Alignas(64) volatile char big[3 * PAGE];\n"
    "    big[0] = 1;\n"
    "}\n"
    "\n"
    "__attribute__((noinline)) void paged(void)\n"
    "{\n"
    "    _Alignas(2 * PAGE) volatile char big[3 * PAGE];\n"
    "    big[0] = 1;\n"
    "}\n"
    "\n"
    "static void run(void)\n"
    "{\n"
    "    if (which == 0)\n"
    "    {\n"
    "        frame();\n"
    "    }\n"
    "    else if (which == 1)\n"
    "    {\n"
    "        allocation(3 * PAGE);\n"
    "    }\n"
    "    else if (which == 2)\n"
    "    {\n"
    "        array(3 * PAGE);\n"
    "    }\n"
    "    else if (which == 3)\n"
    "    {\n"
    "        aligned();\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        paged();\n"
    "    }\n"
    "}\n"
    "\n"
    "static void handler(int signal, siginfo_t* info, void* context)\n"
    "{\n"
    "    (void)signal;\n"
    "    (void)context;\n"
    "    const char* address = info->si_addr;\n"
    "    if (address >= guard && address < guard + PAGE)\n"
    "    {\n"
    "        write(1, \"probed\\n\", 7);\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        write(1, \"fault\\n\", 6);\n"
    "    }\n"
    "    _exit(0);\n"
    "}\n"
    "\n"
    "int main(int argc, char** argv)\n"
    "{\n"
    "    which = argc > 1 ? argv[1][0] - '0' : 0;\n"
    "    char* mapping = mmap(NULL, 26 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\n"
    "    char* region = (char*)(((uintptr_t)mapping + 16 * PAGE - 1) & ~(uintptr_t)(16 * PAGE - 1));\n"
    "    guard = region + 8 * PAGE;\n"
    "    mprotect(guard, PAGE, PROT_NONE);\n"
    "    stack_t alternate = { .ss_sp = malloc(65536), .ss_size = 65536, .ss_flags = 0 };\n"
    "    sigaltstack(&alternate, NULL);\n"
    "    struct sigaction action = { 0 };\n"
    "    action.sa_sigaction = handler;\n"
    "    action.sa_flags = SA_SIGINFO | SA_ONSTACK;\n"
    "    sigaction(SIGSEGV, &action, NULL);\n"
    "    getcontext(&run_context);\n"
    "    run_context.uc_stack.ss_sp = region + 9 * PAGE;\n"
    "    run_context.uc_stack.ss_size = PAGE;\n"
    "    run_context.uc_link = &main_context;\n"
    "    makecontext(&run_context, run, 0);\n"
    "    swapcontext(&main_context, &run_context);\n"
    "    puts(\"skipped\");\n"
    "    return 0;\n"
    "}\n";

static const char check_test_stack_clash_probes_source[] =
    "#include <alloca.h>\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "\n"
    "static unsigned long table_sum(int n)\n"
    "{\n"
    "    unsigned long table[3000];\n"
    "    for (int i = 0; i < 3000; i += 1)\n"
    "    {\n"
    "        table[i] = (unsigned long)(i * n + 7);\n"
    "    }\n"
    "    unsigned long sum = 0;\n"
    "    for (int i = 0; i < 3000; i += 3)\n"
    "    {\n"
    "        sum += table[i] ^ (sum >> 3);\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    "\n"
    "static unsigned long array_sum(int rounds)\n"
    "{\n"
    "    unsigned long sum = 0;\n"
    "    for (int r = 1; r <= rounds; r += 1)\n"
    "    {\n"
    "        const int bytes = r * 1500;\n"
    "        unsigned char data[bytes];\n"
    "        memset(data, r, (size_t)bytes);\n"
    "        for (int i = 0; i < bytes; i += 97)\n"
    "        {\n"
    "            sum += data[i] + (unsigned long)i;\n"
    "        }\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    "\n"
    "static unsigned long allocation_chain(int count)\n"
    "{\n"
    "    unsigned char* blocks[16];\n"
    "    unsigned long sum = 0;\n"
    "    for (int k = 0; k < count && k < 16; k += 1)\n"
    "    {\n"
    "        const size_t bytes = (size_t)(k + 1) * 2500;\n"
    "        blocks[k] = alloca(bytes);\n"
    "        memset(blocks[k], k + 1, bytes);\n"
    "    }\n"
    "    for (int k = 0; k < count && k < 16; k += 1)\n"
    "    {\n"
    "        sum += blocks[k][0] + blocks[k][(size_t)(k + 1) * 2500 - 1];\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    "\n"
    "static int edges(int zero)\n"
    "{\n"
    "    char* none = alloca((size_t)zero);\n"
    "    char* page = alloca(4096);\n"
    "    char* fixed = alloca(10000);\n"
    "    page[0] = 3;\n"
    "    page[4095] = 4;\n"
    "    fixed[0] = 5;\n"
    "    fixed[9999] = 6;\n"
    "    return (none != 0) + page[0] + page[4095] + fixed[0] + fixed[9999];\n"
    "}\n"
    "\n"
    "static unsigned long deep(int depth)\n"
    "{\n"
    "    volatile char pad[8192];\n"
    "    pad[0] = (char)depth;\n"
    "    pad[8191] = (char)(depth * 3);\n"
    "    return depth == 0 ? 1 : deep(depth - 1) * 3 + (unsigned char)pad[0] + (unsigned char)pad[8191];\n"
    "}\n"
    "\n"
    "static int aligned(int x)\n"
    "{\n"
    "    _Alignas(64) int block[1200];\n"
    "    for (int i = 0; i < 1200; i += 1)\n"
    "    {\n"
    "        block[i] = i ^ x;\n"
    "    }\n"
    "    return block[1199] + (int)((unsigned long)block & 63);\n"
    "}\n"
    "\n"
    "// Allocations in a straight line, nothing else between them: the probed capacity of an allocation\n"
    "// each, which a short one would refuse.\n"
    "static unsigned long many(int n)\n"
    "{\n"
    "    unsigned char* p0 = alloca((size_t)(n + 0));\n"
    "    unsigned char* p1 = alloca((size_t)(n + 700));\n"
    "    unsigned char* p2 = alloca((size_t)(n + 1400));\n"
    "    unsigned char* p3 = alloca((size_t)(n + 2100));\n"
    "    unsigned char* p4 = alloca((size_t)(n + 2800));\n"
    "    unsigned char* p5 = alloca((size_t)(n + 3500));\n"
    "    unsigned char* p6 = alloca((size_t)(n + 4200));\n"
    "    unsigned char* p7 = alloca((size_t)(n + 4900));\n"
    "    unsigned char* p8 = alloca((size_t)(n + 5600));\n"
    "    unsigned char* p9 = alloca((size_t)(n + 6300));\n"
    "    unsigned char* p10 = alloca((size_t)(n + 7000));\n"
    "    unsigned char* p11 = alloca((size_t)(n + 7700));\n"
    "    unsigned char* p12 = alloca((size_t)(n + 8400));\n"
    "    unsigned char* p13 = alloca((size_t)(n + 9100));\n"
    "    unsigned char* p14 = alloca((size_t)(n + 9800));\n"
    "    unsigned char* p15 = alloca((size_t)(n + 10500));\n"
    "    unsigned char* p16 = alloca((size_t)(n + 11200));\n"
    "    unsigned char* p17 = alloca((size_t)(n + 11900));\n"
    "    unsigned char* p18 = alloca((size_t)(n + 12600));\n"
    "    unsigned char* p19 = alloca((size_t)(n + 13300));\n"
    "    unsigned char* p20 = alloca((size_t)(n + 14000));\n"
    "    unsigned char* p21 = alloca((size_t)(n + 14700));\n"
    "    unsigned char* p22 = alloca((size_t)(n + 15400));\n"
    "    unsigned char* p23 = alloca((size_t)(n + 16100));\n"
    "    unsigned long sum = 0;\n"
    "    p0[0] = (unsigned char)1;\n"
    "    p1[0] = (unsigned char)2;\n"
    "    p2[0] = (unsigned char)3;\n"
    "    p3[0] = (unsigned char)4;\n"
    "    p4[0] = (unsigned char)5;\n"
    "    p5[0] = (unsigned char)6;\n"
    "    p6[0] = (unsigned char)7;\n"
    "    p7[0] = (unsigned char)8;\n"
    "    p8[0] = (unsigned char)9;\n"
    "    p9[0] = (unsigned char)10;\n"
    "    p10[0] = (unsigned char)11;\n"
    "    p11[0] = (unsigned char)12;\n"
    "    p12[0] = (unsigned char)13;\n"
    "    p13[0] = (unsigned char)14;\n"
    "    p14[0] = (unsigned char)15;\n"
    "    p15[0] = (unsigned char)16;\n"
    "    p16[0] = (unsigned char)17;\n"
    "    p17[0] = (unsigned char)18;\n"
    "    p18[0] = (unsigned char)19;\n"
    "    p19[0] = (unsigned char)20;\n"
    "    p20[0] = (unsigned char)21;\n"
    "    p21[0] = (unsigned char)22;\n"
    "    p22[0] = (unsigned char)23;\n"
    "    p23[0] = (unsigned char)24;\n"
    "    sum += p0[0] + p1[0] + p2[0] + p3[0] + p4[0] + p5[0] + p6[0] + p7[0] + p8[0] + p9[0] + p10[0] + p11[0] + p12[0] + p13[0] + p14[0] + p15[0] + p16[0] + p17[0] + p18[0] + p19[0] + p20[0] + p21[0] + p22[0] + p23[0];\n"
    "    return sum;\n"
    "}\n"
    "\n"
    "// A call after an allocation, its argument an aggregate by value on the stack: a dynamic frame's call area, which\n"
    "// Tier 1 takes from rsp at the call and touches.\n"
    "struct big\n"
    "{\n"
    "    unsigned char bytes[256];\n"
    "};\n"
    "\n"
    "__attribute__((noinline)) static unsigned long take_big(struct big b)\n"
    "{\n"
    "    return b.bytes[0] + b.bytes[128] + b.bytes[255];\n"
    "}\n"
    "\n"
    "static unsigned long call_area(int n)\n"
    "{\n"
    "    struct big b;\n"
    "    memset(&b, 7, sizeof(b));\n"
    "    b.bytes[255] = (unsigned char)n;\n"
    "    volatile char* p = alloca((size_t)n);\n"
    "    p[0] = 1;\n"
    "    return take_big(b) + (unsigned long)p[0];\n"
    "}\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"%lu %lu %lu %d %lu %d %lu %lu\\n\", table_sum(5), array_sum(9), allocation_chain(12), edges(0), deep(20), aligned(77), many(3000), call_area(77));\n"
    "    return 0;\n"
    "}\n";

static const char check_test_stack_clash_tail_source[] =
    "#define _GNU_SOURCE\n"
    "#include <alloca.h>\n"
    "#include <signal.h>\n"
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <sys/mman.h>\n"
    "#include <ucontext.h>\n"
    "#include <unistd.h>\n"
    "\n"
    "#define PAGE 4096\n"
    "\n"
    "static ucontext_t main_context;\n"
    "static ucontext_t run_context;\n"
    "static char* guard;\n"
    "\n"
    "// A fixed frame under a page, its local not touched before the allocation, then a dynamic allocation: of a register's\n"
    "// size or a constant's, under a page or past one.\n"
    "__attribute__((noinline)) void tail_register(int bytes)\n"
    "{\n"
    "    volatile char local[3500];\n"
    "    volatile char* p = alloca(bytes);\n"
    "    p[0] = 1;\n"
    "    local[3499] = p[0];\n"
    "}\n"
    "\n"
    "__attribute__((noinline)) void tail_constant(void)\n"
    "{\n"
    "    volatile char local[3500];\n"
    "    volatile char* p = alloca(3500);\n"
    "    p[0] = 1;\n"
    "    local[3499] = p[0];\n"
    "}\n"
    "\n"
    "__attribute__((noinline)) void tail_constant_large(void)\n"
    "{\n"
    "    volatile char local[3500];\n"
    "    volatile char* p = alloca(9000);\n"
    "    p[0] = 1;\n"
    "    local[3499] = p[0];\n"
    "}\n"
    "\n"
    "__attribute__((noinline)) void tail_array(int bytes)\n"
    "{\n"
    "    volatile char local[3500];\n"
    "    volatile char p[bytes];\n"
    "    p[0] = 1;\n"
    "    local[3499] = p[0];\n"
    "}\n"
    "\n"
    "static void handler(int signal, siginfo_t* info, void* context)\n"
    "{\n"
    "    (void)signal;\n"
    "    (void)context;\n"
    "    const char* address = info->si_addr;\n"
    "    if (address >= guard && address < guard + PAGE)\n"
    "    {\n"
    "        write(1, \"probed\\n\", 7);\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        write(1, \"fault\\n\", 6);\n"
    "    }\n"
    "    _exit(0);\n"
    "}\n"
    "\n"
    "int main(int argc, char** argv)\n"
    "{\n"
    "    const int which = argc > 1 ? argv[1][0] - '0' : 0;\n"
    "    char* mapping = mmap(NULL, 26 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\n"
    "    char* region = (char*)(((uintptr_t)mapping + 16 * PAGE - 1) & ~(uintptr_t)(16 * PAGE - 1));\n"
    "    guard = region + 8 * PAGE;\n"
    "    mprotect(guard, PAGE, PROT_NONE);\n"
    "    stack_t alternate = { .ss_sp = malloc(65536), .ss_size = 65536, .ss_flags = 0 };\n"
    "    sigaltstack(&alternate, NULL);\n"
    "    struct sigaction action = { 0 };\n"
    "    action.sa_sigaction = handler;\n"
    "    action.sa_flags = SA_SIGINFO | SA_ONSTACK;\n"
    "    sigaction(SIGSEGV, &action, NULL);\n"
    "    getcontext(&run_context);\n"
    "    // The function's own stack: 192 bytes above the guard, so that its fixed frame ends inside the guard page and its\n"
    "    // allocation below it.\n"
    "    run_context.uc_stack.ss_sp = guard + PAGE;\n"
    "    run_context.uc_stack.ss_size = 192;\n"
    "    run_context.uc_link = &main_context;\n"
    "    if (which == 0)\n"
    "    {\n"
    "        makecontext(&run_context, (void (*)(void))tail_register, 1, 3500);\n"
    "    }\n"
    "    else if (which == 1)\n"
    "    {\n"
    "        makecontext(&run_context, (void (*)(void))tail_register, 1, 9000);\n"
    "    }\n"
    "    else if (which == 2)\n"
    "    {\n"
    "        makecontext(&run_context, tail_constant, 0);\n"
    "    }\n"
    "    else if (which == 3)\n"
    "    {\n"
    "        makecontext(&run_context, tail_constant_large, 0);\n"
    "    }\n"
    "    else if (which == 4)\n"
    "    {\n"
    "        makecontext(&run_context, (void (*)(void))tail_array, 1, 3500);\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        makecontext(&run_context, (void (*)(void))tail_array, 1, 9000);\n"
    "    }\n"
    "    swapcontext(&main_context, &run_context);\n"
    "    puts(\"skipped\");\n"
    "    return 0;\n"
    "}\n";

#define BUSTER_CHECK_TEST_STACK_CLASH_CASES 5
#define BUSTER_CHECK_TEST_STACK_CLASH_TAIL_CASES 6

// A build of `source` into `object` by Tier 0 or by --tier1 (`tier1`, with or without rematerialization), by each path,
// with `options` (up to two, NULL-ended) and --verify: whether it compiled, every function Tier 1's but `paged`.
static bool check_test_stack_clash_build(const char* executable, const char* source, const char* object, bool tier1, bool rematerialize, bool scalar,
                                         const char* const* options)
{
    char* arguments[20] = { (char*)executable, "--verify" };
    uint32_t count = 2;
    if (tier1)
    {
        arguments[count++] = "--tier1";
        arguments[count++] = "--tier1-refusals";
    }
    if (tier1 && !rematerialize)
    {
        arguments[count++] = "--tier1-no-rematerialize";
    }
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    for (uint32_t o = 0; options != NULL && o < 3 && options[o] != NULL; o += 1)
    {
        arguments[count++] = (char*)options[o];
    }
    arguments[count++] = "-c";
    arguments[count++] = "-o";
    arguments[count++] = (char*)object;
    arguments[count++] = (char*)source;
    arguments[count] = NULL;
    char* text = NULL;
    const int status = check_test_driver_spawn(arguments, &text);
    static const char* const names[] = { "frame", "allocation", "array", "aligned", "run", "handler", "main", "table_sum", "array_sum", "allocation_chain", "edges", "deep", "many", "take_big",
                                         "call_area", "tail_register", "tail_constant", "tail_constant_large", "tail_array" };
    const bool taken = !tier1 || check_test_tier1_taken(text, names, BUSTER_ARRAY_LENGTH(names));
    if ((status != 0 || !taken) && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    return status == 0 && taken;
}

// Whether function `name` of `object` probes its frame: a record touching the byte at rsp (`or byte [rsp], 0`) and a jne
// back to an earlier record of its, tier0_emit_probed_reservation's loop (Tier 1's PROBE_ALLOCATE has the same records).
static bool check_test_stack_clash_probed(const char* object, const char* name, bool* found)
{
    enum
    {
        CHECK_TEST_STACK_CLASH_RECORDS = 1024,
    };
    X86Decoded* records = calloc(CHECK_TEST_STACK_CLASH_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_STACK_CLASH_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_STACK_CLASH_RECORDS, sizeof(uint32_t));
    uint32_t relocations = 0;
    const uint32_t count = records != NULL && offsets != NULL && types != NULL ?
                               check_test_tier1_function(object, name, records, CHECK_TEST_STACK_CLASH_RECORDS, offsets, types, &relocations) : 0;
    *found = count != 0;
    bool touch = false;
    bool back = false;
    for (uint32_t r = 0; r < count; r += 1)
    {
        const X86Instruction* record = &records[r].instruction;
        const uint32_t form = record->form & BUSTER_X86_FORM_NUMBER;
        touch |= form == X86_FORM_OR_RM8_IMM8 && record->mode == BUSTER_X86_MEMORY_BASE && record->rm == X86_REGISTER_RSP && record->displacement == 0 && record->immediate == 0;
        back |= (form == X86_FORM_JNE_REL8 || form == X86_FORM_JNE_REL32) && record->displacement < 0;
    }
    free(records);
    free(offsets);
    free(types);
    return touch && back;
}

// Whether the files at `left` and `right` both read and have the same bytes.
static bool check_test_stack_clash_same(const char* left, const char* right)
{
    uint64_t left_length = 0;
    uint64_t right_length = 0;
    uint8_t* left_bytes = check_test_driver_read_aligned(left, &left_length);
    uint8_t* right_bytes = check_test_driver_read_aligned(right, &right_length);
    const bool same = left_bytes != NULL && right_bytes != NULL && left_length == right_length && memcmp(left_bytes, right_bytes, left_length) == 0;
    free(left_bytes);
    free(right_bytes);
    return same;
}

// A guard program's verdicts, case by case (`cases` of them), of the program at `program` into `verdicts` (each case's
// line).
static bool check_test_stack_clash_cases(const char* program, uint32_t cases, char* verdicts, size_t capacity)
{
    bool ran = true;
    verdicts[0] = 0;
    for (uint32_t c = 0; ran && c < cases; c += 1)
    {
        char command[640];
        char line[64] = { 0 };
        snprintf(command, sizeof(command), "'%s' %u", program, c);
        ran = check_test_driver_capture(command, line, sizeof(line));
        const size_t used = strlen(verdicts);
        snprintf(verdicts + used, capacity - used, "%s", line);
    }
    return ran;
}

// The guard fixture's verdicts (check_test_stack_clash_cases' of its cases).
static bool check_test_stack_clash_verdicts(const char* program, char* verdicts, size_t capacity)
{
    return check_test_stack_clash_cases(program, BUSTER_CHECK_TEST_STACK_CLASH_CASES, verdicts, capacity);
}

// The tail fixture (#1434's review): a function entered 192 bytes above the guard page, whose fixed frame under a page
// ends inside the guard, its local not touched before a dynamic allocation that takes rsp below the guard (of a
// register's size or a constant's, under a page or past one, an alloca's or a variable-length array's). Without the
// prologue's touch of a frame pointer's reservation, the allocation's own touches all fall below the guard, and the
// function steps over it. Every build with the option faults at the guard, as clang -O0's: by Tier 0 and by --tier1
// (with and without rematerialization), by each path, with a frame pointer and without, under --verify. Without the
// option clang's -O0 frame keeps slots at its bottom, inside the guard, so only the option's verdicts are compared.
static void check_test_stack_clash_tail(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/stack-clash-tail.c", directory);
    snprintf(reference, sizeof(reference), "%s/stack-clash-tail-clang", directory);
    const bool written = parse_test_write(source, check_test_stack_clash_tail_source, sizeof(check_test_stack_clash_tail_source) - 1);
    char* arguments[] = { (char*)clang, "-O0", "-fno-stack-protector", "-fstack-clash-protection", "-o", reference, source, NULL };
    char* text = NULL;
    char expected[128] = { 0 };
    const bool referenced = written && check_test_driver_spawn(arguments, &text) == 0 &&
                            check_test_stack_clash_cases(reference, BUSTER_CHECK_TEST_STACK_CLASH_TAIL_CASES, expected, sizeof(expected)) &&
                            strcmp(expected, "probed\nprobed\nprobed\nprobed\nprobed\nprobed\n") == 0;
    free(text);
    check_test_expect(test, referenced, "clang -O0's probed allocations after a fixed frame under a page fault on the guard page");
    static const char* const probe_options[] = { "-fstack-clash-protection", NULL };
    static const char* const pointer_options[] = { "-fstack-clash-protection", "-fno-omit-frame-pointer", NULL };
    for (uint32_t build = 0; referenced && build < 3; build += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t pointer = 0; pointer < 2; pointer += 1)
            {
                char object[512];
                char program[512];
                snprintf(object, sizeof(object), "%s/stack-clash-tail-%u-%u-%u.o", directory, build, path, pointer);
                snprintf(program, sizeof(program), "%s/stack-clash-tail-%u-%u-%u", directory, build, path, pointer);
                const bool compiled = check_test_stack_clash_build(executable, source, object, build != 0, build == 1, path != 0, pointer != 0 ? pointer_options : probe_options);
                check_test_expect(test, compiled, build == 0 ? "the tail fixture compiles by Tier 0 with -fstack-clash-protection" :
                                                               "the tail fixture compiles by --tier1 with -fstack-clash-protection, every function Tier 1's");
                char* link_arguments[] = { (char*)clang, "-o", program, object, NULL };
                char* link_text = NULL;
                const bool linked = compiled && check_test_driver_spawn(link_arguments, &link_text) == 0;
                free(link_text);
                char actual[128] = { 0 };
                const bool same = linked && check_test_stack_clash_cases(program, BUSTER_CHECK_TEST_STACK_CLASH_TAIL_CASES, actual, sizeof(actual)) && strcmp(actual, expected) == 0;
                check_test_expect(test, same, "-fstack-clash-protection: an allocation after a fixed frame under a page faults on the guard page, as clang's");
                if (linked && !same)
                {
                    fprintf(stderr, "  ours:\n%s  clang's:\n%s", actual, expected);
                }
                unlink(object);
                unlink(program);
            }
        }
    }
    unlink(source);
    unlink(reference);
}

static void check_test_driver_stack_clash(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char guard[512];
    char probes[512];
    char reference[512];
    snprintf(guard, sizeof(guard), "%s/stack-clash-guard.c", directory);
    snprintf(probes, sizeof(probes), "%s/stack-clash-probes.c", directory);
    snprintf(reference, sizeof(reference), "%s/stack-clash-clang", directory);
    const bool written = parse_test_write(guard, check_test_stack_clash_guard_source, sizeof(check_test_stack_clash_guard_source) - 1) &&
                         parse_test_write(probes, check_test_stack_clash_probes_source, sizeof(check_test_stack_clash_probes_source) - 1);
    // clang -O0's verdicts without the option and with it, and its probes fixture's line.
    char expected[2][128];
    bool referenced = written;
    for (uint32_t probed = 0; probed < 2; probed += 1)
    {
        char* arguments[] = { (char*)clang, "-O0", "-fno-stack-protector", probed != 0 ? "-fstack-clash-protection" : "-fno-stack-clash-protection", "-o", reference,
                              guard, NULL };
        char* text = NULL;
        referenced = referenced && check_test_driver_spawn(arguments, &text) == 0 && check_test_stack_clash_verdicts(reference, expected[probed], sizeof(expected[probed]));
        free(text);
    }
    const bool clang_verdicts = referenced && strcmp(expected[0], "skipped\nskipped\nskipped\nskipped\nskipped\n") == 0 &&
                                strcmp(expected[1], "probed\nprobed\nprobed\nprobed\nprobed\n") == 0;
    check_test_expect(test, clang_verdicts, "clang -O0 steps over the guard page unprobed, and faults on it with -fstack-clash-protection");
    if (referenced && !clang_verdicts)
    {
        fprintf(stderr, "  clang's verdicts unprobed:\n%s  and probed:\n%s", expected[0], expected[1]);
    }
    char probes_expected[256] = { 0 };
    {
        char* arguments[] = { (char*)clang, "-O0", "-fno-stack-protector", "-fstack-clash-protection", "-o", reference, probes, NULL };
        char* text = NULL;
        referenced = referenced && check_test_driver_spawn(arguments, &text) == 0 && check_test_driver_capture(reference, probes_expected, sizeof(probes_expected)) &&
                     probes_expected[0] != 0;
        free(text);
    }
    check_test_expect(test, referenced, "clang's build of the probes fixture runs");
    const bool ready = referenced && (x86_semantics_initialized || x86_semantics_initialize()) && x86_disassembler_initialize();
    static const char* const probe_options[] = { "-fstack-clash-protection", NULL };
    static const char* const pointer_options[] = { "-fstack-clash-protection", "-fno-omit-frame-pointer", NULL };
    // Tier 0, then --tier1 with rematerialization and without; each by each path, without a frame pointer and with one.
    for (uint32_t build = 0; ready && build < 3; build += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t pointer = 0; pointer < 2; pointer += 1)
            {
                char object[512];
                char program[512];
                snprintf(object, sizeof(object), "%s/stack-clash-%u-%u-%u.o", directory, build, path, pointer);
                snprintf(program, sizeof(program), "%s/stack-clash-%u-%u-%u", directory, build, path, pointer);
                const char* const* options = pointer != 0 ? pointer_options : probe_options;
                const bool compiled = check_test_stack_clash_build(executable, guard, object, build != 0, build == 1, path != 0, options);
                check_test_expect(test, compiled, build == 0 ? "the guard fixture compiles by Tier 0 with -fstack-clash-protection" :
                                                               "the guard fixture compiles by --tier1 with -fstack-clash-protection, every function Tier 1's but the page-aligned one");
                bool found = false;
                const bool probed = compiled && check_test_stack_clash_probed(object, "frame", &found);
                check_test_expect(test, !compiled || (found && probed), "-fstack-clash-protection: a frame of pages starts with the probes' loop");
                char* link_arguments[] = { (char*)clang, "-o", program, object, NULL };
                char* link_text = NULL;
                const bool linked = compiled && check_test_driver_spawn(link_arguments, &link_text) == 0;
                free(link_text);
                char actual[128] = { 0 };
                const bool same_verdicts = linked && check_test_stack_clash_verdicts(program, actual, sizeof(actual)) && strcmp(actual, expected[1]) == 0;
                check_test_expect(test, same_verdicts, "-fstack-clash-protection: frames, allocations and realigned frames fault on the guard page, as clang's");
                if (linked && !same_verdicts)
                {
                    fprintf(stderr, "  ours:\n%s  clang's:\n%s", actual, expected[1]);
                }
                const bool computed = check_test_stack_clash_build(executable, probes, object, build != 0, build == 1, path != 0, options);
                check_test_expect(test, computed, "the probes fixture compiles with -fstack-clash-protection");
                link_text = NULL;
                const bool computed_linked = computed && check_test_driver_spawn(link_arguments, &link_text) == 0;
                free(link_text);
                char line[256] = { 0 };
                check_test_expect(test, computed_linked && check_test_driver_capture(program, line, sizeof(line)) && strcmp(line, probes_expected) == 0,
                                  "the probes fixture prints clang -O0's line through probed frames and allocations");
                unlink(object);
                unlink(program);
            }
        }
    }
    // Without the option, neither tier probes: the guard fixture steps over the guard as clang's does, and its frame has
    // no probes' loop. The last of the pair wins: with the option then -fno-, the objects are the same bytes.
    static const char* const off_options[] = { "-fstack-clash-protection", "-fno-stack-clash-protection", NULL };
    static const char* const on_options[] = { "-fno-stack-clash-protection", "-fstack-clash-protection", NULL };
    for (uint32_t build = 0; ready && build < 2; build += 1)
    {
        char object[512];
        char other[512];
        char program[512];
        snprintf(object, sizeof(object), "%s/stack-clash-off-%u.o", directory, build);
        snprintf(other, sizeof(other), "%s/stack-clash-other-%u.o", directory, build);
        snprintf(program, sizeof(program), "%s/stack-clash-off-%u", directory, build);
        const bool compiled = check_test_stack_clash_build(executable, guard, object, build != 0, true, false, NULL);
        check_test_expect(test, compiled, "the guard fixture compiles without the option");
        bool found = false;
        const bool probed = compiled && check_test_stack_clash_probed(object, "frame", &found);
        check_test_expect(test, !compiled || (found && !probed), build == 0 ? "Tier 0's frames don't probe by default" : "Tier 1's frames don't probe by default");
        char* link_arguments[] = { (char*)clang, "-o", program, object, NULL };
        char* link_text = NULL;
        const bool linked = compiled && check_test_driver_spawn(link_arguments, &link_text) == 0;
        free(link_text);
        char actual[128] = { 0 };
        const bool same_verdicts = linked && check_test_stack_clash_verdicts(program, actual, sizeof(actual)) && strcmp(actual, expected[0]) == 0;
        check_test_expect(test, same_verdicts, "without the option, the guard fixture steps over the guard page, as clang's");
        if (linked && !same_verdicts)
        {
            fprintf(stderr, "  ours:\n%s  clang's:\n%s", actual, expected[0]);
        }
        const bool off = compiled && check_test_stack_clash_build(executable, guard, other, build != 0, true, false, off_options);
        check_test_expect(test, off && check_test_stack_clash_same(object, other), "-fstack-clash-protection then -fno-stack-clash-protection: the default's object");
        const bool on = check_test_stack_clash_build(executable, guard, object, build != 0, true, false, probe_options) &&
                        check_test_stack_clash_build(executable, guard, other, build != 0, true, false, on_options);
        check_test_expect(test, on && check_test_stack_clash_same(object, other), "-fno-stack-clash-protection then -fstack-clash-protection: the option's object");
        unlink(object);
        unlink(other);
        unlink(program);
    }
    unlink(guard);
    unlink(probes);
    unlink(reference);
    check_test_stack_clash_tail(test, clang, executable, directory);
}
