// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #134's acceptance on real and generated C (test-ssa-source):
//
//   build/compiler test-ssa-source [--generate N] [--seed N] [--clang PATH] [--timeout SECONDS] [--steps N] [--keep]
//                                  [preprocessor options] [file...]
//   build/compiler test-ssa-source --self-test [--keep]
//
// Each file's functions, as -c compiles them (ir_source_prepare, test-ir-source's preparation, with the program kept),
// are lowered to the IR with every local in memory and promoted by both of ssa_ir.c's paths, each with ssa.c's verifier
// on the core's result (ssa_ir_promote_verified: every use dominated by its definition, a phi operand per predecessor,
// no trivial phi, and its reaching-definition equations), the vector path's constructor on every function its memory
// bound admits; each promoted function's IR must have no trivial phi either (ssa_ir_phis). A file with a main is
// then run by the shared IR's interpreter (#176) in each of the three forms, all locals in memory, the vector path's SSA
// and the scalar path's, the three at once, each in a child process and a directory of its own with its standard output
// and errors in a file (ssa_source_child): the three must end alike with the same output, a run the interpreter couldn't
// finish fails, and where FILE.expected exists (c-testsuite's convention), each must exit with status 0 having written
// its bytes.
//
// --generate N writes N random programs (ssa_source_generate.c's, of seeds from --seed's on) into the work directory,
// whose control flow is structured and unstructured (gotos into and out of loops, switches whose cases fall through,
// breaks and continues at depth): each is compiled by clang -O0 (--clang's, through the oracle cache) and run, and its
// output and status are what each of its IR forms must give; then it is checked as a file is.
//
// The work directory is under $TMPDIR (or /tmp); --keep keeps it, as a failure does. The report has a line per file,
// with its functions, each path's phis and its runs, a line per failure, and the totals; the exit status is 1 on any
// failure. It is a test outside the compile path, with no vector path of its own. Include after ir_source_test.c,
// ssa_ir.c, interpreter_link.c, ir_interpreter.c, oracle_cache.c, c_testsuite.c (whose processes, files and directory
// removal it takes) and ssa_source_generate.c.

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

// The forms a program runs in: its functions' IR with every local in memory, and promoted by each path.
#define BUSTER_SSA_SOURCE_MEMORY 0
#define BUSTER_SSA_SOURCE_VECTOR 1
#define BUSTER_SSA_SOURCE_SCALAR 2
#define BUSTER_SSA_SOURCE_FORMS 3
// A run's time limit by default, in seconds, and its interpreter's steps, which bound a run first: c-testsuite's 8
// queens (00040.c), 0.05 s natively at -O0, takes 1.35 billion steps with every local in memory, 27 s on a Zen 4
// laptop, and a sanitizer's build (CI's) is several times slower. And a generated program's text's room.
#define BUSTER_SSA_SOURCE_TIMEOUT 600
#define BUSTER_SSA_SOURCE_STEPS (UINT64_C(1) << 32)
#define BUSTER_SSA_SOURCE_TEXT (UINT64_C(1) << 20)

static const char* const ssa_source_forms[BUSTER_SSA_SOURCE_FORMS] = {
    "memory",
    "vector",
    "scalar",
};

typedef struct SsaSourceTotals SsaSourceTotals;
struct SsaSourceTotals
{
    uint64_t files;
    uint64_t left_out;
    uint64_t failed_files;
    uint64_t functions;
    uint64_t instructions;
    // Each path's promoted instructions and phis (the vector path's, then the scalar path's).
    uint64_t promoted[2];
    uint64_t phis[2];
    // Functions that a lowering or a promotion refused, or whose promotion failed a check.
    uint64_t failures;
    // Programs run in the three forms, those whose runs all gave the expected outcome, and the generated programs and
    // those whose clang build couldn't be made or run.
    uint64_t programs;
    uint64_t passed;
    // The runs' interpreter steps, by form.
    uint64_t steps[BUSTER_SSA_SOURCE_FORMS];
    uint64_t generated;
    uint64_t unbuilt;
};

// What a program is expected to do: exit with `status` (a wait status) having written `length` bytes of `output` to its
// standard output and errors.
typedef struct SsaSourceExpected SsaSourceExpected;
struct SsaSourceExpected
{
    const char* output;
    uint64_t length;
    int status;
};

// A run's limits: its seconds and its interpreter's steps.
typedef struct SsaSourceLimits SsaSourceLimits;
struct SsaSourceLimits
{
    uint32_t timeout;
    uint64_t steps;
};

// What a run of a form gave: its process, its wait status (-1 if it couldn't be waited for), its output (malloc's), and
// whether its interpreter returned, with what, as its record says (else the program ended by itself: exit, a signal,
// its time).
typedef struct SsaSourceRun SsaSourceRun;
struct SsaSourceRun
{
    pid_t child;
    char* output;
    uint64_t length;
    int status;
    bool recorded;
    InterpreterResult result;
};

static const char* const ssa_source_statuses[INTERPRETER_UNSUPPORTED + 1] = {
    "ok",
    "invalid",
    "resource",
    "step limit",
    "frame limit",
    "value limit",
    "undefined",
    "memory",
    "arithmetic",
    "symbol",
    "native",
    "unsupported",
};

