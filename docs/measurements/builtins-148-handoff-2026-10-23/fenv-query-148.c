// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
//
// Original bounded observation fixture for #148's Clang constant_p/FENV boundary.
// This is an oracle observation, not an assertion that another Clang version's runtime behavior must match.
// The source-context facts below are separate real builtin calls certified with _Static_assert. Each noinline
// runtime helper contains its own real builtin call. Paired ON/OFF functions preserve the source pragmas.
//
// Compile with the exact approved-host clang 23.1.1, baseline output and separate provenance for its executable:
// clang -std=gnu17 -O0 -march=x86-64 -fno-fast-math -ffp-contract=off -fno-inline -fno-lto -Wall -Wextra -Werror
//       work/fenv-query-148.c -o work/fenv-query-148
// Do not add global -frounding-math, -ffp-model=strict or -ffp-exception-behavior=strict: they would change the
// OFF controls' default environment. ON pragmas supply dynamic rounding and strict exception semantics locally.
// No libm dependency. The fixture runs in one alarm-bounded child, masks FP exceptions, disables DAZ/FTZ, and
// observes ON functions at four matched x87/MXCSR rounding controls and OFF functions once at default controls.
// There are 105 runtime observations. It saves/restores x87 CW and complete MXCSR. Per-case x87 exception
// flags are cleared/read; original child x87 flags are not restored. The parent's FP state is unchanged by fork.
// No expected runtime result/status is hardcoded. Counters and status are read immediately after each helper.

#if !defined(__clang__) || !defined(__linux__) || !defined(__x86_64__)
#error "This observation fixture requires Clang for Linux/x86-64"
#elif __clang_major__ != 23 || __clang_minor__ != 1 || __clang_patchlevel__ != 1
#error "This observation fixture requires the exact approved Clang 23.1.1"
#endif

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int fixture_n;
static volatile unsigned fixture_calls;
static double fixture_global;
static volatile double fixture_volatile;
static volatile unsigned fixture_seed;

#pragma STDC FENV_ACCESS ON

static __attribute__((noinline)) double fixture_effect_call(void)
{
    fixture_calls += 1;
    const double result = fixture_global;
    return result;
}

#pragma STDC FENV_ACCESS ON

enum
{
    BUSTER_FENV_SOURCE_EXACT = __builtin_constant_p(1.0 / 2.0),
    BUSTER_FENV_SOURCE_INEXACT = __builtin_constant_p(1.0 / 3.0),
    BUSTER_FENV_SOURCE_UNCHOSEN_CALL_EXACT = __builtin_constant_p(1 ? 1.0 / 2.0 : fixture_effect_call()),
    BUSTER_FENV_SOURCE_UNCHOSEN_CALL_INEXACT = __builtin_constant_p(1 ? 1.0 / 3.0 : fixture_effect_call()),
    BUSTER_FENV_SOURCE_UNCHOSEN_INCREMENT = __builtin_constant_p(1 ? 7 : fixture_n++),
    BUSTER_FENV_SOURCE_LOGICAL = __builtin_constant_p(0 && fixture_effect_call()),
    BUSTER_FENV_SOURCE_COMMA = __builtin_constant_p((fixture_n++, 1.0 / 3.0)),
    BUSTER_FENV_SOURCE_CALL = __builtin_constant_p(fixture_effect_call()),
    BUSTER_FENV_SOURCE_DYNAMIC = __builtin_constant_p(fixture_global + 1.0 / 3.0),
    BUSTER_FENV_SOURCE_NESTED = __builtin_constant_p(__builtin_constant_p(1.0 / 3.0)),
};

