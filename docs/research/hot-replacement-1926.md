# Controlled-host hot replacement: protocol and diagnostic result

Research owner: [#1926](https://github.com/buster14a/buster/issues/1926), as an
optional extension of its existing compiler/JIT composition and lifetime work.
This report does not expand that issue's SDK acceptance criteria. The separate
CLI run capability remains owned by [#1935](https://github.com/buster14a/buster/issues/1935).

## Answer and decision

**A useful bounded capability is feasible:** replace one host-selected module
implementation between explicit application batches, retaining the process and
its host-owned state. Keep its entry signature, state layout, target and import
contract fixed. Reuse Buster C compilation, canonical IR, native object emission,
object decoding and object JIT. No editor, alternative compiler pipeline,
production dispatch change or generic function-indirection framework is needed.

**Go** for a controlled-host, same-contract quiescent integration experiment.
**No-go** for advertising production hot replacement, arbitrary C state
migration, active-frame rewriting, concurrent versioned execution or a latency
improvement on this evidence. The executable diagnostic proves bounded ownership
and behavior, not workload utility, supported-platform acceptance or performance.

The first stateful extension worth considering is a fixed-layout context owned
by the host and passed explicitly to the module. It was exercised here. It does
not require migrating module globals, TLS or arbitrary pointer graphs.

## Exact source and execution envelope

- Main investigated and built: `8f67df736f13d4edc055110a7a6d619a00a22eaf`.
- Base tree: `8810c6603e47e5eecf26e1d73633619a347c70d8`.
- Before publication, main advanced to `158cf10e303dfc98ce26e0ef6ab992eff95929ba`,
  tree `a9a61114003a821f9dbfdc0b6ac3cc774f5e8471`. The connected-GitHub comparison
  contains retirement automation/policy documentation and tooling changes only;
  no compiler/JIT/link/object/runtime or build-source files changed. Its updated
  AGENTS/workflow instructions were reconciled. The research branch uses that
  newer parent; the executed binaries/results remain explicitly anchored to 8f67.
- Trusted Clang-built Release `ide` SHA-256:
  `ab8df337fa9263607a1f124c692afd5b123ad42c41d13415c021b070e9f669be`.
- Diagnostic host: remote Ubuntu 24.04.3 execution container, Linux x86-64,
  AMD EPYC 9V74 VM with nine exposed CPUs. This is neither the user's desktop,
  a standard GitHub runner, nor the dedicated Benchpress 9700X.
- Host compiler: Clang 20.1.2. GCC 13.3.0 also syntax-checked the host.
- Canonical bootstrap: TinyCC `0fb54300b56512754221d80adda85ddb9815bceb`
  (0.9.28rc), built with GCC 13.3.0. Distribution TCC 0.9.27 first failed at
  `string.c:1020`, the documented limitation already tracked by closed #795.
- Modules: eight trusted accompanying C fixtures, strict `mir-stack`,
  `-fverify-codegen -fno-machine-fallback`, native Linux x86-64, `-g0`.
- Host: `BUSTER_SINGLE_THREADED=1`; serial control and module calls on one OS
  thread. No new worker threads, lane pool, compiler callbacks or dependencies.
- Research files alone were added. Production source, build graph, CI policy,
  admission requirements and generated authority bindings were not changed.

[Build provenance and full compiler commands](../../tools/hot_replace_probe/evidence/build.json),
[module commands/results](../../tools/hot_replace_probe/evidence/module-builds.json),
and the [complete final transcript](../../tools/hot_replace_probe/evidence/run.txt)
are retained. File digests are in
[identities.json](../../tools/hot_replace_probe/evidence/identities.json).

## Source observations, hypotheses and experiments

Read `AGENTS.md`, the research, driver, build, testing, parallelism, benchmarking
and frontend-linkage guides, compiler project documentation, and newest audit
`docs/performance-audits/2026-09-29T184335Z.md` before selecting the pilot.

| Evidence class | Finding |
|---|---|
| Source observation | `compiler_driver_execute_c_single` preserves C preprocessing/parsing/semantics, canonical IR preparation and native object generation. The OBJECT action also serializes/publishes an object file; this is ordinary compilation cost. |
| Source observation | `link_objects` can combine objects using caller-owned arenas. Its optional single-input section aliasing borrows payloads. The single-object pilot needs no separate merge; its relocation/link step is `jit_link_object`. |
| Source observation | `JitProgram` is single-owner. Its object header, sections, symbols and names are borrowed. Imports must remain live for every execution that can reach them. `jit_program_release` supplies no live-user tracking. |
| Hypothesis | Separate candidate mappings and one serial application selector suffice for same-contract replacement without rewriting compiler dispatch or existing code. |
| Executed diagnostic | Separate mappings, busy refusal, publication, old-version leases, final-user reclamation and candidate failure preservation all passed in one process. |
| Executed diagnostic | A fixed host-owned state remained usable across a quiescent implementation change: first call produced total 7/calls 1; second implementation produced total 21/calls 2. |
| Executed diagnostic | Missing entry, missing import, TLS, wrong contract labels, and publication of a correctly tagged different ABI mode were refused while the incumbent remained callable. Injected arena-release failure retained ownership and permitted retry. |
| Rejected inference | An ELF `PLT32` import on disk does not establish that this tested JIT composition rejects the import: `object_read` normalizes ELF relocation types 2 and 4 to `OBJECT_RELOCATION_X86_64_PC32`. The explicitly bound import executed successfully. |
| Remaining source-only concern | Direct `object_from_canonical_codegen_module` preserves the PLT32 enum, whereas `jit_relocation_is_supported` excludes that enum. The direct in-memory composition was not executed here; #1926's future facade must verify its actual object path. No production repair is proposed from this source observation alone. |
| Accepted measurement | **None.** No timing, 9700X acceptance or hosted-validation result exists for this investigation. |

The cheapest distinguishing experiment was the stateless same-signature module:
`int pilot(int)` changes from `value + 1` to `value + 2`. At input 7, the live
host observed 8 before replacement and 9 afterward. The versioned diagnostic
then retained the old v2 lease while selecting v1 for new work: old returned 9,
new returned 8, and release stayed BUSY until the old lease was dropped.
This is serial logical overlap, not proof about simultaneous execution or a
suspended native stack frame.

## Ownership protocol and publication

Each stable `ProbeVersion` slot owns an arena containing the input bytes,
decoded object, section/symbol/relocation/name storage, one `JitProgram`, a typed
application entry and its lifecycle. Its address never changes. In particular,
`program.object` points at `version->object`, not a copied stack-local header.
Do not copy a successfully linked program or a live lease.

Preparation reads and decodes a bounded fixture, checks its contract and entry
kind, allocates a separate JIT image, resolves imports/relocations, finalizes
protections and instruction-cache coherence, then performs symbol lookup. The
current generation is untouched. The tag is an application assertion tied to
the trusted fixture source; it is not C signature inference. Symbols must be
defined FUNCTION entries in TEXT before conversion/calling.

Quiescent publication requires no outstanding leases on the current generation
and the same active-session ABI mode. In this serial host, every module call is
inside a lease; no other thread, callback or address holder can enter it.
**The publication point is `host->active = candidate` in `probe_publish`.**
The candidate is already ready; subsequent work acquires it. The old slot
becomes retired. There is no code patch and no rebinding of old raw addresses.

For a real batch host, stop new entry, finish the existing batch and publish
between batches. A module currently inside a host import is still active: its
return address can point into the old image. Completion means the entire
dynamic extent returned, not merely that the instruction pointer left TEXT.
If the host already uses Buster lanes, capture a generation per work batch and
publish only after the existing lane barriers prove the batch finished. Prewarm
compiler tables serially before starting the persistent gang. This lane
extension is a proposal, not executed coverage here.

**The reclamation decision is absence from the active selector plus zero pins.**
Clear the last lease's callable fields before decreasing its pin count. Clear
the retired slot's entry, call `jit_program_release`, then destroy its arena.
If arena destruction fails, keep the arena/object owner in RELEASE_PENDING,
refuse reuse/publication and permit cleanup retry. JIT release itself has no OS
unmap-success result: the diagnostic proves the decision and ownership sequence,
not independently observed physical unmapping.

## State transitions and failure semantics

| State / event | Result and ownership |
|---|---|
| EMPTY or RELEASED; prepare | PREPARING, then READY or REJECTED; the incumbent is unchanged. |
| READY; same-contract publication at quiescence | ACTIVE; previous ACTIVE becomes RETIRED. |
| READY; quiescent publication with old pins | BUSY; candidate remains READY and incumbent remains ACTIVE. |
| READY; different active-session contract | ABI error; selector and both versions remain unchanged. |
| READY; separate serial versioned publication | ACTIVE; old becomes RETIRED with its old leases still valid. |
| ACTIVE; acquire lease | Snapshot this generation and add its pin. |
| ACTIVE or RETIRED; drop lease | Invalidate callable capability, then remove its pin. |
| RETIRED with pins; reclaim | BUSY; retain code and all borrowed object metadata. |
| READY, REJECTED or RETIRED without pins; reclaim | Release image, then arena; RELEASED only after successful arena destruction. |
| Arena release failure | RELEASE_PENDING retains owner; prepare/publication refused, cleanup retry allowed. |
| Shutdown | Remove active selector, retire its slot, then use the same reclamation checks. |

Recoverable I/O, object decode, policy, JIT link and entry lookup failures affect
only candidate resources. No candidate can publish before READY. Missing lookup
mutates `JitProgram.error` without freeing its mapping, so the lifecycle and
ownership, rather than `error == NONE`, determine whether a slot is live.

Compilation failure occurs upstream of preparation in this external-compiler
pilot; no publication is possible when no candidate object is produced. Invalid
source compilation while the host is running was not separately exercised.
Arena allocation follows the existing fail-fast API; OOM survival is not
promised. Executing bad code, a fault, `longjmp`, corruption or imported side
effects is not a recoverable transaction. After publication, returning to the
old selector cannot undo already-mutated state or external effects.

## Quiescence versus versioned execution

| Property | Chosen quiescent design | More ambitious versioned design |
|---|---|---|
| Entry | One host-selected generation per batch; internal calls stay direct. | Pin a whole generation for each work item/call graph; new work can select the new generation. |
| Old stack frames | Finish before publication. | Continue entirely in their old version; retain every return target and dependency. Never rewrite frames. |
| Publication | Host boundary after drain; one serial selector in this pilot. | Requires synchronized acquire/pin and publication, plus version-consistent state/import ownership. |
| Reclamation | Immediate after publication if no old holder exists. | Deferred until the last old call, leased address, dependency and metadata reader disappears. |
| Cost | Wait for explicit boundary, stage candidate, update selector, release old resources. | Pin synchronization/epochs and overlapping version memory, plus potentially unbounded retirement delay. |
| Evidence here | Executed serial quiescent checks. | Executed serial old/new lease overlap only; concurrency and active frames untested. |

Loading an atomic active pointer and then incrementing its refcount is unsafe:
reclamation can occur between those operations. A future concurrent host needs
one synchronization domain for acquisition/publication/reclamation or a proven
preallocated per-lane generation/epoch protocol. A counter does not discover
escaped addresses. Do not force reclaim after a timeout; keep the old version
or reject/backpressure another update when the bounded slot budget is full.

## Exact replacement envelope

| Concern | Pilot and proposed narrow contract |
|---|---|
| Replaceable unit | Complete implementation/object version at a declared application entry. All internal direct calls and constants belong to that version. No arbitrary host-function patching. |
| Active stack frames | Quiescent design waits for return. Versioned design retains old code through full dynamic extents, including calls into host imports. No saved continuations or nonlocal exits. |
| Escaped function/data addresses | Forbidden outside host-owned leases. Queued callbacks, signal handlers, spawned threads, retained interior data pointers and cross-generation module imports are outside the envelope. |
| Module globals/statics | Nonempty writable DATA/BSS and init/fini registrations rejected. Fresh mappings otherwise create fresh module storage; no automatic preservation or pointer repair. Immutable fixture rodata is retained per version. |
| First stateful extension | `int pilot_step(PilotState *, int)`, same host-owned layout and lifetime. Quiescent updates only; no concurrent old/new mutation of shared state. |
| State schema change | Refused. A later explicit migration would need private new state and publication of `(code,state,generation)` together; aliasing, resources and interior pointers require a separate contract. Byte copying arbitrary C storage is insufficient. |
| TLS | The diagnostic intentionally forwards the TLS fixture to the JIT's `JIT_ERROR_TLS_UNSUPPORTED` gate. No TLS replacement, destructor handling or per-thread migration. |
| ABI | Signature, result, calling convention, layout, target CPU permissions and relevant semantic meaning remain fixed. Name lookup does not prove type compatibility. The two fixture ABI modes are separate sessions. |
| Imports | Explicit name/address/kind bindings; no implicit process-wide symbol lookup. Targets are frozen at load and must remain valid. One permanent host function was bound and executed. External data and unsupported GOT/relocation forms remain restricted. |
| Debug/unwind | Current JIT maps TEXT/RODATA/DATA/BSS and skips debug/unwind sections. No registration, stack-walk or exception-propagation guarantee was tested. Keeping object bytes alive does not register them. |
| Future metadata support | Register per-version records after addresses finalize and before publication. Metadata readers need ownership too. After final users drain, unregister records before freeing record storage or code; rollback partial candidate registration on failure. |
| Platforms | Only native Linux x86-64 serial diagnostic executed. Windows/AArch64/mobile untested. Repeated direct API use is explicitly unsupported on macOS under the existing one-MAP_JIT-region contract. |

## Actual results and mechanism counts

The final host exited **0**, with **72 checks, 0 failures**. All eight module
compilations exited 0 through Buster's strict MIR path. GCC host syntax check
exited 0. Neither `--timings` nor any performance harness was run.

| Counter | Actual diagnostic value | Denominator / interpretation |
|---|---:|---|
| Candidate preparations | 12 | Includes reloading fixtures and rejected candidates, not twelve different sources. |
| Successful mapping constructions | 8 | Includes the missing-entry mapping and a READY cross-ABI candidate that never published. |
| Cumulative mapping bytes | 65,536 | Eight 8,192-byte mappings; cumulative, not peak RSS or a measured resident-memory saving. |
| Publications | 6 | Three initial activations plus three replacements: two quiescent and one serial versioned. |
| Completed arena/mapping-release sequences | 12 | Includes rejected candidates. One injected arena-release refusal was retried. Physical unmap success is not observed by this API. |
| Ordinary module calls | 13 | Correctness calls, not a throughput population. |
| Decoded section records visited | 288 | 12 preparations × 24 normalized section slots. |
| Decoded symbols / relocations counted | 27 / 15 | Includes metadata relocations and rejected inputs; not dynamic relocation instruction counts. |
| Unregistered metadata bytes counted | 560 | Cumulative nonempty skipped sections after contract admission, including repeated `.eh_frame` input. Not registered unwind coverage. |
| Successful explicitly bound imports | 1 | One permanent host function through the file-object route; one x86 import thunk by source construction. |

Failures are exact: wrong ABI labels -> PROBE_ABI; missing entry ->
JIT_ERROR_SYMBOL_NOT_FOUND; missing binding -> JIT_ERROR_UNRESOLVED_IMPORT;
TLS -> JIT_ERROR_TLS_UNSUPPORTED; prepared cross-ABI publication -> PROBE_ABI;
quiescent publication with a pin -> PROBE_BUSY; retired reclamation with a pin
-> PROBE_BUSY. Each relevant prepublication refusal left the incumbent usable.

Retained attempts: host build 1 failed on an existing metadata initializer under
`-Wextra`; build 2, with the configured missing-initializer suppression, exposed
missing standalone hash/DWARF link dependencies. Build 3 added existing
hash/byte-writer/DWARF sources and passed; its earlier run passed 66 checks.
Build 4 added the positive explicit-import experiment and passed; final run
passed 72 checks. These are probe build corrections, not production defects.

## Costs, unavailable environments and counterargument

Separate the following quantities before any latency claim:

1. Cold host/compiler prewarm and ordinary compilation: source-to-object,
   canonical validation, code generation, serialization and file publication.
2. Candidate preparation: object read/decode, contract checks, JIT mapping/copy,
   import thunks, relocation, protection/cache finalization and lookup.
3. Quiescent wait: time until the application finishes its existing batch.
4. Publication: select the fully prepared generation; separately count refused
   attempts. It is not source-to-usable-replacement latency.
5. Retirement/reclamation: deferred old-user lifetime, then mapping/metadata
   release. Do not hide overlapping code/object memory.
6. Ordinary execution: typed host-boundary calls and per-batch lease overhead,
   separately from compilation/replacement. Internal generated calls stay direct.

All elapsed costs above are **unmeasured**, not zero. The optional `--timings`
reports tiny diagnostic phase sums and timer overhead, not acceptance or a
compiler speedup. Preparation currently stops ordinary work on the one control
thread; no background-compilation responsiveness was established.
Full `test_all`, self-host fixed point, mode matrix, sanitizers, MSVC and other
platform gates were not run. This isolated host is outside production
registration; its passing checks do not mark those gates green.

Standard GitHub runners were unavailable for custom probe execution through the
exposed GitHub tools: repository reads/writes are available, but no workflow
dispatch or runner shell operation is callable. No existing workflow executes
this unregistered research directory, and no CI change or browser fallback was
made. Hosted correctness remains outstanding.

The dedicated Benchpress 9700X was unavailable in this session. No Tailscale or
authenticated Benchpress executor was available. The documented service accepts
a fixed reviewed recipe rather than arbitrary commands; its supplied service
units set `MemoryDenyWriteExecute=yes`. This is source evidence about that route,
not a live host-policy probe. No policy was changed to permit the pilot, and no
ad-hoc desktop/SSH performance run was substituted.

The strongest counterargument is that a correct stateless replacement mechanism
may have little practical value: compilation and object staging remain, the
quiescent wait may dominate, metadata integration is absent, and carefully
restricted functions could be cheap to execute in a restarted process. Retaining
host state demonstrates continuity, but no real workload has shown that the
benefit exceeds lifetime/ABI complexity. Versioned execution adds retirement
and synchronization cost before that value is established. Prefer quiescence;
stop rather than add indirection when a useful host cannot expose a safe batch
boundary or keep its state/import contract fixed.

## Prior research, licenses and ownership search

Bounded connected-GitHub searches covered open and closed issues/PRs for JIT,
hot reload/replacement/swap, livepatch, quiescence, versioned execution,
function-pointer dispatch and indirection. Exact title queries and explicit
closed-issue queries found no dedicated replacement owner or evidence-backed
replacement rejection. #1926 and all its current comments were read; its original
scope remains authoritative. A branch search for 1926 returned no branches.
The JIT/PLT32 issue search returned historical #1037; its older assumption that
the JIT already accepts PLT32 must not replace the current path analysis above.
This is bounded coverage, not proof of exhaustive absence. #1233 remains the
separate ELF unwind-binding correctness owner; this work does not duplicate it.

| Primary source | Applicable observation and limit | License verification |
|---|---|---|
| [Linux livepatch current documentation](https://docs.kernel.org/livepatch/livepatch.html) | Task-consistent transitions need safe points; stuck transitions can be indefinite; removal requires no users. Kernel ftrace/stack-inspection machinery is not proposed for this host. | Linux root [COPYING](https://github.com/torvalds/linux/blob/master/COPYING): GPL-2.0-only with Linux-syscall-note; component/file terms may differ. |
| [LLVM JITLink documentation](https://llvm.org/docs/JITLink.html) | Linking and debug/unwind/runtime registration are distinct; managed resources need failure/removal handling. No LLVM runtime dependency or implementation imported. | [LICENSE.TXT](https://github.com/llvm/llvm-project/blob/main/LICENSE.TXT): Apache-2.0 with LLVM exceptions. |
| [GDB unregister protocol](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Unregistering-Code.html) | Remove the entry, announce unregister and call the debugger hook before reclaiming registered code/records. Buster currently registers neither. | Official manual identifies GPL software and GFDL-1.3-or-later documentation. Exact current source-level software version/qualifier not independently verified; no GDB code copied. |
| [Microsoft supported C/C++ changes, updated 2026-05-01](https://learn.microsoft.com/en-us/visualstudio/debugger/supported-code-changes-cpp) | Existing hot reload also places explicit limits on global/static data and layout changes. These product limits are context, not Buster acceptance criteria. | API/product documentation only; no Microsoft implementation scanned. |
| [Kintsugi, USENIX Security 2025](https://www.usenix.org/system/files/usenixsecurity25-mackensen.pdf) | Separates preparation from application at a controlled scheduler boundary. Its Cortex-M4 RTOS/in-place design and retained-until-reboot slots do not establish Buster latency, state migration or reclamation. | Author artifact [Zenodo 16736378](https://zenodo.org/records/16736378): license unverified (empty metadata/no LICENSE or COPYING in scanned archive index); no code copied. |
| [Ginseng technical report](https://www.cs.ucr.edu/~neamtiu/pubs/DSU-TR.pdf) | Broad C updates require compiler transformations, analysis and function/type indirection; this is not assumed acceptable under Buster's dispatch rules. | Paper only; software license unverified and no code copied. |
| Buster at the pinned base | Existing components and constraints form the entire implementation. | No first-party grant selected; README and LICENSES/README.md explicitly retain unresolved #621. Third-party notices do not grant Buster's first-party code. |
| TinyCC pinned bootstrap | Tooling only, not a new dependency of the module or host. | Pinned `tcc.c`/`libtcc.c` headers: LGPL-2.0-or-later; bundled COPYING contains LGPL 2.1 terms. Retain this distinction rather than inventing a different project-wide grant. |

## Smallest useful continuation packet

Keep this research demonstrator and add one opt-in consumer at a declared host
batch boundary after coordinating with #1926. Reuse a compact owned module
handle; keep two stable generation slots, one fixed signature, one host-owned
state layout and permanent explicit host imports. No generic compiler dispatch
table, code patching, additional IR or phase-local worker threads.

First replay the existing probe on a standard GitHub Linux x86-64 runner with
exact source/binary/fixture identities. Add only missing boundary cases needed
for the real consumer: invalid source and object input, writable/lifecycle
refusals, and the actual facade's direct versus serialized object/import route.
Keep recoverable failure and release-retry semantics; do not advertise OOM or
executed-code fault recovery.

Then demonstrate a real host batch whose expensive retained data/resources make
process continuity useful. Predeclare hosted correctness and an authorized,
JIT-permitting 9700X measurement route without weakening existing service or
admission policy. Measure the six separate costs above, whole source-to-next
batch latency, ordinary execution overhead and peak retained memory against that
host's unchanged execution/restart baseline. No threshold is invented here.

Only reconsider versioned concurrency if a measured useful host cannot tolerate
quiescent waiting. Require a separately reviewed acquisition/reclamation proof,
state/import version consistency and metadata lifetimes. Otherwise retain this
narrow outcome and reject the added machinery.
