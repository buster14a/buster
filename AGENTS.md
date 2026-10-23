# buster

This file is the shared repository guide for coding agents. `CLAUDE.md` imports
it; keep project instructions here rather than maintaining parallel copies.

## Goal and scope

Build a new C compiler whose **implementation runs on Linux/x86-64, tuned for
AMD Zen 5**, with heavy, deliberate use of SIMD intrinsics. Optimize both
single-threaded compile throughput and useful multicore scaling while preserving
C correctness and generated-program quality.

Code should aim for the **target machine's maximum achievable throughput** on
the actual workload, measured as useful work completed per second, both per core
and across cores. Design algorithms and data layouts around the relevant
execution, dependency, cache, memory-bandwidth and I/O limits; eliminate
unnecessary computation, passes, copies, allocation and synchronization. Use
representative end-to-end measurements, hardware counters and generated assembly
to identify the limiting resource and validate progress toward that ceiling,
while preserving correctness and generated-program quality.

This is a fresh compiler, not a compatibility branch of `buster14a/buster-legacy`.
Do not inherit that repository's frameworks, historical token parsers, CI
matrices, benchmark services, or acceptance bureaucracy. Reuse an idea only when
it earns its place here. Do not copy implementation code without checking its
license; do not select or change this repository's license on the owner's behalf.

## Licensing and contributions

