# Observed results: stable bootstrap, incorrect classification

**Confirmed defect: [#1352](https://github.com/buster14a/buster/issues/1352). No production repair is included.** The adjacent README is the frozen pre-execution design; its statement that results were unrun at creation is intentional. This file records the subsequent execution.

## What the experiment established

Buster's generic `__builtin_isfinite`, `__builtin_isinf`, and `__builtin_isinf_sign` narrow an x87 `long double` argument to binary64 before classification. Finite x87 values outside binary64's range are consequently classified as infinity.

Independently Clang- and GCC-produced Buster executables both pass the existing ordinary full-source bootstrap gate, including the required C1/C2 executable-byte fixed point. Every tested C0/C1/C2 nevertheless emits the same incorrect classification behavior. This is a shared frontend semantic defect preserved by self-hosting, not evidence that a host compiler miscompiled Buster or that the bootstrap compiler binary was corrupted.

## Immutable identities

Compiler source, separately checked out in two detached worktrees:

- Commit `ade6ac4b6ecb21f30b61b656439bac476c145e2f`.
- Tree `4c5306221fdb22fccc929b55e333163742de17d0`.
- `c_gen.c` blob `ede2de412850975123da3f4a473f017591655a75`.
- Both tracked-source diffs empty before and after execution. Main still resolved to this identity at publication.

Executed research transport commit `e355e19636edfd5bf053768f93108138b2928761`, tree `3727d58d5ff05845c52c84b22202bc151c289143`. The transport contains the probes and workflow; it is not the compiler source being tested. This results-only commit does not trigger another workflow.

[Hosted run 36205944737](https://github.com/buster14a/buster/actions/runs/36205944737), attempt 1; job `108302394497`. The run completed with a **failure** conclusion because both probe steps correctly returned nonzero for wrong answers. Setup, both ordinary bootstrap gates, evidence collection and artifact upload succeeded.

[Complete artifact 10893881334](https://github.com/buster14a/buster/actions/runs/36205944737/artifacts/10893881334): `bootstrap-classification-36205944737-1`, 73,598,983 ZIP bytes, SHA-256:

```text
01b7f215b966b0910b9525c16e33c23dce300c578a5dcf201efc304a3ff43bb7
```

Downloaded ZIP digest matches; all 583 manifest entries verified offline, 584 files including the manifest. GitHub reports expiration on 2026-10-26. The issue and branch retain the source, design, result summary and artifact identities beyond that date; they do not replace the complete raw ZIP.

## Executed matrix

Both C0 builds are unsanitized Debug, split-TU, on the same authorized hosted CPU, using `build.c` configuration and the unchanged required flags. Clang is `Ubuntu clang version 21.1.8 (6ubuntu1)`; GCC is `gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0`. The existing GCC combination lane is compile-only; this successful research execution does not change that lane's admission policy.

C0 is `build/Debug/ide`; C1/C2 are `build/self-host/Debug/ide-stage1` and `ide-stage2`, produced by the unchanged `test_self_host --config Debug` command. Subject configuration: Linux x86-64, O0, frontend SSA, codegen verification, FAST with fallback forbidden versus NONE without the inapplicable strict-MIR option.

| C0 producer | Subject compiler | FAST failed / checked | NONE failed / checked |
|---|---|---:|---:|
| Clang | C0 | 45 / 247 | 45 / 247 |
| Clang | C1 | 45 / 247 | 45 / 247 |
| Clang | C2 | 45 / 247 | 45 / 247 |
| GCC | C0 | 45 / 247 | 45 / 247 |
| GCC | C1 | 45 / 247 | 45 / 247 |
| GCC | C2 | 45 / 247 | 45 / 247 |

Each logical cell was run twice: 24 successful subject compiles and links, 24 observer executions returning 1 for semantic mismatches. The four independent reference profiles (Clang/GCC at O0/O2) were executed in each producer directory: eight reference executions, each passing 247/247. The observer is separately trusted-Clang-compiled and linked without LTO to every subject object.

Both harnesses report:

```text
reference_failures=0 behavioral_failures=12 setup_failures=0 repeat_failures=0
```

Both ordinary bootstrap commands and both retained `cmp C1 C2` checks return 0. No setup failure, timeout, reference failure or repeat discrepancy occurred. Cross-producer executable equality was not a required oracle. The distinct C0 binaries happen to converge to the same C1/C2 bytes.

## First divergence and regression family

The integer-image observer's first failed input is finite `+2^1024`: significand `8000000000000000`, exponent/sign `43ff`.

| Query / context | Expected | Buster, all cells |
|---|---:|---:|
| isfinite, pointer and by-value | 1 | 0 |
| isinf, pointer and by-value | 0 | 1 |
| isinf_sign, pointer and by-value | 0 | 1 |
| User-style finite-data guard | 37 | -1 |

Six finite images fail: positive/negative 2^1024, 2^4000 and x87 maximum. Seven observations each fail: three classifiers through pointer and by-value routes, plus the user guard. Three direct-literal functions also fail, for 45 failed checks total. The remaining 202 checks pass, including the byte-image finite control, original-format signbit, zeros, low normal/subnormal values, actual infinities, quiet NaNs, exact double maximum and float/double controls.

These are exact functions executed inside the frozen subject translation unit; an extracted standalone translation unit was not separately rerun:

```c
int literal_finite(void) { return __builtin_isfinite(0x1p4000L) != 0; }
int literal_infinite(void) { return __builtin_isinf(0x1p4000L) != 0; }
int literal_inf_sign(void) { return __builtin_isinf_sign(-0x1p4000L); }
```

Required and independently observed reference returns: **1, 0, 0**. All Buster returns: **0, 1, -1**.

The observer builds valid x87 images with integer fields and `memcpy`, checks little-endian/x87 ABI assumptions, and computes expected categories from those original integers. No Buster parser, folder, emitted comparison or floating narrowing supplies the oracle. Boolean result interpretation was specified before execution, not normalized after observing failures. Standards and independent representation references are linked in #1352.

## Causal localization and next-generation propagation

Pinned `c_gen.c` lines 15664-15770 insert the narrowing in `c_ir_emit_math_call` before classification. The retained actual canonical IR was decoded offline against the pinned explicit trace writer, consuming all 89,608 bytes and the end marker. In `classify_pointer`, instruction 7 is `IR_OPCODE_CAST` with `IR_CONVERSION_FLOAT_TRUNCATE`, from 80-bit long double value 4 to 64-bit double value 5. Instructions 20 and 33 carry the corresponding other two defective queries. The literal and user-guard functions contain the same narrowing.

Hosted disassembly confirms the first pointer path executes `fldt` followed by `fstpl` before comparison with binary64 infinity. The literal path first materializes the correct x87 bits for 2^4000, then narrows. The problem therefore is not an initially incorrect literal or merely a by-value ABI mismatch.

All 24 token and canonical-IR traces match byte for byte. Within each tested mode, MIR, objects, compile outputs/statuses and runtime outputs/statuses also match across producers, generations and repeats. FAST and NONE have different objects but the same wrong behavior. No stripping or textual normalization was applied.

Each subsequent compiler regenerates the same faulty source-language lowering rule. This does not establish that the wide-classification operation is exercised as data during compilation of Buster itself. Canonical validation properly accepts structurally valid IR; it cannot recover source semantics already replaced by the wrong conversion.

## Artifact identities

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Clang C0 | 37,787,048 | `77c822f0ad38057b0de62e19b69070a402a5ab872c96f4ed2f3c3e6dcc4756ef` |
| GCC C0 | 38,775,168 | `5c91677802f9c9314298e39d6d066a1065f5067557af5c0b93bffec6c2ce0a0b` |
| Each C1/C2 | 46,656,760 | `99ba5615e3b93dafb564eb0d8d17a15fa0cf79238f2888df8fdb9e9ed46df8b3` |
| Common canonical IR | 89,608 | `1eae84c9f2dd1e7b62edf23f16e6e54bece3b2e3c93e3be2c9aa7bcd5d80ea79` |

Raw environment: GitHub-hosted Ubuntu 26.04.1, image `20260920.143.1`, AMD EPYC 7763/four exposed logical CPUs; CMake 4.4.3, Ninja 1.13.2, GNU ld/objdump 2.46. The artifact preserves complete compiler command databases, configuration, exact small-command argv/stdout/stderr/status, stage binaries/metrics, phase traces and the executed corpus/workflow.

## Repair boundary and limits

Restore classification in the original semantic floating format. The x87-specific byte-image control is diagnostic, not a proposed production portability layer. Preserve all admitted formats, floating-environment semantics, canonical validation and lifetimes. No production repair is implemented or validated here.

One hosted run and two ordinary full-source chains were executed. The enhanced repeated C3 audit, full registered suite, sanitizers, other allocators/frontends, foreign targets, signaling NaNs, exception flags and alternate rounding modes were not run. No performance conclusion is drawn from the ordinary gate's retained benchmark lines.

Matching all-state builtin and PR searches found no dedicated duplicate; this is bounded, not exhaustive historical novelty. Only new research paths were written. No existing owner branch, production default, dependency, generated binding, persistent lane or excluded retirement/service/admission/deployment work changed. No merge occurred.

No callable subagent facility was available after discovery. One GPT-6 Astra Pro authored and reviewed this packet serially; independent compiler and integer-oracle evidence is not independent-agent authorship. The result confirms this shared defect, not the absence of others.