// `link` as the interpreter's materializer takes it (interpreter_link.c refuses fixups in code atoms, whose machine code
// the interpreter never runs, the IR carrying its own references): a copy of the link whose code atoms have no fixups,
// the other atoms' in atom order in a table of their own, in `arena`.
static bool ssa_source_data_link(const Link* link, Arena* arena, Link* result)
{
    const uint32_t count = link->fixups.count;
    uint32_t* offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
    SymbolId* target = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, count);
    int32_t* addend = BUSTER_ARENA_PUSH_ARRAY(arena, int32_t, count);
    uint8_t* kind = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count);
    FixupId* begin = BUSTER_ARENA_PUSH_ARRAY(arena, FixupId, link->atoms.count);
    uint32_t* taken = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, link->atoms.count);
    const bool allocated = count != 0 && link->atoms.count != 0 && offset != 0 && target != 0 && addend != 0 && kind != 0 && begin != 0 && taken != 0;
    if (allocated)
    {
        *result = *link;
        offset[0] = link->fixups.offset[0];
        target[0] = link->fixups.target[0];
        addend[0] = link->fixups.addend[0];
        kind[0] = link->fixups.kind[0];
        begin[0] = 0;
        taken[0] = 0;
        uint32_t next = 1;
        for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
        {
            const uint8_t class = link->atoms.class[atom];
            const bool code = class == BUSTER_LINK_CLASS_TEXT || class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI;
            const uint32_t fixups = code ? 0 : link->atoms.fixup_count[atom];
            const FixupId from = link->atoms.fixup_begin[atom];
            for (uint32_t i = 0; i < fixups; i += 1)
            {
                offset[next + i] = link->fixups.offset[from + i];
                target[next + i] = link->fixups.target[from + i];
                addend[next + i] = link->fixups.addend[from + i];
                kind[next + i] = link->fixups.kind[from + i];
            }
            begin[atom] = fixups != 0 ? next : 0;
            taken[atom] = fixups;
            next += fixups;
        }
        result->fixups.offset = offset;
        result->fixups.target = target;
        result->fixups.addend = addend;
        result->fixups.kind = kind;
        result->fixups.count = next;
        result->fixups.capacity = count;
        result->atoms.fixup_begin = begin;
        result->atoms.fixup_count = taken;
    }
    return allocated;
}

// A native's bookkeeping flags (interpreter.c's): the allocators', and the copies' and the fill's, which carry and set
// bytes' definedness.
static uint32_t ssa_source_native_flags(const char* name)
{
    uint32_t flags = 0;
    if (strcmp(name, "malloc") == 0)
    {
        flags = BUSTER_INTERPRETER_NATIVE_ALLOCATE;
    }
    else if (strcmp(name, "calloc") == 0)
    {
        flags = BUSTER_INTERPRETER_NATIVE_CALLOC;
    }
    else if (strcmp(name, "realloc") == 0)
    {
        flags = BUSTER_INTERPRETER_NATIVE_REALLOCATE;
    }
    else if (strcmp(name, "free") == 0)
    {
        flags = BUSTER_INTERPRETER_NATIVE_FREE;
    }
    else if (strcmp(name, "memcpy") == 0 || strcmp(name, "memmove") == 0)
    {
        flags = BUSTER_INTERPRETER_NATIVE_COPY;
    }
    else if (strcmp(name, "memset") == 0)
    {
        flags = BUSTER_INTERPRETER_NATIVE_SET;
    }
    return flags;
}

