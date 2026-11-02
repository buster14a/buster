# One request's configuration

[#1057](https://github.com/buster14a/buster/issues/1057): every client that
prepares real C source as the C driver does takes its semantic configuration
from the same code, so that a benchmark or a test reading the driver's options
can't compile another target, ABI or language than `-c` compiles for them.
[#1041](https://github.com/buster14a/buster/issues/1041) and
[#1045](https://github.com/buster14a/buster/issues/1045) were omissions of this
kind: two benchmarks built their checked program's options without the
options' target, and one without the predefines that a function's `target`
attribute resolves against. `ir_source_prepare`, which `test-ir-source`,
`benchmark-ir-source`, `test-ssa-source` and the Tier 1 source commands share,
had the same gap: it passed the target but not the predefines.

## The contract

`src/buster/compiler/c_check_request.c` is one request's configuration. It
extends the existing contracts (`PPOptions`, `CheckProgramOptions`,
`CheckDriverCommand`) and doesn't replace them:

- `check_request_scan` reads the command-line options that no `PPOptions`
  field decides: code generation the driver doesn't implement (`-pg`,
  `--coverage`, the stack protector's options, `-fcf-protection`, ...) and the
  ABI layouts the checker doesn't implement (`-fshort-enums`,
  `-fpack-struct`). Each family's option decides as clang's driver takes it:
  the first code generation option, and the last stack protector,
  `-fcf-protection`, `-f[no-]short-enums` and packing option. Its answer says
  whether the argument goes on to `pp_parse_options`. The driver's argument
  loop calls it, and so does every other client.
- `check_request_refusal` says what the request asks for that the driver
  doesn't implement, and which option asked for it. In the driver's order:
  code generation, position-independent code when the request emits code
  (#811), a layout, then a language or target the checker doesn't take
  (`check_input_target_supported`). `check_request_describe` writes the
  driver's message for the refusal. The driver prints it after `error: `, and
  the other clients print it in their own reports.
- `check_request_read` (a client's arguments, file included) and
  `check_request_parse` (options, then one file) are how a client reads its
  request: scanned, parsed and refused or accepted as a whole. A request that
  can't be read (no room, an option `pp_parse_options` refuses or one without
  its value, `-o`) or that is refused leaves the configuration all zero, never
  partly read.
- `check_request_program_options` makes a checked program's options from the
  unit's preprocessor. Its `pp->options` is the request's own copy, the one
  the source was preprocessed with. It sets the identifiers, the generated
  code's target and the predefines that the functions' target attributes
  resolve against (`CheckProgram.body_targets`), and it keeps the unit's own
  borrowed state and the client's overrides. The driver, the corpus check
  (both of its programs) and the shared preparation build their programs'
  options through it. No client copies the target or the predefines itself.
- `check_request_receipt` describes the resolved configuration for a
  measurement's report (below). Only a client that asks for it computes it.

The host's ISA stays apart from the generated code's target. A baseline host
build (`BUSTER_HOST_ARCH=x86-64`) runs every scalar twin (`BUSTER_SCALAR_PATH`)
and compiles for the same target as a native build, and the parity tests below
run on both host builds in CI.

## The shared preparation

`src/buster/compiler/c_check_source_prepare.c` takes a file through the stages
that `-c` takes it through before its code. The stages:

1. the request, read and refused as the driver reads and refuses it;
2. preprocessing and checking, as the driver checks (the vector path,
   `check_corpus_check_policy`), with `-w`'s warnings suppressed;
3. the static data (`check_data_prepare`) and the checked program, with the
   request's semantic options and the prepared data's declarations, as `-c`
   passes them;
4. with `CheckSourceRequest.select`, the referenced definitions
   (`C_LOWERING_REACHABLE`) and code generation's check of their target
   features (`check_program_target_features`, #775), which refuses what `-c`
   refuses.

`check_source_prepare` reports the stage that stopped a file
(`CheckSource.stage`), and `check_source_failed` says whether that stage is
the client's failure or a result. A file `-c` would refuse is a result, which
leaves it out of a measurement: the checker's rejection, a refusal by name,
or its target-feature errors. A refused request, a file that doesn't
preprocess and a resource that runs out are failures.
`benchmark-backend-source`, `benchmark-check-lanes`,
`benchmark-lowering-source` and `ir_source_prepare` share it.

Each client keeps its own timing region, report wording and controls. Nothing
in the preparation is timed. The clients time what follows it: the emission,
its lanes, the lowering ways and the IR's passes.
`benchmark-checker-source` and `benchmark-checker-deferred`, which only check,
read their options through `check_request_read` and `check_request_parse`
with `emits` false, as the driver's `-fsyntax-only` reads them.

### Explicit overrides

These are the only departures from `-c`'s preparation, and each is explicit:

- `CheckSourceRequest.scalar` (the scalar twins, as `--scalar`);
- the tests' injected failures (`CheckSourceRequest.fault`);
- what each client does after the preparation: its selection mode
  (`benchmark-lowering-source` also lowers everything), forced lane counts,
  and an emission without unwind tables or `-g`'s rows.

The receipts below name them.

### Lifetimes

A `PPOptions` borrows the command line's strings and the option arena's
arrays (`pp_parse_options`). A checked program borrows its predefines from the
unit's preprocessor, which outlives the program's checking, selection and
target-feature check. A `CheckSource` holds its intern table, preprocessor,
parser and options in an arena of its own. It holds no static object, so it
can be an entry point's callee in any unit (`ir_source_prepare`'s), and
`check_source_release` frees it whatever stage the file reached. A failed
stage leaves the later stages zero. Each file of a multi-file client reads
its options again. The driver's units each copy the command's options into
their own preprocessor. Nothing keeps a configuration from one request to the
next, and the process-lifetime lanes and worker budget (#905) are untouched.

## Receipts

`benchmark-backend-source`, `benchmark-check-lanes` and
`benchmark-lowering-source` print a line per prepared file before its outcome:

```text
backend-source: file=F configuration=DIGEST target=znver5 features=HEX extensions=HEX language=gnu17 unwind=on debug=off overrides=path:vector,selection:reachable,unwind:off,debug:off
```

`DIGEST` is a 64-bit hash (`link_hash`, mixed field by field) of:

- the resolved target (features, extensions, architecture);
- the language and its lexical options;
- the predefined macros' text the unit was preprocessed with, which carries
  every predefine switch, from `-march` to `-funsigned-char`, `-O` and the
  stack protector's level;
- the predefines' fields the checker reads beside its macros;
- `-D`, `-U` and `-include`, in order;
- the header search (`-iquote`, `-I`, `-isystem`, `-idirafter`,
  `--embed-dir`, `-nostdinc`, `-nobuiltininc`, `--buster-headers`);
- `-c`'s own output requirements for the request: its unwind tables
  (`predefines_unwind_tables`) and `-g`'s rows;
- the client's overrides.

It never hashes a pointer's value or an intern id: the same file prepared
twice with the same options gets the same receipt, and another `-march`, `-D`,
`-funsigned-char`, `-I` or override gets another. Fixtures that ask for
different work (another target, another override) are different workloads by
their receipts. Only the benchmarks compute receipts. An ordinary compile
pays nothing for them.

## Tests

`./build/compiler test-checker --requests` runs `check_test_requests`
(`c_check_request_test.c`), then the three benchmarks' own preparation tests.
The default `test-checker` run deals them to its parts too. The checks:

- **Refusals:** option lists of each family, in the orders where the first or
  the last option decides. Each is read by `check_request_parse`, which must
  give the refusal and its option, pass the right arguments on, never read an
  option's value as an option, and leave a refused or unreadable request zero.
  The driver's `-c` or `-fsyntax-only` must then exit with the same status and
  the same first line of errors.
- **Program options:** the identifiers, target and predefines are the
  preprocessor's, and the unit's fields and overrides are kept.
- **Receipts:** stable across two preparations, changed by `-march`, `-D`,
  `-funsigned-char`, `-I` and the overrides, and only `-march` changes the
  target's text.
- **Parity:** original fixtures prepared by `check_source_prepare`, emitted,
  published and built as `-c` builds its object (`check_driver_object_build`,
  which `check_driver_object` calls), against the driver's own `-c` object of
  the same options, byte for byte. The fixtures cover scalar code with static
  data and a jump table, the baseline, AVX2 (`-march=x86-64-v3`) and AVX-512
  (`-march=x86-64-v4`, `znver5`) targets, a function's `target("avx2")`
  attribute, `-funsigned-char`, `-D` and `-std=c11`, and the scalar path.
  Where `-c` refuses, both must refuse the same way: wider vectors than the
  target's registers at the emission, an `always_inline` callee whose target
  the caller lacks at the target-feature check, and `-fshort-enums` at the
  request. The first case runs again last, after the other targets, and must
  make the same object again: one preparation's configuration never reaches the
  next.

## Discontinuities in the measurements' series

At the commit that adds this:

- `benchmark-backend-source`, `benchmark-check-lanes` and
  `benchmark-lowering-source` print a configuration line per file. They now
  refuse the requests the driver refuses (`-fPIC`, `-fshort-enums`, `-pg`, ...),
  which they used to measure as if the options weren't there, and they leave
  out a unit whose target features `-c` refuses. Their checked programs take
  the prepared data's declarations, as `-c`'s do, which gives the same program
  by `CheckProgramOptions`' contract. For the 9700X job's options on the frozen
  `sqlite3.c`, which `-c` compiles, none of this changes what is timed.
- `ir_source_prepare`'s functions with a `target` attribute are compiled for
  the attribute's target (`IrSourceFunction.target_features`), not the
  unit's, and a unit whose target features `-c` refuses is left out. A file
  without a target attribute or an x86 builtin is prepared as before.
- The driver's messages, refusals and objects are unchanged.

## Cost

The driver's added work is per command, not per token: a few string
comparisons per argument (the same ones its loop made before), one refusal
check and a copy of three fields. The preparation's added work is outside
every benchmark's timed region: `-c`'s target-feature check, which most units
skip whole (no target attribute and no x86 builtin), and the receipt's hash of
the predefined macros' text.

A local check on a 4-CPU cloud VM (not the 9700X: no hardware counters, and
its noise floor is wide) compared `df007cd5` (the base) with this change and
with the base again, interleaved, 15 or 9 runs from 2 copies of each
executable, pinned to one CPU:

| Command | base | this change | base again |
|---|---|---|---|
| `-c` of c-testsuite's `00001.c` | 3.449 ms | 3.413 ms (−1.05%) | 3.343 ms (−3.06%) |
| `-c` of `tools/ci/changes.c` (2,537 lines, libc's headers) | 20.10 ms | 19.71 ms (−1.93%) | 18.98 ms (−5.58%) |
| `test-ir-source` of c-testsuite's 220 programs | 841.3 ms | 814.9 ms (−3.14%) | 795.7 ms (−5.42%) |

Both changes are inside the base's difference from itself. The two `-c`
objects are byte for byte the base's. `benchmark-backend-source` and
`benchmark-lowering-source` admit the same 220 programs, with the same
functions, entries, records and lowered counts, apart from the new receipt
lines.

Pending: these costs, for a tiny file and a representative corpus, measured
on the locked 9700X. Their raw evidence belongs in
[buster-performance](https://github.com/buster14a/buster-performance/tree/main/docs/measurements).
The pull request's own 9700X job runs the benchmarks above as it runs them
for any change to their sources.

## Still separate

- The driver checks a unit through `check_driver_unit`, not through
  `check_corpus_check_policy`. The parity tests keep the two equal on their
  fixtures, but the driver's diagnostics, repair and prefix paths stay its own.
- The aliases' and ifuncs' check (`check_driver_aliases`) runs in the driver
  alone.
- `benchmark-checker-bindings` keeps its own reading: its synthetic inputs
  (`--input`) have no file.
