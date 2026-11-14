// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The frame pointer on request (#177), included by c_checker_driver_test.c after c_check_tier1_driver_test.c, whose
// object decoder (check_test_tier1_function) and refusal lines (check_test_tier1_taken) it takes. With
// -fno-omit-frame-pointer every function pushes rbp first and points it at the push, so that rbp chains the frames, as
// a profiler or a debugger without unwind tables walks them; with -momit-leaf-frame-pointer too, a function that calls
// nothing keeps none (predefines_frame_pointer). The fixture's chain is three functions of ours (a frame object, a
// variadic definition, an over-aligned local and a double live across a call among them) between clang's main and
// clang's walker, which follows rbp from its own frame and checks each return address it finds is in the function it
// expects, then that the chain ends at main's frame. Each build, by Tier 0 and by --tier1 (with and without
// rematerialization), by each path and with the leaf rule or without, prints clang's build's output, and its object
// starts each chain function with `push rbp; mov rbp, rsp`, the leaf function too but under the leaf rule. Without the
// option, Tier 0's and Tier 1's frames keep none, as before. The chain's one inlining candidate, frame_third, is noinline:
// --tier1 expands an external definition's calls too (#2541 step 2), which would take its frame out of the chain.

static const char check_test_frame_pointer_source[] =
    "#include <stdarg.h>\n"
    "\n"
    "int frame_walk(int depth);\n"
    "\n"
    "int frame_leaf(int x) { return x * 3 + 1; }\n"
    "\n"
    "__attribute__((noinline)) int frame_third(int x)\n"
    "{\n"
    "    int a[4] = { x, x + 1, x + 2, x + 3 };\n"
    "    return frame_walk(3) * 100 + a[x & 3];\n"
    "}\n"
    "\n"
    "int frame_second(int x, ...)\n"
    "{\n"
    "    va_list list;\n"
    "    va_start(list, x);\n"
    "    int y = va_arg(list, int);\n"
    "    va_end(list);\n"
    "    return frame_third(x + y) + frame_leaf(y);\n"
    "}\n"
    "\n"
    "int frame_first(int x)\n"
    "{\n"
    "    _Alignas(64) char wide[64];\n"
    "    for (int i = 0; i < 64; i += 1)\n"
    "    {\n"
    "        wide[i] = (char)(x + i);\n"
    "    }\n"
    "    double half = x * 0.5;\n"
    "    const int chained = frame_second(x, x + 1);\n"
    "    return chained + (int)(half * 4.0) + wide[x & 63];\n"
    "}\n";

static const char check_test_frame_pointer_main_source[] =
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "\n"
    "int frame_first(int x);\n"
    "int frame_second(int x, ...);\n"
    "int frame_third(int x);\n"
    "int frame_leaf(int x);\n"
    "int main(void);\n"
    "\n"
    "static uintptr_t main_frame;\n"
    "static int walked;\n"
    "\n"
    "// How much of the rbp chain is whole: a point for each return address in its function (past its start, before the\n"
    "// next function's), and one for the chain's end at main's frame. 5 when the chain is whole.\n"
    "__attribute__((noinline)) int frame_walk(int depth)\n"
    "{\n"
    "    const uintptr_t known[6] = { (uintptr_t)frame_third, (uintptr_t)frame_second, (uintptr_t)frame_first, (uintptr_t)main,\n"
    "                                 (uintptr_t)frame_walk, (uintptr_t)frame_leaf };\n"
    "    const uintptr_t* frame = __builtin_frame_address(0);\n"
    "    int whole = 0;\n"
    "    for (int i = 0; i <= depth; i += 1)\n"
    "    {\n"
    "        const uintptr_t back = frame[1];\n"
    "        uintptr_t end = known[i] + 65536;\n"
    "        for (int j = 0; j < 6; j += 1)\n"
    "        {\n"
    "            end = known[j] > known[i] && known[j] < end ? known[j] : end;\n"
    "        }\n"
    "        whole += back > known[i] && back < end;\n"
    "        frame = (const uintptr_t*)frame[0];\n"
    "    }\n"
    "    whole += (uintptr_t)frame == main_frame;\n"
    "    walked += whole;\n"
    "    return whole;\n"
    "}\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    main_frame = (uintptr_t)__builtin_frame_address(0);\n"
    "    const int first = frame_first(5);\n"
    "    const int second = frame_first(70);\n"
    "    printf(\"%d %d %d\\n\", first, second, walked);\n"
    "    return 0;\n"
    "}\n";