_Static_assert(BUSTER_FENV_SOURCE_EXACT == 1, "source constant context: exact floating operand");
_Static_assert(BUSTER_FENV_SOURCE_INEXACT == 1, "source constant context: inexact floating operand");
_Static_assert(BUSTER_FENV_SOURCE_UNCHOSEN_CALL_EXACT == 1, "source fold precedes unchosen syntactic call");
_Static_assert(BUSTER_FENV_SOURCE_UNCHOSEN_CALL_INEXACT == 1, "source constant context permits selected inexact fold");
_Static_assert(BUSTER_FENV_SOURCE_UNCHOSEN_INCREMENT == 1, "source selected integer ignores unchosen increment");
_Static_assert(BUSTER_FENV_SOURCE_LOGICAL == 1, "source short circuit ignores call");
_Static_assert(BUSTER_FENV_SOURCE_COMMA == 0, "source queried increment is not executed");
_Static_assert(BUSTER_FENV_SOURCE_CALL == 0, "source queried call is not executed");
_Static_assert(BUSTER_FENV_SOURCE_DYNAMIC == 0, "source global value is not a constant");
_Static_assert(BUSTER_FENV_SOURCE_NESTED == 1, "source nested query is a constant");

#define BUSTER_FENV_QUERY_CASES(X) \
    X(exact_fp, 1.0 / 2.0) \
    X(inexact_fp, 1.0 / 3.0) \
    X(dynamic_parameter, x + 1.0 / 3.0) \
    X(dynamic_global, fixture_global + 1.0 / 3.0) \
    X(dynamic_integer, (double)integer_x / 3.0) \
    X(unchosen_call_exact, 1 ? 1.0 / 2.0 : fixture_effect_call()) \
    X(unchosen_call_inexact, 1 ? 1.0 / 3.0 : fixture_effect_call()) \
    X(unchosen_increment_exact, 1 ? 1.0 / 2.0 : (fixture_n++, 2.0)) \
    X(unchosen_increment_inexact, 1 ? 1.0 / 3.0 : (fixture_n++, 2.0)) \
    X(volatile_read, fixture_volatile + 1.0 / 3.0) \
    X(direct_call, fixture_effect_call()) \
    X(call_plus_inexact, fixture_effect_call() + 1.0 / 3.0) \
    X(comma_increment, (fixture_n++, 1.0 / 3.0)) \
    X(selected_increment, 1 ? (fixture_n++, 1.0 / 3.0) : 0.0) \
    X(nested_query, __builtin_constant_p(1.0 / 3.0)) \
    X(nested_query_add, 1 + __builtin_constant_p(1.0 / 3.0)) \
    X(integer_conditional_call, 1 ? 7 : fixture_effect_call()) \
    X(integer_conditional_increment, 1 ? 7 : fixture_n++) \
    X(integer_short_and, 0 && fixture_effect_call()) \
    X(integer_short_or, 1 || fixture_n++) \
    X(short_and_plus_inexact, (0 && fixture_effect_call()) + 1.0 / 3.0)

#define BUSTER_FENV_QUERY_DEFINE_ON(name, expression) \
    static __attribute__((noinline)) int fixture_on_##name(double x, int integer_x) \
    { \
        (void)x; \
        (void)integer_x; \
        const int result = __builtin_constant_p(expression); \
        return result; \
    }

BUSTER_FENV_QUERY_CASES(BUSTER_FENV_QUERY_DEFINE_ON)

#pragma STDC FENV_ACCESS OFF

#define BUSTER_FENV_QUERY_DEFINE_OFF(name, expression) \
    static __attribute__((noinline)) int fixture_off_##name(double x, int integer_x) \
    { \
        (void)x; \
        (void)integer_x; \
        const int result = __builtin_constant_p(expression); \
        return result; \
    }

BUSTER_FENV_QUERY_CASES(BUSTER_FENV_QUERY_DEFINE_OFF)

#pragma STDC FENV_ACCESS ON

typedef enum FixtureCase
{
#define BUSTER_FENV_QUERY_ENUM(name, expression) BUSTER_FENV_CASE_##name,
    BUSTER_FENV_QUERY_CASES(BUSTER_FENV_QUERY_ENUM)
    BUSTER_FENV_CASE_COUNT,
} FixtureCase;

typedef struct FixtureFpState FixtureFpState;
struct FixtureFpState
{
    uint16_t x87_control;
    uint32_t mxcsr;
};

typedef struct FixtureObservation FixtureObservation;
struct FixtureObservation
{
    int value;
    int increments;
    unsigned calls;
    uint16_t x87_status;
    uint32_t mxcsr;
};

static uint16_t fixture_x87_control(void)
{
    uint16_t result;
    __asm__ volatile("fnstcw %0" : "=m"(result) : : "memory");
    return result;
}