// The child of a run: the program of `file` in one form, `functions`, run from function `entry` (main) by the shared IR's
// interpreter, in `directory`, its standard input from /dev/null, its standard output and errors into the file `output`
// (fully buffered, as a program's are into a file) and its interpreter's result into the file `record`, killed by an
// alarm after `timeout` seconds. The program's symbols: its functions' (each the interpreter's token for it, a copy of
// its IR beside the data, so that a jump table's 32-bit offsets reach its labels), its labels', its data's (the link's
// atoms, materialized), and what it takes from the C library and libgcc, by name in this process: their functions as
// natives, their objects (stdout) by the sizes the program declares them with. Its exit status is main's result's low
// byte, or 255 if the interpreter stopped it (past `steps` steps among other reasons). It never returns.
static void ssa_source_child(const IrSourceFile* file, const IrFunction* functions, uint32_t entry, const char* directory, const char* output, const char* record,
                             uint32_t timeout, uint64_t steps)
{
    const int input = open("/dev/null", O_RDONLY);
    const int capture = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const int result_file = open(record, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ready = input >= 0 && capture >= 0 && result_file >= 0 && chdir(directory) == 0 && dup2(input, 0) == 0 && dup2(capture, 1) == 1 && dup2(capture, 2) == 2;
    if (ready)
    {
        setvbuf(stdout, 0, _IOFBF, BUFSIZ);
        alarm(timeout);
    }
    const Link* original = file->link;
    const uint32_t count = file->count;
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    IrFunction* copies = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, IrFunction, count) : 0;
    IrInterpreterFunction* table = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, IrInterpreterFunction, count) : 0;
    InterpreterLinkExternal* externals = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, InterpreterLinkExternal, original->symbols.count) : 0;
    InterpreterNativeBinding* natives = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, InterpreterNativeBinding, original->symbols.count) : 0;
    ready = arena.base != 0 && copies != 0 && table != 0 && externals != 0 && natives != 0;
    uint32_t external_count = 0;
    uint32_t native_count = 0;
    for (uint32_t f = 0; ready && f < count; f += 1)
    {
        copies[f] = functions[f];
        uint32_t* labels = copies[f].label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, copies[f].label_count) : 0;
        ready = copies[f].label_count == 0 || (labels != 0 && file->functions[f].labels != 0);
        if (ready && labels != 0)
        {
            memcpy(labels, functions[f].labels, (uint64_t)copies[f].label_count * sizeof(uint32_t));
            copies[f].labels = labels;
        }
        table[f] = (IrInterpreterFunction){
            .symbol = file->functions[f].symbol,
            .function = &copies[f],
            .labels = file->functions[f].labels,
        };
        ready = ready && external_count < original->symbols.count;
        if (ready)
        {
            externals[external_count++] = (InterpreterLinkExternal){ .input = 0, .symbol = file->functions[f].symbol, .address = (uintptr_t)&copies[f] };
        }
    }
    // Each label's symbol, at its token (IrFunction.labels' entry, which br_indirect reads).
    for (uint32_t f = 0; ready && f < count; f += 1)
    {
        for (uint32_t k = 0; ready && k < copies[f].label_count; k += 1)
        {
            ready = external_count < original->symbols.count;
            if (ready)
            {
                externals[external_count++] = (InterpreterLinkExternal){ .input = 0, .symbol = file->functions[f].labels[k], .address = (uintptr_t)&copies[f].labels[k] };
            }
        }
    }
    // What the program takes from the C library and libgcc: each name it refers to and doesn't define.
    void* gcc_s = 0;
    for (SymbolId symbol = 1; ready && symbol < original->symbols.count; symbol += 1)
    {
        const uint8_t flags = original->symbols.flags[symbol];
        const NameId name = original->symbols.name[symbol];
        const bool undefined = original->symbols.atom[symbol] == 0 && original->symbols.source[symbol] != BUSTER_LINK_SOURCE_ABSOLUTE &&
                               (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL && name != 0;
        char spelling[256];
        void* address = 0;
        if (undefined && original->names.length[name] < sizeof(spelling))
        {
            memcpy(spelling, original->names.bytes[name], original->names.length[name]);
            spelling[original->names.length[name]] = 0;
            address = dlsym(RTLD_DEFAULT, spelling);
            if (address == 0)
            {
                gcc_s = gcc_s != 0 ? gcc_s : dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_LOCAL);
                address = gcc_s != 0 ? dlsym(gcc_s, spelling) : 0;
            }
        }
        ready = external_count < original->symbols.count;
        if (address != 0 && ready && (flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION)
        {
            natives[native_count++] = (InterpreterNativeBinding){ .symbol = symbol, .address = address, .flags = ssa_source_native_flags(spelling) };
            externals[external_count++] = (InterpreterLinkExternal){ .input = 0, .symbol = symbol, .address = (uintptr_t)address };
        }
        else if (address != 0 && ready)
        {
            externals[external_count++] = (InterpreterLinkExternal){
                .input = 0,
                .symbol = symbol,
                .address = (uintptr_t)address,
                .size = file->sizes != 0 ? file->sizes[symbol] : 0,
                .writable = true,
            };
        }
    }
    Link data = { 0 };
    InterpreterLink image = { 0 };
    ready = ready && ssa_source_data_link(original, &arena, &data);
    const Link* links[1] = { &data };
    const InterpreterLinkResult materialized = ready ? interpreter_link_materialize(&image, links, 1, externals, external_count, &arena) : (InterpreterLinkResult){ 0 };
    ready = ready && materialized.status == INTERPRETER_LINK_OK;
    // main's arguments: argc 1, argv { "program", 0 } and an empty environment, each in a binding of a symbol of its own
    // past the link's, so that the interpreter knows its memory.
    const uint32_t binding_count = ready ? image.units[0].binding_count : 0;
    InterpreterBinding* bindings = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, InterpreterBinding, (uint64_t)binding_count + 3) : 0;
    char** argv = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, char*, 2) : 0;
    char** envp = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, char*, 1) : 0;
    char* name = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, char, 8) : 0;
    ready = ready && bindings != 0 && argv != 0 && envp != 0 && name != 0;
    InterpreterResult result = { .status = INTERPRETER_RESOURCE };
    if (ready)
    {
        memcpy(bindings, image.units[0].bindings, (uint64_t)binding_count * sizeof(InterpreterBinding));
        memcpy(name, "program", 8);
        argv[0] = name;
        argv[1] = 0;
        envp[0] = 0;
        bindings[binding_count] = (InterpreterBinding){ .symbol = original->symbols.count, .bytes = (uint8_t*)argv, .size = 2 * sizeof(char*), .writable = true };
        bindings[binding_count + 1] = (InterpreterBinding){ .symbol = original->symbols.count + 1, .bytes = (uint8_t*)envp, .size = sizeof(char*), .writable = true };
        bindings[binding_count + 2] = (InterpreterBinding){ .symbol = original->symbols.count + 2, .bytes = (uint8_t*)name, .size = 8, .writable = true };
        const IrInterpreterProgram program = {
            .functions = table,
            .function_count = count,
            .bindings = bindings,
            .binding_count = binding_count + 3,
            .natives = natives,
            .native_count = native_count,
        };
        const InterpreterValue arguments[3] = {
            { .low = 1 },
            { .low = (uintptr_t)argv },
            { .low = (uintptr_t)envp },
        };
        // Natives write the memory the program gives them (strcpy's) without telling the interpreter which bytes they
        // defined, so that it reads an undefined byte's value rather than refuse it; it still checks every access.
        InterpreterOptions options = interpreter_default_options();
        options.check_undefined = false;
        options.max_steps = steps;
        const uint32_t parameters = copies[entry].parameter_count < 3 ? copies[entry].parameter_count : 3;
        result = ir_interpret(&program, file->functions[entry].symbol, arguments, parameters, &options, &arena);
    }
    else if (materialized.status != INTERPRETER_LINK_OK)
    {
        const SymbolId symbol = materialized.symbol < original->symbols.count ? materialized.symbol : 0;
        const NameId name = original->symbols.name[symbol];
        fprintf(stderr, "test-ssa-source: the program's data couldn't be materialized: status %u, symbol %u (%.*s), atom %u, fixup %u\n", materialized.status, symbol,
                name != 0 ? (int)original->names.length[name] : 0, name != 0 ? (const char*)original->names.bytes[name] : "", materialized.atom, materialized.fixup);
        result.status = INTERPRETER_SYMBOL;
    }
    fflush(stdout);
    fflush(stderr);
    const bool written = result_file >= 0 && write(result_file, &result, sizeof(result)) == (ssize_t)sizeof(result);
    _exit(written && result.status == INTERPRETER_OK ? (int)(result.value.low & 0xff) : 255);
}

