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

The `--smoke` mode runs two pristine Preprocessor tests:
`macro_paste_simple.c` and `macro_paste_hashhash.c`. It admits exactly their
original `%clang_cc1 ... -E | FileCheck %s` shape, then directly launches
`clang -cc1 -E SOURCE` and `ide cc -E SOURCE`. It does not execute upstream
shell text or add `-P`. The Buster argv translation is explicit; the original
assertions remain unchanged.

Both compilers independently satisfy the original ordered, nonoverlapping
literal `CHECK` assertions with FileCheck's horizontal whitespace convention.
Missing assertions, unrecognized CHECK modifiers, regex/variable constructs,
extra RUNs and feature directives fail closed. This is a two-test adapter,
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
After building Release `ide`, it runs the two real preprocessing tests and
controls for hidden fixture mutation and compiler launch failure.
It also compares fifteen first-party macro boundary/stringification controls
against both compilers. These regression controls are separate from the two
upstream test identities and never increase the reported upstream coverage.

The existing Python regression-test convention supplies an independent reader
and process controls, not production orchestration or a new dependency:

```sh
BUSTER_CLANG_SUITE_DRIVER=/absolute/build-driver BUSTER_CLANG_SUITE_CHECKOUT=/absolute/llvm-project \
BUSTER_CLANG_SUITE_IDE=/absolute/ide BUSTER_CLANG_SUITE_CLANG=/absolute/clang \
python3 tools/clang_suite_test.py -v
```

The workflow retains exact source/tool identity and complete result artifacts.
Its results are correctness evidence only; no performance acceptance or
hardware run is implied. Whole-suite completion remains on #2280 and its
remaining children; it cannot be inferred from a green inventory/smoke lane.
