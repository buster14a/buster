# Clang test corpus

[Compatibility index](../compatibility.md) · [Campaign #2280](https://github.com/buster14a/buster/issues/2280)

The native build-driver command consumes a separate external LLVM checkout.
It never copies or patches upstream test sources into Buster. C remains the
sole active Buster frontend; the full corpus includes other languages, Clang
tools/APIs and native unit suites whose capability/discovery work is pending.

## Immutable inputs

The input is LLVM `llvmorg-23.1.2`, commit
`85ac560262434c9ccfc0c183ec22d4138ed647fb`.

| Root | Git tree | Tracked leaves |
| --- | --- | ---: |
| `clang/test` | `b475034ef2cd61b58ca99df2f6197effe37f07b5` | 30,712 |
| `clang/unittests` | `6f0895792d934b50aaa6111d7b00cd6e33018bbe` | 410 |
| `clang/tools/scan-build-py/tests` | `e5a25e2dc4a0291548efc2a422aa6088aa73ed1a` | 30 |
| `clang/bindings/python/tests` | `efcb5c86e43f1a372af61728244568770b33c472` | 38 |
| `clang/LICENSE.TXT`, `llvm/LICENSE.TXT` | Two license blobs below | 2 |

The **31,192-leaf source ledger is not a discovered or executed test count**.
Every ledger row starts `unadapted`; even configuration, fixture, license and
symlink leaves are retained. Actual lit expansions, local configuration and
GoogleTest case discovery remain #2287. The static investigation's 24,938
ShTest candidate paths do not establish executed coverage.

LLVM/Clang's main license is `Apache-2.0 WITH LLVM-exception`, verified in
[clang/LICENSE.TXT](https://github.com/llvm/llvm-project/blob/85ac560262434c9ccfc0c183ec22d4138ed647fb/clang/LICENSE.TXT)
(blob `24806ab4c9eb291db4d28e159901f7a0901b9fd1`) and
[llvm/LICENSE.TXT](https://github.com/llvm/llvm-project/blob/85ac560262434c9ccfc0c183ec22d4138ed647fb/llvm/LICENSE.TXT)
(blob `fa6ac540007032cbd0ec772a1c72e6cb5527a4fe`). Both preserve legacy and
third-party terms; per-file exceptions must be reviewed before derivative
material is committed. This adapter imports no upstream source. Buster's
first-party license remains unresolved; see [the license inventory](../../../LICENSES/README.md)
and #621.

## Commands and outputs

Run from the Buster root with the build directory idle. Bootstrap the driver
normally; do not run `generate` alongside a build or suite invocation.

```sh
./build.sh test_clang_suite --self-test
./build.sh test_clang_suite --inventory /absolute/llvm-project /existing/parent/fresh-inventory
./build.sh test_clang_suite --smoke /absolute/llvm-project /existing/parent/fresh-smoke /absolute/ide /absolute/clang
```

The first execution lane is POSIX Linux/macOS. Windows builds retain the
command, but native execution rejects until its reparse-point/source/output
boundary has been implemented and validated (#2291). The output parent must
exist, resolve outside the external checkout, and its final directory must
not exist. Existing outputs are never overwritten. All children run with
bounded capture and a 60-second process-tree deadline.

`git-ls-tree.bin` retains the exact NUL-delimited immutable Git-object census;
`sources.tsv` records mode/blob/path/role/state. `receipt.txt` binds the pin,
counts, SHA-256 digests, source-ledger execution count (always zero), requested
smoke mode and final checkout status. The tool verifies HEAD and status before
and after publication. Ignored/untracked files are rejected. A clean Git status
is not a byte audit of every worktree file: source inventory reads Git objects,
and execution verifies each selected fixture's actual unfiltered blob hash.

## First assertion slice

The `--smoke` mode runs five pristine Preprocessor tests:
`macro_paste_simple.c`, `macro_paste_hashhash.c`, `macro_arg_empty.c`,
`macro_paste_empty.c` and `macro_disable.c`. It admits exactly their
original `%clang_cc1 ... -E | FileCheck %s` shape, then directly launches
`clang -cc1 -E SOURCE` and `ide cc -E SOURCE`. It does not execute upstream
shell text or add `-P`. The Buster argv translation is explicit; the original
assertions remain unchanged.

Both compilers independently satisfy the original ordered, nonoverlapping
literal `CHECK` assertions. The original two cases fold horizontal whitespace.
The three added cases preserve internal spaces and tabs exactly, accepting
only their original `--strict-whitespace` or `-strict-whitespace` RUN spelling.
Both modes normalize CRLF and trim CHECK directive padding, as pinned
[FileCheck](https://github.com/llvm/llvm-project/blob/85ac560262434c9ccfc0c183ec22d4138ed647fb/llvm/lib/FileCheck/FileCheck.cpp)
does without `--match-full-lines`. Unsupported option combinations fail closed.
The new assertions cover empty macro arguments, empty token-paste operands,
and disabled recursive/rescanned macro expansion. Their sources carry no
separate per-file license notice; the pinned Clang license above applies.
No upstream source is copied into Buster.
Missing assertions, unrecognized CHECK modifiers, regex/variable constructs,
extra RUNs and feature directives fail closed. This is a five-test adapter,
not an implementation of general FileCheck/lit/Clang `-verify` (#2288).

Each compiler's stdout/stderr, executable path, portable/native process
status and literal-check verdict is retained, alongside fixture hash evidence
and `smoke-summary.txt`. Reference failure cannot make Buster pass. Source,
launch, timeout, crash, nonzero exit, incomplete capture and assertion failures
remain failed. Compilers run sequentially and both observations are retained.

## Hosted validation

`.github/workflows/clang-suite.yml` bootstraps the native driver and tests the
parser/checker controls, complete inventory, repeated manifests, wrong pins,
dirty/ignored inputs, occupied/aliased outputs and malformed CLI invocations.
After building Release `ide`, it runs the five real preprocessing tests and
controls for hidden fixture mutation and compiler launch failure.
It also compares eighteen first-party macro boundary/stringification controls
against both compilers, using generated temporary sources preserved in the
hosted evidence. Tracked retirement support remains frozen. These regression
controls are separate from the five
upstream test identities and never increase the reported upstream coverage.

The existing Python regression-test convention supplies an independent reader
and process controls, not production orchestration or a new dependency:

```sh
BUSTER_CLANG_SUITE_DRIVER=/absolute/build-driver BUSTER_CLANG_SUITE_CHECKOUT=/absolute/llvm-project \
BUSTER_CLANG_SUITE_IDE=/absolute/ide BUSTER_CLANG_SUITE_CLANG=/absolute/clang \
python3 tools/clang_suite_test.py -v
```

The workflow runs on affected pull requests, exact merge groups and affected
main pushes. It retains exact source/tool identity and complete result artifacts.
Its results are correctness evidence only; no performance acceptance or
hardware run is implied. Whole-suite completion remains on #2280 and its
remaining children; it cannot be inferred from a green inventory/smoke lane.

## Public intrinsic family conformance

The `--intrinsics CHECKOUT FRESH_RESULTS ABSOLUTE_IDE ABSOLUTE_CLANG`
mode uses the same immutable checkout, fresh-output boundary, source ledger and
final receipt as the existing modes. It requires Clang 23.1.2 and the pristine
`clang/lib/Headers` inputs at the pin above. The hosted workflow installs that
exact release through the existing verified LLVM installer.

The first family is the five GNU-C LZCNT APIs reachable from
`<immintrin.h>`: `__lzcnt16`, `__lzcnt32`, `_lzcnt_u32`, `__lzcnt64`
and `_lzcnt_u64`. A first-party fixture supplies nonconstant operands,
observable results and an independent shift-loop oracle. Zero returns its
operand width. Clang's pinned LZCNT header explicitly permits baseline targets;
baseline runtime checks therefore need no Zen 5 or LZCNT processor. Separate
`-march=znver5` object generation checks the requested target without running
Zen 5 instructions on a generic hosted runner. Buster runtime checks cover
both frontend forms and FAST/QUALITY.

Pristine header admission also requires Clang 23's unused F16C wrappers'
`__fp16` vector typedefs and `__builtin_convertvector` calls. The frontend
keeps their two-byte element type distinct from `_Float16`, checks the
four- and eight-lane layouts and conversion lane counts, and reports named
refusals for ordinary storage-half object/ABI uses and reached half-vector
conversion. Reached conversion refusal uses the parsed source operand type,
not a half-vector name appearing only inside an unevaluated `sizeof`. The
lowering budget skips known `sizeof`, `_Alignof` and `typeof` operands, but
`_Generic` evaluation-context classification is not complete. This admission
does not grant F16C intrinsic, promotion or ABI coverage.

This is a family slice of [#2405](https://github.com/buster14a/buster/issues/2405)
and [#2290](https://github.com/buster14a/buster/issues/2290), not the exhaustive
public-API census, a general immediate-domain oracle, or a completed Zen 5
support claim. The original 31,192-leaf corpus ledger remains unchanged and
does not count these first-party controls as upstream test executions. The
Clang 23.1.2 public-header oracle is distinct from the LLVM 21.1.8 finite typed
builtin metadata contract; passing this family does not establish equivalence
between those contracts. No performance validation is inferred.
