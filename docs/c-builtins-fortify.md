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

Within a wrapper's own body, queries of its pointer parameters use the
existing unknown-size answers: `(size_t)-1` for modes 0/1 and zero for modes
2/3. A call that the lowering expands takes its caller's bound instead ("The
wrappers' bounds", below). No dynamic allocation extent is propagated into the
callee. This does not implement Clang's hidden
size arguments or permit mixing size-annotated entry points with Clang-built
callers/callees. Overloadable compatibility symbols carry a
`.__buster_object_size` suffix to distinguish their ordinary ABI from Clang's.
Inline wrappers have private, demand-selected bodies even where GNU inline
rules would otherwise defer the call to an external definition. This retains
the wrapper's checking calls and printf security flags and avoids publishing
duplicate definitions when several translation units include the same header.

On size-annotated wrappers, the accompanying
`diagnose_if(condition, message, severity)` attributes retain their ranges and
validate their strings, but do not issue conditional call-site diagnostics. In
system headers and in user code, `diagnose_if` and `enable_if` are evaluated as
clang evaluates them (#1447, `docs/c-checker.md`'s "diagnose_if and
enable_if"); a system header's condition that the evaluator doesn't take
(`__builtin_object_size` of a parameter, for one) is kept and decides nothing.
The system-header case includes glibc 2.44's `openat2` wrapper, whose
diagnostic concerns a structure size rather than a hidden object size. These limits are separate from libc's runtime checking calls.

The real-header regression now also runs on glibc 2.40 and newer. It includes
`stdio.h`, `string.h`, `strings.h`, `stdlib.h`, `unistd.h`, `wchar.h`, `poll.h`,
`fcntl.h` and `sys/socket.h` with `_GNU_SOURCE`, at both fortify levels 2 and 3,
on scalar/vector and eager/lazy header paths. It exercises copying, formatted
output, an ordinary function address, file reads, wide conversion, polling and
socket I/O. Direct builtin, ABI, symbol and overflow tests remain independent.
The suite also links two translation units containing the same wrappers and
checks writable-format `%n` rejection through the real stdio headers. The
`mbstowcs` case validates object emission and execution through Clang's linker:
the direct executable binder separately rejects two external asm-label
declarations naming the same libc symbol, including without fortification.

## The wrappers' bounds (#2983)

glibc's fortified functions are `__fortify_function` wrappers: `extern inline`,
`always_inline` and `gnu_inline` definitions whose body asks for the object
size of its own parameter (`__builtin___memset_chk (__dest, __ch, __len,
__glibc_objsize0 (__dest))`). Clang's AlwaysInliner expands every call of such
a body, and its code answers the query after the expansion, where the
parameter is the caller's argument: `buf`'s 8 bytes. Before #2983, buster called
the external function instead (an inline-only definition makes no code of its
own) and never asked; inside the body the query answers `(size_t)-1` anyway, so
every `__*_chk` call became the plain one, and `-O2 -D_FORTIFY_SOURCE=2/3`
builds had none of glibc's checks.

The lowering now expands a direct call of such a wrapper in its caller
(`check_lower_wrapper_call`, `c_check_program_lower.c`), as clang does:

- **Which wrappers.** The selection lists them (`check_program_select_wrappers`,
  `c_check_program_select.c`, CheckProgram.wrappers): an `always_inline`
  function, not `noinline`, whose only definition is inline only (or one of
  `pass_object_size`'s private wrappers, above), with a prototype, not
  variadic, of at most eight integer or pointer parameters and an integer,
  pointer or void result, whose body is one statement, `return E;` or, in a
  void function, `E;`. A wrapper with a result may instead have the shape of
  glibc's longer ones (`fgets`, `wcscpy`, `realpath`, `strlcpy`): scalar
  locals, each with an initializer (`size_t sz = __glibc_objsize (__s);`),
  then guards `if (C) return E;`, then `return E;` or the last guard's
  `else return E;` (`check_program_wrapper_statements`). The expressions read
  its parameters and locals and never assign them or take their addresses,
  and refer to nothing but its parameters, locals, constants and
  objects of external linkage, which the caller's code reaches by their
  symbols, through C's operators that read, convert, select and call. Of the
  builtins it may use only the object-size and constant queries and glibc's
  fortified calls (`__builtin___memcpy_chk` and its kin): the lowering plans
  the others' helpers for the selected bodies' nodes alone. Each object a
  listed wrapper refers to is needed wherever the wrapper's object is
  (`check_program_wrapper_needed`).
- **The expansion.** The arguments, each converted to its parameter's type,
  go into temporaries in their order; the parameters' objects then read those
  temporaries (a WRAPPER task binds and unbinds them), and E is lowered in the
  caller's body, converted to the call's type. A longer wrapper's locals go
  into temporaries of their own, bound with the parameters, in their order,
  and its guards become a chain of conditionals, as `C ? E : ...` lowers:
  each C branches past its E, whose value jumps to the chain's end, where the
  last return's joins it. A call in the wrapper's own
  expression stays a call, and so does one more than four expansions deep, a
  musttail return's and a static chain's. Every other reference to the function
  (its address, a runtime call of the same name, a call the rules above don't
  take) still reaches the external symbol, as clang's references reach the
  external definition of an `available_externally` body.
- **The bound.** A query of a parameter in E (`__builtin_object_size` or
  `__builtin_dynamic_object_size`) answers its argument's object size
  (`check_lower_wrapper_size`): where the argument points into a named object
  of a complete, fixed type, or a string literal, at a constant offset (an
  array, `&x`, `&a[i]`, `p + i` with a constant `i`, a pointer's cast), the
  bytes from there to the object's end; otherwise the unknown answers. Modes 1
  and 3 (the closest subobject) depend on where clang computes the bound:
  - a parameter without `pass_object_size` (glibc 2.39's wrappers): clang's
    code generation leaves the query to LLVM's objectsize after inlining,
    which knows objects and not subobjects, so modes 1 and 3 answer as 0 and 2
    do, and `strcpy (s.a, ...)` is bounded by `s` (GCC's build bounds it by
    `s.a`);
  - a parameter with `pass_object_size(M)` (glibc 2.40's Clang overloads),
    queried in a mode M's value serves (clang's `areBOSTypesCompatible`): the
    caller's front end computes it in mode M, where modes 1 and 3 bound an
    array argument by its own bytes, so `strcpy (s.a, ...)` is bounded by
    `s.a`. Mode 3 knows no other subobject.

  The fortified call then keeps its check where the bound is known and the
  length isn't proven within it (`check_lower_fortify`), as clang's code does.

The two tiers both lower from the expanded typed stream, so `-c`, `--tier1`,
`-S`, `-g` and an executable all take the checks.

When compiling an object, assembly, or an executable, buster warns once per
translation unit that requests a positive `_FORTIFY_SOURCE` level. The warning
also applies if glibc selected a positive level before the source undefined the
macro. Use `-Werror=fortify-source` to reject a build with these limits, or
`-Wno-fortify-source` to acknowledge them. Syntax-only and preprocessing-only
commands skip this code-generation diagnostic.

What stays unchecked:

- **Two of glibc's longer wrappers.** Of the 71 `__fortify_function`
  definitions that glibc 2.39's `<stdio.h>`, `<string.h>`, `<strings.h>`,
  `<stdlib.h>`, `<unistd.h>`, `<wchar.h>`, `<poll.h>`, `<fcntl.h>`,
  `<sys/socket.h>`, `<sys/select.h>`, `<syslog.h>` and `<signal.h>` make with
  `_GNU_SOURCE` at `-O2 -D_FORTIFY_SOURCE=2`, 22 declare a local or test before
  their return, and 20 of those expand. Two still reach the external
  function: `fread_unlocked`, whose body at `-O2` (`__USE_EXTERN_INLINES`)
  holds a loop that reads small constant counts by `getc_unlocked`, and
  `recvfrom` under `_GNU_SOURCE`, whose `__SOCKADDR_ARG` parameter is a
  transparent union, not a pointer (without `_GNU_SOURCE` it is a pointer, and
  the call expands).
- **A conditional argument's bound.** `realpath (c ? small : large, ...)`'s
  bound is unknown: GCC's `__builtin_dynamic_object_size` (level 3) picks the
  arm's size at run time, and its level 2 takes the larger. Clang's build, as
  buster's, calls the unchecked function.
- **Allocations.** A pointer from `malloc (8)` has no named object; clang's
  optimizer bounds it by the allocation (`alloc_size`), and buster's answer is
  unknown.

Clang 23's build itself leaves glibc 2.39's `read`, `pread` and `getcwd`
unchecked, whose bodies (`__glibc_fortify`) test `__builtin_constant_p` of the
bound: for an overflowing `read (fd, buf, 16)` into `char buf[8]` its code
calls the plain `read`. glibc's intent, which
GCC's build follows and glibc 2.40's Clang overloads restore, is
`__read_chk`'s abort, and so is buster's. The longer wrappers are the same:
with glibc 2.39, clang's build of an overflowing `fgets`, `fread`, `recv`,
`mbstowcs` or `wcscpy` runs on, and GCC's and buster's abort.

The longer wrappers' guards test `__builtin_constant_p` of their length and
bound (`__glibc_safe_or_unknown_len`, `__glibc_unsafe_len`), which the body's
own checking answers false. The expansion decides them as the caller's code
does (`check_lower_wrapper_guards`): it evaluates the wrapper's nodes in the
caller's context, where a parameter is its argument's constant, a local its
initializer's value, an object-size query of a parameter its argument's bound
and `__builtin_constant_p` whether its operand is constant there. A guard
whose condition is constant is no test: a false one is left out, and a true
one's return ends the expansion. So a constant length that fits, or an unknown
bound, reaches the plain call, as GCC's build does. That matters beyond speed:
glibc's `__fgets_chk` returns NULL for a length of 1 and leaves the buffer,
where `fgets` writes `""` and returns it (#3325's regression). A length known
only at run time, into an object of known size, calls `__fgets_chk` in GCC's
build and in buster's; clang's build with glibc 2.39 calls `fgets`. Where a
guard stays a run's test and some `__builtin_constant_p` answers true in the
caller's context, the body's lowering of the guard can't follow, and the call
stays a call.

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
