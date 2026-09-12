# AArch64 wide atomic update validation

This is the test contract for the RMW/CAS continuation of #36 after #453, not
permission to retire the direct backend. Current run results belong in a new
immutable performance audit and the implementation PR.

## Selected sequence

`MACHINE_A64_ATOMIC_RMW_PAIR` and `MACHINE_A64_ATOMIC_CAS_PAIR` operate on
sixteen-byte integer frame values. Existing canonical lowering supplies promoted
aggregate images. The constrained address is X10; observed halves are X9/X14,
operand halves are X11/X12, and the store-exclusive status is W13. CAS stages
its desired halves in X15/X17. X16 remains frame-address scratch. Metadata
publishes every clobber and flags definition, plus read/write and barrier effects.

Each retry loads frame operands before LDXP/LDAXP. Only register operations
occur between that load-exclusive and STXP/STLXP; there are no frame reloads,
ordinary loads/stores, or calls in the exclusive window. RMW arithmetic carries
or borrows across both limbs. CAS compares both halves and selects either the
desired value or the observed value for the store. Even a mismatch must complete
a successful store-exclusive before returning the observed pair: an unvalidated
pair-exclusive load need not be single-copy atomic. The direct oracle follows
the same atomicity and progress rules. Stronger ordering on a failed comparison
is permitted; unsupported memory orders are not invented by the selector.

The architecture issue underlying the failed-CAS repair is also documented by
[GCC PR111404's AArch64 correction](https://gcc.gnu.org/pipermail/gcc-cvs/2023-November/394322.html).

## Permanent tests

- `machine_test_a64_atomic_pair_updates` exercises both frontend forms, six RMW
  operations plus strong/weak CAS, and six valid ordering combinations. It
  checks payload validation, constrained registers, small/large frame expansion,
  retry destinations, and the absence of unrelated memory accesses in an
  exclusive window. `tests/aarch64_atomic_update_pair_oracle.s` retains an
  independent mnemonic assembly reference.
- `tests/basic_c_aarch64_atomic_update_pair.c` uses a two-u64-limb arithmetic
  reference. Its ten Buster functions cover all bit positions, mixed limbs,
  returned-old values, carry/borrow, strong/weak CAS, signed atomic wrap, a large
  frame, a padding-free aggregate and the original promoted nine-byte aggregate.
  The driver requires ten functions and zero fallback across three AArch64
  desktop targets, four allocators and both frontend forms; matching native
  hosts execute the result. Existing aggregate and i128 census inputs also
  require zero fallback and are not reduced.
- `compiler_driver_test_atomic_pair_contention` is registered on native Linux
  and macOS AArch64 hosts with a configured external compiler. Two independent
  all-host controls precede eight Buster/host combinations (four allocators,
  two frontend forms). The subject's six pointer/scalar-signature functions must
  have zero fallback. The host owns pthreads, independent atomic operations,
  ticket accounting and a thirty-second alarm; a parent deadline also bounds
  execution. Tests cover lost/duplicate updates through a low-limb carry,
  complement-pair tearing on failed CAS, both directions of acquire/release
  publication of ordinary payload fields, and sequentially consistent
  store-buffering outcomes. Stress is supplementary evidence, not an exhaustive
  proof of the memory model.

## External aggregate oracle boundary

The promoted nine-byte representation and deterministic zero padding are Buster
contracts. Keep their original source and assertions under `__BUSTER__`; they
still run in every Buster mode and remain included in the exact ten-function
count. External Clang controls instead exercise the added padding-free Pair
aggregate and all portable integer/ordering cases. Do not claim external
agreement about unspecified promoted padding or apply a larger representation's
comparison rules to a smaller non-atomic expected object. The initial external
Clang run of the promoted case failed; retain it as a failed oracle attempt,
not a passing comparison or a Buster regression.

## Reproduction

Use the ordinary repository build driver and an idle configured build directory:

```sh
./build.sh build --config Release -t test_all
./build.sh test_self_host --config Release
./build.sh test_mode_matrix --config Release
```

On an AArch64 Linux host, independent native controls can additionally run:

```sh
clang -std=gnu11 -O2 -fwrapv -fno-strict-aliasing -funsigned-char \
  tests/basic_c_aarch64_atomic_update_pair.c -latomic -o /tmp/pair-reference
/tmp/pair-reference
clang -std=gnu11 -O2 -fwrapv -fno-strict-aliasing -funsigned-char -pthread \
  tests/host_atomic_pair_contention.c tests/basic_c_atomic_pair_contention.c \
  -latomic -o /tmp/pair-contention
/tmp/pair-contention
```

Repeat the contention control with GCC and both controls at O0. Host compiler,
source revision, native architecture, sanitizer configuration and actual test
results must accompany acceptance. Foreign object generation alone is not
native execution; a clean stress run alone is not performance acceptance.
