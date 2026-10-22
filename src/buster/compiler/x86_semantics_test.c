// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The tests of x86_semantics.c (test-x86-semantics). The processor is the oracle: a harness runs records in this process
// (x86_semantics_test_enter loads every general register, the flags and ZMM0 to ZMM15 from a state, runs the code and
// stores them back; memory operands are RIP-relative into a data page, and RSP points into a stack of the harness's own),
// so a table's claim about a form is checked against what the instruction does, not against another table:
//   - the effects table: every covered form but those that branch or trap, from random states, its operands registers
//     and memory: what it doesn't write stays as it was (registers, vector registers to bit 511, flags, the bytes of
//     memory around an operand), a narrower write keeps or zeroes what its rest says, defined flags and results don't
//     change when everything it doesn't read changes, and every write changes something in some run;
//   - fixtures of the effects the issue names (division, variable shifts, partial writes, 32-bit zero extension, inc's
//     CF, the SSE merges, a call's architectural effects, register and memory operands), and a sample of uncovered forms,
//     which must get the conservative record;
//   - the immediate's contract on every covered form with an immediate and every boundary value: a field that encodes a
//     value gives it back (x86_immediate_value), one that doesn't can't;
//   - form choice: fixtures of the boundary values -129 to 256 at each width, constant materialization, shift counts
//     and the substitutions with their live flags; then every row with every boundary value, contexts and destinations,
//     each chosen record validated and run against a reference sequence that holds the value in R11 (movabs, whose field
//     is the value itself) and uses the operation's register form, which must leave the same registers, memory and live
//     flags (and a record with an imm8 of 0xc8 for 200 must not: the check is sensitive to it);
//   - the vector paths against their scalar twins (x86_choose_forms and x86_instructions_effects) on random inputs, and
//     with their columns ending at a PROT_NONE page;
//   - x86_match.c's immediate ranges against the shared rule on the forms the assembler's syntax supports;
//   - #180's check: every row's forms allowed at the baseline target, and every chosen record valid there;
//   - with --cross-check, GNU as: the chosen records printed (x86_print_instruction) must assemble to the bytes of the
//     chosen forms.
// Usage: build/compiler test-x86-semantics [--seed N] [--runs N] [--cross-check] [--keep]. Exits with status 1 on any
// failure, or if a run faults (the fault handler names the record).
//
// Layout: the tally; the harness (X86SemanticsState, x86_semantics_test_enter, X86SemanticsMachine); the fixtures of the
// effects; the effects table's execution check; the immediate's contract; form choice (its fixtures, the reference
// sequences, the semantic check); the vector paths' twins; x86_match's agreement; GNU as; run_x86_semantics_tests.

#include <stdarg.h>
#include <stddef.h>
#include <signal.h>

#define BUSTER_X86_SEMANTICS_TEST_REPORTED 12
#define BUSTER_X86_SEMANTICS_TEST_PAGE 4096
// The data page's memory operand (aligned for the 16-byte forms that need it), and the stack's top, in the middle of its
// two pages.
#define BUSTER_X86_SEMANTICS_TEST_OPERAND 1024
#define BUSTER_X86_SEMANTICS_TEST_STACK_TOP (BUSTER_X86_SEMANTICS_TEST_PAGE + 2048)
#define BUSTER_X86_SEMANTICS_TEST_RUNS 12
// A write the runs didn't show gets up to this many more, until it shows (x86_semantics_check_table).
#define BUSTER_X86_SEMANTICS_TEST_WITNESS_RUNS 256
#define BUSTER_X86_SEMANTICS_TEST_RFLAGS_BASE 0x202ull

#define BUSTER_X86_SEMANTICS_TEST_NAME(name, ...) #name,
static const char* const x86_semantics_test_form_names[X86_FORM_COUNT] = { BUSTER_X86_FORMS(BUSTER_X86_SEMANTICS_TEST_NAME, BUSTER_X86_SEMANTICS_TEST_NAME) };
#undef BUSTER_X86_SEMANTICS_TEST_NAME

static const char* x86_semantics_test_name(uint32_t form)
{
    return x86_semantics_test_form_names[x86_semantics_row(form)];
}

// ---------------------------------------------------------------------------------------------------------------------
// The tally

typedef struct X86SemanticsTally X86SemanticsTally;
struct X86SemanticsTally
{
    const char* check;
    uint64_t checks;
    uint64_t failures;
};

// Counts a check and, on a failure among the first BUSTER_X86_SEMANTICS_TEST_REPORTED, prints what failed.
static __attribute__((format(printf, 3, 4))) bool x86_semantics_expect(X86SemanticsTally* tally, bool condition, const char* format, ...)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        if (tally->failures <= BUSTER_X86_SEMANTICS_TEST_REPORTED)
        {
            va_list arguments;
            va_start(arguments, format);
            printf("    %s: ", tally->check);
            vprintf(format, arguments);
            printf("\n");
            va_end(arguments);
        }
    }
    return condition;
}

static bool x86_semantics_report(const X86SemanticsTally* tally)
{
    printf("%s: %llu checks, %llu failures\n", tally->check, (unsigned long long)tally->checks, (unsigned long long)tally->failures);
    return tally->failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The harness

// What the harness loads into the processor and stores back: the general registers (RSP the stack's), RFLAGS and ZMM0 to
// ZMM15. x86_semantics_test_enter's offsets are these.
typedef struct X86SemanticsState X86SemanticsState;
struct X86SemanticsState
{
    uint64_t gpr[16];
    uint64_t flags;
    uint64_t padding[7];
    uint8_t vector[16][64];
};
_Static_assert(offsetof(X86SemanticsState, flags) == 128, "x86_semantics_test_enter's offset of the flags");
_Static_assert(offsetof(X86SemanticsState, vector) == 192, "x86_semantics_test_enter's offset of the vector registers");

// The trampoline's own state, which it reaches RIP-relative while every register is the test's.
static __attribute__((used)) uint64_t x86_semantics_test_saved_rsp;
static __attribute__((used)) X86SemanticsState* x86_semantics_test_state;
static __attribute__((used)) uint64_t x86_semantics_test_code;
static __attribute__((used)) uint64_t x86_semantics_test_scratch;

// Loads the state (ZMM0 to ZMM15, the flags, then the general registers, RSP last but RDI, which held the state) and jumps
// to x86_semantics_test_code, which ends with a jump back to x86_semantics_test_return. There it stores the general
// registers, the flags (pushed on the C stack, after RSP is stored) and the vector registers into the state, restores the
// C stack and the callee-saved registers, and returns. The test code may change any register but the trampoline's
// statics, and may use the stack the state's RSP points at.
static __attribute__((naked, noinline)) void x86_semantics_test_enter(X86SemanticsState* state)
{
    __asm__ volatile(".intel_syntax noprefix\n"
                     "push rbx\n"
                     "push rbp\n"
                     "push r12\n"
                     "push r13\n"
                     "push r14\n"
                     "push r15\n"
                     "mov qword ptr [rip + x86_semantics_test_saved_rsp], rsp\n"
                     "mov qword ptr [rip + x86_semantics_test_state], rdi\n"
                     "vmovdqu64 zmm0, zmmword ptr [rdi + 192]\n"
                     "vmovdqu64 zmm1, zmmword ptr [rdi + 256]\n"
                     "vmovdqu64 zmm2, zmmword ptr [rdi + 320]\n"
                     "vmovdqu64 zmm3, zmmword ptr [rdi + 384]\n"
                     "vmovdqu64 zmm4, zmmword ptr [rdi + 448]\n"
                     "vmovdqu64 zmm5, zmmword ptr [rdi + 512]\n"
                     "vmovdqu64 zmm6, zmmword ptr [rdi + 576]\n"
                     "vmovdqu64 zmm7, zmmword ptr [rdi + 640]\n"
                     "vmovdqu64 zmm8, zmmword ptr [rdi + 704]\n"
                     "vmovdqu64 zmm9, zmmword ptr [rdi + 768]\n"
                     "vmovdqu64 zmm10, zmmword ptr [rdi + 832]\n"
                     "vmovdqu64 zmm11, zmmword ptr [rdi + 896]\n"
                     "vmovdqu64 zmm12, zmmword ptr [rdi + 960]\n"
                     "vmovdqu64 zmm13, zmmword ptr [rdi + 1024]\n"
                     "vmovdqu64 zmm14, zmmword ptr [rdi + 1088]\n"
                     "vmovdqu64 zmm15, zmmword ptr [rdi + 1152]\n"
                     "push qword ptr [rdi + 128]\n"
                     "popfq\n"
                     "mov rax, qword ptr [rdi + 0]\n"
                     "mov rcx, qword ptr [rdi + 8]\n"
                     "mov rdx, qword ptr [rdi + 16]\n"
                     "mov rbx, qword ptr [rdi + 24]\n"
                     "mov rbp, qword ptr [rdi + 40]\n"
                     "mov rsi, qword ptr [rdi + 48]\n"
                     "mov r8, qword ptr [rdi + 64]\n"
                     "mov r9, qword ptr [rdi + 72]\n"
                     "mov r10, qword ptr [rdi + 80]\n"
                     "mov r11, qword ptr [rdi + 88]\n"
                     "mov r12, qword ptr [rdi + 96]\n"
                     "mov r13, qword ptr [rdi + 104]\n"
                     "mov r14, qword ptr [rdi + 112]\n"
                     "mov r15, qword ptr [rdi + 120]\n"
                     "mov rsp, qword ptr [rdi + 32]\n"
                     "mov rdi, qword ptr [rdi + 56]\n"
                     "jmp qword ptr [rip + x86_semantics_test_code]\n"
                     "x86_semantics_test_return:\n"
                     "mov qword ptr [rip + x86_semantics_test_scratch], rdi\n"
                     "mov rdi, qword ptr [rip + x86_semantics_test_state]\n"
                     "mov qword ptr [rdi + 32], rsp\n"
                     "mov rsp, qword ptr [rip + x86_semantics_test_saved_rsp]\n"
                     "pushfq\n"
                     "pop qword ptr [rdi + 128]\n"
                     "mov qword ptr [rdi + 0], rax\n"
                     "mov qword ptr [rdi + 8], rcx\n"
                     "mov qword ptr [rdi + 16], rdx\n"
                     "mov qword ptr [rdi + 24], rbx\n"
                     "mov qword ptr [rdi + 40], rbp\n"
                     "mov qword ptr [rdi + 48], rsi\n"
                     "mov qword ptr [rdi + 64], r8\n"
                     "mov qword ptr [rdi + 72], r9\n"
                     "mov qword ptr [rdi + 80], r10\n"
                     "mov qword ptr [rdi + 88], r11\n"
                     "mov qword ptr [rdi + 96], r12\n"
                     "mov qword ptr [rdi + 104], r13\n"
                     "mov qword ptr [rdi + 112], r14\n"
                     "mov qword ptr [rdi + 120], r15\n"
                     "mov rax, qword ptr [rip + x86_semantics_test_scratch]\n"
                     "mov qword ptr [rdi + 56], rax\n"
                     "vmovdqu64 zmmword ptr [rdi + 192], zmm0\n"
                     "vmovdqu64 zmmword ptr [rdi + 256], zmm1\n"
                     "vmovdqu64 zmmword ptr [rdi + 320], zmm2\n"
                     "vmovdqu64 zmmword ptr [rdi + 384], zmm3\n"
                     "vmovdqu64 zmmword ptr [rdi + 448], zmm4\n"
                     "vmovdqu64 zmmword ptr [rdi + 512], zmm5\n"
                     "vmovdqu64 zmmword ptr [rdi + 576], zmm6\n"
                     "vmovdqu64 zmmword ptr [rdi + 640], zmm7\n"
                     "vmovdqu64 zmmword ptr [rdi + 704], zmm8\n"
                     "vmovdqu64 zmmword ptr [rdi + 768], zmm9\n"
                     "vmovdqu64 zmmword ptr [rdi + 832], zmm10\n"
                     "vmovdqu64 zmmword ptr [rdi + 896], zmm11\n"
                     "vmovdqu64 zmmword ptr [rdi + 960], zmm12\n"
                     "vmovdqu64 zmmword ptr [rdi + 1024], zmm13\n"
                     "vmovdqu64 zmmword ptr [rdi + 1088], zmm14\n"
                     "vmovdqu64 zmmword ptr [rdi + 1152], zmm15\n"
                     "cld\n"
                     "pop r15\n"
                     "pop r14\n"
                     "pop r13\n"
                     "pop r12\n"
                     "pop rbp\n"
                     "pop rbx\n"
                     "vzeroupper\n"
                     "ret\n"
                     ".att_syntax prefix\n");
}

// The address the test code jumps back to.
static uint64_t x86_semantics_test_return_address(void)
{
    uint64_t address;
    __asm__("leaq x86_semantics_test_return(%%rip), %0" : "=r"(address));
    return address;
}

// The harness's mapping: a page of code, a page of data (the memory operands' bytes) and two of stack, read, written and
// executed in place (RIP-relative operands reach the data from the code).
typedef struct X86SemanticsMachine X86SemanticsMachine;
struct X86SemanticsMachine
{
    uint8_t* base;
    uint8_t* code;
    uint8_t* data;
    uint8_t* stack;
    uint64_t length;
};

// What the fault handler prints: the record being run.
static char x86_semantics_test_case[256];

// The forms #180's baseline target (a zero X86Target) allows (x86_target_forms), which run_x86_semantics_tests fills.
static uint8_t x86_semantics_test_baseline[X86_FORM_COUNT];

static void x86_semantics_test_fault(int signal_number)
{
    char message[400];
    const int length = snprintf(message, sizeof(message), "test-x86-semantics: signal %d running %s\n", signal_number, x86_semantics_test_case);
    if (length > 0 && write(STDOUT_FILENO, message, (size_t)length) < 0)
    {
        _exit(1);
    }
    _exit(1);
}

static bool x86_semantics_machine_open(X86SemanticsMachine* machine)
{
    static uint8_t signal_stack[1 << 16];
    const uint64_t length = 4 * BUSTER_X86_SEMANTICS_TEST_PAGE;
    uint8_t* base = mmap(0, length, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = base != MAP_FAILED;
    if (result)
    {
        *machine = (X86SemanticsMachine){
            .base = base,
            .code = base,
            .data = base + BUSTER_X86_SEMANTICS_TEST_PAGE,
            .stack = base + 2 * BUSTER_X86_SEMANTICS_TEST_PAGE,
            .length = length,
        };
        const stack_t alternate = {
            .ss_sp = signal_stack,
            .ss_size = sizeof(signal_stack),
            .ss_flags = 0,
        };
        struct sigaction action = { 0 };
        action.sa_handler = x86_semantics_test_fault;
        action.sa_flags = SA_ONSTACK;
        sigemptyset(&action.sa_mask);
        result = sigaltstack(&alternate, 0) == 0;
        const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP };
        for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(signals); i += 1)
        {
            result = sigaction(signals[i], &action, 0) == 0;
        }
    }
    return result;
}

static void x86_semantics_machine_close(X86SemanticsMachine* machine)
{
    if (machine->base != 0)
    {
        munmap(machine->base, machine->length);
    }
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGFPE, SIG_DFL);
    signal(SIGTRAP, SIG_DFL);
}

// Puts the records into the code page, each RIP-relative operand's displacement (an offset into the data page) made
// relative to its instruction's end, then a jump back to the harness (jmp [rip], and the address). Returns false if a
// record doesn't encode.
static bool x86_semantics_machine_load(X86SemanticsMachine* machine, const X86Instruction* records, uint32_t count)
{
    X86Instruction patched[8];
    uint32_t offsets[8];
    bool result = count <= BUSTER_ARRAY_LENGTH(patched);
    int64_t length = result ? x86_encode_scalar(records, count, machine->code, BUSTER_X86_SEMANTICS_TEST_PAGE, offsets, 0) : -1;
    result = length >= 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        patched[i] = records[i];
        if ((records[i].mode & BUSTER_X86_MODE_RIP) == BUSTER_X86_MODE_RIP && (records[i].mode & BUSTER_X86_MODE_MEMORY))
        {
            const uint64_t end = i + 1 < count ? offsets[i + 1] : (uint64_t)length;
            patched[i].displacement = (int32_t)((int64_t)(machine->data + (uint32_t)records[i].displacement) - (int64_t)(machine->code + end));
        }
    }
    length = result ? x86_encode_scalar(patched, count, machine->code, BUSTER_X86_SEMANTICS_TEST_PAGE, 0, 0) : -1;
    result = length >= 0 && (uint64_t)length + 14 <= BUSTER_X86_SEMANTICS_TEST_PAGE;
    if (result)
    {
        static const uint8_t jump_back[] = { 0xff, 0x25, 0, 0, 0, 0 };
        const uint64_t address = x86_semantics_test_return_address();
        memcpy(machine->code + length, jump_back, sizeof(jump_back));
        memcpy(machine->code + length + sizeof(jump_back), &address, sizeof(address));
    }
    return result;
}

// Runs the loaded code from `state`, which it then holds what the code left.
static void x86_semantics_machine_run(X86SemanticsMachine* machine, X86SemanticsState* state)
{
    x86_semantics_test_code = (uint64_t)machine->code;
    x86_semantics_test_enter(state);
}

// RFLAGS's bits of the flags (BUSTER_X86_FLAG_*), and back.
static uint64_t x86_semantics_rflags(uint8_t flags)
{
    static const uint8_t positions[7] = { 0, 2, 4, 6, 7, 11, 10 };
    uint64_t result = 0;
    for (uint32_t i = 0; i < 7; i += 1)
    {
        result |= (uint64_t)((flags >> i) & 1) << positions[i];
    }
    return result;
}

static uint8_t x86_semantics_flags(uint64_t rflags)
{
    static const uint8_t positions[7] = { 0, 2, 4, 6, 7, 11, 10 };
    uint8_t result = 0;
    for (uint32_t i = 0; i < 7; i += 1)
    {
        result |= (uint8_t)(((rflags >> positions[i]) & 1) << i);
    }
    return result;
}

