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

### Current glibc's Clang wrappers (#1361)

`pass_object_size(N)` and `pass_dynamic_object_size(N)` accept modes 0 through
3 on pointer parameters. Definitions require a const pointer, and duplicate
size attributes are rejected. Their parameter modes remain immutable metadata
on the function declaration and its overload candidate; they do not alter C's
type identity or add hidden ABI arguments. A function's metadata allocates one
byte per parameter only when at least one parameter carries a size attribute.
The declaration index distinguishes ordinary and size-annotated signatures,
and the existing scalar/vector conversion comparison prefers a size-annotated
candidate when conversions tie. Function addresses select ordinary overloads.
`check_input_declarator_end` publishes the modes and count through
`CheckInputAttributes`; `check_input_overload_declare` shares them with the
immutable candidates. `check_overload_declare_chain` validates repeated
annotations and `check_overload_better` resolves tied conversions. The bytes
live in the checked unit's output arena through emission, with no readable
padding or mutable aliases; allocation failure follows the normal resource
error path. Only annotated signatures allocate this payload, and no subsequent
consumer rescans the parameter tokens to reconstruct it.

This is the issue's conservative, unchecked compatibility path. Within a
wrapper, queries of its pointer parameters use the existing unknown-size
answers: `(size_t)-1` for modes 0/1 and zero for modes 2/3. Ordinary certified
local-object bounds remain available. No caller's bound or dynamic allocation
extent is propagated into the callee. This does not implement Clang's hidden
size arguments or permit mixing size-annotated entry points with Clang-built
callers/callees. Overloadable compatibility symbols carry a
`.__buster_object_size` suffix to distinguish their ordinary ABI from Clang's.
Inline wrappers have private, demand-selected bodies even where GNU inline
rules would otherwise defer the call to an external definition. This retains
the wrapper's checking calls and printf security flags and avoids publishing
duplicate definitions when several translation units include the same header.

On size-annotated wrappers and in system headers, the accompanying
`diagnose_if(condition, message, severity)` attributes retain
their ranges and validate their strings, but do not issue conditional call-site
diagnostics. `enable_if` accepts an integer constant true condition, as used by
glibc's unconditional overload preference; conditional candidate filtering is
still refused. Both attributes remain unsupported on user functions without
size annotations. The system-header case includes glibc 2.44's `openat2`
wrapper, whose diagnostic concerns a structure size rather than a hidden
object size. These limits are separate from libc's runtime checking calls.

The real-header regression now also runs on glibc 2.40 and newer. It includes
`stdio.h`, `string.h`, `strings.h`, `stdlib.h`, `unistd.h`, `wchar.h`, `poll.h`,
`fcntl.h` and `sys/socket.h` with `_GNU_SOURCE`, at both fortify levels 2 and 3,
on scalar/vector and eager/lazy header paths. It exercises copying, formatted
output, an ordinary function address, file reads, wide conversion, polling and
socket I/O. Direct builtin, ABI, symbol and overflow tests remain independent.
The suite also links two translation units containing the same wrappers and
checks writable-format `%n` rejection through the real stdio headers.

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
- [Clang object-size attributes](https://clang.llvm.org/docs/AttributeReference.html#pass-object-size-pass-dynamic-object-size)
