# builtins

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_builtin.c` supplies the integer builtin actions for
  #148: byte swaps, bit counts/searches, and mixed-type overflow folds, with
  AVX-512 and scalar twins. `test-builtins [--seed N]` checks Clang's results,
  undefined/invalid cases and guarded tails. `benchmark-builtins [--only
  scalar|vector] [--after-scalar MS]` measures synthetic action batches on the
  locked 9700X, pinned to CPU 2. [The builtin guide](../../docs/c-builtins.md) records
  the API and the checking, C selector, feature-query and corpus integration
  still required before #148 can close.

- The companion [builtin integration guide](../../docs/c-builtins-integration.md)
  records the shared capability catalog, typed integer/varargs/misc actions and
  existing-stream runtime expansion. `test-check-builtins`,
  `test-check-varargs`, `test-check-builtins-misc`, `test-builtin-lowering` and
  `test-builtin-varargs` distinguish semantic constraints from emitted execution.
  The bit builtins (bitreverse, clrsb, the generic counts, C23's stdc ones and
  the carry chains: `c_check_builtin_bits.c`, `c_builtin_bits_lower.c`) are the
  graph's miscellaneous builtins; `test-check-builtins-bits` checks their folds
  against the host compiler's builtins, and `test-checker --program-bits` runs
  them natively against clang -O0's build, and the classic ones (`ctz` to
  `bswap64`, #1374: one instruction each by the target, the guide's "The bit
  builtins' instructions") at `x86-64`, `x86-64-v2`, `x86-64-v3` and `znver5`,
  checking that their rows are the target's forms. The generic/C23 bit counts,
  `clrsb*` and `bswapg` reuse those rows (#1652), with narrow-width corrections
  and defined zero/fallback results, including 65–128-bit operands split into
  limbs and operands past 128 bits computed limb by limb (#1447,
  `check_lower_bits_limbs`). The same checker target matrix checks their rows and boundary values;
  `test-checker-driver`'s Tier 1 fixture checks narrow and wide forms at the
  first three targets, and `tests/ir-source/bit-builtins.c` checks the IR
  lowering under command-line and per-function targets. The overflow builtins
  (#1385, the guide's "The overflow builtins' instructions") are signed longs'
  rows of the operation and `seto` and the other classes' cheapest recipes,
  `_BitInt`s of 65 to 127 bits' as `__int128`'s limbs, and past 128 bits the
  exact result in the signed `_BitInt` that holds it (#1447):
  `test-builtin-lowering` runs every recipe natively against clang's builtins,
  `test-checker`'s `overflow-instruction-rows` program by both paths against
  clang -O0 with the rows' forms, `test-checker-driver`'s `tier1-overflow`
  fixture by `--tier1`, and `tests/ir-source/overflow-builtins.c` the IR's
  lowerings, both interpreters and Tier 1. So are the alignment builtins
  (`align_up`, `align_down`, `is_aligned`, folded from a declaration's alignment
  as clang folds them, and past 128 bits limb by limb, #1447) and `expect_with_probability`, whose recipes reuse the
  bit builtins' and `expect`'s; `test-checker --program-align` runs them, and
  `classify_type`, `eh_return_data_regno`, `dwarf_cfa`, `launder` and the
  generic leaves (`debugtrap`, `memcpy_inline` and the others: the guide's
  "Generic builtins of clang's own checks and of an instruction or two"). The
  signaling NaNs and the `_Float16` and `__float128` constants (`nans`,
  `nanf16`, `inff128` and the others) are floating builtins, their NaNs' bits
  exact in static data and code (`check_action_nan_bits`); `test-checker
  --program-float-constants` runs them against clang (the guide's "Signaling
  NaNs and the `_Float16` and `__float128` constants").
  #133's producer lowers a variadic definition's `va_start`, `va_arg` (of
  structs, unions and long doubles too), `va_copy` and `va_end` through them
  over Tier 0's incoming variadic frames (#137); full Tier 1 coverage and corpus
  acceptance remain separate requirements. The atomic builtins lower through
  per-unit leaves (`c_check_program_helpers.c`), their objects aligned as
  clang's code generation aligns their addresses (`check_graph_atomic_alignment`,
  #126); `test-checker` runs them against clang's code and checks that a
  misaligned one is refused exactly where clang calls libatomic. A helper is a
  fallback (#1059): each helper family publishes its contract
  (`check_program_helper_contracts`), its code's effects read by
  `x86_semantics.c` (`check_program_helper_physical`), which `test-checker`
  checks against every helper the emitters make; a fence of a constant order is
  the typed stream's `FENCE` (`check_program_direct_fence`), Tier 0's `mfence`
  or nothing, the IR's `fence`, with no helper; `--helper-report` prints each
  family's direct forms and helper calls in the lowered code, and `test-checker-driver` runs a
  store-buffering litmus by both tiers (the guide's "Helper contracts and
  direct forms"). Seq_cst stores keep marked `SET`/IR `store` semantics and
  emit memory `xchg` in both tiers and the native fallback helper (#1396);
  relaxed/release inline stores remain `mov`, and standalone seq_cst fences
  remain `mfence`. `tests/ir-source/atomic-stores.c` checks assignment results
  and initializing stores through both interpreters and strict Tier 1, with
  separate interpreter unit checks of undefined-byte semantics.
  `--helper-forms=LIST` (the families of a direct form by
  `--helper-report`'s names, `fence`, `atomic-access`, `atomic-rmw` and `asm`,
  or `all`) gives each of their operations its helper's call, the paired
  measurements' other arm; `measure-helpers [--sqlite DIR --speedtest DIR]
  [--runs N] [--compiles N] [--warmups N] [--directory DIR] [--keep]
  [--uncounted]` (`helpers_measure.c`) builds the primitives' kernels and
  speedtest1 by Tier 0 and `--tier1`, each with and without
  `--helper-forms=all`, checks their outputs alike, and times their compiles and
  runs as whole processes in a Williams design's order beside each object's helper calls,
  executable bytes and Tier 1's spill and call stores and loads; the 9700X job
  runs it under the `helpers` gate (the guide's "Paired measurements"). The x86
  builtins whose instructions no Tier 0 row takes (`rdtsc`, `pause`, `xgetbv`,
  `xsetbv`, `monitorx`, `mwaitx`, `tzcnt`, `lzcnt_u16`, the fences, `clflush`,
  `ldmxcsr`, `stmxcsr`, Key Locker's, `umwait`, `tpause` and `xbegin`) are
  per-unit leaves too (`c_builtin_x86_helpers.c`), Key Locker's
  of vectors and pointers both, and its encryptions' with a branch that branch
  sizing sizes. The library builtins (`c_builtin_library.c`) are calls of their
  C library functions by name, with their rows' prototypes, folded where clang
  folds them (`c_check_builtin_library.c`), and the plain names (`sin`,
  `strlen`) are builtins as `-fno-builtin`, `-fno-builtin-NAME` and
  `-ffreestanding` say, declared implicitly with their prototypes as clang
  declares them; a plain call of `memcpy`, `memmove` or `memset` of a constant
  size of at most 32 bytes, of `abs`, `labs` or `llabs`, or of `fabs`,
  `copysign` or their kin is computed in its place, as clang -O0's code
  computes its intrinsic (#1373); `test-checker --program-library` and
  `test-checker-driver` check them against clang (the guide's "Library
  builtins"). The plain table is
  pinned to Clang 23.1.4's official build, CI's reference: its GNU library
  builtins include `strlcat` and `strlcpy`, as every upstream 23.x release's
  do. Debian's and Ubuntu's clang packages (apt.llvm.org's too, which cloud
  containers have) drop both with
  `debian/patches/23-strlcpy_strlcat_warning_removed.diff`, in every release.
  The preprocessor and checker oracles compare `clang --version` with the
  reference, report `expected clang 23.1.4, found …` (`found Ubuntu 23.1.6`)
  when another build is found, and skip only the plain names that build's
  vendor is known to classify differently (the named list
  `cbuiltin_library_table_differences`: Debian's and Ubuntu's `strlcat` and
  `strlcpy`), a difference no named build explains still failing. A fixture
  reruns the comparisons under a fake Ubuntu clang 23.1.6, so CI's reference
  build exercises the skip path too; the fake's script quotes the clang it forwards to, which it
  reaches through a path with a space and the shell's metacharacters. The floating
  classifications past the first ones (`isnormal` to `isfpclass`, `signbitf`,
  `signbitl` and `canonicalize`: `c_check_builtin_float.c`,
  `c_builtin_float_lower.c`) test their `FPClassTest` masks by a table, an x87
  value's from a model of LLVM's expansion (`cbuiltin_float_lower_x87_class`),
  and `test-checker` runs every mask against clang -O0's builds.
  Every build/test/measurement uses the approved 9700X lock per run.