// A random state: random general and vector registers and status flags (DF clear), RSP the stack's top, and random
// bytes in the data and stack pages.
static void x86_semantics_random_state(uint64_t* state, X86SemanticsMachine* machine, X86SemanticsState* result)
{
    for (uint32_t g = 0; g < 16; g += 1)
    {
        result->gpr[g] = x86_test_random(state);
    }
    result->gpr[X86_REGISTER_RSP] = (uint64_t)(machine->stack + BUSTER_X86_SEMANTICS_TEST_STACK_TOP);
    result->flags = BUSTER_X86_SEMANTICS_TEST_RFLAGS_BASE | x86_semantics_rflags((uint8_t)(x86_test_random(state) & BUSTER_X86_FLAGS_STATUS));
    for (uint32_t v = 0; v < 16; v += 1)
    {
        for (uint32_t i = 0; i < 64; i += 8)
        {
            const uint64_t bits = x86_test_random(state);
            memcpy(&result->vector[v][i], &bits, 8);
        }
    }
    for (uint32_t i = 0; i < BUSTER_X86_SEMANTICS_TEST_PAGE; i += 8)
    {
        const uint64_t bits = x86_test_random(state);
        memcpy(machine->data + i, &bits, 8);
    }
    for (uint32_t i = 0; i < 2 * BUSTER_X86_SEMANTICS_TEST_PAGE; i += 8)
    {
        const uint64_t bits = x86_test_random(state);
        memcpy(machine->stack + i, &bits, 8);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The fixtures of the effects

// What a fixture expects of a form's record (fields of X86Effects, and each role's bits).
typedef struct X86SemanticsFixture X86SemanticsFixture;
struct X86SemanticsFixture
{
    uint16_t form;
    uint8_t operation;
    uint8_t reg;
    uint8_t rm;
    uint16_t implicit_reads;
    uint16_t implicit_writes;
    uint8_t flags_read;
    uint8_t flags_written;
    uint8_t flags_undefined;
    uint8_t memory;
    uint16_t properties;
    uint8_t width;
    uint8_t write_bytes;
};

#define BUSTER_X86_R BUSTER_X86_ROLE_READ
#define BUSTER_X86_W BUSTER_X86_ROLE_WRITE
#define BUSTER_X86_RW (BUSTER_X86_ROLE_READ | BUSTER_X86_ROLE_WRITE)
#define BUSTER_X86_V BUSTER_X86_ROLE_VECTOR
#define BUSTER_X86_REST(kind) (BUSTER_X86_REST_##kind << BUSTER_X86_ROLE_REST_SHIFT)
#define BUSTER_X86_FIXTURE(form, operation, reg, rm, reads, writes, read, written, undefined, memory, properties, width, write_bytes) \
    { X86_FORM_##form, X86_OPERATION_##operation, reg, rm, reads, writes, read, written, undefined, memory, properties, width, write_bytes },
static const X86SemanticsFixture x86_semantics_fixtures[] = {
    // Tied operands, and what a write leaves of the rest: 64-bit whole, 32-bit zero-extended, 8- and 16-bit kept.
    BUSTER_X86_FIXTURE(ADD_RM64_R64, ADD, BUSTER_X86_R, BUSTER_X86_RW, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 8, 8)
    BUSTER_X86_FIXTURE(ADD_RM32_R32, ADD, BUSTER_X86_R, BUSTER_X86_RW | BUSTER_X86_REST(ZERO), 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 4, 4)
    BUSTER_X86_FIXTURE(ADD_RM8_R8, ADD, BUSTER_X86_R, BUSTER_X86_RW | BUSTER_X86_REST(MERGE), 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 1, 1)
    BUSTER_X86_FIXTURE(ADD_R16_RM16, ADD, BUSTER_X86_RW | BUSTER_X86_REST(MERGE), BUSTER_X86_R, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 2, 2)
    BUSTER_X86_FIXTURE(ADD_AL_IMM8, ADD, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 1, 0)
    BUSTER_X86_FIXTURE(ADD_RAX_IMM32, ADD, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 8, 0)
    BUSTER_X86_FIXTURE(ADC_RM64_IMM8, ADC, 0, BUSTER_X86_RW, 0, 0, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 8, 8)
    BUSTER_X86_FIXTURE(AND_RM32_R32, AND, BUSTER_X86_R, BUSTER_X86_RW | BUSTER_X86_REST(ZERO), 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0, 0, 4, 4)
    BUSTER_X86_FIXTURE(XOR_RM32_R32, XOR, BUSTER_X86_R, BUSTER_X86_RW | BUSTER_X86_REST(ZERO), 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0,
                       BUSTER_X86_EFFECT_ZERO_IDIOM, 4, 4)
    BUSTER_X86_FIXTURE(XOR_RM8_R8, XOR, BUSTER_X86_R, BUSTER_X86_RW | BUSTER_X86_REST(MERGE), 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0, 0, 1, 1)
    BUSTER_X86_FIXTURE(XOR_RM64_IMM8, XOR, 0, BUSTER_X86_RW, 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0, 0, 8, 8)
    BUSTER_X86_FIXTURE(SUB_R64_RM64, SUB, BUSTER_X86_RW, BUSTER_X86_R, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, BUSTER_X86_EFFECT_ZERO_IDIOM, 8, 8)
    BUSTER_X86_FIXTURE(CMP_RM64_R64, CMP, BUSTER_X86_R, BUSTER_X86_R, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 8, 0)
    BUSTER_X86_FIXTURE(TEST_RM32_IMM32, TEST, 0, BUSTER_X86_R, 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0, 0, 4, 0)
    BUSTER_X86_FIXTURE(MOV_R32_RM32, MOV, BUSTER_X86_W | BUSTER_X86_REST(ZERO), BUSTER_X86_R, 0, 0, 0, 0, 0, 0, 0, 4, 4)
    BUSTER_X86_FIXTURE(MOV_R8_RM8, MOV, BUSTER_X86_W | BUSTER_X86_REST(MERGE), BUSTER_X86_R, 0, 0, 0, 0, 0, 0, 0, 1, 1)
    BUSTER_X86_FIXTURE(MOV_R64_IMM64, MOV, 0, BUSTER_X86_W, 0, 0, 0, 0, 0, 0, 0, 8, 8)
    BUSTER_X86_FIXTURE(MOVZX_R32_RM8, MOVZX, BUSTER_X86_W | BUSTER_X86_REST(ZERO), BUSTER_X86_R, 0, 0, 0, 0, 0, 0, 0, 4, 4)
    BUSTER_X86_FIXTURE(MOVSXD_R64_RM32, MOVSX, BUSTER_X86_W, BUSTER_X86_R, 0, 0, 0, 0, 0, 0, 0, 8, 8)
    BUSTER_X86_FIXTURE(LEA_R64_M, LEA, BUSTER_X86_W, BUSTER_X86_R, 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ADDRESS, 0, 8, 8)
    BUSTER_X86_FIXTURE(XCHG_RM64_R64, XCHG, BUSTER_X86_RW, BUSTER_X86_RW, 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ATOMIC, 0, 8, 8)
    // Fixed registers: multiplication and division, and the sign extensions (a 16-bit write keeps the rest: read too).
    BUSTER_X86_FIXTURE(MUL_RM8, MUL, 0, BUSTER_X86_R, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, 0, 0, 1, 0)
    BUSTER_X86_FIXTURE(MUL_RM16, MUL, 0, BUSTER_X86_R, BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), 0, BUSTER_X86_FLAGS_CO,
                       BUSTER_X86_FLAGS_SZAP, 0, 0, 2, 0)
    BUSTER_X86_FIXTURE(MUL_RM64, MUL, 0, BUSTER_X86_R, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, 0,
                       0, 8, 0)
    BUSTER_X86_FIXTURE(DIV_RM8, DIV, 0, BUSTER_X86_R, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, 0, BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_EFFECT_FAULT, 1, 0)
    BUSTER_X86_FIXTURE(IDIV_RM32, IDIV, 0, BUSTER_X86_R, BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), 0, 0, BUSTER_X86_FLAGS_STATUS,
                       0, BUSTER_X86_EFFECT_FAULT, 4, 0)
    BUSTER_X86_FIXTURE(DIV_RM64, DIV, 0, BUSTER_X86_R, BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), 0, 0, BUSTER_X86_FLAGS_STATUS, 0,
                       BUSTER_X86_EFFECT_FAULT, 8, 0)
    BUSTER_X86_FIXTURE(IMUL_R64_RM64, IMUL, BUSTER_X86_RW, BUSTER_X86_R, 0, 0, 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, 0, 0, 8, 8)
    BUSTER_X86_FIXTURE(IMUL_R32_RM32_IMM8, IMUL, BUSTER_X86_W | BUSTER_X86_REST(ZERO), BUSTER_X86_R, 0, 0, 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, 0, 0, 4, 4)
    BUSTER_X86_FIXTURE(CBW, SIGN_EXTEND, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, 0, 0, 0, 0, 2, 0)
    BUSTER_X86_FIXTURE(CWD, SIGN_SPLIT, 0, 0, BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), BUSTER_X86_GPR(RDX), 0, 0, 0, 0, 0, 2, 0)
    BUSTER_X86_FIXTURE(CDQ, SIGN_SPLIT, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RDX), 0, 0, 0, 0, 0, 4, 0)
    BUSTER_X86_FIXTURE(CQO, SIGN_SPLIT, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RDX), 0, 0, 0, 0, 0, 8, 0)
    // Variable shifts read CL, and by any count may keep or change the flags they affect.
    BUSTER_X86_FIXTURE(SHL_RM64_CL, SHL, 0, BUSTER_X86_RW, BUSTER_X86_GPR(RCX), 0, BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_EFFECT_COUNT, 8, 8)
    BUSTER_X86_FIXTURE(ROR_RM32_CL, ROR, 0, BUSTER_X86_RW | BUSTER_X86_REST(ZERO), BUSTER_X86_GPR(RCX), 0, BUSTER_X86_FLAGS_CO, 0, BUSTER_X86_FLAGS_CO, 0,
                       BUSTER_X86_EFFECT_COUNT, 4, 4)
    BUSTER_X86_FIXTURE(SAR_RM16_1, SAR, 0, BUSTER_X86_RW | BUSTER_X86_REST(MERGE), 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0, 0, 2, 2)
    // inc and dec keep CF.
    BUSTER_X86_FIXTURE(INC_RM64, INC, 0, BUSTER_X86_RW, 0, 0, 0, BUSTER_X86_FLAGS_STEP, 0, 0, 0, 8, 8)
    BUSTER_X86_FIXTURE(DEC_RM16, DEC, 0, BUSTER_X86_RW | BUSTER_X86_REST(MERGE), 0, 0, 0, BUSTER_X86_FLAGS_STEP, 0, 0, 0, 2, 2)
    BUSTER_X86_FIXTURE(NEG_RM32, NEG, 0, BUSTER_X86_RW | BUSTER_X86_REST(ZERO), 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 4, 4)
    BUSTER_X86_FIXTURE(NOT_RM64, NOT, 0, BUSTER_X86_RW, 0, 0, 0, 0, 0, 0, 0, 8, 8)
    // Conditions read their flags; setcc keeps the rest; cmov reads its destination and zeroes a 32-bit one's upper half.
    BUSTER_X86_FIXTURE(JE_REL8, JCC, 0, 0, 0, 0, BUSTER_X86_FLAG_ZF, 0, 0, 0, 0, 0, 0)
    BUSTER_X86_FIXTURE(JLE_REL32, JCC, 0, 0, 0, 0, BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF, 0, 0, 0, 0, 0, 0)
    BUSTER_X86_FIXTURE(SETL_RM8, SETCC, 0, BUSTER_X86_W | BUSTER_X86_REST(MERGE), 0, 0, BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF, 0, 0, 0, 0, 1, 1)
    BUSTER_X86_FIXTURE(CMOVA_R32_RM32, CMOVCC, BUSTER_X86_RW | BUSTER_X86_REST(ZERO), BUSTER_X86_R, 0, 0, BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF, 0, 0, 0,
                       BUSTER_X86_EFFECT_CONDITIONAL, 4, 4)
    // The stack, calls (their architectural effects only: the callee's clobbers are #137's) and returns.
    BUSTER_X86_FIXTURE(PUSH_R64, PUSH, 0, BUSTER_X86_R, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_STACK_WRITE, 0, 8, 0)
    BUSTER_X86_FIXTURE(POP_R64, POP, 0, BUSTER_X86_W, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_STACK_READ, 0, 8, 8)
    BUSTER_X86_FIXTURE(PUSH_IMM8, PUSH, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_STACK_WRITE, 0, 8, 0)
    BUSTER_X86_FIXTURE(CALL_REL32, CALL, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_STACK_WRITE, 0, 8, 0)
    BUSTER_X86_FIXTURE(CALL_RM64, CALL, 0, BUSTER_X86_R, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_STACK_WRITE, 0, 8, 0)
    BUSTER_X86_FIXTURE(RET, RET, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_STACK_READ, 0, 8, 0)
    BUSTER_X86_FIXTURE(LEAVE, LEAVE, 0, 0, BUSTER_X86_GPR(RBP), BUSTER_X86_GPR(RSP) | BUSTER_X86_GPR(RBP), 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_STACK_READ, 0, 8, 0)
    BUSTER_X86_FIXTURE(UD2, TRAP, 0, 0, 0, 0, 0, 0, 0, 0, BUSTER_X86_EFFECT_FAULT, 0, 0)
    // Bit operations.
    BUSTER_X86_FIXTURE(BT_RM64_R64, BT, BUSTER_X86_R, BUSTER_X86_R, 0, 0, 0, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STEP, BUSTER_X86_MEMORY_EFFECT_BIT_STRING, 0, 8, 0)
    BUSTER_X86_FIXTURE(BSF_R32_RM32, BSF, BUSTER_X86_RW | BUSTER_X86_REST(ZERO), BUSTER_X86_R, 0, 0, 0, BUSTER_X86_FLAG_ZF, BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAG_ZF, 0,
                       BUSTER_X86_EFFECT_ZERO_SOURCE, 4, 4)
    BUSTER_X86_FIXTURE(POPCNT_R64_RM64, POPCNT, BUSTER_X86_W, BUSTER_X86_R, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 8, 8)
    // Atomics.
    BUSTER_X86_FIXTURE(LOCK_ADD_M64_IMM8, ADD, 0, BUSTER_X86_RW, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_MEMORY_EFFECT_ATOMIC, 0, 8, 0)
    BUSTER_X86_FIXTURE(LOCK_CMPXCHG_M32_R32, CMPXCHG, BUSTER_X86_R, BUSTER_X86_RW, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, BUSTER_X86_FLAGS_STATUS, 0,
                       BUSTER_X86_MEMORY_EFFECT_ATOMIC, BUSTER_X86_EFFECT_CONDITIONAL, 4, 0)
    BUSTER_X86_FIXTURE(MFENCE, FENCE, 0, 0, 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_FENCE, 0, 0, 0)
    // SSE: scalar merges, loads that zero the rest from memory and keep it from a register, legacy SSE's upper lanes.
    BUSTER_X86_FIXTURE(MOVSD_X_XM64, FMOVE, BUSTER_X86_W | BUSTER_X86_V | BUSTER_X86_REST(MERGE_IF_REGISTER), BUSTER_X86_R | BUSTER_X86_V, 0, 0, 0, 0, 0, 0,
                       BUSTER_X86_EFFECT_KEEP_UPPER, 8, 8)
    BUSTER_X86_FIXTURE(ADDSD_X_XM64, FADD, BUSTER_X86_RW | BUSTER_X86_V | BUSTER_X86_REST(MERGE), BUSTER_X86_R | BUSTER_X86_V, 0, 0, 0, 0, 0, 0,
                       BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_KEEP_UPPER, 8, 8)
    BUSTER_X86_FIXTURE(SQRTSS_X_XM32, FSQRT, BUSTER_X86_W | BUSTER_X86_V | BUSTER_X86_REST(MERGE), BUSTER_X86_R | BUSTER_X86_V, 0, 0, 0, 0, 0, 0,
                       BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_KEEP_UPPER, 4, 4)
    BUSTER_X86_FIXTURE(CVTSI2SD_X_RM64, INT_TO_FLOAT, BUSTER_X86_W | BUSTER_X86_V | BUSTER_X86_REST(MERGE), BUSTER_X86_R, 0, 0, 0, 0, 0, 0,
                       BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_KEEP_UPPER, 8, 8)
    BUSTER_X86_FIXTURE(CVTTSD2SI_R32_XM64, FLOAT_TO_INT, BUSTER_X86_W | BUSTER_X86_REST(ZERO), BUSTER_X86_R | BUSTER_X86_V, 0, 0, 0, 0, 0, 0,
                       BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION, 4, 4)
    BUSTER_X86_FIXTURE(UCOMISD_X_XM64, FCOMPARE, BUSTER_X86_R | BUSTER_X86_V, BUSTER_X86_R | BUSTER_X86_V, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0,
                       BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION, 8, 0)
    BUSTER_X86_FIXTURE(XORPS_X_XM128, VECTOR_XOR, BUSTER_X86_RW | BUSTER_X86_V, BUSTER_X86_R | BUSTER_X86_V, 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ALIGNED,
                       BUSTER_X86_EFFECT_ZERO_IDIOM | BUSTER_X86_EFFECT_KEEP_UPPER, 16, 16)
    BUSTER_X86_FIXTURE(MOVUPS_X_XM, VECTOR_MOVE, BUSTER_X86_W | BUSTER_X86_V, BUSTER_X86_R | BUSTER_X86_V, 0, 0, 0, 0, 0, 0, BUSTER_X86_EFFECT_KEEP_UPPER, 16, 16)
    BUSTER_X86_FIXTURE(MOVD_X_RM32, MOVD, BUSTER_X86_W | BUSTER_X86_V | BUSTER_X86_REST(ZERO), BUSTER_X86_R, 0, 0, 0, 0, 0, 0, BUSTER_X86_EFFECT_KEEP_UPPER, 4, 4)
    BUSTER_X86_FIXTURE(MOVQ_XM64_X, MOVD, BUSTER_X86_R | BUSTER_X86_V, BUSTER_X86_W | BUSTER_X86_V | BUSTER_X86_REST(ZERO), 0, 0, 0, 0, 0, 0, BUSTER_X86_EFFECT_KEEP_UPPER,
                       8, 8)
};
#undef BUSTER_X86_FIXTURE

// Forms outside the subset: each must get the conservative record.
static const uint16_t x86_semantics_uncovered[] = {
    X86_FORM_NONE,
    X86_FORM_MOVABS_AL_MOFFS,
    X86_FORM_RCL_RM64_1,
    X86_FORM_SHLD_RM64_R64_CL,
    X86_FORM_CMPXCHG_RM64_R64,
    X86_FORM_XCHG_R64_RAX,
    X86_FORM_PUSHP_R64,
    X86_FORM_SYSCALL,
    X86_FORM_CPUID,
    X86_FORM_LOCK_BTS_M64_R64,
    X86_FORM_ADDPS_X_XM128,
    X86_FORM_PSHUFD_X_XM128_IMM8,
    X86_FORM_INT_IMM8,
    X86_FORM_ENTER,
};

