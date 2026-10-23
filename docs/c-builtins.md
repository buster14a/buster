# C builtin integer actions

The [typed checking and runtime integration guide](c-builtins-integration.md)
describes the additional typed-action and executable-stream interfaces. The
action-level measurements below retain their original scope and revision.

`c_builtin.c` is the dependency-independent integer-action part of
[#148](https://github.com/buster14a/buster/issues/148). It recognizes 21 names
and folds independent constant calls with AVX-512 and exact scalar paths:

| Names | Integer action |
| --- | --- |
| `__builtin_bswap16`, `32`, `64` | Reverse bytes after conversion to the parameter width |
| `__builtin_clz`, `clzl`, `clzll` | Leading zero count; zero is undefined |
| `__builtin_ctz`, `ctzl`, `ctzll` | Trailing zero count; zero is undefined |
| `__builtin_popcount`, `popcountl`, `popcountll` | Population count |
| `__builtin_parity`, `parityl`, `parityll` | Population count modulo two |
| `__builtin_ffs`, `ffsl`, `ffsll` | One-based first set bit; zero gives zero |
| `__builtin_add_overflow`, `sub_overflow`, `mul_overflow` | Exact mathematical arithmetic, destination conversion, and overflow boolean |

This is an action API, not C source support. On its starting revision,
`f3a68dd2c280ecb2a851a1a41128084f18e191a6`, the compiler has no typed C stream
(#133), Tier 0 C selector (#136), or Tier 1 C selector (#141). These actions
therefore cannot yet compile a C builtin call or satisfy #148's corpus and
two-tier execution acceptance. The issue remains open.

## Contract

`cbuiltin_lookup` takes bounded spelling bytes and returns `CBuiltinId`, or
`CBUILTIN_NONE`. It uses a masked 32-byte load and fixed-size name comparisons;
`cbuiltin_lookup_scalar` is its independent bounded-string twin. The names
are deliberately not a catalogue of unimplemented builtins. A future resolver
should bind once to the ID, rather than do spelling lookups for every fold.

`cbuiltin_fold` and `cbuiltin_fold_scalar` read `CBuiltinIntegers` columns and
write `CBuiltinIntegerResults` columns. The input rows are independent constant
calls, potentially of different builtins. All columns contain `count` entries,
with no required padding or alignment. Input and output ranges are disjoint.
A zero count permits null columns. The actions allocate no memory.

The `a` and `b` columns hold low integer bits. For the bit operations, only `a`
matters: the parameter conversion truncates to 16, 32, or 64 bits, as appropriate
for x86-64 LP64. All `l` and `ll` forms take 64 bits. These are target C widths,
not the width of a vector lane in a generated program.

Overflow rows use `a_type`, `b_type`, and `result_type`: a bit width from 1 to
64, plus `BUSTER_CBUILTIN_SIGNED` for signed values. Signed one-bit types are
invalid. Upper bits outside each width are ignored. A descriptor represents an
integer's arithmetic shape, not a `TypeId`; the checker must validate actual C
types. Clang accepts non-const boolean and enum destinations for its generic
overflow builtins, unlike GCC's documented restriction; the typed adapter
retains their actual integer shape. A boolean is unsigned one-bit. Wider
integers need another action and are not silently
truncated into this API.

Every output row has:

- `value`: the folded bits, or the low destination bits for overflow operations;
- `overflow`: the overflow call's boolean, zero for other operations;
- `status`: `FOLDED`, `UNDEFINED`, `UNSUPPORTED`, or `INVALID_TYPE`.

Undefined, unsupported, and invalid rows write zero to both numeric outputs.
In particular, `clz(0)` and `ctz(0)` do not publish a folded width. The caller
decides how an undefined action affects its evaluation context and diagnostic.

The scalar overflow action uses unsigned 128-bit magnitudes and separate signs.
Its products fit that unsigned representation, including `UINT64_MAX` squared.
The vector action extends each input to two 64-bit words. Add/subtract propagate
carry/borrow into the high word; multiplication computes the full unsigned high
half from four 32-bit products and corrects it for signed inputs. Overflow is a
comparison with the destination value extended back to two words. Neither path
invokes host C signed-overflow behavior.

The vector action handles all supported numeric rows. Each batch uses masks for
the bit-operation and overflow families; invalid rows do not require a scalar
fallback. Every load and store has a bounded tail mask. The scalar action remains
callable independently, including for tiny batches and testing.

Within the bit-operation family, a 32-entry table keeps the parameter-width
shift and operation flags together. One `vpermi2d` classifies the eight rows,
replacing repeated width comparisons and operation-range tests. The input IDs
are widened to qwords already, so each high dword selects the zero descriptor
at index zero; the result has one complete descriptor per qword without another
conversion. The permute uses only five index bits. Every operation still uses
the supported-family mask computed from the full ID, so unsupported IDs that
alias a valid descriptor remain unsupported.

This is a fixed 128-byte lookup table, not another per-row representation. The
input columns, output columns and producer's IDs are unchanged; no sorting,
allocation, copying or extra pass is added. Any legal partition of the rows,
including a partial flush before the next eight-row batch, produces identical
results in original order.

## Integration still required

The checker must own argument count/type checking, constant-expression rules,
operand conversions, unevaluated contexts, and diagnostics. Overflow builtins
use each argument's original integer type, not the usual arithmetic conversions.
They always store the converted result through their third argument, even when
overflow is true. Folding that result cannot discard the store or the pointer
expression's effects. This API returns the store value and boolean separately
so the future typed stream can retain both.

Both selectors must lower nonconstant calls, preserve effects, and respect the
generated-code ISA. An AVX-512 host fold grants no ISA feature to emitted code;
baseline x86-64 bit operations need baseline sequences where `lzcnt`, `tzcnt`,
or `popcnt` are unavailable. Execution fixtures must cover both selectors.

The normal `__has_builtin` mode consults the shared capability inventory in
`c_builtin_catalog.c`; its integer entries currently advertise folding only.
The separate Clang-oracle feature table from #117/#154 remains available for
preprocessor comparisons. Recognition or constant folding alone is insufficient
evidence for advertising source support. Source capabilities require checking
and both required lowering paths. Do not substitute this module's 21 recognized
names for that acceptance evidence.

The other #148 families are outside this fold kernel: varargs (using #137's ABI
plans), `offsetof`, hints/traps, compile-time queries, floating point, memory and
strings, atomics, and frame/return addresses. Wider overflow operand types also
remain outside its supported contract. Companion typed actions and runtime
expansion are described in [the integration guide](c-builtins-integration.md);
their validation is separate from full source and two-tier acceptance.

## Validation and measurements

`./build/compiler test-builtins [--seed N]` checks both paths against the
bootstrap Clang's builtin calls. The overflow oracle passes signed 128-bit
operands, which contain every input value, to Clang's generic overflow builtins
with an actual signed or unsigned `_BitInt(N)` destination for every width.
This is independent of both folding algorithms. It checks boundary values,
all signedness combinations, non-power-of-two widths, mixed/random builtin
rows, invalid descriptors and undefined inputs. Guard pages follow every
column for counts 0 through 33, and follow every recognized name. Compile-time
assertions also exercise Clang's folding of fixed bit-operation fixtures.
These are action-level comparisons, not C programs compiled by Buster.

The descriptor-lookup regressions put invalid IDs with every low five-bit
index beside valid operations, at every lane position, and compare all outputs
with Clang's oracle. Partition checks split the same mixed rows into batches
of 1 through 17 across counts around the vector boundaries and a 257-row input;
they require the scalar result and unchanged memory beyond the logical output.

`./build/compiler benchmark-builtins [--only scalar|vector]
[--after-scalar MS]` times 16,384-row bit, overflow, and mixed batches, including
all result stores, after comparing their complete outputs. It reports the median
and p10–p90 of 21 samples, each calibrated to at least 2 ms. The default alternates
path order; `--only` measures a path repeatedly and `--after-scalar 100` conditions
each vector sample. Run separately locked processes, pinned to CPU 2:

```sh
EXCLUSIVE_OWNER=builtins-148 EXCLUSIVE_TITLE='Integer builtin actions #148' \
EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/148 \
~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-builtins
```

### Recorded 9700X results, October 22, 2026

The measured source is `8b0cd7b2fcf5bb82705607abb24580d34836e652`, merged in
[PR #273](https://github.com/buster14a/buster/pull/273) as
`09ccd1b16585a52f89d3ef43b3941f4d001a91cc`. The merge and tested head have the
same tree, `346870195f3769eed0b1ac987c9da86fe4d09481`. The host was Linux/x86-64,
AMD Ryzen 7 9700X, with Clang 23.1.1 and the repository's `build.sh` flags:
`-O3 -march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall
-Werror -g`. The benchmark executable's SHA-256 is
`c67db28ff8cdb62de21fe4ecab28b989a18e6a3e21b8d1cf81c91500f5c32a8b`.

Three independent processes ran each configuration, interleaving the
configurations in the order default, `--only vector`, and `--only vector
--after-scalar 100`, then repeating that order twice. Every process acquired
and released the shared lock separately and ran on CPU 2. The fixed input is
the benchmark's 16,384-row generator with seed 148. The
[raw process output](c-builtins-148-measurements.txt) retains every process
median and its within-process p10–p90.

Nanoseconds per row below are the median of the three process medians, with
their minimum and maximum in brackets:

| Workload | Scalar, interleaved | Vector, interleaved | Vector alone | Vector after 100 ms scalar |
| --- | ---: | ---: | ---: | ---: |
| Bit operations | 1.082 [1.063, 1.123] | 0.631 [0.630, 0.634] | 0.630 [0.630, 0.632] | 0.632 [0.631, 0.633] |
| Overflow | 3.311 [3.304, 3.311] | 1.295 [1.295, 1.298] | 1.294 [1.294, 1.295] | 1.294 [1.294, 1.295] |
| Mixed | 1.620 [1.608, 1.635] | 1.147 [1.145, 1.151] | 1.146 [1.145, 1.146] | 1.145 [1.144, 1.146] |

The interleaved median throughput ratios are 1.71, 2.56, and 1.41 respectively,
with the vector path ahead in every process. The isolated and conditioned
vector ranges overlap. These results cover complete synthetic action batches,
including result stores. Tiny-batch cutoffs and frontend/whole-compiler speed
remain unmeasured.

The frozen production build passed 221,184 deterministic boundary rows and
1,019,519 generated rows, plus spelling checks and every builtin at guarded
column tails 0 through 33. Defined values and overflow booleans are checked
against Clang; undefined/invalid cases have explicit status expectations.
The generated and boundary suites also passed in an unoptimized validation
build. The call-graph check found zero cycles over 21 translation units,
8,123 functions and 44,285 calls. The frozen vector assembly contains vector
counts, byte shuffles and four 32-bit products for the high multiply halves,
with no gathers or scatters.

The required [PR 9700X workflow](https://github.com/buster14a/buster/actions/runs/39802046396)
passed on this head, including the existing compiler suite and the new builtin
tests. These receipts establish the integer actions' validation; the source
language and selector acceptance listed above remains outstanding.

### Descriptor lookup: cloud measurements, October 23, 2026 (#301)

The descriptor change was compared with `c_builtin.c` from
`1f1e116ad358bed8d5a97824b916908d7cb44e96` using the existing
`c_builtin_benchmark.c` through a standalone entry point that calls
`run_builtin_benchmark`. Both binaries used Ubuntu Clang 18.1.3 with
`-O3 -march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall
-Werror -g`, on a cloud AMD EPYC 9V74, pinned to CPU 4. Adding the full
compiler build's `-mmwaitx -Wno-bitwise-instead-of-logical` switches produced
byte-identical `.text` sections for both binaries. This host is not the 9700X;
these measurements qualify the change for target-host measurement and do not
replace it or measure complete C compilation.

Three independently started processes of each binary ran each configuration,
alternating baseline and candidate. Every process held the common experiment
lock for its own run. Each reports the existing benchmark's 21 calibrated
samples over 16,384 rows, including complete result stores and scalar-output
comparison. The [18 original process logs](measurements/simd-301-2026-10-23/builtins.txt)
and [standalone benchmark entry point](measurements/simd-301-2026-10-23/builtin-benchmark.c)
are checked in with the measurement record. The table gives the median of the
three process medians in ns/row, with their minimum and maximum in brackets:

| Workload | State | Before | Descriptor lookup |
| --- | --- | ---: | ---: |
| Bit operations | interleaved | 1.452 [1.432, 1.496] | 1.086 [1.062, 1.096] |
| Bit operations | vector alone | 1.414 [1.412, 1.448] | 1.074 [1.050, 1.101] |
| Bit operations | after 100 ms scalar | 1.438 [1.412, 1.529] | 1.109 [1.066, 1.154] |
| Overflow | interleaved | 2.847 [2.843, 2.873] | 2.923 [2.856, 2.932] |
| Overflow | vector alone | 2.813 [2.811, 2.857] | 2.880 [2.808, 3.100] |
| Overflow | after 100 ms scalar | 2.951 [2.773, 3.013] | 2.834 [2.774, 2.861] |
| Mixed | interleaved | 2.388 [2.331, 2.440] | 2.090 [2.060, 2.101] |
| Mixed | vector alone | 2.398 [2.369, 2.459] | 2.069 [2.062, 2.155] |
| Mixed | after 100 ms scalar | 2.363 [2.362, 2.422] | 2.094 [2.049, 2.129] |

The bit-operation time falls by 23–25%, and the mixed time by 11–14%.
Overflow does not use the descriptor lookup; its ranges overlap and no
improvement is claimed. Small-count checks over 64 independent calls at batch
counts 1, 7, 8, 9 and 16 also compare complete outputs with the scalar twin.
Across three processes, their median throughput ratios are 1.31–1.46 for bit
operations and 1.08–1.31 for mixed valid/invalid rows. Overflow remains near
parity, with overlapping process ranges at the two counts whose medians were
about 3% slower. The [six small-count process logs](measurements/simd-301-2026-10-23/builtin-tiny.txt)
and their [standalone harness](measurements/simd-301-2026-10-23/builtin-tiny.c)
retain the tested workload and calibration. Both standalone harnesses select
the baseline or candidate source through `-I<checkout>/src`. No tiny-batch
dispatch threshold was introduced.

The standalone optimized `cbuiltin_fold` shrinks from 1,417 to 1,307 bytes.
Its assembly has the intended `vpermi2d`, with no gathers, scatters or vector
spills. Both binaries retain the same two scalar input-pointer spill/reload
slots in the overflow branch; the bit-operation branch has none. The original
guarded-tail, 221,184 boundary-row and 1,019,519
generated-row tests pass, together with 8,224 alias-index oracle rows and
the partition checks. The scalar implementation is unchanged.

## Behavioral references and provenance

The code, fixtures and benchmark inputs are original. The semantic references
are GCC's [bit operation builtins](https://gcc.gnu.org/onlinedocs/gcc/Bit-Operation-Builtins.html),
[byte swapping builtins](https://gcc.gnu.org/onlinedocs/gcc/Byte-Swapping-Builtins.html),
and [integer overflow builtins](https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html),
checked against Clang itself. No implementation or generated table from those
projects was imported. The builtin spellings are the identifiers named in #148.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md).
[The module boundary](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_builtin.c#L4)
is descriptor lookup → `cbuiltin_fold`/`cbuiltin_fold_scalar` → folded value,
overflow and status columns. At this pin, tests/benchmarks supply the inputs;
ordinary checker calls do not establish full builtin source integration.

Inputs are counted u32 IDs/type descriptors and u64 integer operands; outputs
are u64 value and u32 overflow/status. Natural alignment suffices, no padding
is required, and input/output columns do not overlap. Zero count permits null
columns. Eight-lane kernels write status for every logical item, including
undefined, unsupported and invalid types. Each independent constant call is
ready only after the source checker has validated and converted its arguments.
The overflow store and evaluation ordering belong to the eventual consumer;
a folded boolean alone is not its complete behavior.

Retain builtin ID at binding instead of repeating spelling lookup in later
passes. This is an integration obligation, not evidence that the current
source binder does so. Compare producer argument packing and result/store
materialization together; a packed fold benchmark cannot prove this useful
for sparse source calls. Disposition: retain the bounded module, with guarded
and external-Clang checks in `test-builtins`. [#148](https://github.com/buster14a/buster/issues/148) owns binding/checking,
selector/feature-table agreement and actual-source throughput, memory and
fallback qualification. Do not advertise an unsupported builtin solely because
its isolated fold kernel exists.