// Starts `file`'s program in one form (its functions `functions`, main the function `entry`) in a child of its own, whose
// process `run` keeps until ssa_source_finish_form waits for it. `directory`, the form's own (a program may write files),
// is its working directory and holds its output and record.
static void ssa_source_start_form(SsaSourceRun* run, const IrSourceFile* file, const IrFunction* functions, uint32_t entry, const char* directory,
                                  const SsaSourceLimits* limits)
{
    char output[PATH_MAX];
    char record[PATH_MAX];
    snprintf(output, sizeof(output), "%s/ssa-source.out", directory);
    snprintf(record, sizeof(record), "%s/ssa-source.record", directory);
    memset(run, 0, sizeof(*run));
    run->status = -1;
    fflush(stdout);
    fflush(stderr);
    run->child = fork();
    if (run->child == 0)
    {
        ssa_source_child(file, functions, entry, directory, output, record, limits->timeout, limits->steps);
    }
}

// Waits for the child of a run that ssa_source_start_form started in `directory`, and reads its output and record.
static void ssa_source_finish_form(SsaSourceRun* run, const char* directory)
{
    char output[PATH_MAX];
    char record[PATH_MAX];
    snprintf(output, sizeof(output), "%s/ssa-source.out", directory);
    snprintf(record, sizeof(record), "%s/ssa-source.record", directory);
    pid_t waited = run->child > 0 ? 0 : -1;
    while (waited == 0)
    {
        waited = waitpid(run->child, &run->status, 0);
        waited = waited < 0 && errno == EINTR ? 0 : waited;
    }
    run->status = waited == run->child && run->child > 0 ? run->status : -1;
    run->output = c_testsuite_read(output, &run->length);
    uint64_t length = 0;
    char* result = c_testsuite_read(record, &length);
    run->recorded = result != 0 && length == sizeof(InterpreterResult);
    if (run->recorded)
    {
        memcpy(&run->result, result, sizeof(InterpreterResult));
    }
    free(result);
}

// A run's outcome, for the report: how it ended, and what its interpreter said if it returned.
static void ssa_source_describe(const SsaSourceRun* run, char* text, uint64_t capacity)
{
    if (run->recorded && run->result.status != INTERPRETER_OK)
    {
        snprintf(text, capacity, "interpreter %s (function %u, instruction %u, after %" PRIu64 " steps)",
                 ssa_source_statuses[run->result.status <= INTERPRETER_UNSUPPORTED ? run->result.status : INTERPRETER_UNSUPPORTED], run->result.function,
                 run->result.entry, run->result.steps);
    }
    else if (run->status == -1)
    {
        snprintf(text, capacity, "no process");
    }
    else if (WIFSIGNALED(run->status))
    {
        snprintf(text, capacity, "signal %d%s", WTERMSIG(run->status), WTERMSIG(run->status) == SIGALRM ? " (its time ran out)" : "");
    }
    else
    {
        snprintf(text, capacity, "status %d, %" PRIu64 " bytes of output%s", WEXITSTATUS(run->status), run->length, run->recorded ? "" : " (it exited by itself)");
    }
}

// Whether two runs ended alike: their wait statuses, whether their interpreters returned and how, and their output.
static bool ssa_source_same(const SsaSourceRun* a, const SsaSourceRun* b)
{
    const bool results = a->recorded == b->recorded && (!a->recorded || (a->result.status == b->result.status && a->result.value.low == b->result.value.low));
    return a->status == b->status && results && a->output != 0 && b->output != 0 && a->length == b->length && memcmp(a->output, b->output, a->length) == 0;
}

