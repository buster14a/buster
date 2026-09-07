# QuickJS compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in QuickJS compatibility harness takes an external, pristine QuickJS
2026-06-04 checkout and, optionally, a Test262 checkout; upstream sources are
never copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_quickjs --config Release /path/to/quickjs /path/to/test262
```

QuickJS publishes releases as dated tarballs rather than tags, so the pin is
the commit whose tree is the release —
`3d5e064e9dd67c70f7962836505a7fa067bf0a4e`, the one carrying `VERSION`
`2026-06-04` — with no tracked or untracked changes. The harness checks the
commit, the working tree and the `VERSION` file before it compiles anything.
The Test262 path is optional: without it the conformance stage reports itself
skipped, since it is a second external dependency an order of magnitude larger
than the engine. Its pin is upstream's own `TEST262_COMMIT`,
`5c8206929d81b2d3d727ca6aac56c18358c8d790` — advisory rather than enforced:
unlike the QuickJS checkout, the harness does not verify the Test262
checkout's commit, and the exact Buster-vs-Clang runner-summary comparison is
what gates the stage.

The harness owns an explicit 9-unit manifest and compiles it in named stages,
failing at the first one that breaks: the library (`quickjs.c`, `dtoa.c`,
`libregexp.c`, `libunicode.c`, `cutils.c`, `quickjs-libc.c`), then the bytecode
compiler `qjsc.c`, then the one generated input the build needs, then the CLI
`qjs.c`, then the conformance runner `run-test262.c`. Everything is built with
one flag set (`-D_GNU_SOURCE -DCONFIG_VERSION -DCONFIG_CC -DCONFIG_PREFIX -O2
-funsigned-char -fwrapv`) because upstream's Makefile does, and objects that
disagreed about `CONFIG_VERSION` or `_GNU_SOURCE` would not link. The
documented minimal feature configuration is upstream's own `make qjs` default:
no LTO, no sanitizers, no shared libraries, no cosmopolitan build and no
`CONFIG_CHECK_JSVALUE`, which changes `JSValue`'s representation. Four units
stay outside the manifest and are excluded rather than patched:
`unicode_gen.c` regenerates `libunicode-table.h` from a Unicode data drop the
checkout does not carry, the `fuzz/` entry points need a fuzzing engine, and
`tests/bjson.c` and `examples/*.c` build shared libraries loaded at run time,
which is a later driver milestone.

The generated input is `repl.c`: `qjsc` compiles `repl.js` to the bytecode
array `qjs.c` links against. Both engines produce it, and the bytes must be
identical — a compiled-in JavaScript program is a serialized result, and the
strongest equality this harness can ask for. The same check runs over the
project's own workload. Two allocators cannot make it: `qjsc` is the one
QuickJS executable with no `--stack-size` switch, so it parses inside the
engine's fixed 1 MB default, and the NONE and MIR_STACK builds exhaust that
while parsing `repl.js`. Those two continue on the reference bytecode and the
log says so (`bytecode=reference-engine-stack-exhausted`); FAST and QUALITY
produce byte-identical bytecode.

Then the harness runs the nine upstream test scripts `make test` runs, in
upstream's order and with its flags, and compares exit status, standard output
and standard error against the Clang build run for run. It also runs a
project-owned deterministic workload, `tests/basic_quickjs_workload.js`, whose
transcript must match byte for byte: it covers floating-point edge cases,
64-bit and bigint arithmetic, tagged values and property shapes, closures,
generators and exceptions, garbage collection, regular expressions and unicode
tables, JSON, typed arrays and `DataView`, collections, sorting, `eval` and
`Function`, and fixed-epoch dates. Nothing in it reads the clock, the
environment or an address. The engine's own memory report (`qjs -d`) is
compared too: every line of it is a struct size or an allocation count, so an
identical report is a statement about the layouts the two compilers chose as
much as about the run.

The engine stack is the harness's one environmental concession, and the reason
is Buster's frame layout rather than anything upstream. Buster gives every
sibling block in a function its own frame slot where Clang overlaps the ones
whose live ranges cannot intersect, and `JS_CallInternal` — the whole
interpreter, one function, one block per opcode — is the worst case in the
corpus: its prologue probes 41 pages plus 928 bytes, about 165 KB, against the
Clang build's 856. Every nested JavaScript call costs that much C stack, so
QuickJS's own 1 MB default admits five of them where the Clang build manages
1.073 — the two numbers `qjs` reports when a script recurses until the engine
stops it. The harness therefore raises `RLIMIT_STACK` to 512 MB once, up
front, where a soft limit is inherited at spawn, and passes `--stack-size 64M`
to both engines so the comparison stays symmetric. The programs are correct —
every test passes once the engine has room — so this is a code-quality gap and
not a miscompile, and shrinking the frame is optimization work that is not to
be added unasked.

The conformance stage runs a bounded, deterministic Test262 subset: thirteen
directories covering arithmetic, `switch`, tagged templates, `Number`, `Math`,
`BigInt`, sorting, `Set`, `Reflect`, `RegExp.prototype.exec`,
`DataView.prototype`, `WeakRef` and `FinalizationRegistry` — 2.156 tests, run
single-threaded through upstream's own runner with upstream's own feature
policy and known-error list. The harness writes its own copy of
`test262.conf` with three paths bound and two settings dropped (`testdir=`,
which would enumerate the whole tree, and `reportfile=`); upstream's file is
read, never written. The Buster runner's summary line must equal the Clang
runner's exactly. Only FAST and QUALITY run it: `run-test262` fixes the engine
stack at the 1 MB default with no switch, and the NONE and MIR_STACK engines do
not fit their parser inside it, which surfaces as a "SyntaxError: stack
overflow" while a test is being compiled. Those two skip the stage and the log
says which directories they did not run and why.

Compiler cost and generated-code quality are reported separately and must not
be conflated. `QUICKJS_METRIC` carries per-unit compiler wall time beside
Clang's for the same unit, the `-fsource-metrics=` source metrics, and the
count of functions the machine backend handed to the canonical emitter;
`QUICKJS_STAGE` sums them per stage; `QUICKJS_WORKLOAD` and `QUICKJS_CODEGEN`
report the workload's wall time and instructions retired; `QUICKJS_MEMORY`
reports the engine's own allocation report. One recorded run compiled the
6-unit library in 0,90 s under FAST against Clang's 10,1 s for the same
sources — `quickjs.c` alone is 2,9 MB of source, 86.791 lines, 469.236 tokens,
and Buster compiles it in 0,60 s where Clang takes 8,6 — while the workload ran
573,8 M instructions under FAST and 990,1 M under NONE against Clang's 69,9 M.
Both directions are worth trending: the compile-time column is where Buster is
ahead, the instruction column is the generated-code gap. A full matrix takes
about two minutes plus the Clang reference build. Generated objects, metrics,
bytecode and logs remain under `build/quickjs-2026-06-04-<pid>/`, which is not
cleaned up on the way out.

The GNU and POSIX surface QuickJS needs, inventoried before any of it was
implemented: `__attribute__` (both spellings, including the aggregate form
between the keyword and the tag), `_Atomic` as a type specifier and the
`__c11_atomic_*` builtins, `__builtin_frame_address`, `__builtin_alloca`,
`__builtin_signbit`, the constant math builtins hidden behind `<math.h>`'s
`NAN` and `INFINITY`, computed goto, packed structs, bit-fields of enumerated
type, `_GNU_SOURCE` libc and POSIX interfaces from `<dlfcn.h>` to
`<sys/wait.h>`, and pthreads for its worker threads. `__attribute__((packed))`
and `__attribute__((aligned(N)))` now reach both layout engines; QuickJS's
packed structs each hold a single scalar, where the packed and natural layouts
agree, so the harness never depended on it.