static uint16_t fixture_x87_status(void)
{
    uint16_t result;
    __asm__ volatile("fnstsw %0" : "=m"(result) : : "memory");
    return result;
}

static uint32_t fixture_mxcsr(void)
{
    uint32_t result;
    __asm__ volatile("stmxcsr %0" : "=m"(result) : : "memory");
    return result;
}

static FixtureFpState fixture_save_fp(void)
{
    FixtureFpState result = { 0 };
    result.x87_control = fixture_x87_control();
    result.mxcsr = fixture_mxcsr();
    return result;
}

static void fixture_restore_fp(const FixtureFpState* saved)
{
    __asm__ volatile("fldcw %0" : : "m"(saved->x87_control) : "memory");
    __asm__ volatile("ldmxcsr %0" : : "m"(saved->mxcsr) : "memory");
}

static void fixture_set_fp(const FixtureFpState* saved, unsigned rounding, bool default_environment)
{
    // Preserve precision control and reserved control bits, mask exceptions, and choose IEEE subnormal behavior.
    // OFF leaves require the full default environment: nearest, default x87 precision, all exceptions masked,
    // DAZ/FTZ disabled and clear exception status. They are never entered under the other rounding controls.
    const uint16_t control = default_environment ? UINT16_C(0x037f) :
        (uint16_t)((saved->x87_control | UINT16_C(0x003f)) & ~UINT16_C(0x0c00)) | (uint16_t)(rounding << 10);
    const uint32_t mxcsr = default_environment ? UINT32_C(0x1f80) :
        (saved->mxcsr & ~UINT32_C(0xe07f)) | UINT32_C(0x1f80) | (rounding << 13);
    __asm__ volatile("fldcw %0\n\tfnclex" : : "m"(control) : "memory");
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr) : "memory");
}

static int fixture_call(FixtureCase which, bool fenv_on, double x, int integer_x)
{
    int result = 0;
    if (fenv_on)
    {
        switch (which)
        {
#define BUSTER_FENV_QUERY_CALL_ON(name, expression) case BUSTER_FENV_CASE_##name: result = fixture_on_##name(x, integer_x); break;
            BUSTER_FENV_QUERY_CASES(BUSTER_FENV_QUERY_CALL_ON)
        case BUSTER_FENV_CASE_COUNT: break;
        }
    }
    else
    {
        switch (which)
        {
#define BUSTER_FENV_QUERY_CALL_OFF(name, expression) case BUSTER_FENV_CASE_##name: result = fixture_off_##name(x, integer_x); break;
            BUSTER_FENV_QUERY_CASES(BUSTER_FENV_QUERY_CALL_OFF)
        case BUSTER_FENV_CASE_COUNT: break;
        }
    }
    return result;
}

static FixtureObservation fixture_observe(const FixtureFpState* saved, unsigned rounding, FixtureCase which, bool fenv_on)
{
    fixture_n = 0;
    fixture_calls = 0;
    // This volatile value comes from main's actual argc. Even callers at another optimization level cannot
    // replace these arguments/globals with a source literal; no interprocedural constant proof is supplied.
    const unsigned seed = fixture_seed;
    const double argument = (double)(seed & 15u) + 0.25;
    const int integer_argument = (int)(seed & 15u);
    fixture_global = argument;
    fixture_volatile = argument;
    fixture_set_fp(saved, rounding, !fenv_on);
    FixtureObservation result = { 0 };
    result.value = fixture_call(which, fenv_on, argument, integer_argument);
    result.x87_status = fixture_x87_status();
    result.mxcsr = fixture_mxcsr();
    result.increments = fixture_n;
    result.calls = fixture_calls;
    return result;
}

