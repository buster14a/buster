# CPython compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in CPython compatibility harness takes an external, pristine CPython
v3.13.9 checkout; upstream sources are never copied into or patched in this
repository:

```sh
./build.sh build --config Release -t ide
tools/fetch_cpython.sh /path/to/cpython-v3.13.9
./build/build test_cpython --config Release /path/to/cpython-v3.13.9
```

The checkout must be tag `v3.13.9` at commit
`8183fa5e3f78ca6ab862de7fb8b14f3d929421e0` with no tracked or untracked
changes. The harness configures and builds the tree with CPython's own
autoconf build system five times -- once with clang as the reference, once
per register allocator with `ide cc` -- passing `MODULE_BUILDTYPE=static`
because the driver has no `-shared`, and drives each build with `make -j4`.
CPython's own regression suite is the oracle, and the gate is the verdict
comparison: a test the Buster FAST build fails while the Clang build of the
same tree passes fails the run. Tests failing in both builds are environment
findings, reported and not gated on. Both suites run under `PYTHONHASHSEED=0
TZ=UTC -j2 -u none --timeout 120`, so nothing touches the network, and both
get a 512 MB stack limit for the same frame-layout reason the QuickJS harness
raises it (issue 842: CPython tunes `Py_C_RECURSION_LIMIT` assuming an eval
frame well under a kilobyte where Buster's is 4 KB). The NONE, MIR_STACK and
QUALITY builds prove the whole tree still compiles, links, and answers a
deterministic workload -- json, hashlib, pickle round trips, the class
machinery -- byte-for-byte against the Clang build; the full suite runs once
per side.

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
shared-object import, and the driver has no `-shared`) plus
`_testinternalcapi`, whose static build cannot link into the
`_freeze_module` bootstrap under any toolchain -- it references
`_Py_Get_Getpath_CodeObject`, defined only by getpath.o, while the bootstrap
deliberately links getpath_noop.o. Both trees carry the same file, so their
suites skip the same tests. `Python/perf_jit_trampoline.o` is compiled with
`clang -fno-pic -gdwarf-4` in both trees (conditional directives inside a
macro argument, issue 838; the historical DWARF 5 reader gap, issue 840;
GOTPCRELX conversion, issue 841). The ELF reader now accepts the ordinary
DWARF 5 section family (GitHub #77); this harness retains its existing
`-gdwarf-4` pin until the full CPython workflow is revalidated without it. `test_gdb`'s two tests are the expected buster-only
suite divergence: gdb inspects a running python and Buster-linked
executables carry no `.symtab` (issue 843). Refleak hunting, the
resource-gated suite surface (`-u all`), and performance are out of scope.

The run leaves `build/cpython-v3.13.9-<pid>/` behind -- five configured
trees, the workload, and both suite transcripts -- and is not cleaned up on
the way out. A full run is dominated by the two suite executions at about
ten minutes each plus five configure+make cycles; budget roughly an hour.