// Runs the program in its three forms at once, each in a directory of its own under `work` (run-INDEX-FORM), and compares
// them with each other and with `expected`, if any: whether they agree and give it, with each form's interpreter's steps
// in `steps` (BUSTER_SSA_SOURCE_FORMS of them). The directories go unless the run fails or `keep`.
static bool ssa_source_execute(const IrSourceFile* file, IrFunction* const* forms, uint32_t entry, const SsaSourceExpected* expected, const char* name, const char* work,
                               uint64_t index, const SsaSourceLimits* limits, bool keep, uint64_t* steps, int* statuses)
{
    char directories[BUSTER_SSA_SOURCE_FORMS][PATH_MAX];
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/run-%" PRIu64 "-*", work, index);
    bool passed = true;
    for (uint32_t form = 0; form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        snprintf(directories[form], sizeof(directories[form]), "%s/run-%" PRIu64 "-%s", work, index, ssa_source_forms[form]);
        passed = passed && mkdir(directories[form], 0755) == 0;
    }
    if (!passed)
    {
        printf("test-ssa-source: file=%s its runs' directories couldn't be made (%s)\n", name, directory);
    }
    SsaSourceRun runs[BUSTER_SSA_SOURCE_FORMS];
    memset(runs, 0, sizeof(runs));
    for (uint32_t form = 0; passed && form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        ssa_source_start_form(&runs[form], file, forms[form], entry, directories[form], limits);
    }
    for (uint32_t form = 0; passed && form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        ssa_source_finish_form(&runs[form], directories[form]);
    }
    *steps = runs[0].result.steps;
    steps[1] = runs[1].result.steps;
    steps[2] = runs[2].result.steps;
    for (uint32_t form = 0; statuses != 0 && form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        statuses[form] = runs[form].status;
    }
    char text[256];
    // Matching alarms or other signals do not finish a comparison. A normal native exit may legitimately leave no
    // interpreter record; keep it, but reject every signaled/unwaited form and every recorded interpreter failure.
    for (uint32_t form = 0; passed && form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        if (runs[form].status == -1 || !WIFEXITED(runs[form].status) || (runs[form].recorded && runs[form].result.status != INTERPRETER_OK))
        {
            ssa_source_describe(&runs[form], text, sizeof(text));
            printf("test-ssa-source: file=%s the %s form's run didn't finish: %s (%s)\n", name, ssa_source_forms[form], text, directory);
            passed = false;
        }
    }
    for (uint32_t form = 1; passed && form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        if (!ssa_source_same(&runs[0], &runs[form]))
        {
            char other[256];
            ssa_source_describe(&runs[0], text, sizeof(text));
            ssa_source_describe(&runs[form], other, sizeof(other));
            printf("test-ssa-source: file=%s the %s form's run differs from the memory form's: %s against %s (%s)\n", name, ssa_source_forms[form], other, text, directory);
            passed = false;
        }
    }
    if (passed && expected != 0)
    {
        const bool output = runs[0].output != 0 && runs[0].length == expected->length && memcmp(runs[0].output, expected->output, expected->length) == 0;
        if (runs[0].status != expected->status || !output)
        {
            ssa_source_describe(&runs[0], text, sizeof(text));
            printf("test-ssa-source: file=%s its runs give %s, not the expected status %d and %" PRIu64 " bytes of output%s (%s)\n", name, text,
                   WIFEXITED(expected->status) ? WEXITSTATUS(expected->status) : -1, expected->length, output ? "" : ", or other output", directory);
            passed = false;
        }
    }
    for (uint32_t form = 0; form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        free(runs[form].output);
    }
    for (uint32_t form = 0; passed && !keep && form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        c_testsuite_remove_directory(directories[form]);
    }
    return passed;
}

// The arenas a file's check takes: the preparation's (the typed streams and the link, and the tables), each form's IR,
// and the promotions' scratch. Each is emptied for the next file.
typedef struct SsaSourceArenas SsaSourceArenas;
struct SsaSourceArenas
{
    Arena lowered;
    Arena tables;
    Arena forms[BUSTER_SSA_SOURCE_FORMS];
    Arena scratch;
};

