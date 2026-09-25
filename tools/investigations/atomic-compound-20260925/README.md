# Atomic compound-assignment investigation — 2026-09-25

Diagnostic artifacts only. No production fix, integration change, protection change, merge, issue closure, performance measurement, or retirement prerequisite is included.

## Confirmed scope and revisions

The coherent investigation is the boundary between atomic object updates and the C arithmetic/assignment value conversions. Two different conversion points fail: the returned expression is not narrowed after some integer RMWs; mixed-type atomic RHS values are narrowed before their arithmetic. The latter overlaps existing #1137 and is evidence for that issue, not a new duplicate.

| Role | Compiler source commit | Tree |
|---|---|---|
| Original pinned main | `83c5f229288ecd8e23588c11b9ad19edc8bf4be2` | `b310e6573e6c89c618f8466941be4b995021a410` |
| Final freshly rebuilt and retested main | `8ab1e194026dbb7c5391b25e68292743f4b96efb` | `a98734a3c1de9448e563afd588a1996cf8bb8b69` |

Main advanced through #1152's VLA sizeof change. Six actual helper definitions compare byte-for-byte identical, but shared expression machinery changed, so the complete reduced family was executed again on the new source. `atomic-source-equivalence.json` and `main-reconciliation.diff` in the final artifact preserve that check. The owned branch is only an evidence transport; its head is not the compiler source revision.

## Durable inputs and observed outcomes

- [byte_add.c](byte_add.c): byte wraps to zero in memory, but `(value += 1)` returns 256 instead of zero.
- [float_precision.c](float_precision.c): atomic float minus an exact double operand produces zero instead of `-0x1p-24`.
- [observer.c](observer.c): prints actual stored and expression values; its exit zero is deliberately not a correctness assertion.
- [reduced-results.tsv](reduced-results.tsv): every result in the original fresh 160-cell reduction, exact input hashes, command IDs and profile order. The final-current-main replay has identical source hashes, profile/command IDs, compile exits, runtime exits and timeout flags for all 160 cells.
- [bounded generator](../atomic_compound_confirm.py) and [collector](../atomic_compound_probe.py): reconstruct all ten minimal/control inputs and exact profile families. The larger `atomic_compound_reduction.py` is a retained unsuccessful exploratory attempt, not the recommended replay.

The final replay compiled and ran every one of the 160 cells: no compile failures, no timeout, and no unrun cells. Each source expects exit zero. Bit 0 checks the expression value and bit 1 the stored value.

| Case | Clang O0/O2/sanitized O1 | GCC O0/O2/sanitized O1 | Buster: all ten profiles |
|---|---|---|---|
| Atomic unsigned byte 255 += 1 | 0 / 0 / 0 | 0 / 0 / 0 | 1 |
| Atomic unsigned byte 0 -= 1 | 0 / 0 / 0 | 0 / 0 / 0 | 1 |
| Atomic unsigned short 65535 += 1 | 0 / 0 / 0 | 0 / 0 / 0 | 1 |
| Prefix ++ atomic unsigned byte 255 | 0 / 0 / 0 | 0 / 0 / 0 | 1 |
| Postfix ++ byte, explicit result cast, nonwrapping byte, ordinary byte controls | 0 / 0 / 0 | 0 / 0 / 0 | 0 |
| Atomic float 1.0f -= 0x1.000001p0 | 0 / 0 / 0 | 0 / 0 / 0 | 3 |
| Atomic int -2 += 1.5 | 3 / 3 / 3 | 0 / 0 / 0 | 3 |

Buster profiles: `none`, `mir-stack`, `fast`, `quality` × `-ffrontend-ssa`/`-fno-frontend-ssa` at O0, plus FAST × both frontend forms at O2. Every Buster compile requests `-fverify-codegen`; all MIR profiles request `-fno-machine-fallback`. No check is disabled to obtain these results.

Final raw observer stdout, commands 327/329/331 respectively:

```text
Clang: byte=0 result=0 floating=-0x1p-24 result=-0x1p-24 integer=-1 result=-1
GCC:   byte=0 result=0 floating=-0x1p-24 result=-0x1p-24 integer=0 result=0
Buster: byte=0 result=256 floating=0x0p+0 result=0x0p+0 integer=-1 result=-1
```

The integer-fraction row is intentionally not called two-reference agreement. Expectations follow the compound-assignment arithmetic and final conversion rules, not compiler voting. The unsigned intermediates fit int and their final unsigned conversions are defined. Floating inputs/results are exact binary powers and the float-to-int example stays in range. There is no race, unsequenced conflicting access, or required arbitrary sibling evaluation order. Reference sanitizer silence is supporting evidence, not a proof of language validity.

