# CPython compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in CPython compatibility harness takes an external, pristine CPython
v3.13.9 checkout; upstream sources are never copied into or patched in this
repository:

```sh
./build.sh build --config Release -t ide
tools/fetch_cpython.sh /path/to/cpython-v3.13.9
./build.sh test_cpython --config Release /path/to/cpython-v3.13.9
```

For a bounded harness-plumbing check, use
`./build.sh compatibility_spawn_self_test`. On Linux/macOS it checks the real
CPython command helper with conflicting captured variables, a preserved PATH,
an unrelated variable, an empty value and a newly added override. Mismatched
environment slices, embedded NUL bytes and simultaneous inherited/explicit OS
environment policies remain rejected. It also checks the actual zlib configure
helper's relative script `$0`; Linux separately reproduces the old absolute
shebang spelling with a successful child. Other platforms report unsupported
and return failure rather than claiming those shell checks ran.

The independent reference stage can run without a built Buster compiler:

```sh
./build.sh test_cpython --reference-only /path/to/cpython-v3.13.9
```

This still verifies the pristine pin, establishes the exact 8 MiB stack limit,
uses the existing Clang configure/build and deterministic workload, and retains
`workload.py` plus `reference-workload.stdout` below
`build/cpython-reference-self-test-*/`. It prints `CPYTHON_REFERENCE_ONLY` and
explicitly leaves Buster compilation, its workload comparison and both full
suites unrun. A reference-only pass is harness evidence, not CPython support or
compiler acceptance. Compare the retained workload with an independently
invoked host Python when replaying this stage.

Command calls with no overrides keep the captured full-inheritance policy.
Calls requesting `PYTHONHASHSEED=0` and `TZ=UTC` build a complete explicit
environment from the captured key/value snapshot, replacing inherited
occurrences of those keys while retaining all other variables. They do not
combine explicit keys with OS inheritance or modify the parent environment.