static bool x86_semantics_check_fixtures(void)
{
    X86SemanticsTally tally = {
        .check = "effects: fixtures",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_semantics_fixtures); i += 1)
    {
        const X86SemanticsFixture* fixture = &x86_semantics_fixtures[i];
        const X86Effects* effects = x86_effects(fixture->form);
        const char* name = x86_semantics_test_name(fixture->form);
        x86_semantics_expect(&tally, effects->operation == fixture->operation, "%s: operation %u, not %u", name, effects->operation, fixture->operation);
        x86_semantics_expect(&tally, x86_effects_role(effects, BUSTER_X86_ROLE_REG) == fixture->reg, "%s: reg's bits %#x, not %#x", name,
                             x86_effects_role(effects, BUSTER_X86_ROLE_REG), fixture->reg);
        x86_semantics_expect(&tally, x86_effects_role(effects, BUSTER_X86_ROLE_RM) == fixture->rm, "%s: rm's bits %#x, not %#x", name, x86_effects_role(effects, BUSTER_X86_ROLE_RM),
                             fixture->rm);
        x86_semantics_expect(&tally, effects->implicit_reads == fixture->implicit_reads && effects->implicit_writes == fixture->implicit_writes,
                             "%s: implicit reads %#x and writes %#x, not %#x and %#x", name, effects->implicit_reads, effects->implicit_writes, fixture->implicit_reads,
                             fixture->implicit_writes);
        x86_semantics_expect(&tally,
                             effects->flags_read == fixture->flags_read && effects->flags_written == fixture->flags_written &&
                                 effects->flags_undefined == fixture->flags_undefined,
                             "%s: flags read %#x, written %#x, undefined %#x, not %#x, %#x, %#x", name, effects->flags_read, effects->flags_written,
                             effects->flags_undefined, fixture->flags_read, fixture->flags_written, fixture->flags_undefined);
        x86_semantics_expect(&tally, effects->memory == fixture->memory && effects->properties == fixture->properties, "%s: memory %#x and properties %#x, not %#x and %#x",
                             name, effects->memory, effects->properties, fixture->memory, fixture->properties);
        x86_semantics_expect(&tally, effects->width == fixture->width && effects->write_bytes == fixture->write_bytes, "%s: width %u and write bytes %u, not %u and %u", name,
                             effects->width, effects->write_bytes, fixture->width, fixture->write_bytes);
    }
    // A segment override and the fixup flag change no effect.
    x86_semantics_expect(&tally, x86_effects(X86_FORM_ADD_RM64_R64 + BUSTER_X86_FORM_GS) == x86_effects(X86_FORM_ADD_RM64_R64), "GS's ADD_RM64_R64 is ADD_RM64_R64");
    x86_semantics_expect(&tally, x86_effects(X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS + BUSTER_X86_FORM_FIXUP) == x86_effects(X86_FORM_MOV_R64_RM64),
                         "FS's marked MOV_R64_RM64 is MOV_R64_RM64");
    // Control.
    x86_semantics_expect(&tally, x86_effects_control(x86_effects(X86_FORM_JMP_REL32)) == BUSTER_X86_CONTROL_JUMP, "jmp jumps");
    x86_semantics_expect(&tally, x86_effects_control(x86_effects(X86_FORM_JNE_REL8)) == BUSTER_X86_CONTROL_BRANCH, "jne branches");
    x86_semantics_expect(&tally, x86_effects_control(x86_effects(X86_FORM_CALL_REL32)) == BUSTER_X86_CONTROL_CALL, "call calls");
    x86_semantics_expect(&tally, x86_effects_control(x86_effects(X86_FORM_RET)) == BUSTER_X86_CONTROL_RETURN, "ret returns");
    x86_semantics_expect(&tally, x86_effects_control(x86_effects(X86_FORM_INT3)) == BUSTER_X86_CONTROL_TRAP, "int3 traps");
    // The immediate's kinds.
    x86_semantics_expect(&tally, x86_effects_immediate_kind(x86_effects(X86_FORM_ADD_RM64_IMM8)) == BUSTER_X86_IMMEDIATE_VALUE, "add's imm8 is a value");
    x86_semantics_expect(&tally, x86_effects_immediate_kind(x86_effects(X86_FORM_SHL_RM32_IMM8)) == BUSTER_X86_IMMEDIATE_COUNT, "shl's imm8 is a count");
    x86_semantics_expect(&tally, x86_effects_immediate_kind(x86_effects(X86_FORM_BT_RM32_IMM8)) == BUSTER_X86_IMMEDIATE_BIT, "bt's imm8 is a bit index");
    x86_semantics_expect(&tally, x86_effects_immediate_kind(x86_effects(X86_FORM_RET_IMM16)) == BUSTER_X86_IMMEDIATE_CONTROL, "ret's imm16 is control bits");
    x86_semantics_expect(&tally, x86_effects_immediate_kind(x86_effects(X86_FORM_ADD_RM64_R64)) == BUSTER_X86_IMMEDIATE_NONE, "add's register form has no immediate");
    // Uncovered forms: the conservative record.
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_semantics_uncovered); i += 1)
    {
        const X86Effects* effects = x86_effects(x86_semantics_uncovered[i]);
        uint64_t reads = 0;
        uint64_t writes = 0;
        const X86Instruction record = {
            .form = x86_semantics_uncovered[i],
        };
        x86_instruction_effects_scalar(&record, &reads, &writes);
        x86_semantics_expect(&tally,
                             effects->operation == X86_OPERATION_NONE && effects->implicit_writes == 0xffff && effects->flags_undefined == BUSTER_X86_FLAGS_ALL &&
                                 x86_effects_control(effects) == BUSTER_X86_CONTROL_UNKNOWN && reads == BUSTER_X86_RESOURCE_ALL && writes == BUSTER_X86_RESOURCE_ALL,
                             "%s is uncovered: the conservative record, reading and writing everything", x86_semantics_test_name(x86_semantics_uncovered[i]));
    }
    // Every covered form's row is consistent with the form table, and the count is what the rows say.
    uint32_t covered = 0;
    for (uint32_t form = 0; form < X86_FORM_COUNT; form += 1)
    {
        covered += x86_effects(form)->operation != X86_OPERATION_NONE;
    }
    x86_semantics_expect(&tally, covered + 1 == x86_effects_count, "%u covered forms, and %u records past the conservative one", covered, x86_effects_count - 1);
    printf("effects: %u covered forms of %u, %u records of %u bytes and an index of %u bytes\n", covered, (uint32_t)X86_FORM_COUNT, x86_effects_count,
           (uint32_t)sizeof(X86Effects), (uint32_t)sizeof(x86_effects_index));
    return x86_semantics_report(&tally);
}