Basis: [WG14 N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf), 6.3.1.3, 6.3.1.8, 6.5.3.1, 6.5.16 and 6.5.16.2, including the atomic RMW requirement and illustrative original-RHS-type expansion.

## Replay on a standard GitHub correctness runner

Use the [fresh-producer workflow at its executed transport commit](https://github.com/buster14a/buster/blob/3e26acef303a8cb738e93cab052db6b33d34edd4/.github/workflows/atomic-compound-fresh.yml). It checks out the exact production source, builds through build.c, tests startup, executes the bounded collector and preserves mismatches as failures. It neither dispatches the benchmark host nor changes a production branch.

Build and execute on the SAME runner: the documented Release configuration uses `-march=native`. Do not copy that compiler to a different CPU and assume portability. These commands show the tested bootstrap and representative minimal checks after checking out the exact production commit and copying the linked reproduction sources to a scratch directory:

```sh
export BUSTER_TEST_JOBS=2
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o /tmp/buster-atomic-build
/tmp/buster-atomic-build generate --cc clang --ci --linker DEFAULT
/tmp/buster-atomic-build build --config Release -t ide

build/Release/ide cc -std=c17 -fwrapv -fno-strict-aliasing -funsigned-char -g0 \
  -target x86_64-linux -O0 -fverify-codegen -fregister-allocator=fast \
  -ffrontend-ssa -fno-machine-fallback /tmp/byte_add.c -o /tmp/byte-buster
/tmp/byte-buster; printf 'Buster exit=%s\n' "$?"

clang --target=x86_64-linux-gnu -std=c17 -fwrapv -fno-strict-aliasing \
  -funsigned-char -g0 -O0 /tmp/byte_add.c -o /tmp/byte-clang -latomic
/tmp/byte-clang; printf 'Clang exit=%s\n' "$?"
gcc -m64 -std=c17 -fwrapv -fno-strict-aliasing -funsigned-char -g0 -O0 \
  /tmp/byte_add.c -o /tmp/byte-gcc -latomic
/tmp/byte-gcc; printf 'GCC exit=%s\n' "$?"
```

For the complete reduced replay, copy both Python files outside the detached source checkout, set a fresh `PROBE_OUT`, and execute `python3 atomic_compound_confirm.py` from the repository root after the same-host build. It returns nonzero for the documented discrepancies. Reference O1 sanitizers are `-fsanitize=address,undefined -fno-sanitize-recover=all`; their runtime library is placed AFTER the input/output arguments. `commands.jsonl` records the actual post-reordering argv; the descriptive `outcomes.flags` list retains its pre-reordering representation.

Producer and references: Ubuntu Clang **21.1.8 (6ubuntu1)** and GCC **15.2.0 (Ubuntu 15.2.0-16ubuntu1)**. All executions were standard GitHub-hosted Ubuntu 26.04 Linux x86-64. Host/CPU facts and build commands are retained. This is correctness execution, not performance evidence.

## Raw evidence ledger

Actions artifacts below have 30-day retention, expiring **2026-10-25**. The repository files above are the durable reduction, compact complete result matrix, exact commands/generator and evidence index. The full ZIPs additionally contain binaries, raw streams and original per-file SHA256SUMS. Downloaded ZIP digests and internal file hashes were checked. Do not describe the expiring full artifacts as permanent storage.

| Experiment | Run / artifact | ZIP SHA-256 | Classification |
|---|---|---|---|
| Original broad family | [36140294322 / 10865844012](https://github.com/buster14a/buster/actions/runs/36140294322/artifacts/10865844012) | `afeadb109b6efac0d0cdac3bbd7e9ce19f3f925fbd89b34c4ccdb32f7cfb054c` | 840 completed case/config records on original source; mixed successes, wrong results and rejected inputs. GCC runtime setup initially incomplete. |
| Oversized reduction attempt | [36140957069 / 10867301571](https://github.com/buster14a/buster/actions/runs/36140957069/artifacts/10867301571) | `2a2a5a82b48df087a67b4750fc64a60d4ac3f21ba090d1c698954969139b739c` | 34 completed records before the job budget; copied native producer emits SIGILL. Not frontend hang evidence. |
| Bounded copied-producer attempt | [36142240007 / 10867930598](https://github.com/buster14a/buster/actions/runs/36142240007/artifacts/10867930598) | `8c2dd9a21864d1f579953f3e5daa9395bb9f82cabbae29762f4ebcc36874d64b` | 79 records, 81 explicitly unrun; SIGILL makes Buster semantic results unusable. Corrected reference library order. |
| Fresh original-source reductions | [36142767360 / 10866943297](https://github.com/buster14a/buster/actions/runs/36142767360/artifacts/10866943297) | `d41110bc31b13b0f2cf0c6d538bf0ea8edcf530712c6dc6b6c0d85281967121f` | Valid same-host build, startup controls and all 160 reduction cells; 865 internal file hashes verified. |
| Failed source-equivalence extraction | [36143415357 / 10869140443](https://github.com/buster14a/buster/actions/runs/36143415357/artifacts/10869140443) | `56ac4490acb5ea317021d6f49cd5be4248f42447cc70b46b20fe6b02a39576f4` | Collector matched a forward declaration rather than a definition; stopped before build/tests. Corrected, not waived. |
| Final current-main fresh replay | [36144035466 / 10867848037](https://github.com/buster14a/buster/actions/runs/36144035466/artifacts/10867848037) | `edc803d56aa3c6f76e0f8641227b32b048c300853cee95fc0bd69bdc5b4d0d36` | Exact 8ab1e194 source, all six definitions equal, startup controls pass, all 160 cells completed; 868 internal hashes verified. |

The first campaign had Buster 320 passes / 88 wrong-result executions / 72 compile rejections; Clang 156 passes / 24 wrong results; GCC 135 passes / 45 link failures. All 30 ordinary-source controls passed all 14 profiles. The initial GCC floating-atomic link failures need `-latomic`, and the next attempt placed that library too early for the linker. Neither setup failure is reported as invalid C. The complete fresh reduced campaigns have zero link failures. Their per-compiler totals are Buster 40 pass / 60 wrong-result, Clang 27 pass / 3 integer-fraction disagreements, and GCC 30 pass.

Exact compiler SHA-256 values:

```text
Original broad:  4a8e23e3dc4887df711b7a7fd96f4923de3307ee6c920490ccaf2e95a4602924
Fresh original:  bffe495cd07b4841a42d7c3aa137fae88ac31d2edb7983e59e01a3547e985dfa
Fresh current:   66e56a94fc02ba9bbc0e3e0b0e276bda7d617ddb9a20c60899807ed9a87ea696
```

Final artifact raw-file hashes:

```text
outcomes.jsonl: 1f488f95a6513a530e5953ff141eb0603ef8548459e92bea7e8978cc4ebddb1f
commands.jsonl: bf1a46ca6ce556df90756670ec1d6d689b3d5c9f96280156552679f13e2b70c5
results.tsv: 874a23df7d2eb25e38043ad33629d5b734bdffcc0ccea816c2607fecdbea3be1
atomic-source-equivalence.json: 1532aff59d567a58921025811e5a1eaf08654e6fd2eca5fe899e0932836f49b3
```

## Investigation boundaries and rejected explanations

The result-width trigger needs neither mixed RHS precision, volatile qualification, automatic-object initialization nor a side-effecting RHS: the minimal static C17 object suffices. Same-width atomic updates, explicit result narrowing, discarded byte-wrap result, non-atomic wrap and postfix old-value controls distinguish the missing final conversion. The original function-produced RHS counter remains exactly one in the byte-wrap family. The failures recur with independent fresh builds and all tested allocators/frontends; agreement among shared-frontend Buster modes is not used as the language oracle.

Bounded issue/PR searches used actual symbols `c_ir_emit_compound_assignment`, `c_ir_emit_increment`, `c_ir_emit_atomic_float_update` and atomic compound/wrap/narrow/prefix symptoms. #1137/#1139, #361/#370 and likely atomic-builtin/aggregate reports were inspected. Prefix diagnostics inside unevaluated sizeof (#271) are a different invariant. No matching report of the post-RMW narrow expression result was found; exhaustive historical novelty is not claimed. The mixed RHS conversion overlaps #1137 and must be recorded there.

Not tested: full registered suite, ordinary self-host, whole mode_matrix suite, native AArch64/Windows/macOS execution, LLVM-output observations, contention/thread interleavings, floating exception environments, signed narrow-overflow cases, or performance. The larger cancelled campaign never reached its planned LLVM/AArch64 observations. Multiplication/division and Boolean atomic compile rejections in the initial screen are untriaged leads, not additional confirmed issues or manifestations assigned to the result-width root. Source inspection and the newest applicable audit do not establish a measured slowdown. No production repair has been implemented or proven.
