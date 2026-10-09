// Temporary first-party Windows SEH ABI/runtime control for #2279/#3276.
// Clang default validates callback unwinding; explicit async mode also tests
// a faulting argument load. This is diagnostic, not RAD GUI qualification.
typedef void (*callback_fn)(void *);
typedef void (*safe_fn)(callback_fn, callback_fn, void *);
typedef int (*outer_fn)(safe_fn, callback_fn, callback_fn, void *);
typedef int (*bad_arg_fn)(safe_fn, callback_fn, callback_fn, void * volatile *);

__declspec(dllimport) void __stdcall RaiseException(unsigned long, unsigned long, unsigned long, unsigned long long const *);
int __cdecl printf(char const *, ...);
void run_outer_controls(int);
void run_local_controls(void);

#define RAD_SEH_NOINLINE __declspec(noinline)

volatile int call_count, fail_count, value_seen, after_raise, fail_after;
volatile int nested_before, nested_after, leaf_after, outer_after, arg_after;
void * volatile seh_call_pc, * volatile seh_fail_pc;
int seh_checks, seh_errors;

void seh_check(char const *kind, char const *test, int ok)
{
    ++seh_checks;
    printf("RADDBG_SEH_CHECK %s %s %s call=%p fail=%p\n",
           kind, test, ok ? "PASS" : "FAIL", seh_call_pc, seh_fail_pc);
    if (!ok)
    {
        ++seh_errors;
    }
    return;
}

void seh_reset(void)
{
    call_count = fail_count = value_seen = 0;
    after_raise = fail_after = 0;
    nested_before = nested_after = leaf_after = 0;
    outer_after = arg_after = 0;
    seh_call_pc = seh_fail_pc = 0;
    return;
}

RAD_SEH_NOINLINE void normal_callback(void *p)
{
    seh_call_pc = __builtin_return_address(0);
    ++call_count;
    ++*(int *)p;
    return;
}

RAD_SEH_NOINLINE void mutate_then_raise(void *p)
{
    seh_call_pc = __builtin_return_address(0);
    *(int *)p = 41;
    RaiseException(0xe0424242ul, 0, 0, 0);
    after_raise = 1;
    return;
}

RAD_SEH_NOINLINE void leaf_callback(void *p)
{
    seh_call_pc = __builtin_return_address(0);
    *(int *)p = 53;
    RaiseException(0xe0424243ul, 0, 0, 0);
    leaf_after = 1;
    return;
}

RAD_SEH_NOINLINE void nested_callback(void *p)
{
    nested_before = 1;
    leaf_callback(p);
    nested_after = 1;
    return;
}

RAD_SEH_NOINLINE void capture_callback(void *p)
{
    seh_fail_pc = __builtin_return_address(0);
    ++fail_count;
    value_seen = *(int *)p;
    return;
}

RAD_SEH_NOINLINE void original_safe_call(callback_fn f, callback_fn h, void *p)
{
    __try
    {
        f(p);
    }
    __except(1)
    {
        if (h)
        {
            h(p);
        }
    }
    return;
}

static RAD_SEH_NOINLINE _Bool catch_as_bool(callback_fn f, void *p)
{
    _Bool result = 0;
    __try
    {
        f(p);
    }
    __except(1)
    {
        result = 1;
    }
    return result;
}

RAD_SEH_NOINLINE void outlined_safe_call(callback_fn f, callback_fn h, void *p)
{
    if (catch_as_bool(f, p) && h)
    {
        h(p);
    }
    return;
}

static void run_callback_cases(char const *kind, safe_fn s)
{
    int value = 7;

    seh_reset();
    s(normal_callback, capture_callback, &value);
    seh_check(kind, "normal", value == 8 && call_count == 1 &&
              fail_count == 0 && seh_call_pc != 0);

    value = 7;
    seh_reset();
    s(mutate_then_raise, capture_callback, &value);
    seh_check(kind, "mutation-visible", value == 41 && value_seen == 41 &&
              fail_count == 1 && after_raise == 0 &&
              seh_call_pc != 0 && seh_fail_pc != 0);

    value = 7;
    seh_reset();
    s(nested_callback, capture_callback, &value);
    seh_check(kind, "nested-unwind", value == 53 && nested_before == 1 &&
              nested_after == 0 && leaf_after == 0 && value_seen == 53 &&
              seh_call_pc != 0 && seh_fail_pc != 0);
    return;
}

void run_core_controls(void)
{
    run_callback_cases("original", original_safe_call);
    run_callback_cases("outlined", outlined_safe_call);
    return;
}