// Checks `name` (a file read with `options`): its functions lowered and promoted by both paths with the SSA checks, and
// with a main, its program run in the three forms against `expected` (or, without, FILE.expected if there is one), into
// `totals`, with the file's line and a line per failure. A file the preparation leaves out is counted and passes.
static void ssa_source_file(SsaSourceTotals* totals, const char* name, char** options, uint32_t option_count, const SsaSourceExpected* expected, SsaSourceArenas* arenas,
                            const char* work, const SsaSourceLimits* limits, bool keep, int* statuses)
{
    IrSourceFile file;
    ir_source_prepare(&file, name, options, option_count, &arenas->lowered, &arenas->tables, true);
    if (file.status != IR_SOURCE_PREPARED)
    {
        totals->left_out += file.status == IR_SOURCE_LEFT_OUT;
        totals->failed_files += file.status == IR_SOURCE_FAILED;
        printf("test-ssa-source: file=%s %s=\"%s\"\n", name, file.status == IR_SOURCE_LEFT_OUT ? "left_out" : "failed", file.reason);
    }
    IrFunction* forms[BUSTER_SSA_SOURCE_FORMS] = { 0 };
    bool checked = file.status == IR_SOURCE_PREPARED;
    for (uint32_t form = 0; checked && form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        forms[form] = BUSTER_ARENA_PUSH_ARRAY(&arenas->tables, IrFunction, (uint64_t)file.count + 1);
        checked = forms[form] != 0;
    }
    if (file.status == IR_SOURCE_PREPARED && !checked)
    {
        totals->failed_files += 1;
        printf("test-ssa-source: file=%s failed=\"memory ran out for its functions' IR\"\n", name);
    }
    uint64_t instructions = 0;
    uint64_t phis[2] = { 0, 0 };
    uint32_t entry = UINT32_MAX;
    uint32_t failures = 0;
    for (uint32_t f = 0; checked && f < file.count; f += 1)
    {
        const IrSourceFunction* source = &file.functions[f];
        const IrCheck lowered = ir_lower_typed(source->function, source->result_type, source->symbol_count, &arenas->forms[BUSTER_SSA_SOURCE_MEMORY], &arenas->scratch,
                                               &forms[BUSTER_SSA_SOURCE_MEMORY][f]);
        bool promoted = lowered.error == IR_OK;
        IrCheck failed = lowered;
        const char* rule = "lowering";
        uint32_t path_failed = BUSTER_SSA_SOURCE_MEMORY;
        for (uint32_t path = 0; promoted && path < 2; path += 1)
        {
            const uint32_t form = path == 0 ? BUSTER_SSA_SOURCE_VECTOR : BUSTER_SSA_SOURCE_SCALAR;
            SsaCheck verified;
            const IrCheck promotion = ssa_ir_promote_verified(&forms[BUSTER_SSA_SOURCE_MEMORY][f], path == 0, &arenas->forms[form], &arenas->scratch, &forms[form][f], &verified);
            uint32_t trivial = 0;
            const uint32_t count = promotion.error == IR_OK ? ssa_ir_phis(&forms[form][f], &trivial) : 0;
            promoted = promotion.error == IR_OK && verified.error == SSA_OK && trivial == 0;
            if (promoted)
            {
                phis[path] += count;
                totals->promoted[path] += forms[form][f].instruction_count;
            }
            else
            {
                failed = promotion;
                rule = verified.error != SSA_OK ? verified.rule : promotion.error != IR_OK ? "the promotion" : "a trivial phi left";
                path_failed = form;
            }
        }
        if (promoted)
        {
            instructions += forms[BUSTER_SSA_SOURCE_MEMORY][f].instruction_count;
            entry = source->name_length == 4 && memcmp(source->name, "main", 4) == 0 ? f : entry;
        }
        else
        {
            failures += 1;
            printf("test-ssa-source: file=%s function=%s %s form: %s failed (error=%s instruction=%u)\n", name, source->name, ssa_source_forms[path_failed],
                   rule != 0 ? rule : "the SSA checks", ir_source_errors[failed.error <= IR_ERROR_IO ? failed.error : IR_ERROR_IO], failed.instruction);
        }
    }
    totals->failures += failures;
    if (checked)
    {
        totals->files += 1;
        totals->functions += file.count;
        totals->instructions += instructions;
        totals->phis[0] += phis[0];
        totals->phis[1] += phis[1];
    }
    // The program's run, where every function promoted and it has a main.
    const char* outcome = "none";
    char counted[96] = { 0 };
    if (checked && failures == 0 && entry != UINT32_MAX)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s.expected", name);
        uint64_t length = 0;
        char* bytes = expected == 0 ? c_testsuite_read(path, &length) : 0;
        const SsaSourceExpected from_file = { .output = bytes, .length = length, .status = 0 };
        const SsaSourceExpected* wanted = expected != 0 ? expected : bytes != 0 ? &from_file : 0;
        uint64_t steps[BUSTER_SSA_SOURCE_FORMS] = { 0 };
        const bool passed = ssa_source_execute(&file, forms, entry, wanted, name, work, totals->programs, limits, keep, steps, statuses);
        totals->programs += 1;
        totals->passed += passed;
        snprintf(counted, sizeof(counted), " steps=%" PRIu64 "/%" PRIu64 "/%" PRIu64, steps[0], steps[1], steps[2]);
        for (uint32_t form = 0; form < BUSTER_SSA_SOURCE_FORMS; form += 1)
        {
            totals->steps[form] += steps[form];
        }
        outcome = passed ? (wanted != 0 ? "as expected" : "alike") : "failed";
        free(bytes);
    }
    if (checked)
    {
        printf("test-ssa-source: file=%s functions=%u instructions=%" PRIu64 " phis=%" PRIu64 "/%" PRIu64 " failures=%u run=%s%s\n", name, file.count, instructions,
               phis[0], phis[1], failures, outcome, counted);
    }
    fflush(stdout);
    arena_pop_to(&arenas->lowered, 0);
    arena_pop_to(&arenas->tables, 0);
    for (uint32_t form = 0; form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        arena_pop_to(&arenas->forms[form], 0);
    }
}

// clang -O0's object of `source`, through the oracle cache: whether it compiled.
static bool ssa_source_compile(const char* clang, const char* source, const char* object)
{
    char* arguments[] = { (char*)clang, "-O0", "-w", "-c", (char*)source, "-o", (char*)object, 0 };
    const char* files[] = { object };
    OracleRun run = {
        .purpose = "ssa-source: clang -O0 -c",
        .arguments = arguments,
        .files = files,
        .file_count = 1,
        .dependencies = true,
    };
    const bool ran = oracle_run(&run);
    fputs(run.output != 0 ? run.output : "", stderr);
    fputs(run.errors != 0 ? run.errors : "", stderr);
    const bool compiled = ran && WIFEXITED(run.status) && WEXITSTATUS(run.status) == 0;
    oracle_run_release(&run);
    return compiled;
}

