# Buster vs Cuik

Research record: [#1941](https://github.com/buster14a/buster/issues/1941).

Reproducible capture: [evidence archive](Buster-vs-Cuik-evidence.zip). The archive retains shared inputs, independent validators, raw samples, summaries, build commands/logs, hashes and disassembly. It contains no executable binaries or object files. Capture paths are redacted to CAPTURE_ROOT.

Current source inspection · actual correctness-gated measurements · 29 September 2026

## Verdict and scope

**Buster wins measured time to a valid artifact on the larger test. Cuik wins measured generated-code size and the constant-division runtime probe.** Buster has the stronger inspected target, AVX-512, debugging and validation infrastructure. Cuik has the more capable native optimizing middle end and more convenient library integration. There is no justified universal speed or correctness winner.

Inspected on 29 September 2026: Buster `e642c1e32e40568c168d36b0f281c76e4c939139`; Cuik `a7677f74e5391dcc7fdedad61468f4f207c9998f`. Both source trees were built unmodified. Buster's sole active frontend is C; its former custom language is dormant. Cuik describes itself as unfinished. This report compares implemented compiler paths, not the size of Buster's whole monorepo, target enums, historical marketing claims or unrelated Clang benchmarks.

**Performance scope:** AMD EPYC 9V74 cloud KVM VM, nine exposed logical CPUs; eleven retained observations per cell after a warmup, alternating order. These are diagnostic measurements, without a confidence interval or dedicated-host admission. They are not measurements on your Ryzen 7 9700X.

## Actual measurements

| Valid optimized workload | Buster FAST | Cuik optimized | Scoped winner / improvement |
| --- | --- | --- | --- |
| Compile 192,679 bytes / 1,024 synthetic functions | 52.580 ms | 104.966 ms | **Buster**: 1.996× speed; 49.91% less time |
| Peak RSS, same compilation | 48,020 KiB | 54,336 KiB | **Buster**: 11.62% less RSS |
| Code-section bytes, same corpus | 344,053 B | 65,472 B | **Cuik**: 80.97% smaller; 5.255× size ratio |
| Compile 450-byte / four-function probe | 15.877 ms | 12.710 ms | **Cuik**: 1.249× speed; 19.94% less time |
| Peak RSS, small probe | 25,288 KiB | 14,588 KiB | **Cuik**: 42.31% less RSS |
| Code-section bytes, small probe | 600 B | 144 B | **Cuik**: 76.00% smaller; 4.167× size ratio |
| Run 20 million external unsigned /10 calls | 51.661 ms | 33.833 ms | **Cuik**: 1.527× speed; 34.51% less time |

The larger source is header-free unsigned arithmetic, branches and rotates. The small source contains unsigned division by ten, a static helper in a loop, and an array sum. Time includes process creation, frontend, native code generation and object writing; linking is excluded. Generated targets are baseline Linux x86-64/System V, debug output disabled, serial compilation. The runtime harness times the loop inside the process, excluding process launch and correctness preflight.

Buster FAST and QUALITY and Cuik optimized passed **51,200 independent synthetic checks per mode**. The separate probe validator checked eight division boundary cases, 10,000 pseudorandom division inputs, 10,000 helper-loop inputs and sum lengths 0–257. Runtime checksums match. This validates these inputs and modes, not all language or ABI behavior.

**Producer-build caveat:** both compilers were produced with Clang 20.1.2, but their normal project build configurations differ: Buster uses `-O3 -march=native` and includes tests in its compiler executable; Cuik uses `-O2 -march=haswell`. Therefore the timing comparison is between these supported project builds, not binaries built with identical optimization flags. Buster's timed driver used `-O2 -fregister-allocator=fast -fno-machine-fallback`; Cuik used boolean `-O`. Buster's default bounded canonical passes run at every native optimization level. Matching an optimization flag's spelling would not match optimization effort.

Observed min–max compile times: Buster FAST 46.265–80.903 ms versus Cuik 90.815–123.729 ms on the larger corpus; 14.600–16.756 ms versus 12.332–15.239 ms on the tiny probe. This illustrates the noise; median ratios are descriptive, not a population-wide statistical guarantee. No system-header application, self-host corpus, parallel scaling, cold-cache series or PMU comparison was run.

## QUALITY does not close the middle-end gap

| Observation | Buster FAST | Buster QUALITY | Cuik optimized |
| --- | --- | --- | --- |
| Synthetic compilation | 52.580 ms | 54.553 ms | 104.966 ms |
| Synthetic code section | 344,053 B | 344,053 B | 65,472 B |
| Small-probe code section | 600 B | 570 B | 144 B |
| Division microkernel | 51.661 ms | 50.947 ms | 33.833 ms |

QUALITY is an allocator/scheduling choice, not an inliner or general optimizing middle end. It reduces probe bytes by 5%, but does not shrink this synthetic corpus. Its small runtime difference from FAST is not established as statistically significant. Cuik is still 74.74% smaller than QUALITY on the probe. Switching Buster's default allocator would not reproduce Cuik's constant division rewrite or inlining.

## Why Cuik produces better code here

Disassembly confirms a concrete difference for `unsigned long long divide10(unsigned long long x) { return x / 10; }`. Cuik emits multiplication by `0xcccccccccccccccd`, takes the high product and shifts right by three. Buster emits hardware `DIV` and retains frame/setup/store work. Cuik also eliminates the separate static helper by inlining it into the loop; Buster retains a call. These are observed emitted-code differences, not conclusions from optimizer names.

Cuik's measured 1.527× runtime advantage covers the complete external-call microkernel, including the LCG, checksum, calls and function prologues. It cannot be attributed entirely to reciprocal division. The 5.255× synthetic code-size ratio also includes allocator, instruction-selection, frame and optimization differences. It is not a prediction that any single Buster patch will recover 81%.

Sources: [Cuik constant-divisor rewrite](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/opt/peep_int.h#L815-L967), [Cuik IPO/inliner](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/opt/ipo.h), [Buster bounded rewrites](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/ir/ir_fast.c), [Buster division selection](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/codegen/machine_x86_64.c#L3158-L3175). Exact disassembly is included in the evidence package.

## Comparison across compiler dimensions

| Aspect | Buster at pinned revision | Cuik at pinned revision | Winner and limit |
| --- | --- | --- | --- |
| Primary design goal | Low latency / compiler throughput, conservative bounded work | Fast toolchain with a substantially optimizing TB core | Different priorities; neither goal itself is a win |
| Active source language | C; custom-language fixtures dormant | C; experimental Go parser is not a supported CLI compiler | Tie for active C scope |
| Standards / dialects | GNU89–GNU23, C99/C11/C17/C23 options; implemented subsets | C11/C23 options; partial implementations | Buster breadth; complete conformance unmeasured |
| Specific C/GNU features | Complex real floating types, GNU statement expressions, extended asm, vectors, atomics and TLS within target limits | Complex/imaginary and GNU statement expressions rejected; several other features implemented partially | Buster inspected feature coverage |
| Preprocessor coverage | Active diagnostics/directive handling and include caching | Include guards/pragma once; #error/#warning dispatcher entries commented out; #embed disabled | Buster inspected directive coverage |
| Frontend representation | Compact tokens and indexed token ranges; typed semantic facts; direct SSA lowering | Postfix expression stream with separate type/cast arrays; shared interning | No universal winner; both contain compact designs |
| Compiler-internal SIMD | AVX-512 lexer/compaction paths plus scalar/SWAR fallbacks | Generated DFA and SSE-assisted keyword comparisons | Buster wider mechanisms; speed must be measured |
| Native optimizer | Default fold/address/DCE/trivial-parameter pipeline and promotion | GVN, peepholes, memory promotion/SROA, CProp, loop transforms, strength reduction, IPO/inlining | Cuik transformation repertoire |
| Constant division | Variable numerator / constant denominator retains division in tested probe | Reciprocal high multiply + shift in tested probe | Cuik; 34.51% less full microkernel time here |
| Inlining | No ordinary native inliner in inspected path | IPO inliner active with -O; helper disappeared in probe | Cuik capability and this emitted result |
| Automatic vectorization | No comparable native SLP/loop vectorizer in inspected pipeline | SLP is invoked; usable widths depend on target/operation support | Cuik available transformation; no measured SIMD gain |
| Explicit AVX-512 output | EVEX, ZMM register file, opmask placement and feature-gated operations | AVX-512 enum/table entries do not reach corresponding examined emission | Buster implemented capability |
| CPU/feature tuning | -march=znver5 / -mcpu / ordered -mattr controls | C driver hard-codes x86_64-v1 features | Buster accessible target tuning |
| Register allocation | FAST default; separate QUALITY weighted pins/regions plus placement | Rogers allocator in C driver; Briggs alternate library path | No runtime winner from algorithm names |
| Scheduling | QUALITY pressure-focused local scheduling; accept after placement-cost comparison | Global scheduling and generic latency-driven local scheduler | Cuik breadth; Buster explicit pressure acceptance |
| Zen 5 resource model | Inspected scheduler does not model exact Zen 5 execution resources | Generic latency constants, no verified Zen 5 resource model | Neither |
| Native platform coverage | x86-64/AArch64 across ELF/COFF/Mach-O, target-specific ABI/unwind work | x64 Linux/Windows ordinary CI; other backend sources need individual qualification | Buster inspected implementation / test surface |
| Firmware | x86-64/AArch64 UEFI image/boot test paths | No comparable verified path established | Buster inspected path |
| Non-native outputs | Direct Wasm64, bounded WASI/eBPF paths; LLVM bitcode writer | Wasm/AArch64/MIPS backend source presence exceeds ordinary build/validation coverage | Buster inspected implementation/test surface; not blanket target correctness |
| LLVM relationship | Dependency-free binary bitcode export, no integrated LLVM optimizer/backend | Own TB optimizer/backend | Different; external LLVM bridge is Buster capability |
| GPU | Orchestrates external tools; not native C-to-GPU implementation | No equivalent supported direct compiler established | No direct-GPU winner |
| Object/link integration | Own object, assembler/linker and native image paths; current x64 ELF shared/PIE support | Own linker API and optional path; default GNU toolchain delegates linking to Clang | Buster target coverage; speed unmeasured |
| Debugging | DWARF, CodeView/PDB and target unwind facilities with documented boundaries | CodeView/SDG paths; CLI -g selects CodeView; no active DWARF exporter found | Buster inspected cross-platform facilities |
| JIT | Generic host-native object-loader API and driver path; relocation/TLS/platform limits apply | TB JIT APIs implemented; C CLI run path disabled | Buster CLI convenience; both backend APIs exist |
| Library packaging | Reusable per-phase APIs; no equivalent minimal standalone compiler SDK target identified | libCuik and TB independently buildable, shared-library options | Cuik packaging |
| Virtual inputs / embedding | Phase APIs; no equivalent general in-memory include-overlay facility identified | Preprocessor VFS locate/get hooks and AST/token inspection | Cuik integration convenience |
| API stability | No production-stability claim established | Explicitly unstable; some header/implementation names drift | No stable SDK winner |
| Human diagnostics | Structured messages/locations/notes, simpler renderer | Excerpts/carets, macro backtrace and fixit hints | Cuik presentation |
| Machine diagnostics | Stable codes, deep-copied records, structured backend/fallback evidence | Diagnostic callback/buffering, less equivalent structured backend evidence identified | Buster inspected evidence structure |
| Parallel scheduling | Persistent lanes; native link TU cohorts only; -c and per-function work serial | Work-stealing/build DAG; IRgen and IPO tasks; codegen batches of 10,000 functions | Cuik broader task scope; scalability unmeasured |
| Determinism/self-host evidence | Byte-identical fixed point, stronger multigeneration audit and strict mode matrix implemented | Differential/unit test sources; no comparable mandatory fixed-point gate established | Buster implemented evidence infrastructure |
| Ordinary CI execution | Broad registered platform/test/self-host contracts | Windows/Linux build jobs; TB unit job restricted to refs/heads/aaa; no ordinary frontend test execution | Buster coverage design; current all-green not claimed |
| Robustness on measured corpus | FAST and QUALITY validated | Optimized validated; unoptimized failed | Buster tested-mode robustness; general defect rate unknown |
| ELF stack contract | Missing .note.GNU-stack in measured objects | Same missing note in measured objects | Shared weakness |
| Build dependencies | TCC/native build.c, CMake/Ninja; documented hosted Clang exception | Clang/GCC, LuaJIT generators, Ninja/NASM, NBHM/mimalloc submodules | No measured build-speed or simplicity winner |
| Reuse/licensing | No selected first-party license; separate Arm-derived metadata review unresolved | MIT first-party license; submodule terms remain separate | Cuik clear published reuse grant |
| Compiler executable size / project maintainability | Monorepo with tests/generated metadata in executable configuration | Smaller toolchain scope; separate libraries | Unmeasured binary comparison; whole-repo LOC is unfair |
| Application performance / general memory use | Only the small shared corpus measured | Only the small shared corpus measured | No general winner |

## Architecture lessons worth transferring

Buster already has SSA, compact token storage, public phase APIs and SIMD. Recommending that it add these would miss the current implementation. Its C parser records declarations and body/token ranges rather than a complete expression tree; downstream consumers re-walk ranges. Cuik keeps compact postfix expression records, then uses a pointer-linked sea-of-nodes backend with input/use lists. This makes aggressive rewrites convenient, but does not prove lower frontend time or memory.

Buster's recorded layouts include 12-byte C tokens, 16-byte canonical values, 64-byte canonical instruction rows and 24-byte MIR instruction rows on the 64-bit host. A TB node's core size excludes its operand/user/extra arrays, so comparing one node size against one Buster row would be misleading. Keep allocation/retained-byte and traversal counts for equal useful work instead.

Cuik's stronger optimizer is especially relevant to Buster's self-hosted compiler: code quality can reduce the cost of every later compile. The measurements here used a Clang-built Buster executable; they do not measure that self-host feedback loop. Reproduce the same self-host source, final output and fixed point before claiming an improvement there.

Both parallel designs require an explicit request. Buster's cohorts are only consecutive native C inputs in a link invocation and default to one worker. Cuik's IRgen batches contain 50 top-level statements, but its current codegen batches contain 10,000 functions; even -j does not create many codegen tasks for an ordinary small module. Borrow task ownership and phase parallelism ideas, not that fixed granularity or unbounded scheduling overhead.

## Where Buster loses: ranked remedies

| Priority / change | What Cuik does better / proposed Buster approach | How much is established | Acceptance / existing owner |
| --- | --- | --- | --- |
| 1. Constant-divisor lowering | Use Cuik’s exact unsigned reciprocal-multiply/shift method as a capability reference; add bounded canonical/selector rewrites with signedness and overflow rules. Start with /10, /16, %10 and %16. | Cuik took 34.51% less time in the measured external /10 microkernel. A Buster patch’s gain is unknown until measured. | DIV removal; differential boundary/random checks; compile wall/RSS; emitted bytes; repeat on 9700X. |
| 2. Small-leaf inlining | Inline nonrecursive static leaf functions under call-site and code-growth budgets, retaining canonical IR and deterministic source records. Honor address-taking and visibility. | Cuik removed the probe helper call; Buster retained it. No separate helper runtime gain was measured. | Call count and text; compiler wall/RSS; self-host fixed point. Existing [issue #48](https://github.com/buster14a/buster/issues/48). |
| 3. Bounded local value numbering and branch cleanup | Publish use/known-bit/effect facts once; remove redundant arithmetic, self-moves and trivial branches; consider leaf-frame reduction after unnecessary spills disappear. Preserve volatile/atomic/trap/FP behavior. | Cuik code section was 80.97% smaller on the larger corpus. This is the whole-pipeline gap, not a promised recovery from one pass. | Rows/branches/spills/frame bytes and actual total compile cost. [Issue #49](https://github.com/buster14a/buster/issues/49) is the existing local-pipeline owner. |
| 4. Give optimization levels actual pipeline meaning | Keep the low-latency default; add a separately budgeted optimizing tier for inlining, sparse propagation, loop strength reduction and later LICM/SLP. | Cuik’s optimized build took approximately twice Buster’s compile time on this corpus while producing much smaller code. The tradeoff deserves an explicit policy. | Paired compile/text/runtime measurements; requested code-quality tier. [Issue #47](https://github.com/buster14a/buster/issues/47). |
| 5. Use the existing AVX-512 backend with cautious SLP | Pack proven independent, same-shape operations with contiguous memory and valid alias/FP semantics. Begin fixed shapes and scalar tails. | Cuik has SLP; Buster has the stronger explicit 512-bit emitter. No vector speedup was measured here. | Accepted scalar semantics, no invalid widening, feature-off fallback, compile overhead and end-to-end workload. |
| 6. Improve diagnostic presentation | Keep structured codes and existing notes; add source excerpts, caret/range rendering, indexed macro provenance and fixit edits. Allocate on cold error paths. | Cuik has these user-facing features; benefit is feature parity, not a throughput percentage. | Stable text/machine records, invalid-input ordering and expansion ownership. |
| 7. Package a small compiler SDK and virtual inputs | Expose tested static/shared compiler targets and a minimal ownership/lifetime API. Implement in-memory include overlays as direct indexed source tables to respect Buster’s no-callback model. | Cuik makes phase/AST/VFS integration easier, but its unstable API should not be copied as a stability model. | Compile/link public API samples, symbol/export checks, retained arena lifetimes and incremental input replacement. |
| 8. Broaden concurrency only after ownership is explicit | Evaluate -c TU batches and per-function work after signature, source-cursor and inline-assembly ownership are independent. Use persistent lanes and ordered publication. | Cuik schedules more phases, but its 10,000-function codegen grain prevents a simple scalability crown. | N=1/2/4/8 on balanced and skewed real TUs; wall/RSS and deterministic diagnostics. [Issue #54](https://github.com/buster14a/buster/issues/54). |
| 9. Lower the tiny-input startup floor | Profile x86 metadata decode/prewarm separately; investigate immutable minimal production tables or a long-lived compiler service. Avoid paying exhaustive assembler metadata costs for a tiny C object. | Cuik compiled the 450-byte probe 19.94% faster and used 42.31% less RSS. Phase attribution was not measured. | Cold process versus warm in-process, page faults, source/binary size and full target correctness. |
| 10. Resolve the distribution contract | Choose the first-party license explicitly; resolve or replace restricted derived metadata while preserving third-party terms. | Cuik has an MIT grant; Buster’s own license decision is still open. This is an adoption gap, not compiler speed. | Maintainer decision and attribution/provenance review in [issue #621](https://github.com/buster14a/buster/issues/621). |

Do not promise the measured Cuik/Buster ratio as the result of adopting one optimization. Cuik differs simultaneously in optimization, instruction selection, allocation and frames. Preserve Buster’s throughput mode and admit each code-quality feature against compile-time/RSS budgets. More passes or a more sophisticated allocator are costs as well as capabilities.

## Correctness and object-format findings

**Cuik unoptimized output is excluded.** Its 1,024-function synthetic output failed the independent reference validator immediately. Inspection shows argument stores at positive stack offsets without corresponding frame allocation in the tested SysV function, and the optimized reference harness showed caller-variable corruption. The optimized Cuik output passed the same checks. This establishes a failure for that revision, input and mode; it is not an aggregate conformance rate or proof that every unoptimized Cuik function fails.

**Both tested ELF object emitters omit `.note.GNU-stack`.** Ordinary host GNU linking warned and produced an executable GNU_STACK segment. Buster’s existing [issue #1237](https://github.com/buster14a/buster/issues/1237) already tracks the stack-executability contract. Correct object notes and propagation of input stack policy are the remedy; neither compiler wins this dimension. The measurement phase modified neither compiler source tree and did not publish upstream Cuik issues. This branch publishes documentation and evidence only.

Buster’s implemented self-host, native mode and ABI gates are a stronger validation structure. Cuik’s ordinary CI primarily builds binaries and gates TB unit tests to a special branch. Neither that infrastructure inventory nor millions of assertions establishes a numerical general correctness percentage. This session did not run Buster’s full suite or exact-head self-host gate.

## Primary source index

These pinned sources were inspected locally from the repositories; all links refer to the exact compared revisions. Capability claims are source inspection unless the measured-results sections explicitly say otherwise.

- [Buster current compiler scope](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/docs/projects/compiler.md)
- [Buster driver, target modes and native TU lanes](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/docs/agents/driver.md)
- [Buster C frontend ownership/SSA](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/docs/agents/frontend/foundations.md)
- [Buster bounded canonical FAST contract](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/docs/canonical-fast-pipeline.md)
- [Buster FAST allocator](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/codegen/register_allocator_fast.c)
- [Buster QUALITY allocator](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/codegen/register_allocator_quality.c)
- [Buster scheduling](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/codegen/machine_schedule.c)
- [Buster AVX-512 emission/register files](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/codegen/machine_x86_64.c)
- [Buster predicate allocator](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/codegen/register_allocator_predicate.c)
- [Buster LLVM bitcode boundary](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/LLVM_BITCODE.md)
- [Buster reusable phase API](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/frontend/c/c.h)
- [Buster structured diagnostics](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/diagnostic.h)
- [Buster renderer](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/src/buster/lib/compiler/diagnostic.c)
- [Buster build/self-host contracts](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/docs/agents/build.md)
- [Buster validation/CI scope](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/docs/agents/testing.md)
- [Buster first-party license boundary](https://github.com/buster14a/buster/blob/e642c1e32e40568c168d36b0f281c76e4c939139/THIRD_PARTY_NOTICES.md)
- [Cuik modular frontend/backend description](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/README.md)
- [Cuik optimizer actual pass execution](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/opt/optimizer.c#L1413-L1675)
- [Cuik IPO/inlining](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/opt/ipo.h)
- [Cuik division rewrites](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/opt/peep_int.h#L815-L967)
- [Cuik SLP implementation](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/opt/slp.h)
- [Cuik Rogers allocator](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/rogers_ra.c)
- [Cuik target scheduler and vector widths](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/x64/x64_target.c)
- [Cuik x86 emitter](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/x64/x64_emitter.h)
- [Cuik C driver optimizer/target/JIT choices](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/cuik_c/driver/driver.c)
- [Cuik parallel batch sizes](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/cuik_c/driver/driver_sched.h)
- [Cuik preprocessor directives](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/cuik_pp/cpp.c)
- [Cuik diagnostics/macro notes/fixits](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/cuik_pp/diagnostic.c)
- [Cuik VFS and lex APIs](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/include/cuik_lex.h)
- [Cuik postfix expression layout](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/include/cuik_ast.h#L711-L746)
- [Cuik public build DAG](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/include/cuik_driver.h)
- [Cuik debug format dispatch](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/exporter.c)
- [Cuik Darwin toolchain boundary](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/cuik_c/toolchains/darwin.c)
- [Cuik C frontend limitations](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/cuik_c/decl_parser.h)
- [Cuik GNU statement-expression boundary](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/cuik_c/expr_parser.h)
- [Cuik ordinary CI](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/.github/workflows/ci.yml)
- [Cuik build requirements and flags](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/build.lua)
- [Cuik MIT license](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/LICENSE.txt)

## Measurement records and reproduction

The companion evidence archive contains the exact shared C inputs, independent validators, eleven-sample raw compile/runtime records, summary JSON, source/compiler hashes, dependency pins, compiler build logs/commands and emitted disassembly. It intentionally omits executable binaries and object files; their identities and captured code are retained. Compiler builds must be reproduced before running the included scripts, whose paths refer to this capture’s directory layout.

Percent time saved means `100 × (1 − T_winner/T_loser)`. Speed factor means `T_loser/T_winner`. Size reduction uses the same formula with code-section bytes. These are different from percentage throughput increase. Failure/unsupported output has no valid speed score.

### manifest.json

```text
{
  "buster_revision": "e642c1e32e40568c168d36b0f281c76e4c939139",
  "cuik_revision": "a7677f74e5391dcc7fdedad61468f4f207c9998f",
  "machine": "AMD EPYC 9V74 KVM hosted cloud, 9 exposed CPUs; not Zen5",
  "producer": "Ubuntu Clang 20.1.2",
  "binary_hashes": {
    "buster-measurement-build/Release/ide": "24eed0c2e28f821676eae4f903ffa753d4fdddc33152b06558e7659da44865c9",
    "cuik/bin/cuik": "2dbe53dace8783ebaff310a28920d2b8262c524d8adb8b76248a4a2102a8ea80"
  },
  "sources": {
    "probes.c": {
      "sha256": "63c0ed02b3b01f5c2fc80029bb6b982eac78b64554c0344fd53fe368b7d7043d",
      "bytes": 450
    },
    "synthetic-1024.c": {
      "sha256": "e600e48813b90d948219f7b43bc78674d2ada668487ca887e626d26b6b317670",
      "bytes": 192679
    },
    "probe-validator.c": {
      "sha256": "048c0e4c30bfa0e2603883554c28efb112beb6426c4176a7c256673512b1b040",
      "bytes": 1517
    },
    "synthetic-validator.c": {
      "sha256": "bcb7212d38578d2361506373d9fa2fc345a8ab4f55f93019c87437231ccf789b",
      "bytes": 82280
    },
    "measure.py": {
      "sha256": "860f2be25219bfd58d94a3f0f0ec754c58d014f39182782e33641ad0705040a3",
      "bytes": 2657
    },
    "runtime-measure.py": {
      "sha256": "3a86ab18b6a8a99ec6d965b5756a06869a36ca7ca9a4b989ccd1777a4cd1f87a",
      "bytes": 1096
    }
  },
  "dependencies": {
    "NBHM": "2a58f06f887a6c7c44adab98815d3c72b655f2e3",
    "mimalloc": "f2712f4a8f038a7fb4df2790f4c3b7e3ed9e219b",
    "LuaJIT": "2.1.0+git20231223.c525bcb+dfsg-1ubuntu0.1",
    "NASM": "2.16.01-1build1",
    "LLD": "20.1.2-0ubuntu1~24.04.3"
  },
  "validation": {
    "BusterFAST": "51200 synthetic checks passed; probe validator passed",
    "BusterQUALITY": "51200 synthetic checks passed; probe validator passed",
    "CuikOPT": "51200 synthetic checks passed; probe validator passed",
    "CuikUNOPT": "failed fn_0/input0 using O0 trusted validator; O2 validator caller variables corrupted; exclude from valid performance comparisons"
  }
}
```

### compile-summary.json

```text
[
  {
    "source": "probes.c",
    "mode": "buster-fast",
    "samples": 11,
    "median_wall_ns": 15876892,
    "min_wall_ns": 14600373,
    "max_wall_ns": 16756340,
    "median_rss_kib": 25288,
    "object_bytes": 3336,
    "text_bytes": 600
  },
  {
    "source": "probes.c",
    "mode": "buster-quality",
    "samples": 11,
    "median_wall_ns": 15453141,
    "min_wall_ns": 14978514,
    "max_wall_ns": 17590740,
    "median_rss_kib": 25244,
    "object_bytes": 3304,
    "text_bytes": 570
  },
  {
    "source": "probes.c",
    "mode": "cuik-unoptimized",
    "samples": 11,
    "median_wall_ns": 12351937,
    "min_wall_ns": 12080016,
    "max_wall_ns": 13421823,
    "median_rss_kib": 14588,
    "object_bytes": 913,
    "text_bytes": 256
  },
  {
    "source": "probes.c",
    "mode": "cuik-optimized",
    "samples": 11,
    "median_wall_ns": 12710308,
    "min_wall_ns": 12331866,
    "max_wall_ns": 15239143,
    "median_rss_kib": 14588,
    "object_bytes": 739,
    "text_bytes": 144
  },
  {
    "source": "synthetic-1024.c",
    "mode": "buster-fast",
    "samples": 11,
    "median_wall_ns": 52579697,
    "min_wall_ns": 46264996,
    "max_wall_ns": 80902931,
    "median_rss_kib": 48020,
    "object_bytes": 435336,
    "text_bytes": 344053
  },
  {
    "source": "synthetic-1024.c",
    "mode": "buster-quality",
    "samples": 11,
    "median_wall_ns": 54552569,
    "min_wall_ns": 47337879,
    "max_wall_ns": 75923254,
    "median_rss_kib": 47764,
    "object_bytes": 435336,
    "text_bytes": 344053
  },
  {
    "source": "synthetic-1024.c",
    "mode": "cuik-unoptimized",
    "samples": 11,
    "median_wall_ns": 74121816,
    "min_wall_ns": 68277847,
    "max_wall_ns": 103024547,
    "median_rss_kib": 65128,
    "object_bytes": 194779,
    "text_bytes": 131072
  },
  {
    "source": "synthetic-1024.c",
    "mode": "cuik-optimized",
    "samples": 11,
    "median_wall_ns": 104966112,
    "min_wall_ns": 90814584,
    "max_wall_ns": 123729091,
    "median_rss_kib": 54336,
    "object_bytes": 129179,
    "text_bytes": 65472
  }
]
```

### division-runtime-summary.json

```text
[
  {
    "mode": "buster-fast",
    "samples": 11,
    "median_runtime_ns": 51660761,
    "min_runtime_ns": 49941832,
    "max_runtime_ns": 61390525,
    "checksum": 5880082817230044566
  },
  {
    "mode": "buster-quality",
    "samples": 11,
    "median_runtime_ns": 50946802,
    "min_runtime_ns": 50049885,
    "max_runtime_ns": 58285466,
    "checksum": 5880082817230044566
  },
  {
    "mode": "cuik-optimized",
    "samples": 11,
    "median_runtime_ns": 33833089,
    "min_runtime_ns": 31766915,
    "max_runtime_ns": 38940080,
    "checksum": 5880082817230044566
  }
]
```

### measure.py

```text
import pathlib,subprocess,os,json,time,hashlib,statistics,platform
P=pathlib.Path(__file__).resolve().parent
R=P.parent
base=[str(R/'buster-measurement-build/Release/ide'),'cc','-g0','-march=baseline','-target','x86_64-unknown-linux','-fno-machine-fallback','-c']
cuik=[str(R/'cuik/bin/cuik'),'-c','-j1','-lang','c11','-target','x64_linux_gnu']
modes={'buster-fast':base+['-O2','-fregister-allocator=fast'],'buster-quality':base+['-O2','-fregister-allocator=quality'],'cuik-unoptimized':cuik,'cuik-optimized':cuik+['-O']}
rows=[]
for source in ('probes.c','synthetic-1024.c'):
 for sample in range(-1,11):
  ordered=list(modes);ordered=ordered if sample%2==0 else list(reversed(ordered))
  for name in ordered:
   output=P/(name+'-'+source+'.o')
   cmd=modes[name]+[str(P/source),'-o',str(output)]
   with (P/(name+'-'+source+'.stdout')).open('wb') as stdout,(P/(name+'-'+source+'.stderr')).open('wb') as stderr:
    start=time.perf_counter_ns(); process=subprocess.Popen(cmd,stdout=stdout,stderr=stderr,cwd=R)
    _,status,usage=os.wait4(process.pid,0);elapsed=time.perf_counter_ns()-start;process.returncode=os.waitstatus_to_exitcode(status)
   row={'source':source,'sample':sample,'mode':name,'wall_ns':elapsed,'peak_rss_kib':usage.ru_maxrss,'user_seconds':usage.ru_utime,'system_seconds':usage.ru_stime,'exit':process.returncode,'command':cmd}
   rows.append(row)
   if process.returncode: print(json.dumps(row),flush=True);raise SystemExit('Compilation failed '+str(P/(name+'-'+source+'.stderr')))
   if sample==0:
    row['output_sha256']=hashlib.sha256(output.read_bytes()).hexdigest();row['object_bytes']=output.stat().st_size
    section=subprocess.check_output(['size','-A',str(output)],text=True)
    row['text_bytes']=int(next(line.split()[1] for line in section.splitlines() if line.strip().startswith('.text ')))
    (P/(name+'-'+source+'.disassembly.txt')).write_text(subprocess.check_output(['objdump','-dr',str(output)],text=True))
  (P/'compile-samples.json').write_text(json.dumps(rows,indent=2))
summary=[]
for source in ('probes.c','synthetic-1024.c'):
 for name in modes:
  family=[x for x in rows if x['source']==source and x['mode']==name and x['sample']>=0]
  summary.append({'source':source,'mode':name,'samples':len(family),'median_wall_ns':statistics.median(x['wall_ns'] for x in family),'min_wall_ns':min(x['wall_ns'] for x in family),'max_wall_ns':max(x['wall_ns'] for x in family),'median_rss_kib':statistics.median(x['peak_rss_kib'] for x in family),**{k:family[0][k] for k in ('object_bytes','text_bytes')}})
(P/'compile-summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2),flush=True)
```

### runtime-measure.py

```text
import pathlib,subprocess,json,statistics
P=pathlib.Path(__file__).resolve().parent
modes=['buster-fast','buster-quality','cuik-optimized']
rows=[]
for sample in range(-1,11):
 for name in (modes if sample%2==0 else list(reversed(modes))):
  cmd=[str(P/(name+'-probe-runtime')),'20000000']
  process=subprocess.run(cmd,capture_output=True,text=True,check=True)
  ns,checksum=map(int,process.stdout.split())
  rows.append({'sample':sample,'mode':name,'runtime_ns':ns,'checksum':checksum,'command':cmd})
assert len(set(x['checksum'] for x in rows))==1
(P/'division-runtime-samples.json').write_text(json.dumps(rows,indent=2))
summary=[]
for name in modes:
 family=[x for x in rows if x['mode']==name and x['sample']>=0]
 summary.append({'mode':name,'samples':len(family),'median_runtime_ns':statistics.median(x['runtime_ns'] for x in family),'min_runtime_ns':min(x['runtime_ns'] for x in family),'max_runtime_ns':max(x['runtime_ns'] for x in family),'checksum':family[0]['checksum']})
(P/'division-runtime-summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
```

### probes.c

```text
unsigned long long divide10(unsigned long long x) { return x / 10; }
static unsigned long long helper(unsigned long long x) { return x * 3 + 7; }
unsigned long long helper_loop(unsigned long long x, unsigned int n) { for (unsigned int i = 0; i < n; i++) { x = helper(x); } return x; }
unsigned long long sum_loop(const unsigned int *p, unsigned int n) { unsigned long long sum = 0; for (unsigned int i = 0; i < n; i++) { sum += p[i]; } return sum; }
```
