# C builtin integer actions

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
types. For example, it must reject a boolean or enum destination even if its
width resembles an accepted integer, and can describe a boolean input as
unsigned one-bit. Wider integers need another action and are not silently
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

`__has_builtin` is not wired to this list. The current preprocessing-only
command retains its Clang-oracle feature table from #117/#154. Recognition or
constant folding alone is insufficient evidence for advertising source support.
When the C compile path exists, #148 still needs a capability inventory that
reports only builtins with checking and both required lowering paths, plus tests
of that inventory. Do not substitute this module's 21 recognized names for it.

The other #148 families remain unimplemented here: varargs (using #137's ABI
plans), `offsetof`, hints/traps, compile-time queries, floating point, memory and
strings, atomics, and frame/return addresses. Wider overflow operand types also
remain. Their actions should consume the actual typed stream and selector
interfaces as those become available.

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

## Behavioral references and provenance

The code, fixtures and benchmark inputs are original. The semantic references
are GCC's [bit operation builtins](https://gcc.gnu.org/onlinedocs/gcc/Bit-Operation-Builtins.html),
[byte swapping builtins](https://gcc.gnu.org/onlinedocs/gcc/Byte-Swapping-Builtins.html),
and [integer overflow builtins](https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html),
checked against Clang itself. No implementation or generated table from those
projects was imported. The builtin spellings are the identifiers named in #148.