// The generated programs: `count` of them from `seed` on, each written into `work`, built by clang -O0 and run for what
// it must print, then checked as a file is.
static void ssa_source_generated(SsaSourceTotals* totals, uint64_t seed, uint32_t count, const char* clang, char** options, uint32_t option_count, SsaSourceArenas* arenas,
                                 const char* work, const SsaSourceLimits* limits, bool keep)
{
    SsaGenerator* generator = malloc(sizeof(SsaGenerator));
    char* text = malloc(BUSTER_SSA_SOURCE_TEXT);
    for (uint32_t p = 0; p < count; p += 1)
    {
        const uint64_t program_seed = seed * 1000003ull + p;
        char source[PATH_MAX];
        char object[PATH_MAX];
        char executable[PATH_MAX];
        char log[PATH_MAX];
        char output[PATH_MAX];
        snprintf(source, sizeof(source), "%s/program_%u.c", work, p);
        snprintf(object, sizeof(object), "%s/program_%u.o", work, p);
        snprintf(executable, sizeof(executable), "%s/program_%u", work, p);
        snprintf(log, sizeof(log), "%s/program_%u.log", work, p);
        snprintf(output, sizeof(output), "%s/program_%u.out", work, p);
        const uint64_t length = generator != 0 && text != 0 ? ssa_generate_program(generator, text, BUSTER_SSA_SOURCE_TEXT, program_seed) : 0;
        FILE* written = length != 0 ? fopen(source, "w") : 0;
        bool built = written != 0 && fwrite(text, 1, length, written) == length;
        built = written != 0 && fclose(written) == 0 && built;
        built = built && ssa_source_compile(clang, source, object);
        bool timed_out = false;
        char* link[] = { (char*)clang, "-o", executable, object, 0 };
        built = built && c_testsuite_run(link, work, log, limits->timeout, &timed_out) == 0;
        char* run[] = { executable, 0 };
        const int status = built ? c_testsuite_run(run, work, output, limits->timeout, &timed_out) : -1;
        uint64_t bytes = 0;
        char* expected_output = built && !timed_out ? c_testsuite_read(output, &bytes) : 0;
        totals->generated += 1;
        if (expected_output == 0)
        {
            totals->unbuilt += 1;
            printf("test-ssa-source: program of seed %" PRIu64 " (%s): clang's build couldn't be made or run\n", program_seed, source);
        }
        else
        {
            const SsaSourceExpected expected = { .output = expected_output, .length = bytes, .status = status };
            ssa_source_file(totals, source, options, option_count, &expected, arenas, work, limits, keep, 0);
        }
        free(expected_output);
    }
    free(text);
    free(generator);
}

// These controls use the actual C preparation, both promotions and the three child-process execution boundary, with
// no expected-output sidecar. In particular, the timeout must be three real SIGALRM outcomes with empty records, not
// a fabricated wait status or a direct test of the acceptance predicate. Keep the directories until the controls have
// checked every record; an unexpected failure keeps the whole work directory through the command's usual cleanup.
static bool ssa_source_execution_tests(SsaSourceArenas* arenas, const char* work)
{
    typedef struct SsaSourceControl SsaSourceControl;
    struct SsaSourceControl
    {
        const char* name;
        const char* source;
        SsaSourceLimits limits;
        int exit_status;
        int signal;
        bool recorded;
        InterpreterStatus result;
        bool passed;
    };
    const SsaSourceControl controls[] = {
        {
            .name = "normal-return",
            .source = "int main(void) { return 7; }\n",
            .limits = { .timeout = 10, .steps = BUSTER_SSA_SOURCE_STEPS },
            .exit_status = 7,
            .recorded = true,
            .result = INTERPRETER_OK,
            .passed = true,
        },
        {
            .name = "native-exit",
            .source = "extern void exit(int); int main(void) { exit(23); return 0; }\n",
            .limits = { .timeout = 10, .steps = BUSTER_SSA_SOURCE_STEPS },
            .exit_status = 23,
            .passed = true,
        },
        {
            .name = "three-timeouts",
            .source = "int main(void) { for (;;) {} }\n",
            .limits = { .timeout = 1, .steps = UINT64_MAX },
            .signal = SIGALRM,
        },
        {
            .name = "interpreter-step-limit",
            .source = "int main(void) { for (;;) {} }\n",
            .limits = { .timeout = 10, .steps = 1 },
            .exit_status = 255,
            .recorded = true,
            .result = INTERPRETER_STEP_LIMIT,
        },
    };
    bool passed = true;
    for (uint32_t control = 0; control < sizeof(controls) / sizeof(controls[0]); control += 1)
    {
        const SsaSourceControl* test = &controls[control];
        char directory[PATH_MAX];
        char source[PATH_MAX];
        snprintf(directory, sizeof(directory), "%s/control-%u", work, control);
        snprintf(source, sizeof(source), "%s/program.c", directory);
        const bool made = mkdir(directory, 0755) == 0;
        FILE* stream = made ? fopen(source, "w") : 0;
        const uint64_t length = strlen(test->source);
        bool checked = stream != 0 && fwrite(test->source, 1, length, stream) == length;
        checked = stream != 0 && fclose(stream) == 0 && checked;
        SsaSourceTotals totals = { 0 };
        int statuses[BUSTER_SSA_SOURCE_FORMS] = { -1, -1, -1 };
        if (checked)
        {
            ssa_source_file(&totals, source, 0, 0, 0, arenas, directory, &test->limits, true, statuses);
            checked = totals.files == 1 && totals.failed_files == 0 && totals.failures == 0 && totals.functions == 1 && totals.programs == 1 &&
                      totals.passed == (uint64_t)test->passed;
        }
        for (uint32_t form = 0; checked && form < BUSTER_SSA_SOURCE_FORMS; form += 1)
        {
            char record[PATH_MAX];
            snprintf(record, sizeof(record), "%s/run-0-%s/ssa-source.record", directory, ssa_source_forms[form]);
            uint64_t bytes = 0;
            char* result = c_testsuite_read(record, &bytes);
            const bool status = test->signal != 0 ? WIFSIGNALED(statuses[form]) && WTERMSIG(statuses[form]) == test->signal :
                                                  WIFEXITED(statuses[form]) && WEXITSTATUS(statuses[form]) == test->exit_status;
            checked = status && result != 0 && bytes == (test->recorded ? sizeof(InterpreterResult) : 0);
            if (checked && test->recorded)
            {
                InterpreterResult outcome;
                memcpy(&outcome, result, sizeof(outcome));
                checked = outcome.status == test->result;
            }
            free(result);
        }
        printf("test-ssa-source: self-test=%s %s statuses=%d/%d/%d\n", test->name, checked ? "passed" : "FAILED", statuses[0], statuses[1], statuses[2]);
        passed = checked && passed;
    }
    return passed;
}

