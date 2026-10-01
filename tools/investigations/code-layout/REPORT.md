# Code-layout investigation — 2026-09-29

The concrete opportunity is encoder-local x86-64 branch shortening. A conservative
one-shot algorithm has a legality proof and a bounded implementation packet.
Its modeled savings on the hosted unity object are about 2% of function bytes;
this is not a compile-time, execution-time, or validated rewritten-object benefit.
Retain current production behavior. Go for the guarded diagnostic C prototype
below; no-go for production adoption, a general alignment solver, or late linker
byte deletion on this evidence.

## Evidence identities and ownership

- Production source: `8f67df736f13d4edc055110a7a6d619a00a22eaf`, tree
  `8810c6603e47e5eecf26e1d73633619a347c70d8` — current main when work began.
- Successful diagnostic source: `72fa6b3f8b9f9b2d13a55fd94d657e3f4a25098f`, tree
  `cf2191d7294ab730e10648e3a0a7ebb91450a7c7`.
- [Hosted run 36644597106](https://github.com/buster14a/buster/actions/runs/36644597106),
  job `109664515098`, `ubuntu-26.04`, success. Clang 21.1.8 (6ubuntu1), GNU
  objdump 2.46; 4 vCPUs, AMD EPYC 7763. This was a standard GitHub runner.
- Compiler SHA-256:
  `59a4c315321989ebede527eba56709fbba43ab59cf1dc2df84a9cb7253f90c1b`.
- Artifact `11068386417`, `code-layout-36644597106-1`, ZIP SHA-256
  `1688050ec6262c00540238aeaa392197ea5ab4b407e70c961b644b997e931621`.
  Raw objects, full function censuses and logs expire on 2026-10-29. Scripts,
  compact evidence, commands and oracle results are retained in this research
  branch; raw data can be regenerated from the pinned source and commands.
- Main advanced to `158cf10e303dfc98ce26e0ef6ab992eff95929ba` during work.
  The exact comparison contains only retirement automation, policy tests and
  guides. Compiler, assembly, object, linker and build sources are identical.
  The new AGENTS/workflow changes were reviewed. Measurements remain bound
  to the initial revision, not relabeled as measurements of the later tree.

The owner is [#1517](https://github.com/buster14a/buster/issues/1517), including
merged [#1518](https://github.com/buster14a/buster/pull/1518)'s in-place machine
output. All-state issue/PR searches covered layout, branch sizing/relaxation,
alignment, veneers/thunks, and relocation range. No competing general relaxation
implementation owner was found. Existing ownership remains intact: encoding
authority [#267](https://github.com/buster14a/buster/issues/267), unwind events
[#1918](https://github.com/buster14a/buster/issues/1918), signed PUSH relocation
[#1283](https://github.com/buster14a/buster/issues/1283), directive semantics
[#1282](https://github.com/buster14a/buster/issues/1282), COFF limits
[#1578](https://github.com/buster14a/buster/issues/1578), and transactional
metadata fallback [#1579](https://github.com/buster14a/buster/issues/1579).
Historical GOT failures (#764/#765, repaired by #767) are not refiled.
Length-preserving GOT relaxation and prior compact/persistent-lane decisions
are constraints, not invitations to replace the architecture.

Read before choosing the packet: AGENTS; machine, driver, build, testing,
benchmarking, parallelism, project, workflow and research guides; object-emission
and project-tracking documentation; encoding/GOT/executable-padding authorities;
recent performance audits. No production code, required CI check, admission
rule, generated binding, hot MIR row or lane architecture changed. The temporary
branch-only hosted transport is removed from the final research tree.

## Observed source: one native path

First path: C → canonical IR → MIR → FAST placement → x86-64 encoder → ELF64
ET_REL → native linker. Operand and placement facts fix nonbranch size choices;
exact encoding determines their bytes independently of branch displacement
decisions. Block ordering is fixed; changing that ordering
is outside this investigation.

Source anchors below refer to the pinned revision. Files are under
`src/buster/lib/compiler/`.

| Component | Observed responsibility and strategy |
| --- | --- |
| `codegen/machine_x86_64.c` | Ordinary JMP/Jcc and switch recipes use rel32 (10067–10072, 10747–10752, 13859–13865, 16649–16664). Emit once, record row/block/fixup positions, then signed32-check and patch each local field (16678–16723). No ordinary block alignment pass, shrinking pass, or local thunk. |
| x86 capacity/fixups | Checked worst-case budget rejects values above UINT32_MAX (15221–15300). `MachineX64BranchFixup` (14698–14723) carries addend, patch offset, block and label-address flag; arithmetic is checked s64. Layout work is linear in emitted bytes and side rows, with scratch proportional to rows/blocks/fixups/sites. Successful output is legal for its chosen rel32 layout, not minimum-size. |
| `codegen/codegen.c` | Align function starts to 16 on x86, 4 on A64 (11707 onward). Encode directly in the module buffer when possible (12372–12403). Publish debug, relocation and unwind data from finalized offsets (12746–12839). Function-byte savings and module `.text` savings are different quantities. |
| Inline assembler | Owns directives, target-specific alignment/fill and assembly labels/fields. The driver guide describes a forward-reference near-sized path, not the MIR relaxer. x86 inline transactions copy assembled bytes and locally patch asm-goto landing edges (15800–15880); those edges are absent from ordinary branch fixups. Their padding and PC-sensitive semantics invalidate an unguarded outer compactor. |
| `codegen/machine_aarch64.c` | Starts narrow and promotes local conditional branches 4→8→32 bytes, unconditional 4→28. The far transfer is ADR x16 plus four MOVZ/MOVK into x17, ADD and BR (7345 onward). This relative anchor avoids page/base dependence; scratch registers and flag semantics belong to the target authority. Short conditional reach uses signed19 words; B/BL uses signed26 words. Block-address materialization is a fixed six-word sequence. |
| A64 relaxation | Stable fixup order; restart after the first promotion. Rank grows at most twice per fixup, with limit `2F+1` (8438 onward). Insertion moves the suffix and shifts row/block/fixup/call/epilog/inline-relocation/landing maps (8211–8332). Fixed 4-byte growth preserves instruction alignment; no ordinary block-padding recomputation. |
| Native linker | Owns image/section placement, symbol/interposition/import decisions, relocation representability and format checks. Import PLT/stub construction is not a general range-extension veneer pass. Unsupported final A64 branch reach is checked/refused (link.c 7319–7328, 9612–9634, 10729–10755, 12559–12575). |

A64's finite-rank argument proves termination and checked legality, not global
minimum size. With S output bytes, N rows, B blocks, F fixups, C sites, E epilogs
and I inline side rows, its worst-case work includes suffix movement:
`O(F * (S + N + B + F + C + E + I))`, not just branch checks. Retained output
plus side maps use `O(S + N + B + F + C + E + I)` space. Capacity/offset overflow
or an unsupported field must return invalid rather than wrap. No timing of this
path was performed.

## Explicit dependency model and hypotheses

Ordered fragments have start o[i] and selected length l[i]. Ordinary recurrence:
`o[i+1] = o[i] + l[i]`; a mandatory alignment fragment instead applies
`round_up(o[i], a)`. x86 local branch displacement is
`target_boundary - (branch_start + chosen_instruction_length)`. rel8 is signed
[-128,127]; rel32 is signed [-2^31,2^31-1]. Fixups with target addends, page
anchors, external symbols or interior targets need their own equations.

The dependency chain is form → length → downstream starts/padding → endpoint
distance → representable forms. Relocation place/target, debug ranges, unwind
advances and function ends consume those final offsets.

- Fixed instruction sizes derived from operand facts do not depend on layout.
- Form rank can be constrained to grow monotonically. Offsets are nondecreasing
  under widening, but alignment padding is not; relative spans can decrease.
- Without recomputed padding or target addends, deletions inside an endpoint
  interval move an ordinary branch distance toward zero. Outside deletions move
  both endpoints equally. Own backward-branch shortening changes its end-PC and
  must be included explicitly.
- With alignment, padding can absorb growth or replace removed bytes, defeating
  an apparently obvious span-monotonicity proof. ADRP additionally depends on
  pages, and a veneer adds a new fragment and reachability obligation.

Hypotheses tested: (H1) conservative shortening has nonzero present-day width
opportunity; (H2) repeated widening buys substantially more than a single safe
pass; (H3) no-alignment reasoning can be generalized across mandatory alignment.
The cheapest discriminator was a small offline exact layout model before any
compiler build. H1 survives as modeled opportunity, H2 has modest extra savings
on this corpus, and H3 is false. None is an accepted performance claim.

## Bounded alternatives and proofs

### A1: conservative one-shot compaction — chosen prototype

Preconditions: initially valid all-rel32 output; fixed order and padding; normal
zero-addend CFG branch origins; no inline assembly or unsupported PC-sensitive
expansion; no nonzero block-addend fixup. Initially skip whole functions violating
a guard. Do not infer origins by scanning instruction bytes in production.

1. Inspect original fixups in emission order. For a forward candidate, prospective
   short displacement equals its original near displacement. For a backward or
   self candidate it is `old_delta + near_length - 2`. Mark if signed8.
2. Build a sorted deletion-prefix map. Validate exact short-form metadata recipes,
   nonoverlapping intervals, all retained offsets and final field values before
   the first in-place write. No public symbol/relocation/debug offset may point
   into a deleted instruction interior. If planning fails, decline optimization
   while the valid original bytes still exist.
3. Compact once. For a shortened JMP/Jcc, derive the new field from the mapped
   branch start: field at start+1, width1, end-PC start+2. In particular the old
   Jcc field at old_start+2 cannot be generically remapped: it is deleted.
   Unchanged fields, including LEA_BLOCK, remain width32 and are remapped normally.
4. Repatch all local fields and remap every row/block/site/symbol/end offset
   before existing codegen publication. External RIP/PLT/GOT/TLS fields preserve
   their width, target kind and addend; their field places move. Normal final
   object/link representability checks remain authoritative.

Proof: the prospective own deletion was included. Every other deletion between
the endpoints moves displacement toward zero; outside deletions shift both
equally. Marked shorts remain representable and initially valid long local
zero-addend references remain representable. Stable stream order determines
selection; there is no heuristic or hash-order tie. The algorithm terminates
after one finite selection, plan and copy, never revisiting a form choice.

For S bytes, F fixups and K map queries (including fixup endpoints/targets and
all public offset side entries), a sorted prefix map supports
`O(S + F + K log(F+1))` worst-case work and `O(F)` extra lane-local scratch.
Mapping can become linear only after side-table ordering is established, not
assumed. No per-byte map, solver, persistent allocation, callback dispatch,
second IR or hot-row expansion is needed. The 24-byte cold fixup has reserved
bytes; a form/length tag may fit there, subject to sizeof/ABI checks.

Local VA_ARG, stack-allocation and atomic retry edges move intact inside closed
row expansions (x86 16380–16395, 16423–16435, 16466–16473). Candidate capture must
exclude their internal fields. LEA_BLOCK uses the same fixup stream with
`label_address=true` (15672–15685); it is never a shortening candidate.

This proves a legal chosen local layout under the guards, not minimum size or
unchanged final external-link reach at extreme range boundaries. Declining an
invalid initial rel32 layout does not prove no smaller supported layout exists.

### B: shortest-start widening — comparator

Start all candidates short, recompute the entire layout, promote every invalid
short simultaneously in stable order, and final-check all forms. At most F
promotions and F+1 scans: `O(F*(N+F))` work and `O(N+F)` scratch for N fragments.
In the fixed two-form, zero-addend, no-recomputed-alignment model, an invalid
short stays invalid in every componentwise larger state. Induction shows each
promotion necessary in every legal assignment, so the final state is least and
minimum-size **within that model only**.

With mandatory alignment, finite-rank termination and final checked legality
survive; least-state/minimum-size and complete feasibility discovery do not.
If a final long form fails, reject that chosen result. Do not announce globally
"no supported encoding" unless the authority or bounded oracle establishes it.
A trial-shrink algorithm that rechecks the whole tentative layout is also
bounded/legal starting from a valid layout, but has quadratic work and can miss
the minimum. A prefix-layout exhaustive oracle can take `O(2^F*(N+F))` work. The actual
independent real-function oracle instead sums deletions per edge: with E modeled
edges, `O(2^F*(E*F+F))` work, streaming linear scratch, capped at 12 candidates.
The synthetic oracle materializes legal tuples, but has at most eight assignments
per included layout; that bounded list is not a production memory design.

## Executed experiments and actual results

### Exhaustive finite synthetic families

`synthetic.py` enumerates every form assignment for each included layout,
independently of the candidate algorithms. It uses actual x86 branch lengths
and signed8 bounds. This is exhaustive within its declared finite families,
not all possible programs.

| Family | Layouts | Assignments | A1 illegal | A1 nonminimum | Widen illegal | Widen nonminimum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| No alignment, 2/3 branches | 117,648 | 912,960 | 0 | 12,872 | 0 | 0 |
| One alignment, finite 2-branch family | 129,960 | 519,840 | 2,161 | not counted | 0 | 0 |

The second row deliberately applies A1 without its guard and rejects that
generalization. Guarded trial-shrink was legal throughout that family but
nonminimum in 1,972 cases. No-alignment widening used at most four scans.

Three separate witnesses were also exhaustively checked:

- `123 bytes; Jcc T; 4 bytes; align128; T`: near6 ends129, T=256, delta127;
  short2 ends125, T stays256, delta131. Shrinking increases padding by4 and
  creates an illegal branch with no total-size saving.
- `L0; JMP L2; 127 bytes; L1; align256; JMP L1; L2`: both short displacements
  +256/-129 cause simultaneous promotion to261 bytes. First long/second short
  yields258 bytes and second delta-126; exact oracle minimum258. Widening is
  legal but nonminimum.
- `JMP T; 2^31 bytes; T`: neither rel8 nor rel32 represents the displacement.
  Exact oracle finds no assignment; the sparse model allocates no giant buffer.

### Real Buster output, hosted validation

Four existing control-flow fixtures were compiled with debug information and
strict MIR verification/zero fallback. Their source checks contain no assembly
marker. Unity self-compile used native host ISA features, no debug, FAST
allocator, strict verification and zero machine fallback. It reported
`CODEGEN_VERIFY ... mir=5341`, `fallback_functions=0`, 1,790,001 successful exact
encoder attempts, `machine_code_in_place=21598568`, `machine_code_copied=0`.
The compiler and objects were not patched. Compiled fixtures were not executed.

`layout_census.py` reads ELF64 ET_REL symbols/relocations, uses objdump boundaries
cross-checked against actual bytes, excludes relocated candidate instructions,
interior targets and overlapping extents, and checks fixed local edges. It
freezes all other fragments, including emitted internal padding. The independent
small oracle uses deletion equations rather than the candidate layout helper.

| Diagnostic | Four fixtures | Unity |
| --- | ---: | ---: |
| Functions / candidate branches | 17 / 284 | 5,341 / 246,280 |
| Original function bytes | 10,429 | 21,598,568 |
| A1 saved bytes / selected shorts | 604 / 181 | 427,043 / 129,767 |
| Widening saved bytes / selected shorts | 642 / 192 | 444,329 / 134,833 |
| Extra widening savings over A1 | 38 | 17,286 |
| Widening fragment visits / branch checks | 4,981 / 689 | 10,993,993 / 713,759 |
| Maximum widening scans | 3 | 7 |
| Oracle functions / assignments | 9 / 5,443 | 2,502 / 1,096,127 |
| A1 / widening oracle minimum disagreements | 0 / 0 | 157 / 0 |
| Code relocations / RIP-relative instructions requiring repair | 48 / 18 | 52,027 / 9,812 |

All modeled branches were legal. Unity A1 saves 1.977% of body bytes; widening
saves 2.057%. Unity has 833 zero-candidate functions, 1,669 with 1–12 candidates,
and 2,839 above the oracle limit; maximum 11,778. Only 11,719 of 246,280 candidate
edges are in oracle-covered functions. Example real nonminimum A1 result:
`os_barrier_wait`, 370→347 bytes, versus344 for widening and exact oracle.

These counts do not certify production eligibility. In particular, unity's
4,508 candidate-containing functions lack per-function exclusion of inline
assembly/alignment; 3,630 contain code relocations and 1,311 contain RIP-relative
instructions. An emission-origin census is the next cheap guard check.

Separate offline module accounting checks object hashes, every function extent,
16-byte starts and NOP gap bytes. No opaque inter-function or trailing data was
discarded. Reapplying only entry alignment gives:

| `.text` accounting | Current | A1 hypothetical | Widening hypothetical |
| --- | ---: | ---: | ---: |
| Four fixture objects | 10,538 | 9,925 | 9,888 |
| Unity | 21,638,362 | 21,211,177 | 21,194,105 |
| Unity entry-padding bytes | 39,794 | 39,652 | 39,866 |

Unity hypothetical `.text` reductions are 427,185 and 444,257 bytes. In two
fixtures, entry padding absorbs all extra widening savings. This arithmetic
does not validate rewritten relocations, debug or unwind data.

Failed attempt retained: diagnostic `37c7c479d1b4bd90bf744b2e4b67c2c26493968e`,
[run 36644060882](https://github.com/buster14a/buster/actions/runs/36644060882),
job 109662806185, built successfully and passed fixture/synthetic diagnostics,
then rejected unity `xgetbv` because the explicit baseline target lacked XSAVE.
There was no valid unity object from that attempt. The correction adds
`-march=native` to unity, not a fallback exemption. That run also used the
stricter A0 predicate (old displacement without backward own-shortening
adjustment): 596 fixture bytes, versus corrected A1's 604. No result was hidden
or reclassified. Indexed parser optimization preserved the A0 output; A1 was
separately checked on 12,348 generated layouts with zero legality failures.

## Target and object-format authorities remain separate

| Authority | Consequence for layout changes |
| --- | --- |
| x86 ELF | PC32/PLT32 use signed `S+A-P`; ABS32 requires zero-extension, ABS32S sign-extension (link.c4555–4602). Preserve relocation promise/kind, field width and addend. GOTPCREL, GOTPCRELX, REX_GOTPCRELX and CODE_4_GOTPCRELX remain distinct (object.c5431–5457); plain GOTPCREL promises no instruction boundary. Metadata GOT rewrites preserve byte count and field position. |
| AMD64 COFF | REL32_0…REL32_5 have distinct post-field biases; reader normalizes addend by `-4-k` (object.c6565–6574). A common end-PC fiction must not erase that provenance. |
| A64 ELF | CALL26/JUMP26 use 4-byte-scaled signed26 fields (byte range[-2^27,2^27)); PREL32 is signed32, while ABS32's ABI range permits signed-or-unsigned32. An enum/reader spelling is not proof every native writer supports it. ADRP uses `Page(S+A)-Page(P)`. |
| A64 COFF | Page forms have distinct immediate/addend decoding; inline addends are recovered before page arithmetic (object.c516–551). Do not import ELF field assumptions. |
| Veneers/thunks | AAELF64 permits CALL/JUMP veneer insertion for STT_FUNC, separate input sections, or undefined targets; otherwise overflow is an error. X16/X17/flags clobber permissions and BTI landing obligations are ABI-specific. Placement introduces another bounded-range edge; a shared thunk/nearest island heuristic needs its own proof, not this compaction proof. |
| Mach CFI | EH-frame record anchoring differs from a generic field-PC. Buster stores the wire addend as `A-(P-R)`, using record boundary R (object.c6924,13063–13086). Both record and field movement matter. Apple-header retrieval was unavailable; this conclusion is source/documentation supported, not a fresh Apple-spec claim. |
| Debug/unwind | Row offsets drive lines/locations; sites drive module relocations; final code_size drives FDE length (codegen.c12746–12839; dwarf.c349–360,510). Prologue actions remain unchanged for body-only shortening, but function ends, CFI advances where applicable, line/range/location extents and symbols must use final offsets. A64 epilog/unwind offsets are word-scaled and length/epilog fields are 18 bits; unsupported cases reject (object.c10421,10483–10490). |

The native linker rebases debug contributions (link.c2171–2180) and copies
opaque payloads; it does not repair arbitrary internal DWARF deltas after byte
deletion. Finalize encoder layout before debug/unwind serialization. Coordinate
with #1918 rather than designing another frame-event channel.

A confirmed guide discrepancy was recorded under #267: machine.md745–748 says
the ELF reader collapses GOT variants; current code preserves them. This is a
documentation observation, not a newly reproduced linker defect. PLT32 collapse
is a separate explicit current policy. Other relocation concerns remain with
their existing owners and were not claimed as new reproduction results.

## Relevant primary research and licensing

- Boender/Sacerdoti Coen, [On the correctness of a branch displacement
  algorithm](https://www.cs.unibo.it/~sacerdot/PAPERS/assembler.pdf), 2012:
  distance-only relaxation admits a least-state argument; segment/absolute forms
  invalidate it. Finite rank restrictions can prove termination without size
  optimality. This informs the proof boundary; its illustrative x86 table is not
  the encoding-range authority. CerCo code was not imported; its software
  licensing was not established in this investigation.
- LLVM [commit debb2514ea7f062a29e5e4740f9d6ee4cea3b978](https://github.com/llvm/llvm-project/commit/debb2514ea7f062a29e5e4740f9d6ee4cea3b978),
  March 4, 2026, [PR 184544](https://github.com/llvm/llvm-project/pull/184544): fuses
  relaxation/layout so alignment sees fresh upstream offsets, eliminating a
  stale-offset convergence trap; forward-reference stretch preserves displacement
  reasoning. This supports explicit dependency ordering, not a claim that LLVM
  solves the global minimum or that Buster needs its fragment architecture.
  LLVM is Apache-2.0 with LLVM exceptions; no source was copied.
- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
  supplies x86 form/range authority. [AAELF64](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)
  and [AAPCS64](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
  supply Arm relocation/veneer obligations. Fetched AAELF64 header: 2025Q4,
  23 January 2026; later main-branch history does not establish a released 2026Q3
  version. Arm ABI documents use CC-BY-SA4.0 with the patent grant.
- Microsoft [PE/COFF](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
  and [ARM64 unwind](https://learn.microsoft.com/en-us/cpp/build/arm64-exception-handling?view=msvc-170)
  retain their native semantics. GNU Binutils objdump reports GPLv3-or-later;
  it is an offline diagnostic tool, not a new production dependency.
- Buster first-party code remains **unlicensed/unresolved** per README,
  THIRD_PARTY_NOTICES and LICENSES/README (#621). Imported component licenses
  do not license the first-party project. This report does not infer otherwise.

## Commands, limitations and smallest implementation packet

Exact hosted build (fresh checkout, production directory):

```sh
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o ../build-driver
../build-driver generate --cc clang --ci --linker DEFAULT
../build-driver build --config Release -t ide
```

Each source in `fixture_sources.txt`:

```sh
build/Release/ide cc -target x86_64-unknown-linux-gnu -fregister-allocator=fast -fno-machine-fallback -fverify-codegen -g -c SOURCE -o OBJECT
```

Unity and offline diagnostics (all actual argument arrays are in `commands.json`):

```sh
build/Release/ide cc -target x86_64-unknown-linux-gnu -march=native -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g0 -v -fregister-allocator=fast -fno-machine-fallback -fverify-codegen -c src/buster/apps/ide/ide.c -o UNITY_OBJECT
python3 tools/investigations/code-layout/synthetic.py --output synthetic.json
python3 tools/investigations/code-layout/collect.py --production PRODUCTION_CHECKOUT --output EVIDENCE_DIRECTORY
python3 tools/investigations/code-layout/module_alignment.py EVIDENCE_DIRECTORY --output accounting.json
```

`collect.py` invokes the real-function census with exact object lists, the pinned
revision, and a separate fixture source-no-asm flag. The unity flag is absent.
Its paths/checksums are evidence identifiers, not portable absolute-path promises.

Accepted performance measurements: **none**. No changed compiler, rewritten
object, generated-program execution, debug/unwind runtime test, compiler timing
or RSS A/B was accepted. Local work was offline arithmetic/parser validation;
no desktop benchmark was run. No authenticated benchpress session was available
here. The dedicated 9700X's documented GitHub gateway admits only
`validate-buster-v1` smoke, not a performance verdict; native-retirement
performance remains blocked. No bypass, new runner policy or privileged
installation was attempted. Windows/macOS/A64 native validation was not obtained.

The strongest counterargument is concrete: #1518 removed the output copy, and
this proposal adds a traversal plus offset planning/repair to a throughput-first
compiler for modest size savings. Smaller code does not establish faster
compilation or faster generated code. Repeated widening buys only 17,072 extra
hypothetical unity `.text` bytes here and scales poorly on large CFGs.

Smallest useful packet under #1517, with #267/#1918 coordination:

1. A diagnostic-only C encoder-local A1 plan, using existing cold fixups and
   lane scratch. Capture candidate origin/form and refusal reasons. Start with
   whole-function guards for inline asm, nonzero block addends, unsupported
   interior references, and any untracked PC-sensitive edge. Count eligible
   candidates/deletions, metadata lookups and bytes actually moved.
2. Plan/final-check before destructive compaction; handle shortened Jcc fields
   explicitly. Publish only final offsets through existing result/authority
   channels. No change to canonical IR, compact hot rows, persistent lanes,
   fallback/admission policy, format enums or dependencies.
3. Validate ±128 and signed32 boundaries, self/forward/backward/switch/LEA_BLOCK,
   fixed expansion edges, refusal guards, debug ranges, unwind ends and external
   field places against the bounded oracle on standard GitHub runners. Compile,
   link and execute fixture equivalence; check debug/FDE ranges after emission.
   A model-only pass is insufficient for that acceptance.
4. Stop on any representability/offset/guard mismatch. Before a production
   decision, qualify same-root builds and immutable-binary A/A plus cross-root
   controls on benchpress 9700X using the existing paired throughput/RSS contract.
   Predeclare corpus, order, sample count and existing admission envelope there;
   do not invent a weaker size-only acceptance rule. Reject if that envelope
   fails or eligible savings disappear. Until then retain the all-rel32 path.

Immediate gate for this slice is an emission-origin/eligibility census and
diagnostic prototype, not `needs-benchmark` for an unimplemented mechanism.
The broad parent issue's lifecycle and implementation scope are unchanged.