int main(int argc, char **argv)
{
    int test_bad_arg = argc > 1 && argv[1][0] == 'a';

    run_core_controls();
    run_outer_controls(test_bad_arg);
    run_local_controls();
    printf("RADDBG_SEH_RESULT checks=%d failures=%d\n", seh_checks, seh_errors);
    return seh_errors ? 1 : 0;
}

extern volatile int fail_count, after_raise, fail_after, outer_after, arg_after, call_count;
extern void * volatile seh_call_pc, * volatile seh_fail_pc;
extern void seh_check(char const *, char const *, int);
extern void seh_reset(void);
extern void original_safe_call(callback_fn, callback_fn, void *);
extern void outlined_safe_call(callback_fn, callback_fn, void *);
extern void normal_callback(void *);
extern void capture_callback(void *);

static RAD_SEH_NOINLINE void raise_callback(void *p)
{
    (void)p;
    seh_call_pc = __builtin_return_address(0);
    RaiseException(0xe0424244ul, 0, 0, 0);
    after_raise = 1;
    return;
}

static RAD_SEH_NOINLINE void raise_failure(void *p)
{
    (void)p;
    seh_fail_pc = __builtin_return_address(0);
    ++fail_count;
    RaiseException(0xe0424245ul, 0, 0, 0);
    fail_after = 1;
    return;
}

static RAD_SEH_NOINLINE int outer_guard(safe_fn s, callback_fn f, callback_fn h, void *p)
{
    int result = 0;
    __try
    {
        s(f, h, p);
    }
    __except(1)
    {
        result = 1;
    }
    if (!result)
    {
        outer_after = 1;
    }
    return result;
}

static RAD_SEH_NOINLINE int bad_argument_guard(safe_fn s, callback_fn f, callback_fn h, void * volatile *bad)
{
    int result = 0;
    __try
    {
        s(f, h, *bad);
    }
    __except(1)
    {
        result = 1;
    }
    if (!result)
    {
        arg_after = 1;
    }
    return result;
}

static void run_outer_cases(char const *kind, safe_fn s, int test_bad_arg)
{
    int value = 7;
    int caught = 0;
    void * volatile *bad = (void * volatile *)(unsigned long long)1;

    seh_reset();
    caught = outer_guard(s, raise_callback, raise_failure, &value);
    seh_check(kind, "failure-to-outer", caught == 1 && fail_count == 1 &&
              after_raise == 0 && fail_after == 0 && outer_after == 0 &&
              seh_call_pc != 0 && seh_fail_pc != 0);

    if (test_bad_arg)
    {
        seh_reset();
        caught = bad_argument_guard(s, normal_callback, capture_callback, bad);
        seh_check(kind, "bad-argument", caught == 1 && call_count == 0 &&
                  fail_count == 0 && arg_after == 0);
    }
    return;
}

void run_outer_controls(int test_bad_arg)
{
    run_outer_cases("original", original_safe_call, test_bad_arg);
    run_outer_cases("outlined", outlined_safe_call, test_bad_arg);
    return;
}

extern void seh_check(char const *, char const *, int);
extern void seh_reset(void);
extern void * volatile seh_call_pc;

static volatile int local_after_raise;

static RAD_SEH_NOINLINE void mutate_local(int *value)
{
    seh_call_pc = __builtin_return_address(0);
    *value = 41;
    RaiseException(0xe0424247ul, 0, 0, 0);
    local_after_raise = 1;
    return;
}

static RAD_SEH_NOINLINE int local_type_probe(void)
{
    typedef struct THREADNAME_INFO
    {
        unsigned long dwType;
        char const *szName;
        unsigned long dwThreadID;
        unsigned long dwFlags;
    } THREADNAME_INFO;
    THREADNAME_INFO info = {0};
    int caught = 0;
    __try
    {
        RaiseException(0xe0424246ul, 0, sizeof(info) / sizeof(unsigned long long),
                       (unsigned long long const *)&info);
    }
    __except(1)
    {
        caught = 1;
    }
    return caught && sizeof(info) == 3 * sizeof(unsigned long long);
}

static RAD_SEH_NOINLINE int capture_automatic_local(void)
{
    int local = 7;
    int observed = 0;
    local_after_raise = 0;
    __try
    {
        mutate_local(&local);
    }
    __except(1)
    {
        observed = local;
    }
    return observed == 41 && local_after_raise == 0;
}

void run_local_controls(void)
{
    int type_ok;
    int local_ok;

    seh_reset();
    type_ok = local_type_probe();
    seh_call_pc = 0;
    seh_check("local-type", "sizeof-address", type_ok);

    seh_reset();
    local_ok = capture_automatic_local();
    seh_check("local-capture", "automatic-local", local_ok && seh_call_pc != 0);
    return;
}