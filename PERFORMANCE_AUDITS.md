# Performance audit notes

Performance audit history for this repository, newest first. Every entry is a
record of what was measured, what was fixed, and what the numbers were at the
time; the methodology for taking new measurements — which benchmark to trust,
how to profile the sanitized and Release trees, how to symbolize — stays in
the "Benchmarking and diagnostics" section of `AGENTS.md`.

Each audit is one file under `docs/performance-audits/`, named for its id, and
this file is the index. **A new audit adds a file, never a paragraph to an
existing one:** write `docs/performance-audits/<id>.md`, then add one line at
the top of the index below. That is what keeps concurrent audit branches from
colliding — every audit used to be prepended to this file at the same line, so
any two open audit branches conflicted by construction. The index line is the
only shared text left, and `.gitattributes` marks this file `merge=union` so
even that merges without a conflict; check the order after a union merge, since
union keeps both lines but does not know which is newer.

An audit's id is the UTC timestamp at which it is recorded,
`2026-08-22T140351Z`, which is ISO 8601 with the colons dropped because Windows
forbids them in filenames. `tools/new_audit.py` mints it, writes the file and
inserts the index line — mint the id rather than typing one, since it is the
only field two concurrent sessions can pick identically. Seconds resolution is
enough because the id is stamped when a human writes the entry, not when a
benchmark iterates.

Audits up to 2026-08-22 carry the older name: a date plus a sequence letter,
`2026-08-08k`. That scheme is what the timestamp replaces — the letter is
chosen by counting the day's entries, so concurrent sessions reliably chose the
same one. Those names are historical, entries cross-reference each other by
them, and they stay as written; the two audits both dated `2026-08-02` became
`2026-08-02` and `2026-08-02b` when the split gave each its own file, and no
other entry text changed.

The index carries the order, not the directory listing. Timestamp ids do sort
chronologically, but the older letter ids do not past `z` (`aa` sorts before
`b`), a few entries were deliberately recorded out of id order (`2026-08-08k`
says so itself), and `T` sorts before a lowercase letter, so a timestamp id
lands above the same day's letter ids.

Entries written before the C-only consolidation on 2026-08-14 may mention the
removed experimental frontend, its fixtures, or its editor model. The `.bbb`
fixtures and historical documentation remain in-tree for future work, while
the C implementation and C tests are gone. Older entries remain historical
measurement context rather than current implementation guidance.

**Read the newest entry before starting performance work.** It carries the
reference points the next audit is measured against, the finds that were
deliberately left untaken, and the mistakes an earlier audit already paid for.
Leave the older entries as written — they are a record, not documentation to
keep current.

## Audits, newest first

