# Mixing the complete intern-hash state

Preparing the target comparison exposed a failure in the unchanged baseline:
`test-refinement --count 2000 --seed 116` reports one unchanged hash among
14,861 paired bit-flip cases. All scalar/vector hashes, IDs, normalized
spellings and whitespace still agree. The table compares complete keys and
correctly distinguishes colliding spellings.

The failure is reproducible without randomness. Start with 167 `e` bytes,
replace byte 165 with `0x06`, then XOR bit `0x02` in bytes 133 and 165. The two
keys have the same old hash, `64d4cbaa7790e0cd`.

There are valid C identifier examples too. For lengths 46, 47 and 48, start
with `b` bytes, set byte 0 to `a` and byte 34 to `k`, then XOR bit `0x08` in
bytes 2 and 34. Before and after, these hash to the same old value:

| Length | Old hash |
| --- | --- |
| 46 | `0979e6c8a0ac56ab` |
| 47 | `d1a04fc9f511ab03` |
| 48 | `3f3938be4f8e3426` |

The old algorithm truncates the XOR of the whitened lanes' two AES rounds to
64 bits. The paired changes can cancel in that low half while changing the
discarded high half. One final AES round on the complete folded/chained state,
before truncation, breaks these examples. The four-spelling vector path runs
that round on all four folds together. The hash remains noncryptographic;
these checks do not establish collision resistance for arbitrary inputs.

This changes internal intern-table hashes. It preserves spellings, first-seen
IDs, keyword IDs, layouts and the independent oracle's answers. `link_hash`,
which the link representation uses, is separate and unchanged. The final
performance qualification must include the extra mixing round's cost.

`refine_check_fold_mixing` retains all four counterexamples as default test
cases, checking hash separation, scalar/vector equality and distinct stable
IDs. No existing hash-quality assertion has been weakened or removed.

Local diagnostic validation on the Ryzen 9 7940HS with Clang 23.1.3:

- `hash-seeds.c`, seeds 1 through 128: 25,600,000 random hash comparisons,
  11,272,192 structured spellings and 1,898,354 paired flips, all passing.
- Full refinement suite with seed 116, repository sources and frozen SQLite
  3.53.4: all passing, including the four fixed pairs and eight growth cases.
- `baseline-seed116-local.txt` records the original failing run;
  `finalmix-seed116-local.txt` and `hash-seeds-local.txt` record the corrected
  runs. These are correctness results, not target performance evidence.

Build the extended driver with the normal compiler flags and `-Isrc`, then
run it without arguments for the 128-seed hash checks. On the 9700X, every
build and execution must use the shared per-run lock.