static int fixture_run(unsigned seed)
{
    const FixtureFpState saved = fixture_save_fp();
    fixture_seed = seed;
    printf("fixture=fenv-query-148 clang=%s clang-numeric=%d.%d.%d source=%s seed=%u\n",
        __clang_version__, __clang_major__, __clang_minor__, __clang_patchlevel__, __FILE__, seed);
    printf("saved x87-control=0x%04x mxcsr=0x%08x\n", (unsigned)saved.x87_control, saved.mxcsr);
    printf("source-constant-context exact=%d inexact=%d unchosen-call-exact=%d unchosen-call-inexact=%d unchosen-increment=%d short-and=%d comma=%d call=%d dynamic=%d nested=%d\n",
        BUSTER_FENV_SOURCE_EXACT, BUSTER_FENV_SOURCE_INEXACT, BUSTER_FENV_SOURCE_UNCHOSEN_CALL_EXACT,
        BUSTER_FENV_SOURCE_UNCHOSEN_CALL_INEXACT, BUSTER_FENV_SOURCE_UNCHOSEN_INCREMENT, BUSTER_FENV_SOURCE_LOGICAL,
        BUSTER_FENV_SOURCE_COMMA, BUSTER_FENV_SOURCE_CALL, BUSTER_FENV_SOURCE_DYNAMIC, BUSTER_FENV_SOURCE_NESTED);
    static const char* const names[] =
    {
#define BUSTER_FENV_QUERY_NAME(name, expression) #name,
        BUSTER_FENV_QUERY_CASES(BUSTER_FENV_QUERY_NAME)
    };
    _Static_assert(sizeof(names) / sizeof(names[0]) == BUSTER_FENV_CASE_COUNT, "every runtime case has a printed name");
    // Control encoding is the architectural RC field: nearest, downward, upward, toward zero.
    static const char* const rounding_names[] = { "nearest", "downward", "upward", "toward-zero" };
    for (unsigned mode = 0; mode < 5; mode += 1)
    {
        const bool on = mode != 0;
        const unsigned rounding = on ? mode - 1 : 0;
        for (unsigned i = 0; i < BUSTER_FENV_CASE_COUNT; i += 1)
        {
            const FixtureObservation value = fixture_observe(&saved, rounding, (FixtureCase)i, on);
            printf("runtime fenv=%s rounding=%s case=%s result=%d n=%d calls=%u x87-status=0x%04x mxcsr=0x%08x fp-status=0x%02x\n",
                on ? "ON" : "OFF", on ? rounding_names[rounding] : "default", names[i], value.value, value.increments, value.calls,
                (unsigned)value.x87_status, value.mxcsr, (unsigned)((value.x87_status | value.mxcsr) & 0x3fu));
        }
    }
    fixture_restore_fp(&saved);
    const uint16_t restored_control = fixture_x87_control();
    const uint32_t restored_mxcsr = fixture_mxcsr();
    const bool restored = restored_control == saved.x87_control && restored_mxcsr == saved.mxcsr;
    printf("restored x87-control=0x%04x mxcsr=0x%08x controls-match=%u\n", (unsigned)restored_control, restored_mxcsr, restored ? 1u : 0u);
    const bool output_ok = fflush(stdout) == 0 && !ferror(stdout);
    const int result = restored && output_ok ? 0 : 1;
    return result;
}

int main(int argc, char** argv)
{
    (void)argv;
    const pid_t child = fork();
    int result = 1;
    if (child == 0)
    {
        const struct rlimit core = { 0, 0 };
        const bool limited = setrlimit(RLIMIT_CORE, &core) == 0;
        struct sigaction action = { 0 };
        action.sa_handler = SIG_DFL;
        const bool signal_ready = sigemptyset(&action.sa_mask) == 0 && sigaction(SIGALRM, &action, NULL) == 0;
        sigset_t unblocked;
        const bool mask_ready = sigemptyset(&unblocked) == 0 && sigaddset(&unblocked, SIGALRM) == 0 && sigprocmask(SIG_UNBLOCK, &unblocked, NULL) == 0;
        alarm(10);
        const int observed = limited && signal_ready && mask_ready ? fixture_run((unsigned)argc) : 1;
        _Exit(observed);
    }
    else if (child > 0)
    {
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        }
        while (waited < 0 && errno == EINTR);
        if (waited == child && WIFEXITED(status))
        {
            result = WEXITSTATUS(status);
        }
        else
        {
            fprintf(stderr, "fenv-query-148: observation child did not finish normally (status=%d errno=%d)\n", status, errno);
        }
    }
    else
    {
        perror("fenv-query-148: fork");
    }
    return result;
}
