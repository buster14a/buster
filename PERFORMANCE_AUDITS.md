# Performance audit notes

Performance audit history for this repository, newest first. Every entry is a
record of what was measured, what was fixed, and what the numbers were at the
time; the methodology for taking new measurements — which benchmark to trust,
how to profile the sanitized and Release trees, how to symbolize — stays in
the "Benchmarking and diagnostics" section of `AGENTS.md`.

**Read the newest entry before starting performance work.** It carries the
reference points the next audit is measured against, the finds that were
deliberately left untaken, and the mistakes an earlier audit already paid for.
When an audit lands, add a new dated entry at the top and leave the older ones
as written — they are a record, not documentation to keep current.

`2026-08-08l` (Linux x86_64; the same branch as `2026-08-08j`, taking the
remaining ranked leftovers — arena reuse routing, the generated-blob
chunk-pointer table, and the codegen scan items — measured change-by-change
against the `2026-08-08j` reference points; every number from a clang-built
binary). Stage 1 `5.292 G` to `5.136 G` instructions (`-2.9%`, minor faults
`274.2 k` to `270.7 k`), Release `ide test` `7.047 G` to `6.822 G` (`-3.2%`,
faults `84.6 k` to `83.4 k`), sanitized `ide test` `101.006 G` to
`100.611 G`, `ide bench` instruction-neutral (`713.2 M`), byte-identical
fixed point after every change, all 29 modules in Debug, Release, sanitized,
GCC, and Zig trees. Stage 2 `78.7 G` to `76.0 G`, validation only.

- **The arena reuse pool takes non-default reservation sizes through an
  opt-in flag.** The driver's 8 GB per-translation-unit arenas paid a full
  mmap + first-touch zeroing + munmap cycle per unit compiled (once per
  driver test fixture, once per TU in multi-TU links); pooling stays keyed
  by exact reservation size and `pool_reuse` is set only by creation sites
  whose consumers never assume freshly zeroed pages. **A blanket
  generalization measured broken, not slow**: UI tests spun forever walking
  a box tree built from recycled dirty pages — custom-size arenas have
  latent fresh-mmap zero assumptions, which is exactly what the opt-in
  fences off. Release test faults `-1.2 k`, instruction-neutral.
- **Generated assembly blobs decode through chunk pointer/length tables.**
  `import_assembly_metadata`'s aarch64-only mode now regenerates the x86
  artifacts from the pinned reduced `x86_64-xed.jsonl` (new parser, proven
  by re-emitting the records and requiring the byte-identical checked-in
  corpus, and by an unchanged-template run reproducing the previous header
  bit for bit) — template changes no longer need a raw XED checkout. Every
  `*_blob_char` switch (the 560-way one included) became a table index, and
  `_u8_counted` reads its whole base64 group from one chunk. buster cc
  lowers the constant symbol-address tables through the existing
  static-initializer relocation path; the fixed point exercises it end to
  end. Landing this exposed a pre-existing quadratic it amplified:
  `c_ir_symbol_is_thread_local` scanned the whole entity and global tables
  per address-constant initializer element; every data-symbol site already
  records the flag on the IrSymbol, so it is one lookup now. Together with
  empty-side-table fast paths in the two label-metadata validators (with no
  metadata in a function they provably reduce to operand-existence checks
  plus the LABEL_ADDRESS rejection), stage 1 `-3.1%` and Release test
  `-3.2%` on their own.
- **The codegen per-function scan fusion is closed as measured-negative**:
  one combined instruction walk cost `+6 M` instructions and `+3.5%` min
  cycles versus the three separate tight scans (`2.736 G` to `2.838 G` min
  stage-1 cycles) — clang optimizes the simple opcode-only loops better
  than one branchy pass. Do not retry. What survived: the Windows x64
  outgoing-stack sizing pass already computes every call's layout, and a
  per-instruction cache now hands those to emission instead of recomputing
  per call (Linux-neutral; the win is the Win64 CI runner).
