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
  them natively against clang -O0's build. So are the alignment builtins
  (`align_up`, `align_down`, `is_aligned`, folded from a declaration's alignment
  as clang folds them) and `expect_with_probability`, whose recipes reuse the
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
  misaligned one is refused exactly where clang calls libatomic. The x86
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
  declares them; `test-checker --program-library` and `test-checker-driver`
  check them against clang (the guide's "Library builtins"). The floating
  classifications past the first ones (`isnormal` to `isfpclass`, `signbitf`,
  `signbitl` and `canonicalize`: `c_check_builtin_float.c`,
  `c_builtin_float_lower.c`) test their `FPClassTest` masks by a table, an x87
  value's from a model of LLVM's expansion (`cbuiltin_float_lower_x87_class`),
  and `test-checker` runs every mask against clang -O0's builds.
  Every build/test/measurement uses the approved 9700X lock per run.