Read [LICENSING.md](LICENSING.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and [CONTRIBUTING.md](CONTRIBUTING.md) before importing or submitting material.
Original contributions use `Apache-2.0 WITH LLVM-exception`; identified
components and local changes to their portions follow their retained terms.
Do not add the LLVM exception over Apache-only upstream material or label a
mixed-source file as wholly first-party.

Record the sources and revisions of imports, adaptations and generated data,
including generator inputs, and preserve copyright, license, attribution and
modification notices. Changes to the license remain the owner's decision.
Follow the prospective DCO policy and its in-flight-PR transition. Never
fabricate sign-offs, treat agent co-author trailers as human certification, or
retroactively certify historical commits. Add David's sign-off to a commit only
as "David's sign-off" below says.

## David's sign-off

Under "The maintainer's standing certification" in
[CONTRIBUTING.md](CONTRIBUTING.md), David's own agent sessions sign off their
commits for him with:

```text
Signed-off-by: David Gonzalez Martin <davidgmbb@gmail.com>
```

His opt-in gates it: `BUSTER_DCO_SIGN_OFF` set to `davidgmbb` in the session's
environment, which only he sets. Where it isn't set, never add that trailer.
Where it is:

- `.githooks/commit-msg` ends each message that `git commit` or `git merge`
  writes with it, once, after the other trailers, so don't write it yourself.
  Before your first commit, check that `git config core.hooksPath` prints
  `.githooks`; if it doesn't, run `.githooks/enable-sign-off.sh`. Claude Code
  runs it at session start (`.claude/settings.json`), and so does Codex
  (`.codex/hooks.json`) in a trusted project, once David has trusted that hook
  in `/hooks`. If it reports leaving the hook off, write the trailer yourself,
  last. While `core.commentChar` is `auto`, the hook refuses every commit, since
  it can't know the comment character `git commit` picks: set the character
  explicitly, or make the commit with the opt-in unset and the trailer written
  by hand.
- Don't use `git commit -s`, which signs off as the committer
  (`Claude <noreply@anthropic.com>` in cloud sessions), or `--no-verify`, which
  skips the hook. The hook signs nothing while a rebase or a cherry-pick is in
  progress, rewords, amends and continuations included, and a plain
  `git revert` doesn't run it: for a revert, use `git revert --no-commit` and
  then `git commit`.
- Don't sign a commit from before the certification took effect, or a rebase,
  cherry-pick or amend of one: it still needs David's certification for its
  PR's head. Unset the opt-in to amend one outside a rebase
  (`env -u BUSTER_DCO_SIGN_OFF git commit --amend`) or to commit its changes
  again another way (`git cherry-pick --no-commit`), and to commit outside
  material whose provenance the PR can't record; then say so in the PR.

## Host and output scope

The Zen 5 requirement describes the **host compiler executable**, not necessarily
the instruction set of every program it generates. Keep host ISA, generated-code
ISA, and output ABI separate. Do not add other host operating systems, older-CPU
production fallbacks, or hot-path CPU dispatch. Broader output targets and extra
applications are outside the current scope; but the x86 encoder encodes 16- and
32-bit modes' instructions too, as the owner asked.

## Repository identity

The current compiler is `buster14a/buster`, GitHub repository ID `1421805693`,
formerly `buster14a/buster-fast`. The previous implementation remains maintained
at `buster14a/buster-legacy`, ID `1071732997`, with its own history, issues and
pull requests. Verify the destination ID when retargeting a checkout or writer;
old `buster14a/buster` links now address this compiler.

Local checkout directories, `~/buster-fast-bench/`, the shared host lock,
`~/llm-memory/projects/buster-fast/` and its memory symlinks retain their existing
names. The repository-local `buster-fast-9700x` concurrency key is also retained.
Queues use that prefix per PR/ref, each holding one pending run (a newer push
replaces its PR's, and cancels the PR's run in progress); the runner hooks serialize every CI job with
the shared host lock. These are operational resources, not canonical repository URLs; do not rename
them by replacing text. Source paths, executable names and object-file metadata
are unchanged by the repository rename.

## Issue ownership

Every issue being investigated, tackled or fixed must be claimed in the issue
itself before work begins. Read its latest comments and linked pull requests,
then post a comment identifying who is taking the work and the scope being
claimed. A local plan, branch, pull request or assignee alone does not replace
this comment. When work covers several issues, claim each one.

Respect existing claims: coordinate with the current owner before taking over
or starting overlapping work. Keep the claim current with the branch or pull
request, and comment on the issue when handing off or stopping so others know
whether the work is still owned.

## Coding standards

Write code in **C** unless the goal or action explicitly disallows C or requires
another language. This applies to supporting tools, tests, and automation as
well as compiler code.

Follow [the C coding style](docs/coding-style.md) for new or modified compiler
code. The essentials are: C and Clang on Linux/Zen 5, 4-space Allman formatting,
`snake_case` functions, `PascalCase` types, `BUSTER_` macros, one return per
function, direct and explicit control flow, bounded slices, arena-oriented
allocation, and safe SIMD memory accesses. Prefer simple data layouts and
measured end-to-end performance improvements. Prefer single-line statements,
declarations, and function headers where readable, and one aggregate field or
initializer entry per line. Do not import buster-legacy's portability
or infrastructure machinery merely to match its style.

## Data-parallel first

Design every stage and every pull request data-parallel, vectorized and
branch-free first, as `src/buster/compiler/c_lexer.c` and `x86_64_encoder.c`
are built. The tools include masks over many items, prefix sums, in-register
permutes as table lookups, compress and expand, sorting and segmented scans,
bitsets, nearest-smaller-values and pointer jumping. Each vector path has an
exact scalar path for the rare cases, which is also its twin and test oracle:
both must give identical output or, where the two algorithms legitimately
differ, pass a semantic check.

No stage is assumed inherently sequential. Where C fixes an order (macro state,
typedef visibility), vectorize within each step and keep the ordered part small.
A part that stays scalar says why with measurements on the 9700X, as "SIMD: what
pays" in `docs/object-writer-and-linker.md` does. The owner set this rule on
2026-10-20, replacing the premise of
[#15](https://github.com/buster14a/buster/issues/15) that the parser and
the type checker are inherently sequential. The issues that apply it are listed
in the "Data-parallel first" section of
[#115](https://github.com/buster14a/buster/issues/115). "SIMD and Zen 5" in
[the C coding style](docs/coding-style.md) has the Zen 5 costs that shape these
designs and the compilers already built this way.

## Current repository and exact smoke commands

- `src/buster/compiler/c_builtin.c` supplies the integer builtin actions for
  #148: byte swaps, bit counts/searches, and mixed-type overflow folds, with
  AVX-512 and scalar twins. `test-builtins [--seed N]` checks Clang's results,
  undefined/invalid cases and guarded tails. `benchmark-builtins [--only
  scalar|vector] [--after-scalar MS]` measures synthetic action batches on the
  locked 9700X, pinned to CPU 2. [The builtin guide](docs/c-builtins.md) records
  the API and the checking, C selector, feature-query and corpus integration
  still required before #148 can close.

- `src/buster/compiler/x86_zen5.c` supplies the partial operand-sensitive Zen 5
  observation table and scalar/AVX-512 cost-column lookups. `test-x86-zen5`
  checks unknowns, guard-page tails and generated microbenchmark loops;
  `measure-x86-zen5` measures instruction cases on the approved 9700X;
  `benchmark-x86-zen5 [--only scalar|vector] [--after-scalar MS]` times lookups.
  Each build/test/measurement takes the shared host lock separately. Pin
  measurements to CPU 2. [The model guide](docs/x86-zen5.md) records what is
  measured and the coverage, pipe data and real consumers still pending for #181.

- `src/buster/compiler/x86_target.c` defines generated-code ISA profiles, with
  x86-64 baseline as the default, independently of the compiler host.
  `x86_target_forms.c` supplies explicit, fail-closed form eligibility and
  scalar/vector table builders. `test-x86-target` checks the policy and Clang
  predefines; `benchmark-x86-target` measures table construction.
  `docs/x86-target.md` records the API, current catalogue coverage and the
  selector/corpus acceptance still pending for #180.

- `src/buster/compiler/assembler.c` is the callable GNU assembly frontend, with
  AVX-512 and scalar paths and shared form matching in `x86_match.c`.
  `./build/compiler assemble -o output.o input.s` uses it through the existing
  executable. `test-assembler --cross-check` compares both paths and GNU as,
  and links/runs fixtures; `benchmark-assembler [--external]` reports phases,
  initialization/memory and optionally fresh-process GNU as/Clang comparisons.
  Run these through the shared lock; pin benchmarks to CPU 2. The supported
  syntax, limitations, example and measurement method are in
  [docs/assembler.md](docs/assembler.md). `.S` uses the shared preprocessor's
  explicit assembly mode, including `-I`, `-D`, `-U` and source line markers.
- `src/buster/compiler/main.c` is the executable entry point: its commands, and
  for anything else the driver (`run_parse_command`, in `c_parser.c`):
  `./build/compiler [-fsyntax-only] [-E's options] [--scalar] file.c`
  preprocesses and parses the file and prints the diagnostics as clang does,
  with exit status 0 for a unit without errors and 1 otherwise. Nothing is
  written yet. With no file it prints its usage and exits with status 1. It
  includes most of the compiler and all of its tests, and `build.sh` compiles
  it as eight translation units: `main.c` itself, the driver's unit, and each
  `main_unit_*.c`, which names a unit and includes `main.c`. Every unit reads
  all of `main.c`, so each has the whole file's diagnostics, but emits only its
  own commands' code: each branch of `main_command` first tests its unit, and
  clang emits no static function that only other units' branches reach. A new
  command's branch names its unit. On the 9700X the units take 3.5 to 7.1 s
  each (46.7 s in all), where `main.c` as one unit took 28.5 s (#270).
- `build.sh` invokes Clang and produces **only** `build/compiler`, using
  `-march=native`, so the machine executing the build determines the host ISA.
  The benchmark is linked into this executable rather than built separately.
  It compiles the translation units at once, a clang process each, and links
  them in order: the same executable as one clang command for all of them.
- `C_TOKENIZER_NO_DIGIT_SEPARATORS` makes both lexer paths end pp-numbers
  before `'`; the preprocessor sets it in modes before C23, including file
  caches, scratch text and pastes. The standalone lexer's default keeps C23
  separators. `benchmark-lexer --no-digit-separators` measures the flag.
- `src/buster/compiler/c_lexer_test.c` holds the lexer tests. `main.c` includes
  it, and `./build/compiler test-lexer` runs them.
- `src/buster/compiler/c_lexer_benchmark.c` provides the `benchmark-lexer`
  command. The 9700X CI workflow runs it on the repository's C sources by
  default. `docs/c-lexer.md` records the GFNI classifier, the leading-comment
  skip, branch-free chunk state, the pipelined write-out, table-driven
  punctuator kinds, the delimiter scan, chunks read in place, the raw string
  check, numbers without digit separators (`C_TOKENIZER_NO_DIGIT_SEPARATORS`,
  C17's) and their 9700X measurements. `c_lexer_variant.c` is one variant of the
  lexer as a translation unit of its own, which `./build.sh lexer-variant`
  compiles in for `benchmark-lexer` to compare (see "Lexer benchmarks").
- `src/buster/compiler/preprocessor_fuzz.c` provides bounded output comparison
  and the minimizer for the independent preprocessor test harness.
  `preprocessor_fuzz_generate.c` generates cases, `preprocessor_fuzz_clang.c`
  runs the Clang oracle, and `preprocessor_fuzz_test.c` provides
  `test-preprocessor-fuzz`, the harness's own checks; `main.c` includes all
  four. `preprocessor_fuzz_benchmark.c` provides `benchmark-preprocessor-fuzz`,
  measuring the external test harness. `c_preprocessor_fuzz.c` attaches the
  preprocessor to it: `test-preprocessor --fuzz`, below.
  `docs/preprocessor-fuzz.md` records the contract, the comparison and the
  qualification runs.
- `src/buster/compiler/x86_64_encoder.c` is the x86-64 encoder (the form table
  of legacy, VEX and EVEX forms, the scalar reference and the AVX-512 encoder,
  and the fixup interface: `BUSTER_X86_FORM_FIXUP` marks an instruction with a
  symbolic operand, `x86_encode` reports each marked instruction's end, and
  `x86_fixup_distance` gives its fields' offsets and addends from that; and
  `x86_encode_address32` and `x86_encode_apx`, the same for 32-bit addresses and
  for APX's general registers 16 to 31 (REX2, and EVEX's B4 and X4), each with
  templates of its own; and `x86_encode_mode32` and `x86_encode_mode16` (and
  `x86_encode_scalar_32` and `x86_encode_scalar_16`), the same for 32-bit and
  16-bit modes, with their operand and address sizes, segment overrides and own
  instructions, and templates of their own);
  `docs/x86-64-encoder.md` explains its design and measurements.
  `x86_64_encoder_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-x86-encoder` runs them) and
  `x86_64_encoder_benchmark.c` provides `benchmark-x86-encoder`.
- `docs/object-writer-and-linker.md` is the design of the object writer and the
  linker, with the 9700X measurements of mold, wild, lld and the kernel's file
  costs behind it, and the order to build it in. The lane base layer, the
  encoder's fixups, the representation, the object writer, the direct path's
  executables (static ones and dynamic PIEs) and the import, binding and
  snapshot of the C runtime below are implemented, and `benchmark-link`
  measures the direct path against the linkers; the rest of the linker isn't
  yet.
- `src/buster/compiler/lane.c` is the lane base layer that every phase of the
  object writer and the linker runs on (groups of threads that run the same
  code, their barrier and broadcast, `lane_range`, and arenas).
  `lane_test.c` holds its tests (`main.c` includes it; `./build/compiler
  test-lanes` runs them) and `lane_benchmark.c` provides `benchmark-lanes`.
- `src/buster/compiler/dataflow.c` provides aligned bitset liveness and
  interference with AVX-512 and scalar implementations, independent of IR.
  `dataflow_program.c` supplies the shared synthetic CFG generator;
  `dataflow_test.c` provides `test-dataflow` and `dataflow_benchmark.c`
  provides `benchmark-dataflow`.
  `docs/dataflow.md` records the contracts and 9700X measurements.
- `src/buster/compiler/link.c` is the representation the object writer and the
  linker share: names, atoms, symbols and fixups in column arrays indexed by
  32-bit ids, in arenas, and `link_add_code_fixups`, which turns the encoder's
  marked instructions into fixups of the design's kinds. `link_program.c`
  generates test programs in it (static ones without libc, and ones that call
  libc, from a seed and sizes) for the object writer's and the linker's tests
  and benchmarks. `link_test.c` holds the tests of both (`main.c` includes
  them; `./build/compiler test-link` runs them).
- `src/buster/compiler/object_writer.c` is the object writer: an ELF
  relocatable object per unit of the representation (`object_prepare` once per
  link, then `object_build` per object, `object_write_file`, and
  `object_write_units` for a group of lanes). `object_writer_test.c` holds its
  tests (`main.c` includes it; `./build/compiler test-object-writer` runs them)
  and `object_writer_benchmark.c` provides `benchmark-object-writer`.
- `src/buster/compiler/import.c` holds the import path's readers: ELF
  relocatable objects into the representation, archives' members and symbol
  index, shared libraries' exports at their default versions (indexed by name
  hash), version definitions and references, and the commands of the linker
  scripts that `libc.so` and GCC's `_asneeded` libraries are. The object,
  archive and library readers each have a vector path and an exact one, which
  takes what the vector path leaves to it (the design's "Step 5's phases,
  data-parallel"). `runtime.c` finds the C runtime's files as GCC's driver
  does (`Scrt1.o`, `crti.o`, `crtbeginS.o`, `crtendS.o` and `crtn.o`, and the
  `libc.so.6`, `libc_nonshared.a` and loader that `libc.so` names, the loader
  as needed) and imports them into a link of their own, with what each name
  they refer to binds to (the first part of step 5 of the design's plan), each
  object's records as a piece, and an index and a filter of their global
  names and the libraries' (the loader's version indices numbered after
  `libc.so.6`'s);
  `runtime_import_static` does the same for a static executable's runtime
  (`crt1.o`, `crtbeginT.o` and `crtend.o`, and the archives of GCC's group:
  `libgcc.a`, `libgcc_eh.a`, `libatomic.a` where GCC has it, and `libc.a`),
  with each member's strong references and COMDAT groups (step 6's first
  part), `runtime_import_static_pie` for a static PIE's (`rcrt1.o`,
  `crtbeginS.o` and `crtendS.o` with the same archives; step 6's fifth part),
  and `runtime_import_shared` for a shared library's (the dynamic runtime
  without `Scrt1.o`, its unit and piece empty).
  The object reader gives `GOTPCRELX` and `REX_GOTPCRELX` relocations the
  kinds a linker may rewrite (`CALL_GOT` and `LOAD_GOT`) only on the
  instructions those kinds are (`call` or `jmp` through the slot, a `mov`
  from it with `REX.W`), and `GOTPCREL` on any other.
  `import_test.c` holds their tests (`main.c` includes it; `./build/compiler
  test-import` runs them) and `import_benchmark.c` provides
  `benchmark-import`.
- `src/buster/compiler/bind.c` is binding (the second part of step 5): the C
  runtime that `runtime.c` imports joined to a program's link in the order
  GCC's driver links it, one symbol per global name, and each name of the link
  bound as GNU ld binds it: to the program, the runtime's objects, the linker,
  an export of `libc.so.6` at its version (an import), a member of
  `libc_nonshared.a`, which joins, or for a weak reference to nothing; with
  the link's imports and exports listed. It has a vector path and an exact
  one, its twin, which make the same link. `link_program.c`'s programs leave
  room for it (`link_program_generate_begin`, `_atoms` and `_end`).
  It binds against the static runtime too (step 6's second part): the
  archives' members the link needs, found as GNU ld's group finds them, joined
  in archive order with the COMDAT groups after the first of each signature
  discarded, and no name bound to a shared library. And against a shared
  library's runtime (step 6's fifth part), as GNU ld and lld bind `-shared`:
  no start object, a strong reference to a name no library exports left
  undefined for the loader, and every definition with default or protected
  visibility exported (`bind_shared_exports`).
  `bind_test.c` holds its tests (`main.c` includes it; `./build/compiler
  test-bind` runs them) and `bind_benchmark.c` provides `benchmark-bind`.
- `src/buster/compiler/executable.c` writes the direct path's executables: a
  link in that representation laid out, patched and written with a build ID
  by every lane of a group, as a static executable without libc (step 4 of
  the design's plan), as a dynamic PIE against glibc (step 5's third part),
  with its dynamic symbols, stubs and relocations, RELR among them, as a
  static executable with the C runtime (step 6's third part), with its
  IFUNCs' stubs and IRELATIVE relocations, its general- and local-dynamic
  TLS accesses relaxed to local-exec and its init and fini arrays' priorities,
  as a static PIE (step 6's fifth part): the PIE's layout and RELATIVE
  relocations without dynamic symbols, and the static runtime's IFUNCs with
  their IRELATIVE relocations last in `.rela.dyn`. Every kind's loads and
  calls through the GOT of the image's symbols are relaxed, as GNU ld and lld
  relax them, and an IFUNC whose stub only calls reach reads the stub's slot
  through the GOT, as lld's does; all from vector passes with exact twins.
  It also writes shared libraries (`ExecutableDynamic.shared`, with optional
  `soname`): default-visibility global and weak definitions are preemptible,
  calls use non-lazy GOT stubs, and initial-exec TLS uses TPOFF64 slots.
  General-dynamic TLS uses a separate two-word GOT pair per symbol and
  local-dynamic TLS one module pair, preserving `__tls_get_addr` calls;
  DTPMOD64/DTPOFF64 relocations and module offsets are emitted as appropriate.
  Regular shared-library outputs use a new file and rename, preserving any
  old inode that a process still maps.
  Local-exec accesses and undefined DTPOFF32 module offsets in shared
  libraries are refused. Distinct general-/initial-exec slot columns allow
  both models to reference the same symbol. TLSGD target bitsets are reduced
  by whole symbol words, then expanded into slots with an exact scalar twin.
  `dynamic.c` gives it what a PIE needs from the C runtime and from
  binding (`dynamic_inputs`), what a static executable with the C runtime
  needs (`dynamic_static_inputs`) and what a static PIE needs
  (`dynamic_static_pie_inputs`), and what a shared library needs without a
  start object or interpreter (`dynamic_shared_inputs`). `executable_test.c`
  holds the static executables' tests without libc (`./build/compiler test-executable` runs
  them), `dynamic_test.c` the PIEs' (`./build/compiler test-dynamic`) and
  `static_test.c` those with libc and the static PIEs' (`./build/compiler
  test-static`); `main.c` includes all three, and `executable_benchmark.c`
  provides `benchmark-executable`.
  `ExecutableDynamic` borrows counted library/version arrays, so foreign
  producers can supply selected dependencies in input order. A version's
  library is a 32-bit array index; version indices are unique 2..0x7fff and
  grouped by library. Required libraries include unversioned dependencies;
  optional ones are selected by used versions. `dynamic_libraries_test.c`,
  included by `dynamic_test.c`, checks high library indices, foreign imports,
  malformed metadata and exact/vector identity at 1/2/3/8 lanes. Missing import
  version definitions are refused in the existing symbol pass before output.
- `src/buster/compiler/snapshot.c` is the runtime snapshot (step 5's fourth
  part, and step 6's for the other kinds): what `runtime_import` (or
  `runtime_import_static`, `runtime_import_static_pie` or
  `runtime_import_shared`) makes of the C runtime, kept in a file per kind in
  the cache directory (`$XDG_CACHE_HOME/buster`, or `$HOME/.cache/buster`),
  which `runtime_open` (or `runtime_open_static`, `runtime_open_static_pie` or
  `runtime_open_shared`) maps and uses in place when this executable wrote
  it, it is of the kind, its directory hashes right and every file the import
  probed is as it was, and otherwise rebuilds and renames into place. Binding checks each piece of it (a crt object or a member) when
  it first reads it (`runtime_piece_current`). `snapshot_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-snapshot` runs them) and `snapshot_benchmark.c`
  provides `benchmark-snapshot`.
- `src/buster/compiler/link_benchmark.c` provides `benchmark-link`, the whole
  link (step 5's last part): generated programs that call libc linked by the
  direct path (the runtime opened from its snapshot, binding and the dynamic
  PIE's write) and, from the object writer's objects, by GNU ld, lld, mold and
  wild as the design's appendix links them, with the work each does.
  `floors_benchmark.c` provides `benchmark-floors`: what the machine and the
  kernel charge a linker (the design's "The floors"). The two take over the
  appendix's harness and floor programs.
- `src/buster/compiler/foreign.c` is the first part of linking other
  compilers' objects (#86, steps 1 to 5 of the design's "Linking other
  compilers' objects"): a command line of ELF objects, archives (thin ones
  too), shared libraries and `libc.so`'s script subset opened across lanes,
  each archive's symbol index read alone (`import_archive_lazy`; a member's
  header when it loads, `import_archive_member`), each round's objects'
  headers and global symbols parsed into columns of the round's own, their
  records exchanged over fixed hash partitions into each partition's tables,
  archive members loaded in frontier rounds (for the needed shared libraries'
  references too), and COMDAT groups picked by signature; GNU ld's results
  where they don't depend on the archives' order, lld's where they do. Each
  phase has a vector path and an exact twin.
  `foreign_test.c` holds its tests (`main.c` includes both;
  `./build/compiler test-foreign` runs them) and `foreign_benchmark.c`
  provides `benchmark-foreign`.
- `src/buster/compiler/branch_sizing.c` is branch sizing (#144): the rel8 or
  rel32 form of each branch inside a function, chosen before the function is
  encoded, from `x86_encode`'s offsets and rounds over the branches alone
  (`size_branches`, its scalar twin `size_branches_scalar`, and
  `branch_sizing_validate`, the checks the two leave to the code generator, as
  `x86_encode` leaves `x86_validate`'s). `branch_sizing_generate.c` generates
  functions for its tests and benchmark: shaped like clang's -O0 and -O2 code,
  and chains of branches that take a round each. `branch_sizing_test.c` holds
  its tests (`main.c` includes it after `x86_64_encoder_test.c`, whose random
  instructions and printer it takes; `./build/compiler test-branch-sizing`
  runs them) and `branch_sizing_benchmark.c` provides
  `benchmark-branch-sizing`. `docs/branch-sizing.md` explains its design and
  measurements.
- `src/buster/compiler/x86_semantics.c` is what the forms a code generator
  emits mean (#221), in tables keyed by `X86Form` beside the encoder's:
  - each covered form's effects (`x86_effects`): its operands' reads and
    writes by encoding role (tied ones both), its fixed registers, what a
    narrower write does to the rest of a register, its flags read, defined
    and left undefined, its memory, control, faults and MXCSR, and how its
    immediate's bits become a value. An uncovered form gets a conservative
    record, and a call has only its architectural effects (its callee's
    clobbers are #137's);
  - the immediate's contract (`x86_immediate_encodes`, `x86_immediate_field`
    and `x86_immediate_value`): a selector's value against a field's bits;
  - each record's resources read and written, its operands resolved
    (`x86_instructions_effects`, AVX-512, and its scalar twin);
  - value-preserving form choice for an operation with an immediate
    (`x86_choice_row`, `x86_choose_forms`, AVX-512, its scalar twin
    `x86_choose_forms_scalar`, and `x86_choice_instruction`): the
    sign-extended imm8, the accumulator's form, constant materialization, and
    other instructions (xor, test r, r, a narrower test, opt-in inc and dec)
    only where the live flags allow, which initialization works out from the
    effects.

  `x86_semantics_test.c` holds its tests (`main.c` includes it after the
  assembler's, taking the encoder tests' printer and branch sizing tests'
  runner; `./build/compiler test-x86-semantics` runs them) and
  `x86_semantics_benchmark.c` provides `benchmark-x86-semantics`.
  `docs/x86-semantics.md` has the contract, the covered subset, how to add a
  form and the measurements.
- `src/buster/compiler/c_types.c` is the C type table (#132, contract 5 for
  #133 and #137): `TypeId`s, hash-consed one at a time and in batches, tags,
  clang's record layout (one record at a time, and 16 at once in vectors), the
  conversions as `vpermb` tables 64 pairs at a time, compatibility and
  composite types, and clang's spelling of a type, all as clang 23 has them on
  x86-64. `c_types_test.c` holds its tests (`main.c` includes both;
  `./build/compiler test-types` runs them) and `c_types_benchmark.c` provides
  `benchmark-types`. `docs/c-types.md` records the design and its 9700X
  measurements.
- `src/buster/compiler/calling_convention.c` is the System V AMD64 calling
  convention (#137, contract 3 for #136): every `TypeId`'s classes as clang 23
  classifies them, a row of 16 offsets per type, records and arrays folded
  four at a time in vectors with an exact twin; arguments' and results'
  registers and stack slots as clang and LLVM's `CC_X86_64_C` give them, by
  prefix sums 16 arguments at a time with an exact twin; and `va_arg`'s plan.
  `calling_convention_test.c` holds its tests (`main.c` includes both;
  `./build/compiler test-calling-convention` runs them) and
  `calling_convention_benchmark.c` provides `benchmark-calling-convention`.
  `docs/calling-convention.md` records the design, where clang's LLVM types
  and GCC decide otherwise than the classes, and its 9700X measurements.
- `src/buster/compiler/call_graph.c` is the call graph check, the coding
  style's rule against recursion checked mechanically: `check-call-graph`
  reads the LLVM IR of the translation units `build.sh` compiles, builds their
  call graph and reports every cycle (Tarjan's algorithm, iterative) that isn't
  in its allowlist, `call_graph_allowed`. `./build.sh check-call-graph` writes
  that IR to a temporary directory and runs it. `call_graph_test.c` holds its
  tests (`main.c` includes both; `./build/compiler test-call-graph` runs them),
  and `call_graph_capture/` the C fixture that checks the IR capture itself.
- `src/buster/compiler/c_intern.c` is the identifier intern table (an
  `IdentId` per distinct spelling) and the predefined names that get the first
  ids (keywords, directive names, the preprocessor's names), and
  `src/buster/compiler/c_refine.c` is token refinement: per file, after the
  lexer, each identifier's `IdentId` (in place of its end) and each token's
  exact leading-space bit. `docs/c-refinement.md` explains the design and its
  9700X measurements. `c_refine_test.c` holds their tests (`main.c` includes
  it; `./build/compiler test-refinement` runs them) and `c_refine_benchmark.c`
  provides `benchmark-refinement`.
- `src/buster/compiler/c_preprocessor.c` is the preprocessor: one translation
  unit's token stream from its files' refined tokens (C23's `#embed` among its
  directives), and `./build/compiler -E` (as `clang -E`, with `-dM` and `--embed-dir=`).
  `src/buster/include` holds the compiler's own freestanding headers, which
  `--buster-headers` puts where clang's resource directory goes (the default),
  with clang's after them for the headers they don't supply (never for a name
  they have, so their `#include_next` reaches the C library's), and
  `-nobuiltininc` leaves out the compiler's headers, whichever they are.
  `docs/c-preprocessor.md` explains the design, the `clang -E`
  oracle and the 9700X measurements. `c_preprocessor_test.c` holds its tests
  (`main.c` includes it; `./build/compiler test-preprocessor` runs them),
  `c_preprocessor_fuzz.c` its differential fuzzing (#109; `main.c` includes
  it after the tests) and `c_preprocessor_benchmark.c` provides
  `benchmark-preprocessor`. Its `FileCache` holds what is prepared from each
  file (by identity) and every path looked up, for every translation unit that
  preprocesses with it (`PPOptions.files`; a unit without one makes its own).
  `c_predefines.c` gives the predefined macros the target and the options decide,
  as clang does (`-march`, the `-m` feature flags, `-march=native` from CPUID,
  `-O`, `-f...`, `-g`), from tables generated from clang 23; `c_predefines_test.c`
  checks them against clang under `test-preprocessor --clang` and regenerates the
  tables (`test-preprocessor --generate-targets`). Of the `-f` options that
  change clang's keywords, lexing or `-E` output (#281), `-E` models GNU's
  keywords (`-f[no-]gnu-keywords`, `-f[no-]asm`), `$` in identifiers, and char's
  signedness and `wchar_t`'s width in `#if` (from `c_predefines.c`'s options),
  and refuses the others where their last option turns them on
  (`pp_take_family`, `pp_settle_families`).
- `src/buster/compiler/c_preprocessor_history.c`, included by `c_preprocessor.c`,
  records macro expansion history in cold records and compressed output runs;
  `pp_diagnose_output` renders a diagnostic for an output-token index.
  `c_preprocessor_history_test.c` is part of `test-preprocessor`, including its
  Clang diagnostic comparisons. `benchmark-preprocessor --history-cost`
  compares recording on/off and reports retained and temporary history bytes.
- `src/buster/compiler/c_files.c` is #150's per-file jobs: files prepared on a
  group of lanes into a `FileCache`, each lane interning into a table of its
  own that a merge (rule 4's exchange) gives the shared table's ids in the
  order of first occurrences, so that they don't depend on the lane count; and
  the run-ahead, a frontier walk over the files' literal includes by the
  preprocessor's own search. `docs/c-files.md` has the design and the 9700X
  measurements. `c_files_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-files` runs them) and `c_files_benchmark.c` provides
  `benchmark-files`.
- `src/buster/compiler/c_parser.c` is the parser (#127): the preprocessor's
  token stream to a tree, a parent, a role and a node depth for every token,
  and for a unit without errors its postorder with each subtree's size. The
  vector path builds it with kernels in AVX-512, each with an exact scalar
  twin, and a pass of checks that flags what C's grammar doesn't allow, whose
  top-level declarations go to the scalar parser; the scalar parser alone
  (`--scalar`) builds the same tree and gives the diagnostics, at clang's
  places with clang's messages. It also holds the driver. `docs/c-parser.md`
  explains the design and its 9700X measurements. `c_parser_test.c` holds its
  tests (`main.c` includes it; `./build/compiler test-parser` runs them) and
  `c_parser_benchmark.c` provides `benchmark-parser`.
- `src/buster/compiler/profile.c` is the profiling tools: `ab` (builds of
  several trees run in turn, under the 9700X's lock) and the reports of perf's
  and objdump's text, `hot-loops`, `label-spills`, `spill-report` and
  `branch-misses`. `profile_test.c` holds their tests (`main.c` includes both;
  `./build/compiler test-profile` runs them). `docs/profiling.md` is how the
  lexer and the encoder were measured and profiled on the 9700X, with these
  tools: read it before tuning a stage.
- `src/buster/compiler/parallel.c` is the runner the 9700X job runs its tests
  through, `parallel`: a list of commands at once, each one's output printed
  in the list's order. `parallel_test.c` holds its tests (`main.c` includes
  both; `./build/compiler test-parallel` runs them); "The parallel runner"
  below describes both.
- `build/` is disposable output. Do not commit generated binaries or captures.

From the repository root, on the approved Linux Ryzen 7 9700X host:

```sh
./build.sh
clang -dM -E -march=native -fwrapv -funsigned-char -x c /dev/null > build/predefines.h
./build/compiler -fsyntax-only -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c
./build/compiler test-call-graph
./build.sh check-call-graph
./build/compiler test-profile
./build/compiler test-parallel
./build/compiler test-lexer
./build/compiler test-lexer --cplusplus
./build/compiler test-lexer --no-digraphs --no-line-comments
./build/compiler benchmark-lexer src
./build/compiler test-preprocessor-fuzz --std c17
./build/compiler test-preprocessor-fuzz --std c23
./build/compiler benchmark-preprocessor-fuzz
./build/compiler test-x86-encoder
./build/compiler benchmark-x86-encoder
./build/compiler test-branch-sizing --cross-check
./build/compiler benchmark-branch-sizing
./build/compiler test-x86-semantics --cross-check
./build/compiler benchmark-x86-semantics
./build/compiler test-lanes
./build/compiler benchmark-lanes
./build/compiler test-dataflow
./build/compiler benchmark-dataflow
./build/compiler test-link
./build/compiler test-object-writer --cross-check
./build/compiler benchmark-object-writer
./build/compiler test-import --cross-check
./build/compiler benchmark-import
./build/compiler test-bind --cross-check
./build/compiler benchmark-bind
./build/compiler test-executable
./build/compiler test-dynamic --cross-check
./build/compiler test-static --cross-check
./build/compiler benchmark-executable
./build/compiler test-snapshot
./build/compiler benchmark-snapshot
./build/compiler benchmark-link
./build/compiler benchmark-floors
./build/compiler test-foreign --cross-check
./build/compiler benchmark-foreign --linkers
./build/compiler test-refinement src
./build/compiler benchmark-refinement src
./build/compiler test-preprocessor --clang
./build/compiler test-preprocessor --tables src
./build/compiler benchmark-preprocessor --tables src
./build/compiler -E -march=native -Isrc -fwrapv -funsigned-char -o build/main.i src/buster/compiler/main.c
./build/compiler benchmark-preprocessor --clang -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
./build/compiler benchmark-preprocessor --embed 16777216
./build/compiler test-files
./build/compiler test-files --flat src
./build/compiler test-files -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c src/buster/compiler/*_benchmark.c
./build/compiler benchmark-files -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c src/buster/compiler/*_benchmark.c
./build/compiler test-parser --clang
./build/compiler test-parser --clang -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c
./build/compiler benchmark-parser -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c
./build/compiler test-types
./build/compiler benchmark-types
./build/compiler test-calling-convention --cross-check
./build/compiler benchmark-calling-convention
```

The compiler's sources need `-march=native`'s macros (`c_lexer.c` stops with
`#error` without AVX-512's), which the preprocessor's predefines give as clang's
do for the same options (`c_predefines.c`).

## Sharing the 9700X

Agent sessions and CI share the 9700X: the buster14a org's self-hosted runner
runs on the same machine. Its timings only mean something when one thing runs
there at a time, so everything takes one lock:

- Run every build, test, benchmark and perf run there through
  `EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh COMMAND...`.
  Also set `EXCLUSIVE_TITLE` to the session's title and `EXCLUSIVE_LINK` to
  its link (its claude.ai page, or else the PR or issue it works on): whoever
  waits sees them in `/var/lib/buster-9700x/owner`.
- The runner's job hooks take the same lock for every CI job, and CI goes
  first: `exclusive.sh` doesn't take the lock while a CI job waits for it or
  for 20 s after one ends (the runner's next job starts waiting about 8 s
  later), and gives it back if it got it then. While CI jobs queue, a session's
  run waits for all of them. A CI job's owner lines give its workflow, job, PR
  number and title (or a push's first line), the run's link and the PR's.
- Hold the lock per run, never around a series of runs: a CI job waits for it
  and times out after 10 minutes.
- `~/buster-fast-bench/README-9700x.md` on the host describes the setup and how
  to undo it.

## Agent memory

Agents keep what they learn about this project in the owner's private
`davidgmbb/llm-memory` repository, cloned at `~/llm-memory` on each of the
owner's computers:

- Claude Code's memory folder for this project is a symlink to
  `~/llm-memory/projects/buster-fast/`, so Claude saves its memories there as
  usual.
- Any other agent reads `~/llm-memory/projects/buster-fast/MEMORY.md` before
  starting and saves what later sessions will need there, in the same format:
  one fact per file, indexed in `MEMORY.md`. `~/llm-memory/AGENTS.md` has the
  rules, and Codex's automatic memories are copied to `~/llm-memory/codex/`.
- Don't save what this repository already records, and never save secrets.
- After saving, run `~/llm-memory/bin/llm-memory sync` to share the memory with
  the owner's other computers.

Where `~/llm-memory` doesn't exist, skip this.

## Call graph check

The coding style forbids recursion over input-dependent structures, and
`./build.sh check-call-graph` checks it on every function of the compiler. It
writes the LLVM IR of `build.sh`'s translation units, with the same flags and
optimization level, `-O3`, but `-Xclang -disable-llvm-passes` (no pass, not
even the always-inliner, so a call in the IR is a call in the source, while
the IR holds every body the build may inline: at `-O0`, clang doesn't emit an
`extern inline` body at all) into a temporary directory, and runs
`./build/compiler check-call-graph FILE.ll...` on them (so it needs a build).
First it captures `src/buster/compiler/call_graph_capture` the same way, a
cycle through an `extern inline` body beside a strong definition in another
unit, and fails unless the check reports it.
The command reads each `define` and the direct `call`, `invoke` and `callbr`
instructions in its body; a callee is its translation unit's function or alias
of that name first (static functions are per unit: the benchmarks' copies of
`lane.c` are separate functions), then the external symbol's strong
definitions, or all its weak ones when it has none (the linker takes one of
them by link order), and every definition of the name in the calling unit,
which clang may inline into the call whatever the linker takes (an
`available_externally` or `*_odr` body, `always_inline`, `flatten`). A
replaced weak body that clang wouldn't inline then counts too: that can only
add a cycle, never hide one; an alias, through any chain of them, is its target's
definition in its own module, whatever replaces that name elsewhere. A second strong definition of a name fails the check. Calls of functions defined
nowhere (libc, intrinsics) and calls through pointers, which the style allows
only at the thread-entry boundary, are left out; a call of an ifunc or of an
alias that leads to no definition fails the check.

It reports each strongly connected component of more than one function and
each function that calls itself, with the shortest call path through the
component's first function, as `unit.ll:function -> ... -> unit.ll:function`,
then a summary line, and exits with status 1 on a cycle not in
`call_graph_allowed`, on an allowlist entry that matches no cycle, and on IR it
can't read. An entry names a component's functions, each as many times as
the component has a function of that name; add one only for the
style's "trivial statically bounded recursion", with a comment saying what
bounds its depth. The list has three entries, the preprocessor fuzz generator's
recursions (conditionals in groups, expressions, invocations in arguments),
each bounded by its depth argument. The 9700X job runs
`test-call-graph` and the check after the build; the check, which captures the
units' IR at once, takes about 2 s.

`./build/compiler test-call-graph` (about 0.4 s) checks the report, exactly,
on IR of the call forms clang writes and of what must be left out, a
self-call, a two-function cycle, a cycle with a chord (whose path must be the
shortest), a cycle through three translation units beside static and external
functions of the same name, clang's IR of recursion through aliases and of a
call of an ifunc, aliases that lead nowhere, clang's IR of weak and strong
functions and aliases of one name, of an alias of a replaced weak function, and
of `available_externally`, `always_inline` and `flatten` inlining past a
strong definition elsewhere, in both orders, replaced weak bodies, two units' copies of a cycle
allowed by one entry, a cycle grown past its entry with an unused entry, a
cycle through two units' static functions of the same name that entries naming
it once mustn't match, malformed IR, and a chain of a million functions and a
ring of 100,000, deeper than a recursive walk's stack.

## Lexer tests

`./build/compiler test-lexer` needs no corpus. Its checks are ports of the
snippet and random-input checks of `c/c_tokenizer_test.c` in
Accelerated-Zig-Parser, where the lexer comes from. They compare `c_tokenize`
with the scalar lexer token by token, each input twice: with C23's digit
separators, and without them (`C_TOKENIZER_NO_DIGIT_SEPARATORS`, as C17 lexes
numbers, which the preprocessor asks for before C23). Each check prints the
second's counts after the first's. The inputs:

- the boundary snippet after 0 to 1,099 bytes of padding at each of the 64
  misalignments from a cache line: 70,400 placements, none of which may need
  the scalar lexer;
- 200,000 random inputs made of weighted C pieces. `--fuzz N` sets how many,
  and `--seed N` picks another sequence (the same inputs as upstream's
  `--seed N`). The first inputs that differ are saved as `fuzz_failure_N.c` in
  the current directory (`fuzz_failure_no_separators_N.c` without
  separators);
- page edges: every length from 0 to 1,099 bytes, cut as a prefix and as a
  suffix from the repeated snippet, four strings of random inputs (which
  continue the random sequence) and three comment templates, placed to end
  exactly at the end of a readable page and to start exactly at the start of
  one, between `PROT_NONE` guard pages: 35,200 inputs. These also compare
  `c_tokenize_vectorized`, and a read outside the source faults; the command
  then names the input and exits with status 1. Length 0 at a page end points
  at the guard page itself. The chunk loop loads aligned 64-byte lanes, so
  only a lane wholly outside the source can fault; an over-read within the
  source's last lane stays on a readable page and is not caught. The
  leading-comment scan's loads are unaligned and masked to the source, so the
  guard pages check them fully;
- 7,040 comment cases across every source misalignment: closers around the
  leading-comment scan's 64-byte loads and around 512-byte chunk boundaries,
  including closers whose `*` ends a chunk (also a chunk wholly inside the
  comment), a spliced closer, a `/*/` whose `*/` ends the comment, and a
  comment inside a preprocessing directive;
- numbers: 17 pp-numbers (every kind of digit, prefix and suffix, a `.` and
  exponents, and numbers that have separators already) with a `'` at every
  place in them, each before 18 followers (the end of the source, what a
  separator may join, quotes, other literals and comments), with no line
  splice around the `'`, one on either side, or `\r\n` ones on both, placed so
  that the `'` is at byte 0 to 2, 62 to 65 or 509 to 514 of a source that
  starts a chunk: 80,946 cases. Without separators, a case without a splice
  must stay on the vectorized path;
- 20 numbers with a `'` (16 in C and 4 in C++) whose tokens must be the ones
  clang 23.1.3's raw lexer gives them (`-std=c17`, `c23`, `c++11` and
  `c++26`), from both lexers.

`--cplusplus` tokenizes everything as C++, with the C++ snippet and pieces.
`--raw-strings` enables C11's raw prefixes in C; `--raw-strings-c99`
enables `R` and `LR`. Raw literals and near misses are checked around
chunk and page boundaries. Both modes run in the 9700X workflow.
`--no-digraphs` and `--no-line-comments` tokenize C without digraphs and without
`//` comments (`C_TOKENIZER_NO_DIGRAPHS` and `C_TOKENIZER_NO_LINE_COMMENTS`), as
the preprocessor lexes `-std=c89` (both), C94 (the second) and `-fno-digraphs`
(the first): the same inputs, lexed otherwise, through every check; C++ takes
neither (#288). The command prints each check's counts and first differences,
and exits with status 1 if anything fails. The 9700X job's test step runs C++,
C and C's three other modes. Before adding the C89 modes, the C and C++
runs took about 2.8 s and 3.0 s on the 9700X. For a longer run:

```sh
./build/compiler test-lexer --cplusplus --seed 7 --fuzz 2000000
```

## Lexer benchmarks

`./build/compiler benchmark-lexer [--cplusplus] [--no-digraphs]
[--no-line-comments] [--no-digit-separators] path...` checks each matching source against the scalar
lexer and reports 21 interleaved-pass median throughput, cycles per byte,
instructions per byte and IPC (hardware counters are zero when unavailable).
Paths can be files or directories; `--cplusplus` selects C++ source/header
extensions, and `--no-digraphs` and `--no-line-comments` C's modes, as
`test-lexer` takes them, and
`--no-digit-separators` lexes numbers as C17 does, as the preprocessor does
before C23 (`docs/c-lexer.md`, "Numbers without digit separators"). The default
9700X job runs
`benchmark-lexer src` after the tests, pinned to one allowed CPU. Measurements
are informational: correctness failures fail CI, but throughput values are not
yet compared against a performance baseline. Keep just one compiler executable.

`./build.sh lexer-variant NAME=SOURCE...` builds `build/compiler` with other
lexers in it, each SOURCE a copy of `c_lexer.c` (or its assembly, from
`./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s`), as
`c_lexer_variant_NAME`. `benchmark-lexer` then also compares each with
`c_tokenize` on every file (a difference fails it), times them in an order
that rotates by pass, and prints each one's change from `c_tokenize`.
Where its code lands moves a variant by up to about 1.5% even when its code
is `c_tokenize`'s (a copy of it as a variant is the comparison's control), and
builds in separate processes differ by about 2%: `docs/profiling.md`
("Comparing two versions") has how to use both.

## Profiling tools

`./build/compiler test-profile` (about 10 ms) checks `profile.c`. Each tool
runs on texts in objdump's, clang's and perf script's formats, and its report
must be exactly the expected one:

- `hot-loops`: a loop nested in another, an indirect jump, two copies of the
  function (the busiest one taken by the samples' addresses: their offsets in
  the file through its LOAD segments), samples of another function and of
  another file, of a name that only starts with the function's, past the last
  instruction and before the first symbol, and an lld executable, whose
  offsets aren't its addresses; samples without file offsets, a disassembly
  without objdump -p's headers, a missing function and disassembly out of
  order (errors);
- `label-spills` and `spill-report`: spills, reloads and folded reloads
  labeled with the tag in order in the function and in no other (not source
  paths or other comments that hold the words), and nothing else changed;
  their samples by stack slot and source location, the labeled copy found by
  address, without the samples of a static function of the same name in
  another unit (after it too), of the function's static local (`f.counter`),
  of another tag, of a shared library at the same offsets or of another
  tree's file of the same name, and in an lld executable; a label at the
  function's address, from the symbol table (`objdump -t`); labels out of order or of another tag, missing from the disassembly
  or in it twice, samples without file offsets, and an invalid tag (errors);
  and clang's assembly of a program whose static function `function` (also
  an assembler keyword) prints `__func__`, labeled, assembled and run, which
  must print the same;
  and one whose first instruction is a spill, labeled, linked and
  disassembled, whose report must find its labels (clang and objdump);
- `branch-misses`: records mispredicted, predicted and neither, fields that
  aren't records, the load bias and the disassembly's instructions, and
  10,000 branches, which grow its table;
- `ab`: the rotating order, and runs that succeed, fail and can't start, with
  two argument lists, and through a lock (`env`) pinned to an allowed CPU.

The commands are `ab [--rounds N] [--cpu N] [--lock PATH] TREE... --
ARGUMENT... [-- ARGUMENT...]...`, `hot-loops FUNCTION DISASSEMBLY SAMPLES
[--loops N]`, `label-spills FUNCTION TAG INPUT.s OUTPUT.s`, `spill-report
FUNCTION LABELED.s DISASSEMBLY SAMPLES [--slots N]` and `branch-misses BRSTACK [--bias
ADDRESS] [--disassembly FILE] [--branches N]`; `docs/profiling.md` has the
perf and objdump commands that make their inputs. Run `ab` itself outside the
9700X's lock, with `--lock ~/buster-fast-bench/exclusive.sh`, which takes it
for each run. The 9700X job's test step runs the tests.

## The parallel runner

`./build/compiler parallel [--jobs N] [--timeout SECONDS] [--github] [LIST]`
runs a list of commands at once. LIST (standard input without one) holds them:
each starts at a heading, a line `== TITLE`, and its script is the lines after
it, up to the next heading; before the first heading there may be blank lines
and comments. Each script runs in a bash of its own, as GitHub Actions runs a
step's `run` (`bash --noprofile --norc -e -o pipefail`), in a process group of
its own, with standard input from `/dev/null`, its standard output and errors
into one capture, and no other descriptor. The commands start in the list's
order, at most `--jobs` at once, unpinned: by default one per core of the CPUs
the process may run on, 8 on the 9700X, whose 15 CPUs online would put two tests
on a core's SMT siblings, which slow each other down, as do the lanes' spinning
waits (the job's tests took 21.3 to 22.4 s with 8 jobs, and 22.9 to 23.3 s with
15, test-lanes 2.3 to 2.9 s of them rather than 0.2). The report gives each
one's heading, with its outcome and time, then its output, in the list's order
as soon as it and every command before it have ended; with `--github`, between
`::group::` and `::endgroup::`, and an `::error::` annotation for each that
failed. Then come a summary line and a line naming each command that failed. The
exit status is 1 if a command failed or couldn't start, a signal stopped the
run, or the report was lost. The report never holds up the run (#276's review):
a command's output is read into memory when it ends and its capture closed, so
that only running commands hold one, and the report goes out only as fast as its
reader takes it, at most `PIPE_BUF` bytes at a time into a pipe or a terminal
once `poll` says it has room, while the runner keeps starting commands,
enforcing their timeouts and taking signals, all from one `poll` on a
`signalfd`. Whatever a command's shell leaves running in its group is killed
when the shell ends, and a command still running `--timeout` seconds after it
started is killed with its group and fails, though its shell may end before the
runner reaps it. SIGINT, SIGTERM and SIGHUP stop the run: the signal goes on to
every running command's group (a second one sends SIGKILL), nothing more starts,
and the report gives what every command wrote; once every command has ended, one
drops the rest of the report rather than wait for its reader. A report that
can't be written (EPIPE: `parallel | head`, whether or not SIGPIPE is ignored)
stops the run, and its commands are killed.

The 9700X job's test step ("Run the tests at once") runs every test through it,
with `--timeout 180`, before the benchmarks, which run one at a time and pinned.
A test goes in its list as a heading and the step's commands, the longest
first, so that they start first; one that needs the machine to itself doesn't
belong there. On the 9700X its tests took 52.1 s one after another, as the
job's steps ran them, and take 21.8 s through the runner, the time of the
longest, `test-calling-convention --cross-check` (18.1 s alone), whose six
configurations run in turn. To run the list as the job does (on the 9700X,
under its lock, as one run):

```sh
sed -n "/<<'TESTS'$/,/^ *TESTS$/{//!{s/^ \{10\}//;p}}" .github/workflows/9700x.yml | ./build/compiler parallel
```

`./build/compiler test-parallel` (about 7 s) checks it, each report exactly
as expected with its times written as `*`:

- lists: headings, scripts, blank lines and comments before the first heading,
  a heading without its space, a last line without a newline and `==` inside a
  line; then each error at its line: a line before the first heading, a heading
  without a title, a command without a script (before another heading and at
  the end), a title that repeats, a NUL byte, and no commands;
- order: commands that end out of order, printed in the list's order, each with
  its standard output and errors in the order it wrote them, output that
  doesn't end in a newline and none at all, an exit status and a signal; then
  `--github`'s groups and annotations, with a title GitHub must read escaped;
  and a command's descriptors, without `/proc` (whose PID namespace may not be
  the shell's): standard input reads nothing though the runner's is a file
  with a line in it, and no other than the three is open (an external `true`
  redirected to each from 3 to 63), though the runner has one open without
  `FD_CLOEXEC`;
- at once: three commands that each wait for the other two pass with 3 jobs;
  with one job, four commands' starts and ends in a log come in turn; and eight
  with 3 jobs are never more than 3 at once, by the runner's times;
- output: 1 MB from one command, past a pipe's 64 KB;
- processes: a command past its timeout killed with the process it started,
  what a command leaves running killed when its shell ends, and a shell that
  can't start;
- a slow reader (#276's review): a report whose pipe its reader leaves full for
  1 s while a command passes its timeout, which must be killed at its deadline
  and fail, and while a command sends SIGTERM, which must go on to it at once;
  SIGTERM once every command has ended, which must drop the rest of the report
  rather than wait for the reader; and 61 commands with room for 32
  descriptors, 60 of them ending before the first, whose captures must close
  as they end;
- signals: SIGTERM from a command stops the run, goes on to the command's
  group, and what didn't start is reported so; a run stopped while its command
  passes fails; SIGINT then SIGTERM, from a command that ignores both, kill it;
  SIGHUP while ignored (as under nohup) doesn't stop the run, and SIGCHLD
  ignored doesn't keep the runner from its commands; each run leaves the signal
  mask and the dispositions as they were;
- the command: its report and exit status for lists on standard input and in a
  file that pass and fail, a report with no reader (the run stops, and the
  runner ends at once with its command killed), with SIGPIPE at its default and
  ignored, a reader that leaves after the first line (the run fails though its
  command passes), a command that ends past its deadline while the runner is
  stopped (SIGSTOP), which must fail, a list's error, a list that can't be
  read, and invalid options;
- the cores: `--jobs`' default against the distinct physical and core ids that
  `/proc/cpuinfo` gives the CPUs the process may use.

The runner is test tooling outside the compile path and has no vector path: a
list is a few dozen commands, and the time goes into them. The 9700X job's
test step runs its tests too.

## x86-64 disassembler

`src/buster/compiler/x86_64_disassembler.c` decodes bounded raw-byte slices into
semantic records with actual encoded lengths. Its tests and raw-binary command
adapter are included by `main.c`; the benchmark is a separate translation unit
in the same executable. [The disassembler notes](docs/x86-64-disassembler.md)
define the supported 64-bit catalogue subset and prefix restrictions.

On the approved host, with the usual per-run lock:

```sh
./build/compiler test-x86-disassembler --objdump
./build/compiler disassemble input.text
./build/compiler benchmark-x86-disassembler
./build/compiler benchmark-x86-disassembler --vector --after-scalar 200
```

The independent GNU tools check instruction boundaries and reassembled operands.
Keep hand-written semantic fixtures and guarded tails: scalar/vector equality
and re-encoding through the shared catalogue do not constitute an independent
oracle. Benchmarks separate decode from decode plus formatting and include
`--file raw.text` and `--only scalar|compact|avx512|batched`. `batched` is the
default API path; `compact` isolates validation specialization with scalar
matching. `--iterations N` and `--after-scalar MS` distinguish short bursts
after scalar work from sustained runs.

## x86-64 encoder tests

`./build/compiler test-x86-encoder` (about 1.8 s) checks that every form fits a
template (and that building each, in every mode, at a `PROT_NONE` page writes
nothing past it), both encoders on 761 known encodings from GNU as (each also
with the fixup flag, whose end they must report), a digest of the scalar
encoder's output for 100,000 random instructions, every batch width and variant
of `x86_encode` against the scalar encoder (bytes, offsets and fixup ends, in
each of the four loops: with neither, either or both of offsets and fixup ends)
on every count from 0 to 199 and on 300,000 random instructions of every form
(legacy with x87 and MMX, VEX and EVEX, with opmasks, broadcasts, rounding,
registers 16-31, gathers and scatters, FS and GS segment overrides, and one in
eight marked with the fixup flag), the same between `PROT_NONE` guard pages, and
that `x86_validate` finds every invalid operand and the scalar encoder rejects
it (memory operands in the 232 FS and GS forms without room for the override
among them). `x86_encode` checks nothing: it takes what `x86_validate` accepts,
as the code generator makes it (an immediate or rel8 outside its field's range,
for one, gets its low bytes). Then it runs those checks again with 32-bit
addresses, but the known fixups: 34 known encodings, a digest of their own,
`x86_encode_16_address32` and `x86_encode_64_address32` against the scalar
encoder on the random streams and at page edges, and invalid operands, among
them MPX memory operands and memory operands in the 312 FS and GS forms and the
116 forms without room for the `67`. And again for APX, but the known fixups:
153 known encodings, 31 known invalid instructions (as GNU as rejects them), a
digest of their own, `x86_encode_16_apx` and `x86_encode_64_apx` against the
scalar encoder on random streams in which half the forms have REX2 and a quarter
are EVEX ones, and at page edges, and invalid operands, among them a high byte
register with REX2, registers 16 to 31 in forms without REX2 or EVEX and XMM16
to XMM31 in SSE's forms. And in 32-bit and 16-bit modes: 159 and 150 known
encodings, 13 and 16 known fixups, a digest of each, the modes' loops at 16 and
64 instructions per iteration (`x86_encode_16_mode32` and the rest) against the
scalar encoders on random streams with both address sizes and segment overrides
in the decorations, and at page edges, instructions longer than 15 bytes (16-bit
mode has them), which `x86_validate_16` must reject, and invalid operands, among
them registers that would need REX, 16-bit addresses that aren't ModRM's, forms
of 64-bit mode alone, instructions past 15 bytes and segment overrides where an
instruction takes none. `--address32`, `--apx`, `--mode32` and `--mode16` run
only their pass, and `--count N` and `--seed N` change the random streams.

The digests were recorded when GNU as encoded the same streams to the same
bytes. After changing the form table, the scalar encoder or the fixup interface,
cross-check with GNU as (binutils 2.47 for the recorded digests) for every entry
point, then record the digests the test prints:

```sh
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 > build/x86.s
as build/x86.s -o build/x86.o
./build/compiler test-x86-encoder --compare-object build/x86.o 400000 --seed 1
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --address32 > build/x86_32.s
as build/x86_32.s -o build/x86_32.o
./build/compiler test-x86-encoder --compare-object build/x86_32.o 400000 --seed 1 --address32
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --apx > build/x86_apx.s
as build/x86_apx.s -o build/x86_apx.o
./build/compiler test-x86-encoder --compare-object build/x86_apx.o 400000 --seed 1 --apx
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --mode32 > build/x86_mode32.s
as build/x86_mode32.s -o build/x86_mode32.o
./build/compiler test-x86-encoder --compare-object build/x86_mode32.o 400000 --seed 1 --mode32
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --mode16 > build/x86_mode16.s
as build/x86_mode16.s -o build/x86_mode16.o
./build/compiler test-x86-encoder --compare-object build/x86_mode16.o 400000 --seed 1 --mode16
```

(`build.sh` deletes `build/`, so run these after building.) The printed assembly
uses only operands GNU as encodes with the same form (`{load}`/`{store}`,
`{vex}` and `{evex}`, immediates outside the imm8 range for imm32 forms, `{nf}`
for APX's NF forms, `{dfv=...}` for `ccmp`'s and `ctest`'s default flags value,
and so on), so any difference is a bug in one of them. A quarter of the fields
that can be symbolic (32-bit displacements, rel32s, `loop`'s and `jrcxz`'s
rel8s, imm32s and 64-bit immediates) are printed as symbols plus their values;
GNU as writes zeros and a relocation for them, and `--compare-object` reads
`.text`, `.lengths` and `.rela.text` from its object and compares the bytes
(with those fields zeroed where the reported fixup ends put them) and each
relocation's offset, type, addend and symbol with the ones the fixup ends and
`x86_fixup_distance` give, and the mode's vectorized encoder's bytes and fixup
ends with the scalar encoder's. With `--address32`, the printer adds `addr32`
where no general register gives the address size, and the relocations of
displacements other than RIP-relative ones are R_X86_64_32; with `--apx`, it
names registers 16 to 31 `r16` to `r31` (`r16d`, `r16w`, `r16b`); and with
`--mode32` and `--mode16`, the printed assembly is in `.code32` or `.code16`,
and the printer writes the mode's mnemonics and prefixes (`addr16`, `data32`,
`jcxz`, `lcall`, segment overrides as prefixes, which GNU as keeps on a default
segment, and `.arch i386` around a test register's move), whose relocations are
never PLT32 or 32S. Cross-check more than one seed after changing forms (1.7
million instructions over seeds 1, 7 and 99 were last checked for each 64-bit
entry point, with 198,573, 197,926 and 197,735 relocations, and 1.2 million for
each of 32-bit and 16-bit mode, with 135,563 and 136,322).

## x86-64 encoder benchmark

`./build/compiler benchmark-x86-encoder [--count N] [--section N] [--uniform |
--vector] [--mode32 | --mode16] [--offsets] [--fixups] [--after-scalar MS]
[--only NAME]` checks every implementation against the scalar loop, then
reports 21 interleaved-pass medians per encoded instruction (time, cycles,
instructions, IPC, and the throttle: PMCx1A0's undocumented umask 0x08, raw
event `0x1000008a0`, which #199 reads as the dispatch slots Zen 5's power cap
takes). The stream is
a compiler-like mix of 16,384 legacy instructions by default (comparable with
older measurements), a mix like vectorized code (AVX-512, AVX2, BMI) with
`--vector`, or random instructions of every form with `--uniform`; `--section N`
encodes it N instructions per call, `--offsets` stores every instruction's
offset, and `--fixups` marks the calls and RIP-relative operands and reports
their ends. The variants are the batch widths (`x86_encode_4` to
`x86_encode_64`, instructions per loop iteration), the template gather, output
store and fixup store alternatives that lost, `x86_encode_64_offsets_then_pick`,
the other way to have fixup ends (every offset, then the caller picks the marked
ones), and `x86_encode_64_apx` and `x86_encode_16_apx`, APX's loops (the streams
have no registers 16 to 31, so their bytes are the same). `--after-scalar MS`
runs the scalar loop (256 instructions per call) for MS ms before each timed
pass of the other implementations: the state a compiler runs its vector phases
in. Lighter code lets the 9700X's clock boost over 30 to 300 ms, and at that
clock a heavy AVX-512 loop runs under the core's power cap for 5 to 20 ms
([#199's report](https://github.com/buster14a/buster/issues/199#issuecomment-6297314071)
has the controls), longer than a pass. Use 100 to 300: from a settled clock
30 ms barely boosts it, and 100 ms reaches the state whatever the passes before
did. Compare
time as well as cycles, since the clock differs by 2% to 5% between the states.
Measure the back-to-back state with `--only NAME`, which runs one loop alone:
in full runs without the option, the loops after the scalar loop's own passes
(54 ms of the compiler mix) run partly throttled, the more so the nearer they
follow it. The entry points use 16 per iteration: after scalar code it is 0.3%
to 7% faster than 64 on every stream, while back to back 64 is up to 6% faster
on some (`docs/x86-64-encoder.md`'s "Zen 5's costs" and "Both states" have the
numbers). `--mode32` and `--mode16`
time the mode's loops at 64 and 16 per iteration (`x86_encode_64_mode32` and
`x86_encode_16_mode32`, or the 16-bit ones) and its scalar loop instead, on a
mix like 32-bit compiler output or like real-mode code (or with `--uniform`, the
mode's forms). Decide them, and any other performance question, with
measurements on the 9700X (not Zen 4, and not llvm-mca, whose predictions are
well off), using uops.info's Zen 5 port data to explain them;
`docs/x86-64-encoder.md` has the current numbers. Clang's code for the loop
changes a lot with small source changes, so compare every width after changing
the encoder, and profile which loop the samples land in. The default 9700X job
runs it after the tests, pinned to one allowed CPU; like the lexer's, its
measurements are informational.

## Branch sizing tests

`./build/compiler test-branch-sizing [--seed N] [--count N] [--cross-check]
[--keep]` (about 1.4 s on the 9700X, and 2.5 s with `--cross-check`) checks
`branch_sizing.c`. Every function it sizes must be one `branch_sizing_validate`
accepts, as the code generator's are, and both paths must give identical
results: the records, the offsets, the length, the rounds, the long branches
and the worklist's evaluations. Each result must also be the test's reference's
(relaxation over the records, as an assembler does it, with each record's
length from `x86_encode_scalar`), pass `x86_validate`, have
`x86_encode_scalar`'s offsets and length, aim every branch at its label's
record, and be long exactly where a rel8 can't reach:

- forms: every form number as a branch's record, with and without a segment
  override or the fixup flag: `branch_sizing_validate` accepts `JMP_REL8` and
  the 16 `Jcc_REL8` (found from the form table's opcodes), which grow by 3 and
  4 to the rel32 forms after them, and refuses every other;
- errors, `branch_sizing_validate`'s: a branch's index equal to the one
  before, below it, at the count and past it, a target past the count, a form
  that isn't a branch's, a branch's form with FS, GS or the fixup flag, at
  every place among 40 branches (the first error is the one reported), more
  branches than records, more records than `x86_encode` takes (which both
  paths refuse too), and a record `x86_validate` rejects, after a branch's
  error;
- boundaries: a probe branch, forward and backward, jmp and jcc, at every
  displacement from 100 to 135 and from −140 to −105 with every branch short,
  over 0 to 3 branches that grow or don't: it must end long exactly when its
  displacement with theirs is out of rel8's range;
- random functions (`--count N`, 1,500 by default) of up to 100,000 records,
  a branch every 1 to 12, at uniform and compiler-like distances, with
  `branch_sizing_generate.c`'s filler or random instructions of every form;
  also with the worklist from rounds 1, 2 and 3 and with no round limit,
  which must give the same records and offsets;
- chains (`branch_sizing_chain`) of 1 to 1,000 links, forward and backward,
  which take a round per link: up to `BUSTER_BRANCH_SIZING_ROUNDS`, then the
  worklist;
- page edges: the records, the branch columns and the offsets each ending at a
  `PROT_NONE` page, for every count up to 70 records at every branch density
  and every branch count up to 40: a read past an end faults, and the command
  then names the case and exits with status 1;
- memory: a function sized with no scratch memory to spare, then 64 bytes
  more at a time until it succeeds: every failure must be
  `BRANCH_SIZING_ERROR_MEMORY` and leave the records and offsets as they were.

`--cross-check`, which needs GNU as, prints 300 random functions, the
boundaries' probes and the chains as GNU as source, each in a section of its
own with a label at every target, and GNU as's relaxation of them (every
branch short at first, then growing the ones out of range) must give the same
bytes as `x86_encode_scalar` of the sized records; an object that can't be read,
or a function left uncompared, fails the check. The files go in a directory
under `$TMPDIR` (or `/tmp`), removed unless `--keep` or a failure. `--seed N`
changes the generated functions. The 9700X job's test step runs it with
`--cross-check`.

## Branch sizing benchmark

`./build/compiler benchmark-branch-sizing [--seed N] [--rounds N] [--only ROW]
[--measure NAME [--samples N]]` (about 3 s) sizes pools of about 262,000
records of generated functions per row: functions of 40, 250, 2,000 and 10,000
records shaped like clang's -O0 code (tier 0's) and -O2 code
(`docs/branch-sizing.md` has the statistics they follow), a mix of the function
sizes of sqlite3 at -O0, and chains of 1,000 links forward and backward. After
checking that every way of sizing them gives the same records and offsets, it
reports medians of 21 samples, in cycles per branch and per record with IPC, of
`x86_encode` of each function (the caller's encode after sizing, the
reference), `size_branches` and `size_branches_scalar`, and each path's phases
as the difference between runs that stop after a phase and before it: the
vector path, the scalar path, and the vector path with each alternative to its
gathers measured against it (`compress` and `window`). A sample sizes the pool
in groups of at least 2,048 records, each group's records read into cache and
its branches' records put back outside the time (as generated, or as sized for
the reference encode: every whole run must come to the sized functions'
lengths), around rdpmc reads of this thread's cycles and instructions; the
pools are larger than the branch predictor learns (a pool of 33,000 records
sized again and again let it learn the scalar path's branches). `--rounds N`
sizes with another round limit, `--only ROW` runs one row, and `--measure NAME`
one measurement (an entry point's name, or a path's run through a phase, as
`vector-setup`) for `--samples N` samples, for perf. Run it under the 9700X's
lock with `taskset -c 2`. `docs/branch-sizing.md` has the numbers. The default
9700X job runs it after the tests, pinned to one allowed CPU; like the others,
its measurements are informational.

## x86 semantics tests

`./build/compiler test-x86-semantics [--seed N] [--runs N] [--cross-check]
[--keep]` (about 0.2 s on the 9700X) checks `x86_semantics.c`. The processor is
the oracle: a harness runs records in the test's process, loading every
general register, the flags and ZMM0 to ZMM15 from a state, then storing them
back. Memory operands are RIP-relative into a data page, RSP is its own stack,
and a fault names the record. The checks:

- the effects table: every covered form but those that branch or trap, from
  `--runs` random states (12), with register and memory operands in turn.
  Each run checks that:
  - what the form doesn't write stays as it was: registers, vector registers
    to bit 511, flags, and the memory around its operand and below the stack;
  - a narrower write keeps or zeroes the rest as its record says;
  - defined flags and results don't change when everything it doesn't read
    changes, every register compared whole (vector ones to bit 511);
  - a destination that only merges doesn't read the bytes it defines;
  - every write shows a change in some run: one that no run showed gets up
    to 256 more runs, with register operands, and one that never shows is
    listed as missing coverage, not failed.
- cmov and setcc of all 16 conditions on all 64 combinations of the status
  flags, against a model of the conditions;
- shifts' CF: SAR, SHR and SHL at every width, by an immediate and by CL,
  around their widths, against a model (SAR's CF by the width or more is the
  sign bit, defined), with `x86_instruction_flags` defining CF where the model
  does; and pairs of records through `x86_instruction_flags_substitutable`, run
  from every state of the status flags: a live flag it takes must come out
  alike, and `or ebx, 0` for `shl ebx, 0`, refused with CF live, must show CF
  apart;
- fixtures:
  - division's, mul's and the sign extensions' fixed registers, variable
    shifts' CL, partial writes and 32-bit zero extension, inc's CF, the SSE
    merges and legacy SSE's upper lanes;
  - a call's architectural effects, register and memory operands, zero
    idioms, shift counts;
  - the substitutions' legality for every set of live flags;
  - uncovered forms, which must get the conservative record;
- the immediate's contract on every covered form with an immediate and about
  60 boundary values: a field that takes a value gives it back, and one that
  doesn't can't (an imm8 of `0xc8` is −56, not 200);
- form choice:
  - fixtures of the values −129 to 256 at each width, constant
    materialization (INT32_MAX, 0x80000000, UINT32_MAX, negative 64-bit
    values), shift counts and the substitutions by their live flags;
  - every row with the boundary values and random ones, six live-flag
    contexts and three destinations. Each chosen record is validated and run
    against a reference sequence that holds the value in R11 and uses the
    operation's register form, and must leave the same registers, memory and
    live flags;
  - every candidate chosen by some request;
- `x86_choose_forms` and `x86_instructions_effects` against their scalar
  twins on random inputs, and with every column ending at a `PROT_NONE` page at
  every count to 100;
- #180's check: every row's forms allowed at the baseline target
  (`x86_target_forms`), every chosen record valid there
  (`x86_target_validate`);
- `x86_match.c`'s immediate ranges against the shared rule on the forms the
  assembler's syntax supports. It refuses the unsigned spellings of sign-extended
  patterns, which the shared rule takes; those are counted, not failed.

`--cross-check` also prints every chosen record with the encoder tests'
printer: GNU as must assemble them to the chosen forms' bytes. `--keep` leaves
its directory under `$TMPDIR` (or `/tmp`). The 9700X job's test step runs it
with `--cross-check`.

## x86 semantics benchmark

`./build/compiler benchmark-x86-semantics [--requests N] [--functions N]
[--seed N] [--after-scalar MS] [--only NAME]` (about 3 s) checks that the
vector paths agree with their scalar twins, then reports:

- the tables' bytes, and `x86_semantics_initialize`'s time;
- form choice per request: `--requests` requests (65,536) shaped like a code
  generator's constants, by `choose-scalar` and `choose-avx512`. Also the
  chosen forms' bytes against an imm32 for every value;
- operand-resolved effects per record, on `--functions` functions (2,048) of
  96 records shaped like clang's -O0 and -O2 code (branch sizing's generator),
  by `effects-scalar` and `effects-avx512`, beside `encode` (`x86_encode` of
  the same records);
- `code`: the run time and size of the choices that change the instruction
  sequence or its encoding (an imm8 against an imm32, inc, xor, test r, r, a
  narrower test, `mov r32` against movabs), in blocks of 64 over 8 registers.

Each case reports 21 interleaved-pass medians: nanoseconds, cycles,
instructions, IPC, and the throttle (PMCx1A0's umask 0x08, as in #199).
`--only NAME` times one path alone, back to back. `--after-scalar MS` runs the
case's scalar twin for MS ms (100 to 300) before every AVX-512 sample. Run it
under the 9700X's lock with `taskset -c 2`. `docs/x86-semantics.md` has the
numbers in both states. The default 9700X job runs it after the tests; like
the others, its measurements are informational.

## Lane tests

`./build/compiler test-lanes` (about 0.2 s) checks the lane base layer. The
arena checks run on the calling thread, and every other check runs on groups of
1, 2, 3 and 8 lanes and compares what the lanes produce with a sequential
reference:

- arenas: pushes at every power-of-two alignment up to 2 MB, invalid
  alignments, a push one byte past the capacity (it fails and leaves the arena
  as it was), sizes and counts whose arithmetic overflows, and the page sizes
  asked for, read back from `/proc/self/smaps`;
- `lane_range` and `lane_range_by_cost` for every count from 0 to 1,000, with
  seven cost patterns: the parts must cover the items in lane order, and each
  item must go to the lane whose share of the total cost holds its midpoint;
  and the same of `range_part` and `range_part_by_cost` with 64, 512 and 1,001
  parts;
- 10,000 rounds of a sync and a broadcast, with a lane arriving late every
  1,000 rounds, by turns so late that the others sleep on the futex and only
  so late that they wait in `mwaitx`, then as many broadcasts back to back,
  and narrow tasks spread by `lane_from_task_index`;
- a lane that makes itself a group of one and rejoins, and the scratch arenas;
- the design's exchange (rule 4 in "Lanes"): records counted per hash
  partition, placed, filled and resolved by partition, byte-identical to the
  reference for every lane count;
- parts (rules 1 and 3): 12 inputs cut by `lane_parts_by_cost`, whose parts the
  lanes take with `lane_take`, then count, place and fill by part, with a late
  lane in every loop. The output must be byte-identical to the reference,
  every part must be taken once, and every lane must get the same order,
  costliest first.

A watchdog ends the run with status 1 if a group doesn't finish within 60 s,
which is how a lost wake-up would show. `--rounds N` and `--seed N` change the
rounds and the random inputs.

## Deterministic output tests

`./build/compiler test-diagnostics` checks the buffered diagnostic merge:
the AVX-512 radix sort against stable insertion sort, guard-page tails,
complete 64-bit keys, grouped notes, the earliest fatal cutoff, and bounded
allocation failures. Jobs run in forward and reverse order with delayed
completion at 1, 2, 3 and 8 lanes; the bytes must agree.

`test-preprocessor` (without files), `test-object-writer`, `test-executable`,
`test-dynamic`, `test-static` and `test-files` also check output after changing
opaque identifier assignments. Each accepts `--id-order N` (default 153). The
object test covers both section modes at every lane count; the preprocessor
compares both `-E` text modes and diagnostics through both kernels and both job
orders; the per-file jobs' test primes the shared table before the walk fills
the cache, and compares every translation unit's `-E` text, diagnostics and
locations and every cached file's tokens at every lane count. The 9700X workflow
runs these checks as part of the stage commands.

`./build/compiler benchmark-diagnostics [--only scalar|selected]
[--after-scalar MS]` compares the
ordering paths with 21 interleaved samples, in ns/key and invariant-TSC
ticks/key. Run both isolated `--only selected` and
`--only selected --after-scalar 100`, across several processes pinned to CPU 2
under the host's lock. [The determinism guide](docs/determinism.md) specifies
the caller's source-order and lifetime contracts and the integration still
required by #151 and #152.

## Lane benchmark

`./build/compiler benchmark-lanes [--lanes N] [--cpus LIST] [--rounds N]` pins
lane i of groups of 1 to 8 lanes to the i-th CPU of `--cpus` (by default the
allowed CPUs in order: 0 to 7 on the 9700X, one per core) and reports
21-pass medians of a sync and a broadcast when every lane arrives at once, of
`pthread_barrier_wait` (the RAD Debugger's barrier on Linux) in the same loop,
of waking lanes that wait in `mwaitx` and that sleep on the futex, and of
starting a group. Then, on the largest group, how much slower lanes run after
waiting 0.2 to 5 ms for a lane that works alone, and how well rule 1's parts
and the static ranges balance three phases, with every lane at full speed and
with half of them at half speed. Then what an arena's reservation and first
touches cost. Run it under the 9700X's lock
without `taskset`, since it pins its lanes itself.
`docs/object-writer-and-linker.md` ("Lanes") has the numbers. The default 9700X
job runs it after the tests; like the others, its measurements are
informational.

## Link representation tests

`./build/compiler test-link` checks `link.c` and `link_program.c`:

- random records of every table read back from the columns, with ids from 1
  and record 0 zero;
- the name hash: `link_hash`'s values for known names, which pin it (it is
  part of the snapshot's format), and `link_hash_names` against `link_hash`
  on random names of 0 to 200 bytes, every count to 40, every length in every
  lane of a batch, and names ending at a `PROT_NONE` page;
- every table at its capacity, the side table of wide addends full, a name
  arena too small, and arenas too small for the tables: each add fails with
  `LINK_ERROR_FULL` and changes nothing;
- each rule the adds and `link_check` enforce, with the error it gets;
- every fixup kind on the instructions it can patch, with the bytes, field
  offsets and addends GNU as gives (binutils 2.42), each alone and in streams
  of up to 1,000 marked instructions with unmarked ones between them, then
  every kind on fields it can't patch, and references that don't match the
  marked instructions;
- addends around int32's limits, which go to the side table exactly when they
  don't fit, through an instruction's distance to its end too;
- generated programs with seven sets of options: the defaults, the smallest,
  16 units, no absolute addresses, `--functions N` workers (2,000 by default)
  without `SIZE32` and `SIZE64`, which wild 0.10 doesn't link, and two that
  call libc (the defaults, and 16 units without sizes). The same seed must
  give byte-identical tables and bytes, and another seed others. Each is laid
  out in memory as a static executable at a fixed address (`link_test_load`,
  which patches every kind and is the reference for the direct path's patch
  kernel) and run in a child process. A program that calls libc gets
  stand-ins for `write`, `strlen`, `environ` and `__errno_location`, and the
  child does what the C runtime does around `main`. Each must exit with status
  0 having printed the output the generator's model predicts, and again with
  its TLS segment starting half its alignment past a multiple of it, as a
  linker may start `.tdata` when `.tbss` is more aligned. Each unit's
  functions come one after another, as a compiler's object has them, with an
  FDE each in the unit's `.eh_frame` atom: GCC's CIE, the function's range and
  the call frame instructions its pushes and pops give, `_start`'s saying it
  is the outermost frame. Every set with two thread-locals, an init function
  and a fini function must have every class but the C runtime's (`INIT` and
  `FINI`) and every kind its options allow, and a copy of the defaults'
  program with the hexadecimal digits' fixup one byte off must print
  something else;
- and two shared libraries, the two sets that call libc as libraries
  (`LinkProgramOptions.shared`, from seeds of their own; the other checks that
  take test-link's sets don't): position-independent code whose exports are
  its functions (the workers, `buster_mix` and the entry,
  `buster_library_main`, which takes `main`'s arguments), every other global
  symbol hidden, the thread-locals reached with initial-exec, and each
  function's FDE at a local symbol at its start. Each is generated and run as
  the others are, through its entry, and must have no local-exec access, no
  `PC32`, `PC64` or size of a symbol another object may preempt, its exports
  exactly its functions, and each FDE at a local symbol.

`--seed N` changes the random records and programs. The programs check is
what scales: `--functions 1000000` builds three programs of a million workers
each.

`--print-assembly DIR` writes the same programs (for the same `--seed` and
`--functions`) as GNU as source instead: a directory for each set, with a
`unit_N.s` per unit and `expected`, the output. Each atom's bytes go in its
class's section, with a label at each symbol and a `.reloc` for each fixup, so
GNU as's object of a unit is a reference for the object writer's. To check
the programs with real toolchains, assemble every unit, then link each
program, run it and compare what it prints (the `static-*` directories with
`-static -e _start`, the `libc-*` ones through `cc`, and the `shared-*` ones
with `cc -shared`, each loaded by a program that calls its
`buster_library_main` with `main`'s arguments, as test-object-writer's
harness does):

```sh
./build/compiler test-link --print-assembly build/programs
for s in build/programs/*/unit_*.s; do as $s -o ${s%.s}.o; done
for d in build/programs/static-*; do
    ld.bfd -static -e _start -o $d/program $d/unit_*.o && $d/program | cmp - $d/expected
done
for d in build/programs/libc-*; do
    cc -o $d/program $d/unit_*.o && $d/program | cmp - $d/expected
done
for d in build/programs/shared-*; do
    cc -shared -o $d/library.so $d/unit_*.o && ./harness $d/library.so | cmp - $d/expected
done
```

The same with `ld.lld`, `mold` or, for the `*-no-sizes` sets, `wild` for
`ld.bfd`, and with `-fuse-ld=lld`, `mold` or `wild` and `-no-pie` or `-static`
for `cc`. On the 9700X, GNU ld 2.47, lld 23.1.1 and mold 3.0.0 linked every
set and wild 0.10.0 the `*-no-sizes` ones, 37 links per seed, for seeds 1, 2,
3, 7, 99 and 1,000, and for seed 5 with 100,000 workers; every program printed
what its model predicts. GNU ld and lld (with `-z norelro`) also start the TLS
segment off its alignment when told where: seed 1's `static-defaults` with
`--section-start=.tdata=0x500008 --section-start=.tbss=0x500030`.

## Object writer tests

`./build/compiler test-object-writer [--seed N] [--cross-check] [--keep]` checks
`object_writer.c`:

- objects: each unit's object of `test-link`'s seven programs and two shared
  libraries (with 500 workers in the one that takes `--functions`), of a link
  made by hand (gaps between atoms, an atom of no size, weak, hidden and
  protected symbols, pointers into a string pool, a call to and a pointer to a
  local IFUNC, units without atoms between and after the others) and of a link
  without atoms, with and without function sections.
  Each is built twice, the second time over poisoned memory, and must come out
  the same; the test's own ELF reader reads it back and checks the rules every
  object keeps (`ELFOSABI_GNU` exactly when a symbol is an `STT_GNU_IFUNC`);
  and it is compared with the representation, from which the test works out
  every section, symbol and relocation again, with its own tables of section
  formats and relocation types. Among them, every local-exec TLS reference
  must be an `R_X86_64_TPOFF32` against its TLS symbol over a zero field, every
  call through the GOT an `R_X86_64_GOTPCRELX`, and every reference to a local
  IFUNC against the IFUNC. Then an arena too small for an object
  (`OBJECT_ERROR_FULL`), an addend past int64 (`OBJECT_ERROR_RANGE`), and links
  with an absolute symbol, referenced and not, which `object_prepare` refuses;
- many sections: one unit of 33,000 functions in sections of their own, 66,041
  sections: `e_shnum` and `e_shstrndx` in section 0, and symbols' indices
  through `SHN_XINDEX` and `.symtab_shndx`;
- lanes: every unit's object of a program of 24 units and a 25th without
  atoms written by `object_write_units` with groups of 1, 2, 3 and 8 lanes,
  byte-identical to `object_build`'s; and a group in which a lane has no room
  to start, which writes no object and gives every unit `OBJECT_ERROR_FULL`;
- files: `object_write_file` into a new file, over a larger and a smaller one
  (in place: the same inode, truncated), over one with another hard link (a
  new file with its mode, and the other link keeps the old bytes), also with a
  name of `NAME_MAX` bytes, into `/dev/null`, into a pipe, and into a missing
  directory (an error);
- with `--cross-check`, which needs `readelf`, `llvm-readobj`, `as`,
  `ld.bfd`, `ld.lld`, `mold` and `wild` on `PATH`, and a `cc` that takes
  `-fuse-ld=wild` (GCC 16 on the 9700X):
  - `readelf -W` and `llvm-readobj` list every object's OS ABI, sections,
    symbols and relocations, which must be what the test's reader read, field
    by field;
  - GNU ld, lld, mold and wild link each program's objects, without errors or
    warnings: the static programs with `-static -e _start`, the ones that
    call libc through `cc` as a PIE, with `-no-pie` and with `-static`, and
    the shared libraries through `cc -shared`, each loaded by a harness that
    `cc` builds, which calls its `buster_library_main` with `main`'s
    arguments (`dlopen` and `dlsym`). The executable (or the harness) must
    exit with status 0 having printed what the program's model predicts. wild links only the programs without `SIZE32` and `SIZE64`, which
    wild 0.10 refuses, and among them one of 33,000 functions past
    `SHN_LORESERVE` sections. The same linkers link, the same three ways, a
    program whose `main` calls a local IFUNC directly and through a pointer,
    which must exit with status 42, 21 from each call (calling the resolver
    itself would return its implementation's address);
  - GNU as assembles each unit as `test-link --print-assembly` writes it, and
    its object must match the writer's: the same sections and bytes, symbols
    and relocations. They may differ where GNU as does: it orders sections as
    the source names them and places them in the file its own way, keeps an
    empty `.text`, `.data` and `.bss`, adds a `.note.gnu.property` where
    binutils is built to (as on the 9700X), writes no `.comment` or
    `STT_FILE`, keeps only the unnamed symbols relocations into `SHF_MERGE`
    sections need, and for a `.reloc` against a named local symbol keeps the
    symbol where the writer, like GNU as for an instruction, names the
    section's. So a relocation's target is compared by where it points, but
    its form must be the same for the GOT, TLS and size types and into
    `SHF_MERGE` sections.

The objects and programs go in a new directory under `$TMPDIR` (or `/tmp`),
removed at the end unless `--keep`. `--seed N` changes the programs. On the
9700X (binutils 2.47, LLVM 23.1.1, mold 3.0.0, wild 0.10.0 and GCC 16.2.1)
the checks take about 0.04 s, and 3.7 s with `--cross-check`.

## Object writer benchmark

`./build/compiler benchmark-object-writer [--units N] [--functions N]
[--filler N] [--function-sections] [--directory DIR] [--seed N]` generates a
program of `--units` units (64 by default), each of `--functions` workers (200)
of up to `--filler` filler instructions (40), with 8 globals, 2 thread-locals,
20 strings and an init and a fini function per unit: by default each unit is
about one of synth's translation units. It reports, per object, 21-pass medians
of `object_prepare` (once per link), of `object_build` alone (with cycles,
instructions and IPC), and of writing each object to a file in `--directory`
(`$TMPDIR` or `/tmp`) over the one from the pass before (in place) and as a new
file, each against its floor: the same system calls without the writer's
`fstat`. Each way writes files of its own and the ways take turns going first,
since rewriting pages just written costs less. Run it pinned to one CPU, and
with `--directory` on each file system to measure;
`docs/object-writer-and-linker.md` ("The object writer (`-c`)") has the numbers
for tmpfs and btrfs. The default 9700X job runs it after the tests, pinned to
one allowed CPU; like the others, its measurements are informational.

## Import tests

`./build/compiler test-import [--seed N] [--cross-check]` (about 0.21 s on
the 9700X, and 0.32 s with `--cross-check`) checks `import.c` and
`runtime.c`:

- round trips: each unit's object of `test-link`'s seven programs (with 500
  workers in the one that takes `--functions`), with and without function
  sections, and the object writer's unit of 33,000 functions in sections of
  their own (past `SHN_LORESERVE` sections), imported into a link of its own.
  Each of the unit's atoms must be in the atom its section became, byte for
  byte where the writer put it, each symbol at the same place with the same
  name, flags and size, and each fixup at its place, of its kind, pointing at
  the same place with the same addend, and no other fixups;
- an object made by hand with a section of every class, the sections the
  import drops (`.sframe` with its relocations, a note, `.note.GNU-stack` and
  `.comment`), every kind of symbol (an IFUNC and absolute ones among them)
  and every relocation type the representation has, out of order, with
  `R_X86_64_NONE` and addends past int32, a COMDAT group and prioritized init
  and fini arrays: each section, symbol, fixup, group and priority imported
  exactly, the atoms' bytes in place in the file; among them `GOTPCRELX` and
  `REX_GOTPCRELX` relocations on a `call`, a `jmp` and a `mov` through the GOT,
  which import as `CALL_GOT` and `LOAD_GOT`, and on a `push`, an `add` and a
  `mov` whose addend isn't -4, which import as `GOTPCREL`; the same with its
  relocations in order, with `e_shnum` and `e_shstrndx` in section 0, with
  `.eh_frame` as `SHT_PROGBITS` and with tables' `sh_entsize` 0; and one
  without a symbol table, as `crtn.o` is. Then each check a well-formed object
  never trips, one change at a time, with the error, the index of the
  section, symbol or relocation, the type and the byte offset it reports;
- archives made by hand (a 32-bit index, a 64-bit one and none, long names,
  an odd member's padding, a member of no bytes, a member without a symbol,
  an index of several of the vector path's batches with a member's symbols
  across them, and no members), then malformed ones;
- a shared library made by hand: version definitions, a name at a default
  version and at a hidden one, IFUNC, `GNU_UNIQUE`, `COMMON`, TLS and
  absolute symbols, weak and protected ones, hidden and local ones and one at
  `VER_NDX_LOCAL`, which aren't exported, references, and a name exported
  twice (the index finds the first); the same without `.gnu.version`; then
  malformed ones;
- linker scripts: `libc.so`'s and GCC's `_asneeded` ones, commas, comments
  and `OUTPUT_FORMAT`, then each error at its offset;
- page edges: every prefix of each input made by hand (the object also with
  its relocations in order), and the input with each byte set to 0, 0xff and
  a random value, ending at a `PROT_NONE` page, through every reader: each
  succeeds, or fails reporting a place within its input. A read past the end
  faults; the command then names the input and exits with status 1;
- the paths: each object, archive and library above (the round trips, the
  inputs made by hand, every malformed one, every page-edge input and this
  system's runtime) is read by its reader's vector path, its exact path and
  the reader itself, each into a link (or a read) of its own. The reader's
  result and records must be the exact path's; the vector path may take an
  input only when the exact path reads it without an error, and must then
  make the same records, column by column. Each well-formed input must take
  the vector path, which sorts relocations out of order, and each check
  prints how many inputs each path took. A library's index must find what a
  linear scan of its exports finds;
- discovery: GCC's search under a directory of the test's own, whose newer
  version's directory has no `crtbeginS.o`: the search picks the older
  version and probes the newer one's `crtbeginS.o` too, so that installing it
  leaves the probes stale (`runtime_probes_current`) and a fresh search picks
  it;
- the runtime: `runtime_import` on this system's files. Each crt object must
  import with what it is for (`_start`, `_init` and `_fini` in `.init` and
  `.fini` atoms, crti.o's fragment first, `__dso_handle`, `__TMC_END__`),
  `libc.so.6` with its soname, `write`, `strlen`, `environ` and
  `__errno_location` at `GLIBC_2.2.5`, `__libc_start_main` at `GLIBC_2.34`
  and `GLIBC_ABI_DT_RELR`, the loader with its soname, `__tls_get_addr` at
  `GLIBC_2.3` and `_r_debug` and `__libc_stack_end` at `GLIBC_2.2.5`, its
  version indices past `libc.so.6`'s, `libc_nonshared.a` with `atexit` in its
  index, and every reference to each name bound: `main` to the program,
  `__libc_start_main` and `__cxa_finalize` to libc, `__gmon_start__` and the
  `_ITM_` functions to nothing, `__TMC_END__` to `crtendS.o`, and
  `_GLOBAL_OFFSET_TABLE_`, which the crt objects that use the GOT name, to the
  linker. Each object's piece must follow the one before, with its atoms in
  its own unit and its names' bytes one run, and the global names (binding's
  index of the runtime's names and libc's) must hold each name once, found by
  its bytes and in its filter, each global symbol's its own: `main` defined by
  none of them, `__libc_start_main` libc.so.6's, `_r_debug` the loader's,
  numbered after `libc.so.6`'s exports, `__dso_handle` crtbeginS.o's, `atexit`
  a member's, and `_GLOBAL_OFFSET_TABLE_` and the init and fini arrays' bounds
  the linker's, whether or not the runtime refers to them;
- the static runtime: `runtime_import_static` on this system's files.
  `crt1.o`, `crtbeginT.o` and `crtend.o` with `_start`, `_init`,
  `__dso_handle` and `__TMC_END__`, and `crt1.o`'s ABI note; the archives in
  GCC's group order, each one's members one after another; `main` bound to
  the program, `__libc_start_main` to `libc.a`'s member,
  `__gmon_start__` to nothing, `__TMC_END__` to `crtend.o`,
  `_dl_relocate_static_pie` to `crt1.o`'s empty one, and
  `_GLOBAL_OFFSET_TABLE_` and the IRELATIVE relocations' bounds to the
  linker, and nothing to a shared library; the names its linker defines
  among the global names, the linker's, and no other; `memcpy` an IFUNC of
  `libc.a`'s, `_nl_current_LC_CTYPE_used` absolute, and `libgcc.a`'s
  priorities; each object's COMDAT groups as its `ImportObject` has them,
  with the same signature numbers for groups of the same signature and
  different ones otherwise; each piece's strong references as a scan of its
  symbols finds them, with the member each loads, and each global name's
  defining member; `link_check` on the whole runtime; and every crt object
  and member taking the vector path;
- the static PIE's runtime: `runtime_import_static_pie`, checked as the
  static runtime is: `rcrt1.o`, `crtbeginS.o` and `crtendS.o`, with the
  same archives; `_dl_relocate_static_pie` bound to `libc.a`'s
  `dl-reloc-static-pie.o`, which relocates the executable, and its
  `_DYNAMIC` to the linker; the IRELATIVE relocations' bounds, which
  `libc.a` refers to weakly and a static PIE's linker doesn't define, to
  nothing; and with `--cross-check`, every relocation compared with
  readelf's, as the static runtime's are;
- the shared library's runtime: `runtime_import_shared`, without a start
  object (its piece empty, no ABI note); `crti.o`, `crtbeginS.o`, `crtendS.o`
  and `crtn.o`; `libc.so.6`, the loader and `libc_nonshared.a`; nothing
  referring to `main`, which is no global name; `_init` crti.o's and
  `__dso_handle` crtbeginS.o's; the names its linker defines (`_DYNAMIC`
  among them), and no other; and every crt object and member taking the
  vector path.

`--cross-check`, which needs `readelf`, compares `libc.so.6`'s and the
loader's exports (name, version, type, binding and size) and references with
`readelf -W --dyn-syms`, and every relocation of the crt objects and the
archives' members, the static runtime's among them, with `readelf -W -r`:
each one must be a fixup of its kind at its offset (a `GOTPCRELX` or
`REX_GOTPCRELX` one's by the instruction before it, as the test reads it: 379
of `libgcc.a`'s are `GOTPCREL` on the 9700X, adds of slots), against the
symbol (or the section) readelf names, with its addend, and there must be no
other fixups. `--seed N` changes the programs
and the random bytes. The 9700X job's test step runs it with `--cross-check`.

## Import benchmark

`./build/compiler benchmark-import [--units N] [--functions N] [--seed N]
[--library PATH]... [--archive PATH]...` checks that each reader's vector
path reads its inputs as the exact path does, then reports 21-run medians of
both, the paths taking turns (time, cycles, instructions, IPC):

- objects: the object writer's objects of a generated program of `--units`
  units (64) of `--functions` workers (200), each about one of synth's
  translation units; the same with function sections; one of 33,000
  functions in sections of their own; this system's crt objects and
  `libc_nonshared.a`'s members; and a static executable's crt objects and
  archives' members, per symbol table entry and relocation;
- `libc.so.6` and each `--library` (per export and reference), with its
  exports' index against the open-addressing table it replaced, built and
  looked up for every name; `libc_nonshared.a` and each `--archive` (per
  index entry); `libc.so`'s script, which has no vector path;
  `runtime_import` end to end; the static runtime's archives; and
  `runtime_import_static` and `runtime_import_static_pie` end to end (a
  static PIE's runtime is the static executable's archives with other crt
  objects, so the archives' rows are its too).

Run it under the 9700X's lock with `taskset -c 2`. With `--library
/usr/lib/libstdc++.so.6 --library /usr/lib/libLLVM.so.23.1 --archive
/usr/lib/libc.a` it measures the larger files the design's "Step 5's phases,
data-parallel" has the numbers for. The default 9700X job runs it after the
tests, pinned to one allowed CPU; like the others, its measurements are
informational.

## Bind tests

`./build/compiler test-bind [--seed N] [--cross-check] [--keep]` (about
72 ms on the 9700X, and 1.05 s with `--cross-check`) checks `bind.c` against
this system's runtime (`runtime_import`). Every check binds by both paths,
which must make the same link, column by column, and the same imports,
exports and members:

- programs: test-link's two sets that call libc, each generated with room for
  the runtime, Scrt1.o, crti.o and crtbeginS.o joined after the program's
  symbols, its atoms, then the rest (`link_program_generate_begin`, `_atoms`
  and `_end`, which runs `link_check` on the whole link). Then: one symbol per
  global name; every name's bytes in the link's own arena and every atom's in
  the program's; every fixup's target defined, imported from `libc.so.6` at
  its export's version and with its type, the linker's, or a weak reference
  to nothing; `main` the program's, and what the crt objects refer to bound
  as GNU ld binds it (`__libc_start_main` at `GLIBC_2.34`, `__cxa_finalize`
  weakly, `__gmon_start__` and the `_ITM_` functions to nothing,
  `_GLOBAL_OFFSET_TABLE_` to the linker, `__TMC_END__` to `crtendS.o`,
  `__dso_handle` in `crtbeginS.o`, hidden); the crt objects' atoms before the
  program's and after it in GCC's order, each with the runtime's bytes and
  fixups; `.init`'s and `.fini`'s fragments `crti.o`'s first and `crtn.o`'s
  last, and `crtendS.o`'s `.eh_frame` last;
- names: every name of the programs and every export of `libc.so.6`, looked
  up in the runtime's index, must find what a scan of the runtime's names
  finds;
- rules: links made by hand, each exercising one rule, with what binding
  reports: a strong definition of `_start` (a duplicate); one of `data_start`,
  which Scrt1.o defines weakly (the program's wins), and a weak one and a weak
  absolute one (Scrt1.o's wins, first in link order, at `__data_start`'s place
  over the absolute one); weak definitions of `_init`, with their atoms
  before and after `bind_begin` (crti.o's strong one takes the symbol, with
  its atom in `.init` and its visibility); a reference to
  `__dso_handle` (defined by crtbeginS.o's atom, hidden); strong and weak
  references to a name nothing defines (an error, and nothing); definitions
  of `strlen`, which libc.so.6 defines, and `_dl_argv`, which it refers to
  (exports), a hidden `qsort` and a name of the program's own (not); two
  symbols named `write`; references added after `bind_begin` of a name the
  runtime has and of `memcpy`; strong and weak references to `__cxa_finalize`,
  which crtbeginS.o refers to weakly; strong and weak references to
  `pthread_atfork`, which a member defines (it joins, and doesn't), and a
  hidden one (it joins); hidden and protected references to `puts`, which only
  libc.so.6 defines (an error), and a weak hidden one (nothing, not an
  import); references to the init and fini arrays' bounds (the linker's); a
  local `_init`; a symbol of the program's that it never defines (left to
  `link_check`); and a link without room. Then the rules this system's runtime
  can't reach through the entry points, on binding's own steps: a strong
  reference joining a weak one that binding left to nothing (an error, but
  not for a name left to `crtendS.o`), a name left to `crtendS.o` with source
  `RUNTIME_ARCHIVE`, and an import a member came to define dropped from the
  imports;
- an atexit program: `main` registers a handler with `atexit` and returns 3,
  the handler exits with 0. Bound, it must pass `link_check`, and `atexit.oS`
  must join, defining `atexit`, its `__cxa_atexit` imported at `GLIBC_2.2.5`
  and its `__dso_handle` bound to crtbeginS.o's;
- the linkers' cases, programs whose binding GNU ld and lld decide for
  themselves: a weak `_init` that exits with 7 if it runs, against crti.o's;
  `main` returning a weak `data_start` of 7, after Scrt1.o's of 0; a call
  through the GOT to a hidden `puts` (refused); `main` returning whether a
  weak hidden `puts` isn't 0; references to the arrays' bounds; and `main`
  returning `_r_debug`'s `r_version` plus 10 when `__libc_stack_end` isn't 0,
  which only the loader exports (status 11), with both references strong,
  both weak and one of each. GNU ld and lld need a library `AS_NEEDED` only
  for a strong reference, so with weak ones alone the loader isn't needed and
  both are left to nothing, unversioned, and with a strong one both are
  imports at the loader's versions. Each must bind, or fail to, as the
  linkers link it;
- with `--cross-check`, which needs `ld.lld`, `ld.bfd`, `cc` and `readelf`:
  each bound program written as objects, one per unit, linked by ld.lld and
  ld.bfd as a PIE with only `libc.so.6` and, as needed, the loader besides,
  in GCC's order and with `--eh-frame-hdr` as GCC's driver asks, without
  errors or warnings, must run and print what its model predicts (the atexit
  program must exit with 0, the loader's cases with 11); and the program's own
  objects, linked by `cc` with lld, must have in readelf's `.dynsym` the
  imports at their versions and with their types, the weak references with
  default visibility and the exports binding found, and lld's `--why-extract`
  the members that joined. Each linkers' case's own objects, linked by `cc`
  with GNU ld and with lld, must link, or fail to, as binding did, and run
  with the case's status (0), and so must its bound link.

Then the same against the static runtime (`runtime_import_static`), by both
paths, which must make the same link and join the same members:

- static programs: test-link's two sets that call libc. The members that
  join must be those GNU ld's archive scan loads, and no others: an oracle of
  the test's own that scans the archives' symbol indices in the group's order
  until a pass loads nothing, rather than reading binding's references. Then
  `link_check`; one symbol per global name and none of a shared library;
  every fixup's target defined, absolute, the static linker's, a weak
  reference to nothing, or `__tls_get_addr`, left to the relaxation; `main`
  the program's, `__libc_start_main` a member's, `_init` crti.o's,
  `__dso_handle` crtbeginT.o's and `_dl_relocate_static_pie` crt1.o's; and
  each joined object's atoms, in link order, but those of a COMDAT group
  whose signature an object before it has, with their priorities;
- static rules, on links made by hand: a strong reference to a name nothing
  defines (an error naming it); strong and weak references to
  `__tls_get_addr` (left undefined); the static linker's names (the
  linker's), and `_DYNAMIC`, which only a PIE's linker defines (an error
  naming it); weak and strong references to `puts` (the weak one binds to its
  member only if the closure loads it for another reason); a weak definition
  of `__libc_start_main` (crt1.o's reference takes it, and its member doesn't
  join); a weak absolute definition of `data_start` (crt1.o's weak one, first
  in link order, takes its place, at `__data_start`'s, as GNU ld and lld
  bind it); a reference to a name a member defines absolutely; and weak
  definitions of 4 and of 24 names the members refer to (none thread-local),
  which the vector path's closure compares with, or past 16 leaves to the
  exact path's;
- with `--cross-check`: each static program's own objects linked by
  `cc -static` with GNU ld (its `--trace` given twice) and with lld
  (`--why-extract`), which must load the members that joined and no others,
  and run printing what the model predicts; and five C programs that `cc`
  compiles (hello, stdio, threads, jumps and weak-puts), each imported as the
  program, bound and checked as the static programs are, and linked by both
  linkers, with the same members, to run with the program's status.

Then the static programs, the static rules, and with `--cross-check` the C
programs (compiled with `-fPIE`), against the static PIE's runtime
(`runtime_import_static_pie`), by both paths, which must make the same link
and join the same members, checked as against the static runtime: the
members the oracle's scan loads, among them `libc.a`'s
`dl-reloc-static-pie.o`, which `rcrt1.o` makes join by not defining
`_dl_relocate_static_pie`; that name the member's, its `_DYNAMIC` the
linker's and `__dso_handle` crtbeginS.o's; the rules' linker's names a static
PIE's (`_DYNAMIC` among them), and a strong reference to
`__rela_iplt_start`, which a static PIE's linker leaves to libc.a's weak
references, an error naming it; and with `--cross-check`, each
linked by `cc -static-pie` with GNU ld (`--trace` twice) and with lld
(`--why-extract`), which must load the same members and run as the model
predicts, or with the program's status.

Then against a shared library's runtime (`runtime_import_shared`), by both
paths, which must make the same link and binding:

- shared programs: test-link's two sets that call libc, bound as libraries:
  `link_check`; no `_start`, as there is no start object; `_init`, `_fini` and
  `__dso_handle` hidden and not exported; and the exports exactly the link's
  definitions with default or protected visibility, global or weak, in
  symbol order, `main` among them;
- shared rules, on links made by hand without `main`: a strong reference
  with default visibility to a name nothing defines stays undefined, neither
  an import nor an export, and a hidden one is an error naming it;
  definitions with default, protected and hidden visibility and a weak one,
  all but the hidden one exports; and references to `__tls_get_addr` and
  `puts`, imports of the loader's at `GLIBC_2.3` and of `libc.so.6`'s at
  `GLIBC_2.2.5`;
- with `--cross-check`, three C libraries that `cc` compiles with `-fPIC`
  (exports of every visibility; imports from `libc.so.6` and the loader,
  general-dynamic TLS with `-mtls-dialect=gnu`, and undefined and weak
  references; and `atexit`, whose member joins), each imported, bound and
  checked as the programs are, then linked by `cc -shared` with GNU ld, which
  must link it, and with lld, whose `.dynsym` must have the imports at their
  versions, the undefined and weak references and the exports binding found,
  and whose `--why-extract` the members that joined.

The objects and programs go in a new directory under `$TMPDIR` (or `/tmp`),
removed at the end unless `--keep`. The 9700X job's test step runs it with
`--cross-check`.

## Bind benchmark

`./build/compiler benchmark-bind [--functions LIST] [--seed N]` binds
generated programs that call libc, of 1, 64, 2,000 and 102,400 workers by
default (`--functions` takes a list), 200 workers per unit as synth's
translation units have, by each path, against the dynamic PIE's runtime, a
static executable's, a static PIE's and a shared library's, and reports
21-run medians (time,
cycles, instructions, IPC) of the match (the program's global symbols looked
up in the runtime's names, per global symbol), `bind_begin`, the static
runtimes' closure (the members the link needs, per member) and `bind_end`,
each run on the same link put back as it was. The paths must make the same
links. Run it under the 9700X's lock with `taskset -c 2`; the design's "Step
5's phases, data-parallel" and "Static links, as built" have the numbers. The
default 9700X job runs it after the tests, pinned to one allowed CPU;
like the others, its measurements are informational.

## Direct executable tests

`./build/compiler test-executable` (about 0.2 s on the 9700X) checks
`executable.c` in a directory of its own under `--directory` (`TMPDIR` or
`/tmp` by default), which it removes unless `--keep` says to leave it:

- the build ID's chunk hash: every length up to 2,100 bytes, cut at random
  places, hashes as it does whole, and zeros of each length up to a page hash
  apart from the next length's;
- test-link's program sets, and one of 50,000 workers in 38 chunks, each
  written by groups of 1, 2, 3 and 8 lanes, every time from zeroed fields over
  the last file scribbled over, so that a lane that skipped a field or a chunk
  shows. The files and build IDs must be byte-identical, and each program must
  run and print what its model predicts. In each file every fixup's field must
  hold its kind's value; a load or call through the GOT that relaxes (a
  `CALL_GOT` or `LOAD_GOT` with the addend -4 to a symbol of the image's that
  isn't an IFUNC: `executable_test_relaxes`, the test's own rule) must have its
  instruction rewritten (`mov` to `lea`, `call` to `addr32 call`, `jmp` to
  `jmp` and a `nop`) and every other must keep its own; the GOT must have a
  slot for each target of a GOT kind that a fixup reads unrelaxed (numbered
  from 1 in symbol order, holding its value), the headers must
  have lld's layout, no chunk boundary may split a field, and the build ID
  must be the tree of the file's chunks. The unwind tables, read back as an
  unwinder reads them (`executable_test_unwind`): `PT_GNU_EH_FRAME` exactly
  over `.eh_frame_hdr`; its version, encodings, pointer to `.eh_frame` and
  count of the FDEs that a walk of `.eh_frame` finds (a terminator only last);
  its table sorted, each entry an FDE and its initial location, each FDE
  once; a lookup of each FDE's first and last byte, by libgcc's binary
  search, finding it, and one of each function finding its FDE, of the
  function's size; and the write's count of FDEs, which must have come in
  order. The exact twins must write the same file with 1 and 8 lanes. A copy
  of the defaults' program with one pointer 8 bytes off must print something
  else, and the two sets that call libc (step 5's) must be refused, an import
  named, without creating the output;
- chunk boundaries: programs with a call's rel32 0 to 4 bytes before the
  nominal 256 KB boundary and an `ABS64` 0 to 8 bytes before 512 KB, both in
  atoms larger than a chunk, and with a call through the GOT (`call [rip +
  slot]`, which relaxes to `addr32 call` and so rewrites the 2 bytes before
  its field) whose 6 bytes start 0 to 6 bytes before 256 KB. A boundary must
  move to the end of a field (or such an instruction) that crosses it and stay
  where it is otherwise, with every lane count, and the program must run;
- the output file: created; overwritten in place by a smaller program (and
  truncated) and with its mode restored; written beside it and renamed over it
  when it has another link (which keeps its bytes), when it is a symbolic link,
  when a process runs it (which runs on), when it is someone else's (checked
  only when the test may `chown`, as root), past a file a killed link left
  under the name without random digits, and with a name of `NAME_MAX` bytes;
  and written in order into a FIFO, which a failed link leaves in place: for
  a reader that opens it and reads to the first end of file, with inotify
  counting one close after writing, and for one that holds the read end of a
  FIFO that may be written but not read (which only binds a user other than
  root). A directory and a path under a regular file are refused, naming the
  call. A link with an undefined symbol fails, naming it, and leaves the
  output alone; one whose `ABS32` falls below 0 removes it, and so does one
  whose `PC32` leaves int32's range, which only the checked path sees; one
  that fails while writing beside the output leaves the output, its other
  link, and nothing beside them;
- limits: a weak thread-local symbol defined nowhere (offset 0 in its field
  and its GOT slot); TLS blocks of 2 GB (linked) and past it, atoms past 2 GB
  and one whose padding wraps (refused); a bad entry point, the first of two
  symbols defined nowhere in different lanes' ranges, the smallest failing
  fixup, an atom aligned past a page, a bad init array atom and an atom of
  each of the C runtime's classes (`.init` and `.fini`), which step 4's
  executables have no section for, each reported alike with every lane count;
  and a lane with no scratch memory to spare, then 64 bytes more at a time,
  until the link succeeds: every failure must be `EXECUTABLE_ERROR_MEMORY` and
  leave no output;
- the unwind scanner: `.eh_frame` atoms of each CIE `.eh_frame_hdr` indexes
  (GCC's in versions 1 and 3, with a personality, an LSDA and a signal frame,
  and with a personality's pointer absolute or a LEB128) and of seven it
  doesn't, with up to 40 FDEs and with or without a terminator, each as built
  and changed in each of 19 ways: records too short, cut after `pc_begin`, too long,
  pointing into their CIE or past themselves, without `pc_begin`'s fixup or
  with an `ABS32` there, trailing bytes and one CIE past the walk's eight
  (refused); a terminator at the end or before an FDE, a second CIE, eight
  CIEs, a fixup among the instructions and a wide addend; and initial
  locations at int32's limits from the header and one past each. The walk
  must count the FDEs the test knows of, or refuse the atom; the vector path
  must count the same or leave the atom to the walk, and take every atom of
  one CIE and its FDEs; and with table entries, both must give a reference's,
  or refuse an entry outside int32;
- the sort: tables sorted with equal neighbors, sorted but for one pair,
  reversed, random, all equal, and from `INT32_MIN` to `INT32_MAX`, of every
  count to 200 and some to 5,000: the check must say whether they are sorted,
  and the sort (always, for the exact twin) give a stable sort's order;
- unwind links: 40 functions aligned so that the layout's buckets put them
  out of their FDEs' order, in 12 `.eh_frame` atoms of the CIEs the header
  indexes, written by every lane count and by the exact twins into the same
  file, whose table must come out sorted and whose program must run; an
  initial location at `INT32_MAX` from the header (linked) and one past it
  (refused, naming its atom), and one from a wide addend (linked); and the
  first of two atoms the header can't index, in different lanes' ranges,
  refused alike by every lane count, and one before a symbol defined nowhere,
  refused for the atom: by the count, before the layout;
- blocks: 24 random links from the seed and 24 from seed 116 (whose links
  once outgrew the test's arena for their bytes) for the vector passes over
  blocks of 16 atoms (the count's and the placement's) and 16 symbols (their
  references): atoms of every class a static executable takes, in runs of
  random lengths so that blocks change bucket at every lane, of sizes from 0,
  zero-fill ones among them, aligned up to a page; symbols in half of them,
  thread-local or not, weak ones defined nowhere, GOT slots for some, and a
  table pointing at every one; every other link's atom table no larger than
  its atoms, past the class gather's limit. Each is written by every lane
  count and by the exact twins into the same file, which runs;
- the patch kernel: the fast path against the checked one, on random fixups
  of every kind with wide addends, at every count from 0 to 199 from four
  starts, with the kind column ending at a `PROT_NONE` page.

`--seed N` changes the programs and the random inputs, and `--functions N`
the workers of the set named after it (2,000 by default).

## Dynamic PIE and shared library tests

`./build/compiler test-dynamic [--seed N] [--functions N] [--directory PATH]
[--cross-check] [--keep]` (about 1.54 s with `--cross-check` on the 9700X)
checks `executable.c`'s dynamic PIEs against this system's
runtime (`runtime_import`), in a directory of its own under `--directory`
(`TMPDIR` or `/tmp` by default), which it removes unless `--keep`. Each
program is bound by `bind.c` and written by groups of 1, 2, 3 and 8 lanes,
each time from zeroed fields over the last file scribbled over and every
other time over poisoned scratch memory: the files must be byte-identical.
Each file is read back and checked by the test's own rules: the headers and
`.dynamic` (lld's tags, `DT_INIT` and `DT_FINI` crti.o's functions, and
`.gnu.version_r`'s entries, as many as its `sh_info`, its chain and
`DT_VERNEEDNUM` say);
`.dynsym` (every dynamic symbol once, undefined, an import at its version,
before the exports, and each export at its address, found through
`.gnu.hash` as glibc's loader looks it up); `.gnu.hash`'s shape (lld's); and
every relocation the loader needs, from `.rela.dyn` and `.relr.dyn` decoded,
against the GOT's slots and the `ABS64` fields of the atoms the loader writes,
with each slot's bytes and each call's stub; the loads and calls through the
GOT, each that relaxes by test-executable's rule rewritten and each other
kept, with a GOT slot for each symbol that an unrelaxed GOT kind or a call to
a dynamic symbol reaches, and for no other, in symbol order; and the unwind
tables, by test-executable's rules (the runtime's FDEs among them). The link's
bytes are binding's again after the writes, for the checks and the objects
that lld links. The writer's exact twins must write the same file with 1 and 8
lanes, and a write without RELR must have the same relocations in `.rela.dyn`
alone and run alike. The checks:

- programs: test-link's two sets that call libc, and one of `--functions`
  workers (2,000 by default) whose table of pointers has targets that the
  writer's filter takes for symbols outside the image. Each must run under
  the system's loader, without stand-ins, and print what its model predicts;
- cases: test-bind's atexit program and linkers' cases, each bound or refused
  as GNU ld and lld link it, and run with their links' status; the loader's
  cases with `DT_NEEDED` the loader after `libc.so.6` exactly when a strong
  reference needs it, and `.gnu.version_r` an entry for each library, each
  import's version in its library's;
- exports: sixteen functions whose names `libc.so.6` defines too, which
  binding exports and `main` finds with `dlsym` through the executable's
  `.gnu.hash`;
- imports: 256 of `libc.so.6`'s functions, each called through a stub and
  pointed to, which the loader must bind at their versions;
- refusals: a pointer to `main` in `.rodata` (a text relocation), an `ABS32` to
  `main` and a `PC32` to a dynamic symbol in `.data`, a `lea` of and a `PC64`
  in `.data` to a hidden weak reference to nothing, which stays at address 0
  wherever the program loads, and a `lea` of, a call to and a `PC64` in `.data`
  to an absolute symbol, which stays its value (GNU ld and lld refuse them
  too), each refused with its fixup by every lane count and by the exact twins,
  without an output; a call to and a pointer to a weak reference to nothing,
  and to a hidden one, loads through the GOT slots of the first and of the
  program's data (by `GOTPCREL`, which doesn't relax), the dynamic one's first
  in its word of the bitsets, and a load of the data that relaxes to a `lea`, a
  pointer to `main` at an odd address, which RELR can't hold, and pointers in
  `.data` and `.rodata` to, a load through the GOT of and an `ABS32` in `.data`
  to an absolute symbol at 42, which need no relocation (as lld links them),
  all of which run; and a lane short of scratch memory (lane 0 of 1 and of 8
  lanes, the last of 2 and of 8), with which every link that fails must fail
  with `EXECUTABLE_ERROR_MEMORY` and no output, and one with room enough must
  run.

With `--cross-check`, which needs `ld.lld`, `cc`, `objdump` and `readelf`, the
bound link's objects (the object writer's) are linked by lld as a PIE against
`libc.so.6` and the loader alone (with `--build-id`, and with `-z
pack-relative-relocs` when libc defines `GLIBC_ABI_DT_RELR`), and the two files
must have the same segments (the program headers' types in order, their flags
and the `PT_LOAD`s' alignments), the same dynamic symbols (name, binding, type,
whether defined, version), the same symbols with a GOT slot the loader fills,
as many GOT slots relocated RELATIVE and as many that no relocation fills (but
for ours of thread-locals' offsets, whose initial-exec accesses lld relaxes to
local-exec), the same `R_X86_64_64`s with their addends, as many RELATIVE
pointers outside the GOT, the same init and fini arrays' sizes, the same
`DT_NEEDED` libraries in order, each with the same needed versions, the same
`DT_INIT`, `DT_FINI`, `DT_GNU_HASH` and `DT_VERNEED`, and `.eh_frame_hdr`
tables (lld's with `--eh-frame-hdr`, as GCC's driver asks) that index the
same functions with the same ranges. And each program runs again with a
library preloaded, which `cc` builds: its `write` walks the stack with
libgcc's `_Unwind_Backtrace`, which finds each frame's FDE through
`PT_GNU_EH_FRAME` and the table, and the walk must go through `main` and end
at `_start`, the outermost frame. And `readelf -wF`'s reading of each of the
program's functions' FDEs (the CFA's rule and each saved register's, from each
location on) must be, row for row, what `objdump -d`'s disassembly of the
function's pushes and pops gives as GCC's CFI: the CFA 8 bytes further from
`rsp` after each push and 8 nearer after each pop, a callee-saved register a
push saves at the CFA less the new offset, and one a pop restores as it was.
`--seed N` changes the programs. The 9700X job's test step runs it with
`--cross-check`.

The same command also writes generated and compiled shared libraries against
`runtime_import_shared`. Every library is byte-identical on 1, 2, 3 and 8 lanes
and through the exact twins, then loads with `dlopen` and runs through the
generated harness. The file checks require ET_DYN without PT_INTERP, SONAME,
the library's exported symbols and initial-exec TLS, and the corresponding
dynamic relocations. Compiled fixtures verify that the executable's definitions
preempt the library's calls, data and pointer tables. With `--cross-check`, lld
links the same objects with `-shared`; the comparison accounts for its lazy PLT
slots and our non-lazy GLOB_DAT slots. Local-exec TLS, text relocations, a direct
address of a preemptible function, and currently general/local-dynamic TLS are
refused. See "The shared library" in the linker design for the supported scope.
Mapped-file checks hold the old library's descriptor and shared mapping across
replacement on every lane count; its inode and bytes must survive while new
opens find the new library. File-cleanup checks also preserve a concurrently
created destination when a first publication fails.

## Static executable tests

`./build/compiler test-static [--seed N] [--functions N] [--directory PATH]
[--cross-check] [--keep]` (about 2.9 s on the 9700X, and 3.2 s with
`--cross-check`) checks `executable.c`'s static executables with the C
runtime against this system's static runtime (`runtime_import_static`), then
its static PIEs against the static PIE's (`runtime_import_static_pie`), in a
directory of its own under `--directory` (`TMPDIR` or `/tmp` by default),
which it removes unless `--keep`. Each program is bound by `bind.c` and
written by groups of 1, 2, 3 and 8 lanes and by the writer's exact twins with
1 and 8 lanes, each time from the atoms' bytes as binding left them (the TLS
relaxation and the GOT relaxation rewrite them), with zeroed fields over the
last file scribbled over, every other time over poisoned scratch memory: the
files must be byte-identical. A link without general- or local-dynamic TLS
accesses is then written once more over the bytes the last write left, its
loads and calls relaxed already, and must come out the same.
Each file is read back and checked by the test's own rules:

- the headers: `_start` the entry, no `PT_INTERP`, a `PT_NOTE` with the start
  file's ABI note (`crt1.o`'s or `rcrt1.o`'s), and `PT_TLS` over `.tdata` and
  `.tbss`; a static executable an `ET_EXEC` without `PT_DYNAMIC`, and a static
  PIE an `ET_DYN` with `PT_DYNAMIC` over `.dynamic`, whose tags are lld's:
  `DT_FLAGS_1` PIE, no `DT_NEEDED` or versions, `.dynsym` with the null
  symbol alone, `.rela.dyn`'s address and size and `DT_RELACOUNT` its
  RELATIVE relocations, the arrays', and `DT_INIT` and `DT_FINI` `crti.o`'s
  functions;
- the IFUNCs: an `IRELATIVE` relocation for each stub's slot and for each
  IFUNC's GOT slot without a stub, its addend the IFUNC's resolver, in
  `.rela.iplt` (a static PIE's after its RELATIVE ones in `.rela.dyn`); each
  stub in `.iplt` a `jmp` through its own slot; an IFUNC with a stub at its
  stub's address for its calls, and everywhere when a fixup other than a call
  takes it (its GOT slot too), where one whose stub only calls reach reads the
  stub's slot through the GOT, as lld's reads its `.got.plt` entry; and a
  static executable's `__rela_iplt_start` and `__rela_iplt_end` at
  `.rela.iplt`'s bounds;
- a static PIE's RELATIVE relocations: one of each GOT slot that holds an
  address in the image (an IFUNC's stub among them, when it stands for the
  IFUNC's address) and one of each `ABS64` field of an atom the loader writes
  whose target is in the image, with the value as its addend, and no others:
  `.relr.dyn`'s (decoded, the field holding the addend) for the slots and the
  fields at even addresses in atoms aligned to 2 or more when `libc.a` takes
  `DT_RELR` (it defines `arc4random`, as glibc 2.36 and later do), and
  `.rela.dyn`'s for the others;
- every fixup's field: test-executable's kinds' values, with an IFUNC's stub
  for its address, a general- or local-dynamic access as the local-exec
  sequence it relaxes to, `DTPOFF32` as an offset from the thread pointer, an
  absolute symbol's value, and nothing for a call of `__tls_get_addr`, which
  the access consumed; each `CALL_GOT` and `LOAD_GOT` with the addend -4 to a
  symbol of the image's that isn't an IFUNC relaxed from its instruction in
  the link's bytes (the test checks it is the `call`, `jmp` or `REX.W` `mov`
  the kind says): `addr32 call`, `jmp` and a `nop`, or `lea`, and every other
  keeping its instruction; the GOT's slots in symbol order (but for the
  targets whose every slot read relaxed, and the IFUNCs whose stubs only
  calls reach), then the stubs'; and every name of the linker's where its
  section or segment says (`_DYNAMIC` at `.dynamic`);
- the init and fini arrays: their prioritized atoms first, by priority and in
  id order, then the others in id order, from their section's start to its
  end;
- the unwind tables, by test-executable's rules.

The checks:

- programs: test-link's two sets that call libc, and one of `--functions`
  workers (2,000 by default): each must run and print what its model
  predicts, as a static executable and as a static PIE;
- compiled: test-bind's C programs (hello, stdio, threads, jumps and weak-puts)
  and this file's: prioritized constructors and destructors beside libgcc's
  `cpuinfo.o`'s; decimal floating point, whose rounding mode and flags are
  libgcc's thread-local variables, reached by general-dynamic accesses, from a
  thread too; GCC 14's heap trampolines, whose member's accesses are
  local-dynamic, where `cc` is GCC 14 or later (it says so otherwise); and
  assembly with each general- and local-dynamic sequence through the PLT and
  the GOT (status 31), with a local IFUNC called, pointed to and compared, and
  a global one called through the GOT (status 53), with an IFUNC called
  directly, called through its GOT slot and loaded through it, whose stub only
  calls reach, so that the loads read the stub's slot (status 63), with a weak
  IFUNC defined nowhere, which has no resolver, loaded through the GOT, taken
  by a `lea` and pointed to, each 0 (status 0), and with absolute symbols that
  another object defines (combined with `ld -r`, since GNU as resolves a `lea`
  of an absolute symbol its file defines), taken by a `lea`, through the GOT,
  as an immediate and pointed to (status 0). `cc` compiles each, and each must
  run as `cc -static`'s link of the same object does, with the same status and
  output, both from directories of the same length (`argv[0]`). As static PIEs,
  each must run as `cc -static-pie`'s link does, but the IFUNC program, which
  must run with status 53, as lld's link does (GNU ld's gives the pointer in
  `.data` the IFUNC's implementation, where a `lea` gives its `.iplt` entry,
  and the comparison fails: 52), and the weak IFUNC's and the absolute symbols'
  programs, whose `lea` of address 0 and of an absolute symbol a static PIE
  refuses (`EXECUTABLE_ERROR_RELOCATION`, by every write), as `cc -static-pie`
  refuses them;
- refusals: assembly with a call of `__tls_get_addr` outside an access
  (refused naming the symbol), a general-dynamic access without its prefixes
  (its fixup), a pointer to an absolute symbol farther than int32 from the
  image's base (the symbol) and a `lea` of one whose offset from the base
  fits int32 but not its distance from the `lea` (its fixup, as GNU ld says
  the relocation is truncated; a static PIE refuses any `PC32` to an absolute
  symbol, as a relocation), each refused alike by every lane count and by
  the exact twins, without an output; as a static PIE, besides, a pointer to
  `main` in `.rodata` (a text relocation), an `ABS32` to `main` in `.data`
  and a `lea` of a weak reference to nothing, each refused as a relocation
  naming its fixup; and on the TLS program a lane short of
  scratch memory (lane 0 of 1 and of 8 lanes, the last of 2 and of 8), with
  which every write that fails must fail with `EXECUTABLE_ERROR_MEMORY` and
  no output, and one with room enough must run.

With `--cross-check`, which needs a `cc` that takes `-fuse-ld=lld`, each
compiled program is linked by `cc -static -fuse-ld=lld` too (and
`-static-pie`), which must run alike and have as many `IRELATIVE`
relocations, the same TLS segment's alignment and init and fini arrays of the
same sizes; but lld 23 links the weak IFUNC's program wrong (an `IRELATIVE`
relocation with resolver 0, which faults at startup), so that one only has to
link, and the static PIEs that a static PIE refuses aren't linked; lld packs
a static PIE's RELATIVE relocations into `.relr.dyn` (`-z
pack-relative-relocs`) as ours are, and a static PIE must have as many as
lld's. Then the GOT (`executable_test_got`, from the files' sections): our
slots must be as many as lld's `.got` entries and IFUNCs' `.got.plt` entries,
but for those of thread-locals' offsets, whose initial-exec accesses lld
relaxes to local-exec, and in a static executable those that only tests and
binops with REX read through the GOT, which lld writes with immediates
without PIC (`static_test_immediate_slots`: 5 of the decimal program's).
`--seed N` and
`--functions N` change the generated programs. The 9700X job's test step runs
it with `--cross-check`.

## Direct executable benchmark

`./build/compiler benchmark-executable [--directory PATH] [--functions LIST]
[--lanes N] [--seed N] [--only kernels|hash|links|dynamic|static|static-pie|shared]`
(about 4 s) reports medians of:

- the patch kernel and the variants it was chosen over (a branch on the kind,
  the checked path, 64-bit targets, and gathers with scalar stores or
  scatters) on the design's three synthetic sizes, each variant's bytes
  checked against the kernel's first;
- the chunk hash: 256 KB in L2, whole and in pieces, and 64 MB;
- links of generated programs of 1, 64, 2,000 and 180,000 workers
  (`--functions` takes a list), written into `--directory` (`TMPDIR` or
  `/tmp`) by a group of `--lanes` lanes that runs every write of a case, as a
  compiler runs its phases on one group: in place and as a new file, phase by
  phase, against the floor, one `pwrite` of the same bytes; and with one lane,
  the patch kernel on each program's own mix of kinds, three ways: relaxing
  the loads and calls through the GOT from the code generator's instructions
  (put back before each run, outside the time), relaxing them over the forms
  the relaxation wrote, and without relaxing them; and the unwind tables'
  passes: the FDEs counted by the vector path and by the walk, their table
  entries both ways, and the table's sorted check and sort;
- the same sizes as programs that call libc, bound against the system's
  runtime and written as dynamic PIEs the same way, with their dynamic
  symbols, relocations, RELR words and stubs (`--only dynamic` runs these
  alone);
- bound against its static runtime and written as static executables,
  with their stubs, IRELATIVE relocations and GOT slots (`--only static`);
- and bound against the static PIE's runtime and written as static PIEs,
  with their stubs, relocations (RELATIVE among them) and GOT slots
  (`--only static-pie`);
- bound against the shared-library runtime and written as libraries, including
  exports, preemptible calls and initial-exec TLS (`--only shared`).

Executable writes after a case's first reuse the last one's file, whose loads
and calls through the GOT the writer relaxed already. Shared-library writes
replace their file each time. Run it under the 9700X's lock
with `taskset -c 2` for one lane, or with `taskset -c 0-7` for 8 (one CPU per
core), since the lanes' threads keep the affinity they start with.
`docs/object-writer-and-linker.md` ("Patching and writing", "The first
executables", "The dynamic PIE", "The static executable with libc", "The
static PIE", "GOT relaxation in every executable" and "The shared library") has
the numbers.
The default 9700X job runs it after the tests, pinned to one allowed CPU;
like the others, its measurements are informational.

## Snapshot tests

`./build/compiler test-snapshot [--seed N] [--functions N] [--directory PATH]
[--keep]` (about 0.31 s on the 9700X) checks `snapshot.c` with this system's
runtimes, a dynamic PIE's, a static executable's, a static PIE's and a
shared library's, in a directory of its own under `--directory` (`TMPDIR` or
`/tmp` by default), which it removes unless `--keep`:

- round trip: the imported runtime written as a snapshot and loaded back must
  have the import's records, every one that binding and a dynamic PIE read
  (the names' and atoms' bytes and the members' names through the loaded
  runtime's deltas), every piece current, and the loaded runtime must use the
  file's mapping and no arena; so must it after another snapshot was renamed
  over the file it maps. Then the same for the static runtime
  (`runtime_import_static`), with its kind, its archives, its members'
  references (with the members they load) and COMDAT groups and its global
  names' members, for the static PIE's (`runtime_import_static_pie`), and
  for the shared library's (`runtime_import_shared`), its start object's
  piece empty;
- links, static links and static PIE links: test-link's two sets that call
  libc and a program of `--functions` workers (2,000 by default), bound by
  both of binding's paths against the imported runtime and against a loaded
  one: the same links and bindings, written as dynamic PIEs, or against the
  static runtime as static executables and against the static PIE's as static
  PIEs, by 1 and 8 lanes into the same bytes, which run; and test-bind's
  loader case bound both ways too, the loaded runtime's PIE running with its
  status (11);
- opening: `runtime_open` in a directory two levels below one that exists,
  which it makes private: imported and written, then loaded; with no
  directory, or without an identity, imported and nothing written; and where
  the directory can't be made, the runtime still imported, the failure named;
- kinds: `runtime_open_static`, `runtime_open_static_pie` and
  `runtime_open_shared`, each imported and written, then loaded, with its
  kind's records; the dynamic runtime's snapshot beside them, the four
  (`runtime.snapshot`, `runtime-static.snapshot`,
  `runtime-static-pie.snapshot` and `runtime-shared.snapshot`) the
  directory's only files; each kind's snapshot loaded as each other kind,
  stale; and each kind's snapshot renamed to the next kind's name in turn,
  stale there, imported again and written over;
- staleness: snapshots with probes of the test's own files, each changed one
  way (rewritten in place to the same size, its mode changed, replaced by a
  rename, removed, created where a probe found nothing, a file added to a
  probed directory, written with its times put back), and one written by
  another executable, must be stale, and `runtime_open` must rebuild them;
- invalid files, of the dynamic PIE's runtime's snapshot and of the static
  PIE's: 1,600 damaged copies (cut short, one byte longer, a byte of the
  directory changed at sampled offsets and in every byte of the header), each
  made in place over the snapshot and repaired after its load, must be
  invalid, and so must 10 whose header was changed and then rehashed
  (another version or magic, a region misaligned, running or starting past
  the end or of the wrong size, counts out of range, the directory ending
  before its last region), which only the header's own checks catch;
  `runtime_open` must replace them with the kind's runtime; a directory and
  a FIFO at the snapshot's path must neither block a link (run in a child
  under an alarm) nor fail it, the FIFO replaced and the directory left with
  nothing beside it;
- damaged pieces, of each kind's snapshot: a byte of the pieces' records
  changed at 123 offsets (at each 1/61 of them, and the first and last of
  each of their regions), each copy of which must load with every piece
  current but the one whose records hold the byte, by the test's own reading
  of the layout (none for a byte of padding or of a column no piece's hash
  covers). Then a program bound against a snapshot with a damaged piece, by
  each path: `crt1.o`'s (or `Scrt1.o`'s, or `rcrt1.o`'s), `crtend.o`'s (or
  `crtendS.o`'s), or a member the program joins must fail the binding with
  `BIND_ERROR_SNAPSHOT`, and a member it doesn't join must bind as against
  the whole snapshot. After a failure, `snapshot_discard` and `runtime_open`
  import the runtime and write a snapshot that loads;
- concurrency: eight processes opening the runtime at once in an empty
  directory must each get it and leave one snapshot, which loads, and no other
  file;
- the cache directory: `snapshot_directory` from `XDG_CACHE_HOME` and `HOME`.

`--seed N` and `--functions N` change the programs. The 9700X job's test step
runs it.

## Snapshot benchmark

`./build/compiler benchmark-snapshot [--directory PATH] [--runs N] [--only
dynamic|static|static-pie|shared]` reports, for a dynamic PIE's runtime, a
static executable's, a static PIE's and a shared library's (`--only` one of
them), medians of `--runs` runs (41 by default) of the import without a
snapshot, `snapshot_write` into a directory under `--directory` (`TMPDIR` or
`/tmp`), `runtime_open` (or `runtime_open_static`, `runtime_open_static_pie`,
`runtime_open_shared`) from the snapshot phase by
phase (the compiler's identity, the mapping, the header and the directory's
hash, the view, the probes' `stat`s), `runtime_release` of a loaded runtime,
the alternatives the design weighed (the snapshot read with `pread` into new
memory, mapped without the hash, and mapped and hashed whole as format 3
checked it), and binding a program of one worker against the imported
runtime, against one loaded afresh each time (as a link binds, its pieces
faulted in and checked), against one loaded once, and the checks alone of the
pieces it read. Every binding must make the same link. Run it under the
9700X's lock with `taskset -c 2`, with `--directory` on each file system to
measure (the cache directory is under `$HOME`, btrfs there).
`docs/object-writer-and-linker.md` ("The runtime snapshot") has the numbers.
The default 9700X job runs it after the tests, pinned to one allowed CPU; like
the others, its measurements are informational.

## Link benchmark

`./build/compiler benchmark-link [--functions LIST [--filler N]] [--seed N]
[--directory PATH] [--cache PATH] [--cpus LIST] [--linkers LIST] [--runs N]
[--only direct|linkers] [--kinds LIST] [--keep]` (about 122 s on the 9700X)
links generated programs that call libc, without `SIZE32` and `SIZE64`, as
dynamic PIEs, static executables, static PIEs and shared libraries (`--kinds`, a
list of `dynamic`, `static`, `static-pie` and `shared`, every kind by default),
by the direct path and by each linker, and reports medians:

- the programs: one worker and 2,000 workers (about hello world's and tcc's
  outputs), and one of synth's shape, 102,400 workers in 512 units with up to
  64 filler instructions each (44 MB); or with `--functions`, a program of
  each count of workers, of up to `--filler` (8) filler instructions each;
- the direct path, on a lane per CPU of `--cpus` (CPU 2): `runtime_open` (or
  `runtime_open_static`, `runtime_open_static_pie`), which must load the
  kind's snapshot in `--cache` (the cache directory), and `bind_begin`; then,
  after the program's atoms, `bind_end` and `dynamic_inputs` (or
  `dynamic_static_inputs`, `dynamic_static_pie_inputs`), and
  `executable_write` (a static PIE's with `ExecutableOptions.dynamic` and
  `.runtime` both set) over the file of the run before into a directory under
  `--directory` (`TMPDIR` or `/tmp`). Each part is the
  median of its runs after a warm-up, each run from the link and binding as
  they were before the part, put back outside the time (a static link's
  COMDAT signatures among them), and must bind the link the first binding
  made (its atoms, symbols and fixups) and write the warm-up's file again (its
  build ID); the link is the parts' sum. A binding that finds a piece of the
  snapshot damaged (`BIND_ERROR_SNAPSHOT`) throws the measurement away: the
  snapshot is discarded and written again, outside any measurement, and the
  program measured again from the start, once. Then its work: the bytes
  written, the fixups applied, the names looked up, and over the warm-ups the
  page faults and how much the peak resident set grew; and the floor, one
  `pwrite` of the output;
- the objects, each unit's as `-c` writes them, on the same lanes;
- each linker of `--linkers` (`ld.bfd`, `ld.lld`, `mold` and `wild`) linking
  them with the appendix's command, the clang driver's with the matched
  options (its `-static` one for a static executable, with
  `--start-group -lgcc -lgcc_eh -lc --end-group`, and its `-static-pie` one
  for a static PIE: `-static -pie --no-dynamic-linker -z text`, `rcrt1.o`,
  `crtbeginS.o` and `crtendS.o`, with the same group), with its own threads
  and on one (pinned to the first of `--cpus`):
  the wall time from `posix_spawn` to the wait's return, over the output of
  the run before, the median of 41 runs after a warm-up with a 20 ms pause
  before each, or past 4 MB of output 11 runs after 200 ms, and GNU ld's 5
  after 500 ms (`--runs N` sets every count). Then one run more without
  forking, through the command itself as a helper (`--rusage`), whose rusage
  gives its page faults and peak resident set: a program spawned from the
  benchmark would have the benchmark's peak resident set as its own when that
  was higher, which Linux carries over at `exec`. The helper's own floor, a
  program that does nothing, is printed first.

Every program written must run and print what its model predicts, but wild's
static executables and static PIEs of a program whose `.tbss` is more aligned
than its `.tdata`, whose TLS segment wild 0.10 starts at `.tdata`'s
alignment, off its own wherever the layout puts it there, so that glibc's
static startup faults: the benchmark doesn't run wild on such a program, and
says so. `--only direct` and `--only linkers` run one side, and
`--keep` leaves the directory.
Run it under the 9700X's lock without `taskset`: it pins its lanes and the
linkers' runs on one thread itself, and the linkers' other runs need every CPU.
`docs/object-writer-and-linker.md` ("The whole link") has the numbers. The
default 9700X job runs it after benchmark-snapshot, with its lane on the job's
first allowed CPU; like the others, its measurements are informational. The
linkers' side, about 121 s of its 122, times other programs on the machine,
the same for every pull request: the job runs it on pushes to main, dispatched
runs and pull requests that change `link_benchmark.c` or `floors_benchmark.c`,
and only the direct path (`--only direct`, about 1 s) on other pull requests.

## Floors benchmark

`./build/compiler benchmark-floors [--directory PATH] [--cpu N] [--only
files|threads|calls|memory|teardown]` (about 26 s on the 9700X's tmpfs)
measures what the machine and the kernel charge a linker however it works,
each figure a median, pinned to `--cpu` (CPU 2):

- files: the output written from memory into `--directory` (`TMPDIR` or
  `/tmp`) at 16 KB, 256 KB, 4 MB, 64 MB and 512 MB, as a new file, with
  `O_TRUNC`, in place, as a temporary file renamed over it, and through a
  shared mapping (new, with `fallocate` and `MADV_HUGEPAGE`, and in place),
  each time with its open and its close, 15 runs, or 5 with a `syncfs` after
  each from 64 MB;
- threads: 64 MB and 512 MB written by 1, 2, 4 and 8 threads on the first
  CPUs this process may use, each a contiguous share, by `pwrite` (new and in
  place) and through a shared mapping, starting the threads outside the time;
- calls: what a linker pays per input file, on this process's `libc.so.6`:
  `open` and `close`, `stat`, `mmap` and `munmap` with and without touching
  every page, and `pread` of the whole file;
- memory: 256 MB of anonymous memory touched for the first time with base
  pages and with `MADV_HUGEPAGE`, and `memcpy`, non-temporal stores and
  `memset` from 256 KB to 256 MB;
- teardown: a child, on another CPU when this process may use one, touches
  64 MB, 1 GB and 4 GB, sends this process the time through a pipe and exits:
  the time from then to `waitpid`'s return, which a linker that forks hides
  from its caller.

It exits with status 1 on invalid arguments and when a call fails: any lane's
write, naming its `errno`, or a child that dies before its message or its exit.

Run it under the 9700X's lock without `taskset`, and with `--directory` on each
file system to measure. `docs/object-writer-and-linker.md` ("The floors") has
the numbers. The default 9700X job runs it after benchmark-link, where that
runs the linkers' side (the machine's floors don't depend on a pull request
either); like the others, its measurements are informational.

## Foreign tests

`./build/compiler test-foreign [--seed N] [--functions N] [--directory PATH]
[--cross-check] [--keep]` (about 2.4 s on the 9700X, and 2.9 s with
`--cross-check`, 1.7 s of each the slow lanes' pauses below) checks
`foreign.c`. Every resolution runs by both paths and with groups of 1, 2, 3
and 8 lanes, with partitions of about 16 records
(`ForeignCommand.partition_records`) so that the cases' few records spread
over every lane, which must describe it byte for byte alike: the loaded
objects, every name's resolution, winner and counts, every symbol's name id
and every COMDAT group's fate.

- programs: `test-link`'s seven sets, each unit's object written by the object
  writer, resolved four ways: every unit an object; the units after the first
  in an archive after it; split between an archive and a thin archive; and an
  archive before the first unit, which only lld's back references load. Every
  name must be defined, but the linker's (`runtime_linker_name`), the weak
  references to nothing and, for the sets that call libc, libc's, which bind
  to `libc.so.6` through the system's `libc.so` script, with the crt objects
  and libgcc where GCC's driver puts them;
- compiled programs: C sources `cc` compiles (and C++ that `g++` compiles at
  `-O0`, for COMDAT groups of inline functions and templates), resolved as
  cases with what each must give (`foreign_test_outcomes`): commons of two
  sizes beside strong and weak definitions; GNU ld's common rule (a common
  symbol loads the member that defines its name as global data, not one that
  defines it as a function, weakly or as another common); weak and strong
  definitions; a name two archives define, in both orders, a member that needs
  an earlier member of its own archive, a thin archive, `--whole-archive` and
  `-u`; back references; a shared library beside an archive in all five orders
  with the object; a duplicate; undefined names; a PIE linked with libc as
  GCC's driver links one; COMDAT groups in objects and archive members,
  among them a member loaded for a later one; a strong reference before an
  archive whose member defines the name and an object after it that defines
  it too, weakly (the member loads and its definition wins) and strongly (a
  duplicate); a shared library that refers to a name an archive after it
  defines, needed and under `--as-needed` (the member loads), and under
  `--as-needed` with nothing needing it (nothing loads); and a thin archive
  that holds another archive's member (GNU ar's `/N:M`);
- with `--cross-check`, which needs `ld.bfd`, `ld.lld`, `cc` and (for the C++
  cases) `g++`: each program and case linked by GNU ld (`--trace` twice, `-Map`
  and `-y`) and by lld (`--why-extract`, `-Map` and `-y`). The members each
  loads (GNU ld names a thin archive's by its path), each winner (its symbol's
  address in the output's `.symtab` within the map's input section of the
  object and section the resolution chose; TLS symbols from `PT_TLS`'s
  address; not symbols of `SHF_MERGE` sections, which linkers merge), each
  group kept exactly when its first section is in the map, and the `-y` lines
  about loaded objects (GNU ld's, every one, for names in two objects or more
  but the linker's; lld's, each one, since it prints a definition only when it
  takes the name over) must be the resolution's, and a link fails exactly when
  the resolution leaves a name undefined or defines one twice. Where a case
  says a linker differs as `foreign.c` documents (GNU ld with back references
  and an archive before a shared library and its object; lld with GNU ld's
  common rule, an as-needed library nothing needs, whose references it
  counts, and a thin archive of another archive's member, which lld 23
  refuses; both for a member loaded for a later one, whose COMDAT group they
  keep), its differences are counted apart and printed as a count;
- malformed inputs, each refused naming its place (the failure's error, input
  and member, and the reader's error, section, symbol, type and offset): a
  compiled object with one field changed (a symbol's binding, type, section
  index, common alignment or name; the symbol table's first global; a
  relocation section's link, target or type), a broken archive member (refused
  only once it loads), a thin archive's missing member, a member that isn't an
  object, a script that doesn't parse, a `-l` name no directory has, a missing
  file, a FIFO (refused, not waited on), an input of four bytes (the ELF
  magic alone) and an unknown option. Each malformed object and each file
  that fails is resolved again on 2 and 3 lanes, each lane in turn pausing
  2 ms at every decision to go on (`ForeignCommand.slow_lane`), so that the
  others go on to the next phase and fail there while it decides: the
  failure must be the same, and a watchdog fails the test if the lanes wait
  for each other for 60 s. Then a group that can't start (in a child whose
  `RLIMIT_NPROC` leaves no room for a thread, which doesn't bind root) must
  fail as `FOREIGN_ERROR_LANES`, with CPUs given and without;
- page edges: inputs given in memory (`ForeignArgument.bytes`) ending at a
  `PROT_NONE` page: every prefix of an object and of an archive, and the object
  with each byte set to 0, 0xff and a random value, each resolved by both paths
  alike or refused alike; a read past the end faults;
- extended sections: an object made by hand of 65,527 sections, its global
  symbols in sections 65522 and 65521 through `SHN_XINDEX` (the values of
  `SHN_COMMON` and `SHN_ABS`) beside an absolute and a common one: each must
  be what its `st_shndx` says, by both paths, and with a second object that
  defines the first strongly too, a duplicate.

The files go in a new directory under `--directory` (`TMPDIR` or `/tmp`),
removed unless `--keep`. `--seed N` changes the generated programs, and
`--functions N` the workers of the set that takes them. The default 9700X job
runs it with `--cross-check`.

## Foreign benchmark

`./build/compiler benchmark-foreign [--functions LIST] [--filler N] [--seed N]
[--lanes LIST] [--cpus LIST] [--runs N] [--directory PATH] [--kinds LIST]
[--linkers] [--keep]` (about 9.3 s on the 9700X with `--linkers`) resolves
`link_program.c`'s programs that call libc, written as objects, 200 workers
per unit: one worker, 2,000, and synth's shape (102,400 workers in 512 units,
up to 64 filler instructions each), or with `--functions` a program of each
count. Each is resolved as GCC's driver links it (`--kinds`: `dynamic`, a PIE
against `libc.so`'s script, libgcc and the crt objects; `static`, against
`libgcc.a`, `libgcc_eh.a` and `libc.a` in a group, whose members the rounds
load; and `members`, the dynamic one with every unit but the first in an
archive), by each path and each lane count of `--lanes` (1 by default), the
lanes pinned to `--cpus` in order. It reports what the resolution did (its
inputs, objects, members, rounds, global symbols, groups, names and
partitions) and medians of `--runs` runs (21) after a warm-up: the whole
resolution and each phase (open, indices, headers, symbols, exchange, resolve,
load, groups and winners), the slowest lane's. With `--linkers`, mold
(`--perf`) and wild (`--time`) link the same command lines on one thread, 11
runs after a warm-up, and the medians of their phases that do the same work
are printed: mold's `read_input_files` and `resolve_symbols`, wild's opening,
loading, resolving and section resolution. Run it under the 9700X's lock with
`taskset -c 2` for one lane, or with `--cpus` for several.
`docs/object-writer-and-linker.md` ("Steps 1 to 5, as built") has the numbers.
The default 9700X job runs it after benchmark-snapshot, pinned to one allowed
CPU: with `--linkers` on pushes to main, dispatched runs and pull requests
that change `foreign_benchmark.c`, since mold's and wild's times don't depend
on a pull request, and without it on other pull requests. Like the others,
its measurements are informational.

## Dataflow tests

`./build/compiler test-dataflow [--seed N] [--count N]` checks the IR-independent
liveness and interference primitives in `dataflow.c`:

- generated CFGs: chains, diamonds, self-loops, nested loops, irreducible
  two-entry loops, several exits, random local components, and connected
  dispatch loops. Every shape runs with 0, 1, 2, 3, 8, 17, 65, 257 and
  2,049 blocks at each of 0, 1, 63, 64, 65, 511, 512, 513, 1,024 and 1,025
  values, then `--count` more seeds and sizes per shape (24 by default). The
  same seed must reproduce the columns and input rows. The postorder must
  visit every block once, and every row must be 64-byte aligned and padded
  to 512 bits;
- the AVX-512 and scalar fixpoints: identical `in` and `out` rows and iteration
  counts, both liveness equations satisfied, and all padding bits zero.
  Solving again over poisoned output must reproduce the result and iteration
  count, including zero padding after the poisoned rerun. Small graphs also
  use an independent per-value reachability check: a value is live when a
  path reaches its use before its definition. Graphs of 257 and 2,049 blocks
  independently check selected boundary and seed-derived values at every
  block with a reverse-reachability oracle;
- handwritten graphs: disconnected cycles, a self-loop without a use, a value
  both used and defined in one block, every combination of the transfer
  function's three input bits, the exact postorder of a chain, and repeated
  successor/self/exit edges compared with a deduplicated graph;
- interference: empty rows and every bit position in rows of 512, 1,024 and
  1,536 bits, with disjoint and intersecting inputs, checked by both paths;
- page edges: each matrix ends at a `PROT_NONE` page for every boundary value
  count above, with zero-size rows pointing at the inaccessible page itself.
  Both solvers and the interference check must stay within the rows, and the
  poisoned bytes before each matrix must remain unchanged;
- failure handling: insufficient arena capacity leaves the arena and result
  unchanged, successful allocations zero the rows, malformed successor
  offsets or targets leave the postorder and scratch arena unchanged, and
  missing scratch memory and an edge in an empty CFG are rejected.

`--seed N` changes the generated inputs (1 by default). The command reports
the check and failure counts and exits with status 1 on any failure. The
9700X job's test step runs it.

## Dataflow benchmark

`./build/compiler benchmark-dataflow [--seed N] [--after-scalar MS]
[--only scalar|avx512]` compares the scalar and AVX-512 fixpoints on four
generated functions: a small diamond CFG with
16 blocks and 127 values, nested loops with 256 blocks and 1,025 values, and
random local components and a connected dispatch loop, each with 4,096 blocks
and 4,097 values. The local-components case is a streaming control, not a model
of a large real function. `--seed N` selects the inputs (1 by default); generator
version 2 mixes the seed before generating topology and rows.

Each workload first checks identical rows and iteration counts, then reports
21 interleaved-pass medians and minimum/maximum ranges with alternating
implementation order. Both paths use the same repetition count, calibrated
with warm batches; every retained sample lasts at least 2 ms. Each timed solve
includes the rows its solver clears; generation, allocation and postorder are
outside the timed region. Both paths use direct calls through matching
`noinline` benchmark wrappers, each starting a 64-byte line, as do the AVX-512
solver's two loops in `dataflow.c` (for one-vector rows and the rest), which
its wrapper jumps to, so that edits to the driver don't move the solvers' loops
within lines (the scalar twin's numbers once depended on it: `docs/dataflow.md`,
"Where the scalar twin's code falls"). The output gives nanoseconds, cycles and instructions
per function, cycles per block per iteration, IPC, and the throttle per block
per iteration (PMCx1A0's undocumented umask 0x08, which #199 reads as the
dispatch slots Zen 5's per-core power cap takes). Hardware counter values
are zero when unavailable. A scalar/vector disagreement fails the command.
Run at least five independent processes, releasing the host lock between
them, when recording a comparison; process-median ranges expose variation
that one process's 21 passes may miss.

The AVX-512 solver is heavy enough for the power cap, so measure it in both
states, as the encoder's benchmark does (#199): `--only avx512` times it alone,
back to back, and `--after-scalar MS` runs complete scalar solves of the
workload, untimed, for MS ms (100 to 300) before every AVX-512 sample, the state
it runs in after a compiler's lighter phases. Without either, its samples follow
the scalar ones in alternate passes and run partly throttled.
`docs/dataflow.md` has the three states' numbers.

Run it under the 9700X's lock with `taskset -c 2`. `docs/dataflow.md` records
the contracts, measurements and implementation choice. The default 9700X job
runs it after the tests, pinned to one allowed CPU; like the other benchmarks,
its measurements are informational. Building use/def rows from IR columns and
running the corpora remain outside this module's current scope and wait for
#131.

## Preprocessor harness tests

`./build/compiler test-preprocessor-fuzz [--fuzz N] [--seed N] [--clang PATH]
[--std c17|c23]` checks the independent tooling for #109. It requires Clang 23
on `PATH`, or the executable selected by `--clang PATH`. The defaults are
64 generated cases, seed 1, `clang` and C17; the 9700X job's test step runs
both C17 and C23. The checks cover:

- deterministic generation, including every one of the 36 construct families
  even when `--fuzz N` is smaller, and repeatability for seeds 1, 7 and 99;
- independent rejection of each invalid `#`/`##` form, with acceptance after
  repairing its sole intentional fault; arithmetic and bitwise predicates
  exercise both branch outcomes, and replacing an expression with zero must
  change the accepted output tokens;
- batched Clang preprocessing against one process per case: the same
  acceptance or rejection, token kinds, spellings and token counts, with
  whitespace ignored. Cases have distinct name prefixes and marker tokens
  delimit their outputs; a rejected batch is rerun one case at a time.
  Accepted cases are also batched separately so rejection fallback cannot
  conceal a batching error;
- isolation of cases, line information and predefined counter behavior.
  `__COUNTER__` cases run alone because its state belongs to the Clang process;
- output comparison, including unequal acceptance, token counts, kinds and
  spellings, whitespace-only changes, and invalid oracle results; and the
  reader of clang's `-dump-tokens` text, on lines as clang 23 writes them
  (escapes, keywords, annotations, and tokens written with line splices, whose
  bytes as written go on to more lines), and the lines it must refuse;
- a planted comparison for the minimizer: delete complete lines, then tokens,
  while preserving the difference, until only the two required identifier
  tokens remain. Saving and reading a `fuzz_failure_N.c` fixture must preserve
  its bytes without overwriting an existing failure; the self-test removes
  its temporary files.

The oracle runs `clang -E -P -undef -nostdinc -std=c17` or `-std=c23` with
`-x c`. It resets each case's source filename and line number identically in
batched and individual runs, and splits the output at its markers, each a
whole identifier that ends its printed line (found by its bytes outside
literals, since a case's output can hold text that doesn't lex back); any
other marker fails the split. A batch's output is tokenized with `c_tokenize`
as the mode lexes. Infrastructure failures are distinct from source
rejection.

This is test tooling outside the compile path and has no new vector path.
These self-tests check the harness alone, with the minimizer on its planted
comparison; `test-preprocessor --fuzz` (below) runs the preprocessor through
it. `docs/preprocessor-fuzz.md` has the harness's contract and its use.

## Preprocessor harness benchmark

`./build/compiler benchmark-preprocessor-fuzz [--clang PATH] [--std c17|c23]
[--seed N]` requires Clang 23 and defaults to `clang`, C17 and seed 1. It
processes 16 generated family-0 cases (object macros and their shared
alias/redefinition scaffold) together, then in separate Clang
processes, checking that every case is accepted and the output byte totals
agree. After one warm-up pass, 21 measured passes alternate which mode runs
first and report median milliseconds per batch and cases per second.

The timing includes generation, launching Clang, temporary-file I/O and
cleanup. It measures the external test harness, not Buster compilation
speed. Run it under the 9700X's lock with `taskset -c 2`; the default 9700X job
runs it after the tests, pinned to one allowed CPU. Measurements are
informational; execution or output-check failures fail the command.

## Refinement tests

`./build/compiler test-refinement [--count N] [--seed N] [path...]` (about
1.3 s without paths) compares `refine_tokens`, with each variant of its batched
interning (`RefineVariant`), with `refine_tokens_scalar` and with an oracle of
its own (splices and UCNs removed in plain passes, and a different table): every
identifier must get the same id and every token the same leading-space bit, on:

- the hash's four forms on spellings of 0 to 200 bytes (the one that hashes
  four spellings at once, up to 32 bytes, with their first lanes);
- 88,064 structured spellings that must all hash differently: repeated and
  swapped 64-byte chunks and 16-byte lanes, and #192's review's 32-byte
  identifiers with paired changes in two lanes;
- about 14,800 spellings where the same bit flipped in two lanes must change
  the hash;
- the predefined names, every name's keyword answer in each of the ten modes
  from `c89` to `gnu23` and in five of them with `-fgnu-keywords` or
  `-fno-gnu-keywords` (#281), and 24 spellings chosen apart from the predefined
  names with clang's answer (17 keywords, 7 not);
- 300,000 random spellings that grow the table three times;
- 22 boundary fixtures (splices between and inside tokens, with `\r\n`, blanks
  and trigraphs; comments; UCNs; identifiers of 62 to 65 bytes), each at all 64
  misalignments from a cache line;
- identifiers of every length from 1 to 130 bytes with a line splice (four
  spellings, one a trigraph's) at every position inside, or a UCN at every
  position, each followed by the same identifier written plainly: 50,570
  inputs;
- 20,000 random inputs (`--count N`) of identifiers, splices, comments,
  digraphs, pp-numbers and other tokens, one in 16 with trigraphs;
- 9,600 prefixes and suffixes of random inputs that end exactly at a readable
  page's end, before a `PROT_NONE` page, or start at a page's start: a read
  outside the source faults, and the test names the input and exits with 1;
- every `.c` and `.h` file under each path. On the 9700X, `src` and the C
  library corpus (`~/azp-bench/c-tokenizer/corpus/libc`) pass.

`--seed N` picks another random sequence. The 9700X job's test step runs it on
`src`.

## Refinement benchmark

`./build/compiler benchmark-refinement [--cplusplus] [--trigraphs]
[--after-scalar MS] [--only NAME] path...` lexes every `.c` and `.h` file once,
checks every implementation against `refine_tokens_scalar`, and reports the
share of identifiers the table finds and how often the vectorized path takes a
scalar one. Then come 21 interleaved passes, each from a table reset to the
predefined names, with the medians per token and per identifier (time, cycles,
instructions, IPC) of the variants (`refine_tokens_16`, `_32`, `_64` and
`_fused`; `refine_tokens` is the 64 one), the scalar path, `c_tokenize` and
`c_tokenize` then refinement per file. `--only NAME` runs one of them, its
passes back to back, and `--after-scalar MS` runs the scalar path over the
corpus for MS ms (100 to 300) before each timed pass of a vectorized one: the
state after lighter code, in which Zen 5's power cap slows a heavy AVX-512 loop
(#199, as the encoder's benchmark does). The columns, table and source stay in
the caches from pass to pass, unlike the preprocessor's, whose refine phase is
what #115's budget measures. `docs/c-refinement.md` has the numbers for
`sqlite3.c`, the C library corpus and `src`. Run it pinned to one CPU; the
default 9700X job runs it on `src`, and like the others its measurements are
informational.

## Preprocessor tests

`./build/compiler test-preprocessor --compare-clang [--clang PATH] [options]
corpus...` is #125's real-source oracle. It accepts directories or
`--files-from LIST --root DIR`, compares lexical kinds, spellings and locations,
and reports every Clang rejection as an explicit skip. Process/configuration
failures fail the run. `--report NEW.tsv` records every file's result and skip
reason. It refuses `--buster-headers`: both sides search as clang does, which
leaves ours no headers of its own to select. `c_preprocessor_oracle.c`
implements this interface;
[the frozen oracle guide](docs/preprocessor-oracle.md) records the pinned
headers/corpora, manifest identities, CI subset and on-demand commands.

`./build/compiler test-preprocessor [--clang] [--keep] [--seed N] [--count N]
[options] [file...]`, or `test-preprocessor --tables path...`:

- without files, the 320 built-in cases (`pp_test_cases`), each preprocessed by
  the vectorized kernels and their scalar twins. They must give the same stream
  token for token (kind, flags, value and location) and the same directive
  tables, with the expected spellings and the expected number of warnings. The
  cases are of our own:
  - on the topics of the C standard's examples;
  - the behaviors probed against GCC and clang;
  - inputs past the vectorized kernels' limits;
  - directives and builtins that nest through the main loop's frames;
  - #193's review fixes, line markers among them;
  - the `-f` options it models (#281): GNU's keywords, `$` (what follows it
    joining the tokens after it, as clang lexes them), char's signedness and
    `-f[no-]short-wchar`'s width and signedness in `#if`;
  - c89's and C94's lexing without `//` comments, and c89's without digraphs,
    pasted and stringized too, and `-f[no-]digraphs` in four modes (#288);
  - each difference from clang that #109's fuzzing found, minimized, and the
    ones probing around it found;
  - `#embed`: its data, parameters, errors and files, and where `-E` prints it;
  - #119's includes, in files and links the case makes: header names written out
    and computed, `#include_next` through three directories, an inactive group's
    includes, a cycle at the depth limit, one file through a symbolic link and a
    hard link, and clang's recoveries and diagnostics' places; and #254's
    review's: a header name written out that is too long for a path (and an
    `#embed` one longer than its buffer), `__has_include`'s and `__has_embed`'s
    operands as their `#if` line writes them, and the header the depth limit
    refuses.

  A case can also give extra files and options for both sides, and the exact
  text `-E` prints with line markers. Then `__TIMESTAMP__` in two files with
  modification times of their own must give each file's time (in UTC), and
  `SOURCE_DATE_EPOCH`'s when it is set; the `-m` options buster refuses
  though clang takes them (another target's, which clang ignores with a
  warning, and those that change the target) must be refused, and each `-m`
  flag that changes no macro taken; and each of `pp_test_option_sets` must be
  refused, its message naming the option, or taken without one: the `-f`
  options that change clang's keywords, lexing or `-E` output, which
  `pp_parse_options` models or refuses where their family's last option turns
  them on (docs/c-preprocessor.md, "The `-f` options"); the expansion history's checks; #embed's vector/scalar text and lookup checks; and
  the fuzz batch: its isolation rules checked on small sources (what a case
  leaves open makes clang read it alone), its comparison on pairs of texts
  (whitespace between tokens doesn't count, a literal's contents and joined
  tokens do, and our tokens that don't lex back, as an unterminated literal,
  end where ours do in both texts), with `--clang` its emulation of clang's
  -P printer checked on fixtures of its own, then 1,500 cases of the
  independent harness's generator in each of C17 and C23
  (`c_preprocessor_fuzz.c`), which both kernels must give
  alike (streams, directive tables, diagnostics' counts, history and both
  printers' bytes), with -P text that lexes back into the stream. Then the directive tables (#118,
  `c_preprocessor_directives_test.c`): 2,000 random sources (`--count N`,
  `--seed N`) of directives, `%:`, `#` alone, line markers, unknown names,
  `#`s inside lines, literals and comments and after line splices, and
  conditionals nested to many levels, balanced and not, and nests past the
  vectorized pairing's depth limit, each prepared by both kernels, whose tables
  (directives, line ends, kinds, pairing, notes and include guard) must be the
  oracle's: a walk of the tokens one at a time, of the test's own. And 17
  fixtures of a file's conditional structure (`#else` and `#elif` after
  `#else`, a directive without `#if`, groups left open, in skipped groups and in
  an included file, extra tokens, `#elifdef` before C23), whose diagnostics
  both kernels must give alike, and ten of `#define`'s and `#undef`'s
  constraints and redefinitions (#121). And 20 fixtures of include guards and
  `#pragma once` (#120): the guard's shapes clang's multiple-include
  optimization takes and those it doesn't, `#undef` between inclusions, an
  inclusion from inside the file, the macro defined before the first, links to
  a `#pragma once` file, `#import`. Each run must skip as many inclusions by
  guards and by `#pragma once` as expected, by both kernels. Then a location
  space shrunk to end where an inclusion, and then the first pasted spelling,
  would start (`--location-limit N`, #124): both kernels must stop there, with
  the output before it, and report the inclusion at its `#include` and the
  spelling without a place. And `test-preprocessor` with a file and
  `benchmark-preprocessor` (with `--clang`, and `--compare-clang`), run as
  children on a small file, must take `--location-limit N` with its value as
  ours, kept from clang, succeeding in a space the file fits in and running
  out of one of a byte;
- with files, each file with `-E`'s options (`-I`, `-isystem`, `-D`, `-std=`,
  `-ffreestanding`, `-target TRIPLE`, ...), by both kernels, which must agree;
- with `--tables`, the directive tables of every `.c` and `.h` file under each
  path, by both kernels, against the oracle (about 0.02 s for `src`, 0.6 s for
  the C library corpus).

In both, the vectorized printer's text must be the scalar printer's byte for
byte, with line markers and without, and must lex back into the stream's
spellings.

Every case and file runs with clang's answers to the feature queries
(`--clang-features`, the oracle's mode), and a check runs a line of queries both
ways: by default they answer what buster implements.

`--clang` also compares every case and file with `clang -E` and the same
options (but `--predefines` and `--location-limit`, ours alone), token spelling by token spelling, and then where clang's line markers
put each token (its presumed line and include stack) with where ours put it. It
prints the first difference with the tokens around it. For files, our side
reads clang's own predefines for the options through `--predefines`, and clang
gets `--buster-headers`, which it hasn't, as ours' whole search
(`pp_clang_arguments`): after `-nobuiltininc`, each directory ours keeps, by
identity and a system directory over a user's (`pp_dedupe_directories`), with
its option, and in clang's resource directory's place a directory of links to
its headers but buster's; or nothing with `-nostdinc` or `-nobuiltininc`. The
files it finds through the links are compared under their names in clang's
resource directory, escaped as clang writes them, as ours names them. The macros at the file's end also compare against `clang -dM -E` with our own predefines. Without
files, `--clang` also checks the built-in predefines, the keywords and the
features:
- in 26 option sets (the ten modes from `c89` to `gnu23`, C94 as
  `-std=iso9899:199409` and as `-std=c89 -fdigraphs`, C94 and `gnu89` without
  digraphs, `-ffreestanding`, `-O2`, two combinations, the six ISA profiles of
  `docs/x86-target.md` and two with `-mtune`), `pp_predefines` must give
  `clang -dM -E` line for line, but for the patch release's three macros;
- `c_predefines.c`'s generated tables (each x86-64 feature's flag, closures and
  macros, each `-march` CPU's macro name and features, the `-mtune` names) must
  be what `c_predefines_test.c`'s generator learns from clang now;
- about 2,200 option sets must give `clang -dM -E`'s macros, and be accepted or
  refused alike: every `-march` CPU and `native`, every feature flag on and off
  on x86-64 and on the CPUs with the most features, each `-O`, `-f`, `-g` and
  `-m` option `c_predefines.c` reads, alone and in the orders that matter (an
  invalid value before the last of an option clang reads only the last of among
  them), each `-m` option that changes no macro (each flag alone, the hardening
  flags against each other, and each value option's rule at its edges), each of
  clang's `-m` aliases alone and against its option in either order, `-m`
  options clang refuses too (`-mavx512`), the 16 sanitizers with a macro or in clang's incompatible pairs, alone,
  in pairs and in pairs with either one turned off after them, each of them and
  each other group with `-fno-sanitize=all` after it and before it, and 400
  random combinations, some with those `-m` flags and aliases (`--seed N`
  changes them);
- `-E -dM` of a file with every kind of definition must be clang's byte for byte
  (but `__BUSTER__`), and `__FLT_EVAL_METHOD__` clang's under
  `-ffp-eval-method=` and without SSE;
- in each mode (the ten and C94), `__is_identifier` of every predefined name, and of clang's
  keyword candidates chosen apart from them (`pp_test_keyword_candidates`),
  must give clang's answer, and so must it in 25 sets of `-f` options
  (`pp_test_keyword_sets`: GNU's keywords on and off, in each order of
  `-f[no-]gnu-keywords` and `-f[no-]asm`, and the refused families turned off
  again);
- each refused option set must change clang's `-E` of a probe (from the set's
  `-std=` alone, or no options), and each taken one must give clang's tokens;
- in each mode, `__has_feature` and `__has_extension` of every name in clang
  23's `Features.def` must give clang's answers, including 45 sanitizer option sets and target queries;
- buster's own headers (`src/buster/include`) against clang's, for each header,
  8 modes, hosted and freestanding (176 sets): with each set, clang's run must
  define the same macros (but the guards and helpers each names its own way),
  expand each to the same tokens, make the same declarations and give the same
  diagnostics; and our preprocessor with `--buster-headers` must give clang's
  tokens on all of them, with no warnings from the headers. Then, in each mode,
  hosted and freestanding, two units (32) include them with the C library's
  headers that define some of their macros or ask them for parts and clang's
  headers they don't supply (`<immintrin.h>` and others), buster's last and
  first: clang with them, searched as `--buster-headers` searches
  (`-Wsystem-headers`), must warn about nothing in the headers and expand every
  macro as clang's own headers do, and our preprocessor must give its tokens,
  with no warnings from the headers; `-nobuiltininc` and `-nostdinc` must
  leave out buster's headers and clang's after them alike; a quoted name
  beside one of clang's headers must come from buster's when buster's has it;
  the files' comparison with `--buster-headers` must agree with clang on a
  unit of buster's and clang's headers (`<inttypes.h>`, which reaches buster's
  `<stdint.h>` through glibc's) and an `-isystem` directory's `<limits.h>`
  given after the flag, also with clang's links in directories whose names it
  escapes (a quote, a backslash, a byte past ASCII), on `__has_include` with
  the flag alone, with `-nobuiltininc` and with `-nostdinc`, and with clang's
  resource directory and buster's given again as `-I`, `-isystem` and
  `-idirafter` (13 ways, through a symbolic link, a trailing slash and `/./`
  too); and the benchmark's clang command, with 300 `-D` options and the
  search, must reach clang whole;
- the fuzz batch's cases must give clang's acceptance, and an accepted case's
  stream, printed with clang's -P spacing, must lex into clang's tokens (with
  a comment's `/` and a line splice's `\` as tokens, which only tokens printed
  next to each other make, and our tokens that don't lex back whole:
  `preprocessor_fuzz_lex_printed`). Where clang's text can't show where such a
  token ends (an unterminated literal off a pragma's line), clang's own tokens
  for the case alone (`clang -Xclang -dump-tokens`) must confirm ours; on a
  pragma's line that goes on past one (`#pragma omp` expands its tokens),
  which clang's tokens don't have, the case counts as unverified; a
  comparison that can't be made leaves its case without a result. With
  `--clang`, the batch first checks these on fixtures of its own, among them
  #263's second review's probes.

- the structure and definition fixtures' diagnostics must be clang's, byte for byte, and the
  guard fixtures' output and skips by guards clang's (`-Xclang -print-stats`),
  as every file's skips by guards must be. `sqlite3.c`, musl 1.2.5's 1,357 sources,
Lua 5.4.8 and this repository's translation units are token-identical;
`docs/c-preprocessor.md` has the configurations. The 9700X job's test step runs the cases
and the repository's translation units with `--clang`, and `--tables src`.

`sqlite3.c`, musl 1.2.5's 1,357 sources, Lua 5.4.8 and this repository's
translation units are token-identical; `docs/c-preprocessor.md` has the
configurations. The 9700X job runs the cases and the repository's translation
units with `--clang`.

The fuzz batch's options: `--fuzz N` cases per mode (0 for none), `--first N`
the first case's id, `--seed N` (1 by default), `--std c17|c23` one mode,
`--jobs N` worker processes (1 by default; the cases don't depend on it), and
`--print-case N`, which prints case N as clang reads it alone. Each
difference from clang is confirmed on the case alone; the first 4 that each
worker finds in a mode are minimized and saved as `fuzz_failure_N.c` in the
current directory. One block of 64 cases in 128 has every case read alone
too, which must give its batch's result, and a batch clang gives no result
for has its cases read alone. It exits with status 1 on any difference, a
batch that differs from its cases alone, or a case without a result. For a
qualification run (a million cases in each mode take about 7 minutes on the
9700X's 15 threads, and 15 to 24 on a 7940HS's 16, mostly clang's processes):

```sh
./build/compiler test-preprocessor --clang --fuzz 1000000 --seed 11 --jobs 16
```

`docs/preprocessor-fuzz.md` has the comparison's rules, the generator's
constructs and the qualification runs.
`./build/compiler test-preprocessor --generate-targets` prints `c_predefines.c`'s
generated block from clang 23 (on `PATH`), to paste over the old one when clang
changes; `docs/c-preprocessor.md` ("The predefined macros") has the procedure.

## Preprocessor benchmark

`./build/compiler benchmark-preprocessor [--clang] [--only vectorized|scalar]
[--read-limit N,...] [options] file...` preprocesses each file from nothing per
run (a new intern table and preprocessor), with the vectorized kernels and their
scalar twins, 21 passes after a warm-up, and reports the medians of time, cycles
and instructions per input token and per output token, IPC, each phase's share
and time (read, lex, refine, directive tables, main loop), the release after the
run (the files' mappings among it), `-E -P`'s throughput with its printing
(input bytes and output tokens per second, #124) and what a run did
(directives, runs, expansions, invocations, pastes, the `#if` lines and their
lengths, and the inclusions skipped by guards and by `#pragma once`).
`--read-limit` gives a list of the largest files a run reads rather than maps
(16 KB by default; 0 maps every file), each run in turn in every pass, which
must all give the same output.
`--clang` also times `clang -E -P` with the same options, a process per file, with `--buster-headers` translated to ours' search and every argument passed (`pp_clang_preprocess`). A failed clang run fails the benchmark and reports no timing.
Our predefines follow the target options as clang's do; `--predefines FILE`
still replaces them (the benchmark leaves it out of clang's command line). The
runs answer the feature queries as clang does (`--clang-features`), so that they
do the work `clang -E` does. The
default 9700X job runs the smoke command above on `main.c`, pinned to one allowed
CPU; like the others, its measurements are informational.

`./build/compiler benchmark-preprocessor --defines [--only vectorized|scalar]
path...` times definitions and lookups alone (#121): every `.c` and `.h` file
under the paths is read and prepared once, then each of 21 passes after a
warm-up, file by file, defines its `#define`s, compiles its macros and looks up
its identifiers as the main loop's runs test them, by each kernel, which must
make the same macros. It reports the medians in cycles per `#define`, per macro
compiled and per lookup. `docs/c-preprocessor.md` has the numbers for the C
library corpus.

`--guard-cost` times the vectorized kernels with the skip of guarded files and
without it (`PPOptions.no_guard_skip`), in place of the scalar twins: the
output's tokens must be the same (#120).

`./build/compiler benchmark-preprocessor --tables [--only vectorized|scalar]
path...` times the directive tables alone (#118): every `.c` and `.h` file under
the paths is read and prepared once, then each of 21 passes after a warm-up
builds every file's table by each kernel, whose tables must be the same, and it
reports the medians per input token and per directive, IPC, and the scan's
share. Run it under the 9700X's lock with `taskset -c 2`;
`docs/c-preprocessor.md` has the numbers for the C library corpus. The default
9700X job runs it on `src`, pinned to one allowed CPU.

`./build/compiler benchmark-preprocessor --embed N` measures instead the text
`-E` prints for an `#embed`'s N bytes (random, from a fixed seed), by
`pp_embed_text_vector` and its scalar twin in the printers' pieces, 21 passes
after a warm-up: medians per input byte, and the text's throughput. The two
texts must be the same. Run it pinned to one CPU; `docs/c-preprocessor.md` has
the 9700X's numbers.

## Per-file jobs tests

`./build/compiler test-files [--lanes LIST] [--seed N] [--id-order N] [--keep]
[--flat] [options] [path...]` (about 0.7 s without paths) checks `c_files.c`.
Every check fills a file cache on each lane count of `--lanes` (1, 2, 3 and 8),
each time with a new intern table and cache, and must give the first lane
count's results byte for byte: the shared table's ids and spellings, every
record's tokens, values, leading-space bits, directive table and guard, what the
walk did, and each translation unit's output stream (kinds, flags, values and
locations), the text `-E` prints for it with and without line markers and its
diagnostics when it preprocesses with the cache. One lane refines straight into
the shared table, so it is the merge's scalar twin. Each unit's spellings and
diagnostics must also be those it gives with a cache of its own, with as many
errors and warnings, and nothing may be written to stderr while the cache is
filled.

Then every check runs again on each lane count with the shared table's ids
handed out in another order (#153: ids are opaque, and nothing may depend on
their order), as `test-preprocessor --id-order` does for its cases. Before the
walk fills the cache, the table is primed with 17 to 47 names no file has, then
with every other name of the first run's table, in reverse from a place the
seed picks (`--id-order N`, 153 by default, plus the lane count's place in the
list), so that the walk finds those in the table and gives the others ids after
them; every lane prepares every round, so that on more than one lane the merge
does. The predefined names keep their fixed ids. The first run's other names
must all be in the table, which may hold no others, and both those primed and
those added after them must have changed ids. Every unit must give the first
run's `-E` text (both modes) and diagnostics byte for byte, and its stream's
kinds, flags and locations, each identifier's and keyword's id spelled as the
first run's; every record must be the first run's, each identifier's value and
the guard spelled the same, and the walk must do what it did:

- the tree: a directory of the test's own (under `$TMPDIR` or `/tmp`) with
  three translation units and the headers they include, by every kind of
  include the walk resolves: the includer's directory, `-I` and `-isystem`, an
  `#include_next` chain through three directories, a header reached by four
  paths (a symbolic link and `..` among them), a hard link to a header a
  computed include names, guarded and `#pragma once` headers included twice, a
  header under `#if 0` that exists and one that doesn't, and a missing header
  in an active line; and two warnings that name identifiers: an `#if` in a
  header two units include, whose notes name the macros it was expanded from,
  and a macro redefined. The counts are exact: 13 files prepared (each identity
  once) in 4 rounds, 1 unused, 2 requests not found (no diagnostic), the units'
  errors and warnings, and their system calls after the walk (only the
  computed include's). It runs with the vectorized kernels and with their
  scalar twins, cut into parts and split once, on every lane
  (`FilesRun.lane_bytes` 0);
- spellings: 240 generated files of identifiers (a shared vocabulary drawn
  unevenly, names of their own, long names, line splices and UCNs) prepared
  without following includes, 165,562 ids, so that the shared table grows in a
  merge, and a translation unit that includes them all: in parts, split once,
  and with two lanes working, and in two runs into one cache, which must equal
  one run on one lane;
- contexts: a guarded header that includes itself through `./` and `sub/..`,
  which the walk must take once (an alias of a file in the same context, by
  its directory's device and inode, isn't walked again), and one header in two
  directories (a hard link) whose own `local.h` differ, walked in both;
- dollars: a cache filled with `$` as punctuation (`-fno-dollars-in-identifiers`,
  #281), whose unit must split `aa$bb` into three tokens on every lane count;
- special paths: a FIFO that an `#include` under `#if 0` names (the walk's
  probe mustn't block) and an active one names (an error, as alone), and a
  quoted name of 2,400 bytes in a directory of 2,539, three requests of which
  must share one search;
- memory: runs on 2 and 3 lanes short of the round's arena or of one lane's
  scratch memory, at sizes that fail the walk's, the merge's and a file's
  allocations: every unit must give what it gives alone;
- with paths, their `.c` and `.h` files as translation units with `-E`'s
  options (`-I`, `-isystem`, `-D`, `--predefines`, `-std=`, ...), all the walk's
  roots; with `--flat`, only prepared (a corpus such as the C library's).

The checks that would hang if they regressed run under a 60-second alarm, which
fails the test.

`--seed N` changes the spellings, `--id-order N` the ids' order, and `--keep`
leaves the directory. On the 9700X, `sqlite3.c`, musl 1.2.5's 1,357 sources and
the C library corpus (`--flat`) pass too, and Lua 5.4.8 did before the ids'
order and the units' `-E` text and diagnostics were checked.
The 9700X job's test step runs it, on `src` with `--flat`, and on the
repository's translation units.

## Per-file jobs benchmark

`./build/compiler benchmark-files [--lanes LIST] [--cpus LIST] [--runs N]
[--spin MS] [--flat] [--bytes map|read|populate] [--read-limit N]
[--lane-bytes N] [options] path...` fills a cache from the paths (with
`--flat`, without following their includes) on each lane count of `--lanes`
(1 to 8), lane i pinned to the i-th CPU of `--cpus` (the allowed CPUs, in
order), every run from nothing after `--spin` ms (2) of spinning on every lane.
`--read-limit` sets the largest file the lanes read rather than map (16 KB),
`--bytes` how lanes map the larger ones (or read them), and `--lane-bytes` a
round's bytes per working lane (4 MB). It reports medians of `--runs` (21):
the run's time, speedup and time to the roots' round, the tokens per second,
each phase on the lane that spent the most on it (preparing, the merge's
lookups, exchange, additions and rewrite, the include lines, the searches, the
work one lane does alone, and the waits), how busy the lanes were, what a run
did, and without `--flat` the files the walk prepared that no unit entered,
with the largest. Run it under the 9700X's lock without `taskset`: it pins its
lanes. `docs/c-files.md` has the numbers for the C library corpus, `sqlite3.c`,
musl and this repository. The default 9700X job runs it on the repository's
translation units; like the others, its measurements are informational.

## Parser tests

`./build/compiler test-parser [--clang [PATH]] [--mutations N] [--seed N]
[--streams N] [--reports N] [--keep] [options] [file or directory...]`
(about 7 s on the 9700X without files, with `--clang`) checks `c_parser.c`; `--clang` needs
clang 23 on `PATH`, or the one PATH names. Each unit goes through both paths,
the vector path with its fallback and the scalar parser alone, which must read
the same tokens and give the same outcome, and on a unit without errors the same
tree: the same parent, node depth and role on every token. The tree must keep
its invariants (every parent another token or the root, at no greater depth;
every opener's parent its closer, after it; no cycle), and its postorder and
subtree sizes must be a depth-first walk's, whose in-order walk gives the tokens
back in order. Then each kernel against its scalar twin, from the twins'
outputs before it: K1 (depths, enclosing openers and each block's view), K3's
classification, K2 (roles, flags, typed states and statement contexts, also
from random roles and with every block in the twin's loop), K1's second part,
K4 (with the stack's top in registers and with windows of 1, 3 and 8 lanes),
K5, and K6 (flags and classes, also with every second and every third block
left to the twin); and the twins together with the fallback, whose tree must be
the scalar parser's. Those checks put every array in an arena followed by 4 GB
and its own size of address space that may not be touched, so that a load past
an array, or through a 32-bit index that wrapped below 0, faults. On:

- snippets: 202 of valid and invalid code, each with the first error clang gives
  (or none), which the parser must give at the same line and column with the
  same message; `--clang` checks each expectation against clang too, which
  must be a parse issue but for the snippets marked as the errors clang's
  semantic analysis gives where its parser took a declaration as ours does (an
  unknown type name, a missing type specifier, a type name where an operand
  would be), or as code only its semantic analysis rejects, which the parser
  takes; and the oracle itself: clang given an option it doesn't know must
  count as no answer, and an error in a header a file includes must reject the
  file;
- fixtures: four trees checked by hand, parent by parent, among them #127's
  comma case (`f(a ? b, c : d, e)` has two arguments), its cast case (`(T) * p`
  is a cast where `T` names a type and a product where it doesn't) and a
  declarator's spine (`(*a[3])(double)`: array, pointer, function);
- generated inputs, without files: `--streams N` random streams of C's tokens
  (2,000; half with balanced brackets), the snippets after 0 to 63 file-scope
  `;`, at every block alignment, and nine kinds of nesting 100,000 levels deep
  (commas in nested ternaries among them) and a run of 100,000 attributes, which
  mustn't grow the native stack, nor make a token cost more for what is open
  around it;
- the driver, without files: eleven cases' exit statuses and exact messages (a
  valid, an empty and an invalid file, the last with `--scalar` too, an
  `#error`, a missing header, a missing file, a directory, a sparse file past
  the preprocessor's size limit, no file and an unknown option), 16 runs of
  each, after which the address space and the descriptors must be as they were;
  then every reservation and mapping of a run on a valid and on an invalid file
  failed in turn (74 and 75), in children whose address space may grow a page
  less than a run reached: the last fails, then, a page below the peak that run
  reached, the one before it, down to the first. Each run must exit with status
  1 having said why in a fatal error, and leave nothing behind;
- files: every `.c` file under each path, preprocessed with -E's options, must
  parse; with `--clang`, the parser must accept exactly what `clang
  -fsyntax-only` with the same options accepts (our side reads clang's
  predefines for target options from `--predefines`);
- mutations (`--mutations N`, with files): N copies of the files' -E text with
  a token deleted, duplicated or swapped with the next. With `--clang`, a copy
  clang accepts must parse, and one whose first error is a parse issue must fail
  with its first error on the same line; without, both paths must agree as on
  any file.

`--explain` tallies instead why the checks sent valid code to the scalar parser,
by signature, with an example of each, and `--print-tree FILE [FIRST LAST]`
prints each token's role, node depth and parent by both paths, with the
differences marked. `--seed N` changes the random inputs and the mutations,
`--reports N` how many differences are printed (200), and `--keep` keeps the
files in the directory the command makes under `$TMPDIR` (or `/tmp`). On the
9700X, `sqlite3.c`, the 733 files of the libc corpus that clang accepts and the
repository's translation units pass with `--clang`, and so does Lua 5.4.8
locally; `docs/c-parser.md` has the corpora and the mutations' counts. The 9700X job runs
it without files and on the repository's translation units, both with `--clang`.

## Parser benchmark

`./build/compiler benchmark-parser [--passes N] [--only NAME|stages] [--repeat N]
[--counters pipeline|branches|memory|stalls] [options] file...` preprocesses
each file with -E's options and turns its tokens into the parser's outside the
timing, checks that the vector path gives the scalar parser's outcome and tree,
and times `--passes` interleaved passes (21) after a warm-up: the whole paths
over every file (the input's conversion and its twin, the scalar parser, the
twins together and the vector path), then each stage per file on shared arrays,
after the stages before it (each kernel and its twin, K4's three loops, the
fallback, and the postorder by its branch-free passes and by a walk). It reports
medians per token of time, cycles (with the fastest and slowest pass) and
instructions, the events of the `--counters` set, and the share of top-level
declarations and tokens the fallback took. `--only` times one path or stage
(`stages` for every stage), and a stage named alone runs `--repeat` times per
file, for perf. Run it under the 9700X's lock with `taskset -c 2`;
`docs/c-parser.md` has the numbers for `sqlite3.c`, the libc corpus and `main.c`.
The default 9700X job runs it on `main.c`, pinned to one allowed CPU; like the
others, its measurements are informational.

## Scoped binding kernels

`./build/compiler test-bindings [--seed N]` checks the standalone scoped C
binding kernels: stack replay and the vector batch against an independent
scope oracle, 100,000-level nesting, protected-page tails and allocation
failure. `benchmark-bindings [--events N] [--names N] [--only scalar|batch|sort]
[--after-scalar MS]` measures synthetic events under the 9700X lock, pinned to
CPU 2. The parser adapter and corpus acceptance for #128 remain pending;
[docs/c-bindings.md](docs/c-bindings.md) describes the contract and boundaries.

## Type tests

`./build/compiler test-types [--seed N] [--count N] [--clang PATH] [--keep]`
(about 0.2 s on the 9700X) checks `c_types.c`. It needs clang 23 on `PATH`, or
the one `--clang` names:

- builtins: every builtin type's kind, code, size and alignment, from a table
  of clang 23's values of its own;
- validation: each request clang refuses, with the `TypeError` it gets, and the
  edge cases it accepts; and normalization: the spellings of one type that must
  get one `TypeId` (a qualified array and an array of qualified elements,
  qualified parameters, an aligned typedef of one, or of its type's own
  alignment), and those that must not;
- hash-consing: random constructions through every constructor, compared pair
  by pair with a structural comparison of the test's own, which writes the
  normalizations again from C's rules: equal structures get equal ids, and
  different ones never do;
- batches: random requests, some invalid, given one at a time and in random
  slices of `type_derive_batch` to two tables, which must end up identical
  (results, columns and index), with the index grown well past its first size;
  then aligned typedefs' requests of a tag completed since their wrapper was
  made, through both;
- layout: clang's corner cases, then random records of every member type,
  bit-fields of every width next to every type, packed, packs, aligned,
  nesting, anonymous members and flexible array members, with member counts
  around the vector layout's blocks of 16 and inputs past its limits, laid out
  by the scalar layout in one table and by `type_layout_records` in another:
  identical offsets, sizes, alignments and completion order; and member lookup
  of a fresh table's first member, directly and through an anonymous member;
- conversions: every pair of codes through both vector kernels and the scalar
  twin, at every length to 200 and ending at a `PROT_NONE` page; the TypeId
  batch against `type_usual_arithmetic`; and clang's results as fixtures;
- compatibility, composite types and printing: fixtures, the C standard's
  examples among them, types that share their insides 64 levels deep (2^64
  leaves), which each walk must take pair by distinct pair, composites of
  chains of 65,536 pointers, whose canonical comparisons must share one memo,
  and the walks with every amount of room to 16 KB left in either scratch
  arena, which must answer right or say they couldn't;
- clang as the oracle: a world of random types, records and enums (`--count`
  sets its size), written as C with a typedef per type built from earlier
  ones. `clang -std=gnu17 -fsyntax-only` must accept its `_Static_assert`s of
  every size, alignment and member offset, of compatibility and of enums'
  underlying types; the same file compiled and run checks every bit-field's
  bits; and clang's AST dump gives its spelling of every type, usual
  arithmetic conversion, promotion, bit-field promotion and composite type,
  which `type_print`'s must equal.

The files go in a new directory under `$TMPDIR` (or `/tmp`), removed unless
`--keep`. `--seed N` changes every random input. The 9700X job's test step
runs it.

## Type benchmark

`./build/compiler benchmark-types [--seed N] [--requests N] [--records N]
[--pairs N] [--only construction|layout|conversions]` checks that each vector
path agrees with its scalar twin, then reports 21 interleaved-pass medians per
item (time, cycles, instructions, IPC):

- construction: 65,536 derivation requests over a working set of derived
  types, every one a hit and then 1 in 8 a miss into a fresh table, one at a
  time (`type_derive`) and as a batch (`type_derive_batch`);
- layout: 4,096 records of 0 to 24 members laid out one at a time as they were
  defined, and in levels of nesting by the scalar layout and by the vector one
  (`type_layout_records`);
- conversions: 65,536 pairs of arithmetic codes, drawn as a C program's
  operands are, through the scalar twin and the `vpermb` and `vpermi2b`
  kernels; the promotions; and the same pairs as TypeIds through
  `type_usual_arithmetic_types` and `type_usual_arithmetic`.

Run it under the 9700X's lock with `taskset -c 2`. `docs/c-types.md` has the
numbers and the layout designs the vector one was chosen over. The default
9700X job runs it after the tests, pinned to one allowed CPU; like the
others, its measurements are informational.

## Calling convention tests

`./build/compiler test-calling-convention [--seed N] [--count N]
[--cross-check] [--signatures N] [--clang PATH] [--gcc PATH] [--keep]`
(about 0.05 s on the 9700X, and 4.8 s with `--cross-check`) checks
`calling_convention.c`, by its vector paths and their twins at 16-, 32- and
64-byte vectors:

- merge: clang's merge and post-merge, written again as clang writes them,
  against the module's 64-entry merge table and post-merge, on every pair of
  classes;
- fixtures: types whose classes, pass bits and results clang's IR shows
  (scalars, vectors, complex types, `_BitInt`s, records with unaligned and
  zero-size members, bit-fields, flexible array members, `_Atomic` and
  `nullptr_t` members, `i128` storage, which LLVM never splits, whatever the
  high eightbyte under an INTEGER low one and refused under another, runs of
  bit-fields that are an `i128` and that aren't, unions whose storage member a
  zero-length array of a larger record decides (each of the walk's rules alone
  among them), and the narrow low parts clang widens into a general register;
  and the `_Atomic` types whose LLVM types decide: of types of no size, which
  LLVM makes larger (to 32 KB, and refused past the alignment the module
  keeps), as the records ending in one, with their slots, the records it lays
  out otherwise than C, and vectors LLVM keeps whole, splits or, of bools,
  passes as integers; and the vectors that aren't `_Atomic` which LLVM breaks
  up or packs: of bools, with their integer storage and layout, refused from
  16 bytes up where clang passes them as `<N x i1>`, with the records of one,
  and smaller copies past 128 (`SMALLER`), of `_BitInt`s narrower than a byte,
  of long doubles (refused in registers, st0 and st1 as results), of
  `__float128`s, of `_BitInt`s of 128 bits and more, and of lengths that
  aren't a power of two in memory, returned in rax, rdx and rcx, in xmm0, xmm1
  and st0, or through a hidden pointer), and calls whose locations clang and
  LLVM give (an `UNCOUNTED` argument that splits the next record, an
  `__int128` without two registers left, a hidden pointer, a record of a
  flexible array member alone as an `i0`, a struct whose narrow low part takes
  a general register, unions stored as an `i128`, which clang counts as two
  registers with a NO_CLASS or SSE high eightbyte, runs of bit-fields where an
  uncounted `_Atomic int` leaves one register, split or whole on the stack,
  the larger copies' slots, byval or an integer by clang's count, `_Atomic`
  bool vectors, a vector of bools that isn't `_Atomic` returned through a
  hidden pointer, a result LLVM demotes to a hidden pointer clang doesn't
  count, and 513 bools, an 80-byte copy, and as a result a hidden pointer
  clang doesn't count), `va_arg`'s plans for an aligned `long` and an `i128`
  with an SSE high eightbyte, and the levels across calls (a record larger
  than 64 bytes that waits for a `LARGER` member, and records of no size that
  an `_Atomic` of them in a later call reads: a flexible one, and two that
  hold a struct of 128 bytes by an array of none, which wait for what its
  alignment reads), the walk without scratch memory for its stack, after which
  every type must be classified and an `_Atomic` it aligns be right or
  refused, and the walk over 64 unions of two members of the one before, 2^64
  paths, under an alarm;
- worlds: random worlds of `--count` types (2,000 by default), classified by
  both paths into tables that must be identical, and every type at every
  offset it can be at compared with an oracle of the test's own, clang's
  `classify` transcribed with an explicit stack of frames; then classified
  again by the twin, every type at once, into the same table (zero-length
  arrays of large records among the types, which wait in the levels for the
  types their alignment reads), which a world without `_Atomic`s of types of
  no size checks, its levels ordered by the waits alone;
- assignment: random calls over a world's types, named and variadic, by both
  paths, which must give the same locations and totals, also with the columns
  ending at a `PROT_NONE` page; and calls of 65,542 `long`s and of 32,767 and
  32,768 two-register arguments, around the most the vector path's 16-bit
  register sums count (`BUSTER_ABI_COUNTED_ARGUMENTS`), past which it leaves a
  call to its twin.

With `--cross-check`, which needs clang 23 and GCC 16 on `PATH` (or `--clang
PATH` and `--gcc PATH`) and libatomic: for each compiler and vector width (the
C built with `-march=x86-64`, `x86-64-v3` and `x86-64-v4`), a world and
`--signatures` signatures (400) over its passable types, random but for
clang's first five, which pass runs of bit-fields where its count of registers
runs ahead of LLVM's, are written as C (the types, every argument's and
result's bytes, a C callee and a C caller of ours) and as our side, a caller
and a callee per signature as `X86Instruction` records through `x86_encode`,
`link_add_code_fixups` and the object writer. The compiler's driver links them
and the program runs: each side checks every argument, result, hidden pointer,
`al` and `va_arg` value against the bytes, both ways. The six runs (clang's,
then GCC's, each at 16, 32 and 64 bytes) go at once, a child process each, and
the test prints their lines in that order; a run that fails keeps its files.
GCC's worlds leave out
what GCC lays out or passes otherwise than clang (the test names each, GCC's
own bugs among them, and `docs/calling-convention.md` lists them), and clang's
a type in registers that holds an array of elements of no size, on which clang
23 crashes now and then, one with an SSE low eightbyte and bit-fields in an
INTEGER high one, whose access units the module doesn't work out yet, an
aligned typedef of an `_Atomic` vector of bools, whose atomic loads crash it,
a record SSE, SSEUP of a size other than 16, 32 or 64 bytes and one SSE, SSE
that holds a `__float128`, which the module doesn't model yet either, and a
union of a vector LLVM packs into fewer bits than its bytes, whose copies lose
the other members' bits; an argument clang would pass as an `i0` becomes a
long (its passes crash on them now and then), as does an unnamed one clang's
`va_arg` reads elsewhere than its call puts it; bytes clang doesn't pass
aren't compared; and the C side copies a `SMALLER` type's LLVM bytes, all that
clang's objects of it hold. The files go in a new directory under `$TMPDIR`
(or `/tmp`), removed unless `--keep` or a failure. The 9700X job's test step
runs it with `--cross-check`.

## Calling convention benchmark

`./build/compiler benchmark-calling-convention [--seed N] [--count N]
[--only classification|assignment] [--path vector|scalar] [--after-scalar
MS]` (about 1 s on the 9700X) checks that each vector path of
`calling_convention.c` agrees with its twin (the classification's rows, pass
bits, stack bytes, alignments, storage, layouts and results; the
assignment's locations and calls), then reports 21 interleaved-pass medians
per item (time, cycles, instructions, IPC, branch mispredictions and the
throttle, PMCx1A0's umask 0x08, #199):

- classification: a table of `--count` types (32,768), records and unions of
  0 to 8 members mostly, with the pointers, arrays, vectors and aligned
  typedefs their members have, classified from nothing by `abi_classify_all`
  and its twin at 16-, 32- and 64-byte vectors, per type and per record
  member; then by `abi_classify` and its twin on the batches a front end
  hands them: windows of the 1, 4 or 16 types made since its last call, a
  call per dependency level;
- assignment: `--count` calls over those types at 16 bytes, most of 0 to 6
  arguments, some of 4 to 14 and a few of 15 to 22, one in four variadic, by
  `abi_assign` and its twin per argument, with the share of arguments the
  vector path left to its twin and why (from a mirror of its rules,
  `abi_benchmark_deferred`, to keep in step with `abi_assign`); then the same
  shapes with integers, pointers and doubles alone (`assign-simple`).

Each run takes the whole table or every call once. Below 16,384 types the
branch predictor learns the table from run to run: with 4,096, the
classification mispredicts 3 to 10 times less often and runs up to 2.3 times
as fast. `--path` times one path alone, back to back, and `--after-scalar MS`
runs the twin for MS ms before each sample of the vector path. Run it under
the 9700X's lock with `taskset -c 2`; `docs/calling-convention.md` has the
numbers. The default 9700X job runs it after the tests, pinned to one allowed
CPU; like the others, its measurements are
informational.