- [`2026-08-22T091548Z`](docs/performance-audits/2026-08-22T091548Z.md) — take the three cache-miss leads: the promoted-member memset, the selector's per-value prepass, and the linker's image copy
- [`2026-08-22T084855Z`](docs/performance-audits/2026-08-22T084855Z.md) — survey the stage-1 cache misses: one per-value prepass, one per-field-access memset, and the copy sites that own a fifth of them
- [`2026-08-22T082003Z`](docs/performance-audits/2026-08-22T082003Z.md) — the 2026-08-22 branch-miss survey, re-measured per source line with the LBR mispredict flag
- [`2026-08-22f`](docs/performance-audits/2026-08-22f.md) — no lever in the intern probe loop or the entity scope walk: both hash tables measured at their floor, and one nearby branch measured on four trees and rejected
- [`2026-08-22e`](docs/performance-audits/2026-08-22e.md) — give the x86 selector's prepasses a row layout and a candidate list instead of five more walks of the instruction list
- [`2026-08-22d`](docs/performance-audits/2026-08-22d.md) — hotspot 6 of the branch-miss survey: punctuator ladders, and the two per-token predicates hiding behind them
- [`2026-08-22c`](docs/performance-audits/2026-08-22c.md) — classify the IR delimiter index from the punctuator id, and keep the two delimiter walks apart
- [`2026-08-22b`](docs/performance-audits/2026-08-22b.md) — give the cleanup-attribute scans the well-known ids, and size what a spelling-to-id swap actually buys
- [`2026-08-22a`](docs/performance-audits/2026-08-22a.md) — delete the allocator's free-register loop, not just its skip branch
- [`2026-08-21d`](docs/performance-audits/2026-08-21d.md) — compare identifier tokens by interned id, not by spelling
- [`2026-08-20a`](docs/performance-audits/2026-08-20a.md) — give the SIMD vocabulary its first dword operation and drop the allocator's byte-mask collapse
- [`2026-08-19c`](docs/performance-audits/2026-08-19c.md) — align canonical value slots with a mask instead of a hardware divide
- [`2026-08-19b`](docs/performance-audits/2026-08-19b.md) — fold validated selection facts into the population that already owns them, and keep the common debug-line…
- [`2026-08-19a`](docs/performance-audits/2026-08-19a.md) — make the successful C producer/consumer boundary explicit and remove a complete heterogeneous validation pass
- [`2026-08-18r`](docs/performance-audits/2026-08-18r.md) — separate the dominant allocator-row population and give it a homogeneous kernel
- [`2026-08-18q`](docs/performance-audits/2026-08-18q.md) — project operand lanes once, then scan only active homogeneous sets
- [`2026-08-18p`](docs/performance-audits/2026-08-18p.md) — make the selector/allocator boundary explicit and consume block-parameter rows once
- [`2026-08-18o`](docs/performance-audits/2026-08-18o.md) — classify allocator lanes once and make the machine-row append path typed
- [`2026-08-18n`](docs/performance-audits/2026-08-18n.md) — make allocator copies and rematerializations consume the prepared register lanes
- [`2026-08-18m`](docs/performance-audits/2026-08-18m.md) — make allocator frame traffic consume the prepared memory lane
- [`2026-08-18l`](docs/performance-audits/2026-08-18l.md) — classify variable base-memory rows into zero/disp8/disp32 lanes
- [`2026-08-18k`](docs/performance-audits/2026-08-18k.md) — move the dominant exact memory population into dense base/displacement lanes
- [`2026-08-18j`](docs/performance-audits/2026-08-18j.md) — turn the remaining immediate population into prewarmed patch lanes
- [`2026-08-18i`](docs/performance-audits/2026-08-18i.md) — publish the dominant exact lane instead of selecting it again per row
- [`2026-08-18h`](docs/performance-audits/2026-08-18h.md) — keep compact GPR rows in their homogeneous table until the generic lane is actually needed
- [`2026-08-18g`](docs/performance-audits/2026-08-18g.md) — make the common dense GPR encoding a four-byte unit, and measure the population before batching
- [`2026-08-18f`](docs/performance-audits/2026-08-18f.md) — prewarm the closed GPR encoding population, and reject gather-then-compress SIMD
- [`2026-08-18e`](docs/performance-audits/2026-08-18e.md) — separate the allocator-edit command stream from x86 row emission
- [`2026-08-18d`](docs/performance-audits/2026-08-18d.md) — remove dead generic-encoder state
- [`2026-08-18c`](docs/performance-audits/2026-08-18c.md) — classify once across the population
- [`2026-08-18b`](docs/performance-audits/2026-08-18b.md) — compact width lanes, active-arity selection, wide sentinel fills, and scope-local debug storage
- [`2026-08-18a`](docs/performance-audits/2026-08-18a.md) — exact-recipe scratch, selector storage reuse, and dead debug traversal
- [`2026-08-17h`](docs/performance-audits/2026-08-17h.md) — indexed selector facts, minimal-prepass ownership, exact-writer stores, and reusable DWARF scratch
- [`2026-08-17g`](docs/performance-audits/2026-08-17g.md) — shape-cache hashing, metadata-address borrowing, and x86 function-shape scans
- [`2026-08-17f`](docs/performance-audits/2026-08-17f.md) — exact-emitter validation, dispatch, and staging
- [`2026-08-17e`](docs/performance-audits/2026-08-17e.md) — machine-path dead work and SIMD-oriented dataflow
- [`2026-08-17d`](docs/performance-audits/2026-08-17d.md) — native codegen verifier and selector edge construction
- [`2026-08-17c`](docs/performance-audits/2026-08-17c.md) — cache/branch investigation
- [`2026-08-17b`](docs/performance-audits/2026-08-17b.md) — x86 metadata cache compaction, second pass
- [`2026-08-17a`](docs/performance-audits/2026-08-17a.md) — x86 metadata cache compaction
- [`2026-08-16e`](docs/performance-audits/2026-08-16e.md) — `CToken` 16 -> 12 lands
- [`2026-08-16d`](docs/performance-audits/2026-08-16d.md) — the `2026-08-16c` shrink batch lands
- [`2026-08-16c`](docs/performance-audits/2026-08-16c.md) — struct-size / cache-behavior survey
- [`2026-08-16b`](docs/performance-audits/2026-08-16b.md) — compiler throughput
- [`2026-08-16a`](docs/performance-audits/2026-08-16a.md) — canonical AArch64 form validity is derived once instead of per operation, based on `ae39b781`
- [`2026-08-15j`](docs/performance-audits/2026-08-15j.md) — CI test time, not compiler throughput
- [`2026-08-15i`](docs/performance-audits/2026-08-15i.md) — the form-selection candidate loop stops re-deriving loop-invariant work, based on `91eba52e`
- [`2026-08-15h`](docs/performance-audits/2026-08-15h.md) — the byte-template table is probed as a cache instead of a dictionary, based on `37d59296`
- [`2026-08-15g`](docs/performance-audits/2026-08-15g.md) — selection stops re-deriving each candidate's operand count, based on `2aa6930f`
- [`2026-08-15f`](docs/performance-audits/2026-08-15f.md) — the AArch64 large-stride gap `2026-08-15d` recorded is closed, based on `38907fbb`
- [`2026-08-15e`](docs/performance-audits/2026-08-15e.md) — the byte-template table is sized from the module, based on `6aad801c`
- [`2026-08-15d`](docs/performance-audits/2026-08-15d.md) — a second, value-keyed byte-template table, based on `ec884958`
- [`2026-08-15c`](docs/performance-audits/2026-08-15c.md) — value-free emissions are memoized as byte templates, based on `879d3e00`
- [`2026-08-15b`](docs/performance-audits/2026-08-15b.md) — the canonical x86-64 path takes the prepared binding route, based on `01758505`
- [`2026-08-15a`](docs/performance-audits/2026-08-15a.md) — canonical x86-64 emission stops re-deriving per-form metadata on every instruction, based on `134b96b0`
- [`2026-08-14b`](docs/performance-audits/2026-08-14b.md) — single metadata authority for the x86-64 machine encoder, based on `3dec8b04`
- [`2026-08-14a`](docs/performance-audits/2026-08-14a.md) — exact machine-encoder dispatch and prepared-token fast path, based on `00ed8ca5`
- [`2026-08-13d`](docs/performance-audits/2026-08-13d.md) — file-map `madvise(MADV_SEQUENTIAL)` A/B experiment, based on `053de57e169095f9e642b9d4275f8170307bf5c1`, not…
- [`2026-08-13c`](docs/performance-audits/2026-08-13c.md) — C frontend true 3-TU split, based on `39bc0ffa`, uncommitted during measurement
- [`2026-08-13b`](docs/performance-audits/2026-08-13b.md) — bounded Clang split-TU PCH A/B experiment, based on `aba451f3`, not landed
- [`2026-08-13a`](docs/performance-audits/2026-08-13a.md) — instruction-selection and scheduling foundation, based on `33e4b016`
- [`2026-08-11l`](docs/performance-audits/2026-08-11l.md) — register-allocator live-range splitting — the plan-stage-7 capability the span-pin design deferred and the…
- [`2026-08-11d`](docs/performance-audits/2026-08-11d.md) — the merged-tree re-baseline the stage-9 and stage-10 entries both demanded, plus two of stage 10's recorded…
- [`2026-08-11f`](docs/performance-audits/2026-08-11f.md) — frequency-aware pin economics — the lever `2026-08-10l` and `2026-08-10n`/`2026-08-10o` both named:…
- [`2026-08-11i`](docs/performance-audits/2026-08-11i.md) — register-allocator stage 10 follow-on — the System V vector ABI, closing the one remaining vector-typed…
- [`2026-08-11h`](docs/performance-audits/2026-08-11h.md) — where quality-mode compile cost goes, and the register-allocator plan's stage 12 settled. Developed against…
- [`2026-08-11k`](docs/performance-audits/2026-08-11k.md) — correction record, no code change: the `2026-08-11` entry's sentence "its `bench` medians 1.58 ms IO / 1.40…
- [`2026-08-11a`](docs/performance-audits/2026-08-11a.md) — the AArch64 machine backend's second installment — local promotion brought to x86-64 parity, then the AAPCS64…
- [`2026-08-11e`](docs/performance-audits/2026-08-11e.md) — register-allocator — QUALITY's span pins extended to the vector register file, the lead `2026-08-10o`…
- [`2026-08-11c`](docs/performance-audits/2026-08-11c.md) — register-allocator stage 10 follow-on — the machine vector file widens from ZMM0-15 to ZMM0-31, the `10o`…
- [`2026-08-11g`](docs/performance-audits/2026-08-11g.md) — the remaining non-vector selection gaps of the `2026-08-10o` census, lifted in measured order on merged main…
- [`2026-08-11b`](docs/performance-audits/2026-08-11b.md) — the measurement `2026-08-10o` left blocked — the vector subset's dynamic payoff in buster-built stages —…
- [`2026-08-11`](docs/performance-audits/2026-08-11.md) — correctness, not throughput: every machine-register-allocator-built stage crashed `ide bench` — MIR_STACK…
- [`2026-08-10o`](docs/performance-audits/2026-08-10o.md) — register-allocator stage 10 — vectors, run as selection coverage first and allocation second, on the merged…
- [`2026-08-10n`](docs/performance-audits/2026-08-10n.md) — register-allocator stage 9 — pressure-aware scheduling over the machine IR, built with the…
- [`2026-08-10m`](docs/performance-audits/2026-08-10m.md) — instruction selection — the compare/branch fusion `2026-08-10j` recorded as its top selection lead, on both…
- [`2026-08-10l`](docs/performance-audits/2026-08-10l.md) — stage-8 pin economics — the three leads `2026-08-10j` left untaken, each built and measured on both corpora…
- [`2026-08-10k`](docs/performance-audits/2026-08-10k.md) — the second `2026-08-10j` selection lead taken — every integer immediate spent the ten-byte movabs, and the…
- [`2026-08-10j`](docs/performance-audits/2026-08-10j.md) — register-allocator stage 8 — live-range-scoped pins, and the first QUALITY win under real register pressure
- [`2026-08-10i`](docs/performance-audits/2026-08-10i.md) — the promotion/edge-contract pair `2026-08-09ap` specified, landed — and the third leg neither entry…
- [`2026-08-10h`](docs/performance-audits/2026-08-10h.md) — register-allocator stage 11 — the AArch64 machine backend, in four gated commits: the allocators…
- [`2026-08-10g`](docs/performance-audits/2026-08-10g.md) — rebase onto main, and the numbers refreshed on the merged tree
- [`2026-08-10f`](docs/performance-audits/2026-08-10f.md) — not a throughput audit — an ABI feature costed against the gate
- [`2026-08-10e`](docs/performance-audits/2026-08-10e.md) — the retry `2026-08-10d` left untaken, built and measured
- [`2026-08-10d`](docs/performance-audits/2026-08-10d.md) — not a throughput audit — a correctness fix costed against the gate, recorded here so the next audit does not…
- [`2026-08-10c`](docs/performance-audits/2026-08-10c.md) — the structural buy-back the `2026-08-09g` entry recorded as its next step — IR source ranges stop carrying…
- [`2026-08-10b`](docs/performance-audits/2026-08-10b.md) — the structural buy-back `2026-08-09g` recorded as the next step — the `2026-08-09c` Deus-Lex compaction…
- [`2026-08-10a`](docs/performance-audits/2026-08-10a.md) — not a throughput audit — the System V outgoing-argument alignment fix costed against the gate, measured…
- [`2026-08-09ap`](docs/performance-audits/2026-08-09ap.md) — local promotion built, measured, and reverted — with the sequencing it proves
- [`2026-08-09ao`](docs/performance-audits/2026-08-09ao.md) — the pressure corpus, and the finding that reframes the rest of the register-allocator plan
- [`2026-08-09an`](docs/performance-audits/2026-08-09an.md) — register-allocator stage 7 — QUALITY stops guessing and starts measuring, and finally wins
- [`2026-08-09am`](docs/performance-audits/2026-08-09am.md) — register-allocator stage 13 — optimization intent selects the allocator
- [`2026-08-09al`](docs/performance-audits/2026-08-09al.md) — register-allocator stage 7 — QUALITY exists, is verified, and does not beat FAST here
- [`2026-08-09ak`](docs/performance-audits/2026-08-09ak.md) — selection quality — member addresses become one instruction
- [`2026-08-09aj`](docs/performance-audits/2026-08-09aj.md) — selection quality — folded address arithmetic
- [`2026-08-09ai`](docs/performance-audits/2026-08-09ai.md) — register-allocator stage 7 lead — constant rematerialization, taken early because it is local
- [`2026-08-09ah`](docs/performance-audits/2026-08-09ah.md) — register-allocator stage 5 — allocator traffic metrics, and two negative results they explain
- [`2026-08-09ag`](docs/performance-audits/2026-08-09ag.md) — register-allocator stage 5 — copy coalescing: the largest single quality win of the project
- [`2026-08-09af`](docs/performance-audits/2026-08-09af.md) — register-allocator stage 6 — spill-slot reuse by defining block
- [`2026-08-09ae`](docs/performance-audits/2026-08-09ae.md) — register-allocator stage 6 — machine-path debug line entries reach canonical parity
- [`2026-08-09ad`](docs/performance-audits/2026-08-09ad.md) — register-allocator stage 3 — array loads, bit scans, and unsigned-64 float conversions: ninety-nine percent
- [`2026-08-09ac`](docs/performance-audits/2026-08-09ac.md) — register-allocator stage 3 — bit-field aggregate literals: coverage reaches ninety-eight percent
- [`2026-08-09ab`](docs/performance-audits/2026-08-09ab.md) — register-allocator stage 3 — variadic aggregate arguments: coverage reaches ninety-seven percent
- [`2026-08-09aa`](docs/performance-audits/2026-08-09aa.md) — register-allocator stage 3 — guard-page probes lift the frame bail; coverage reaches ninety-five percent
- [`2026-08-09z`](docs/performance-audits/2026-08-09z.md) — register-allocator stage 3 — array literals: coverage reaches ninety percent
- [`2026-08-09y`](docs/performance-audits/2026-08-09y.md) — register-allocator stage 3 — aggregate literals and debug traps lift coverage past eighty percent
- [`2026-08-09x`](docs/performance-audits/2026-08-09x.md) — register-allocator stage 6 — R12/R13 complete the callee-saved file; ModRM base quirks fixed
- [`2026-08-09w`](docs/performance-audits/2026-08-09w.md) — register-allocator stage 5 — straight-line edge contracts land structurally, and measure flat
- [`2026-08-09v`](docs/performance-audits/2026-08-09v.md) — register-allocator stage 6 — callee-saved registers, and register-copy edits instead of memory moves
- [`2026-08-09u`](docs/performance-audits/2026-08-09u.md) — register-allocator stage 6, first slice — spill slots only for values that touch memory
- [`2026-08-09t`](docs/performance-audits/2026-08-09t.md) — register-allocator stage 5, first slice — liveness-driven spill kills put FAST ahead of canonical
- [`2026-08-09s`](docs/performance-audits/2026-08-09s.md) — register-allocator stage 4 — the FAST local allocator, first allocator to survive the soak
- [`2026-08-09r`](docs/performance-audits/2026-08-09r.md) — register-allocator stage 3 — indirect calls, aligned locals, and atomics
- [`2026-08-09q`](docs/performance-audits/2026-08-09q.md) — register-allocator stage 3 — stack arguments and the memory-class aggregate ABI, crossing half the tree
- [`2026-08-09p`](docs/performance-audits/2026-08-09p.md) — register-allocator stage 3 — scalar float operations and the XMM ABI
- [`2026-08-09o`](docs/performance-audits/2026-08-09o.md) — register-allocator stage 3 — switch chains, variadic calls, and the aggregate ABI, taking the machine path to…
- [`2026-08-09n`](docs/performance-audits/2026-08-09n.md) — register-allocator stage 3, subset growth — shifts, the divide family, and direct System V calls in the…
- [`2026-08-09m`](docs/performance-audits/2026-08-09m.md) — register-allocator stage 3, first increment — MIR_STACK wired into `codegen_generate_canonical_module` with…
- [`2026-08-09l`](docs/performance-audits/2026-08-09l.md) — register-allocator stage 2 — the x86-64 scalar selector, MIR_STACK placement, and encoder over the compact…
- [`2026-08-09k`](docs/performance-audits/2026-08-09k.md) — register-allocator project stage 0+1 — the current-HEAD rebaseline under the new token metrics, plus the…
- [`2026-08-09j`](docs/performance-audits/2026-08-09j.md) — incremental IDE workspace analysis
- [`2026-08-09i`](docs/performance-audits/2026-08-09i.md) — the 512-bit SIMD builtin vocabulary and the tokenizer port onto it, measured against `2026-08-09g` on the…
- [`2026-08-09h`](docs/performance-audits/2026-08-09h.md) — not a throughput audit — a robustness change costed against the gate. An external audit reported that the…
- [`2026-08-09g`](docs/performance-audits/2026-08-09g.md) — proposal 3 of `2026-08-08k` taken as its own session — `CToken` 48 to 16 bytes with offset-based spellings…
- [`2026-08-09f`](docs/performance-audits/2026-08-09f.md) — the `c_parse_scope_add_entity` byte-FNV lead the `2026-08-09d` entry left on the table — every number…
- [`2026-08-09e`](docs/performance-audits/2026-08-09e.md) — the two remaining `2026-08-08l` stage-1 leads — the `c_parse_type_layout` persistent cache and the…
- [`2026-08-09d`](docs/performance-audits/2026-08-09d.md) — the `c_symbol_intern` cheaper identity path the `2026-08-09a` shape flagged as a lead — every number…
- [`2026-08-09c`](docs/performance-audits/2026-08-09c.md) — the full Deus-Lex-Machina compaction emitter for the buster tokenizer that `2026-08-08k` recorded as the…
- [`2026-08-09b`](docs/performance-audits/2026-08-09b.md) — the operand/target/immediate pools + build/consume SoA row split that `2026-08-09a` scoped as the top lever…
- [`2026-08-09a`](docs/performance-audits/2026-08-09a.md) — the first 2026-08-08k follow-up batch, on the merged tree — every number clang-built, measured…
- [`2026-08-08k`](docs/performance-audits/2026-08-08k.md) — a vectorization/branch-removal/ data-flattening survey — cycles, branch-miss, and L1d-miss `perf record`…
- [`2026-08-08l`](docs/performance-audits/2026-08-08l.md) — the same branch as `2026-08-08j`, taking the remaining ranked leftovers — arena reuse routing, the…
- [`2026-08-08j`](docs/performance-audits/2026-08-08j.md) — the IrValue label-metadata side table — the structural item every audit since `2026-08-08g` listed as THE…
- [`2026-08-08i`](docs/performance-audits/2026-08-08i.md) — started from the two 18:1x Superluminal captures of Release `ide test` and the stage-1 `ide cc`, analyzed…
- [`2026-08-08h`](docs/performance-audits/2026-08-08h.md) — the structural follow-up that took three of the `2026-08-08g` leftovers the same day — every number from a…
- [`2026-08-08g`](docs/performance-audits/2026-08-08g.md) — started from the two 15:58 Superluminal captures of `ide test` and the stage-1 `ide cc`, then re-profiled…
- [`2026-08-08f`](docs/performance-audits/2026-08-08f.md) — the finds came from two fresh Superluminal captures — the stage-1 compile and, for the first time, `ide test`…
- [`2026-08-08e`](docs/performance-audits/2026-08-08e.md) — method of `2026-08-08d`; every quoted number from a clang-built binary, `perf stat -e…
- [`2026-08-08d`](docs/performance-audits/2026-08-08d.md) — method of `2026-08-08c` plus one addition: a Superluminal capture of the stage-1 self-host compile, queried…
- [`2026-08-08c`](docs/performance-audits/2026-08-08c.md) — same method as `2026-08-08b`: `perf record -F 999 --call-graph fp`, instruction counts from…
- [`2026-08-08b`](docs/performance-audits/2026-08-08b.md) — `perf record -F 999 --call-graph fp`, instruction counts from `STEP_INSTRUCTIONS` and `perf stat -e…
- [`2026-08-08`](docs/performance-audits/2026-08-08.md) — sanitized Debug clang, `perf record -F 999 --call-graph fp`, `25650` samples, instruction counts from `perf…
- [`2026-08-07`](docs/performance-audits/2026-08-07.md) — sanitized Debug clang, `perf record -F 999 --call-graph fp` on a quiet host, `65466` samples
- [`2026-08-02b`](docs/performance-audits/2026-08-02b.md) — Release + Debug, with `--instrument --time-trace`
- [`2026-08-02`](docs/performance-audits/2026-08-02.md) — Release `bench_all`, `BUSTER_INSTRUMENT=1`