The checkout must be tag `v3.13.9` at commit
`8183fa5e3f78ca6ab862de7fb8b14f3d929421e0` with no tracked or untracked
changes. The harness configures and builds the tree with CPython's own
autoconf build system three times -- once with clang as the reference, once
per register allocator with `ide cc` -- passing `MODULE_BUILDTYPE=static`,
and drives each build with `make -j4`. The static module build predates the
driver's x86-64 Linux `-shared`/PIE support (#1712); switching to shared
modules needs a pristine harness run to requalify it.
Both compilers configure and build in GNU11: configure receives the dialect
in `CFLAGS` alongside `-g -O3`, and the make command supplies
`CFLAGS_NODIST=-std=gnu11` after upstream's configured `-std=c11`, retaining
its warning and visibility flags. The separately compiled Buster trampoline
uses the same dialect. This selects the existing GNU callback-storage
conversion policy for CPython's `void *` module slots; ISO C still rejects
implicit function-pointer/object-pointer conversions, and incompatible
function-pointer signatures remain errors in the configure probes.
CPython's own regression suite is the oracle, and the gate is the verdict
comparison: a test the Buster FAST build fails while the Clang build of the
same tree passes fails the run. Tests failing in both builds are environment
findings, reported and not gated on. Both suites run under `PYTHONHASHSEED=0
TZ=UTC -j2 -u none --timeout 120`, so nothing touches the network, and both run
with an exact 8 MiB soft stack limit. That limit is the evaluator-frame
regression gate from issue #79: raising the stack concealed frames too large
for CPython's recursion accounting instead of testing the ordinary Linux
budget. The current state of that gate is recorded under
[Evaluator frame status](#evaluator-frame-status-issue-79). The QUALITY
build proves the whole tree still
compiles, links, and answers a deterministic workload -- json, hashlib, pickle
round trips, the class machinery -- byte-for-byte against the Clang build; the
full suite runs once per side. The retired NONE and MIR_STACK modes are not
part of the active harness.

pyconfig.h is the record of what ~700 autoconf probes concluded about the
compiler, and the harness diffs it against the Clang configure with exactly
one expected divergence: `HAVE_GCC_ASM_FOR_X64`, whose probe writes a bare
literal-register `asm` this compiler refuses by design. Two others stood
there until the `__atomic_*` family (issue 829) and the hard error for an
incompatible function-pointer assignment (issue 830) landed on main; both are
gated now rather than allowed, so a regression in either fails the run. Any
other divergence fails it too.
Getting the probes to this state was most of the harness's yield: autoconf
reads link failures, `-E` output and `sizeof` refusals as answers, so a
compiler that mispronounces any of them silently configures a different
python.

What the harness does not gate, and why: `Modules/Setup.local` disables the
seven modules upstream marks `*shared*` (each exists to exercise
shared-object import, which the static module build cannot produce) plus
`_testinternalcapi`, whose static build cannot link into the
`_freeze_module` bootstrap under any toolchain -- it references
`_Py_Get_Getpath_CodeObject`, defined only by getpath.o, while the bootstrap
deliberately links getpath_noop.o. Both trees carry the same file, so their
suites skip the same tests. Buster compiles `Python/perf_jit_trampoline.o`
itself with its supported `-g` debug mode and the ordinary non-PIC driver
default. The former object-specific `-fno-pic` spelling became a no-op once
GitHub #76 moved the unit away from a substituted Clang object; GitHub #78
records the GOTPCRELX linker gap that originally required it and the full
conversion table that retired the workaround. The unit still directly
exercises source conditional directives inside a macro argument. The Clang
reference compiles the same unit through CPython's generated make rules.
The removed substitute's Clang-specific `-gdwarf-4` spelling is no longer
needed; the ELF reader accepts the ordinary DWARF 5 section family (GitHub
#77). `CPYTHON_UNIT` records this Buster-built object independently for every
allocator, and `CPYTHON_REMAINDER` distinguishes a later whole-tree failure
without treating the unit as failed. `test_gdb`'s two tests are the expected buster-only
suite divergence: gdb inspects a running python, and the exemption was
recorded when Buster-linked executables carried no `.symtab` (issue 843,
GitHub #80). The ELF symbol-table writer has since landed (#606), so the
exemption stays only until a pristine run shows whether gdb now agrees.
Refleak hunting, the
resource-gated suite surface (`-u all`), and performance are out of scope.

The run leaves `build/cpython-v3.13.9-<pid>/` behind -- three configured
trees, the workload, and both suite transcripts -- and is not cleaned up on
the way out. A full run is dominated by the two suite executions at about
ten minutes each plus three configure+make cycles; budget roughly an hour.

## Evaluator frame status (issue #79)

Measured on `main` `222cf440` with a Clang-built Release `ide` on x86-64
Linux, compiling the pinned `Python/ceval.c` with the harness's include and
define set. `-U__SSE2__` is diagnostic only: without it the unit stops on the
Clang intrinsic-header barrier (#1419), which therefore still blocks the
pristine harness. `_PyEval_EvalFrameDefault` frame sizes (bytes below the
pushed registers):

| Compiler | Frame |
|---|---|
| Clang 18 `-O3` | 392 |
| Buster FAST | 1,672 |
| Buster QUALITY | 2,488 |
| Buster NONE / MIR_STACK | 209,920 |

The historical mixed-build evidence covers FAST: a Clang tree whose
`ceval.o` alone is Buster FAST passes the unmodified
`test_functools` (including both `test_lru_recursion` methods) under the 8 MiB
limit, and an `lru_cache` recursion to the full C recursion limit raises
`RecursionError` rather than overflowing. That mixed build needs about
6.5 MiB of stack to reach the limit where the Clang build needs about 2 MiB,
so the margin is real but not large. The QUALITY frame measurement alone
does not prove recursion safety. NONE and MIR_STACK were retired and are no
longer built by the harness. A pristine all-Buster run, where the C wrapper frames
are Buster-compiled too, remains outstanding until #1419 is resolved.