// The fixture compiled into `object` (-c) by Tier 0, or with `tier1` by --tier1 (with or without rematerialization),
// by the vector path or with `scalar` its twin, with -fno-omit-frame-pointer where `keep`, and with `leaf`
// -momit-leaf-frame-pointer after it: whether it compiled, with every function Tier 1's under --tier1.
static bool check_test_frame_pointer_build(const char* executable, const char* source, const char* object, bool tier1, bool rematerialize, bool scalar, bool keep,
                                           bool leaf)
{
    char* arguments[16] = { (char*)executable, "--verify" };
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
    if (keep)
    {
        arguments[count++] = "-fno-omit-frame-pointer";
    }
    if (leaf)
    {
        arguments[count++] = "-momit-leaf-frame-pointer";
    }
    arguments[count++] = "-c";
    arguments[count++] = "-o";
    arguments[count++] = (char*)object;
    arguments[count++] = (char*)source;
    arguments[count] = NULL;
    char* text = NULL;
    const int status = check_test_driver_spawn(arguments, &text);
    const char* const names[1] = { "" };
    const bool taken = !tier1 || check_test_tier1_taken(text, names, 1);
    if ((status != 0 || !taken) && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    return status == 0 && taken;
}

// Whether function `name` of `object` starts with `push rbp; mov rbp, rsp` (in either of mov's register forms).
static bool check_test_frame_pointer_prologue(const char* object, const char* name, bool* found)
{
    enum
    {
        CHECK_TEST_FRAME_POINTER_RECORDS = 1024,
    };
    X86Decoded* records = calloc(CHECK_TEST_FRAME_POINTER_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_FRAME_POINTER_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_FRAME_POINTER_RECORDS, sizeof(uint32_t));
    uint32_t relocations = 0;
    const uint32_t count = records != NULL && offsets != NULL && types != NULL ?
                               check_test_tier1_function(object, name, records, CHECK_TEST_FRAME_POINTER_RECORDS, offsets, types, &relocations) : 0;
    *found = count >= 2;
    bool pushed = false;
    if (*found)
    {
        const X86Instruction* push = &records[0].instruction;
        const X86Instruction* set = &records[1].instruction;
        const uint32_t push_form = push->form & BUSTER_X86_FORM_NUMBER;
        const uint32_t set_form = set->form & BUSTER_X86_FORM_NUMBER;
        const bool push_rbp = push_form == X86_FORM_PUSH_R64 && (push->rm == X86_REGISTER_RBP || push->reg == X86_REGISTER_RBP);
        const bool direct = set->mode == BUSTER_X86_REGISTER_DIRECT;
        const bool mov_rbp = direct && ((set_form == X86_FORM_MOV_RM64_R64 && set->rm == X86_REGISTER_RBP && set->reg == X86_REGISTER_RSP) ||
                                        (set_form == X86_FORM_MOV_R64_RM64 && set->reg == X86_REGISTER_RBP && set->rm == X86_REGISTER_RSP));
        pushed = push_rbp && mov_rbp;
    }
    free(records);
    free(offsets);
    free(types);
    return pushed;
}

static void check_test_driver_frame_pointer(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char harness[512];
    char harness_object[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/frame-pointer.c", directory);
    snprintf(harness, sizeof(harness), "%s/frame-pointer-main.c", directory);
    snprintf(harness_object, sizeof(harness_object), "%s/frame-pointer-main.o", directory);
    snprintf(reference, sizeof(reference), "%s/frame-pointer-clang", directory);
    const bool written = parse_test_write(source, check_test_frame_pointer_source, sizeof(check_test_frame_pointer_source) - 1) &&
                         parse_test_write(harness, check_test_frame_pointer_main_source, sizeof(check_test_frame_pointer_main_source) - 1);
    char* reference_arguments[] = { (char*)clang, "-O0", "-fno-omit-frame-pointer", "-o", reference, source, harness, NULL };
    char* harness_arguments[] = { (char*)clang, "-O0", "-fno-omit-frame-pointer", "-c", "-o", harness_object, harness, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool harnessed = written && check_test_driver_spawn(harness_arguments, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    const bool ran = referenced && harnessed && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the frame pointer fixture runs");
    // Each of the walker's two walks finds the chain whole: 5 points each.
    const size_t length = strlen(expected);
    check_test_expect(test, !ran || (length > 4 && strcmp(expected + length - 4, " 10\n") == 0), "clang's build of the frame pointer fixture walks a whole chain");
    const bool ready = ran && (x86_semantics_initialized || x86_semantics_initialize()) && x86_disassembler_initialize();
    check_test_expect(test, !ran || ready, "the semantics and the disassembler's tables");
    static const char* const chain[3] = { "frame_first", "frame_second", "frame_third" };
    // Tier 0, then --tier1 with rematerialization and without; each by each path, without the leaf rule and with it.
    for (uint32_t build = 0; ready && build < 3; build += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t leaf = 0; leaf < 2; leaf += 1)
            {
                char object[512];
                char program[512];
                snprintf(object, sizeof(object), "%s/frame-pointer-%u-%u-%u.o", directory, build, path, leaf);
                snprintf(program, sizeof(program), "%s/frame-pointer-%u-%u-%u", directory, build, path, leaf);
                const bool compiled = check_test_frame_pointer_build(executable, source, object, build != 0, build == 1, path != 0, true, leaf != 0);
                check_test_expect(test, compiled, build == 0 ? "the frame pointer fixture compiles by Tier 0" : "the frame pointer fixture compiles by --tier1, every function Tier 1's");
                bool chained = compiled;
                for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(chain); c += 1)
                {
                    bool found = false;
                    chained = chained && check_test_frame_pointer_prologue(object, chain[c], &found);
                }
                check_test_expect(test, !compiled || chained, "-fno-omit-frame-pointer: each chain function starts push rbp; mov rbp, rsp");
                bool found = false;
                const bool leaf_pushed = compiled && check_test_frame_pointer_prologue(object, "frame_leaf", &found);
                check_test_expect(test, !compiled || (found && leaf_pushed == (leaf == 0)),
                                  leaf == 0 ? "-fno-omit-frame-pointer: a leaf function keeps the frame pointer" : "-momit-leaf-frame-pointer: a leaf function keeps none");
                char* link_arguments[] = { (char*)clang, "-o", program, object, harness_object, NULL };
                char* link_text = NULL;
                const bool linked = compiled && check_test_driver_spawn(link_arguments, &link_text) == 0;
                free(link_text);
                char actual[256] = { 0 };
                check_test_expect(test, linked && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                                  "the frame pointer fixture's walker finds the whole rbp chain through our frames, as through clang's");
                unlink(object);
                unlink(program);
            }
        }
    }
    // Without the option, neither tier keeps a frame pointer where its frame doesn't need one: the leaf function and the
    // frame object's function start without the push.
    for (uint32_t build = 0; ready && build < 2; build += 1)
    {
        char object[512];
        snprintf(object, sizeof(object), "%s/frame-pointer-omitted-%u.o", directory, build);
        const bool compiled = check_test_frame_pointer_build(executable, source, object, build != 0, true, false, false, false);
        check_test_expect(test, compiled, "the frame pointer fixture compiles without the option");
        bool found_leaf = false;
        bool found_third = false;
        const bool leaf_pushed = compiled && check_test_frame_pointer_prologue(object, "frame_leaf", &found_leaf);
        const bool third_pushed = compiled && check_test_frame_pointer_prologue(object, "frame_third", &found_third);
        check_test_expect(test, !compiled || (found_leaf && found_third && !leaf_pushed && !third_pushed),
                          build == 0 ? "Tier 0's frames keep no frame pointer by default" : "Tier 1's frames keep no frame pointer by default");
        unlink(object);
    }
    unlink(source);
    unlink(harness);
    unlink(harness_object);
    unlink(reference);
}

// asm statements whose templates return (#1839), placed in their callers as asm bodies: `leave; ret` with a value in
// eax, on a branch of the function and on the template's own branch, which leave their functions past the epilogue, as
// clang's objects do. With -fno-omit-frame-pointer both compilers' frames are rbp's, so that the way out returns to the
// caller: each build (Tier 0, by each path, under --verify) prints clang's build's output, rbx among the caller's
// registers it keeps (the harness sets it before a call that returns through the template and reads it after), as a
// function whose template returns keeps no value of its own in a callee-saved register.
static const char check_test_asm_return_source[] =
    "int ret_early(int x)\n"
    "{\n"
    "    if (x > 3)\n"
    "    {\n"
    "        __asm__ volatile(\"movl $7, %%eax\\n\\tleave\\n\\tret\" ::: \"eax\");\n"
    "    }\n"
    "    return x * 2;\n"
    "}\n"
    "\n"
    "int ret_pick(int x)\n"
    "{\n"
    "    int kept = x * 5;\n"
    "    __asm__ volatile(\"cmpl $5, %0\\n\\tjne 1f\\n\\tmovl $99, %%eax\\n\\tleave\\n\\tret\\n1:\" : : \"r\"(x) : \"eax\", \"cc\");\n"
    "    return x + 1 + kept;\n"
    "}\n";

static const char check_test_asm_return_main_source[] =
    "#include <stdio.h>\n"
    "\n"
    "int ret_early(int x);\n"
    "int ret_pick(int x);\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    int s = 0;\n"
    "    for (int i = 0; i < 8; i += 1)\n"
    "    {\n"
    "        s = s * 3 + ret_early(i) + ret_pick(i);\n"
    "    }\n"
    "    long kept;\n"
    "    __asm__ volatile(\"movq $0x1234, %%rbx\\n\\tmovl $5, %%edi\\n\\tcall ret_pick\\n\\tmovq %%rbx, %0\"\n"
    "                     : \"=r\"(kept) : : \"rax\", \"rcx\", \"rdx\", \"rsi\", \"rdi\", \"r8\", \"r9\", \"r10\", \"r11\", \"rbx\", \"memory\", \"cc\");\n"
    "    printf(\"%d %ld\\n\", s, kept);\n"
    "    return 0;\n"
    "}\n";

static void check_test_driver_asm_return(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char harness[512];
    char harness_object[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/asm-return.c", directory);
    snprintf(harness, sizeof(harness), "%s/asm-return-main.c", directory);
    snprintf(harness_object, sizeof(harness_object), "%s/asm-return-main.o", directory);
    snprintf(reference, sizeof(reference), "%s/asm-return-clang", directory);
    const bool written = parse_test_write(source, check_test_asm_return_source, sizeof(check_test_asm_return_source) - 1) &&
                         parse_test_write(harness, check_test_asm_return_main_source, sizeof(check_test_asm_return_main_source) - 1);
    char* reference_arguments[] = { (char*)clang, "-O0", "-fno-omit-frame-pointer", "-o", reference, source, harness, NULL };
    char* harness_arguments[] = { (char*)clang, "-O0", "-fno-omit-frame-pointer", "-c", "-o", harness_object, harness, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool harnessed = written && check_test_driver_spawn(harness_arguments, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    const bool ran = referenced && harnessed && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the returning asm templates runs");
    // rbx as the harness set it: 0x1234, 4660.
    const size_t length = strlen(expected);
    check_test_expect(test, !ran || (length > 6 && strcmp(expected + length - 6, " 4660\n") == 0), "clang's build keeps the caller's rbx through a template's return");
    // Each path, with the frame pointer the option keeps and with the default options, which omit it elsewhere: a function
    // whose template returns keeps one either way, for its `leave` (tier0_kept_pointer).
    for (uint32_t build = 0; ran && build < 4; build += 1)
    {
        char object[512];
        char program[512];
        snprintf(object, sizeof(object), "%s/asm-return-%u.o", directory, build);
        snprintf(program, sizeof(program), "%s/asm-return-%u", directory, build);
        const bool compiled = check_test_frame_pointer_build(executable, source, object, false, false, (build & 1) != 0, build < 2, false);
        check_test_expect(test, compiled, "the returning asm templates compile by Tier 0, under --verify");
        char* link_arguments[] = { (char*)clang, "-o", program, object, harness_object, NULL };
        char* link_text = NULL;
        const bool linked = compiled && check_test_driver_spawn(link_arguments, &link_text) == 0;
        free(link_text);
        char actual[256] = { 0 };
        check_test_expect(test, linked && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                          "the returning asm templates leave their functions as clang's build's do, the caller's rbx kept");
        unlink(object);
        unlink(program);
    }
    unlink(source);
    unlink(harness);
    unlink(harness_object);
    unlink(reference);
}
