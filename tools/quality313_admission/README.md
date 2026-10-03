# QUALITY #313 admission micro packet

Research-only branch `codex/313-admission-measurement`. One standard Ubuntu
read-only push job, at most ten minutes, no compiler/probe execution locally,
no dedicated host access and no compiler production/test/workflow changes.

Frozen subjects remain baseline `97fb07f42b432864bee9fd38b70eb69b15252991` and
candidate `735d270728441245c5a1d1721220c16408b101b9`. Their full QUALITY source
Git blobs are respectively `c8d23927d87d6f6f320df23863d5dfff4edd5828` and
`747ffac55f9276633c85bcb8aa99a8232a3f459c`; private headers are respectively
`ee652fb8b2565f217d99d5dfbee1a4525c0f7259` and
`9f3dc31c6ec2900ba93fe8689326d07db2e5a995`. The workflow verifies every blob,
retains full inputs and extracts exact helper text with `extract.c`.
Extraction also requires the interval/traffic type declarations byte-identical
between subjects; the generated includes and all packet files are hashed.
No helper algorithm is copied or rewritten into committed packet code.

`select.c` includes the exact old max-heap sift or actual candidate comparator,
ordered sift, placement sift and admission offer. Its baseline wrapper copies
the literal first min(N,4096) entries and stops, matching the old prefix policy.
The candidate wrapper scans N and calls the actual offer. Both then perform
the literal final best-first heapification loop from QUALITY. The measured
scope assumes every provided interval is eligible: it excludes traffic/lifetime
construction, eligibility tests, inverse mapping, candidate pops, placement
probes and every other part of a compiler invocation.

The shared driver and selection wrapper compile as separate translation units,
with `-fno-lto` through link. The driver cannot see or discard repeated selector
calls. Recorded disassembly permits checking the repeated call loop. No
instrumentation counter, rank oracle or checksum walk occurs inside the timing
span; after timing a full payload/set checksum reaches a volatile sink.

Fixed cells: populations 16, 4096, 4099, 8192 crossed with equal, ascending and
descending u64 weights (all exceed 2^40), fixed limit 4096. Encounter IDs use a
bijective modular permutation for these four sizes, rather than source order.
Every payload/start/end/marginal field is initialized deterministically. Before
and after each 4096-operation timed batch, an independent oracle counts superior
input entries directly (no heap implementation), validates expected membership,
unique payloads, best-first parent relations and both sides' four-word canaries.
The baseline oracle instead requires exactly the literal prefix; its equal
placement order remains the old weight-only rule. Set checksums are independent
of heap order. This measures new policy overhead, not equivalent output sets
where N exceeds the cap.

`collect.c PREFIX BASELINE_EXE CANDIDATE_EXE` chooses one inherited CPU and pins
itself, so both children inherit the same affinity. Each of the twelve cells
has one retained warmup pair and twelve fixed alternating A/B pairs, giving
312 raw child records. Every child has a 60-second alarm. No failure changes
the fixed population; no hidden iteration adjustment or adaptive retry exists.
Each child reports exactly 4096 operations, a successful independent oracle,
elapsed monotonic nanoseconds and a set checksum. CSV contains every sample,
including warmups and failures; a missing/failed oracle or changed per-variant
checksum fails collection. Below/equal to the cap both subjects must also have
the same set checksum. Analysis excludes pair 0 and reports paired C/B ratios,
absolute nanoseconds per selector and raw variation; workflow success alone
asserts no economic threshold or dedicated-host budget.

Equal traffic exercises lower-ID admission/placement ties, ascending traffic
exercises sustained replacements beyond the cap and descending traffic
exercises cheap rejection. At exactly 4096, the candidate worst-heapifies once
at cap and then best-heapifies; below cap admission performs no heap work.
N=8192 is bounded stress, not evidence of real-project candidate prevalence.

Earlier full-stress evidence remains distinct and immutable: corrected hosted
run 37151344386 observed 4099 eligible candidates and one cap hit in each shape;
late-hot removed eleven allocator edits and 41 .text bytes, with successful
four-seed generated-runtime oracles. Its missing llvm-size command failure is
preserved and static retained-ELF inspection supplies sizes. Real candidate
unity census observed zero cap hits in both frontend cells. Full compile-time
medians were noisy; this packet makes no prior profitability claim. The first
rejected-clobber experiment 37150651728 remains preserved too.

Validation before publication is source review and whitespace/Bash syntax
inspection only. No compiler, extractor, micro executable or measurement is
run locally. Trusted Clang compilation and all oracle/timing results are hosted
gates. Buster first-party licensing remains unselected per pinned
`LICENSES/README.md` and #621; no third-party implementation or dependency is
imported.
