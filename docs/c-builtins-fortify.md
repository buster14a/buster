# Fortified libc builtins (#803)

The source adapter accepts the fourteen exact glibc checking builtins:
`memcpy`, `memmove`, `mempcpy`, `memset`, `strcpy`, `stpcpy`, `strncpy`,
`stpncpy`, `strcat`, `strncat`, `sprintf`, `snprintf`, `vsprintf` and
`vsnprintf`, spelled `__builtin___NAME_chk`.

Their prototypes come from the existing, provenance-recorded Clang metadata.
`c_builtin_fortify.c` is a separate exact-name admission table, not an expansion
of the frozen own-main intrinsic inventory. Ordinary call checking supplies
arity, parameter conversions, return types and default variadic promotions.
The x86 constraint adapter's 64-operand limit does not apply to these calls.

`c_check_program_fortify.c` selects an ordinary or checking libc symbol and
uses the ordinary call lowering/ABI path. It does not manufacture declarations,
change binding identities or implement a second variadic ABI. It preserves all
arguments to checking calls, including the `va_list` and printf security flag.

## When a check can be removed

A size of `(size_t)-1` allows an ordinary memory/string call. A proven write
length at most the destination bound also permits reduction for memory calls,
`strncpy` and `stpncpy`. For `strcpy` and `stpcpy`, a literal's bytes up to its
first terminating zero, including a proven interior offset, can establish a
sufficient bound. `strncat`'s source limit is **not** a bound on the entire
output string, so it does not permit this reduction.

Formatted-output calls additionally require a proven **zero security flag**.
A nonzero flag remains checked even with size `(size_t)-1`: libc uses it to
protect `%n`. For `snprintf`/`vsnprintf`, a proven maximum output size at most
the destination bound is sufficient only with that zero flag. Otherwise the
size and flag remain runtime operands. Removed operands must be effect-free
constants; all retained operands keep their original conversions and are
evaluated once. Object-size query folding reads the runtime query result,
not a possibly deferred source constant-expression proof.

## Boundaries

`__builtin_dynamic_object_size` accepts the same mode constraints and certified
static object bounds as `__builtin_object_size`. Without a certified static
bound it returns the conservative unknown sentinel (`(size_t)-1` for modes
0/1, zero for modes 2/3). This is not dynamic allocation/VLA extent tracking,
or a claim of Clang-equivalent interprocedural object-size optimization.
Unevaluated pointer side effects are not executed to guess a bound.

The glibc 2.39 header reproducer in #803 is distinct from glibc 2.40 and newer
Clang-specific fortify wrappers. Those require `pass_object_size`,
`pass_dynamic_object_size`, overload selection and hidden-parameter ABI support
from #147. This change does not ignore those attributes, disable fortification,
or claim to fix that separate blocker. The real-header regression runs on
pre-2.40 glibc; on newer hosts it reports the prerequisite explicitly. Direct
builtin, ABI, symbol and overflow tests run on both.

## Validation

`build/compiler test-checker-fortify --clang clang --executable build/compiler`
runs the focused driver suite. The same cases are included in
`test-checker-driver --executable build/compiler`. The tests use both compiler
paths and `-O0`/`-O2`, with external Clang and libc as independent oracles.
They cover all fourteen calls, pointer/length results, argument effects,
variadic floating and long-double arguments, explicit `va_list`, more than
64 operands, checked/plain linker symbols, rejected signatures and modes,
buffer-overflow `SIGABRT`, and writable `%n` with flags zero, one and two.

Implementation and fixtures are original project material. The existing
metadata retains its recorded LLVM provenance. No external implementation or
header fixture is imported. Behavioral references:

- [GCC object-size checking builtins](https://gcc.gnu.org/onlinedocs/gcc/Object-Size-Checking.html)
- [glibc source fortification](https://sourceware.org/glibc/manual/latest/html_node/Source-Fortification.html)
- [glibc's Clang wrapper change](https://sourceware.org/pipermail/libc-alpha/2024-February/154531.html)