- **Still open, with fresh evidence.** `c_ir_query_execute` is now the top
  Release-test symbol (`11.55%`, mostly inlined `_attempt` bodies): the
  query machine deliberately re-runs a parent attempt from scratch after
  each missing sub-query completes, so the cost is restart re-execution,
  not the completed-list scan; a structural cut means resumable attempts,
  a deep redesign of the machine (the 08-08e result-memo remains negative —
  don't retry it). `c_parse_type_layout` (`~2.5%` of stage 1) pays an
  O(type-table) seeding pass per sizeof/alignof in constant expressions;
  the audit-proposed persistent cache must survive in-place type
  completion — incomplete enums carry a provisional 4/4 layout until their
  underlying type lands and character arrays infer bounds from
  initializers later, so only completed struct/union layouts are safely
  immutable, and a demand-driven rewrite has to carry the array-bound
  arm's embedded constant evaluator with it.
- Reference points for the next audit, all clang-built: stage 1 `5.136 G`
  instructions / `~270.7 k` minor faults, Release `ide test` `6.822 G` /
  `~83.4 k` faults, sanitized `ide test` `100.611 G`, `ide bench`
  `713.2 M` per run. Stage 2 `76.0 G`, validation only.

`2026-08-08j` (Linux x86_64; the IrValue label-metadata side table — the
structural item every audit since `2026-08-08g` listed as THE open shape
change, done as its own change per the `2026-08-08h` warning. Every number
from a clang-built binary). Stage 1 `5.473 G` to `5.292 G` instructions
(`-3.3%`, minor faults `287.1 k` to `274.2 k`), Release `ide test` `7.273 G`
to `7.047 G` (`-3.1%`, faults `85.9 k` to `84.6 k`), sanitized `ide test`
`103.677 G` to `101.006 G` (`-2.6%`), `ide bench` instruction-neutral
(`713.0 M`), byte-identical fixed point (`SELF_HOST deterministic
bytes=28718824`), all 29 modules in Release, sanitized Debug, GCC, and Zig
trees. Stage 2 `80.0 G` to `78.7 G`, validation only.

- **IrValue shrank 64 to 24 bytes.** The seven address-of-label provenance
  fields (`is_label_value`, `has_label_provenance`, `has_non_label_provenance`,
  `label_blocks`/count, `label_paths`/count) moved into `IrValueLabelMetadata`
  entries in a per-function side table sorted by value id
  (`ir_value_label_metadata_find`/`_ensure` plus a by-value getter that
  returns zero metadata for absent entries). Every validator and provenance
  helper keeps its exact logic reading through the table — including the
  `!first` operand rejections in `ir_label_metadata_transfer_valid` and the
  LABEL_ADDRESS/INDIRECT_BRANCH shape rules — so forged-metadata tests and
  malformed-IR validation behave identically; only the storage moved.
- **The C frontend now tracks label metadata only when the function can
  produce a label value.** `c_ir_lower_body_initialize`'s existing
  task-capacity token walk also spots `&&` followed by an identifier naming a
  defined label (a variable sharing a label's name after a logical `&&` only
  costs a spurious enable), and referencing a global that carries
  label-address relocations enables tracking lazily. Everything else skips
  the apparatus: no non-label provenance paths for ordinary pointer stores,
  no `c_ir_label_metadata_store_for_place` root walks per store
  (`emit_store_place`'s 95%-metadata cost), no per-function
  `label_metadata_store_*` scratch arrays (13 bytes/value zeroed or
  0xff-filled), and empty tables make the per-value label checks in both
  validators nearly free (`transfer_valid` was `13 ms`/run). Functions with
  label addresses run the identical tracking from the first statement, so
  mixing rejections and the dynamic-index failure messages are unchanged
  there. The observable delta in label-free functions: metadata-path soft
  `failure_message` text can no longer be planted, and the tracking-capacity
  hard failure cannot trigger.
- Remaining leads carried from `2026-08-08i`, all still open: routing the
  per-include c_lex arenas through the arena reuse pool (the rest of the
  memory-touch tax), the generated-blob chunk-pointer table (`*_blob_char`
  560-case switch, ~`20 ms`/test + cc), `c_ir_query_execute` (`41.5 ms`
  exclusive; memo negative in 08-08e), the `c_parse_type_layout`
  rollback-poisoned persistent cache, and fusing the three per-function
  codegen scans.
- Reference points for the next audit, all clang-built: stage 1 `5.292 G`
  instructions / `~1.1 s` wall / `~274 k` minor faults, Release `ide test`
  `7.047 G` / `~84.6 k` faults, sanitized `ide test` `101.006 G`,
  `ide bench` `713.0 M` per run. Stage 2 `78.7 G`, validation only.

`2026-08-08i` (Linux x86_64; started from the two 18:1x Superluminal captures
of Release `ide test` and the stage-1 `ide cc`, analyzed with
`SuperluminalCmd llm` per-line annotations plus full call-graph traversal —
the traversal attributed the kernel time exactly: `kernel_init_pages` was the
#1 stage-1 exclusive symbol at `114.5 ms`. Every number from a clang-built
binary, measured change-by-change). Stage 1 `6.450 G` to `5.473 G`
instructions (`-15.2%`, wall `~1.15 s` to `~1.04 s`, minor faults `309.5 k`
to `287.1 k`), Release `ide test` `7.629 G` to `7.273 G` (`-4.7%`, faults
`87.7 k` to `85.9 k`), sanitized `ide test` `121.695 G` to `103.677 G`
(`-14.8%`), `ide bench` instruction-neutral (`713.0 M` exactly), byte-identical
fixed point (`SELF_HOST deterministic bytes=28704144`), all 29 modules in
Release and sanitized Debug. Stage 2 `91.4 G` to `80.0 G`, validation only.

- **`__LINE__`/`__FILE__` were rebuilt from scratch on every logical line of
  every file.** `c_preprocess_builtins` ran at the top of each main-loop
  iteration: a `string_format` of the line number, a byte-by-byte re-quote of
  the file path, and two `c_macro_define` calls (each a hash find), all
  arena-allocated and immediately garbage. The two macros are now defined once
  with a `builtin` kind, the loop stores the current logical line/path into
  two head-of-list fields (`builtin_line`/`builtin_path` on the first
  `CMacro`), and `c_macro_replacement_tokens` — the single point both expander
  paths and conditional evaluation flow through — materializes the one-token
  replacement only when the macro actually expands. `#undef` skips
  builtin-kind macros (the per-line rebuild made them un-undefinable, so the
  observable behavior is preserved). Stage 1 `-9.3%` instructions and
  `-21.6 k` faults on its own; also killed the `string_format_va` `12 ms` the
  capture showed inside `c_preprocess`.
- **The self-host stage caught a real codegen bug in this change.** The first
  materialization used a hand-written backward `do/while` itoa
  (`digits[9 - length]`); clang-built `ide` compiled and ran it correctly, but
  the buster-compiled stage 1 miscompiled it and stage 2 failed with "could
  not lower logical expression core" on `__LINE__` — the diagnostic blames the
  consumer, not the macro. Minimal repro (25 lines, segfaults under
  `ide cc`, correct under clang): the same loop inside a struct-returning
  function; in `main` or with forward indexing it is correct. Follow-up task
  filed; the shipped materialization formats via `string_format` (runs
  per-expansion, so the win stands). Lesson repeated from `2026-08-08d`:
  **the fixed point is the dialect *and* codegen gate — run `test_self_host`
  after each frontend change, and never behind a pipe that eats the `cc:
  error` line** (a `| grep instructions` hid this failure for three
  measurement rounds).
- **`c_translate_source` was 55% of `c_lex`'s exclusive time, one byte at a
  time.** The phase-1 loop (CR/LF normalization, splice removal, line/column
  checkpoints) now finds the next `\r`/`\n`/`\\` by SWAR zero-in-word
  detection eight bytes per step and bulk-copies the clean run (checkpoint
  sequence provably identical: a clean run is exactly the case where
  offset/column stay linear). Stage 1 `-295 M` instructions (`-5.1%` with the
  asm-operand fix below). Two SWAR attempts in the same family **measured
  negative and were reverted**: the `c_lex` comment-body scan (`+12 M` —
  comments are only ~4% of `c_lex` and `/* ... */` borders are full of `*`
  bytes that defeat the probe) and a tight whitespace-run loop (`+6 M` —
  single spaces dominate, the extra compare loses to the loop re-dispatch).
- **`buster_x86_metadata_hash_string` zeroed a 4 KB stack buffer and copied
  the string byte-by-byte per hash** — 20 strings per form over 11013 forms
  at decode/validation, ~900 MB of memset per `ide test`; both captures put
  `__memset_avx512` under `validate_form_record`. It hashes the pool span in
  place now. Most of this round's sanitized drop beyond the builtins fix.
- Four smaller scans, in one batch worth test `-2.8%` / stage 1 `-1.5%`:
  `codegen_target_for_abi` re-folded a six-feature array per aggregate-ABI
  query (now a lazily built per-abi table); `c_ir_integer_literal_fits` did an
  `ir_type_from_id` chase per candidate per literal (a `literal_limits[kind]`
  max-value array filled at scalar-type creation answers it in one compare;
  zero means "not cached", falling back to the exact old path);
  `c_parse_lookup_entity` re-hashed the name once per scope level (hoisted —
  it is loop-invariant); and the IR-side group-scan walkers
  (`c_ir_scan_delimiter_group`, `c_ir_root_conditional`,
  `c_ir_expression_core_range`, `c_parse_asm_operand_name_token`) copied a
  48-byte `CToken` per scanned position to ask one punctuator question — they
  read the `punctuator` id through the pointer now (digraph ids are distinct,
  so the old one-character-spelling exclusion is preserved exactly).
- **Remaining shape, for the next audit.** The memory-touch tax is now the
  stage-1 headline by far: `kernel_init_pages` `114.5 ms` + fault plumbing +
  `__zap_vma_range` `37 ms` teardown in the capture (this round only took
  `-22 k` of the `309 k` faults). Attribution: `c_lex` include arrays
  `~26 ms`, `c_ir_emit_local` `23 ms`, `debug_model_build` `16.4 ms`, rest
  diffuse. The levers are row shrinking (the IrValue label-metadata sparse
  side table stays the top open structural item: 32 of 64 bytes per value,
  plus `ir_label_metadata_transfer_valid` `13 ms`/run and
  `c_ir_emit_store_place`'s 95%-on-metadata `19 ms` in the test capture) and
  routing the per-include `c_lex` arrays through the arena reuse pool.
  Second structural item: the generated-blob accessors
  (`*_blob_char` is a 560-case switch called four times per decoded byte over
  a 1.7 MB pool — `~20 ms` of every test run, also on the `cc` path); the
  generator should emit a chunk-pointer table so decode becomes per-chunk
  `memcpy` plus a flat base64 loop. Also open: `c_ir_query_execute` `41.5 ms`
  exclusive in the test capture (retry-shaped; memo measured negative in
  `2026-08-08e`, needs a structural cut, and `16 ms` of its time is
  `__memcmp` under `c_ir_type_name_internal_attempt`);
  `c_parse_type_layout` re-runs a whole-type-table builtin sizing pass plus
  fixpoint per static assert (`~20 ms` stage 1 — needs a rollback-poisoned
  persistent layout cache, fill header outside `CParseResult` like the
  aggregate probe); `codegen_generate_canonical_module` still runs three
  separate per-function instruction scans (`direct_call_uses` 14.4%,
  `saves_rbx` 6.8%, `x64_call_layout` 13.5% of its samples) that want fusing
  into one pass; lane-parallel per-function codegen remains the big blocked
  lever. Environment note: on this host the sanitized `ide` now needs
  `ASAN_OPTIONS=verify_asan_link_order=0` (a preloaded library wins the
  initial-library-list race); the suite itself is unaffected.
- Reference points for the next audit, all clang-built: stage 1 `5.473 G`
  instructions / `~1.04 s` wall / `~287 k` minor faults, Release `ide test`
  `7.273 G` / `~85.9 k` faults, sanitized `ide test` `103.677 G`, `ide bench`
  `713.0 M` instructions per run. Stage 2 `80.0 G`, validation only.

`2026-08-08h` (Linux x86_64; the structural follow-up that took three of the
`2026-08-08g` leftovers the same day — every number from a clang-built
binary, measured change-by-change). Release `ide test` `8.818 G` to
`7.628 G` instructions (`-13.5%`, wall `~0.86 s` to `~0.79 s`, minor faults
`101.6 k` to `87.7 k`), stage 1 `6.554 G` to `6.450 G` (`-1.6%`, minor
faults `326 k` to `309 k`, wall `~1.22 s` to `~1.15 s`), sanitized
`ide test` `141.964 G` to `121.695 G` (`-14.3%` across both rounds), `ide bench` instruction-neutral
(`713.0 M`), byte-identical fixed point after each change.

- **Preprocessed output is now a list of contiguous token ranges, and
  expansion-free lines never touch the expansion machinery.** A logical line
  whose identifiers name no defined macro expands to itself, so `c_preprocess`
  appends its staging array directly as a `CPreprocessTokenRange` (adjacent
  ranges merge); only lines that actually expand still run
  `c_preprocess_expand` and materialize one exact-size array from their node
  list. Final assembly is a few `memcpy`s instead of the per-token node-list
  walk that held `3.2%` of stage-1 leaf samples, and unexpanded tokens stop
  paying a task node plus an output node each. Stage 1 `-1.9%` instructions,
  `-18 k` faults, and most of this round's stage-1 wall drop.
- **The parse side got the IR side's matching-delimiter index.**
  `CTokenPositionIndex` now carries a whole-stream `matching_delimiters`
  table (closer position per properly nested `(`/`[`/`{`, built in the same
  lazy sweep as the spelling positions; a mismatched closer unmatches
  everything still open so malformed regions keep their exact scalar walks).
  The type-parse machine's PARAMETERS walk and BEGIN-stage bracket scan hop
  whole nested groups through it — a properly nested group is paren-balanced
  inside, so the hop leaves the depth walk identical. Release `ide test`
  fell `12.9%` — the machine's re-scans over nested declarator groups were
  far bigger than its `9.5%` self time suggested — for `+0.26%` stage 1
  (the 4 B/token table on the unity TU), the same trade the `2026-08-08f`
  position index made at ~50x return.
- **Destroyed default-shaped arenas park in a per-thread pool for reuse**
  (`ARENA_POOL_LIMIT` 16). The reservation and its faulted pages survive, so
  the next `arena_create` skips the `munmap`/`mmap` pair and the kernel's
  first-touch zeroing; a reused arena hands out dirty bytes — the same
  contract `arena_reset_to_start` already imposes everywhere. Thread-local
  (`BUSTER_THREAD_LOCAL_DECL`) so it degrades to a plain global in
  single-threaded builds and stays race-free in threaded ones; execute,
  locked, multi-arena, and custom-size reservations bypass it unchanged.
  Worth `-4 k` faults and `~15 ms` of `ide test` wall; instruction-neutral.
- **Measured negative, reverted: staging the per-function IR arrays in the
  lowering scratch with exact-size copy-out.** The theory (THP zeroes the
  worst-case capacity slack folio-by-folio) did not hold up: `-4.6 k`
  stage-1 faults for `+14 M` instructions, and `ide test` faults *rose*
  `13 k` because the enlarged scratch reservation fell out of the arena pool
  and churned per fixture. It also tripped the 64 MB scratch reservation on
  the hardening tests' giant functions. The real IrValue shape work — the
  sparse label-metadata side table (432 field references across four files)
  — remains open and should be done as its own change, not as a staging
  trick.
- Reference points for the next audit, all clang-built: stage 1 `6.450 G`
  instructions / `~1.15 s` wall / `~309 k` minor faults, Release `ide test`
  `7.628 G` / `~87.7 k` faults, sanitized `ide test` `121.695 G`,
  `ide bench` `713.0 M` instructions per run.
  Stage 2 `91.4 G`, validation only.

`2026-08-08g` (Linux x86_64; started from the two 15:58 Superluminal captures
of `ide test` and the stage-1 `ide cc`, then re-profiled after each round with
`perf record --call-graph fp` plus batch `llvm-symbolizer --inlines` over the
leaf sample addresses — the leaf histogram named four scans the function-level
capture view smeared. Every number from a clang-built binary). Eight fixes:
stage 1 `8.548 G` to `6.554 G` instructions (`-23.3%`, wall `~1.40 s` to
`~1.22 s`), Release `ide test` `9.150 G` to `8.818 G` (`-3.6%`), sanitized
`ide test` `141.964 G` to `139.852 G` (`-1.5%`), at the byte-identical fixed
point with all 29 modules passing in both configurations. `ide bench`
instruction-neutral (`713.1 M` to `713.0 M`). Stage 2 `121.6 G` to `92.2 G`
(`-24%`, validation only). Minor faults unchanged by design (`~325 k`) — this
audit took instruction quadratics; the fault volume remains the top leftover.

- **The file-scope redeclaration lookup was ~12% of stage 1 on its own.** For
  every file-scope declaration, `c_analyze_semantics` scanned the whole entity
  table for a same-named scope-0 entity (`existing`/`conflicting` selection).
  A name-keyed hash chain (`next_by_name` + `name_lookup_buckets`, filled in
  `c_parse_scope_add_entity` beside the existing scope- and typedef-chains)
  now serves it: candidates are gathered from the chain (newest-first) and
  replayed in ascending entity order so `existing`/`conflicting` pick the
  same entities the linear scan picked; a 64-candidate overflow falls back to
  the original scan. The same chain replaced the two linear entity scans in
  `c_parse_type_layout` (typedef probe via the existing typedef chain,
  enumerator/constexpr probe via `c_parse_first_constant_entity`) and the
  identical enumerator scan in `c_ir_array_bound_evaluate_attempt` — together
  the `c_parse_type_layout` static-assert path fell from `46.5 ms` exclusive
  in the capture to `~1.9%` of a shorter run, and stage-1 `__memcmp` samples
  fell to zero.
- **`c_parse_bind_function_body` classified every body token with all seven
  predicates.** `label_address` (a backward paren-matching scan per token),
  `asm_operand_name`, and `c_ir_named_label_at` were computed per token and
  then discarded unless the token was an identifier. The block is now gated on
  `token.kind == C_TOKEN_IDENTIFIER` (exact: every predicate implies it) with
  the scanning predicates evaluated last under `&&`. The stage-1 capture had
  charged `31 ms` to the label-address prefix scans alone.
- **Canonical IR validation ran twice per compile.** The driver validates
  with detailed diagnostics at driver.c:1355, then
  `codegen_generate_canonical_module` re-validated the same module
  (`~34 ms`/run each, and `ir_label_metadata_transfer_valid` is the dominant
  per-value cost). `CodegenModuleOptions.assume_validated` — set only by the
  driver — skips the second run; every other caller keeps the internal
  validation.
- **`codegen_record_canonical_locations` was locals x instructions.** Per
  debug local it scanned all instructions for the defining
  `LOCAL`/`ARGUMENT`; one pass now builds a `local id -> place` array
  (first-definition-wins, ids `>= local_count` keep the scan as fallback).
  `30.5 ms` exclusive in the stage-1 capture.
- **Codegen label-address relocation resolution rescanned every module
  relocation per function.** Label-address relocations only come from global
  initializers (computed-goto tables); a side index list built after global
  emission is walked and compacted per function instead. `~1.7%` of stage 1
  at the scan line.
- **The C-type -> IR-type mapping fixed point swept every type per pass.**
  `c_lower_to_ir`'s two-round pass loop now iterates a compacting worklist
  (`c_ir_type_mapping_pending`: unmapped, qualified-alias, or mapped
  aggregate with unresolved layout — the three gates the pass branches use).
  Pending is monotone within a round, so compaction cannot drop a type that
  could still act; the worklist preserves ascending type order. This was
  `~4.4%` of the Release test run (leaf samples at the function-type
  parameter walks) — tests run `c_lower_to_ir` per fixture.
- **Both lexers now skip literal bodies eight bytes per step** through the
  same table-AND idiom as the identifier runs (`2026-08-08e`): the buster
  tokenizer string scan (quote, backslash, CR/LF, recovery `;`, non-ASCII
  stay special so malformed-UTF-8 stepping is unchanged) and the C
  char/string literal scan (both quote kinds, backslash, newline).
  Instruction-neutral on `ide bench` (its corpus has short strings); the win
  is in `c_lex` and the string-heavy test fixtures.
- **Remaining shape, for the next audit** (unchanged fault volume is the
  headline): `kernel_init_pages` was `120 ms` of the stage-1 capture — the
  peak leftover, split across lexer arrays, `c_ir_emit_local` (`28 ms`),
  `debug_model_build` (`15 ms`); IrValue's inline label-metadata fields are
  the likely biggest slice and want a sparse side table. The preprocessor
  flattens its token-node list with a pointer-chase copy loop that alone held
  `3.2%` of stage-1 leaf samples (c.c `result.tokens[output_index++] =
  node->token`) — chunked output arrays would kill the copy and the 16-byte
  node headers. `ide test` still spends `~60 ms` in per-test arena
  create/destroy fault churn; arenas reset dirty today, so a reuse pool has
  the same contract. `c_type_parse_machine_run` is still the top Release-test
  symbol (`9.5%`, the PARAMETERS token walk) — a parse-side matching-paren
  index (the IR side already has one) is the structural fix. Lane-parallel
  per-function codegen remains the big blocked lever. Small residues:
  `c_macro_find` `1.6%` (hash tags), `codegen_record_line` `1.6%`,
  `buster_x86_metadata_decode_tables_once` runs on the `cc` path (`~1.2%`).
- Reference points for the next audit, all clang-built: stage 1 `6.554 G`
  instructions / `~1.22 s` wall / `~326 k` minor faults, Release `ide test`
  `8.818 G`, sanitized `ide test` `139.852 G`, `ide bench`
  `713.0 M` instructions per run. Stage 2 `92.2 G`, validation only.

`2026-08-08f` (Linux x86_64; the finds came from two fresh Superluminal
captures — the stage-1 compile and, for the first time, `ide test` itself —
cross-checked with perf + `llvm-symbolizer --inlines` because the hot step
functions are fully inlined and the capture attributes only their call lines.
Every number from a clang-built binary). One fix: Release `ide test`
`10.259 G` to `9.148 G` instructions (`-10.8%`, `c_frontend_tests` `428 ms`
to `386 ms`) and sanitized `ide test` `170.937 G` to `141.964 G` (`-17.0%`),
at the byte-identical fixed point with all 29 modules passing in both
configurations and GCC warning-clean. Stage 1 `8.498 G` to `8.548 G`
(`+0.6%`) — the accepted cost of the index build below on the unity TU.

- **Two "does this range contain spelling X" scans were ~10% of the Release
  test run.** The `ide test` capture put `c_type_parse_machine_run` first at
  `83.7 ms` exclusive and `c_parse_apply_vector_attribute` at `37.6 ms`;
  inline-aware symbolization placed the machine's time on the `_Alignas`
  parameter sweep and the vector-attribute call. Both asked the same question
  per declaration scan over overlapping token ranges. `CTokenPositionIndex`
  now records the sorted token positions of the two rare spellings once per
  parse (built lazily from the fixed token stream, header behind a stable
  pointer so speculative rollbacks cannot rewind or rebuild it), and both
  queries became a binary search returning the same lowest-position match the
  linear scans found. Sanitized fell `17%` because each scanned token was an
  out-of-line `-O0` predicate call there. The `+0.6%` stage-1 instruction
  cost is the one-time class computation for every identifier token of the
  unity TU; its Release-test return is 20x that.
- **Remaining shape, for the next audit:** the test capture shows
  `kernel_init_pages` `44.5 ms` plus `__zap_vma_range` `15 ms` inside
  `ide test` — per-test arena create/destroy churn, a different fault source
  than stage 1's, untouched. Stage 1's fault attribution is now diffuse
  (`c_ir_emit_local` `21 ms`, `debug_model_build` `15 ms`, lexer arrays
  `~34 ms`) — each needs its own array-shape change; no single fix remains.
  After the position index, the Release-test profile top is
  `c_type_parse_machine_run` `10.9%` (now genuinely the machine's own step
  dispatch), `c_ir_query_execute` `8.3%` (the retry-shaped machine, negative
  memo result recorded in `2026-08-08e`), and `c_lower_to_ir` `7.2%` with its
  cost smeared across call sites — structural work, not scan removal.

`2026-08-08e` (Linux x86_64, method of `2026-08-08d`; every quoted number from
a clang-built binary, `perf stat -e instructions,minor-faults` plus the same
`perf record`/annotate flow, buster-built stages fixed-point validation only).
This audit took the `2026-08-08d` leftovers plus the first slices of the
memory-shape and SWAR proposals, single-threaded throughout. Release
`ide test` `12.087 G` to `10.259 G` (`-15.1%`, `c_frontend_tests` `466 ms` to
`428 ms`, `x86_64_metadata_tests` `166 ms` to `68 ms`), sanitized `ide test`
`185.739 G` to `170.937 G` (`-8.0%`, `x86_64_metadata_tests` `1.60 s` to
`0.78 s`), and stage 1 held flat in instructions (`8.486 G` to `8.500 G`)
while its **wall fell `~1.50 s` to `1.24 s` (`-17%`) and minor faults fell
`426k` to `325k` (`-24%`)** — the win this round is memory shape, not
instruction count. Byte-identical fixed point, all 29 modules in Release and
sanitized Debug, GCC warning-clean, `ide bench` A/B `718.6 M` to `713.1 M`
instructions with `BENCH_PARSE` min `~92 us` to `~88.6 us`.

- **A memoized "pure" function must be keyed on its whole input closure.**
  `buster_x86_metadata_physical_register_view` resolves an operand's register
  class/width through ~60 case-insensitive literal probes; a memo keyed by
  `atom_offset` alone shipped six test failures because two branches also read
  `operand.width_offset` (variable-width GPR atoms). Keyed on the pair it is
  correct and worth most of the metadata module's drop in both configurations.
  The audit's `x86_64_metadata_test` cohort sweeps also computed the same nine
  per-form token answers twice (now one mask pass) and probed sixteen ` SCCn `
  tokens per form where one ` SCC` probe clears almost every form.
- **Two more negative results, measured and reverted rather than shipped.** A
  machine-lifetime success-only memo over the C query machine
  (`c_ir_query_execute`) measured `+0.4%` stage 1 and no Release change: the
  per-root completed window already captures intra-root reuse, cross-root
  repeats of the same token range are rare, and the hash costs more than the
  scans it saves. The per-token spelling-class memo (below) likewise costs
  `+0.2-0.4%` in Release where `string_equal`'s length early-out is already
  free — it stays because the sanitized run, where each predicate is an
  out-of-line `-O0` call, pays for it.
- **The parser's spelling predicates now read one lazily computed class byte
  per token** (`token_classes` in `CParseResult`, immune to speculative-parse
  rollback because spellings never change): declaration-keyword-for-dialect at
  four binder scan sites and the `vector_size` probe in
  `c_parse_apply_vector_attribute`. The three sites in the syntax-only
  declaration scanner keep the direct predicate — they have no `CParseResult`.
- **`c_translate_source` no longer writes a 16-byte location per source byte.**
  Locations are checkpoints where the offset/column linearity breaks (byte
  after a newline, byte after a splice) plus a cursor in `CLexResult` that
  amortizes lookups to one advance, since token and diagnostic offsets only
  grow. This is most of the fault reduction and the stage-1 wall drop; the
  worst-case checkpoint arrays are still reserved at source length but only
  one entry per line is ever touched.
- **Identifier runs scan eight bytes per step** in both lexers through a
  256-byte continue table built from the scalar predicate at first use (no
  drift), ANDing eight entries and falling back to the byte loop at the run's
  end. Worth `-5.5 M` bench instructions and part of the stage-1 wall drop;
  the full simdjson-style block classification (whitespace, comments, strings
  in one pass) remains open and is the natural next step of this line.
- `c_ir_add_array_type` got the pointer cache's watermark-sweep index keyed
  `(element, count)` — cold in every current profile, taken as insurance while
  the shape was fresh.
- **Leftovers for the next audit:** stage-1 instructions are flat and its
  profile has no dominating function left; the remaining big levers are
  structural — the lane-parallel per-function codegen/object path (blocked on
  wanting threads), the full SWAR lexer, and the rest of the fault volume
  (`325k` ≈ 1.3 GB touched: token arrays at 40 B per actual token, parse-side
  token-indexed arrays, IR/codegen buffers). `c_type_parse_machine_run` is now
  the top Release-test symbol (`14.8%`) — the type-parse machine re-scans
  declaration ranges per attempt and nobody has profiled *inside* it yet.
  Windows CI showed `compiler_driver_tests` `+20.7%` and stage 1 `+17%` on
  the `2026-08-08d` commit in the same job where `c_frontend_tests` fell
  `27%` — single sample on the noisiest runner, no mechanism identified;
  watch the next main runs before believing or dismissing it.
- Reference points for the next audit, all clang-built: stage 1 `8.500 G`
  instructions / `~1.24 s` wall / `~325k` minor faults, Release `ide test`
  `10.259 G` (`c_frontend_tests` `428 ms`, `x86_64_metadata_tests` `68 ms`),
  sanitized `ide test` `170.937 G` (`c_frontend_tests` `6.49 s`,
  `x86_64_metadata_tests` `0.78 s`), `ide bench` `713.1 M` instructions per
  run, `BENCH_PARSE` min `~88.6 us` at `files=61`. Stage 2 `121.6 G`,
  validation only.

`2026-08-08d` (Linux x86_64, method of `2026-08-08c` plus one addition: a
Superluminal capture of the stage-1 self-host compile, queried through
`SuperluminalCmd llm` — its inline-aware call graph and per-line timings
attributed the libc time that `--call-graph fp` smeared into whichever big
function last pushed a frame, and that attribution found four of the fixes
below. Every quoted number is from a clang-built binary; the stage-2 delta is
self-hosting validation only). Nine fixes in four commits' worth of changes:
stage 1 `12.753 G` to `8.486 G` instructions (`-33.5%`, wall `1.76 s` to
`1.50 s`), Release `ide test` `15.213 G` to `12.087 G` (`-20.6%`,
`c_frontend_tests` `746 ms` to `466 ms`, `parser_tokenizer_tests` `72.7 ms` to
`26.0 ms`), sanitized `ide test` `246.055 G` to `185.739 G` (`-24.5%`,
`c_frontend_tests` `9.15 s` to `6.84 s`), at the byte-identical fixed point
with all 29 modules passing in both configurations. `ide bench` unchanged by
instruction count (`720.3 M` to `718.6 M` for the whole run, A/B measured —
wall medians drifted `92-102 us` across runs on the same binary, which is why
the bench verdict is quoted in instructions). Stage 2 fell `188.4 G` to
`121.9 G` (`-35%`); the no-regalloc canonical path amplifies every removed
rescan.

- **One block-chain walk was a quarter of the Release test suite.** The
  assignment rewrite in `c_ir_lower_assignment_statement_step` pops the
  just-emitted load to recover its place, and found the predecessor of the
  block's last instruction by walking the chain from `first_instruction` —
  once per assignment, so quadratic in block length, and `26.4%` of Release
  `ide test` samples (`87.8%` of them on the walk's one `cmp`; the 4096
  compound assignments in `c_test_frontend_scratch_and_hardening` are exactly
  the shape that triggers it). The builder now tracks the predecessor of the
  last appended instruction; block switches and tail edits invalidate it, and
  the walk survives only as the fallback for the invalidated case. Release
  `ide test` `15.213 G` to `14.271 G`, `c_frontend_tests` wall `-29%`, and the
  time share was far larger than the instruction share — the walk was a
  dependent-load pointer chase, so profile *and* count, in both directions.
- **The libc time perf could not attribute was five separate finds, and the
  Superluminal capture named every one.** `perf` showed `~17%` of stage-1
  samples in unresolved `libc.so.6` hex offsets credited to whole phases;
  the capture's call graph split them: (1) `c_punctuator_length` compared all
  57 punctuator spellings by `memcmp` in enum order at every punctuator — a
  `;` sat behind ~52 failed probes, `51.6%` of `c_lex`'s inclusive time. A
  first-byte dispatch derived from the same table (so id and spelling still
  cannot drift) compares at most a handful of candidates byte-inline. (2)
  `c_parse_promoted_member_type` memset a visited slot per type in the unit on
  every `.field` designator lookup (`~56 ms`); the marks are generation
  stamps now — a query is a counter bump. (3) `c_declaration_keyword` walked
  66 `char const*` keywords calling `string_from_pointer` (strlen) on every
  candidate per identifier query from five parser scan loops; it is a
  once-built hashed set over `String8` spellings. (4) `object_from_canonical_
  codegen_module` resolved every relocation by three linear scans (entries by
  symbol id, then symbols by name twice), and (5) `object_append_dwarf` did
  the name scan again per DWARF relocation — together `~9%` of stage-1 wall.
  Both now use an entry-by-symbol-id array plus a name probe table recording
  the lowest defined and lowest undefined index per name, preserving the
  scans' first-match-in-index-order semantics exactly.
- **The buster tokenizer paid an out-of-line call per string-literal byte.**
  `tokenizer_utf8_sequence_length` was called for every byte inside
  string/char literals; the two 16.7 MB `TOKEN_MAX_LENGTH` overflow
  regressions made it `4.8%` of Release `ide test`. ASCII bytes now advance
  without the call (`parser_tokenizer_tests` `72.7 ms` to `26.0 ms`), and the
  bench A/B confirmed the added branch costs nothing on the corpus.
- **`c_parse_aggregate_lookup` took the `2026-08-08c` leftover index, but the
  rollback machinery dictated its shape.** `c_type_parse_rollback` restores
  `CParseResult` wholesale from a checkpoint copy while shared array storage
  keeps its contents, so an incrementally-maintained index can hold stale
  entries. The `(kind, tag)` probe table therefore validates its recorded id
  against the live type table on every hit and falls back to the old linear
  scan on staleness or saturation; its fill header lives *outside*
  `CParseResult` so a rollback cannot rewind the fill count while slots stay
  populated (that mismatch could overfill the table and hang the probe).
  Misses prove absence because every tagged type passes through
  `c_parse_add_type` and tags/kinds never mutate after creation — both
  verified, not assumed. Same-shape fixes: `c_ir_add_pointer_type` cache
  misses now sweep only types added since the last sweep instead of the whole
  table, and the hot `CToken` by-value copies in the two
  `c_type_parse_parenthesized_step` scan loops became pointer walks (each
  40-byte copy was an `__asan_memcpy` at `-O0`; `5.6%` of the sanitized run).
- **The self-host stage is also the dialect gate.** The first version of the
  object-symbol probe helper compiled under clang but failed `ide cc`: the C
  frontend rejects a statement that follows an if-`return` inside `for (;;)`
  (a `break`-exited `for (;;)` is fine). Rewriting the probe as a `while`
  compiled. Minimal repro is recorded in the follow-up task; until that gap
  is fixed, condition-less loops in this codebase need `break`-shaped exits
  or `while` spellings, and a trusted-compiler build proves nothing about
  self-hostability of new code.
- **Leftovers for the next audit, deliberately untaken:** `c_ir_add_array_type`
  still scans every type per call (was not hot in any profile here);
  `x86_64_metadata` retains `~9%` of Release `ide test`
  (`pool_string_equal_literal`, pattern probes); `c_ir_query_execute`
  type-prediction is `4.8%` of Release test; sanitized `string_equal` residue
  is `c_parse_apply_vector_attribute` `2.6%` and `c_ir_type_name_prefix`
  `2.2%`. The big structural one: the capture showed `~6.5%` of stage-1 wall
  in `kernel_init_pages` — first-touch zeroing of pages the compiler asks for
  and mostly never revisits. `c_lex` allocates one 40-byte `CToken` plus one
  16-byte `CSourceLocation` per *source byte* per file, and the parse-machine
  buffers are token-count-sized; the fix direction is arena reuse across
  files/phases and right-sizing those worst-case preallocations, which is a
  memory-shape change, not a scan removal.
- Reference points for the next audit, all clang-built: stage 1 `8.486 G`
  (wall `~1.50 s`), Release `ide test` `12.087 G` (`c_frontend_tests`
  `466 ms`, `x86_64_metadata_tests` `166 ms`, `compiler_driver_tests`
  `161 ms`), sanitized `ide test` `185.739 G` (`c_frontend_tests` `6.84 s`,
  `x86_64_metadata_tests` `1.60 s`), `ide bench` `BENCH_IO` median
  `~272 us` / `BENCH_PARSE` min `~92 us` at `files=61` (quote bench deltas in
  instructions: `718.6 M` per full run). Stage 2 `121.9 G`, validation only.

`2026-08-08c` (Linux x86_64, same method as `2026-08-08b`: `perf record -F 999
--call-graph fp`, instruction counts from `STEP_INSTRUCTIONS` and `perf stat -e
instructions`, every quoted number from a clang-built binary; buster-compiled
stage executables were used for fixed-point validation only). Seven commits:
stage 1 `16.999 G` to `12.713 G` instructions (`-25.2%`, wall `2.38 s` to
`1.83 s`), sanitized `ide test` `331.97 G` to `246.05 G` (`-25.9%`), Release
`ide test` `21.70 G` to `15.21 G` (`-29.9%`), at the byte-identical fixed point
with all 29 modules passing in Release and sanitized Debug and `ide bench`
unchanged (`BENCH_IO` median `~272 us`, `BENCH_PARSE` median `~98 us`,
`files=61`).

- **Four more translation-unit quadratics in `c_lower_to_ir`, all the shape the
  previous entries predicted** — a per-item query answered by rescanning a
  whole table. In fix order, with stage 1 after each: the per-function local
  count scanned every entity per lowered definition (`13.2%` of samples; one
  bucketing pass, `16.999 G` to `16.813 G` but wall `-11%` — the scan was
  memory-bound, so the win is time, not instructions); the string-literal
  array-bound inference scanned every declaration per unresolved array type
  from inside the round/pass/type fixpoint nest (CSR bucket of object
  declarations by type, `16.813 G` to `15.213 G`); five symbol/global passes
  joined declarations to entities by rescanning all declarations per entity
  (one CSR by entity, `15.213 G` to `14.371 G`); and
  `c_parse_scope_for_token` answered "deepest scope under root holding this
  token" by scanning every scope in the unit and walking each candidate's
  ancestors (`4.3%` of samples but `11.6%` of instructions — a
  children-by-parent index descended from the root instead, `14.371 G` to
  `12.706 G`; the during-parse static-assert caller keeps the linear scan
  because scopes are still growing there). Sample share and instruction share
  disagree in both directions across these — profile *and* count.
- **The x86 metadata module's remaining cost was rescanning and re-deriving
  static data per query.** Three commits, sanitized `ide test` after each:
  `string_offset_terminated` re-found each string's NUL by linear scan per
  call, mostly `physical_register_view` asking the same atom's length once per
  literal in its ~50-entry chain — a distance-to-NUL table filled in one
  backward pass at decode makes it one read (`331.97 G` to `306.80 G`);
  `copy_form` re-tokenized the form's whole pattern to normalize
  prefix/family metadata and filtering ran it per candidate per iteration —
  normalized forms are now cached per id (`306.80 G` to `277.32 G`); emit and
  coverage paths still re-parsed patterns per call and the test helper
  `x86_64_metadata_test_string_contains` compared all needle bytes at every
  offset — pattern semantics are memoized per form id behind a seed-field
  guard (a fabricated or edited form parses fresh) and the probe rejects on
  the first byte (`277.32 G` to `246.05 G`). `x86_64_metadata_tests`:
  sanitized `5.77 s` to `1.59 s`, Release `529 ms` to `166 ms`.
- Stage-1 profile after all fixes is genuinely flat: `c_lower_to_ir` fell from
  `26%` of samples to under `1.2%`, and no function exceeds `5%` (`c_lex`
  `4.9%`, `codegen_generate_canonical_module` `4.8%`,
  `object_from_canonical_codegen_module` `4.6%`). Sanitized shape:
  `__asan_memcpy` `~21%` (diffuse), `string_equal` `8.3%`
  (`c_parse_apply_vector_attribute` `2.4%`, `c_ir_type_name_prefix` `1.9%`,
  `c_parse_aggregate_lookup` `1.6%`), `c_ir_lower_assignment_statement_step`
  `5.0%`, `c_token_is_punctuator` `4.4%`.
- **Known remaining find, deliberately not taken:** `c_parse_aggregate_lookup`
  scans every type per tag lookup during parsing. It needs an incrementally
  maintained `(kind, tag)` hash index in `CParseResult` (the
  `entity_lookup_buckets`/`typedef_lookup_buckets` pattern), returning the
  oldest match; worth ~1.6% sanitized plus some parse-time stage 1. The
  remaining `string_equal` sites are spelling predicates — re-read the
  `2026-08-08` warning before touching them; only removing comparisons wins.
- Reference points for the next audit, all clang-built: stage 1 `12.713 G`,
  sanitized `ide test` `246.05 G` (`c_frontend_tests` `9.6 s`,
  `x86_64_metadata_tests` `1.6 s`), Release `ide test` `15.21 G`
  (`c_frontend_tests` `780-870 ms` run-to-run — quote instructions, not this
  wall spread), Release `BENCH_IO` median `272-280 us`, `BENCH_PARSE` median
  `98-100 us` at `files=61`. Stage 2 fell `262.2 G` to `187.7 G` — quoted as
  self-hosting validation only, since it runs on a buster-compiled stage 1.

`2026-08-08b` (Linux x86_64, `perf record -F 999 --call-graph fp`, instruction
counts from `STEP_INSTRUCTIONS` and `perf stat -e instructions`). Two
configurations were audited together for the first time: **compile throughput**
(clang-built Release `ide` compiling the unity TU, the `Self-host stage 1` step)
and the sanitized Debug `ide test` CI critical path. Every number here comes
from a clang-built binary; buster-compiled stage executables are self-hosting
validation only and none of their timings are quoted. Net result across six
commits: stage 1 `27.436 G` to `17.000 G` instructions (`-38.0%`, `3.19 s` to
`2.16 s`) and sanitized `ide test` `430.25 G` to `332.16 G` (`-22.8%`), at the
byte-identical self-host fixed point with all 29 modules passing in both
configurations and `ide bench` unchanged throughout.

- **Five whole-table scans, four of them quadratic, in one audit.** Each was a
  query answered by re-deriving a global property per item, and each is the
  shape the `2026-08-08` entry below told the next audit to look for. In the
  order they were fixed, with the stage 1 instruction count after each:
  - `c_ir_type_is_flexible_array` scanned every type and every member of every
    aggregate, from depth 5 inside the `type_mapping_round` / `pass` /
    `type_index` fixpoint nest — `21.2%` of the compile on its own.
    `c_ir_collect_flexible_array_types` marks the set once per round.
    `27.436 G` to `21.216 G` (`-22.7%`).
  - `c_ir_incomplete_array_has_initializer` scanned every declaration and every
    entity, once per array type. Now indexed by type in one pass.
    `21.216 G` to `20.549 G` (`-3.1%`).
  - `c_ir_function_signature` called `ir_prepare_program_abi`, sweeping every
    type in the program, once per function declaration. Removed outright:
    every ABI read outside the resolver goes through `ir_type_abi_value`,
    which resolves on demand, so the sweep was pre-warming only. `20.549 G` to
    `19.666 G` (`-4.3%`).
  - `c_ir_label_metadata_store_for_place` scanned every value in the function
    on every store, relocating each place to ask whether it shared the root
    just written. Places are now filed once into an intrusive list headed by
    their root. `19.672 G` to `17.000 G` (`-13.6%`), and sanitized `ide test`
    `366.44 G` to `332.16 G` (`-9.4%`) — it was the one hotspot both
    configurations shared.
  - `x86_64_metadata` string consumers read the pool one
    `buster_x86_metadata_string_byte` call per byte. See below.
- **The optimizer's inline hint does not exist in the tree CI measures.**
  `BUSTER_INLINE` expanded to nothing when `BUSTER_OPTIMIZE` was off, so the
  already-decoded guard `c343ab8` added to `buster_x86_metadata_decode_tables`
  was still an out-of-line call in sanitized Debug, at `3.2%` of `ide test`.
  That commit's sanitized win came from shrinking the guard's `-O0` frame, not
  from inlining. `BUSTER_ALWAYS_INLINE` now holds in every configuration and
  `BUSTER_INLINE` is defined in terms of it; `nm` on the sanitized Debug `ide`
  no longer lists the guard. Worth `-0.9%`. Reserve the new spelling for
  one-test guards on per-byte or per-record paths — forcing a large body inline
  everywhere costs Debug compile time and steppability.
- **The decoded string pool is contiguous, so stop reading it a byte at a
  time.** The one-time decode already flattens the chunked generated pool into
  a host array, but every consumer still went `string_byte` -> `pool_byte` ->
  the decode guard: three calls per byte over a `1.7 MB` pool, roughly `27%` of
  the sanitized run across substring searches, NUL scans, and the pattern
  tokenizer that walks all `11013` forms. `buster_x86_metadata_pool_span` /
  `_string_span` hand out a view once. Sanitized `ide test` `424.46 G` to
  `366.44 G` (`-13.7%`), `x86_64_metadata_tests` `8.21 s` to `5.17 s` (`-37%`).
  `string_span` is public because the metadata tests are a separate translation
  unit in non-unity builds.
- **A correct incremental fix can still be worthless; measure it.** The first
  attempt at the ABI sweep resumed from the longest already-resolved prefix,
  which is sound because a resolved ABI is never invalidated. It measured
  `-0.2%` and left `c_ir_function_signature` at `6.1%` of the profile, because
  `ir_resolve_type_abi` returns without resolving when a type's layout is
  unresolved, so one early incomplete type pins the prefix near zero. Profiling
  after the change, not just before it, is what caught this.
- Shares after all six commits, stage 1 out of `17.000 G`: `c_lower_to_ir`
  `24.3%`, `c_lex` `4.6%`, `c_ir_body_task_set_loop_cleanup_targets` `4.1%`,
  `object_from_canonical_codegen_module` `3.9%`,
  `codegen_generate_canonical_module` `3.6%`, `c_ir_lower_frame_fallback`
  `2.8%`. No single quadratic dominates any more and the profile is flat.
- Shares after all six commits, sanitized `ide test` out of `332.16 G`:
  `__asan_memcpy` `19.5%`, `string_equal` `7.1%`,
  `buster_x86_metadata_string_offset_terminated` `6.4%`,
  `buster_x86_metadata_emit_token_equal` `5.7%`,
  `x86_64_metadata_test_string_contains` `4.2%`,
  `c_ir_lower_assignment_statement_step` `4.1%`, `c_token_is_punctuator`
  `2.7%`. Module totals `c_frontend_tests` `8.70 s` and
  `x86_64_metadata_tests` `5.23 s`. `string_offset_terminated` *rose* in share
  because the run shrank around it: it re-finds a NUL from an offset every time
  a record's string length is wanted, and caching the length in the decoded
  record is the obvious next step.
- Reference points for the next audit, all clang-built: Release `ide test`
  `c_frontend_tests` `693 ms`, `x86_64_metadata_tests` `517 ms`. Release
  `ide bench` `BENCH_IO` median `263 us`, `BENCH_PARSE` median `92 us` — both
  unchanged by every commit here, which is the expected result and was checked
  each time rather than assumed.
- Process note: `--sanitize` is a sticky `generate`-time flag. A measurement
  script that runs the sanitized pass last and then measures Release without
  `--no-sanitize` silently profiles a sanitized tree; self-host aborts in
  `ld.so` and Release test times triple. Also do not build while a measurement
  runs — two overlapping runs produce plausible wall times that are pure
  contention. Instruction counts survive both mistakes; wall times do not.

`2026-08-08` (Linux x86_64, sanitized Debug clang, `perf record -F 999
--call-graph fp`, `25650` samples, instruction counts from `perf stat -e
instructions`). Supersedes the shares in the `2026-08-07` entry below, whose
mechanism was fixed by `23a574e`: `CToken` no longer crosses the spelling
predicates by value, and ASan memcpy shadow scanning is `8.8%` of the run
rather than half of `c_frontend_tests`.

- **A lazily-decoded table's guard cost 20% of the whole test suite.**
  `buster_x86_metadata_decode_tables` decodes once behind
  `buster_x86_metadata_tables_decoded`, but every accessor called it
  out-of-line, including the per-byte string accessors, so the decode was free
  and the already-decoded test was not. Splitting it into a `BUSTER_INLINE`
  test plus a cold `buster_x86_metadata_decode_tables_once` body moved the run
  from `678.81 G` to `537.66 G` instructions (`-20.8%`) and `32.93 s` to
  `25.80 s`; `x86_64_metadata_tests` `14.90 s` to `8.25 s` (`-45%`) and
  `assembly_tests` `1.10 s` to `0.66 s` (`-40%`). Self-hosting stays at the
  byte-identical fixed point. Look for this shape wherever a self-initializing
  accessor is called per byte or per record.
- Module shares after that change: `c_frontend_tests` `61.6%` (`15.79 s`),
  `x86_64_metadata_tests` `31.8%` (`8.25 s`), `assembly_tests` `2.6%`. Before
  it the two were effectively tied (`15.79 s` against `14.90 s`), so
  "`c_frontend_tests` is the largest cost" became true only once the metadata
  module was fixed.
- **The sanitized tree runs UBSan as well as ASan, so each instrumented
  struct-field access costs roughly 20 instructions** — an inline
  `__ubsan_handle_type_mismatch_v1` null/alignment test plus an ASan shadow
  test. `c_token_is_punctuator` is 94 instructions and `string_equal` is 274.
  The consequence is counter-intuitive and was measured three times: **splitting
  an aggregate into separate field accesses is slower than passing it by
  value**, because the by-value copy is one instrumented `memcpy` while each
  field read pays the full pair of checks. Against a `678.81 G` baseline, a
  length/first-byte fast path in `c_token_spelling_equal` measured `695.19 G`,
  the same path with `BUSTER_INLINE` on both predicates `701.03 G`, and routing
  the comparison through a scalar-argument helper `577.96 G` against the
  `537.66 G` state it was applied to. All three were reverted. Do not retry a
  local rewrite of these predicates; only removing the comparison entirely wins.
- **Giving the token a `CPunctuator` id removed another 20% of the suite.**
  `c_token_is_punctuator` reaching `string_equal` had been `14.1%` of the whole
  run, `~19%` counting its own frame and `string_equal`'s ASan fake stack, from
  `1155` call sites over a closed set of 56 spellings. `c_punctuator_length`
  now returns the id it already matched, `CToken` carries it in a `u16` that
  shares the word `pack_alignment` used to own — so `CToken` stays 48 bytes,
  locked by a `BUSTER_CT_CHECK` — and the predicate is `token->punctuator ==
  punctuator`. The run went from `537.91 G` to `430.69 G` instructions
  (`-19.9%`), `24.98 s` to `20.9-21.8 s`; `c_frontend_tests` `15.27 s` to
  `10.7-11.4 s` (`-26%`, and quote the instruction count instead — the wall-time
  spread across two runs of the identical binary is that wide). Compile
  throughput moved with it: self-host stage 1 `29.50 G` to `27.25 G` (`-7.6%`)
  and stage 2 `442.85 G` to `394.65 G` (`-10.9%`), at the byte-identical fixed
  point.
- Two properties make the single compare sound, and a change that breaks either
  one is silently wrong rather than loud. **Only a `C_TOKEN_PUNCTUATOR` token
  may carry a nonzero id**, so the predicate needs no kind test; every site that
  retypes an existing token assigns `punctuator` alongside `kind`.
  **Digraphs keep ids distinct from the punctuators they spell** (`<:` is not
  `[`), because every caller was asking about a spelling. The one place that
  edits a spelling in flight, `c_ir_compound_assignment_operator`, drops the
  `'='` and hands the result to the spelling-driven `c_conditional_operator`, so
  it clears the id that no longer describes it.
- Converting the punctuator tests that went through `c_token_spelling_equal`
  was worth `0.04%` (`430.84 G` to `430.68 G`): they sit on the directive path,
  not the hot loop. It was kept for the invariant, not the number. The
  `C_CONDITIONAL_MATCH` chain in `c_conditional_operator` is still a linear walk
  of string compares and was deliberately left alone — it does not appear in the
  profile, and converting it means reworking the spelling truncation above.
- After the change `string_equal` is `5.6%` and no longer punctuator work: it is
  `c_parse_apply_vector_attribute` and `c_ir_type_name_prefix` matching keywords
  and type names. The largest single buster frame is now
  `c_ir_label_metadata_store_for_place` at `7.4%`, and module shares are
  `c_frontend_tests` `51.5%` (`10.72 s`) against `x86_64_metadata_tests` `39.9%`
  (`8.31 s`) — the two modules are close to tied again, so the next audit should
  not assume the C frontend is where the time is.
- Self-host stage ratio, for the canonical codegen path that runs no register
  allocation: stage 1 `3.45 s` / `29.50 G` instructions, stage 2 `37.96 s` /
  `442.85 G`. That is `11.0x` by wall time but `15.0x` by instructions retired;
  quote the instruction ratio, since wall time on this host varies ~10%.

`2026-08-07` (Linux x86_64, sanitized Debug clang, `perf record -F 999
--call-graph fp` on a quiet host, `65466` samples). This is the CI critical
path; `ide test` totals `~65 s` here:

- `c_frontend_tests` is `72.7%` of the sanitized run (`47.7 s` by
  `TEST_MODULE_TIMING`); `x86_64_metadata_tests` is the only other large module
  at `23.5%` (`15.4 s`). Everything else together is under `4%`.
- **Half of `c_frontend_tests` is ASan's memcpy shadow check.**
  `QuickCheckForUnpoisonedRegion`, inlined into `___interceptor_memcpy` in
  `libclang_rt.asan-x86_64.so`, is `50.3%` of the module's samples.
- Charging that sanitizer cost to the buster frame that provokes it, the module
  is dominated by two one-line predicates in
  `src/buster/lib/compiler/frontend/c/c.c`:
  `c_token_is_punctuator` (`46.3%`) and `c_token_spelling_equal` (`21.4%`),
  then `string_equal` (`8.9%`), `c_ir_label_metadata_store_for_place` (`3.2%`),
  `c_ir_lower_assignment_statement_step` (`2.8%`) and
  `c_type_parse_parenthesized_step` (`2.7%`).
- Mechanism: `CToken` is a 40-byte aggregate passed **by value** into
  `c_token_is_punctuator`, which forwards it **by value again** to
  `c_token_spelling_equal`. At `-O0` clang materializes each by-value aggregate
  argument with a real `call memcpy` (visible in the disassembly of
  `c_token_is_punctuator`, alongside a per-call `__asan_stack_malloc_1`), and
  under ASan every one of those goes through the interceptor and its shadow
  scan. The predicate itself is one enum compare plus one string compare.
- The same profile of an unsanitized Debug build has the same shape without the
  amplification: `c_token_is_punctuator` `38.9%`, `string_equal` `16.8%`,
  `c_token_spelling_equal` only `4.7%`. Sanitizing multiplies the module by
  `3.8x` overall and concentrates the extra cost on the by-value token copies.
- Cost by call site inside `c_frontend_tests` (sanitized): `c_test.c:7229`
  `30.1%`, `:6974` `18.1%`, `:7166` `14.6%`, `:6726` `13.3%`, `:6312` `9.1%`,
  `:7227` `4.3%`. Five of these six are the `c_lower_to_ir`/`c_parse` calls of
  the inline stress blocks; `c_lower_to_ir` accounts for roughly `68%` of the
  module and `c_parse` for `21%`.
- Method validation (independent, non-sampling): direct
  `timestamp_take()` brackets around those call sites in the *same* process
  agree with the sampled shares to within `0.04` percentage points sanitized
  (`7229` `30.99%` measured vs `31.03%` sampled; `6974` `19.76`/`19.77`; `7166`
  `14.59`/`14.61`; `6726` `12.24`/`12.25`; `6312` `8.45`/`8.42`; `7227`
  `4.01`/`4.03`) and to within `0.3` points unsanitized. Every extracted return
  address disassembles to the instruction immediately after a `call` to the
  callee the callchain names.
- **Correction to an earlier note:** `c_test.c:7227` is *not* ~70% of
  `c_frontend_tests`. It is `4.3%` sanitized and `3.6%` unsanitized
  (`0.49 s` of `13.8 s` by direct timing). The `~70%` figure came from
  `perf script -F ip,sym,srcline`, whose raw-return-address resolution shifts
  every call site by 1–2 lines. The dominant single site is `c_test.c:7229`,
  the `c_lower_to_ir` of the same vla-assembly stress block — so the block was
  identified correctly and the line and magnitude were not.

`2026-08-02` (Linux x86_64, Release + Debug, with `--instrument --time-trace`):

- `build/Release` compile time is currently dominated by the unity TU:
  `CMakeFiles/ide.dir/Release/src/buster/apps/ide/ide.c.o` at `20613 ms`
  in `ninja_log_summary build --limit 20`.
- `build/Debug` split compilation hotspots (same summary):
  `parser.c.o` (`707 ms`), `codegen.c.o` (`582 ms`), `analysis.c.o` (`475 ms`),
  `ir.c.o` (`361 ms`).
- Pre-refactor `bench_all` result snapshots (the default was the I/O mode):
  - Release: `BENCH_IO parse_all_tests iterations=200 files=59 min_ns=398673 median_ns=418340`.
  - Debug: `BENCH_IO parse_all_tests iterations=200 files=59 min_ns=941358 median_ns=975512`.
- In `BUSTER_INSTRUMENT` mode:
  - Tokenize: `55_363 ns` median (Release), `297_607 ns` median (Debug).
  - Parse: `48_272 ns` median (Release), `340_853 ns` median (Debug).
- Pre-refactor `BENCH_FILE` output was consistently slowest-first; `tests/basic_variadic.bbb`
  is the dominant long-tail input across both configs.
- Superluminal capture of the old `ide bench` shows `parser_parse_bench` paths flowing
  heavily through `file_read -> os_file_open -> __libc_open64`, indicating
  benchmark procedure repeatedly performs disk reads instead of amortizing source
  input.
- The two-mode benchmark now separates parser throughput from filesystem input;
  compare `BENCH_PARSE` and `BENCH_IO` using their mode-specific phase and
  per-file rows when investigating parser throughput.

`2026-08-02` (Linux x86_64, Release `bench_all`, `BUSTER_INSTRUMENT=1`):

- Before the two-mode refactor, an opt-in preloaded-source benchmark path produced:
  - `BENCH_IO parse_all_tests ... median_ns=421736` (baseline: `file_read` each iteration).
  - `BENCH_PARSE parse_all_tests ... median_ns=90831` (files memory-mapped once, then reused).
- The preloaded benchmark reduced total wall time by roughly `78.5%` (`421736 -> 90831`).
- Tokenize/parse split moved only modestly:
  - Baseline `BENCH_IO_PHASE tokenize` median: `57241`, parse median: `48711`.
  - Preloaded `BENCH_PARSE_PHASE tokenize` median: `50023`, parse median: `39756`.
- The remaining gap between modes is mostly filesystem read overhead in the old
  path; parser/tokenization itself improves only ~12–19%.
- The dominant input is still `tests/basic_variadic.bbb`, but its parse path also
  drops (median `11171 -> 10430`) under preloaded-source reuse.