// Resolved effects with register and memory operands, a zero idiom and shift counts: x86_instruction_effects_scalar's.
static bool x86_semantics_check_resolved(void)
{
    X86SemanticsTally tally = {
        .check = "effects: resolved operands",
    };
    typedef struct X86SemanticsResolved X86SemanticsResolved;
    struct X86SemanticsResolved
    {
        X86Instruction record;
        uint64_t reads;
        uint64_t writes;
        const char* what;
    };
    const uint64_t status = BUSTER_X86_RESOURCE_STATUS;
    const uint64_t memory = BUSTER_X86_RESOURCE_MEMORY;
    const X86SemanticsResolved cases[] = {
        {
            .record = { .form = X86_FORM_ADD_RM64_R64, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX },
            .reads = BUSTER_X86_RESOURCE_GPR(1) | BUSTER_X86_RESOURCE_GPR(2),
            .writes = BUSTER_X86_RESOURCE_GPR(2) | status,
            .what = "add rdx, rcx",
        },
        {
            .record = { .form = X86_FORM_ADD_RM64_R64, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 16 },
            .reads = BUSTER_X86_RESOURCE_GPR(1) | BUSTER_X86_RESOURCE_GPR(3) | BUSTER_X86_RESOURCE_GPR(6) | memory,
            .writes = memory | status,
            .what = "add [rbx + rsi * 8 + 16], rcx",
        },
        {
            .record = { .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2) },
            .reads = BUSTER_X86_RESOURCE_GPR(3) | BUSTER_X86_RESOURCE_GPR(6),
            .writes = BUSTER_X86_RESOURCE_GPR(0),
            .what = "lea rax, [rbx + rsi * 4]: no memory",
        },
        {
            .record = { .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP },
            .reads = memory,
            .writes = BUSTER_X86_RESOURCE_GPR(0),
            .what = "mov rax, [rip + x]",
        },
        {
            .record = { .form = X86_FORM_MOV_RM32_R32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -8 },
            .reads = BUSTER_X86_RESOURCE_GPR(0) | BUSTER_X86_RESOURCE_GPR(5),
            .writes = memory,
            .what = "mov [rbp - 8], eax: a store",
        },
        {
            .record = { .form = X86_FORM_MOV_R8_RM8, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX },
            .reads = BUSTER_X86_RESOURCE_GPR(0) | BUSTER_X86_RESOURCE_GPR(1),
            .writes = BUSTER_X86_RESOURCE_GPR(0),
            .what = "mov al, cl: keeps the rest of RAX, so reads it",
        },
        {
            .record = { .form = X86_FORM_MOV_R8_RM8, .reg = X86_REGISTER_AH, .rm = X86_REGISTER_RCX },
            .reads = BUSTER_X86_RESOURCE_GPR(0) | BUSTER_X86_RESOURCE_GPR(1),
            .writes = BUSTER_X86_RESOURCE_GPR(0),
            .what = "mov ah, cl",
        },
        {
            .record = { .form = X86_FORM_MOVZX_R32_RM8, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX },
            .reads = BUSTER_X86_RESOURCE_GPR(1),
            .writes = BUSTER_X86_RESOURCE_GPR(0),
            .what = "movzx eax, cl: no read of RAX",
        },
        {
            .record = { .form = X86_FORM_XOR_RM32_R32, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R9 },
            .reads = 0,
            .writes = BUSTER_X86_RESOURCE_GPR(9) | status,
            .what = "xor r9d, r9d: a zero idiom reads nothing",
        },
        {
            .record = { .form = X86_FORM_XOR_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
            .reads = BUSTER_X86_RESOURCE_GPR(0) | memory,
            .writes = BUSTER_X86_RESOURCE_GPR(0) | status,
            .what = "xor eax, [rax]: memory, no idiom",
        },
        {
            .record = { .form = X86_FORM_XOR_RM8_R8, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RCX },
            .reads = BUSTER_X86_RESOURCE_GPR(1),
            .writes = BUSTER_X86_RESOURCE_GPR(1) | status,
            .what = "xor cl, cl: keeps the rest of RCX",
        },
        {
            .record = { .form = X86_FORM_PXOR_X_XM128, .reg = 3, .rm = 3 },
            .reads = BUSTER_X86_RESOURCE_VECTOR(3),
            .writes = BUSTER_X86_RESOURCE_VECTOR(3),
            .what = "pxor xmm3, xmm3: a zero idiom of the low 128 bits, but ZMM3's upper bits pass through",
        },
        {
            .record = { .form = X86_FORM_MOVAPS_X_XM, .reg = 3, .rm = 4 },
            .reads = BUSTER_X86_RESOURCE_VECTOR(3) | BUSTER_X86_RESOURCE_VECTOR(4),
            .writes = BUSTER_X86_RESOURCE_VECTOR(3),
            .what = "movaps xmm3, xmm4: keeps ZMM3's bits above 127, so reads it",
        },
        {
            .record = { .form = X86_FORM_SHL_RM64_IMM8, .rm = X86_REGISTER_RDX, .immediate = 0 },
            .reads = BUSTER_X86_RESOURCE_GPR(2),
            .writes = BUSTER_X86_RESOURCE_GPR(2),
            .what = "shl rdx, 0: no flags",
        },
        {
            .record = { .form = X86_FORM_SHL_RM64_IMM8, .rm = X86_REGISTER_RDX, .immediate = 64 },
            .reads = BUSTER_X86_RESOURCE_GPR(2),
            .writes = BUSTER_X86_RESOURCE_GPR(2),
            .what = "shl rdx, 64: masked to 0",
        },
        {
            .record = { .form = X86_FORM_SHL_RM64_IMM8, .rm = X86_REGISTER_RDX, .immediate = 3 },
            .reads = BUSTER_X86_RESOURCE_GPR(2),
            .writes = BUSTER_X86_RESOURCE_GPR(2) | status,
            .what = "shl rdx, 3",
        },
        {
            .record = { .form = X86_FORM_SHL_RM64_CL, .rm = X86_REGISTER_RDX },
            .reads = BUSTER_X86_RESOURCE_GPR(1) | BUSTER_X86_RESOURCE_GPR(2) | status,
            .writes = BUSTER_X86_RESOURCE_GPR(2) | status,
            .what = "shl rdx, cl: the flags pass through for a count of 0",
        },
        {
            .record = { .form = X86_FORM_DIV_RM64, .rm = X86_REGISTER_RCX },
            .reads = BUSTER_X86_RESOURCE_GPR(0) | BUSTER_X86_RESOURCE_GPR(1) | BUSTER_X86_RESOURCE_GPR(2),
            .writes = BUSTER_X86_RESOURCE_GPR(0) | BUSTER_X86_RESOURCE_GPR(2) | status,
            .what = "div rcx",
        },
        {
            .record = { .form = X86_FORM_PUSH_R64, .rm = X86_REGISTER_RBX },
            .reads = BUSTER_X86_RESOURCE_GPR(3) | BUSTER_X86_RESOURCE_GPR(4),
            .writes = BUSTER_X86_RESOURCE_GPR(4) | memory,
            .what = "push rbx",
        },
        {
            .record = { .form = X86_FORM_CALL_REL32 },
            .reads = BUSTER_X86_RESOURCE_GPR(4),
            .writes = BUSTER_X86_RESOURCE_GPR(4) | memory | BUSTER_X86_RESOURCE_CONTROL | BUSTER_X86_RESOURCE_CALL,
            .what = "call f: RSP and the return address, and the callee's clobbers left to #137",
        },
        {
            .record = { .form = X86_FORM_JL_REL8 },
            .reads = BUSTER_X86_RESOURCE_FLAGS & ((uint64_t)(BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF) << BUSTER_X86_RESOURCE_FLAGS_SHIFT),
            .writes = BUSTER_X86_RESOURCE_CONTROL,
            .what = "jl",
        },
        {
            .record = { .form = X86_FORM_MOVSS_X_XM32, .reg = 1, .rm = 2 },
            .reads = BUSTER_X86_RESOURCE_VECTOR(1) | BUSTER_X86_RESOURCE_VECTOR(2),
            .writes = BUSTER_X86_RESOURCE_VECTOR(1),
            .what = "movss xmm1, xmm2: keeps the rest of XMM1",
        },
        {
            .record = { .form = X86_FORM_MOVSS_X_XM32, .reg = 1, .mode = BUSTER_X86_MEMORY_RIP },
            .reads = memory | BUSTER_X86_RESOURCE_VECTOR(1),
            .writes = BUSTER_X86_RESOURCE_VECTOR(1),
            .what = "movss xmm1, [rip + x]: zeroes the rest of XMM1, and keeps ZMM1's bits above 127",
        },
        {
            .record = { .form = X86_FORM_ADDSD_X_XM64, .reg = 4, .rm = 5 },
            .reads = BUSTER_X86_RESOURCE_VECTOR(4) | BUSTER_X86_RESOURCE_VECTOR(5) | BUSTER_X86_RESOURCE_MXCSR,
            .writes = BUSTER_X86_RESOURCE_VECTOR(4),
            .what = "addsd xmm4, xmm5",
        },
        {
            .record = { .form = X86_FORM_LOCK_XADD_M64_R64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE },
            .reads = BUSTER_X86_RESOURCE_GPR(0) | BUSTER_X86_RESOURCE_GPR(7) | memory,
            .writes = BUSTER_X86_RESOURCE_GPR(0) | memory | status,
            .what = "lock xadd [rdi], rax",
        },
        {
            .record = { .form = X86_FORM_MFENCE },
            .reads = memory,
            .writes = memory,
            .what = "mfence: orders every access",
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        uint64_t reads = 0;
        uint64_t writes = 0;
        x86_instruction_effects_scalar(&cases[i].record, &reads, &writes);
        x86_semantics_expect(&tally, reads == cases[i].reads && writes == cases[i].writes, "%s: reads %#llx and writes %#llx, not %#llx and %#llx", cases[i].what,
                             (unsigned long long)reads, (unsigned long long)writes, (unsigned long long)cases[i].reads, (unsigned long long)cases[i].writes);
    }
    // A shift's flags by its count.
    typedef struct X86SemanticsCount X86SemanticsCount;
    struct X86SemanticsCount
    {
        uint16_t form;
        int32_t count;
        uint8_t written;
        uint8_t undefined;
    };
    const X86SemanticsCount counts[] = {
        { X86_FORM_SHL_RM64_IMM8, 0, 0, 0 },
        { X86_FORM_SHL_RM64_IMM8, 1, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF },
        { X86_FORM_SHL_RM64_IMM8, 5, BUSTER_X86_FLAGS_LOGICAL & ~BUSTER_X86_FLAG_OF, BUSTER_X86_FLAG_AF | BUSTER_X86_FLAG_OF },
        { X86_FORM_SHL_RM64_IMM8, 65, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF },
        { X86_FORM_SHR_RM32_IMM8, 32, 0, 0 },
        { X86_FORM_SHR_RM8_IMM8, 9, BUSTER_X86_FLAGS_LOGICAL & ~(BUSTER_X86_FLAG_OF | BUSTER_X86_FLAG_CF), BUSTER_X86_FLAG_AF | BUSTER_X86_FLAG_OF | BUSTER_X86_FLAG_CF },
        { X86_FORM_SAR_RM16_IMM8, 16, BUSTER_X86_FLAGS_LOGICAL & ~(BUSTER_X86_FLAG_OF | BUSTER_X86_FLAG_CF), BUSTER_X86_FLAG_AF | BUSTER_X86_FLAG_OF | BUSTER_X86_FLAG_CF },
        { X86_FORM_ROL_RM64_IMM8, 1, BUSTER_X86_FLAGS_CO, 0 },
        { X86_FORM_ROR_RM8_IMM8, 3, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAG_OF },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(counts); i += 1)
    {
        const X86Instruction record = {
            .form = counts[i].form,
            .rm = X86_REGISTER_RBX,
            .immediate = counts[i].count,
        };
        uint8_t read = 0;
        uint8_t written = 0;
        uint8_t undefined = 0;
        x86_instruction_flags(&record, &read, &written, &undefined);
        x86_semantics_expect(&tally, read == 0 && written == counts[i].written && undefined == counts[i].undefined, "%s by %d: read %#x, written %#x, undefined %#x",
                             x86_semantics_test_name(counts[i].form), counts[i].count, read, written, undefined);
    }
    // Substitutions: inc for add 1 needs CF dead, dec for add -1 CF and AF, test r, r for cmp 0 AF, xor for mov 0 all.
    typedef struct X86SemanticsSubstitution X86SemanticsSubstitution;
    struct X86SemanticsSubstitution
    {
        uint16_t original;
        uint16_t substitute;
        uint8_t same;
        uint8_t dead;
    };
    const X86SemanticsSubstitution substitutions[] = {
        { X86_FORM_ADD_RM64_IMM8, X86_FORM_INC_RM64, BUSTER_X86_SAME_STEP, BUSTER_X86_FLAG_CF },
        { X86_FORM_SUB_RM32_IMM8, X86_FORM_DEC_RM32, BUSTER_X86_SAME_STEP, BUSTER_X86_FLAG_CF },
        { X86_FORM_ADD_RM16_IMM8, X86_FORM_DEC_RM16, BUSTER_X86_SAME_REVERSE_STEP, BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_AF },
        { X86_FORM_CMP_RM64_IMM8, X86_FORM_TEST_RM64_R64, BUSTER_X86_SAME_TEST, BUSTER_X86_FLAG_AF },
        { X86_FORM_MOV_RM64_IMM32, X86_FORM_XOR_RM32_R32, 0, BUSTER_X86_FLAGS_STATUS },
        // lea for add keeps every flag that add defines: none may be live.
        { X86_FORM_ADD_RM64_IMM32, X86_FORM_LEA_R64_M, 0, BUSTER_X86_FLAGS_STATUS },
        // add for lea defines what lea keeps: the same.
        { X86_FORM_LEA_R64_M, X86_FORM_ADD_RM64_IMM32, 0, BUSTER_X86_FLAGS_STATUS },
        { X86_FORM_TEST_RM64_IMM32, X86_FORM_TEST_RM32_IMM32, BUSTER_X86_SAME_NARROW_TEST, BUSTER_X86_FLAG_SF },
        { X86_FORM_ADD_RM64_IMM8, X86_FORM_ADD_RM64_IMM32, BUSTER_X86_SAME_ALL, 0 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(substitutions); i += 1)
    {
        const X86SemanticsSubstitution* entry = &substitutions[i];
        for (uint32_t live = 0; live <= BUSTER_X86_FLAGS_STATUS; live += 1)
        {
            const bool expected = (live & entry->dead) == 0;
            const bool substitutable = x86_flags_substitutable(x86_effects(entry->original), x86_effects(entry->substitute), entry->same, (uint8_t)live);
            x86_semantics_expect(&tally, substitutable == expected, "%s for %s with flags %#x live: %s", x86_semantics_test_name(entry->substitute),
                                 x86_semantics_test_name(entry->original), live, substitutable ? "substitutable" : "not");
        }
    }
    return x86_semantics_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The effects table, executed

// A random register for an operand descriptor: a vector register, or a general one but RSP (the harness's stack) and
// SPL, sometimes AH to BH for a byte register (x86_validate refuses those beside REX, and the caller retries).
static uint8_t x86_semantics_random_register(uint64_t* state, uint16_t descriptor)
{
    const uint32_t class = descriptor & BUSTER_X86_OPERAND_CLASS;
    uint8_t result = 0;
    if (class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM)
    {
        result = (uint8_t)x86_random_below(state, 16);
    }
    else if (class == BUSTER_X86_CLASS_GPR8 && x86_random_below(state, 4) == 0)
    {
        result = (uint8_t)((4 + x86_random_below(state, 4)) | BUSTER_X86_REGISTER_HIGH_BYTE);
    }
    else
    {
        const uint32_t number = x86_random_below(state, 15);
        result = (uint8_t)(number >= X86_REGISTER_RSP ? number + 1 : number);
    }
    return result;
}

// A general register's number (AH to BH as RAX to RBX), or a vector register's.
static uint32_t x86_semantics_register_number(uint8_t value)
{
    return (value & BUSTER_X86_REGISTER_HIGH_BYTE) ? (value & 3u) : (value & 15u);
}

// A random record of the form: random registers, the memory operand RIP-relative into the data page when `memory` (or
// when rm takes only memory), and a random immediate (a count or bit index often 0 or 1). Retries until x86_validate takes
// it. Division never divides by RAX or RDX, which hold the dividend.
static X86Instruction x86_semantics_random_record(uint64_t* state, uint32_t form, bool memory)
{
    const X86FormDescription* description = &x86_form_descriptions[form];
    const X86Effects* effects = x86_effects(form);
    const bool division = effects->operation == X86_OPERATION_DIV || effects->operation == X86_OPERATION_IDIV;
    X86Instruction result = { 0 };
    bool valid = false;
    for (uint32_t attempt = 0; !valid && attempt < 1000; attempt += 1)
    {
        result = (X86Instruction){
            .form = (uint16_t)form,
        };
        if (description->reg != BUSTER_X86_OPERAND_NONE)
        {
            result.reg = x86_semantics_random_register(state, description->reg);
        }
        if (description->rm != BUSTER_X86_OPERAND_NONE)
        {
            const bool in_memory = (description->rm & BUSTER_X86_OPERAND_MEMORY) && (memory || !(description->rm & BUSTER_X86_OPERAND_REGISTER));
            if (in_memory)
            {
                result.mode = BUSTER_X86_MEMORY_RIP;
                result.displacement = BUSTER_X86_SEMANTICS_TEST_OPERAND;
            }
            else
            {
                result.rm = x86_semantics_random_register(state, description->rm);
            }
        }
        const uint32_t kind = x86_effects_immediate_kind(effects);
        if (description->immediate_bytes == 8)
        {
            result.immediate = (int32_t)x86_test_random(state);
            result.displacement = (int32_t)x86_test_random(state);
        }
        else if (kind == BUSTER_X86_IMMEDIATE_COUNT || kind == BUSTER_X86_IMMEDIATE_BIT)
        {
            const uint32_t choice = x86_random_below(state, 4);
            result.immediate = choice == 0 ? 0 : choice == 1 ? 1 : (int32_t)x86_random_below(state, 256) - 128;
        }
        else if (kind == BUSTER_X86_IMMEDIATE_CONTROL)
        {
            // roundss's and roundsd's imm8 uses bits 3 to 0 (bits 7 to 4 are reserved), cmpss's and cmpsd's 2 to 0.
            result.immediate = (int32_t)x86_random_below(state, 8);
        }
        else if (description->immediate_bytes != 0)
        {
            const uint32_t shift = 64 - 8 * description->immediate_bytes;
            result.immediate = (int32_t)((int64_t)(x86_test_random(state) << shift) >> shift);
        }
        const bool divides_dividend = division && (result.mode & BUSTER_X86_MODE_MEMORY) == 0 && x86_semantics_register_number(result.rm) <= X86_REGISTER_RDX &&
                                      x86_semantics_register_number(result.rm) != X86_REGISTER_RCX;
        valid = !divides_dividend && x86_validate(&result, 1) == 1;
    }
    return result;
}

// Makes a division's operands fault-free: a divisor that isn't 0 (nor -1 for idiv), and a dividend whose quotient fits:
// for div, a high half below the divisor; for idiv, a high half that is the low half's sign.
static void x86_semantics_fix_division(uint64_t* state, X86SemanticsMachine* machine, const X86Instruction* record, X86SemanticsState* machine_state)
{
    const X86Effects* effects = x86_effects(record->form);
    const uint32_t width = effects->width;
    const uint64_t mask = x86_width_mask(width);
    const bool is_signed = effects->operation == X86_OPERATION_IDIV;
    uint64_t divisor = x86_test_random(state) & mask;
    divisor = divisor == 0 || divisor == mask ? 7 : divisor;
    if (record->mode & BUSTER_X86_MODE_MEMORY)
    {
        memcpy(machine->data + BUSTER_X86_SEMANTICS_TEST_OPERAND, &divisor, width);
    }
    else
    {
        const uint32_t number = x86_semantics_register_number(record->rm);
        const uint32_t shift = (record->rm & BUSTER_X86_REGISTER_HIGH_BYTE) ? 8 : 0;
        machine_state->gpr[number] = (machine_state->gpr[number] & ~(mask << shift)) | divisor << shift;
    }
    // The dividend: (AH:AL), DX:AX, EDX:EAX or RDX:RAX.
    const uint64_t low = machine_state->gpr[X86_REGISTER_RAX] & mask;
    const uint64_t sign = (low >> (8 * width - 1)) & 1;
    const uint64_t high = is_signed ? (sign ? mask : 0) : (x86_test_random(state) & mask) % divisor;
    if (width == 1)
    {
        machine_state->gpr[X86_REGISTER_RAX] = (machine_state->gpr[X86_REGISTER_RAX] & ~0xff00ull) | high << 8;
    }
    else
    {
        machine_state->gpr[X86_REGISTER_RDX] = (machine_state->gpr[X86_REGISTER_RDX] & ~mask) | high;
    }
}

static bool x86_semantics_zero_bytes(const uint8_t* bytes, uint32_t from, uint32_t to)
{
    bool result = true;
    for (uint32_t i = from; i < to; i += 1)
    {
        result &= bytes[i] == 0;
    }
    return result;
}

// Per form, which writes ever changed something over its runs (a write that never does is a wrong row).
typedef struct X86SemanticsChanges X86SemanticsChanges;
struct X86SemanticsChanges
{
    uint32_t roles_written;
    uint32_t roles_changed;
    uint32_t implicit_written;
    uint32_t implicit_changed;
};

// One run of a record from a random state, against x86_instruction_effects_scalar's and x86_instruction_flags's claims
// (see the file's comment), then again from the state with everything the record doesn't read changed.
static void x86_semantics_check_run(X86SemanticsTally* tally, X86SemanticsMachine* machine, uint64_t* state, const X86Instruction* record, X86SemanticsChanges* changes)
{
    const X86Effects* effects = x86_effects(record->form);
    const char* name = x86_semantics_test_name(record->form);
    const bool memory = (record->mode & BUSTER_X86_MODE_MEMORY) != 0;
    snprintf(x86_semantics_test_case, sizeof(x86_semantics_test_case), "%s (reg %u, rm %u, mode %#x, immediate %d)", name, record->reg, record->rm, record->mode, record->immediate);
    uint64_t reads = 0;
    uint64_t writes = 0;
    x86_instruction_effects_scalar(record, &reads, &writes);
    uint8_t flags_read = 0;
    uint8_t flags_written = 0;
    uint8_t flags_undefined = 0;
    x86_instruction_flags(record, &flags_read, &flags_written, &flags_undefined);

    static X86SemanticsState input;
    static X86SemanticsState output;
    static X86SemanticsState second_input;
    static X86SemanticsState second_output;
    static uint8_t data[BUSTER_X86_SEMANTICS_TEST_PAGE];
    static uint8_t stack[2 * BUSTER_X86_SEMANTICS_TEST_PAGE];
    static uint8_t data_after[BUSTER_X86_SEMANTICS_TEST_PAGE];
    static uint8_t stack_after[2 * BUSTER_X86_SEMANTICS_TEST_PAGE];
    x86_semantics_random_state(state, machine, &input);
    if (effects->operation == X86_OPERATION_DIV || effects->operation == X86_OPERATION_IDIV)
    {
        x86_semantics_fix_division(state, machine, record, &input);
    }
    if (effects->memory & BUSTER_X86_MEMORY_EFFECT_BIT_STRING)
    {
        // bt with a register offset into memory: an offset within the operand.
        input.gpr[x86_semantics_register_number(record->reg)] = x86_test_random(state) % (8u * effects->width);
    }
    if (effects->operation == X86_OPERATION_LEAVE)
    {
        input.gpr[X86_REGISTER_RBP] = (uint64_t)(machine->stack + BUSTER_X86_SEMANTICS_TEST_STACK_TOP + 64);
    }
    memcpy(data, machine->data, sizeof(data));
    memcpy(stack, machine->stack, sizeof(stack));
    output = input;
    const bool loaded = x86_semantics_machine_load(machine, record, 1);
    x86_semantics_expect(tally, loaded, "%s doesn't encode", x86_semantics_test_case);
    if (loaded)
    {
        x86_semantics_machine_run(machine, &output);
        memcpy(data_after, machine->data, sizeof(data_after));
        memcpy(stack_after, machine->stack, sizeof(stack_after));

        // Registers it doesn't write stay; and what a write does to the rest of a register.
        for (uint32_t g = 0; g < 16; g += 1)
        {
            const bool written = (writes & BUSTER_X86_RESOURCE_GPR(g)) != 0;
            x86_semantics_expect(tally, written || output.gpr[g] == input.gpr[g], "%s changed general register %u, which it doesn't write", x86_semantics_test_case, g);
        }
        for (uint32_t v = 0; v < 16; v += 1)
        {
            const bool written = (writes & BUSTER_X86_RESOURCE_VECTOR(v)) != 0;
            x86_semantics_expect(tally, written || memcmp(output.vector[v], input.vector[v], 64) == 0, "%s changed vector register %u, which it doesn't write",
                                 x86_semantics_test_case, v);
            const bool upper_kept = memcmp(output.vector[v] + 16, input.vector[v] + 16, 48) == 0;
            x86_semantics_expect(tally, !written || !(effects->properties & BUSTER_X86_EFFECT_KEEP_UPPER) || upper_kept, "%s changed vector register %u's bits above 127",
                                 x86_semantics_test_case, v);
        }
        const uint8_t values[BUSTER_X86_ROLES] = { record->reg, record->rm, record->vvvv };
        // Two operands that write the same register (xchg al, ah) each change the other's bytes: their rests aren't checked.
        const uint32_t reg_bits = x86_effects_role(effects, BUSTER_X86_ROLE_REG);
        const bool shared = (reg_bits & BUSTER_X86_ROLE_WRITE) && (x86_effects_role(effects, BUSTER_X86_ROLE_RM) & BUSTER_X86_ROLE_WRITE) && !memory &&
                            x86_semantics_register_number(record->reg) == x86_semantics_register_number(record->rm);
        for (uint32_t role = 0; role < BUSTER_X86_ROLES; role += 1)
        {
            const uint32_t bits = x86_effects_role(effects, role);
            const uint32_t rest = shared ? BUSTER_X86_REST_WHOLE : (bits & BUSTER_X86_ROLE_REST) >> BUSTER_X86_ROLE_REST_SHIFT;
            if ((bits & BUSTER_X86_ROLE_WRITE) && !(role == BUSTER_X86_ROLE_RM && memory))
            {
                const uint8_t value = values[role];
                changes->roles_written |= 1u << role;
                if (bits & BUSTER_X86_ROLE_VECTOR)
                {
                    const uint8_t* before = input.vector[value & 15];
                    const uint8_t* after = output.vector[value & 15];
                    const bool zeroes = rest == BUSTER_X86_REST_ZERO || (rest == BUSTER_X86_REST_MERGE_IF_REGISTER && memory);
                    const bool keeps = rest == BUSTER_X86_REST_MERGE || (rest == BUSTER_X86_REST_MERGE_IF_REGISTER && !memory);
                    x86_semantics_expect(tally, !zeroes || x86_semantics_zero_bytes(after, effects->write_bytes, 16), "%s didn't zero XMM%u's bytes %u to 15",
                                         x86_semantics_test_case, value & 15, effects->write_bytes);
                    x86_semantics_expect(tally, !keeps || memcmp(after + effects->write_bytes, before + effects->write_bytes, 16 - effects->write_bytes) == 0,
                                         "%s didn't keep XMM%u's bytes %u to 15", x86_semantics_test_case, value & 15, effects->write_bytes);
                    changes->roles_changed |= memcmp(before, after, 16) != 0 ? 1u << role : 0;
                }
                else
                {
                    const uint32_t number = x86_semantics_register_number(value);
                    const uint64_t before = input.gpr[number];
                    const uint64_t after = output.gpr[number];
                    const bool implicit = (effects->implicit_writes >> number) & 1;
                    if ((value & BUSTER_X86_REGISTER_HIGH_BYTE) && !shared)
                    {
                        x86_semantics_expect(tally, (before & ~0xff00ull) == (after & ~0xff00ull), "%s changed more of register %u than its high byte", x86_semantics_test_case,
                                             number);
                    }
                    else if (rest == BUSTER_X86_REST_MERGE && !implicit)
                    {
                        const uint64_t kept = ~x86_width_mask(effects->write_bytes);
                        x86_semantics_expect(tally, (before & kept) == (after & kept), "%s changed register %u past its %u bytes", x86_semantics_test_case, number,
                                             effects->write_bytes);
                    }
                    else if (rest == BUSTER_X86_REST_ZERO && !implicit)
                    {
                        x86_semantics_expect(tally, (after >> 32) == 0, "%s didn't zero register %u's upper half", x86_semantics_test_case, number);
                    }
                    changes->roles_changed |= before != after ? 1u << role : 0;
                }
            }
        }
        for (uint32_t g = 0; g < 16; g += 1)
        {
            if (((effects->implicit_writes >> g) & 1) && g != X86_REGISTER_RSP)
            {
                const uint64_t before = input.gpr[g];
                const uint64_t after = output.gpr[g];
                changes->implicit_written |= 1u << g;
                changes->implicit_changed |= before != after ? 1u << g : 0;
                // A narrow form writes at most AX or DX of an implicit register; a 32-bit one zeroes the upper half (but
                // cmpxchg, which may not write it at all).
                x86_semantics_expect(tally, effects->width >= 4 || (before >> 16) == (after >> 16), "%s changed register %u past its 16 bits", x86_semantics_test_case, g);
                x86_semantics_expect(tally, effects->width != 4 || (effects->properties & BUSTER_X86_EFFECT_CONDITIONAL) || (after >> 32) == 0,
                                     "%s didn't zero register %u's upper half", x86_semantics_test_case, g);
            }
        }
        // Flags it neither defines nor leaves undefined stay; DF stays.
        const uint8_t changed = flags_written | flags_undefined;
        const uint8_t flags_before = x86_semantics_flags(input.flags);
        const uint8_t flags_after = x86_semantics_flags(output.flags);
        x86_semantics_expect(tally, ((flags_before ^ flags_after) & ~changed & BUSTER_X86_FLAGS_ALL) == 0, "%s changed flags %#x, which it keeps", x86_semantics_test_case,
                             (flags_before ^ flags_after) & ~changed & BUSTER_X86_FLAGS_ALL);
        // Memory: the operand's bytes if it stores there, the stack's 8 below RSP if it pushes, and nothing else.
        const uint32_t rm_bits = x86_effects_role(effects, BUSTER_X86_ROLE_RM);
        const bool stores = memory && (rm_bits & BUSTER_X86_ROLE_WRITE) && !(effects->memory & BUSTER_X86_MEMORY_EFFECT_ADDRESS);
        const uint32_t operand_bytes = x86_form_description(record->form)->operand_bytes;
        bool data_kept = true;
        for (uint32_t i = 0; i < BUSTER_X86_SEMANTICS_TEST_PAGE; i += 1)
        {
            const bool inside = stores && i >= BUSTER_X86_SEMANTICS_TEST_OPERAND && i < BUSTER_X86_SEMANTICS_TEST_OPERAND + operand_bytes;
            data_kept &= inside || data[i] == data_after[i];
        }
        x86_semantics_expect(tally, data_kept, "%s changed memory it doesn't store to", x86_semantics_test_case);
        const bool pushes = (effects->memory & BUSTER_X86_MEMORY_EFFECT_STACK_WRITE) != 0;
        bool stack_kept = true;
        for (uint32_t i = 0; i < 2 * BUSTER_X86_SEMANTICS_TEST_PAGE; i += 1)
        {
            const bool inside = pushes && i >= BUSTER_X86_SEMANTICS_TEST_STACK_TOP - 8 && i < BUSTER_X86_SEMANTICS_TEST_STACK_TOP;
            stack_kept &= inside || stack[i] == stack_after[i];
        }
        x86_semantics_expect(tally, stack_kept, "%s changed stack memory it doesn't push to", x86_semantics_test_case);

        // Again, with every register, flag and vector register it doesn't read changed (RSP stays the stack's).
        second_input = input;
        for (uint32_t g = 0; g < 16; g += 1)
        {
            if (!(reads & BUSTER_X86_RESOURCE_GPR(g)) && g != X86_REGISTER_RSP)
            {
                second_input.gpr[g] = x86_test_random(state);
            }
        }
        const uint8_t unread_flags = (uint8_t)(BUSTER_X86_FLAGS_STATUS & ~flags_read);
        const uint8_t new_flags = (uint8_t)((x86_semantics_flags(input.flags) & ~unread_flags) | (x86_test_random(state) & unread_flags));
        second_input.flags = BUSTER_X86_SEMANTICS_TEST_RFLAGS_BASE | x86_semantics_rflags(new_flags);
        for (uint32_t v = 0; v < 16; v += 1)
        {
            if (!(reads & BUSTER_X86_RESOURCE_VECTOR(v)))
            {
                for (uint32_t i = 0; i < 64; i += 8)
                {
                    const uint64_t bits = x86_test_random(state);
                    memcpy(&second_input.vector[v][i], &bits, 8);
                }
            }
        }
        memcpy(machine->data, data, sizeof(data));
        memcpy(machine->stack, stack, sizeof(stack));
        second_output = second_input;
        x86_semantics_machine_run(machine, &second_output);
        for (uint32_t g = 0; g < 16; g += 1)
        {
            const bool written = (writes & BUSTER_X86_RESOURCE_GPR(g)) != 0;
            x86_semantics_expect(tally, !written || second_output.gpr[g] == output.gpr[g], "%s wrote general register %u from something it doesn't read",
                                 x86_semantics_test_case, g);
        }
        for (uint32_t v = 0; v < 16; v += 1)
        {
            // The whole register, bits 511 to 128 included: an unread register is changed whole.
            const bool written = (writes & BUSTER_X86_RESOURCE_VECTOR(v)) != 0;
            x86_semantics_expect(tally, !written || memcmp(second_output.vector[v], output.vector[v], 64) == 0, "%s wrote ZMM%u from something it doesn't read",
                                 x86_semantics_test_case, v);
        }
        const uint8_t defined_difference = (x86_semantics_flags(second_output.flags) ^ x86_semantics_flags(output.flags)) & flags_written;
        x86_semantics_expect(tally, defined_difference == 0, "%s defined flags %#x from something it doesn't read", x86_semantics_test_case, defined_difference);
        x86_semantics_expect(tally, memcmp(machine->data, data_after, sizeof(data_after)) == 0 && memcmp(machine->stack, stack_after, sizeof(stack_after)) == 0,
                             "%s stored something it doesn't read", x86_semantics_test_case);

        // A destination that keeps its register's other bits is read for them; whether it reads the bytes it defines is
        // its READ bit (add al, cl does, mov al, cl doesn't; addsd does, sqrtsd doesn't). Without it, changing only those
        // bytes must change nothing the record leaves.
        for (uint32_t role = 0; role < BUSTER_X86_ROLES; role += 1)
        {
            const uint32_t bits = x86_effects_role(effects, role);
            const uint32_t rest = (bits & BUSTER_X86_ROLE_REST) >> BUSTER_X86_ROLE_REST_SHIFT;
            const bool merges = rest == BUSTER_X86_REST_MERGE || (rest == BUSTER_X86_REST_MERGE_IF_REGISTER && !memory);
            const bool register_operand = !(role == BUSTER_X86_ROLE_RM && memory);
            if ((bits & BUSTER_X86_ROLE_WRITE) && !(bits & BUSTER_X86_ROLE_READ) && merges && register_operand && !shared)
            {
                const uint8_t value = values[role];
                // Other operands that read the same register would see the change: skip those records.
                bool read_elsewhere = false;
                for (uint32_t other = 0; other < BUSTER_X86_ROLES; other += 1)
                {
                    const uint32_t other_bits = x86_effects_role(effects, other);
                    const bool other_register = !(other == BUSTER_X86_ROLE_RM && memory);
                    read_elsewhere |= other != role && (other_bits & BUSTER_X86_ROLE_READ) && other_register &&
                                      ((other_bits ^ bits) & BUSTER_X86_ROLE_VECTOR) == 0 &&
                                      x86_semantics_register_number(values[other]) == x86_semantics_register_number(value);
                }
                const bool vector_role = (bits & BUSTER_X86_ROLE_VECTOR) != 0;
                read_elsewhere |= !vector_role && ((effects->implicit_reads >> x86_semantics_register_number(value)) & 1);
                if (!read_elsewhere)
                {
                    second_input = input;
                    if (vector_role)
                    {
                        for (uint32_t i = 0; i < effects->write_bytes; i += 1)
                        {
                            second_input.vector[value & 15][i] ^= (uint8_t)(1 + x86_random_below(state, 255));
                        }
                    }
                    else
                    {
                        const uint32_t shift = (value & BUSTER_X86_REGISTER_HIGH_BYTE) ? 8 : 0;
                        const uint64_t defined = x86_width_mask(effects->write_bytes) << shift;
                        second_input.gpr[x86_semantics_register_number(value)] ^= (x86_test_random(state) | 1) & defined;
                    }
                    memcpy(machine->data, data, sizeof(data));
                    memcpy(machine->stack, stack, sizeof(stack));
                    second_output = second_input;
                    x86_semantics_machine_run(machine, &second_output);
                    x86_semantics_expect(tally, memcmp(second_output.gpr, output.gpr, sizeof(output.gpr)) == 0 && second_output.flags == output.flags &&
                                                    memcmp(second_output.vector, output.vector, sizeof(output.vector)) == 0,
                                         "%s read the bytes its destination (role %u) defines, which it says it only writes", x86_semantics_test_case, role);
                }
            }
        }
    }
}

// Every covered form but those that branch, call, return or trap, from BUSTER_X86_SEMANTICS_TEST_RUNS random states,
// with register and memory operands in turn. A write that no run showed (a byte rotated by 8, a value that is its own
// result, a condition that didn't hold) gets up to BUSTER_X86_SEMANTICS_TEST_WITNESS_RUNS more runs, with register
// operands, until it shows; one that never does is listed as missing coverage, not failed: the runs' other checks fail a
// write the table lacks (a register changed that it doesn't write), and one it claims and the form never makes is only
// conservative.
static bool x86_semantics_check_table(X86SemanticsMachine* machine, uint64_t seed, uint32_t runs)
{
    X86SemanticsTally tally = {
        .check = "effects: every covered form executed",
    };
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 11;
    uint32_t forms = 0;
    uint32_t skipped = 0;
    uint32_t witness_runs = 0;
    uint32_t never_shown = 0;
    for (uint32_t form = 1; form < X86_FORM_COUNT; form += 1)
    {
        const X86Effects* effects = x86_effects(form);
        if (effects->operation != X86_OPERATION_NONE && x86_effects_control(effects) != BUSTER_X86_CONTROL_NONE)
        {
            skipped += 1;
        }
        else if (effects->operation != X86_OPERATION_NONE)
        {
            X86SemanticsChanges changes = { 0 };
            for (uint32_t run = 0; run < runs; run += 1)
            {
                const X86Instruction record = x86_semantics_random_record(&state, form, (run & 1) != 0);
                x86_semantics_check_run(&tally, machine, &state, &record, &changes);
            }
            uint32_t extra = 0;
            bool unshown = (changes.roles_written & ~changes.roles_changed) != 0 || (changes.implicit_written & ~changes.implicit_changed) != 0;
            while (unshown && extra < BUSTER_X86_SEMANTICS_TEST_WITNESS_RUNS)
            {
                const X86Instruction record = x86_semantics_random_record(&state, form, false);
                x86_semantics_check_run(&tally, machine, &state, &record, &changes);
                extra += 1;
                unshown = (changes.roles_written & ~changes.roles_changed) != 0 || (changes.implicit_written & ~changes.implicit_changed) != 0;
            }
            if (unshown)
            {
                printf("    effects: %s: writes never shown in %u runs (operands %#x, implicit registers %#x): missing coverage\n", x86_semantics_test_name(form),
                       runs + extra, changes.roles_written & ~changes.roles_changed, changes.implicit_written & ~changes.implicit_changed);
            }
            witness_runs += extra;
            never_shown += unshown;
            forms += 1;
        }
    }
    printf("effects: %u covered forms executed %u times each and %u more until every write showed (%u forms with one that never did), %u that branch, call, "
           "return or trap left to the fixtures\n",
           forms, runs, witness_runs, never_shown, skipped);
    return x86_semantics_report(&tally);
}

// Whether condition code `condition` (as jcc's, setcc's and cmovcc's low nibble numbers it: o, no, b, ae, e, ne, be, a,
// s, ns, p, np, l, ge, le, g) holds for the flags.
static bool x86_semantics_condition(uint32_t condition, uint8_t flags)
{
    const bool cf = (flags & BUSTER_X86_FLAG_CF) != 0;
    const bool pf = (flags & BUSTER_X86_FLAG_PF) != 0;
    const bool zf = (flags & BUSTER_X86_FLAG_ZF) != 0;
    const bool sf = (flags & BUSTER_X86_FLAG_SF) != 0;
    const bool of = (flags & BUSTER_X86_FLAG_OF) != 0;
    const bool tests[8] = { of, cf, zf, cf || zf, sf, pf, sf != of, zf || sf != of };
    return tests[condition >> 1] != ((condition & 1) != 0);
}

// cmovcc and setcc on every condition and every combination of the status flags, against x86_semantics_condition: cmov
// moves the source when the condition holds and keeps the destination otherwise, but a 32-bit one zeroes its upper half
// either way; setcc writes 1 or 0 to the low byte and keeps the rest. And the flags each reads are the condition's:
// changing any other leaves the result alone.
static bool x86_semantics_check_conditions(X86SemanticsMachine* machine, uint64_t seed)
{
    X86SemanticsTally tally = {
        .check = "effects: conditions on every flag",
    };
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 29;
    static X86SemanticsState input;
    static X86SemanticsState output;
    for (uint32_t condition = 0; condition < 16; condition += 1)
    {
        const uint16_t setcc = (uint16_t)(X86_FORM_SETO_RM8 + 6 * condition);
        const uint16_t cmovs[3] = { (uint16_t)(X86_FORM_CMOVO_R16_RM16 + 6 * condition), (uint16_t)(X86_FORM_CMOVO_R32_RM32 + 6 * condition),
                                    (uint16_t)(X86_FORM_CMOVO_R64_RM64 + 6 * condition) };
        const uint8_t condition_flags = x86_effects(setcc)->flags_read;
        x86_semantics_expect(&tally, x86_effects(setcc)->operation == X86_OPERATION_SETCC, "condition %u's setcc form", condition);
        for (uint32_t flags = 0; flags <= BUSTER_X86_FLAGS_STATUS; flags += 1)
        {
            const bool holds = x86_semantics_condition(condition, (uint8_t)flags);
            // Flags outside the ones it reads don't change the result: the same with them all flipped.
            const bool holds_flipped = x86_semantics_condition(condition, (uint8_t)(flags ^ (BUSTER_X86_FLAGS_STATUS & ~condition_flags)));
            x86_semantics_expect(&tally, holds == holds_flipped, "condition %u reads flags outside %#x", condition, condition_flags);
            for (uint32_t width_index = 0; width_index < 4; width_index += 1)
            {
                const bool set = width_index == 3;
                const X86Instruction record = {
                    .form = set ? setcc : cmovs[width_index],
                    .reg = set ? 0 : (uint8_t)X86_REGISTER_RBX,
                    .rm = set ? (uint8_t)X86_REGISTER_RBX : (uint8_t)X86_REGISTER_RSI,
                };
                snprintf(x86_semantics_test_case, sizeof(x86_semantics_test_case), "%s with flags %#x", x86_semantics_test_name(record.form), flags);
                x86_semantics_random_state(&state, machine, &input);
                input.flags = BUSTER_X86_SEMANTICS_TEST_RFLAGS_BASE | x86_semantics_rflags((uint8_t)flags);
                output = input;
                if (x86_semantics_expect(&tally, x86_semantics_machine_load(machine, &record, 1), "%s encodes", x86_semantics_test_case))
                {
                    x86_semantics_machine_run(machine, &output);
                    const uint64_t before = input.gpr[X86_REGISTER_RBX];
                    const uint64_t source = input.gpr[X86_REGISTER_RSI];
                    uint64_t expected = 0;
                    if (set)
                    {
                        expected = (before & ~0xffull) | (holds ? 1 : 0);
                    }
                    else if (width_index == 0)
                    {
                        expected = holds ? (before & ~0xffffull) | (source & 0xffff) : before;
                    }
                    else if (width_index == 1)
                    {
                        expected = holds ? source & 0xffffffffull : before & 0xffffffffull;
                    }
                    else
                    {
                        expected = holds ? source : before;
                    }
                    x86_semantics_expect(&tally, output.gpr[X86_REGISTER_RBX] == expected && x86_semantics_flags(output.flags) == flags, "%s: %#llx, not %#llx",
                                         x86_semantics_test_case, (unsigned long long)output.gpr[X86_REGISTER_RBX], (unsigned long long)expected);
                }
            }
        }
    }
    return x86_semantics_report(&tally);
}

// push, pop and leave move RSP as they say: push writes the 8 bytes below it, pop and leave read [rsp] and [rbp].
static bool x86_semantics_check_stack(X86SemanticsMachine* machine, uint64_t seed)
{
    X86SemanticsTally tally = {
        .check = "effects: the stack",
    };
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 13;
    static X86SemanticsState input;
    static X86SemanticsState output;
    const uint64_t top = (uint64_t)(machine->stack + BUSTER_X86_SEMANTICS_TEST_STACK_TOP);
    const X86Instruction push = {
        .form = X86_FORM_PUSH_R64,
        .rm = X86_REGISTER_RBX,
    };
    x86_semantics_random_state(&state, machine, &input);
    output = input;
    snprintf(x86_semantics_test_case, sizeof(x86_semantics_test_case), "push rbx");
    if (x86_semantics_expect(&tally, x86_semantics_machine_load(machine, &push, 1), "push rbx encodes"))
    {
        x86_semantics_machine_run(machine, &output);
        uint64_t pushed = 0;
        memcpy(&pushed, machine->stack + BUSTER_X86_SEMANTICS_TEST_STACK_TOP - 8, 8);
        x86_semantics_expect(&tally, output.gpr[X86_REGISTER_RSP] == top - 8 && pushed == input.gpr[X86_REGISTER_RBX], "push rbx: RSP %#llx, pushed %#llx",
                             (unsigned long long)output.gpr[X86_REGISTER_RSP], (unsigned long long)pushed);
    }
    const X86Instruction pop = {
        .form = X86_FORM_POP_R64,
        .rm = X86_REGISTER_R12,
    };
    x86_semantics_random_state(&state, machine, &input);
    output = input;
    snprintf(x86_semantics_test_case, sizeof(x86_semantics_test_case), "pop r12");
    if (x86_semantics_expect(&tally, x86_semantics_machine_load(machine, &pop, 1), "pop r12 encodes"))
    {
        uint64_t top_value = 0;
        memcpy(&top_value, machine->stack + BUSTER_X86_SEMANTICS_TEST_STACK_TOP, 8);
        x86_semantics_machine_run(machine, &output);
        x86_semantics_expect(&tally, output.gpr[X86_REGISTER_RSP] == top + 8 && output.gpr[X86_REGISTER_R12] == top_value, "pop r12: RSP %#llx, r12 %#llx",
                             (unsigned long long)output.gpr[X86_REGISTER_RSP], (unsigned long long)output.gpr[X86_REGISTER_R12]);
    }
    const X86Instruction leave = {
        .form = X86_FORM_LEAVE,
    };
    x86_semantics_random_state(&state, machine, &input);
    input.gpr[X86_REGISTER_RBP] = top + 64;
    output = input;
    snprintf(x86_semantics_test_case, sizeof(x86_semantics_test_case), "leave");
    if (x86_semantics_expect(&tally, x86_semantics_machine_load(machine, &leave, 1), "leave encodes"))
    {
        uint64_t saved = 0;
        memcpy(&saved, machine->stack + BUSTER_X86_SEMANTICS_TEST_STACK_TOP + 64, 8);
        x86_semantics_machine_run(machine, &output);
        x86_semantics_expect(&tally, output.gpr[X86_REGISTER_RSP] == top + 72 && output.gpr[X86_REGISTER_RBP] == saved, "leave: RSP %#llx, RBP %#llx",
                             (unsigned long long)output.gpr[X86_REGISTER_RSP], (unsigned long long)output.gpr[X86_REGISTER_RBP]);
    }
    return x86_semantics_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The immediate's contract

// The boundary values: around every width's limits, signed and unsigned, and the issue's.
static const int64_t x86_semantics_values[] = {
    INT64_MIN,
    INT64_MIN + 1,
    -4294967297ll,
    -4294967296ll,
    -2147483649ll,
    -2147483648ll,
    -2147483647ll,
    -65537,
    -65536,
    -32769,
    -32768,
    -32767,
    -257,
    -256,
    -255,
    -200,
    -129,
    -128,
    -127,
    -2,
    -1,
    0,
    1,
    2,
    3,
    7,
    8,
    9,
    15,
    16,
    17,
    31,
    32,
    33,
    63,
    64,
    65,
    126,
    127,
    128,
    129,
    200,
    254,
    255,
    256,
    257,
    32767,
    32768,
    32769,
    65535,
    65536,
    2147483647ll,
    2147483648ll,
    2147483649ll,
    4294967295ll,
    4294967296ll,
    0x7fffffffffffffffll,
};

// For every covered form with an immediate and every boundary value: a value the form takes comes back from its field
// (x86_immediate_value), and its record validates; a value of the form's width it refuses doesn't (its field's bits
// stand for another value: an imm8 of 0xc8 is -56, not 200).
static bool x86_semantics_check_immediates(void)
{
    X86SemanticsTally tally = {
        .check = "immediates: the contract",
    };
    uint64_t forms = 0;
    uint64_t taken = 0;
    for (uint32_t form = 1; form < X86_FORM_COUNT; form += 1)
    {
        const X86Effects* effects = x86_effects(form);
        const X86FormDescription* description = &x86_form_descriptions[form];
        const uint32_t kind = x86_effects_immediate_kind(effects);
        if (effects->operation != X86_OPERATION_NONE && kind != BUSTER_X86_IMMEDIATE_NONE)
        {
            forms += 1;
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_semantics_values); i += 1)
            {
                const int64_t value = x86_semantics_values[i];
                const bool encodes = x86_immediate_encodes(form, value);
                int32_t immediate = 0;
                int32_t high = 0;
                x86_immediate_field(form, value, &immediate, &high);
                const uint64_t back = x86_immediate_value(form, immediate, high);
                const uint64_t pattern = (uint64_t)value & x86_width_mask(effects->width);
                const uint64_t expected = kind == BUSTER_X86_IMMEDIATE_VALUE ? pattern : (uint64_t)value;
                taken += encodes;
                if (encodes)
                {
                    x86_semantics_expect(&tally, back == expected, "%s takes %lld, but its field %d stands for %#llx", x86_semantics_test_name(form), (long long)value, immediate,
                                         (unsigned long long)back);
                    // The record (with registers the form takes) validates.
                    X86Instruction record = {
                        .form = (uint16_t)form,
                        .immediate = immediate,
                        .displacement = description->immediate_bytes == 8 ? high : 0,
                    };
                    record.reg = description->reg != BUSTER_X86_OPERAND_NONE ? (uint8_t)X86_REGISTER_RCX : 0;
                    if (description->rm != BUSTER_X86_OPERAND_NONE)
                    {
                        record.rm = (description->rm & BUSTER_X86_OPERAND_REGISTER) ? (uint8_t)X86_REGISTER_RDX : 0;
                        record.mode = (description->rm & BUSTER_X86_OPERAND_REGISTER) ? 0 : BUSTER_X86_MEMORY_RIP;
                    }
                    x86_semantics_expect(&tally, x86_validate(&record, 1) == 1, "%s with the field of %lld doesn't validate", x86_semantics_test_name(form), (long long)value);
                }
                else if (kind == BUSTER_X86_IMMEDIATE_VALUE && x86_value_fits_width(value, effects->width))
                {
                    x86_semantics_expect(&tally, back != expected, "%s refuses %lld, but its field stands for it", x86_semantics_test_name(form), (long long)value);
                }
            }
        }
    }
    // The issue's example: add rax, 200 needs the imm32; the imm8's field 0xc8 adds -56.
    x86_semantics_expect(&tally, !x86_immediate_encodes(X86_FORM_ADD_RM64_IMM8, 200) && x86_immediate_encodes(X86_FORM_ADD_RM64_IMM32, 200), "add rax, 200 takes the imm32");
    x86_semantics_expect(&tally, x86_immediate_value(X86_FORM_ADD_RM64_IMM8, (int8_t)0xc8, 0) == (uint64_t)-56, "an imm8 of 0xc8 in add rax, imm8 is -56");
    // At 32 bits, 0xffffffff is -1: the imm8 takes it.
    x86_semantics_expect(&tally, x86_immediate_encodes(X86_FORM_ADD_RM32_IMM8, 4294967295ll) && !x86_immediate_encodes(X86_FORM_ADD_RM64_IMM8, 4294967295ll),
                         "0xffffffff is -1 at 32 bits, not at 64");
    // A value outside its width is no value of the operation.
    x86_semantics_expect(&tally, !x86_immediate_encodes(X86_FORM_ADD_RM8_IMM8, 256) && !x86_immediate_encodes(X86_FORM_ADD_RM8_IMM8, -129), "256 and -129 aren't bytes");
    // Counts: [0, 31], or [0, 63] for a 64-bit operand, refused past it rather than masked.
    x86_semantics_expect(&tally, x86_immediate_encodes(X86_FORM_SHL_RM32_IMM8, 31) && !x86_immediate_encodes(X86_FORM_SHL_RM32_IMM8, 32) &&
                                     x86_immediate_encodes(X86_FORM_SHL_RM64_IMM8, 63) && !x86_immediate_encodes(X86_FORM_SHL_RM64_IMM8, 64) &&
                                     !x86_immediate_encodes(X86_FORM_SHL_RM64_IMM8, -1),
                         "shift counts within the masking");
    printf("immediates: %llu covered forms with an immediate, %llu of their value pairs taken\n", (unsigned long long)forms, (unsigned long long)taken);
    return x86_semantics_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Form choice

// Where a request's destination is: a general register other than RAX, RAX, or memory (for imul, its source).
typedef enum X86SemanticsDestination
{
    X86_SEMANTICS_DESTINATION_REGISTER,
    X86_SEMANTICS_DESTINATION_ACCUMULATOR,
    X86_SEMANTICS_DESTINATION_MEMORY,
} X86SemanticsDestination;

// One request's form, by both paths, which must agree.
static uint16_t x86_semantics_choose(X86SemanticsTally* tally, uint32_t row, int64_t value, uint16_t context)
{
    const uint16_t rows[1] = { (uint16_t)row };
    const int64_t values[1] = { value };
    const uint16_t contexts[1] = { context };
    uint16_t scalar = 0xffff;
    uint16_t vector = 0xffff;
    x86_choose_forms_scalar(rows, values, contexts, 1, &scalar);
    x86_choose_forms(rows, values, contexts, 1, &vector);
    x86_semantics_expect(tally, scalar == vector, "row %u, value %lld, context %#x: scalar %u, vector %u", row, (long long)value, context, scalar, vector);
    return scalar;
}

// The context word for a destination, live flags and inc and dec allowed or not.
static uint16_t x86_semantics_context(X86SemanticsDestination destination, uint8_t live, bool inc_dec)
{
    uint16_t result = live;
    result |= destination != X86_SEMANTICS_DESTINATION_MEMORY ? BUSTER_X86_CONTEXT_REGISTER : 0;
    result |= destination == X86_SEMANTICS_DESTINATION_ACCUMULATOR ? BUSTER_X86_CONTEXT_ACCUMULATOR : 0;
    result |= inc_dec ? BUSTER_X86_CONTEXT_INC_DEC : 0;
    return result;
}

typedef struct X86SemanticsChoiceFixture X86SemanticsChoiceFixture;
struct X86SemanticsChoiceFixture
{
    uint8_t operation;
    uint8_t width;
    uint8_t destination;
    uint8_t live;
    bool inc_dec;
    int64_t value;
    uint16_t form;
};

#define BUSTER_X86_LIVE BUSTER_X86_FLAGS_STATUS
#define BUSTER_X86_CHOICE_FIXTURE(operation, width, destination, live, inc_dec, value, form) \
    { X86_OPERATION_##operation, width, X86_SEMANTICS_DESTINATION_##destination, live, inc_dec, value, X86_FORM_##form },
static const X86SemanticsChoiceFixture x86_semantics_choice_fixtures[] = {
    // The issue's boundaries at every width: a value fits an imm8 only if its sign extension gives it back.
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, false, -129, ADD_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, false, -128, ADD_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, false, 127, ADD_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, false, 128, ADD_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, false, 200, ADD_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, ACCUMULATOR, BUSTER_X86_LIVE, false, 200, ADD_RAX_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, false, 255, ADD_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, false, 256, ADD_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, MEMORY, BUSTER_X86_LIVE, false, 200, ADD_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, false, 4294967295ll, NONE)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, false, -1, ADD_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, REGISTER, BUSTER_X86_LIVE, false, -129, ADD_RM32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, REGISTER, BUSTER_X86_LIVE, false, -128, ADD_RM32_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, REGISTER, BUSTER_X86_LIVE, false, 127, ADD_RM32_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, REGISTER, BUSTER_X86_LIVE, false, 128, ADD_RM32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, REGISTER, BUSTER_X86_LIVE, false, 200, ADD_RM32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, ACCUMULATOR, BUSTER_X86_LIVE, false, 200, ADD_EAX_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, REGISTER, BUSTER_X86_LIVE, false, 255, ADD_RM32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, REGISTER, BUSTER_X86_LIVE, false, 256, ADD_RM32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, REGISTER, BUSTER_X86_LIVE, false, 4294967295ll, ADD_RM32_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 4, REGISTER, BUSTER_X86_LIVE, false, 4294967296ll, NONE)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 2, REGISTER, BUSTER_X86_LIVE, false, -129, ADD_RM16_IMM16)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 2, REGISTER, BUSTER_X86_LIVE, false, -128, ADD_RM16_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 2, REGISTER, BUSTER_X86_LIVE, false, 127, ADD_RM16_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 2, REGISTER, BUSTER_X86_LIVE, false, 128, ADD_RM16_IMM16)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 2, REGISTER, BUSTER_X86_LIVE, false, 200, ADD_RM16_IMM16)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 2, ACCUMULATOR, BUSTER_X86_LIVE, false, 200, ADD_AX_IMM16)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 2, REGISTER, BUSTER_X86_LIVE, false, 255, ADD_RM16_IMM16)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 2, REGISTER, BUSTER_X86_LIVE, false, 256, ADD_RM16_IMM16)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 2, REGISTER, BUSTER_X86_LIVE, false, 65535, ADD_RM16_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 1, REGISTER, BUSTER_X86_LIVE, false, -129, NONE)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 1, REGISTER, BUSTER_X86_LIVE, false, -128, ADD_RM8_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 1, REGISTER, BUSTER_X86_LIVE, false, 127, ADD_RM8_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 1, REGISTER, BUSTER_X86_LIVE, false, 128, ADD_RM8_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 1, REGISTER, BUSTER_X86_LIVE, false, 200, ADD_RM8_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 1, ACCUMULATOR, BUSTER_X86_LIVE, false, 200, ADD_AL_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 1, REGISTER, BUSTER_X86_LIVE, false, 255, ADD_RM8_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 1, REGISTER, BUSTER_X86_LIVE, false, 256, NONE)
    BUSTER_X86_CHOICE_FIXTURE(CMP, 8, REGISTER, BUSTER_X86_LIVE, false, 200, CMP_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(AND, 4, MEMORY, BUSTER_X86_LIVE, false, -1, AND_RM32_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(IMUL, 8, REGISTER, BUSTER_X86_LIVE, false, 127, IMUL_R64_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(IMUL, 8, REGISTER, BUSTER_X86_LIVE, false, 128, IMUL_R64_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(IMUL, 8, REGISTER, BUSTER_X86_LIVE, false, 2147483648ll, NONE)
    BUSTER_X86_CHOICE_FIXTURE(IMUL, 2, MEMORY, BUSTER_X86_LIVE, false, 200, IMUL_R16_RM16_IMM16)
    BUSTER_X86_CHOICE_FIXTURE(PUSH, 8, REGISTER, BUSTER_X86_LIVE, false, -128, PUSH_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(PUSH, 8, REGISTER, BUSTER_X86_LIVE, false, 200, PUSH_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(PUSH, 8, REGISTER, BUSTER_X86_LIVE, false, -2147483648ll, PUSH_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(PUSH, 8, REGISTER, BUSTER_X86_LIVE, false, 2147483648ll, NONE)
    // Constants: a 32-bit write zero-extends, an imm32 in a 64-bit form sign-extends, and movabs takes the rest.
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, 0, false, 0, XOR_RM32_R32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, BUSTER_X86_FLAG_ZF, false, 0, MOV_R32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, BUSTER_X86_LIVE, false, 2147483647ll, MOV_R32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, BUSTER_X86_LIVE, false, 2147483648ll, MOV_R32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, BUSTER_X86_LIVE, false, 4294967295ll, MOV_R32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, BUSTER_X86_LIVE, false, -1, MOV_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, BUSTER_X86_LIVE, false, -2147483648ll, MOV_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, BUSTER_X86_LIVE, false, -2147483649ll, MOV_R64_IMM64)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, BUSTER_X86_LIVE, false, 4294967296ll, MOV_R64_IMM64)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, REGISTER, BUSTER_X86_LIVE, false, INT64_MIN, MOV_R64_IMM64)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, MEMORY, BUSTER_X86_LIVE, false, -1, MOV_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, MEMORY, BUSTER_X86_LIVE, false, 2147483648ll, NONE)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 8, MEMORY, 0, false, 0, MOV_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 4, REGISTER, 0, false, 0, XOR_RM32_R32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 4, REGISTER, BUSTER_X86_LIVE, false, 4294967295ll, MOV_R32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 4, REGISTER, BUSTER_X86_LIVE, false, -1, MOV_R32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 2, REGISTER, 0, false, 0, MOV_R16_IMM16)
    BUSTER_X86_CHOICE_FIXTURE(MOV, 1, REGISTER, BUSTER_X86_LIVE, false, 200, MOV_R8_IMM8)
    // Shift counts: by 1 the short form, within the masking the imm8, past it nothing.
    BUSTER_X86_CHOICE_FIXTURE(SHL, 4, REGISTER, BUSTER_X86_LIVE, false, 1, SHL_RM32_1)
    BUSTER_X86_CHOICE_FIXTURE(SHL, 4, REGISTER, BUSTER_X86_LIVE, false, 0, SHL_RM32_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(SHL, 4, REGISTER, BUSTER_X86_LIVE, false, 31, SHL_RM32_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(SHL, 4, REGISTER, BUSTER_X86_LIVE, false, 32, NONE)
    BUSTER_X86_CHOICE_FIXTURE(SAR, 8, MEMORY, BUSTER_X86_LIVE, false, 63, SAR_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(SAR, 8, REGISTER, BUSTER_X86_LIVE, false, 64, NONE)
    BUSTER_X86_CHOICE_FIXTURE(ROR, 1, REGISTER, BUSTER_X86_LIVE, false, 31, ROR_RM8_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(SHR, 2, REGISTER, BUSTER_X86_LIVE, false, -1, NONE)
    // Substitutions, by the live flags: inc and dec (allowed, and CF, and for -1 AF, dead), test r, r (AF dead), and a test
    // narrowed (on a register; with bit 31 set, SF dead).
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_LIVE, true, 1, ADD_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_FLAG_ZF, true, 1, INC_RM64)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_FLAG_ZF, false, 1, ADD_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, MEMORY, BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAG_CF, true, 1, INC_RM64)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAG_CF, true, -1, ADD_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(ADD, 8, REGISTER, BUSTER_X86_FLAGS_STATUS & ~(BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_AF), true, -1, DEC_RM64)
    BUSTER_X86_CHOICE_FIXTURE(SUB, 4, REGISTER, 0, true, 1, DEC_RM32)
    BUSTER_X86_CHOICE_FIXTURE(SUB, 1, REGISTER, 0, true, 255, INC_RM8)
    BUSTER_X86_CHOICE_FIXTURE(CMP, 8, REGISTER, BUSTER_X86_FLAG_ZF, false, 0, TEST_RM64_R64)
    BUSTER_X86_CHOICE_FIXTURE(CMP, 8, REGISTER, BUSTER_X86_LIVE, false, 0, CMP_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(CMP, 8, MEMORY, BUSTER_X86_FLAG_ZF, false, 0, CMP_RM64_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(TEST, 8, REGISTER, BUSTER_X86_LIVE, false, 100, TEST_RM8_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(TEST, 8, ACCUMULATOR, BUSTER_X86_LIVE, false, 100, TEST_AL_IMM8)
    BUSTER_X86_CHOICE_FIXTURE(TEST, 8, REGISTER, BUSTER_X86_LIVE, false, 200, TEST_RM32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(TEST, 8, REGISTER, BUSTER_X86_LIVE, false, 2147483648ll, NONE)
    BUSTER_X86_CHOICE_FIXTURE(TEST, 8, REGISTER, BUSTER_X86_FLAG_ZF, false, 2147483648ll, TEST_RM32_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(TEST, 8, MEMORY, BUSTER_X86_LIVE, false, 100, TEST_RM64_IMM32)
    BUSTER_X86_CHOICE_FIXTURE(TEST, 2, REGISTER, BUSTER_X86_LIVE, false, 200, TEST_RM16_IMM16)
    BUSTER_X86_CHOICE_FIXTURE(TEST, 4, REGISTER, BUSTER_X86_LIVE, false, -1, TEST_RM32_IMM32)
};
#undef BUSTER_X86_CHOICE_FIXTURE

static bool x86_semantics_check_choice_fixtures(void)
{
    X86SemanticsTally tally = {
        .check = "choice: fixtures",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_semantics_choice_fixtures); i += 1)
    {
        const X86SemanticsChoiceFixture* fixture = &x86_semantics_choice_fixtures[i];
        const uint32_t row = x86_choice_row(fixture->operation, fixture->width);
        if (x86_semantics_expect(&tally, row != BUSTER_X86_CHOICE_NO_ROW, "operation %u at width %u has a row", fixture->operation, fixture->width))
        {
            const uint16_t context = x86_semantics_context(fixture->destination, fixture->live, fixture->inc_dec);
            const uint16_t form = x86_semantics_choose(&tally, row, fixture->value, context);
            x86_semantics_expect(&tally, form == fixture->form, "operation %u at %u bytes, %lld, context %#x: %s, not %s", fixture->operation, fixture->width,
                                 (long long)fixture->value, context, x86_semantics_test_name(form), x86_semantics_test_name(fixture->form));
        }
    }
    x86_semantics_expect(&tally, x86_choice_row(X86_OPERATION_ADC, 3) == BUSTER_X86_CHOICE_NO_ROW && x86_choice_row(X86_OPERATION_LEA, 8) == BUSTER_X86_CHOICE_NO_ROW,
                         "no row for a width of 3 or for lea");
    return x86_semantics_report(&tally);
}

// The register form of a row's operation at its width (its value in a register instead of an immediate), for the
// reference sequence.
static uint16_t x86_semantics_register_form(uint32_t operation, uint32_t width)
{
    const uint32_t log2 = width == 1 ? 0 : width == 2 ? 1 : width == 4 ? 2 : 3;
    static const uint16_t forms[][4] = {
        [X86_OPERATION_ADD] = { X86_FORM_ADD_RM8_R8, X86_FORM_ADD_RM16_R16, X86_FORM_ADD_RM32_R32, X86_FORM_ADD_RM64_R64 },
        [X86_OPERATION_OR] = { X86_FORM_OR_RM8_R8, X86_FORM_OR_RM16_R16, X86_FORM_OR_RM32_R32, X86_FORM_OR_RM64_R64 },
        [X86_OPERATION_ADC] = { X86_FORM_ADC_RM8_R8, X86_FORM_ADC_RM16_R16, X86_FORM_ADC_RM32_R32, X86_FORM_ADC_RM64_R64 },
        [X86_OPERATION_SBB] = { X86_FORM_SBB_RM8_R8, X86_FORM_SBB_RM16_R16, X86_FORM_SBB_RM32_R32, X86_FORM_SBB_RM64_R64 },
        [X86_OPERATION_AND] = { X86_FORM_AND_RM8_R8, X86_FORM_AND_RM16_R16, X86_FORM_AND_RM32_R32, X86_FORM_AND_RM64_R64 },
        [X86_OPERATION_SUB] = { X86_FORM_SUB_RM8_R8, X86_FORM_SUB_RM16_R16, X86_FORM_SUB_RM32_R32, X86_FORM_SUB_RM64_R64 },
        [X86_OPERATION_XOR] = { X86_FORM_XOR_RM8_R8, X86_FORM_XOR_RM16_R16, X86_FORM_XOR_RM32_R32, X86_FORM_XOR_RM64_R64 },
        [X86_OPERATION_CMP] = { X86_FORM_CMP_RM8_R8, X86_FORM_CMP_RM16_R16, X86_FORM_CMP_RM32_R32, X86_FORM_CMP_RM64_R64 },
        [X86_OPERATION_TEST] = { X86_FORM_TEST_RM8_R8, X86_FORM_TEST_RM16_R16, X86_FORM_TEST_RM32_R32, X86_FORM_TEST_RM64_R64 },
        [X86_OPERATION_MOV] = { X86_FORM_MOV_RM8_R8, X86_FORM_MOV_RM16_R16, X86_FORM_MOV_RM32_R32, X86_FORM_MOV_RM64_R64 },
        [X86_OPERATION_IMUL] = { X86_FORM_NONE, X86_FORM_IMUL_R16_RM16, X86_FORM_IMUL_R32_RM32, X86_FORM_IMUL_R64_RM64 },
        [X86_OPERATION_ROL] = { X86_FORM_ROL_RM8_CL, X86_FORM_ROL_RM16_CL, X86_FORM_ROL_RM32_CL, X86_FORM_ROL_RM64_CL },
        [X86_OPERATION_ROR] = { X86_FORM_ROR_RM8_CL, X86_FORM_ROR_RM16_CL, X86_FORM_ROR_RM32_CL, X86_FORM_ROR_RM64_CL },
        [X86_OPERATION_SHL] = { X86_FORM_SHL_RM8_CL, X86_FORM_SHL_RM16_CL, X86_FORM_SHL_RM32_CL, X86_FORM_SHL_RM64_CL },
        [X86_OPERATION_SHR] = { X86_FORM_SHR_RM8_CL, X86_FORM_SHR_RM16_CL, X86_FORM_SHR_RM32_CL, X86_FORM_SHR_RM64_CL },
        [X86_OPERATION_SAR] = { X86_FORM_SAR_RM8_CL, X86_FORM_SAR_RM16_CL, X86_FORM_SAR_RM32_CL, X86_FORM_SAR_RM64_CL },
        [X86_OPERATION_PUSH] = { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_PUSH_R64 },
    };
    return operation < BUSTER_ARRAY_LENGTH(forms) ? forms[operation][log2] : X86_FORM_NONE;
}

// The reference sequence of a row's operation on a value: the value into R11 by movabs (its field is the value itself:
// no extension), then the operation's register form; for a shift, the count into ECX and the CL form. `destination` is
// the operand the chosen record has (for imul, `source` its source and reg the destination). Returns the count.
static uint32_t x86_semantics_reference(uint32_t operation, uint32_t width, int64_t value, X86Instruction destination, X86Instruction* records)
{
    const uint16_t form = x86_semantics_register_form(operation, width);
    const bool shift = operation >= X86_OPERATION_ROL && operation <= X86_OPERATION_SAR;
    uint32_t count = 0;
    if (shift)
    {
        records[count++] = (X86Instruction){
            .form = X86_FORM_MOV_R32_IMM32,
            .rm = X86_REGISTER_RCX,
            .immediate = (int32_t)value,
        };
        X86Instruction operate = destination;
        operate.form = form;
        operate.reg = 0;
        operate.immediate = 0;
        records[count++] = operate;
    }
    else
    {
        records[count++] = (X86Instruction){
            .form = X86_FORM_MOV_R64_IMM64,
            .rm = X86_REGISTER_R11,
            .immediate = (int32_t)(uint32_t)value,
            .displacement = (int32_t)((uint64_t)value >> 32),
        };
        if (operation == X86_OPERATION_IMUL)
        {
            // destination = source; destination *= r11.
            X86Instruction load = destination;
            load.form = width == 2 ? X86_FORM_MOV_R16_RM16 : width == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64;
            load.immediate = 0;
            records[count++] = load;
            records[count++] = (X86Instruction){
                .form = form,
                .reg = destination.reg,
                .rm = X86_REGISTER_R11,
            };
        }
        else if (operation == X86_OPERATION_PUSH)
        {
            records[count++] = (X86Instruction){
                .form = form,
                .rm = X86_REGISTER_R11,
            };
        }
        else
        {
            X86Instruction operate = destination;
            operate.form = form;
            operate.reg = X86_REGISTER_R11;
            operate.immediate = 0;
            records[count++] = operate;
        }
    }
    return count;
}

// One request run both ways from one random state: the chosen record (x86_choice_instruction) and the reference
// sequence must leave the same general registers (but R11, and RCX for a shift, the reference's own), vector registers,
// data and stack, and the same flags among those live after it that the operation doesn't leave undefined. Returns
// whether they did, so that the caller can check the check.
static bool x86_semantics_check_request(X86SemanticsTally* tally, X86SemanticsMachine* machine, uint64_t* state, uint32_t row, int64_t value, X86SemanticsDestination destination,
                                        uint16_t context, uint16_t form)
{
    const X86ChoiceRow* choice = &x86_choice_rows[row];
    const uint32_t operation = choice->operation;
    const bool shift = operation >= X86_OPERATION_ROL && operation <= X86_OPERATION_SAR;
    // The destination: a register other than RSP, R11 and RCX (RAX for the accumulator), or memory; imul's source.
    X86Instruction target = { 0 };
    uint8_t chosen_register = (uint8_t)(x86_random_below(state, 13));
    chosen_register = chosen_register >= X86_REGISTER_RCX ? chosen_register + 1 : chosen_register;
    chosen_register = chosen_register >= X86_REGISTER_RSP ? chosen_register + 1 : chosen_register;
    chosen_register = chosen_register >= X86_REGISTER_R11 ? chosen_register + 1 : chosen_register;
    chosen_register = destination == X86_SEMANTICS_DESTINATION_ACCUMULATOR ? X86_REGISTER_RAX : chosen_register;
    if (operation == X86_OPERATION_IMUL)
    {
        target.reg = chosen_register;
        if (destination == X86_SEMANTICS_DESTINATION_MEMORY)
        {
            target.mode = BUSTER_X86_MEMORY_RIP;
            target.displacement = BUSTER_X86_SEMANTICS_TEST_OPERAND;
        }
        else
        {
            target.rm = (uint8_t)(x86_random_below(state, 2) ? chosen_register : X86_REGISTER_RBX);
        }
    }
    else if (operation != X86_OPERATION_PUSH)
    {
        if (destination == X86_SEMANTICS_DESTINATION_MEMORY)
        {
            target.mode = BUSTER_X86_MEMORY_RIP;
            target.displacement = BUSTER_X86_SEMANTICS_TEST_OPERAND;
        }
        else
        {
            target.rm = chosen_register;
        }
    }
    const X86Instruction record = x86_choice_instruction(form, target, value);
    X86Instruction references[4];
    const uint32_t reference_count = x86_semantics_reference(operation, choice->width, value, target, references);
    snprintf(x86_semantics_test_case, sizeof(x86_semantics_test_case), "choice %s for operation %u at %u bytes, value %lld, context %#x", x86_semantics_test_name(form),
             operation, choice->width, (long long)value, context);
    // The chosen record must be one the baseline target allows (#180's check, which x86_validate's is part of).
    bool result = x86_semantics_expect(tally,
                                       x86_target_validate(x86_semantics_test_baseline, &record, 1) == 1 &&
                                           x86_validate(references, reference_count) == reference_count,
                                       "%s: a record isn't valid at the baseline target", x86_semantics_test_case);
    static X86SemanticsState input;
    static X86SemanticsState chosen;
    static X86SemanticsState reference;
    static uint8_t chosen_data[BUSTER_X86_SEMANTICS_TEST_PAGE];
    static uint8_t chosen_stack[2 * BUSTER_X86_SEMANTICS_TEST_PAGE];
    static uint8_t data[BUSTER_X86_SEMANTICS_TEST_PAGE];
    static uint8_t stack[2 * BUSTER_X86_SEMANTICS_TEST_PAGE];
    if (result)
    {
        x86_semantics_random_state(state, machine, &input);
        memcpy(data, machine->data, sizeof(data));
        memcpy(stack, machine->stack, sizeof(stack));
        chosen = input;
        result = x86_semantics_machine_load(machine, &record, 1);
        if (result)
        {
            x86_semantics_machine_run(machine, &chosen);
            memcpy(chosen_data, machine->data, sizeof(chosen_data));
            memcpy(chosen_stack, machine->stack, sizeof(chosen_stack));
            memcpy(machine->data, data, sizeof(data));
            memcpy(machine->stack, stack, sizeof(stack));
            reference = input;
            result = x86_semantics_machine_load(machine, references, reference_count);
        }
        if (result)
        {
            x86_semantics_machine_run(machine, &reference);
            for (uint32_t g = 0; g < 16; g += 1)
            {
                const bool scratch = g == X86_REGISTER_R11 || (shift && g == X86_REGISTER_RCX);
                result &= scratch || chosen.gpr[g] == reference.gpr[g];
            }
            result &= memcmp(chosen.vector, reference.vector, sizeof(chosen.vector)) == 0;
            result &= memcmp(chosen_data, machine->data, sizeof(chosen_data)) == 0 && memcmp(chosen_stack, machine->stack, sizeof(chosen_stack)) == 0;
            // The live flags the operation doesn't leave undefined: the reference's operation's, or for a shift the chosen
            // record's by its count (the reference's CL form doesn't know it).
            uint8_t flags_read = 0;
            uint8_t flags_written = 0;
            uint8_t flags_undefined = 0;
            x86_instruction_flags(shift ? &record : &references[reference_count - 1], &flags_read, &flags_written, &flags_undefined);
            const uint8_t live = context & BUSTER_X86_CONTEXT_LIVE_FLAGS;
            const uint8_t compared = live & ~flags_undefined;
            result &= ((x86_semantics_flags(chosen.flags) ^ x86_semantics_flags(reference.flags)) & compared) == 0;
        }
    }
    return result;
}

// The chooser on every row, boundary value (and random ones), context and destination: each form the two paths agree on
// must validate and do what the reference does. A value the row's forms can't take gets no form.
static bool x86_semantics_check_choices(X86SemanticsMachine* machine, uint64_t seed)
{
    X86SemanticsTally tally = {
        .check = "choice: every row run against its reference",
    };
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 17;
    // The live flags and whether inc and dec are allowed: every flag live; none; ZF alone; all but CF; all but CF and
    // AF; all but SF.
    static const uint8_t lives[] = {
        BUSTER_X86_FLAGS_STATUS,
        0,
        BUSTER_X86_FLAG_ZF,
        BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAG_CF,
        BUSTER_X86_FLAGS_STATUS & ~(BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_AF),
        BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAG_SF,
    };
    uint64_t requests = 0;
    uint64_t chosen = 0;
    uint64_t forms_seen[BUSTER_X86_CHOICE_ROWS_MAX] = { 0 };
    for (uint32_t row = 0; row < x86_choice_row_count; row += 1)
    {
        const uint32_t random_values = 24;
        for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(x86_semantics_values) + random_values; v += 1)
        {
            int64_t value = 0;
            if (v < BUSTER_ARRAY_LENGTH(x86_semantics_values))
            {
                value = x86_semantics_values[v];
            }
            else
            {
                // Random values of each magnitude.
                const uint32_t bits = 1 + x86_random_below(&state, 64);
                value = (int64_t)(x86_test_random(&state) >> (64 - bits));
                value = x86_random_below(&state, 2) ? -value : value;
            }
            for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(lives); l += 1)
            {
                for (uint32_t d = 0; d < 3; d += 1)
                {
                    const X86SemanticsDestination destination = (X86SemanticsDestination)d;
                    const bool inc_dec = (l & 1) != 0;
                    const uint16_t context = x86_semantics_context(destination, lives[l], inc_dec);
                    const uint16_t form = x86_semantics_choose(&tally, row, value, context);
                    requests += 1;
                    if (form != X86_FORM_NONE)
                    {
                        chosen += 1;
                        for (uint32_t c = 0; c < BUSTER_X86_CHOICE_CANDIDATES; c += 1)
                        {
                            forms_seen[row] |= x86_choice_rows[row].forms[c] == form ? 1u << c : 0;
                        }
                        x86_semantics_expect(&tally, x86_semantics_check_request(&tally, machine, &state, row, value, destination, context, form),
                                             "%s: not what the reference does", x86_semantics_test_case);
                    }
                    else
                    {
                        // No form: the value doesn't fit the width, or it needs a register (at 64 bits, past an imm32;
                        // or it's 64-bit memory for mov).
                        const X86ChoiceRow* choice = &x86_choice_rows[row];
                        const bool count_row = choice->kind == BUSTER_X86_IMMEDIATE_COUNT;
                        const bool fits = count_row ? (uint64_t)value <= x86_count_mask(choice->width) : x86_value_fits_width(value, choice->width);
                        const bool register_mov = choice->operation == X86_OPERATION_MOV && destination != X86_SEMANTICS_DESTINATION_MEMORY;
                        const bool needs_register = choice->width == 8 && !count_row && (value < INT32_MIN || value > INT32_MAX);
                        x86_semantics_expect(&tally, (!fits || needs_register) && !(register_mov && fits),
                                             "row %u (operation %u at %u bytes) has no form for %lld with context %#x", row, choice->operation, choice->width, (long long)value,
                                             context);
                    }
                }
            }
        }
    }
    // Every candidate of every row was chosen by some request.
    for (uint32_t row = 0; row < x86_choice_row_count; row += 1)
    {
        uint32_t candidates = 0;
        for (uint32_t c = 0; c < BUSTER_X86_CHOICE_CANDIDATES; c += 1)
        {
            candidates |= x86_choice_rows[row].forms[c] != X86_FORM_NONE ? 1u << c : 0;
        }
        x86_semantics_expect(&tally, forms_seen[row] == candidates, "row %u: candidates %#x chosen, of %#x", row, (uint32_t)forms_seen[row], candidates);
    }
    // The check is sensitive: add rax, imm8 with 0xc8 (-56) is not add rax, 200.
    const uint32_t add = x86_choice_row(X86_OPERATION_ADD, 8);
    const bool wrong = x86_semantics_check_request(&tally, machine, &state, add, 200, X86_SEMANTICS_DESTINATION_REGISTER, BUSTER_X86_FLAGS_STATUS | BUSTER_X86_CONTEXT_REGISTER,
                                                   X86_FORM_ADD_RM64_IMM8);
    x86_semantics_expect(&tally, !wrong, "an imm8 of 0xc8 standing for 200 must fail the check");
    printf("choice: %llu requests, %llu with a form, each run against its reference\n", (unsigned long long)requests, (unsigned long long)chosen);
    return x86_semantics_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The vector paths' twins

// Maps `bytes` rounded up to whole pages before a PROT_NONE page, and returns the end of the readable part (as the
// encoder's tests do, x86_map_guarded).
static bool x86_semantics_check_twins(uint64_t seed, uint32_t count)
{
    X86SemanticsTally tally = {
        .check = "twins: the vector paths against the scalar ones",
    };
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 19;
    uint16_t* rows = malloc(count * sizeof(uint16_t));
    int64_t* values = malloc(count * sizeof(int64_t));
    uint16_t* contexts = malloc(count * sizeof(uint16_t));
    uint16_t* scalar_forms = malloc(count * sizeof(uint16_t));
    uint16_t* vector_forms = malloc(count * sizeof(uint16_t));
    X86Instruction* records = malloc(count * sizeof(X86Instruction));
    uint64_t* scalar_reads = malloc(count * sizeof(uint64_t));
    uint64_t* scalar_writes = malloc(count * sizeof(uint64_t));
    uint64_t* vector_reads = malloc(count * sizeof(uint64_t));
    uint64_t* vector_writes = malloc(count * sizeof(uint64_t));
    const bool allocated = rows && values && contexts && scalar_forms && vector_forms && records && scalar_reads && scalar_writes && vector_reads && vector_writes;
    if (x86_semantics_expect(&tally, allocated, "the columns"))
    {
        // Form choice: random rows, values (boundaries, small, any) and context words (every bit, the unused ones too).
        for (uint32_t i = 0; i < count; i += 1)
        {
            rows[i] = (uint16_t)x86_random_below(&state, x86_choice_row_count);
            const uint32_t kind = x86_random_below(&state, 3);
            values[i] = kind == 0 ? x86_semantics_values[x86_random_below(&state, BUSTER_ARRAY_LENGTH(x86_semantics_values))]
                        : kind == 1 ? (int64_t)x86_random_below(&state, 512) - 256
                                    : (int64_t)x86_test_random(&state);
            contexts[i] = (uint16_t)x86_test_random(&state);
        }
        x86_choose_forms_scalar(rows, values, contexts, count, scalar_forms);
        x86_choose_forms(rows, values, contexts, count, vector_forms);
        for (uint32_t i = 0; i < count; i += 1)
        {
            x86_semantics_expect(&tally, scalar_forms[i] == vector_forms[i], "request %u (row %u, %lld, context %#x): scalar %u, vector %u", i, rows[i], (long long)values[i],
                                 contexts[i], scalar_forms[i], vector_forms[i]);
        }
        // Resolved effects: random records of covered and uncovered forms (with segment overrides and the fixup flag), with
        // random fields.
        for (uint32_t i = 0; i < count; i += 1)
        {
            uint32_t form = x86_random_below(&state, X86_FORM_COUNT);
            for (uint32_t attempt = 0; attempt < 8 && x86_random_below(&state, 4) != 0 && x86_effects(form)->operation == X86_OPERATION_NONE; attempt += 1)
            {
                form = x86_random_below(&state, X86_FORM_COUNT);
            }
            const uint32_t extra = x86_random_below(&state, 8);
            form += extra == 0 ? BUSTER_X86_FORM_FS : extra == 1 ? BUSTER_X86_FORM_GS : 0;
            form |= x86_random_below(&state, 8) == 0 ? BUSTER_X86_FORM_FIXUP : 0;
            const uint64_t a = x86_test_random(&state);
            const uint64_t b = x86_test_random(&state);
            memcpy(&records[i], &a, 8);
            memcpy((uint8_t*)&records[i] + 8, &b, 8);
            records[i].form = (uint16_t)form;
            const uint32_t mode = x86_random_below(&state, 4);
            records[i].mode = mode == 0 ? 0 : mode == 1 ? BUSTER_X86_MEMORY_BASE : mode == 2 ? (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(x86_random_below(&state, 4)) : records[i].mode;
            // Registers 0 to 15 (AH to BH sometimes) half the time, and rm the same as reg a quarter of it (zero idioms,
            // and a memory operand based on the register reg names).
            if (x86_random_below(&state, 2) == 0)
            {
                records[i].reg = (uint8_t)x86_random_below(&state, 16);
                records[i].rm = (uint8_t)x86_random_below(&state, 16);
                records[i].reg |= x86_random_below(&state, 8) == 0 ? BUSTER_X86_REGISTER_HIGH_BYTE : 0;
            }
            records[i].rm = x86_random_below(&state, 4) == 0 ? records[i].reg : records[i].rm;
        }
        x86_instructions_effects_scalar(records, count, scalar_reads, scalar_writes);
        x86_instructions_effects(records, count, vector_reads, vector_writes);
        for (uint32_t i = 0; i < count; i += 1)
        {
            x86_semantics_expect(&tally, scalar_reads[i] == vector_reads[i] && scalar_writes[i] == vector_writes[i],
                                 "record %u (%s, reg %u, rm %u, mode %#x, immediate %d): scalar %#llx %#llx, vector %#llx %#llx", i, x86_semantics_test_name(records[i].form),
                                 records[i].reg, records[i].rm, records[i].mode, records[i].immediate, (unsigned long long)scalar_reads[i], (unsigned long long)scalar_writes[i],
                                 (unsigned long long)vector_reads[i], (unsigned long long)vector_writes[i]);
        }
    }
    // Page edges: every count to 100, each column ending at a PROT_NONE page (a read past it faults: the handler names the
    // case), and the outputs too.
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint64_t mapped[7] = { 0 };
    uint8_t* ends[7];
    for (uint32_t i = 0; i < 7; i += 1)
    {
        ends[i] = x86_map_guarded(100 * sizeof(X86Instruction), page, &mapped[i]);
    }
    bool mapped_all = true;
    for (uint32_t i = 0; i < 7; i += 1)
    {
        mapped_all &= ends[i] != 0;
    }
    if (x86_semantics_expect(&tally, mapped_all && allocated, "the guarded columns"))
    {
        for (uint32_t edge_count = 0; edge_count <= 100; edge_count += 1)
        {
            snprintf(x86_semantics_test_case, sizeof(x86_semantics_test_case), "the vector paths on %u items at a page's end", edge_count);
            uint16_t* edge_rows = (uint16_t*)(ends[0] - edge_count * sizeof(uint16_t));
            int64_t* edge_values = (int64_t*)(ends[1] - edge_count * sizeof(int64_t));
            uint16_t* edge_contexts = (uint16_t*)(ends[2] - edge_count * sizeof(uint16_t));
            uint16_t* edge_forms = (uint16_t*)(ends[3] - edge_count * sizeof(uint16_t));
            X86Instruction* edge_records = (X86Instruction*)(ends[4] - edge_count * sizeof(X86Instruction));
            uint64_t* edge_reads = (uint64_t*)(ends[5] - edge_count * sizeof(uint64_t));
            uint64_t* edge_writes = (uint64_t*)(ends[6] - edge_count * sizeof(uint64_t));
            memcpy(edge_rows, rows, edge_count * sizeof(uint16_t));
            memcpy(edge_values, values, edge_count * sizeof(int64_t));
            memcpy(edge_contexts, contexts, edge_count * sizeof(uint16_t));
            memcpy(edge_records, records, edge_count * sizeof(X86Instruction));
            x86_choose_forms(edge_rows, edge_values, edge_contexts, edge_count, edge_forms);
            x86_instructions_effects(edge_records, edge_count, edge_reads, edge_writes);
            x86_semantics_expect(&tally,
                                 memcmp(edge_forms, scalar_forms, edge_count * sizeof(uint16_t)) == 0 && memcmp(edge_reads, scalar_reads, edge_count * sizeof(uint64_t)) == 0 &&
                                     memcmp(edge_writes, scalar_writes, edge_count * sizeof(uint64_t)) == 0,
                                 "%u items at a page's end", edge_count);
        }
    }
    for (uint32_t i = 0; i < 7; i += 1)
    {
        if (ends[i] != 0)
        {
            munmap(ends[i] - (mapped[i] - 2 * page) - page, mapped[i]);
        }
    }
    free(rows);
    free(values);
    free(contexts);
    free(scalar_forms);
    free(vector_forms);
    free(records);
    free(scalar_reads);
    free(scalar_writes);
    free(vector_reads);
    free(vector_writes);
    printf("twins: %u requests and %u records, and every count to 100 at a page's end\n", count, count);
    return x86_semantics_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// #180's target

// Every candidate of every row must be a form the baseline target allows (x86_target_forms of a zero X86Target), so a
// chosen record passes x86_target_validate at every target. #180's table fails closed (a form its rows don't classify is
// unavailable), so the covered forms it doesn't allow at the baseline are listed: those that need a feature (popcnt,
// lzcnt, tzcnt, roundss, roundsd) and the baseline ones its rows don't classify yet, which a selector that emits them
// adds there.
static bool x86_semantics_check_target(void)
{
    X86SemanticsTally tally = {
        .check = "target: the rows' forms at the baseline",
    };
    for (uint32_t row = 0; row < x86_choice_row_count; row += 1)
    {
        for (uint32_t candidate = 0; candidate < BUSTER_X86_CHOICE_CANDIDATES; candidate += 1)
        {
            const uint16_t form = x86_choice_rows[row].forms[candidate];
            x86_semantics_expect(&tally, form == X86_FORM_NONE || x86_target_form_allowed(x86_semantics_test_baseline, form), "row %u's %s isn't a baseline form", row,
                                 x86_semantics_test_name(form));
        }
    }
    uint32_t beyond = 0;
    printf("target: covered forms #180's table doesn't allow at the baseline:");
    for (uint32_t form = 1; form < X86_FORM_COUNT; form += 1)
    {
        if (x86_effects(form)->operation != X86_OPERATION_NONE && !x86_semantics_test_baseline[form])
        {
            printf(" %s", x86_semantics_test_name(form));
            beyond += 1;
        }
    }
    printf(" (%u)\n", beyond);
    return x86_semantics_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// x86_match's agreement

// The assembler's matcher (x86_match.c) keeps immediate ranges of its own: on the forms its syntax supports whose immediate is a
// value, the shared rule must take every value it takes. It takes fewer: not the unsigned spelling of a sign-extended
// pattern (0xffffffff for an imm8 at 32 bits, which is -1), which the shared rule takes. Those are counted, not failed.
static bool x86_semantics_check_match(void)
{
    X86SemanticsTally tally = {
        .check = "x86_match: its immediate ranges against the shared rule",
    };
    uint64_t forms = 0;
    uint64_t agreed = 0;
    uint64_t unsigned_spellings = 0;
    for (uint32_t form = 1; form < BUSTER_X86_FORM_LEGACY_MODES; form += 1)
    {
        const X86FormDescription* description = &x86_form_descriptions[form];
        const X86Effects* effects = x86_effects(form);
        if (x86_match_supported(description) && description->immediate_bytes != 0 && x86_effects_immediate_kind(effects) == BUSTER_X86_IMMEDIATE_VALUE)
        {
            forms += 1;
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_semantics_values); i += 1)
            {
                const int64_t value = x86_semantics_values[i];
                const X86Operand operand = {
                    .value = value,
                    .kind = X86_OPERAND_IMMEDIATE,
                };
                const bool theirs = x86_match_immediate(description, &operand);
                const bool ours = x86_immediate_encodes(form, value);
                const bool unsigned_spelling = ours && !theirs && value > 0 && description->immediate_bytes < effects->width;
                x86_semantics_expect(&tally, theirs == ours || unsigned_spelling, "%s, %lld: x86_match %s it, the shared rule %s", x86_semantics_test_name(form), (long long)value,
                                     theirs ? "takes" : "refuses", ours ? "takes" : "refuses");
                agreed += theirs == ours;
                unsigned_spellings += unsigned_spelling;
            }
        }
    }
    printf("x86_match: %llu forms it supports with a value immediate, %llu value pairs agree, %llu unsigned spellings it refuses and the shared rule takes\n",
           (unsigned long long)forms, (unsigned long long)agreed, (unsigned long long)unsigned_spellings);
    return x86_semantics_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// GNU as

// With --cross-check: every form the chooser picks for the boundary values (each row, value, context and destination
// once), printed as x86_print_instruction prints it, must assemble with GNU as to the bytes x86_encode_scalar gives it:
// GNU as picks the same encoding for the instruction the chooser picked. (A register-to-register form is printed with
// {load} or {store}, which picks the form; the immediates' are GNU as's own choice.)
static bool x86_semantics_check_assembler(uint64_t seed, const char* parent, bool keep)
{
    X86SemanticsTally tally = {
        .check = "GNU as: the chosen forms",
    };
    char directory[512];
    char source[600];
    char object[600];
    char log[600];
    const int length = snprintf(directory, sizeof(directory), "%s/buster-x86-semantics-XXXXXX", parent);
    const bool made = length > 0 && (uint64_t)length + 32 < sizeof(directory) && mkdtemp(directory) != 0;
    x86_semantics_expect(&tally, made, "a directory for the files");
    snprintf(source, sizeof(source), "%s/choices.s", directory);
    snprintf(object, sizeof(object), "%s/choices.o", directory);
    snprintf(log, sizeof(log), "%s/as.log", directory);
    const uint64_t capacity = 64 * BUSTER_ARRAY_LENGTH(x86_semantics_values) * 6 * 3;
    X86Instruction* records = malloc(capacity * sizeof(X86Instruction));
    uint8_t* encoded = malloc(BUSTER_X86_ENCODER_CAPACITY(capacity));
    uint32_t* offsets = malloc((capacity + 1) * sizeof(uint32_t));
    uint64_t count = 0;
    FILE* file = made && records && encoded && offsets ? fopen(source, "w") : 0;
    x86_semantics_expect(&tally, file != 0, "the source file");
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 23;
    if (file != 0)
    {
        fprintf(file, ".intel_syntax noprefix\n.text\n");
        static const uint8_t lives[] = { BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_FLAG_ZF };
        for (uint32_t row = 0; row < x86_choice_row_count; row += 1)
        {
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(x86_semantics_values); v += 1)
            {
                for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(lives); l += 1)
                {
                    for (uint32_t d = 0; d < 3; d += 1)
                    {
                        const int64_t value = x86_semantics_values[v];
                        const uint16_t context = x86_semantics_context((X86SemanticsDestination)d, lives[l], l != 0);
                        const uint16_t form = x86_semantics_choose(&tally, row, value, context);
                        if (form != X86_FORM_NONE && count < capacity)
                        {
                            X86Instruction target = { 0 };
                            const uint8_t chosen_register = d == X86_SEMANTICS_DESTINATION_ACCUMULATOR ? X86_REGISTER_RAX : (uint8_t)(1 + x86_random_below(&state, 3));
                            if (x86_choice_rows[row].operation == X86_OPERATION_IMUL)
                            {
                                target.reg = chosen_register;
                                target.rm = d == X86_SEMANTICS_DESTINATION_MEMORY ? 0 : X86_REGISTER_RSI;
                                target.mode = d == X86_SEMANTICS_DESTINATION_MEMORY ? BUSTER_X86_MEMORY_RIP : 0;
                                target.displacement = d == X86_SEMANTICS_DESTINATION_MEMORY ? 256 : 0;
                            }
                            else if (x86_choice_rows[row].operation != X86_OPERATION_PUSH)
                            {
                                target.rm = d == X86_SEMANTICS_DESTINATION_MEMORY ? 0 : chosen_register;
                                target.mode = d == X86_SEMANTICS_DESTINATION_MEMORY ? BUSTER_X86_MEMORY_RIP : 0;
                                target.displacement = d == X86_SEMANTICS_DESTINATION_MEMORY ? 256 : 0;
                            }
                            records[count] = x86_choice_instruction(form, target, value);
                            x86_print_instruction(file, &records[count], 0);
                            fprintf(file, "\n");
                            count += 1;
                        }
                    }
                }
            }
        }
        fclose(file);
    }
    char* arguments[] = { "as", "--64", "-o", object, source, 0 };
    const int status = file != 0 ? branch_sizing_test_run(arguments, log) : -1;
    x86_semantics_expect(&tally, status == 0, "GNU as assembles the chosen forms (see as.log), status %d", status);
    uint64_t object_length = 0;
    uint8_t* bytes = status == 0 ? x86_read_file(object, &object_length) : 0;
    x86_semantics_expect(&tally, status != 0 || bytes != 0, "GNU as's object can be read");
    if (bytes != 0)
    {
        const Elf64_Shdr* header = 0;
        const uint8_t* section = x86_elf_section(bytes, object_length, SHT_PROGBITS, ".text", &header);
        const int64_t encoded_length = x86_encode_scalar(records, count, encoded, BUSTER_X86_ENCODER_CAPACITY(count), offsets, 0);
        const bool same = section != 0 && encoded_length >= 0 && header->sh_size == (uint64_t)encoded_length && memcmp(section, encoded, header->sh_size) == 0;
        if (!same && section != 0 && encoded_length >= 0)
        {
            uint64_t offset = 0;
            const uint64_t shorter = header->sh_size < (uint64_t)encoded_length ? header->sh_size : (uint64_t)encoded_length;
            while (offset < shorter && section[offset] == encoded[offset])
            {
                offset += 1;
            }
            uint64_t record = 0;
            while (record + 1 < count && offsets[record + 1] <= offset)
            {
                record += 1;
            }
            printf("    %llu bytes against GNU as's %llu, first differing at byte %llu, in record %llu (%s)\n", (unsigned long long)encoded_length,
                   (unsigned long long)header->sh_size, (unsigned long long)offset, (unsigned long long)record, x86_semantics_test_name(records[record].form));
        }
        x86_semantics_expect(&tally, same, "the chosen forms' bytes are GNU as's");
        free(bytes);
    }
    printf("GNU as: %llu chosen records\n", (unsigned long long)count);
    if (made && !keep && tally.failures == 0)
    {
        unlink(source);
        unlink(object);
        unlink(log);
        rmdir(directory);
    }
    else if (made)
    {
        printf("kept %s\n", directory);
    }
    free(records);
    free(encoded);
    free(offsets);
    return x86_semantics_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// run_x86_semantics_tests

static bool x86_semantics_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = end != text && *end == 0;
    *value = result ? parsed : *value;
    return result;
}

static int run_x86_semantics_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t runs = BUSTER_X86_SEMANTICS_TEST_RUNS;
    bool cross_check = false;
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid &= x86_semantics_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
        {
            valid &= x86_semantics_parse_u64(argv[i + 1], &runs) && runs != 0 && runs <= 1000;
            i += 1;
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    bool result = valid;
    if (!valid)
    {
        fprintf(stderr, "usage: test-x86-semantics [--seed N] [--runs N] [--cross-check] [--keep]\n");
    }
    else
    {
        const bool initialized = x86_encoder_initialize() && x86_semantics_initialize();
        const X86Target baseline = { 0 };
        x86_target_forms(&baseline, x86_semantics_test_baseline);
        printf("x86 semantics: seed %llu, %u choice rows, initialization %s\n", (unsigned long long)seed, x86_choice_row_count, initialized ? "ok" : "failed");
        result = initialized;
        X86SemanticsMachine machine = { 0 };
        const bool opened = result && x86_semantics_machine_open(&machine);
        if (result && !opened)
        {
            printf("x86 semantics: can't map the harness's code (%s)\n", strerror(errno));
        }
        result = result && opened;
        if (result)
        {
            result = x86_semantics_check_fixtures() && result;
            result = x86_semantics_check_resolved() && result;
            result = x86_semantics_check_immediates() && result;
            result = x86_semantics_check_choice_fixtures() && result;
            result = x86_semantics_check_twins(seed, 20000) && result;
            result = x86_semantics_check_target() && result;
            result = x86_semantics_check_match() && result;
            result = x86_semantics_check_table(&machine, seed, (uint32_t)runs) && result;
            result = x86_semantics_check_stack(&machine, seed) && result;
            result = x86_semantics_check_conditions(&machine, seed) && result;
            result = x86_semantics_check_choices(&machine, seed) && result;
        }
        if (cross_check && initialized)
        {
            const char* parent = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
            result = x86_semantics_check_assembler(seed, parent, keep) && result;
        }
        x86_semantics_machine_close(&machine);
        printf("x86 semantics: %s\n", result ? "ok" : "FAILED");
    }
    return result ? 0 : 1;
}
