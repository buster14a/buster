// A deliberately refused assignment. `test_hook_func` has the prototype
// `int(const char*, int)` and `rl_startup_hook` points at `int(void)`; clang
// has made this an error by default since Clang 16 and GCC diagnoses it, while
// this frontend used to compile it in silence (issue #830). The silence flipped
// autoconf probes that read the diagnostic as their answer -- this is exactly
// CPython's readline check, which concluded from a successful compile that
// `rl_startup_hook` takes arguments and set Py_RL_STARTUP_HOOK_TAKES_ARGS where
// the Clang reference configure did not.
//
// The driver test asserts the diagnostic; tests/basic_c_function_pointer_compatibility.c
// is the other half, the conversions that must stay legal. This file is
// expected never to compile.

typedef int rl_hook_func_t(void);
extern rl_hook_func_t* rl_startup_hook;
extern int test_hook_func(const char* text, int state);

int main(void)
{
    rl_startup_hook = test_hook_func;
    return 0;
}