static int run_ssa_source_tests(int count, char** arguments)
{
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    uint64_t generate = 0;
    uint64_t seed = 134;
    uint64_t timeout = BUSTER_SSA_SOURCE_TIMEOUT;
    uint64_t steps = BUSTER_SSA_SOURCE_STEPS;
    const char* clang = "clang";
    bool keep = false;
    bool self_test = false;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--self-test") == 0)
        {
            self_test = true;
        }
        else if (strcmp(arguments[i], "--generate") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &generate) && generate <= 100000;
        }
        else if (strcmp(arguments[i], "--seed") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &seed);
        }
        else if (strcmp(arguments[i], "--timeout") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &timeout) && timeout != 0 && timeout <= 3600;
        }
        else if (strcmp(arguments[i], "--steps") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &steps) && steps != 0;
        }
        else if (strcmp(arguments[i], "--clang") == 0)
        {
            valid = i + 1 < count;
            clang = valid ? arguments[++i] : clang;
        }
        else if (strcmp(arguments[i], "--keep") == 0)
        {
            keep = true;
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            // An option and its value, which pp_parse_options reads with each file, as test-ir-source takes them.
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    valid = valid && (self_test ? file_count == 0 && generate == 0 : file_count != 0 || generate != 0);
    SsaSourceArenas arenas = {
        .lowered = arena_reserve(UINT64_C(1) << 36),
        .tables = arena_reserve(UINT64_C(1) << 32),
        .scratch = arena_reserve(UINT64_C(1) << 34),
    };
    for (uint32_t form = 0; form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        arenas.forms[form] = arena_reserve(UINT64_C(1) << 34);
        valid = valid && arenas.forms[form].base != 0;
    }
    valid = valid && arenas.lowered.base != 0 && arenas.tables.base != 0 && arenas.scratch.base != 0;
    const char* temporary = getenv("TMPDIR");
    char work[PATH_MAX];
    snprintf(work, sizeof(work), "%s/ssa-source-XXXXXX", temporary != 0 && temporary[0] == '/' ? temporary : "/tmp");
    valid = valid && mkdtemp(work) != 0;
    const bool epoch = valid && oracle_epoch_begin();
    const SsaSourceLimits limits = { .timeout = (uint32_t)timeout, .steps = steps };
    SsaSourceTotals totals = { 0 };
    const bool controls = !valid || !self_test || ssa_source_execution_tests(&arenas, work);
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        ssa_source_file(&totals, files[i], options, option_count, 0, &arenas, work, &limits, keep, 0);
    }
    if (valid && generate != 0)
    {
        ssa_source_generated(&totals, seed, (uint32_t)generate, clang, options, option_count, &arenas, work, &limits, keep);
    }
    const bool passed = valid && controls && totals.failed_files == 0 && totals.failures == 0 && totals.passed == totals.programs && totals.unbuilt == 0 &&
                        (self_test || totals.functions != 0);
    if (valid)
    {
        printf("test-ssa-source: totals files=%" PRIu64 " left_out=%" PRIu64 " failed=%" PRIu64 " functions=%" PRIu64 " instructions=%" PRIu64
               " promoted=%" PRIu64 "/%" PRIu64 " phis=%" PRIu64 "/%" PRIu64 " failures=%" PRIu64 " programs=%" PRIu64 " passed=%" PRIu64 " steps=%" PRIu64
               "/%" PRIu64 "/%" PRIu64 " generated=%" PRIu64 " unbuilt=%" PRIu64 "\n",
               totals.files, totals.left_out, totals.failed_files, totals.functions, totals.instructions, totals.promoted[0], totals.promoted[1], totals.phis[0],
               totals.phis[1], totals.failures, totals.programs, totals.passed, totals.steps[0], totals.steps[1], totals.steps[2], totals.generated, totals.unbuilt);
        oracle_cache_report(oracle_cache(), "test-ssa-source");
        if (!keep && passed)
        {
            c_testsuite_remove_directory(work);
        }
        else
        {
            printf("test-ssa-source: files kept in %s\n", work);
        }
    }
    else
    {
        fprintf(stderr, "usage: compiler test-ssa-source [--generate N] [--seed N] [--clang PATH] [--timeout SECONDS] [--steps N] [--keep] [preprocessor options] "
                        "[file...]\n       compiler test-ssa-source --self-test [--keep]\n");
    }
    oracle_epoch_end(epoch);
    for (uint32_t form = 0; form < BUSTER_SSA_SOURCE_FORMS; form += 1)
    {
        arena_release(&arenas.forms[form]);
    }
    arena_release(&arenas.scratch);
    arena_release(&arenas.tables);
    arena_release(&arenas.lowered);
    free(files);
    free(options);
    return passed ? 0 : 1;
}
