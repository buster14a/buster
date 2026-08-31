# Forgejo closed-issue archive

The 116 issues that were already closed when buster moved to GitHub on
2026-08-31. They were not copied into the GitHub tracker — they are finished
work, and copying them would renumber them a second time — but the code and the
commit history reference them by number, so their text is kept here verbatim.
Open issues are indexed in `issue-migration-map.md`.

---

## forgejo#17 — Leak inside libFuzzer in Alpine

Closed 2026-06-01. Original: https://code.buster14a.com/buster/buster/issues/17

Upstream: https://gitlab.alpinelinux.org/alpine/aports/-/issues/18201
```
alpine:~$ cat main.c
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}

alpine:~$ ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=verbosity=1 ./main -max_len=4096 -max_total_time=2
==26809==AddressSanitizer: failed to intercept '__b64_ntop'
==26809==AddressSanitizer: failed to intercept '__b64_pton'
==26809==AddressSanitizer: failed to intercept '__dn_comp'
==26809==AddressSanitizer: failed to intercept '__dn_expand'
==26809==AddressSanitizer: failed to intercept 'pthread_mutexattr_getprioceiling'
==26809==AddressSanitizer: failed to intercept 'pthread_mutexattr_getrobust_np'
==26809==AddressSanitizer: failed to intercept '__cxa_rethrow_primary_exception'
==26809==AddressSanitizer: libc interceptors initialized
|| `[0x10007fff8000, 0x7fffffffffff]` || HighMem    ||
|| `[0x02008fff7000, 0x10007fff7fff]` || HighShadow ||
|| `[0x00008fff7000, 0x02008fff6fff]` || ShadowGap  ||
|| `[0x00007fff8000, 0x00008fff6fff]` || LowShadow  ||
|| `[0x000000000000, 0x00007fff7fff]` || LowMem     ||
MemToShadow(shadow): 0x00008fff7000 0x000091ff6dff 0x004091ff6e00 0x02008fff6fff
redzone=16
max_redzone=2048
quarantine_size_mb=256M
thread_local_quarantine_size_kb=1024K
malloc_context_size=30
SHADOW_SCALE: 3
SHADOW_GRANULARITY: 8
SHADOW_OFFSET: 0x00007fff8000
==26809==Installed the sigaction for signal 11
==26809==Installed the sigaction for signal 7
==26809==Installed the sigaction for signal 8
==26809==T0: FakeStack created: 0x7b89426e7000 -- 0x7b89431f0000 stack_size_log: 20; mmapped 11300K, noreserve=0, true_start: 0x7b89426e7000, start of first frame: 0x7b89426f0000
==26809==T0: stack [0x7ffcac0aa000,0x7ffcac8aa000) size 0x800000; local=0x7ffcac8a8b24
==26809==AddressSanitizer Init done
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: 1447500228
INFO: Loaded 1 modules   (1 inline 8-bit counters): 1 [0x555d1b0b7b40, 0x555d1b0b7b41),
INFO: Loaded 1 PC tables (1 PCs): 1 [0x555d1b0b7b48,0x555d1b0b7b58),
INFO: A corpus is not provided, starting from an empty corpus
==26809==T1: FakeStack created: 0x7b893fbed000 -- 0x7b893feb0000 stack_size_log: 18; mmapped 2828K, noreserve=0, true_start: 0x7b893fbe7000, start of first frame: 0x7b893fbf0000
==26809==T1: stack [0x7f89440fa000,0x7f894411aa90) size 0x20a90; local=0x7f894411a954
#2	INITED cov: 1 ft: 1 corp: 1/1b exec/s: 0 rss: 40Mb
#2097152	pulse  cov: 1 ft: 1 corp: 1/1b lim: 4096 exec/s: 1048576 rss: 197Mb
#2286082	DONE   cov: 1 ft: 1 corp: 1/1b lim: 4096 exec/s: 762027 rss: 212Mb
Done 2286082 runs in 3 second(s)
==26809==LeakSanitizer: checking for leaks
==26812==SuspendAllThreads retry: 0

=================================================================
==26809==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 8 byte(s) in 1 object(s) allocated from:
    #0 0x555d1b00ed80 in malloc /home/buildozer/aports/main/llvm-runtimes/src/llvm-project-22.1.3.src/compiler-rt/lib/asan/asan_malloc_linux.cpp:67:3
    #1 0x555d1b053188 in operator new(unsigned long) cxa_noexception.cpp
    #2 0x555d1aeee17d in fuzzer::FuzzerDriver(int*, char***, int (*)(unsigned char const*, unsigned long)) /home/buildozer/aports/main/llvm-runtimes/src/llvm-project-22.1.3.src/compiler-rt/lib/fuzzer/FuzzerDriver.cpp:832:3
    #3 0x555d1af1c663 in main /home/buildozer/aports/main/llvm-runtimes/src/llvm-project-22.1.3.src/compiler-rt/lib/fuzzer/FuzzerMain.cpp:20:10
    #4 0x7f89441fd193  (/lib/ld-musl-x86_64.so.1+0x42193) (BuildId: 2b26dfbb1a8172e32ed88052349fd6c997e6aa79)
    #5 0x7ffcac8a9e48  (<unknown module>)

Indirect leak of 48 byte(s) in 1 object(s) allocated from:
    #0 0x555d1b00ed80 in malloc /home/buildozer/aports/main/llvm-runtimes/src/llvm-project-22.1.3.src/compiler-rt/lib/asan/asan_malloc_linux.cpp:67:3
    #1 0x555d1b053188 in operator new(unsigned long) cxa_noexception.cpp
    #2 0x555d1aeee17d in fuzzer::FuzzerDriver(int*, char***, int (*)(unsigned char const*, unsigned long)) /home/buildozer/aports/main/llvm-runtimes/src/llvm-project-22.1.3.src/compiler-rt/lib/fuzzer/FuzzerDriver.cpp:832:3
    #3 0x555d1af1c663 in main /home/buildozer/aports/main/llvm-runtimes/src/llvm-project-22.1.3.src/compiler-rt/lib/fuzzer/FuzzerMain.cpp:20:10
    #4 0x7f89441fd193  (/lib/ld-musl-x86_64.so.1+0x42193) (BuildId: 2b26dfbb1a8172e32ed88052349fd6c997e6aa79)
    #5 0x7ffcac8a9e48  (<unknown module>)

SUMMARY: AddressSanitizer: 56 byte(s) leaked in 2 allocation(s).
MS: 5 ShuffleBytes-ChangeByte-CMP-ChangeBit-CrossOver- DE: "\001\000\000\000\000\000\000\000"-; base unit: adc83b19e793491b1c6ea0fd8b46cd9f32e592fc


artifact_prefix='./'; Test unit written to ./crash-da39a3ee5e6b4b0d3255bfef95601890afd80709
Base64:
```

---

## forgejo#29 — Suport cosmopolitan

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/29

_(no description)_

---

## forgejo#30 — Support fil-c

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/30

_(no description)_

---

## forgejo#68 — Solve forgejo speed issue

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/68

https://codeberg.org/forgejo/forgejo/issues/12443
https://codeberg.org/forgejo/forgejo/pulls/12457

---

## forgejo#537 — c frontend: fix the conditional-type prediction that miscompiles mask64_prefix

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/537

**Task for an agent.** Read `AGENTS.md` first; it holds the house rules this change has to satisfy (one return per function, orientation headers, no unverified comments).

## What is wrong

`ide cc` miscompiles a conditional whose arms are a 64-bit constant and a 64-bit shift expression. Five-line repro, no SIMD involved:

```c
volatile u32 opaque; opaque = 32; u32 lane = opaque;
u64 v = lane >= 64 ? ~(u64)0 : (((u64)1 << lane) - 1);
/* clang: 0x00000000ffffffff   ide cc: 0xffffffffffffffff */
```

That is `mask64_prefix` from `src/buster/lib/simd.h` expanded, so **every `mask64_prefix(n)` with a runtime `n` in [32,64) returns `~0` in self-hosted stages**.

## Cause (already diagnosed, verify before fixing)

From `ide cc -emit-llvm`: the conditional's result slot is allocated **i32**. Both arms are computed correctly in i64, then `trunc`ed into the slot and `sext`ed back on reload. The slot is sized by the *predicted* type — `c_ir_predict_expression_type_attempt` → `c_ir_query_conditional_type` (`src/buster/lib/compiler/frontend/c/c_gen.c:1906`, with `c_ir_conditional_result_type_attempt` at `c_gen.c:19437`) — which returns invalid here and falls back to `builder->s32_type`, not by the resolved type.

It needs **both** arms to be what they are: `cond ? ~(u64)0 : x` alone is fine, `cond ? y : ((u64)1 << lane) - 1` alone is fine, the shift outside a conditional is fine. All four allocator modes (`fast`, `quality`, `mir-stack`, `-fno-register-allocator`) fail identically, so this is upstream of codegen. The same prediction machinery has produced wrong answers before (see the sizeof/array-bound history in `PERFORMANCE_AUDITS.md`).

## Blast radius

Self-hosted stages only — clang builds the compiler correctly; this is about what `ide cc` *emits*. The live caller is `ir_source_search_vector` (`src/buster/lib/compiler/ir/ir.c:92-93`): `mask64_prefix(lanes * 4)` covers [32,64) for 8..15 lanes, so the wrong mask over-reads the loaded vector. The extra lanes are masked out of `covered` immediately afterwards, so it is latent — but it is a real over-read.

Coverage missed it because `simd_test.c` and `tests/basic_c_simd.c` only ask for `mask64_prefix(0)`, `(1)` and `(64)`, all compile-time constants.

## Definition of done

1. The prediction returns the correct 64-bit type for this shape (fix the prediction, do not special-case `mask64_prefix`).
2. The five-line case is registered as a driver fixture under `tests/`.
3. `simd_test.c` and `tests/basic_c_simd.c` gain a **runtime-count sweep** over `mask64_prefix` (every n in 0..64 through a volatile), not just constants.
4. `c_parse.c:11490`'s workaround macro `c_parse_census_lanes_below` is reverted to a plain `mask64_prefix` call (it exists only to dodge this bug).
5. Four-mode self-host reaches a byte-identical stage 2, and `test_all_combinations_ci` is green.

---

## forgejo#538 — parser: write a kind|punctuator sidecar from the preprocessor's token emitter

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/538

**Task for an agent.** Performance work: read the newest entry of `PERFORMANCE_AUDITS.md` for the reference numbers before measuring anything, and record the result as a new audit file under `docs/performance-audits/` (mint the id with `tools/new_audit.py`).

## The finding

The parser-side SIMD census (audit `2026-08-22T220912Z`, PR 532) found the layout fact underneath every ranked parser lever: **`CToken` is a 12-byte AoS row, and every parser pass reads exactly two of those bytes** — `kind` and `punctuator`, at offsets 10 and 11 — to ask what shape a token is. That is four full strided walks of the token stream per parse, each touching 12 bytes to use 2.

`lcm(12,64) = 192`, so today's kernels must gather fields with fixed `vpermt2b` index vectors across three chunks per sixteen tokens (see `c_parse_token_census` in `src/buster/lib/compiler/frontend/c/c_parse.c` for the working example).

## The task

Have the **preprocessor's token emitter** write a parallel `kind|punctuator` sidecar array as it fills the token rows, and switch the parser's shape queries to read it. This turns all four strided parser walks into contiguous byte scans, and lets the census kernel drop its projection step entirely.

Constraints and prior art:

- The window emitter already writes the final 12-byte `CToken` row format directly into the preprocessor's final slots (see the preprocessor token-stream work, PR 500) — the sidecar has to be written on that same path, in the same private-arena scheme, without adding a second pass over the stream.
- The reference (non-compact) lexer path must produce an identical sidecar; the differential gate compares the two implementations and is the thing that proves the change.
- Budget the memory: one byte per token against ~570 K identifiers-plus-everything-else per stage-1 compile. Say in the audit what the sidecar costs in bytes and in the emitter's instruction count, not just what it saves.

## Definition of done

- Sidecar written by the emitter, consumed by `c_parse_token_census`, and by the other three shape walks it can serve.
- Outputs byte-identical, self-host fixed point holds, `-march=x86-64` clean.
- Pinned A/B pairs (11 is the house standard) with per-run instruction and branch deltas, recorded in a new audit; report the emitter-side cost separately from the parser-side win.

---

## forgejo#539 — parser: classify c_parse_position_index_build from the token-census tiles

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/539

**Task for an agent.** Performance work: read the newest `PERFORMANCE_AUDITS.md` entry first, record a new audit under `docs/performance-audits/` at the end.

## The lead

From the parser-side SIMD census (audit `2026-08-22T220912Z`, PR 532), ranked by *addressable* ceiling, this is lead #2 and the largest one still open:

> `c_parse_position_index_build` classification — 70.8 M instructions total, **43.6 M addressable**. The 16.0 M serial stack and the 11.2 M ordered appends stay.

Lead #1 (the token census in `c_analyze_semantics`) is already taken and landed −44.2 M instructions; its kernel is the template to follow.

## The task

Feed `c_parse_position_index_build`'s classification from the **same 2,048-token tiles** the census kernel builds, instead of re-walking the stream per position. The census projects `kind`/`punctuator` out of the 12-byte AoS rows with a fixed pair of `vpermt2b` index vectors (`lcm(12,64) = 192`, sixteen tokens span three chunks), then answers each class with one `vpcmpeqb` + `popcnt` over 64 tokens. Reuse that projection rather than repeating it.

**Do not retry as scoped:** the "compare in place" form — no projection, constant lane masks per 192-byte phase — was measured at 5.6 instructions/token against 1.0 for project-then-count. A 64-byte chunk holds only 5.33 tokens, so every class compare wastes 12x its lanes.

Note the interaction: if the `kind|punctuator` sidecar task lands first, the projection disappears and this kernel gets simpler. Either order works; check which is already merged before starting.

## Definition of done

- Classification served from the shared tiles; the serial stack and ordered appends left alone (they are not addressable).
- Outputs byte-identical, self-host fixed point holds.
- 9+ pinned A/B pairs with instruction and branch deltas in a new audit; state what fraction of the 43.6 M was actually recovered.

---

## forgejo#540 — aarch64: pass and return bare __int128 in AAPCS64 register pairs

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/540

**Task for an agent.** AArch64 backend work. Read `AGENTS.md` ("Machine instruction selection and scheduling", "Platform and backend boundaries") first.

## What is missing

Bare `__int128` **parameters and returns** are still rejected by the AArch64 shape gate. A struct-wrapped `__int128` selects fine; the bare form does not. This was probed directly — do **not** trust the census here, because `ZIG_NO_INT128_SHIFTS` (defined for `__aarch64__` in `tests/c_abi.h`) gates the c_abi suite's bare-i128 pair out of the aarch64 run entirely, despite that gate's comment claiming 128-bit passing and returning "stay tested". The census reports signature class 0 for i128 and is wrong.

PR 519 already lowered i128 **bodies** in the canonical AArch64 emitter (pair convention x9:x10 left, x11:x12 right, x13/x14 scratch, low eightbyte first; helpers `c_a64_load_high` / `c_a64_store_high`). This task is the signature half.

## The task

1. Model AAPCS64's rule that a 128-bit integer takes an **even-numbered X register pair** — the current placement does not model register-pair alignment at all. Cover arguments, returns, the register/stack boundary, and the padding rule when the pair does not fit.
2. Decide what to do about the `ZIG_NO_INT128_SHIFTS` gate now that its shift half is deleted: either lift it (and make the suite's bare-i128 pair run on aarch64) or narrow it to what is genuinely unsupported, with a comment that matches reality.
3. `machine_a64_type_is_scalar_register` bounds operands to 8 bytes, so i128-touching functions fall back whole to the canonical emitter and both pipelines share the fixed code. Keep that arrangement unless you can show it is the thing blocking you.

## How to validate — oracle against clang, not against canonical

Canonical is not always right. Build hand-written probes and measure them against clang:

```
clang -target aarch64-linux-gnu -nostdlib -static -fuse-ld=lld
```

with a `_start` plus an `svc #0` exit, run under `qemu-aarch64`. Cross the value space at every half boundary, constant and variable, all twelve comparisons, casts from every narrow width, and run each shape through all four pipelines (`none`, `mir-stack`, `fast`, `quality`). Same-compiler c_abi runs cannot see an ABI divergence — that is exactly how two wrong-answer bugs survived the census and the qemu differential earlier.

## Definition of done

- Bare i128 parameters and returns select and produce clang-identical results under qemu in all four modes.
- A registered fixture under `tests/` that fails before the change.
- The `tests/c_abi.h` gate reflects what is actually supported.
- `test_all_combinations_ci` green (it compiles the live tree — do not edit `src/` while it runs).

---

## forgejo#541 — aarch64: implement i128 divide/remainder, clz/ctz and float conversions

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/541

**Task for an agent.** AArch64 backend work, follow-on to PR 519.

## What is missing

On AArch64 these 128-bit operations are still **diagnosed** (`CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION`), not implemented:

- `__int128` / `unsigned __int128` divide and remainder
- `clz` / `ctz` on 128-bit values
- float ↔ i128 conversions in both directions

The x86-64 canonical emitter already has the long-division loop; the intended shape of this work is to **port it**, not to invent one.

## Context you need

PR 519 gave the canonical AArch64 emitter its i128 block: branchless CSEL shift composites (`mvn` spells 63−amount so amount 0 stays exact; LSLV/LSRV/ASRV's mod-64 wrap only feeds CSEL-discarded lanes), adds/adc, subs/sbc, umulh/madd multiply, per-half bitwise, eor/orr/cset equality, cmp+sbcs ordered compares (LESS_EQUAL/GREATER swap operands — sbcs leaves Z reflecting the high subtraction alone), sign/zero-filled casts, and constants' second immediate. Pair convention: x9:x10 left, x11:x12 right, x13/x14 scratch, low eightbyte first. Helpers `c_a64_load_high` / `c_a64_store_high`.

The a64 machine selector needs no change: `machine_a64_type_is_scalar_register` bounds operands to 8 bytes and every 128-bit producer refuses selection, so i128-touching functions fall back whole to the canonical emitter and both pipelines share the fixed code.

## How to validate

Generate a clang-differential probe: values × amounts at every half boundary, constant **and** variable amounts, carries, all twelve comparisons, casts from every narrow width. Compile with `ide cc` for both targets on all four pipelines. Reference is **`zig cc -fwrapv`** — plain `zig cc` traps buster's `-fwrapv` wraps as UBSan.

Every i128 op silently used its 64-bit row on the low half before PR 519, and the same-compiler c_abi suite only caught shifts, by luck. Assume anything unproven is wrong.

## Definition of done

- div/rem, clz/ctz and float↔i128 all lower on a64, matching the reference across the probe.
- Registered fixtures under `tests/`, failing before the change.
- All four allocator modes agree; `test_all_combinations_ci` green.

---

## forgejo#542 — aarch64: resolve the union-f64 HFA classification divergence against clang

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/542

**Task for an agent.** ABI bug, AArch64. Small, sharply scoped, needs a cross-compiler harness.

## The bug

Cross-compiler `c_abi` pairing against clang fails at **"union f64" in both directions** on AArch64 — buster-compiled caller / clang-compiled callee and the reverse. It is pre-existing, and it is a **union-HFA classification divergence**: AAPCS64's rules for when a union counts as a homogeneous floating-point aggregate are not what the implementation does.

Same-compiler runs cannot see it: both sides agree with each other and disagree with clang. That is why the census and the qemu differential are both clean here.

## Where to work

- `tests/c_abi_*` (the ported Zig `test/c_abi` suite, PR 516) has the shapes.
- Classification lives on the canonical AArch64 path in `src/buster/lib/compiler/codegen/`; HFA/vector overflow closes the V file while integer overflow leaves the X file open — that asymmetry is deliberate and documented, do not "fix" it on the way past.

## How to validate

Oracle against **clang**, not against buster's canonical emitter:

```
clang -target aarch64-linux-gnu -nostdlib -static -fuse-ld=lld
```

`_start` plus `svc #0` exit, run under `qemu-aarch64`. For the c_abi lane, compile the three TUs together with a local no-op `write()` stub TU, or the link goes dynamic and qemu has no aarch64 interpreter.

Build the cross pairing explicitly: one side clang, the other `ide cc`, then swap. Cover unions of one, two, three and four doubles, unions mixing float and double, unions mixing a double with an integer, and each of those nested one level inside a struct.

## Definition of done

- Cross-compiler c_abi pairing passes "union f64" in both directions.
- Whatever rule was wrong is written down as a comment where the classification happens (a constraint the code cannot show).
- A registered fixture that fails first, in both pairing directions.

---

## forgejo#543 — win64: get the c_abi executable running under wine

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/543

**Task for an agent.** Win64 ABI work. Read `AGENTS.md` ("Platform and backend boundaries") and the orientation headers of `object.c` and `link.c` — those headers, not any summary, are the authority on the linker's COMDAT/weak model.

## Where this stands

Linking `tests/c_abi_main.c` + `tests/c_abi_main_generated.c` (clang, COFF) against a **buster-compiled** `tests/c_abi_cfuncs.c` now produces an image, after PR 527 gave the linker a replaceable-definition rule for `IMAGE_COMDAT_SELECT_ANY` constant pools (`__real@...`, `__xmm@...`).

Two things are left:

1. **`_fltused` is undefined.** Nothing in the tree defines it for a linked *user* program — only `src/buster/lib/entry_point.c` has one, for buster's own binaries. Today you have to hand it a one-line `int _fltused = 1;` translation unit to get an image. Decide where it belongs and make the c_abi Win64 lane build without a hand-written stub.

2. **The image faults under wine**, inside `zig_panic`'s deliberate `*(volatile int*)0 = 1` — i.e. a c_abi check failed, not a crash. **`lld-link` on the exact same objects faults identically**, which is the check that isolates it: the remaining failure is a **Win64 ABI mismatch in buster-compiled `c_abi_cfuncs.c`**, not a linker defect. Re-run that check first to confirm it still holds, then find the mismatching shape.

## Method

Use the wine differential harness: cross-compile the fixtures to Win64 and run them under wine (`-target` must precede `-march` on the command line). The harness is trustworthy again since the vector-return fixes, so **a wine lane mismatch is a buster bug**, not harness noise.

Bisect the failing shape by splitting `c_abi_cfuncs.c` — the suite is per-shape by construction, so compile one half with clang and one with `ide cc` and narrow. Recent Win64 ABI work landed >W vector legalization (pieces/straddle/direct-or-sret+bounce) and clang's shapes for single-lane / sub-8-byte vectors and `__int128`; the remaining fault is most likely a shape adjacent to those, so check them against clang's own output first.

Watch for the trap that already cost a session: `RSI` is **callee-saved** under Win64, and the `__int128` vocabulary had been using it freely.

## Definition of done

- The c_abi Win64 executable builds without a hand-written `_fltused` TU and runs clean under wine with `c_abi_cfuncs.c` compiled by `ide cc`.
- The mismatching ABI shape gets a registered fixture that fails first.
- `test_all_combinations_ci` green.

---

## forgejo#544 — codegen: alias single-chunk machine streams instead of flattening them

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/544

**Task for an agent.** Performance work, from the copy cluster in audit `2026-08-22T195419Z` (PR 521). Read the newest `PERFORMANCE_AUDITS.md` entry for current reference numbers before measuring; record a new audit at the end.

## The lead

`machine_stream_flatten` (`src/buster/lib/compiler/codegen/machine.c:1381`) is **3.17% of DRAM samples** — the largest single member of the ranked copy cluster. It copies each builder stream's chunks into one flat array.

The observation that makes it addressable: **chunks and flattened arrays share one arena**, and `MACHINE_BUILDER_CHUNK_BYTES` is 16 KB = 682 instruction rows, so **most functions are single-chunk**. A single-chunk stream could alias its chunk storage as the final array instead of copying it.

The rest of the cluster, for context and possible follow-on: `link_objects` (`link.c:717`) 3.21%, `link_native_executable_elf64` (`link.c:2148`) 2.47%, `c_source_map_append` 1.65%.

## Constraints

- Aliasing means the chunk must not be reused or reset while the flattened array is live. Establish that from the arena's lifetime rules, and write the invariant down as a comment where the aliasing happens.
- `MACHINE_BUILDER_CHUNK_BYTES` is a shared constant that a capacity computation and the emitters both depend on — if the fix ties more code to its value, that dependency belongs in a named constant, not a respelled literal.
- Measure how often single-chunk actually holds on stage 1 before writing the fix; the whole lead rests on that fraction, and it is cheap to count.
- A DRAM-sample ranking predicts which fix pays **badly** — that has bitten this codebase before. Instructions first: take the change if instructions drop, and treat a miss-count improvement without an instruction win as unproven.

## Definition of done

- Single-chunk streams alias; multi-chunk streams keep flattening.
- Outputs byte-identical, self-host fixed point holds.
- 11 pinned A/B pairs, instruction/branch/L1d/DRAM deltas, new audit file plus its index line.

---

## forgejo#545 — codegen: skip the template-cache memset with a dirty watermark

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/545

**Task for an agent.** Performance work, small and self-contained. Lead (2) from audit `2026-08-22T195419Z` (PR 521).

## The lead

`codegen_generate_canonical_module`'s **template-cache memset** (`src/buster/lib/compiler/codegen/codegen.c`, near the x64 metadata/template cache — the cache entry type is `CodegenX64TemplateCacheEntry`, lookups go through `codegen_canonical_x64_template_entry`) shows a **12.63x cycles-to-instructions ratio**: it is almost pure memory traffic.

The suspicion to test: **the arena is fresh there**, so most of what the memset clears was never dirtied. A dirty-watermark skip — track the highest slot actually written and clear only up to it, or skip entirely on a fresh arena — should remove most of the traffic.

## Constraints

- Confirm the "arena is fresh" claim in the code before relying on it; a comment that has not been verified against the code does not get written down.
- A never-evicting cache is worse than none — that lesson is already recorded from the canonical x86 metadata emission series. If the watermark work tempts you into changing the cache's eviction behaviour, that is a different change and needs its own measurement.
- Instructions first. This one may show up as cycles and misses with a flat instruction count; say so plainly in the audit rather than quoting only the flattering counter.

## Definition of done

- Watermark (or equivalent skip) in place, with the invariant that makes it sound written next to it.
- Outputs byte-identical, self-host fixed point holds.
- Pinned A/B pairs with cycles, L1d and DRAM deltas alongside instructions; new audit file plus index line.

---

## forgejo#546 — ci: gate -fregister-allocator=none in the self-host test

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/546

**Task for an agent.** CI/gating work. Small, and it closes a real hole.

## The hole

`test_self_host` does **not** exercise `-fregister-allocator=none`. The self-host comparison in `build.c` runs the default and `mir-stack` (`self_host_compile_add` / `self_host_compare_action`, the `-fregister-allocator=mir-stack` argument around `build.c:3174`), so the **canonical emitter — the reference every other mode is checked against — can rot with no gate noticing.**

It already did. On 2026-08-24 the canonical emitter was found to have never captured incoming argument registers at entry: it read each parameter's argument register at its `IR_OPCODE_ARGUMENT` instruction, which is only sound if the entry block is a contiguous run of parameter homes. A parameter written with **array syntax** breaks that — the C frontend emits the bound's element-count × element-size multiply between the homes, and that multiply's scratch is RAX and **RCX**, the fourth System V argument. The defect reproduced as far back as `45f8c133` (2026-07-30), when canonical was the *only* emitter, and went unnoticed for a month because no function in `ide.c` had the triggering shape until `d3311023` (2026-08-18).

The by-hand recipe that would have caught it — all four modes must reach the same byte-identical stage 2:

```
for m in none mir-stack fast quality; do
  build/Release/ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 \
    -DBUSTER_INCLUDE_TESTS=0 -fregister-allocator=$m \
    src/buster/apps/ide/ide.c -o build/s1-$m
  ./build/s1-$m cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 \
    -DBUSTER_INCLUDE_TESTS=0 src/buster/apps/ide/ide.c -o build/s2-$m
  sha256sum build/s2-$m
done
```

## The task

Wire that four-mode chain into the self-host gate so `none` and `quality` are covered, not just default and `mir-stack`.

Judgement to make and to justify in the change: a full four-mode self-host is not free, and CI wall time is already gated by the 4-CPU Windows runner. Options worth pricing before picking one — run all four only on the fastest Linux lane, run `none` on a reduced translation unit, or run the extra modes on a schedule rather than per push. Whatever you choose, **`none` must be gated somewhere that fails a PR**, not only nightly, and the reasoning belongs in `AGENTS.md`'s Tests section.

## Definition of done

- `-fregister-allocator=none` self-host is gated and demonstrably fails when the canonical emitter is broken (revert `93d4d85a` locally to prove it).
- Added CI wall-time cost measured and stated.
- `AGENTS.md` records which modes are gated where.

---

## forgejo#547 — c frontend: drop the dead bound multiply for constant array parameters

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/547

**Task for an agent.** Small codegen cleanup with a known trigger.

## What is wrong

For a parameter written with **array syntax**, the C frontend emits the array bound's `element_count × element_size` multiply into the function's entry block — **even when the bound is a compile-time constant**, and in **every** allocator mode. The product is dead weight: it is a constant.

This was found while fixing the canonical emitter's entry captures (2026-08-24, commit `93d4d85a`): that multiply lands *between* the parameter homes, its scratch is RAX and RCX, and RCX is the fourth System V argument register — which is exactly how it broke argument capture. The capture bug is fixed; the pointless multiply is not.

## The task

Fold the multiply when the bound is constant, so nothing is emitted between the parameter homes for the common case. Check both the constant-bound path and the genuine VLA path — a VLA parameter's multiply is real work and must stay.

Related, worth checking while you are there: `tests/basic_c_machine_*` fixtures `arr_param` and `vla_param` in `machine_test.c` are the existing regression pair (NONE vs FAST, checked against absolute values because NONE is the oracle everywhere else there). Fixture text has to go in the right String8 segment of `machine_test.c` — there are six, only some feed the x86-64 program, and a fixture in the wrong one silently vanishes.

## Definition of done

- Constant-bound array parameters emit no multiply; VLA parameters are unchanged.
- The `arr_param` / `vla_param` fixtures still pass in all four modes, and the entry block is verified to be a contiguous run of parameter homes for the constant case.
- Instruction delta on stage 1 reported (small, but it should not be negative).

---

## forgejo#548 — object: read clang ELF objects built without -fno-pic (R_X86_64_32S, GOTPCREL)

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/548

**Task for an agent.** Object-reader gap. Self-contained, and it unblocks harness work.

## What is wrong

Reading a **clang ELF object built without `-fno-pic`** fails with `OBJECT_ERROR_UNSUPPORTED_TARGET` on `R_X86_64_32S` and GOTPCREL relocations. Since PIC is clang's default, this bites **any** ELF differential harness the moment it stops passing `-fno-pic` — and it was found exactly that way, during the Win64 COMDAT/weak work (PR 527).

## The task

Support those relocation types in the object reader. Read the orientation header of `src/buster/lib/compiler/object/object.c` first — it owns the boundaries here — and `link.c`'s header for what the linker expects to receive.

Scope it honestly: `R_X86_64_32S` is a straightforward absolute form; GOTPCREL implies a GOT, which the linker may or may not model today. If full GOT support is out of scope, say so in the issue's follow-up rather than half-implementing it — a reader that accepts a relocation it cannot resolve correctly is worse than one that refuses it.

## Definition of done

- A clang ELF object built **without** `-fno-pic` reads and links, or is refused with a diagnostic that names the specific unsupported relocation instead of the whole target.
- A registered fixture using a clang-produced PIC object.
- What is deliberately still unsupported is written down where the reader refuses it.

---

## forgejo#549 — codegen: materialize block addresses for label addresses and computed goto

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/549

**Task for an agent.** Cross-target design work. Larger than the other open tasks — expect a design pass before any code, and expect to split it into more than one PR.

## What is missing

Taking the address of a label, and everything that follows from it, is unsupported on **both** machine paths. From the AArch64 census (audit `2026-08-22T205418Z`, verified per class, not assumed):

- label addresses: 8 fixture functions
- `INDIRECT_BRANCH`: 1
- computed-goto `ARGUMENT`: 1
- inline assembly: 10 (separate concern, not this task)

The label/indirect-branch group was measured **equally unsupported on the x86-64 machine path**, which is what makes this cross-target design work rather than an AArch64 catch-up item. Functions using it fall back whole to the canonical emitter.

## What the design has to answer

1. **Block-address materialization** — a label's address is a code address that is only known after layout and relaxation, so it needs its own **relaxer fixup class**, on both targets, plus whatever the object writers need to emit for it (and for PIC, a relative form).
2. **Allocator edge contracts across indirect edges.** An `INDIRECT_BRANCH` has an edge set that is not derivable from the terminator alone. Live-range splitting, the frequency weighting in the fast allocator, and the spill placement all assume a known successor list. Decide how the edge set is declared and what the allocators are allowed to assume at an indirect edge — this is the part that will decide whether the change is small or large.
3. Interaction with the existing switch lowering, which already carries a side-table terminator on a64 (`switch_opcode` in the description) and a compare chain — a computed goto is close enough to that shape that it may reuse the machinery.

## Suggested shape of the work

- A written design first (an audit-style document or a PR description), naming the fixup class, the edge-declaration mechanism, and what each allocator mode must do.
- Then x86-64, then AArch64, each with the census re-measured — the census corpus changes underneath you (it tripled in one day when the c_abi port merged), so **re-measure, never quote a stale count**.

## Definition of done

- The ten-or-so label/indirect fixture functions select on both targets in all four allocator modes.
- Census re-measured and quoted before and after.
- `test_all_combinations_ci` green, including the aarch64 lanes.

---

## forgejo#555 — c frontend: an identifier in an array parameter's bound is unbound at lowering

Closed 2026-08-24. Original: https://code.buster14a.com/buster/buster/issues/555

## What is wrong

An identifier in an array parameter's bound that is **not** a call is rejected
on valid C:

```c
static long extra = 1;
static int head(long n, int a[extra + n]) { return a[0]; }
int main(void) { int v[4] = {13, 1, 2, 3}; return head(2, v); }
```

```
cc: error: t.c:2:31: in function 'head': could not lower variable-length array parameter bounds
```

The inner failure (visible by not overwriting `builder.failure_message` in the
parameter loop of `c_lower_to_ir`) is `could not lower unbound identifier
'extra'`, from `c_gen.c` around the "unbound identifier" diagnostic in
`c_ir_lower_expression_core_step`.

Same shape, same cause, all rejected:

```c
static int object = 1;
static int head(long n, int a[(long)&object - (long)&object + n]) { return a[0]; }

static int helper(void) { return 5; }
static int head2(long n, int a[(long)&helper - (long)&helper + n]) { return a[0]; }
```

The identical expressions work in a **body** VLA bound (`int a[extra + n];`
inside a function compiles and runs), so this is specific to the declarator.

## How it was diagnosed

`c_parse_array_suffixes` (`c_parse.c`) records a parameter's bound as a token
range and never binds the identifiers inside it, so
`parse.identifier_uses` -- and therefore `token_entities`, which is derived
from it in `c_lower_to_ir` -- has no entry for any token in the bound. Verified
in gdb: for `bound(n)` in a declarator both `token_entities[callee]` and
`token_entities[argument]` are `0xFFFFFFFF`.

Lowering copes for two of the three cases by name rather than by entity:

- a **local/parameter** name resolves through the name-based fallback loop over
  `builder->locals` in `c_ir_lower_expression_core_step` (right after
  `c_ir_identifier_entity`), which is why `int a[n]` works;
- a **callee** resolves through `c_ir_find_function_for_call`, which is
  name-based, which is why PR #554 could make `int a[g(n)]` work;
- a **global object or function designator** has no such fallback --
  `c_ir_emit_global_place` and the `C_ENTITY_FUNCTION` arm both need the entity
  from `c_ir_identifier_entity`, which returns invalid, so `value` stays
  invalid and the "unbound identifier" diagnostic fires.

## Where to start

- `src/buster/lib/compiler/frontend/c/c_gen.c`, `c_ir_lower_expression_core_step`,
  the `CEntityId entity = c_ir_identifier_entity(builder, index);` that opens
  the identifier arm. `c_ir_identifier_entity_or_lookup` right next to it
  already does the file-scope fallback that would resolve these; the question
  is whether widening that arm is safe for body identifiers, where an unbound
  name currently *is* an error.
- `src/buster/lib/compiler/frontend/c/c_parse.c`, `c_parse_array_suffixes` and
  `c_parse_bind_identifier`, for the root-cause alternative: bind a parameter
  bound's identifiers at parse time. That is the honest fix and it would also
  retire the parameter-bound reachability pass PR #554 added, but
  `c_parse_array_suffixes` is shared with every other declarator (binding there
  would double-bind local ones), it takes no scope, and
  `c_parse_bind_identifier` asserts on `identifier_use_capacity` -- so the
  capacity accounting and the scope plumbing both have to be worked out first.

## Constraints and do-not-retries

- PR #554 already handled the call case and the three capacities it needed;
  do not re-derive those. Its parameter-bound reachability pass exists only
  because the parser records no identifier use here -- if the parser fix lands,
  delete the pass and re-measure.
- Widening the capacity scan over the declarator **unconditionally** costs
  0,05% of a self-compile's instructions; PR #554 gates it on the definition
  having an array parameter for half that. Keep the gate.

## How to validate

Differential against clang with the exit code carrying the answer (binaries
built by `ide cc` swallow `printf` output, so `return` the value):

```sh
clang -O0 -o a t.c && ./a; echo $?
build/Release/ide cc -o b t.c && ./b; echo $?
```

Cover: a global object in the bound, a function designator's address in the
bound, both in the second dimension of a two-dimensional parameter, and an
enumerator. Then extend `tests/basic_c_vla.c` (it already carries
`check_call_bound_parameter`), and run `./build.sh test_all_combinations_ci`
plus `./build.sh test_self_host`. Report the self-compile instruction delta
measured with both compilers over one fixed baseline tree.

## Definition of done

The four shapes above compile, run, and agree with clang; `tests/basic_c_vla.c`
covers them; the local matrix is green; the instruction delta is reported.

---

## forgejo#573 — compat: compile and test cJSON

Closed 2026-08-25. Original: https://code.buster14a.com/buster/buster/issues/573

Use cJSON as the smallest real-world compatibility target for the C frontend and native pipeline.

Upstream: https://github.com/DaveGamble/cJSON

An initial smoke compile reached cJSON.c and stopped because __DBL_EPSILON__ is not predefined. Fix that and continue until the unmodified library and upstream tests pass; reduce every compiler defect found into a focused buster regression test.

Acceptance criteria:

- Pin an upstream tag or commit in an opt-in compatibility harness without vendoring sources into buster.
- Compile cJSON.c and cJSON_Utils.c with ide cc -c.
- Link and run the upstream tests, first with a system linker if useful for isolation, then through ide cc end to end.
- Compare test results and representative JSON round trips with Clang.
- Exercise the default FAST allocator and the NONE canonical path; expand to MIR_STACK and QUALITY once the baseline passes.
- Record compiler time and the source metrics for the pinned workload.

Keep upstream source patches at zero. Any required driver flags or environment differences must be documented by the harness.

---

## forgejo#574 — compat: compile and test zlib

Closed 2026-08-25. Original: https://code.buster14a.com/buster/buster/issues/574

Use zlib as the first serious external project compatibility target.

Upstream: https://github.com/madler/zlib

A current smoke run completed most configure probes and compiled adler32.c and crc32.c. The first production blocker was logical-expression lowering in deflate.c; the example program also exposed the comma-heavy conditional expression in the gzgetc macro. These are small, high-signal failures that should become focused regression fixtures.

Acceptance criteria:

- Pin an upstream tag or commit in an opt-in compatibility harness without vendoring sources.
- Compile every zlib translation unit and build libz.a.
- Build and run example, minigzip, and the upstream test targets.
- Validate compression and decompression against Clang-built zlib with deterministic corpus hashes.
- Separate compile/codegen validation from archive and linker validation by cross-linking where useful.
- Pass with FAST and NONE, then verify MIR_STACK and QUALITY produce the same observable results.
- Record compile time, source metrics, and compression throughput without treating generated-code speed as compiler throughput.

After the explicit manifest passes, support the unmodified upstream configure and make workflow as a separate driver-compatibility gate.

---

## forgejo#575 — compat: compile and test Lua

Closed 2026-08-26. Original: https://code.buster14a.com/buster/buster/issues/575

Compile and run the Lua interpreter as the first complete language runtime built by buster.

Upstream: https://www.lua.org/ and https://github.com/lua/lua

A current smoke compile stopped on missing __DBL_MANT_DIG__ and on conditional-expression type prediction in lua_absindex. Resolve these and reduce each defect into focused frontend tests before continuing through the rest of the source.

Acceptance criteria:

- Pin a stable upstream release in an opt-in external compatibility harness.
- Compile all Lua production translation units with no upstream source patches.
- Link the lua and luac executables through ide cc.
- Run the upstream Lua tests and compare results with the same release built by Clang.
- Add deterministic workloads covering parsing, bytecode generation, table operations, closures, coroutines, varargs, floating point, and protected errors.
- Pass FAST and NONE first, followed by MIR_STACK and QUALITY.
- Record compile time, source metrics, runtime failures, and allocator fallbacks.

Build-system compatibility is secondary to getting an explicit source manifest compiling and running correctly.

---

## forgejo#576 — compat: compile and test yyjson

Closed 2026-08-26. Original: https://code.buster14a.com/buster/buster/issues/576

Use the yyjson amalgamation to stress large translation units, integer formatting, aliasing, unaligned access, and optimized parser control flow.

Upstream: https://github.com/ibireme/yyjson

A current smoke compile progressed roughly 7,000 source lines before rejecting a compound assignment. The reported function context did not appear to match the displayed source location, so diagnostic attribution should be checked as well as lowering correctness.

Acceptance criteria:

- Pin an upstream release in an opt-in harness without vendoring sources.
- Compile the yyjson amalgamation unmodified with ide cc -c.
- Run the upstream unit tests and data-driven tests.
- Differentially parse and serialize a deterministic JSON corpus with Clang and compare success, failure, and output bytes where ordering is defined.
- Exercise FAST and NONE, followed by MIR_STACK and QUALITY.
- Reduce every compiler failure into a focused buster regression test.
- Record compiler time and source metrics for the amalgamation.

Do not enable optional SIMD paths until the portable implementation passes; add them later as explicit feature configurations.

---

## forgejo#577 — compat: compile and test selected stb libraries

Closed 2026-08-28. Original: https://code.buster14a.com/buster/buster/issues/577

Use selected stb single-header libraries to battle-test preprocessing, implementation macros, header reinclusion, static functions, image codecs, font parsing, and data-structure macros.

Upstream: https://github.com/nothings/stb

Start with stb_image, stb_image_write, stb_ds, and stb_truetype rather than treating the whole repository as one opaque pass or fail.

Acceptance criteria:

- Pin one upstream commit in an opt-in harness without checking third-party sources into buster.
- Compile each selected implementation in its own translation unit and in one combined stress translation unit where supported upstream.
- Run applicable upstream tests plus deterministic image decode, image encode, font raster, and stb_ds workloads.
- Compare decoded pixels, encoded file hashes where deterministic, glyph bitmaps, and data-structure results against Clang.
- Exercise FAST and NONE first, followed by MIR_STACK and QUALITY.
- Capture preprocessing/source metrics, especially include amplification and output token counts.
- Reduce every defect into a minimal buster regression fixture.

Keep platform-specific graphics and window dependencies out of the initial milestone.

---

## forgejo#578 — compat: compile and test LZ4

Closed 2026-08-28. Original: https://code.buster14a.com/buster/buster/issues/578

Use LZ4 to validate performance-sensitive integer code, unaligned memory access, endian handling, large inputs, and the compiler driver across a multi-file library and CLI.

Upstream: https://github.com/lz4/lz4

Acceptance criteria:

- Pin an upstream release in an opt-in external compatibility harness.
- Compile the portable library first, then the frame library and lz4 CLI, without upstream source patches.
- Run upstream unit, round-trip, corruption, and interoperability tests.
- Cross-check buster-compressed data with Clang-built LZ4 and vice versa.
- Pass the same workloads with FAST, NONE, MIR_STACK, and QUALITY.
- Add explicit baseline and native CPU configurations only after the portable path passes.
- Record compiler time and source metrics separately from compression throughput and generated-code instruction counts.
- Reduce every compiler defect into a focused buster test.

Treat upstream make or CMake detection as a later driver milestone; begin with an explicit source manifest.

---

## forgejo#579 — compat: compile and test SQLite

Closed 2026-08-28. Original: https://code.buster14a.com/buster/buster/issues/579

Use SQLite as the industrial C compatibility milestone after the smaller library targets pass.

Upstream: https://sqlite.org/

Begin with the official amalgamation and shell rather than the full source-generation toolchain. This target stresses a very large translation unit, macros, varargs, filesystem interfaces, threading, floating point, and sustained real application execution.

Acceptance criteria:

- Pin an official SQLite release in an opt-in harness without vendoring sources into buster.
- Compile sqlite3.c and shell.c unmodified and link the sqlite3 executable through ide cc.
- Run deterministic SQL scripts covering schema changes, transactions, indexes, joins, triggers, virtual tables available in the selected configuration, backup, and PRAGMA integrity_check.
- Compare database files and query results with the same release built by Clang where SQLite promises compatibility.
- Run the upstream test suite in staged subsets, documenting unsupported optional features explicitly.
- Pass FAST and NONE before expanding to MIR_STACK and QUALITY.
- Record compile time, source metrics, peak memory, and linker time.

Every failure must be classified as frontend, codegen, ABI, libc/header, linker, driver, or upstream build-system compatibility and reduced when practical.

---

## forgejo#580 — compat: compile and run sbase utilities

Closed 2026-08-28. Original: https://code.buster14a.com/buster/buster/issues/580

Use sbase as a breadth test for many small POSIX command-line programs and system-header interactions.

Upstream: https://git.suckless.org/sbase/

This milestone should follow the core library targets so failures in POSIX declarations and driver behavior are not mixed with basic expression-lowering defects.

Acceptance criteria:

- Pin one upstream commit in an opt-in external harness without vendoring sources.
- Compile and link each utility with ide cc using an explicit manifest first.
- Run upstream tests where available and add deterministic differential cases against the host utilities or a Clang-built sbase.
- Cover pipelines, files, standard input, binary data, error exits, large arguments, and locale-independent behavior.
- Track which utilities pass, fail to compile, fail to link, or misbehave at runtime instead of exposing only one aggregate result.
- Exercise FAST and NONE across the complete set and sample MIR_STACK and QUALITY once stable.
- Reduce compiler defects into focused buster fixtures.

After explicit builds pass, make the upstream makefile work with CC set to ide cc as a driver-compatibility gate.

---

## forgejo#581 — compat: compile and run DoomGeneric headlessly

Closed 2026-08-28. Original: https://code.buster14a.com/buster/buster/issues/581

Build DoomGeneric as a complete, engaging application-level compiler test with a headless deterministic platform layer.

Upstream: https://github.com/ozkl/doomgeneric

The initial target should avoid window-system noise. Supply a narrow external test platform that feeds deterministic input, runs a known demo or fixed number of ticks, and hashes frames and relevant game state. Do not add Doom or WAD assets to the repository.

Acceptance criteria:

- Pin an upstream commit and document the separately supplied shareware or user-provided data requirement.
- Compile all portable DoomGeneric sources without upstream patches.
- Link the headless executable through ide cc.
- Compare frame and state hashes against the same sources built by Clang for a deterministic run.
- Exercise file I/O, audio stubs, timing stubs, input events, rendering, and save/load behavior.
- Pass FAST and NONE, then MIR_STACK and QUALITY.
- Record compile time, source metrics, runtime fallbacks, and the first divergent tick on failure.
- Reduce compiler bugs into focused tests whenever possible.

A graphical platform backend is a later optional milestone, not part of the first correctness gate.

---

## forgejo#582 — compat: compile and test QuickJS

Closed 2026-08-28. Original: https://code.buster14a.com/buster/buster/issues/582

Use QuickJS as a stretch compatibility target for a compact but demanding language runtime and standard library.

Upstream: https://bellard.org/quickjs/

This target stresses floating-point edge cases, 64-bit and wider integer work, garbage collection, tagged values, function-pointer dispatch, atomics, regular expressions, bytecode, and extensive libc and POSIX interfaces.

Acceptance criteria:

- Pin an official release in an opt-in external harness without vendoring sources.
- Inventory required GNU extensions, generated inputs, platform APIs, and optional features before changing buster.
- Compile and link the qjs executable with a documented minimal feature configuration and no upstream source patches.
- Run the upstream tests, then a bounded deterministic subset of Test262.
- Compare exit status, stdout, stderr, and serialized results with a Clang build.
- Pass FAST and NONE before attempting MIR_STACK and QUALITY across the full suite.
- Record compile time, source metrics, peak memory, fallbacks, and test counts.
- Reduce compiler failures into focused fixtures.

Unsupported optional JIT or platform integrations may remain excluded when the configuration is explicit and reproducible.

---

## forgejo#583 — compat: compile and bootstrap musl libc

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/583

Use musl as a long-term compiler, ABI, linker, TLS, atomics, inline-assembly, and freestanding-runtime compatibility target.

Upstream: https://musl.libc.org/

This is deliberately a stretch milestone. It should begin only after zlib, Lua, LZ4, and substantial SQLite coverage pass, because otherwise overlapping frontend, system-header, object, linker, startup, and ABI failures will be difficult to classify.

Acceptance criteria:

- Pin an official musl release in an isolated compatibility workspace without vendoring it into buster.
- Inventory required C extensions, target-specific assembly, TLS models, atomics, object relocations, and linker behavior.
- Compile the portable C subset, then build a complete libc archive and startup objects for one supported Linux architecture.
- Link and run static hello-world and progressively broader libc-test programs without using the host libc.
- Run the musl libc-test suite in classified subsets and compare with a Clang-built musl of the same configuration.
- Preserve reproducibility and report every excluded target-specific component.
- Add FAST and NONE coverage where applicable and reduce compiler defects into focused tests.
- Record compile time, source metrics, archive/link time, and ABI failures.

Do not weaken existing hosted-libc behavior or self-hosting to make this target pass.

---

## forgejo#639 — link: copy relocations do not define an imported data symbol's aliases, so extern environ reads NULL

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/639

`extern char **environ` reads NULL in a Buster-linked non-PIE executable, so
programs that walk the environment crash on the first dereference. Two sbase
utilities fail this way — `env` and `find` — and the sbase harness (#638)
records them as a known gap rather than fixing them.

Five-line reproduction:

```c
#include <stdio.h>
extern char **environ;
int main(void) { printf("environ=%p\n", (void *)environ); return 0; }
```

```
$ build/Release/ide cc env.c -o env && ./env
environ=(nil)
$ gcc -fno-pic -no-pie env.c -o env-gcc && ./env-gcc
environ=0x7ffd55d24ea8
```

## What happens

Imported data reaches a non-PIE executable through a copy relocation: the
linker reserves a slot in the executable and emits `R_X86_64_COPY`, and the
loader copies the shared library's bytes into it during relocation processing.
Buster does exactly that, and the relocation is applied — but the copy is
taken before glibc's startup code stores the environment pointer, so the slot
holds the pre-initialization value, which is zero.

GNU ld does not have this problem because it defines the symbol's whole alias
set at the copy slot. In `env-gcc` above:

```
$ readelf -W --dyn-syms env-gcc | tail -2
     6: 0000000000404018     8 OBJECT  WEAK   DEFAULT   26 environ@GLIBC_2.2.5
     7: 0000000000404018     8 OBJECT  GLOBAL DEFAULT   26 __environ@GLIBC_2.2.5
```

Because the executable now defines `__environ`, glibc's own references to it
bind to the executable's copy, and `__libc_start_main`'s `__environ = ev`
writes into that copy. The program reads the value glibc just stored. Buster's
executable defines only `environ`, the name the object referenced, so glibc's
write lands in libc's own storage and the copy stays stale.

## What the fix needs

The driver would have to read the shared library's ELF symbol table — it
already does this for PE exports in `compiler_driver_read_library_exports`,
and `NativeDynamicLibrary` already carries an `exported_symbols` array that is
only filled on Windows — group the exports that share an address, and pass the
alias set for each imported data symbol to the linker. `link.c`'s ELF
executable writer would then emit one dynamic symbol per alias at the copy
slot, which means the dynsym count, the hash table and the dynamic string
table all grow by the alias count.

Nothing else in the compatibility set has hit this: it needs a libc variable
that libc itself writes after startup and that is exported under more than one
name. `environ` is the common one.

## Where it is recorded today

- `sbase_copy_relocation_gaps` in `build.c` — the harness expects `env` and
  `find` to differ from the Clang build and reports a stale expectation if one
  starts matching.
- The sbase section of `AGENTS.md`.

---

## forgejo#641 — c: call a function declared through a type name rather than a declarator

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/641

## What is wrong

A function declared through a *type name* rather than a parameter-list
declarator cannot be called by name in the same translation unit:

```c
int f(int x) { return x + 1; }
extern __typeof(f) g;            // or: typedef int F(int); extern F g;
int main(void) { return g(1); }  // cc: error: could not lower unbound identifier 'g'
```

Taking its address works (`int (*p)(int) = g;` lowers, and the call through
`p` is correct), so only the by-name call path is missing. Clang and GCC
compile all three forms.

## How it was diagnosed

Found while implementing `__attribute__((alias))` for musl (issue 583). musl's
`weak_alias(old, new)` expands to `extern __typeof(old) new
__attribute__((__weak__, __alias__(#old)))`, so every name musl publishes is
declared this way. That work is unblocked -- musl always has a prototype for
the aliased name too, and the entity-matching relaxation in
`c_parse_entity_kind_redeclares` (`c_parse.c`) makes the two declarations one
entity, so the prototype's `C_DECLARATION_FUNCTION` is what a call resolves
against. The gap survives only for a name whose *sole* declaration is the
type-name form.

## Where to start

- `c_analyze_semantics` in `src/buster/lib/compiler/frontend/c/c_parse.c`
  computes `CDeclarationKind` from `function_name.length`, which is the
  declarator's function-name token. With no parameter-list declarator there is
  no such token, so the declaration is filed `C_DECLARATION_OBJECT` even
  though its type is `C_TYPE_FUNCTION`.
- `c_ir_build_function_name_index` in `c_gen.c` only indexes
  `C_DECLARATION_FUNCTION`, so `c_ir_find_function` misses the name, and
  `c_ir_find_function_for_call` additionally checks arity against
  `builder->signatures[declaration_index]`, which `c_ir_function_signature`
  fills from `declaration.parameter_start/parameter_count` -- empty for this
  shape. The parameter list lives in the `CType`, not in the declaration.
- The diagnostic is also misleading: the identifier branch in
  `c_ir_lower_expression_core_step` reports "could not lower unbound
  identifier" when the entity *is* bound and it is the function lookup that
  failed.

## Definition of done

`tests/basic_c_weak_alias.c` currently proves its `__typeof`-declared aliases
through their addresses, with a comment saying why. Change those to direct
calls, and add the plain `typedef int F(int); extern F g;` shape. Then
`./build.sh build --config Release -t test_all` and
`./build.sh test_self_host --config Release` stay green, and
`./build/build test_musl --config Release ~/dev/musl-v1.2.6` keeps
`MUSL_INVENTORY compiled=1190 failure_hash=0xa5f8413710a89d51` (measured
2026-08-28) or moves it deliberately.

---

## forgejo#644 — codegen: an x87 aggregate object rejects the whole function

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/644

The canonical emitter refuses any function one of whose values has a canonical
type that *contains* f80 without *being* the scalar x87 shape. That gate is now
the sole blocker for the seventeen `src/math` units named in issue 583, whose
static initializers PR 643 taught the frontend to fold.

## Reproduce

```sh
./build.sh build --config Release -t ide
printf 'static long double a[2];\nlong double f(void){return a[1];}\n' > /tmp/a.c
./build/Release/ide cc -c /tmp/a.c -o /dev/null
# cc: error: C code generation failed with error 3 (CODEGEN_ERROR_UNSUPPORTED_ABI),
# function 0 ('f', ...), instruction 0, opcode 0, operation 73
```

No initializer is involved: an uninitialized array reproduces it, and it
reproduced identically on the binary from before PR 643. These all fail the
same way, while the scalar and pointer forms beside them succeed:

| shape | verdict |
| --- | --- |
| `long double q(long double *p){return p[1];}` | ok |
| `static long double s; long double q(void){return s;}` | ok |
| `static struct S{long double x;} s; long double q(void){return s.x;}` | ok |
| `static long double a[2]; long double *q(void){return &a[1];}` | ok |
| `static long double a[2]; long double q(void){return a[1];}` | **error 3** |
| `static long double a[2]; long double q(void){long double *p=a; return p[1];}` | **error 3** |
| `long double q(void){long double a[2]; a[0]=1.0L; return a[1];}` | **error 3** |
| `static long double a[2]; ... (const unsigned char*)a` inside a function, `-fregister-allocator=none` | **error 3** |

Note the last row: reading the array's *bytes* is refused too, under the NONE
allocator only, because that is the allocator whose path is the canonical
emitter. `tests/basic_c_long_double_static_initializer.c` works around it by
reaching each object through a file-scope pointer; that workaround should be
removable once this is fixed.

## Where it is

`codegen_generate_canonical_module_attempt` in
`src/buster/lib/compiler/codegen/codegen.c`, in the block that opens with the
comment "The recursive contains query is broader than the x87 payload we can
interpret" (around line 7985 as of `dfc07bb2`). For each of the function's
values it asks `codegen_canonical_x64_type_contains_f80_cached`, and if the
answer is yes it demands `codegen_canonical_x64_type_is_f80_x87_shape_cached`,
which requires `layout.size == 16 && layout.alignment == 16` plus an ABI class
of x87. A `[2 x long double]` is 32 bytes, so it fails that and the whole
function is rejected.

The gate is deliberate — its comment says it exists so that "an
instruction-specific path" cannot "mistake their bytes for scalar f80 data".
The work is to let an x87 *object* through as memory while keeping every
x87 *ABI value* on the narrow shape it has today: an array or struct of long
doubles is addressable storage that only ever moves through loads, stores and
`memcpy`, never through a return-class or argument-class decision.

## Validate

- The four shapes in the table above compile, and their generated code loads
  and stores the right ten bytes.
- `./build.sh build --config Release -t test_all` and
  `./build.sh test_self_host --config Release`.
- `./build/build test_musl --config Release ~/dev/musl-v1.2.6`: the seventeen
  units of issue 583 — `floorl`, `ceill`, `roundl`, `truncl`, `rintl`, `modfl`,
  `__rem_pio2l`, `atanl`, `expl`, `logl`, `log2l`, `log10l`, `log1pl`, `powl`,
  `tgammal`, `erfl`, `exp10l` — should compile. Rebaseline
  `MUSL_EXPECTED_COMPILED_UNITS` and `MUSL_EXPECTED_FAILURE_HASH` in `build.c`
  from the `MUSL_INVENTORY` line and update the class counts in AGENTS.md's
  musl section (currently 66 code-generation failures, of which these are part).
- Oracle the emitted code against Clang for the same source, by object bytes.
  `~/dev/musl-v1.2.6` is pinned at tag v1.2.6, commit
  `9fa28ece75d8a2191de7c5bb53bed224c5947417`; never modify it.

## Do not retry

Loosening `codegen_canonical_x64_type_is_f80_x87_shape_cached` itself is the
wrong lever: that predicate answers an ABI question — "is this type returned
and passed in x87?" — and an array of long doubles is not. The gate that needs
to change is the per-value scan, which currently conflates "this function
touches x87 storage" with "this function passes x87 across a boundary".

---

## forgejo#646 — link: an undefined weak symbol must resolve to zero, not become a dynamic import

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/646

## What is wrong

An undefined *weak* symbol must resolve to address zero when nothing in the
link defines it. Buster's linker instead turns it into a dynamic import, so the
program builds and then dies at load.

Reproduce with the compiler as of PR #645, which is what made weak symbols
reachable from C source in the first place (`.weak` in a module-level
`__asm__` block):

```c
typedef unsigned long word;
int c(word* absent);
__asm__(".text\n"
        ".globl e\n"
        ".type e, @function\n"
        "e:\n"
        "xorl %ebp, %ebp\n"
        ".weak absent_symbol\n"
        ".hidden absent_symbol\n"
        "lea absent_symbol(%rip), %rdi\n"
        "andq $-16, %rsp\n"
        "call c\n"
        "movl %eax, %edi\n"
        "movl $60, %eax\n"
        "syscall\n"
        "hlt\n");
int c(word* absent){ return absent == 0 ? 0 : 7; }
```

```sh
./build/Release/ide cc -e e -o /tmp/wk /tmp/wk.c
/tmp/wk
# symbol lookup error: /tmp/wk: undefined symbol: absent_symbol
```

The object itself is right. `readelf -sW` shows `absent_symbol` as
`OBJECT WEAK HIDDEN UND`, matching Clang byte for byte in binding and
visibility, and `ld -static` on the same object resolves the `lea` to address
zero and produces a program that runs. Only Buster's own link is wrong.

## Why it matters

This is exactly musl's `_DYNAMIC` in `arch/x86_64/crt_arch.h`: a static
program's startup code takes the address of `_DYNAMIC` and reads zero to learn
it is static. Any startup object Buster produces is therefore linkable by GNU
`ld` but not by Buster's own linker. Measured 2026-08-28.

## Where to start

`link_objects` in `src/buster/lib/compiler/link/link.c` already arbitrates
`ObjectSymbol.weak` for duplicate *definitions* (around
`link.c:826-838`); nothing consults it for an undefined one. The driver links
with `allow_undefined_symbols = true`, so the undefined symbol survives into
`link_native_executable_elf64_x86_64` / `..._dynamic`, which decides at
`link.c:2396` and `link.c:2667` whether an undefined symbol becomes a dynamic
import. A weak undefined that no input object and no shared library defines
should instead resolve to zero and its relocations be applied against that,
rather than joining the dynamic symbol table.

Take care not to change the existing case: a weak undefined that a shared
library *does* define must still bind to the library, which is what the
present code effectively gets right for the hosted link.

## Adjacent, and probably worth doing in the same change

`__attribute__((weak))` on a C declaration is still parsed and ignored, so a
weak symbol can only be spelled from assembly today. musl declares `_init` and
`_fini` weak in `crt/crt1.c`, and Buster's `crt1.o` emits both as strong
undefined for that reason. `IrSymbol.is_weak` and `ObjectSymbol.weak` already
carry the bit end to end (PR #645); the frontend just has to set it. The
sibling `__attribute__((alias))` work is tracked separately.

## Definition of done

- The repro above exits 0 under Buster's own link, on every register allocator.
- A hosted program that references a weak symbol a shared library defines still
  binds to the library.
- `__attribute__((weak))` on a C declaration produces `STB_WEAK`, checked in a
  fixture the way `tests/basic_c_global_asm_weak.c` checks the assembly
  spelling.
- `./build.sh build --config Release -t test_all` and
  `./build.sh test_self_host --config Release` green.

---

## forgejo#651 — c: __attribute__((packed)) and __attribute__((aligned)) are parsed and then ignored

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/651

`__attribute__((packed))` and `__attribute__((aligned(N)))` are accepted and
then ignored: the declaration compiles, no diagnostic is printed, and the
object gets the layout it would have had without the attribute. `_Alignas`,
which asks the same question in standard C, is honoured, so this is an
attribute-plumbing gap rather than a layout-engine one.

Measured 2026-08-29 on `3206be61`, Release `ide`.

## Reproduce

```sh
./build.sh build --config Release -t ide
cat > /tmp/al.c <<'EOF'
#include <stdio.h>
struct A { _Alignas(32) int i; };
struct __attribute__((packed)) P { char c; int i; };
static char buf[8] __attribute__((aligned(64)));
int main(void){ printf("%d %d %d %d %d\n", (int)sizeof(struct A), (int)_Alignof(struct A),
                        (int)sizeof(struct P), (int)_Alignof(struct P),
                        (int)((unsigned long)buf % 64 == 0)); return 0; }
EOF
clang -o /tmp/al-clang /tmp/al.c && /tmp/al-clang     # 32 32 5 1 1
./build/Release/ide cc -Wall -o /tmp/al-buster /tmp/al.c && /tmp/al-buster   # 32 32 8 4 0
```

`_Alignas` agrees (32/32). `packed` does not (`sizeof` 8 vs 5, `_Alignof` 4 vs
1) and neither does `aligned(64)` on a file-scope object, which lands
unaligned. `-Wall` says nothing in either case.

A member-level `__attribute__((packed))` is ignored the same way:
`struct C { char c; int i __attribute__((packed)); }` is 8 bytes rather than 5.

## Why it matters

A silently different layout is an ABI divergence that no fixture can trip over
by accident, because both halves of a Buster-only program agree with each
other. It shows up when Buster-compiled code meets code another compiler
built, or when a program computes an offset by hand. Live users in the pinned
compatibility corpora:

- QuickJS `cutils.h` declares `struct __attribute__((packed)) packed_u64` /
  `packed_u32` / `packed_u16` and reads unaligned memory through them. The
  single member sits at offset 0, so the idiom happens to work anyway, which is
  exactly the kind of accident that keeps this hidden.
- QuickJS `quickjs.c` uses `__attribute__((aligned(JS_MALLOC_ALIGN)))` on
  flexible array members of its allocator headers.
- SQLite's `vdbeapi.c` uses `__attribute__((aligned(8)))` on a buffer it then
  casts.
- libc-test's `src/functional/tls_local_exec.c` uses `aligned(64)` and
  `aligned(4096)` on `__thread` objects, and the test checks the addresses.

## Where to start

`_Alignas` already reaches the layout engine, so the alignment path exists and
the work is to route the attributes into it and to teach the struct layout an
alignment of one per member. Grep for the attribute parser in
`src/buster/lib/compiler/frontend/c/c_parse.c` and for `_Alignas` in
`c_parse.c`/`c_gen.c` to find both ends. `IrTypeLayout` carries `size` and
`alignment`; a packed aggregate needs its member offsets computed without
member alignment padding as well as its own alignment reduced.

## Validate

- The reproducer above prints Clang's five numbers under `ide cc`.
- A fixture under `tests/` that a host compiler and Buster both compile,
  linked in both directions the way
  `tests/basic_c_long_double_aggregate_{caller,callee}.c` are, so a layout
  disagreement fails rather than being agreed on privately.
- `./build.sh build --config Release -t test_all` and
  `./build.sh test_self_host --config Release`.
- The QuickJS and SQLite harnesses (`test_quickjs`, `test_sqlite`), whose
  sources contain the uses listed above.

## Do not retry

Do not make an unknown or unimplemented attribute a hard error as part of this:
the corpora carry many attributes Buster deliberately ignores, and turning the
whole class fatal fails units that are correct today. The change is to
implement these two, not to reject the rest.

---

## forgejo#653 — c: compose an unprototyped function declaration with a later prototype

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/653

## What is wrong

A function declared with an empty parameter list -- `long f();`, which in C99
declares a function taking an *unspecified* number of arguments rather than
none -- conflicts with a later prototype for the same name:

```c
long cp_c();
long cp_c(long nr, long u) { return nr + u; }
// cc: error: conflicting declaration of 'cp_c' (previous type 2, new type 4)
```

Clang and GCC compose the two: C11 6.7.6.3p15 says a function type with a
parameter type list is compatible with an unprototyped one when the return
types are compatible and the parameters are unaffected by the default
argument promotions. `c_parse_types_compatible` in
`src/buster/lib/compiler/frontend/c/c_parse.c` compares
`left_type.parameter_count != right_type.parameter_count` first and rejects
the pair, so the entity search in `c_analyze_semantics` finds no compatible
candidate and falls through to `C_DIAGNOSTIC_CONFLICTING_DECLARATION`.

## Why it matters

It is two of the 100 remaining `MUSL_UNSUPPORTED` units, and the two are the
whole "conflicting declarations" class of the inventory:

- `src/thread/__syscall_cp` -- `hidden long __syscall_cp_c();` at the top,
  then `weak_alias(sccp, __syscall_cp_c)` whose `__typeof(sccp)` is a
  prototype.
- `src/thread/pthread_cancel` -- `hidden long __cancel(), __syscall_cp_asm(),
  __syscall_cp_c();` at the top, then full prototypes for two of the three
  further down the same file.

Both are pre-existing: they fail identically on `3206be61`, before the
issue #641 fix that made a type-name declaration a function declaration.

## Where to start

- `c_parse_types_compatible` (`c_parse.c`), the `C_TYPE_FUNCTION` case. An
  unprototyped function type has `parameter_count == 0` and is not variadic --
  which is also what `long f(void)` has, and *that* one must keep conflicting
  with `long f(long)`. The two therefore have to be told apart on the type
  before the compatibility rule can be written; a `void` parameter list and an
  empty one are the same `CType` shape today, so this needs a bit on `CType`
  (or a parameter record) set where the declarator is parsed, not a new scan.
- Composition matters as much as compatibility: after the two are accepted as
  one entity, the entity's type has to become the *prototyped* one, or the
  call site resolves against a signature with no parameters. The array-bound
  composite in `c_analyze_semantics` (`existing->type = declaration->type`
  when a redeclaration completes an unbounded array) is the existing precedent
  for adopting the more complete type.
- The default-argument-promotion caveat is real: `f()` is *not* compatible
  with `f(char)` or `f(float)`. Clang warns rather than errors on those, but a
  conservative reading -- accept the composite only when every parameter type
  is unaffected by the promotions -- keeps the two musl units and refuses the
  shapes the standard refuses.

## How to validate

- A fixture under `tests/` covering: the accepted composition, a call through
  it resolving to the prototype's arity, `f(void)` still conflicting with
  `f(long)`, and a promotion-affected parameter still refused.
- `./build.sh build --config Release -t test_all` and
  `./build.sh test_self_host --config Release` stay green.
- `./build/build test_musl --config Release ~/dev/musl-v1.2.6` should reach
  `compiled=1251` with a new failure hash; the gate is
  `compiled=1249 failure_hash=0x4a0fec5b97536fd8` as of 2026-08-29, and both
  numbers move on purpose. The two units named above should leave the
  `MUSL_UNSUPPORTED` list and nothing else should join it.

---

## forgejo#656 — link: a default-visibility undefined weak symbol no library defines must also resolve to zero

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/656

## What is wrong

An undefined weak symbol with **default visibility** that no shared library
actually defines still comes out non-zero. Issue #646 fixed the hidden case --
a reference nothing can preempt is relocated against zero at link time -- but
left the default-visibility one to the dynamic loader, because buster does not
know what a shared library exports on ELF. The loader is then asked for a
symbol nothing has, and buster's own representation of the import is what
answers instead of zero: a function gets its PLT thunk's address and an object
gets the copy slot the writer reserved for it, both non-zero.

Measured 2026-08-29, on the tree with the #646 fix in it:

```c
extern __attribute__((weak)) unsigned long absent_object;
__attribute__((weak)) void absent_function(void);
int main(void) { return (&absent_object == 0 && absent_function == 0) ? 0 : 7; }
```

```sh
./build/Release/ide cc -o /tmp/wu /tmp/wu.c && /tmp/wu ; echo $?   # 7
clang -no-pie -o /tmp/wuc /tmp/wu.c && /tmp/wuc ; echo $?          # 0
```

The program loads and runs -- before #646 it died at load with `symbol lookup
error` -- so this is the remaining half, not a regression.

## Why it matters

musl's `crt/crt1.c` declares `_init` and `_fini` weak, and nothing else in a
link that has no `crti.o` defines them. Buster compiles that startup object
correctly (`readelf -sW` shows both as `FUNC WEAK UND`), but linking it with
buster's own linker gives both a PLT thunk, so `__libc_start_main` sees them
as present and calls into a null GOT slot. `ld -static` resolves both to zero.
Reproduced by hand on 2026-08-29 against `/home/david/dev/musl-v1.2.6`, with a
stub `__libc_start_main` reporting which of the two came back non-null:

```sh
./build/Release/ide cc -std=c99 -nostdinc -fno-builtin -fno-strict-aliasing \
  -fno-stack-protector -D_XOPEN_SOURCE=700 -I<musl arch/generated includes> \
  -O2 -g0 -c -o /tmp/crt1.o <musl>/crt/crt1.c
./build/Release/ide cc -e _start -o /tmp/crtprog /tmp/crt1.o /tmp/startmain.c
```

`crtprog` exits 9, the status the stub returns for a non-null `_fini`.

## Where to start

The decision is `link_elf_symbol_resolves_to_zero` in
`src/buster/lib/compiler/link/link.c`, just above the first ELF writer. It
answers `hidden || !dynamic_image` today because that is all the information
there is. The information it wants is `NativeDynamicLibrary.exported_symbols`
and `exports_known`, which `link.h` already carries and which
`compiler_driver_pe_library_exports` in `driver.c` already fills in **for PE
only** -- it maps the DLL and reads its export directory. The ELF equivalent
is the same shape: map the `.so`, read `.dynsym`/`.dynstr`, record the names.
Then the rule becomes the correct one -- a weak undefined that no library in
the link is known to export resolves to zero, and one that is exported stays
an import -- and it subsumes the visibility test rather than replacing it.

Two things to settle before writing it:

- **Finding the libraries.** The ELF writers name `libc.so.6` in `DT_NEEDED`
  themselves; it is not in `options.dynamic_libraries` at all, and no path for
  it is ever resolved. A search list (`-L` paths, the sysroot,
  `/lib/x86_64-linux-gnu`, `/usr/lib`, `/lib64`) is host-dependent, so decide
  what a link does when the file is not found: falling back to today's import
  makes the answer differ between machines, which a fixture cannot assert.
  Keeping the visibility rule as the floor and using exports only to *promote*
  a reference to an import is the conservative shape.
- **Cross links.** A `-target` link has no host libc to read. The same
  fallback question applies, and the sysroot is the only honest source.

## Definition of done

- The repro above exits 0 under buster's own link, on every register allocator.
- A weak reference to a symbol libc does define still binds to it, which
  `tests/basic_c_weak_undefined.c` checks through `puts`.
- musl's `crt1.o`, linked by buster's own linker, reports `_init` and `_fini`
  as null.
- `./build.sh build --config Release -t test_all` and
  `./build.sh test_self_host --config Release` green.

---

## forgejo#659 — link: AArch64 copy relocations keep the x86-64 relocation type

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/659

The AArch64 dynamic ELF writer leaves copy relocations at the x86-64
relocation type. An imported data symbol in an `aarch64-linux-gnu`
executable comes out as type 5, which on AArch64 is not `R_AARCH64_COPY`
(1024) but a nonsense relocation the loader would apply as a 16-bit
absolute move.

## Reproduction

```c
extern int optind;
int main(void) { return optind; }
```

```
$ build/Release/ide cc -target aarch64-linux-gnu a64.c -o a64
$ readelf -W --relocs a64
0000000000401018  0000000100000402 R_AARCH64_JUMP_SLOT    0000000000401110 optind + 0
0000000000401020  0000000200000402 R_AARCH64_JUMP_SLOT    0000000000000000 exit + 0
0000000000401110  0000000100000005 R_AARCH64_P32_MOVW_UABS_G0 0000000000401110 optind + 0
```

The last entry should read `R_AARCH64_COPY`. Measured 2026-08-29 at
`178c78f6`.

## Why it happens

`link_native_executable_elf64_aarch64_dynamic` in
`src/buster/lib/compiler/link/link.c` does not lay out its own image: it
calls the x86-64 dynamic writer and then patches the result. Its
relocation fix-up loop walks exactly `import_count` entries — the PLT
range — and rewrites each `Info` word to `R_AARCH64_JUMP_SLOT` (1026):

```c
for (u32 import_index = 0; import_index < import_count; import_index += 1)
{
    u64 relocation = relocation_offset + (u64)import_index * ELF_RELOCATION_SIZE;
    link_write_u64(bytes, relocation + 8, ((u64)(import_index + 1) << 32) | 1026);
```

The x86-64 writer emits the copy relocations *after* that range, at
`relocation_offset + plt_relocation_size + n * ELF_RELOCATION_SIZE`, so
the loop never reaches them and they keep the `R_X86_64_COPY` type 5 the
x86 writer wrote.

## Where to start

- `link_native_executable_elf64_aarch64_dynamic` — the fix-up loop above.
  It needs a second loop over the copy range, or the existing loop needs
  to cover both ranges and pick the type per entry.
- The copy range's length is the number of **slots**, not the number of
  data imports: PR 654 made two imported names for one library object
  share a slot. The x86 writer computes it as `copy_slot_count` and
  publishes it in the `DT_RELA`/`DT_RELASZ` pair (tags 7 and 8); reading
  it back from the dynamic section is more robust than recomputing it.
- The alias dynamic symbols PR 654 added need no fix-up — they carry no
  relocation — but a test should confirm that, because the aarch64 writer
  recomputes `import_count` itself and a future change that makes it
  count dynamic symbols instead of imports would silently walk into them.

## Constraints

Not currently reachable through the driver on a normal host: PR 654's
`compiler_driver_elf_library_exports` rejects a shared library whose
`e_machine` disagrees with the target, so an x86-64 host finds no
aarch64 `libc.so.6` unless `--sysroot` points at one. The relocation is
still wrong in the emitted image, which is what an aarch64 host or a
sysroot cross build would load. Treat it as a correctness bug with a low
blast radius, not as an urgent one.

## Validating

`link_test` is the cheap gate — the `copy_alias_*` block added by PR 654
builds an x86-64 image against a synthetic export table and asserts on
the relocation types, and the aarch64 counterpart belongs beside the
existing `aarch64_libc_*` cases. `link_test_elf_relocation_count(image,
1024)` is the assertion. The `readelf` line above is the end-to-end
check; a real execution test needs an aarch64 host or a sysroot, which
CI's `aarch64-macos-mini` runner cannot supply (Mach-O, not ELF).

## Done when

An `aarch64-linux-gnu` executable with an imported data symbol emits
`R_AARCH64_COPY` for every copy-relocation slot, `link_test` asserts it,
and the JUMP_SLOT entries are unchanged.

---

## forgejo#660 — link: ELF executables carry no symbol versioning

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/660

Buster's ELF executables carry no symbol versioning at all: no
`.gnu.version`, no `.gnu.version_r`, no `DT_VER*` entries, and no version
index on any dynamic symbol. Most links are unaffected because the loader
falls back to a library's default version, but the divergence is real and
it is silent.

## The observable difference

glibc publishes `sys_errlist` only under non-default versions (`@`, not
`@@`) — four of them, one per historical layout:

```
$ readelf -W --dyn-syms /usr/lib/libc.so.6 | grep ' sys_errlist@'
   ...: 0000000000216be0  1008 OBJECT  GLOBAL DEFAULT   36 sys_errlist@GLIBC_2.12
   ...: 0000000000216be0  1008 OBJECT  GLOBAL DEFAULT   36 sys_errlist@GLIBC_2.2.5
   ...: 0000000000216be0  1008 OBJECT  GLOBAL DEFAULT   36 sys_errlist@GLIBC_2.3
   ...: 0000000000216be0  1008 OBJECT  GLOBAL DEFAULT   36 sys_errlist@GLIBC_2.4
```

An unversioned reference therefore has no default to bind to, and GNU ld
refuses the link:

```
$ clang -fno-pic -no-pie t.c -o t
/usr/bin/ld: t.o: in function `main': undefined reference to `sys_errlist'
```

Buster links it and the program runs:

```
$ build/Release/ide cc t.c -o t && ./t
err=No such file or directory
```

Measured 2026-08-29 at `178c78f6`, glibc 2.42 on Arch.

The answer happened to be right here. It is not guaranteed to be: nothing
in the image states which version was intended, so the binding is
whatever the loader picks.

## Why this matters beyond the one symbol

A differential harness that compares Buster against Clang treats "Clang
refuses, Buster accepts" as a Buster success, because the harness only
sees the Buster side succeed. Any fixture reaching a
non-default-versioned symbol is therefore measuring nothing. **Do not use
`sys_errlist` as a clang-differential fixture** — that is how this was
found, while writing copy-relocation tests for #639.

The second-order case is the one to worry about: a program built against
a newer glibc that references a symbol whose *default* version moved. GNU
ld records the version it resolved at link time and the loader honors it;
Buster records nothing, so the same binary binds by name to whatever the
running glibc calls default. That is the versioning mechanism's entire
purpose.

## Where to start

- `link_native_executable_elf64_x86_64_dynamic` in
  `src/buster/lib/compiler/link/link.c` builds `.dynsym`, `.dynstr`,
  `.hash` and the dynamic array. `.gnu.version` is a parallel `u16` per
  dynamic symbol; `.gnu.version_r` is a per-library list of the version
  names actually needed; `DT_VERNEED`/`DT_VERNEEDNUM`/`DT_VERSYM` (tags
  0x6ffffffe, 0x6fffffff, 0x6ffffff0) publish them.
- The version a reference should get comes from the defining library, so
  the reader added for #639 —
  `compiler_driver_elf_dynamic_data_symbols` in
  `src/buster/lib/compiler/driver/driver.c` — is the natural place to
  pick it up. It already parses section headers to reach `SHT_DYNSYM`;
  `.gnu.version` (`SHT_GNU_versym`, 0x6fffffff) and `.gnu.version_d`
  (`SHT_GNU_verdef`, 0x6ffffffd) sit beside it and give the index and the
  name for every exported symbol.
- That reader currently collects **data** symbols only, because copy
  relocations were all it was for. Versioning applies to functions too,
  so this needs the function side as well — which is a real cost
  increase on a path deliberately kept off the normal link (it runs only
  when a link has an undefined data symbol,
  `compiler_driver_object_imports_data`). Decide that trade before
  widening it.

## Constraints

- Diagnosing "no default version for this symbol" the way GNU ld does is
  the cheaper half and may be worth doing alone: it turns a silent
  divergence into an error and would have caught the `sys_errlist` case
  with no image-format work at all.
- Emitting version records is the expensive half and changes the dynamic
  section layout, which the AArch64 dynamic writer patches by index
  (see #659). Any change here must be checked against that writer.

## Validating

`link_test` for the emitted sections and dynamic tags. End-to-end, the
comparison that matters is against GNU ld on the same source: a symbol
GNU ld refuses must be refused, and a versioned reference must record the
same version string GNU ld records (`readelf -W --version-info`). The
compatibility harnesses do not currently reach any of this — sbase,
zlib, Lua and QuickJS all use default-versioned symbols only.

## Done when

At minimum: a reference to a symbol with no default version is diagnosed
rather than linked. Fully: dynamic symbols carry version indexes,
`.gnu.version_r` names the versions the image needs, and `readelf -W
--version-info` on a Buster executable agrees with GNU ld's for the same
program.

---

## forgejo#661 — c: a trailing noreturn attribute marks sibling declarators, and the marker is not merged across declarations

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/661

Two defects in the C frontend's `noreturn` detection, found 2026-08-29 while auditing `noreturn` handling against clang at `40a87bd4`. The first drops live code; the second only costs dead code. Both live in the same pair of functions, so one change should carry both.

## Defect 1 — a trailing attribute spills onto sibling declarators (miscompile)

`c_ir_declaration_is_noreturn` (`src/buster/lib/compiler/frontend/c/c_gen.c:1116`) scans the declaration's whole *declarator list* range through `c_ir_noreturn_marker_in_range` (`c_gen.c:1043`), so an attribute owned by one declarator marks every declarator in the declaration.

```c
#include <stdio.h>
#include <stdlib.h>
void die(int) __attribute__((noreturn)), other(int);
void die(int c) { exit(c); }
void other(int x) { printf("other %d\n", x); }
int main(void) { other(1); printf("after\n"); return 0; }
```

buster terminates the block after the call to `other` and emits `ud2`, so the program dies with SIGILL (exit 132) before either line is flushed. clang prints `other 1` / `after` and exits 0.

The fix is to scan only the range belonging to the declarator being asked about. An attribute in the *specifier* range does legitimately apply to every declarator — `__attribute__((noreturn)) void a(int), b(int);` marks both — so keep that half as it is.

## Defect 2 — the marker is not merged across an entity's declarations

`is_noreturn` is computed per declaration (`c_gen.c:36647`) and a call resolves to one candidate per entity, the first declaration, per the comment at `c_gen.c:7678`. The marker on any later declaration is therefore invisible:

```c
void die(int);
__attribute__((noreturn)) void die(int);
int f(int x) { die(x); }          /* no ud2 after the call; clang treats it as noreturn */

void die(int);
__attribute__((noreturn)) void die(int x) { while (1) { (void)x; } }
int f(int x) { die(x); }          /* same: the attribute on the definition is lost */
```

Only unreachable code is emitted, so no wrong answers — but the shape (plain declaration in a header, attribute on the definition) is ordinary C.

Prefer OR-ing the marker across all declarations that share the entity when the signature table is built, over moving which candidate the call resolves to: that resolution carries its own documented unprototyped-vs-prototyped rule and is not the thing that is wrong here.

## Oracle

```sh
./build.sh build --config Release -t ide
build/Release/ide cc -c x.c -o x.o && objdump -d --no-show-raw-insn x.o
```

`ud2` immediately after the `call` means buster saw the `noreturn`. `mov $0x0,%eax` before the `ud2` means it did not — that trailing `ud2` is the ordinary falls-off-the-end terminator, not the noreturn one. Cross-check with `clang -c -Wreturn-type`: no `-Wreturn-type` warning means clang treats the callee as noreturn.

## Done when

`tests/basic_c_noreturn_call.c` (registered at `src/buster/tests/compiler/driver/driver_test.c:5288`) covers the multi-declarator shape — running to a normal exit rather than trapping — and both redeclaration shapes, and the driver suite is green.

## Verified as working, do not regress

`_Noreturn` in any specifier position, leading and trailing `__attribute__((noreturn))` and `((__noreturn__))`, attribute lists carrying other attributes (`((cold, noreturn))`), `__declspec(noreturn)`, `<stdnoreturn.h>`'s `noreturn` macro, the name fallback for `abort`/`__assert_fail`/`__assert_perror_fail`, and the resumes-after-call exemption in `c_gen.c:9652` that keeps `?:`, `&&`/`||`, loop and `if` conditions and statement expressions working.

---

## forgejo#662 — c: a statement expression containing control flow yields 0 instead of its value

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/662

A GNU statement expression whose body contains a branching statement (`if`, `while`) evaluates to 0 instead of the value of its final expression statement. Silent — no diagnostic. Found 2026-08-29 at `40a87bd4`.

```c
#include <stdio.h>
int a(int x) { return ({ if (x > 100) { x = 1; } 7; }); }
int b(int x) { int v = 0; v = ({ if (x > 100) { x = 1; } 7; }); return v; }
int c(int x) { return ({ if (x > 100) { x = 1; } 7; }) + 1; }
int d(int x) { printf("d=%d\n", ({ if (x > 100) { x = 1; } 7; })); return 0; }
int e(int x) { return ({ if (x > 100) { x = 1; } x + 6; }); }
int n(int x) { return ({ while (x > 100) { x = 1; } 7; }); }
int main(void) { printf("a=%d b=%d c=%d e=%d n=%d\n", a(1), b(1), c(1), e(1), n(1)); d(1); return 0; }
```

buster prints `a=0 b=0 c=1 e=0 n=0` and `d=0`; clang prints `a=7 b=7 c=8 e=7 n=7` and `d=7`. Every context is affected: initializer, `return`, assignment, arithmetic operand, call argument.

A statement expression with no branching statement in it is correct — `({ x = x + 1; 7; })` yields 7 — so the value is being lost once the body's control flow opens new blocks: the result is read from the wrong block, or never wired to the block the final expression statement ends up in. Lowering is in `src/buster/lib/compiler/frontend/c/c_gen.c`; start from the `C_IR_LOWER_FRAME_STATEMENT_EXPRESSION` frame and `statement_expression_mode` on the body frame.

## Why it survived

The existing coverage, `c_test_statement_expression_control_call` and `c_test_statement_expression_nested_call` at `src/buster/tests/compiler/frontend/c/c_test.c:7248`, only exercises `(void)({ ... })`. The value is discarded in both, so a lost value is invisible.

## Done when

The shapes above match clang, coverage exists for the value-producing case in each context (a runtime fixture diffed against clang is the strongest form), and `test_all` is green.

## Related

A declaration inside a value-producing statement expression (`int v = ({ int t = x + 1; t; });`) fails with "use of undeclared identifier"; filed separately. The two may share a root cause — if this fix covers it, say so on that issue.

---

## forgejo#663 — c: a declaration inside a value-producing statement expression is not visible to later statements

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/663

The C frontend rejects a declaration inside a value-producing GNU statement expression: the declared name is not visible to the statements that follow it inside the same statement expression. Found 2026-08-29 at `40a87bd4`.

```c
int g(int x) { int v = ({ int t = x + 1; t; }); return v; }
/* cc: error: use of undeclared identifier 't' */

int f(int x) { int v = x > 100 ? 1 : ({ int t = x + 1; t; }); return v; }
/* same */
```

clang compiles both. This is the idiomatic shape — it is most of why the construct exists, as in `({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })` — so it deserves a real scope fix rather than a special case.

Establish first whether the declaration is dropped at parse time or whether lowering cannot see its scope; the diagnostic wording points at name resolution rather than lowering. Start from the `C_IR_LOWER_FRAME_STATEMENT_EXPRESSION` frame and `statement_expression_mode` in `src/buster/lib/compiler/frontend/c/c_gen.c`, and the scope and entity handling in `src/buster/lib/compiler/frontend/c/c_parse.c`.

## Done when

Both shapes compile and produce clang's answers, coverage lands next to the existing statement-expression tests at `src/buster/tests/compiler/frontend/c/c_test.c:7248` (which only cover `(void)({ ... })` today), and `test_all` is green.

## Related

A statement expression containing an `if`/`while` yields 0 instead of its value; filed separately. Both may live in the same lowering path — rebase onto that fix if it lands first.

---

## forgejo#664 — c: the C23 [[...]] attribute syntax is not parsed at all

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/664

The C frontend does not support the C23 `[[...]]` attribute syntax at all. A declaration carrying one does not register, so the failure surfaces as an unrelated "undeclared identifier" later. Every `-std` mode behaves the same — `c17`, `c23`, `c2x`, `gnu23`. Found 2026-08-29 at `40a87bd4`.

```c
[[noreturn]] void die(int);
int f(int x) { die(x); }
/* cc: error: use of undeclared identifier 'die'  -- the declaration never registered */

[[deprecated]] void die(int);                         /* same failure */

[[noreturn]] void die(int x) { while (1) { (void)x; } }
/* cc: error: use of undeclared identifier 'x'    -- on a definition too */

int f(int x) { [[maybe_unused]] int y = x; return y; }
/* cc: error: use of undeclared identifier 'maybe_unused'  -- statement position too */
```

clang accepts all of these.

## Scope

Parse and skip `[[...]]` attribute lists everywhere the standard allows them — declarations, declarators, statements — including the scoped `[[vendor::attr]]` form and the balanced-token argument form `[[attr(...)]]`. Honor at minimum `[[noreturn]]`: `c_ir_noreturn_marker_in_range` in `src/buster/lib/compiler/frontend/c/c_gen.c:1043` already has a `[[` branch, which is dead code today because parsing fails before it can ever see one. Wire it up and prove it fires.

Attributes buster chooses not to act on should be skipped silently rather than rejected, matching how unknown names inside GNU `__attribute__` are already treated.

## Oracle for `[[noreturn]]`

```sh
./build.sh build --config Release -t ide
build/Release/ide cc -c x.c -o x.o && objdump -d --no-show-raw-insn x.o
```

`ud2` immediately after the `call` means the marker was seen; `mov $0x0,%eax` before the `ud2` means it was not (that trailing `ud2` is the ordinary falls-off-the-end terminator).

## Done when

The shapes above compile, `[[noreturn]]` reaches the lowering, fixtures land under `tests/` following `tests/basic_c_noreturn_call.c` and its registration at `src/buster/tests/compiler/driver/driver_test.c:5288`, parser coverage lands in `src/buster/tests/compiler/frontend/c/c_test.c`, and `test_all` is green.

If the full C23 attribute grammar is too large for one change, land parse-and-skip plus `[[noreturn]]` first and record here what was left out.

---

## forgejo#665 — c: noreturn on a function pointer type or typedef is ignored

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/665

`noreturn` written on a function pointer type or typedef is ignored: only declarations are consulted, so a call through the pointer does not end control flow. Found 2026-08-29 at `40a87bd4`.

```c
typedef __attribute__((noreturn)) void (*die_fn)(int);
int f(int x, die_fn p) { p(x); }
```

buster emits `mov $0x0,%eax` then the falls-off-the-end `ud2`, i.e. the call is treated as returning. clang treats it as noreturn — `clang -c -Wreturn-type` does not warn on this function.

`c_ir_declaration_is_noreturn` (`src/buster/lib/compiler/frontend/c/c_gen.c:1116`) reads a `CDeclaration`; nothing carries the marker on a type, so the indirect call site at `c_ir_emit_call_target` (`c_gen.c:9743`) has nothing to consult. Carrying it would mean a bit on the function type rather than on the declaration.

Impact is dead code only — no wrong answers — which is why this is filed separately from the `noreturn` defects that do drop live code. A plain pointer type is correctly *not* treated as noreturn even when it is initialized from a noreturn function, which matches clang; only the spelled-on-the-type case is missing.

## Done when

The shape above ends control flow at the indirect call, a fixture covers it alongside `tests/basic_c_noreturn_call.c` (registered at `src/buster/tests/compiler/driver/driver_test.c:5288`), and `test_all` is green.

Oracle: `build/Release/ide cc -c x.c -o x.o && objdump -d --no-show-raw-insn x.o`; `ud2` immediately after the `call *%reg` means the marker was seen.

---

## forgejo#666 — c: calling an unprototyped function with arguments is refused as 'could not prepare C calls'

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/666

Calling an unprototyped function with at least one argument is refused when the translation unit has no definition to supply the parameters. Found 2026-08-29 at `40a87bd4`.

```c
void die();
int f(int x) { die(x); return 0; }
/* cc: error: in function 'f': could not prepare C calls */

int die();
int f(int x) { return die(x); }        /* same */

void die();
int f(int x) { die(x, 2); return 0; }  /* same */
```

clang compiles all three. Two neighbouring shapes already work, which brackets the gap:

```c
void die(); int f(void) { die(); return 0; }                      /* ok: zero arguments */
void die(); void die(int x) { (void)x; } int f(int x){ die(x); return 0; }  /* ok: a later definition supplies the prototype */
```

The second is the documented unprototyped-to-prototyped candidate replacement at `src/buster/lib/compiler/frontend/c/c_gen.c:7678`; the missing case is the one where no prototyped declaration ever appears, so the call must be lowered from the argument types alone (default argument promotions). The refusal is raised by call preparation in `c_gen.c`; `c_ir_signature_call_supported` and the arity check in `c_ir_emit_call_target` (`c_gen.c:9701`) are the places to start.

Note the dialect split: `()` means "unspecified parameters" through C17 and these must compile, but C23 makes `()` equivalent to `(void)`, where a call with arguments is a constraint violation. If the C23 behaviour is deliberate, the fix is still worth making — the diagnostic should then name the real problem instead of "could not prepare C calls", and the pre-C23 dialects should accept the call.

## Done when

The three shapes above compile in the pre-C23 dialects and run correctly against clang's answers, C23 either accepts them or refuses them with an accurate diagnostic, coverage lands in `src/buster/tests/compiler/frontend/c/c_test.c`, and `test_all` is green.

---

## forgejo#673 — c: noreturn on a struct member declarator is ignored

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/673

`noreturn` written on a struct or union member declarator is ignored: a call
through the member does not end control flow. Found 2026-08-29 while fixing
#665, which covered the same marker on a typedef, a plain function-pointer
declarator, and a parameter declarator.

```c
extern int sink;
struct ops { __attribute__((noreturn)) void (*fail)(int); };
void member_case(int x, struct ops* o) { o->fail(x); sink = 5; }
```

buster emits the store to `sink`; clang does not, and `clang -c -Wreturn-type`
does not warn on the non-void spelling `int member_case(int x, struct ops* o)
{ o->fail(x); }`.

Impact is dead code only — no wrong answers — the same as #665.

## Where the mechanism already is

PR 672 carries the marker on the function type: the parser notes the function
type ids a declarator spelled it on into
`CParseResult.noreturn_function_types`, `c_parse_type_is_noreturn` answers for
one id, the C-to-IR type mapping copies the answer onto `IrType.is_noreturn`,
and `c_ir_emit_prepared_call_step` reads it through the signature it builds
from the pointed-to function type. Adding members means one more note site;
nothing downstream changes.

The three existing sites are `c_parse_declaration_type` (the wrapper around
`c_parse_declaration_type_derive`), the declarator segment loop in
`c_parse_local_declarations`, and `c_parse_parameter_segment`. Each captures
`result->type_count` before its declarator runs, calls
`c_parse_noreturn_candidate_function_type`, and only then scans tokens for the
marker.

Members are built instead inside the type-parse machine's member frame, around
`result->members[result->member_count++] = (CMember){...}` in `c_parse.c`,
which is why they were left out rather than grown into that change.

## Constraints paid for already

- **Only a type the declarator itself built may be noted.** A declarator that
  merely names an existing function type — `__attribute__((noreturn)) handler
  fail;` over a shared `typedef void handler(int);` — must not mark the
  typedef, or every other call written with it ends control flow and the live
  code after those calls is deleted. The member frame's `frame->checkpoint`
  is taken per member *declaration*, so `frame->checkpoint.type_count` is a
  sound lower bound for this guard; a per-declarator bound would be tighter
  but is not required for correctness.
- **The marker's token range must not span sibling declarators.** Scan the
  shared specifiers and this declarator separately, never the span between
  them, or a preceding declarator's own trailing attribute is read as this
  one's. `c_ir_declaration_is_noreturn` and the block-scope site both do this.
- Do **not** treat a plain member pointer initialized or assigned from a
  noreturn function as noreturn; clang does not, and #665 says so explicitly.

## Done when

The shape above ends control flow at the indirect call, the member case joins
`tests/basic_c_noreturn_type.c` under the same `-S` assertion the driver test
already runs (`must_not_be_reached` must appear nowhere in the assembly;
registered near `src/buster/tests/compiler/driver/driver_test.c:5350`), and
`test_all` is green.

Oracle: `build/Release/ide cc -S -target x86_64-unknown-linux-gnu x.c -o x.s`
and grep for the callee that must not be reached; `clang -c -Wreturn-type` on
the non-void spelling is the semantic oracle.

Watch the cost: the note sites run per declarator, and PR 672 measured its
three at +2 321 528 instructions (+0,0225%) on a fixed `ide.c` compile. A
member site that scans tokens before proving a function type was derived would
cost more than that on its own.

---

## forgejo#678 — c: an attribute list on a member declarator drops the whole aggregate body

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/678

A member declarator cannot carry an attribute list of its own. Two shapes are
rejected, and the whole aggregate body is dropped rather than the attribute
being ignored: the struct ends up with zero members, so the first error a user
sees blames an unrelated member access.

Found 2026-08-29 while fixing #673, which needed only the shared-specifier
spelling and so did not depend on either of these. `noreturn` is not involved
— any attribute reproduces both.

## The two shapes

A trailing attribute list on a **parenthesized** member declarator:

```c
struct s { void (*f)(int) __attribute__((aligned(8))); };
int g(struct s* o) { o->f(1); return 1; }
// buster: type 's' has no member named 'f' (0 fields available)
// clang:  accepted
```

A leading attribute list on a member declarator that is not the first of its
list:

```c
struct s { int a, __attribute__((aligned(8))) b; };
int g(struct s* o) { return o->b; }
// buster: type 's' has no member named 'b' (0 fields available)
// clang:  accepted
```

What already works, and bounds the gap: a trailing attribute on a plain,
pointer, or array member declarator (`int x __attribute__((aligned(8)));`,
`int *x ...`, `int x[2] ...`), and a leading attribute at the head of the
segment, which is the shared-specifier position (`__attribute__((packed)) int
x;`).

## Where it is

Both live in `c_type_parse_aggregate_segment_step` in
`src/buster/lib/compiler/frontend/c/c_parse.c`, in the declarator loop that
runs after `C_TYPE_PARSE_STAGE_FALLBACK`.

- The loop opens with `declarator = frame->declarator_start;` followed by
  `c_parse_pointer_chain`, with no `c_parse_skip_attributes` first. The
  segment head skips the leading attributes of the whole segment once, before
  the base type is parsed, so only the first declarator of the list is covered.
- The parenthesized path pushes a `C_TYPE_PARSE_FRAME_PARENTHESIZED` child
  with `end = frame->declarator_end`, which still spans the trailing attribute
  tokens, and the child requires the declarator to end exactly at `end`. The
  non-parenthesized path does not hit this because it calls
  `c_parse_skip_attributes` around its array suffixes.

Neither is a token-scan cost: an attribute list at either position is a
`c_parse_skip_attributes` call on a range the loop already walks.

## Constraint

Whatever range the parenthesized child is given must not swallow the trailing
attributes of a *sibling*: `frame->declarator_end` is already the top-level
comma boundary, so trimming the trailing attribute list off the end before the
push keeps it inside this declarator.

## Once it parses

`c_type_parse_aggregate_segment_step` already scans
`[frame->declarator_start, frame->declarator_end)` for the `noreturn` marker
alongside the shared specifiers (added by #673). That half of the scan finds
nothing today precisely because neither shape above parses; the moment they do,
`struct ops { void (*fail)(int) __attribute__((noreturn)), (*ok)(int); };`
should mark `fail` and leave `ok` alone. Adding that pair to
`tests/basic_c_noreturn_type.c` — the marked one calling `must_not_be_reached`,
the plain one falling through to a return the runtime half observes — is the
cheapest way to prove the sibling boundary holds.

## Done when

Both shapes above compile and the attribute takes effect (an `aligned` member
moves the layout, which `sizeof`/`offsetof` observes), the member-list sibling
pair in `tests/basic_c_noreturn_type.c` marks only its own declarator, and
`./build.sh build --config Release -t test_all` is green.

Oracles: `clang -fsyntax-only` for acceptance, and a `sizeof`/`offsetof` probe
compared against clang for the layout half.

---

## forgejo#679 — c: a [static N] array parameter makes the function's definition a LOCAL symbol

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/679

## What is wrong

A parameter declared with C99's `static` array bound makes the enclosing
function's **definition** come out as a `LOCAL` ELF symbol instead of `GLOBAL`.
`ide cc -c` exits 0 and prints nothing, so the gap is completely silent, and
when nothing in the same translation unit calls the function the now-internal
definition is dropped: the object comes out with no symbol and no `.text` at
all.

Verified on main `0dfd901d`, Release `ide`, x86_64-linux, 2026-08-29.

```c
// r1.c -- LOCAL in buster, GLOBAL in clang
void f(char b[static 8], unsigned n);
void f(char *b, unsigned n) { b[0] = (char)n; }
```

```c
// r2.c -- absent from the object entirely
void g(char b[static 8], unsigned n) { g[0] = (char)n; }
```

```
$ ide cc -c -o r1.o r1.c && readelf -sW r1.o | grep -w f
     1: 0000000000000000    54 FUNC    LOCAL  DEFAULT    1 f
$ clang -c -o r1c.o r1.c && readelf -sW r1c.o | grep -w f
     3: 0000000000000000    24 FUNC    GLOBAL DEFAULT    2 f

$ ide cc -c -o r2.o r2.c && readelf -sW r2.o
Symbol table '.symtab' contains 1 entry:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
```

Plain `char b[8]` (no `static`) is fine and comes out `GLOBAL`. The `static`
in the bound is the whole trigger: it reproduces with a constant bound, with
`sizeof` in the bound expression, whether the `static` appears on the
prototype, on the definition, or on both, and whether or not the definition
spells the parameter as a pointer.

It is **not** the visibility attribute. buster ignores
`__attribute__((visibility("hidden")))` and emits `GLOBAL DEFAULT`, which links
fine -- 258 of musl's 264 hidden symbols come out global.

## How it was diagnosed

`c_declaration_well_known_set` in
`src/buster/lib/compiler/frontend/c/c_gen.c` answers "which storage-class-ish
spellings appear in this declaration" by walking **every** pre-body token of
the declaration, with no nesting depth tracked. A function definition's
pre-body tokens include its whole parameter list, so the `static` inside
`[static 8]` is counted as a declaration specifier.

Both consumers on the function path read that answer:

- `c_gen.c:38365` and `c_gen.c:38434`: `bool internal =
  c_declaration_well_known_set(preprocess, declaration,
  C_SYMBOL_WELL_KNOWN_BIT(STATIC)) != 0;` -- `internal` picks
  `IR_LINKAGE_INTERNAL` for the symbol, which is the `LOCAL` binding.
- The same `internal`, combined with `!function_needed[declaration_index]`,
  is what skips the definition altogether when nothing in the unit calls it,
  which is the empty-object symptom.

The parser already gets this right. `c_parse.c:10693` tracks a
`specifier_depth` over the same token range, and its comment names both
hazards by name -- a statement expression in an initializer, and an array
parameter's `[static 3]` being a bound qualifier rather than a storage class.
The IR-lowering copy of the scan is the one that never learned it, so the
entity is correctly non-static while the emitted symbol is internal.

`EXTERN`, `INLINE*` and the `THREAD_*` bits are read through the same
unguarded scan (`c_gen.c:37726`, `37927`, `37975`, `38020`, `38097`, `38201`),
so the fix belongs in the scan rather than at any one call site.

## Why it matters

musl's `src/internal/procfdname.c` defines `__procfdname`, declared in
`src/internal/syscall.h` as

```c
hidden void __procfdname(char __buf[static 15+3*sizeof(int)], unsigned);
```

The unit compiles, so the `test_musl` compile inventory never sees a problem,
but the symbol is `LOCAL` in `libc-buster.a` where Clang has it
`GLOBAL HIDDEN`. It is the rank-2 `LIBCTEST_BLOCKER`, wanted by 15 libc-test
units. This is the second time a linkability gap has been invisible from the
compile side; the first was the weak-alias work (PR 642).

## How to validate a fix

```sh
./build.sh build --config Release -t ide
./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test
```

`LIBCTEST_EXPECTED_PASSING` should move up from 79 and the rank-2 blocker
should disappear; rebaseline `LIBC_TEST_EXPECTED_PASSING` and
`LIBC_TEST_EXPECTED_STATE_HASH` in `build.c` deliberately -- the harness
prints the new values. `MUSL_EXPECTED_COMPILED_UNITS` must **not** move: this
is a link-side gap, not a compile-side one, and a moving compile count would
mean the change did something else as well.

A census against a harness run directory (`build/musl-v1.2.6-<pid>/`) catches
any siblings:

```sh
readelf -sW libc-clang.a  | awk '$5=="GLOBAL" && $6=="HIDDEN" && $7!="UND" {print $8}' | sort -u > ch.txt
readelf -sW libc-buster.a | awk '($5=="GLOBAL"||$5=="WEAK") && $7!="UND" {print $8}' | sort -u > bg.txt
comm -23 ch.txt bg.txt
```

The `$7!="UND"` filter matters on both sides: counting undefined references as
definitions hides the bug.

## Definition of done

- A `[static N]` parameter no longer changes the linkage of the function that
  declares it, in any of the four spellings above.
- A focused fixture under `tests/` pins that such a function is callable from
  another translation unit -- a single-TU fixture cannot see this, since the
  call inside the unit is exactly what keeps the internal definition alive.
  `ide test` never names `tests/basic_c_*` fixtures directly, so the wiring
  into `driver_test.c` has to be proved by deliberately breaking the assertion
  and confirming it fails.
- `test_all` and `test_musl` pass; the musl baselines are rebaselined in the
  same change, with the compile count unmoved.

---

## forgejo#680 — c: an `aligned` attribute on one member declarator aligns the whole segment

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/680

An `aligned` attribute written on one member declarator raises the alignment of
every member the segment declares, not just the declarator that carries it.
Found 2026-08-29 while fixing #678, which made two more member-declarator
attribute positions parse and so made the divergence reachable from three
spellings instead of one.

```c
struct t1 { char c; int a, b __attribute__((aligned(32))); };
struct t2 { char c; int a __attribute__((aligned(32))), b; };
struct t3 { char c; __attribute__((aligned(32))) int a, b; };
// clang:  t1 size 64, a at 4,  b at 32
//         t2 size 64, a at 32, b at 36
//         t3 size 96, a at 32, b at 64
// buster: all three size 96, a at 32, b at 64
```

`t3` is the shared-specifier spelling and is right: an attribute before the
declarators belongs to all of them. `t1` and `t2` are wrong -- buster reads a
declarator's own list as if it had been written in the shared position.

This is an ABI divergence, not a missed optimization: `sizeof` and the offset
of every following member move.

## Where it is

`c_type_parse_aggregate_segment_step` in
`src/buster/lib/compiler/frontend/c/c_parse.c` pushes one
`C_TYPE_PARSE_FRAME_ALIGNMENT` child over the whole segment
`[frame->start, frame->end)`. That frame appends one `CAlignmentSpecifier` per
`_Alignas`/`aligned` word it finds anywhere in the range and hands back a
single `(alignment_start, alignment_count)` run, which every member row of the
segment then copies verbatim. The frame's scan uses `c_parse_alignment_word`,
which matches `aligned`/`__aligned__` inside a GNU attribute list as well as
`_Alignas`, so a declarator-position `aligned` lands in the shared run.

The records already carry `token_start`, so the segment step has what it needs
to partition the run: a record before `frame->shared_specifier_end` is shared,
one inside `[declarator_start, declarator_end)` belongs to that declarator
alone. The obstacle is that a member row names a *contiguous* run, and
"the shared ones plus this declarator's own" is not contiguous once a second
declarator carries a list. Re-appending a filtered run per member is the
obvious shape; it has to stay inside `alignment_capacity` and inside what
`c_type_parse_rollback` restores.

`packed` is deliberately segment-wide and must stay that way -- the comment
above the `c_parse_layout_attributes` call in the same function says so.

## Done when

`t1` and `t2` above compute clang's numbers, `t3` is unchanged,
`tests/basic_c_packed_layout.c` gains all three, and
`./build.sh build --config Release -t test_all` is green.

Oracle: `sizeof`/`offsetof` compared against clang.

Note that `tests/basic_c_packed_layout.c` today asserts two shapes from #678
whose attributed declarator sits where the two readings happen to agree
(`struct list_declarator_aligned`, whose first member is at offset zero).
Those assertions stay correct under the fix; they are just not the ones that
would have caught this.

---

## forgejo#681 — c: an attribute list on a declarator is refused outside an aggregate body

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/681

#678 fixed two attribute-list positions for *member* declarators. The same two
shapes are still refused outside an aggregate body, at file scope and at block
scope, by different code. Found 2026-08-29 while fixing #678, which did not
depend on either.

```c
// 1. trailing list on a parenthesized declarator -- file scope
void (*fp)(int) __attribute__((aligned(64)));
int g(void) { fp(1); return 1; }
// buster: use of undeclared identifier 'fp'
// clang:  accepted

// 2. the same at block scope
int g(void) { void (*fp)(int) __attribute__((aligned(64))) = 0; (void)fp; return 1; }
// buster: use of undeclared identifier 'fp'
// clang:  accepted

// 3. leading list on a declarator that is not the first of its list -- block scope
int g(void) { int a = 1, __attribute__((aligned(8))) b = 2; return a + b; }
// buster: use of undeclared identifier 'aligned'
// clang:  accepted
```

The same leading-list spelling at file scope (`int a, __attribute__((aligned(64))) b;`)
already works, which bounds shape 3 to the block-scope declarator loop.

Impact is a refused translation unit, not a wrong answer.

## Where it is

Shapes 1 and 2 are the file-scope and block-scope halves of what #678 fixed
inside `c_type_parse_aggregate_segment_step`: a
`C_TYPE_PARSE_FRAME_PARENTHESIZED` child is handed a range that still spans the
trailing attribute tokens, and the child requires the declarator to end exactly
at the range it is given. Both sites call
`c_parse_parenthesized_declaration_type` in
`src/buster/lib/compiler/frontend/c/c_parse.c` -- around line 8777 for file
scope, bounded by `c_parse_declarator_segment_end`, and around line 10994 for
block scope, bounded by its own `suffix_end`. #678 added
`c_parse_trailing_attribute_start`, which is exactly the trim these two need
before the call; the member site is the worked example.

Shape 3 is the block-scope declarator loop failing to skip a leading attribute
list on a declarator after a comma, the same omission #678 fixed in the
aggregate segment loop.

## Constraint

The same one #678 records: whatever range is trimmed must not swallow a
*sibling* declarator's trailing list. Both call sites already bound the range
at the top-level comma, so trimming inside that bound keeps the list with the
declarator that wrote it.

## Done when

All three shapes above compile and the attribute takes effect,
`tests/basic_c_packed_layout.c` gains them beside the member shapes #678 put
there, and `./build.sh build --config Release -t test_all` is green.

Oracles: `clang -fsyntax-only` for acceptance, `sizeof`/`_Alignof` and the
object's address modulo its alignment compared against clang for the effect.

---

## forgejo#688 — c: a packed attribute on one member declarator packs the whole segment

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/688

A `packed` attribute written on one member declarator packs every member the
segment declares, not just the declarator that carries it. Found 2026-08-29
while fixing #680 (PR 687), which fixed the same divergence for `aligned` and
deliberately left this one alone -- the comment above the
`c_parse_layout_attributes` call says `packed` is segment-wide on purpose, and
that is what needs revisiting rather than a bug hiding behind it.

```c
struct u6 { char c; int a __attribute__((packed)), b; };
struct u9 { char c; int a, b __attribute__((packed)); };
// clang and gcc:  u6 size 12, a at 1, b at 8
//                 u9 size 12, a at 4, b at 8
// buster:         u6 size  9, a at 1, b at 5
//                 u9 size  9, a at 1, b at 5
```

Both reference compilers agree, so this is an ABI divergence rather than a
dialect question: `sizeof` and the offset of every following member move. Note
`u9`, where buster packs `a` because a *later* declarator asked -- the attribute
reaches backwards.

## Where it is

`c_type_parse_aggregate_segment_step` in
`src/buster/lib/compiler/frontend/c/c_parse.c` calls `c_parse_layout_attributes`
once over `[frame->start, frame->end)` at `C_TYPE_PARSE_STAGE_BEGIN` and stores
the answer in `frame->is_packed`, which every member row of the segment copies
into `CMember.is_packed`.

PR 687 solved the identical partition for the alignment run and left the shape
to copy: the segment frame knows `frame->shared_specifier_end` and each
declarator's `[declarator_start, declarator_end)`, so `packed` is this member's
when the attribute sits before the shared end or inside this declarator's own
range. `packed` is a single bool rather than a run, so it needs none of the
re-appending `c_parse_member_alignment_run` does -- two scans, one over the
shared range and one over the declarator, OR'd together, is the whole change,
and it is the same two-range split `c_ir_declaration_is_noreturn` and the
`noreturn` member scan already use. Watch the ordering the existing comment
protects: the `packed` scan runs before the alignment child frame appends so
that the alignment run stays contiguous, and per-declarator scans must not
break that.

Note that the aggregate-wide spellings -- `struct __attribute__((packed)) P`
and `struct P { ... } __attribute__((packed))` -- are a different path
(`c_parse_aggregate_attributes`) and stay as they are.

## Done when

Both shapes above compute clang's numbers, the aggregate-wide and
`#pragma pack` shapes in `tests/basic_c_packed_layout.c` are unchanged, that
fixture gains both, and `./build.sh build --config Release -t test_all` and
`./build.sh test_self_host --config Release` are green.

Oracle: `sizeof`/`offsetof` compared against clang.

---

## forgejo#689 — c: an aligned(N) below the natural alignment is rejected instead of ignored

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/689

A GNU `aligned(N)` whose N is below the natural alignment of what it is written
on is rejected rather than ignored. GCC's `aligned` only ever *raises*
alignment -- lowering it needs `packed` as well -- so a request the type already
satisfies is a no-op for both reference compilers. Buster treats it as a hard
failure, and the failure is silent where it matters most. Found 2026-08-29 while
fixing #680 (PR 687), by an oracle probe that used `aligned(2)` on an `int` as a
harmless shared record; measured on `main` at 5e04db07 and unchanged by that PR.

```c
struct s1 { char c; __attribute__((aligned(2))) int a; };
struct s2 { char c; int a __attribute__((aligned(2))); };
int g __attribute__((aligned(2)));
```

- `sizeof(struct s1)` and `sizeof(struct s2)` fold to **4**. clang and gcc say
  8, with `a` at offset 4. No diagnostic is issued; the wrong number is simply
  handed to the program.
- `_Alignof(struct s1)` fails to lower:
  `in function 'main': could not lower logical expression core`.
- Defining an object of the type fails with an internal message:
  `C IR lowering cannot resolve definition 'g1' with C type 577 (kind 28) and IR
  type 32`.
- The object declarator `int g __attribute__((aligned(2)));` is rejected with
  `invalid object alignment`. clang and gcc accept it.

The silent `sizeof` is the serious one: a header that writes `aligned(2)` on
something already 4- or 8-aligned -- which is ordinary defensive style -- gets a
wrong layout with no warning.

## Where it is

Two engines evaluate the same records and are documented as having to agree:

- `c_ir_alignment_evaluate` in `src/buster/lib/compiler/frontend/c/c_gen.c`
  answers false when `requested < natural_alignment`, alongside the genuine
  rejections (not a power of two, wider than `UINT32_MAX`).
- `c_parse_layout_alignment_specifiers` in
  `src/buster/lib/compiler/frontend/c/c_parse.c` does the same for the `sizeof`
  folding, and its header comment states the rule it implements.

Both already compute `BUSTER_MAX(alignment, requested)`, so ignoring a small
request is what the max does on its own once the rejection is dropped -- the
work is deciding *which* spelling may be ignored, not the arithmetic.

`_Alignas` is not the same question and must not be folded into the same
answer. C requires the alignment of a declaration to be at least the natural
one, and clang rejects `_Alignas(2) int` with *requested alignment is less than
minimum alignment of 4 for type 'int'*. So the record needs to carry which
spelling produced it -- `CAlignmentSpecifier` has no such bit today, and
`c_parse_alignment_word` matches `_Alignas`, `aligned`, `__aligned` and
`__aligned__` through one predicate -- and a below-natural `_Alignas` should
become a diagnostic (`C_DIAGNOSTIC_INVALID_ALIGNMENT`) rather than the
layout-resolution failure it is now. Note that the aggregate-attribute path
(`c_parse_aggregate_attributes`) and the object-declarator path reach the same
two evaluators, so all four spellings above are one fix.

Whatever the resolution, the failure mode has to change: a rejected specifier
currently makes a layout unresolvable, and every symptom above is a different
component's way of reporting that it never got a layout. None of them names the
attribute.

## Done when

The four shapes above compute clang's numbers (8, 8, offset 4, and an accepted
object), `_Alignas(2) int` is diagnosed by name rather than failing to lower,
`tests/basic_c_packed_layout.c` gains the member and object shapes,
`./build.sh build --config Release -t test_all` and
`./build.sh test_self_host --config Release` are green.

Oracle: `sizeof`/`offsetof` compared against clang, plus clang's own diagnostic
for the `_Alignas` half.

---

## forgejo#691 — driver: -D NAME= defines NAME as 1 rather than as empty

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/691

`ide cc -D NAME=` -- a `-D` whose value is empty -- defines `NAME` as `1` instead of
as nothing.  Clang and GCC both define it as an empty replacement list; only a `-D`
with no `=` at all means `1`.

## Repro

```c
// dtest.c
int probe = 0 EMPTY;
```

```
$ ide cc -E -DEMPTY= dtest.c
int probe = 0 1 ;

$ clang -E -DEMPTY= dtest.c
int probe = 0 ;

$ gcc -E -DEMPTY= dtest.c
int probe = 0 ;
```

The no-`=` form agrees with the other two (`-DEMPTY` gives `1` everywhere), and a
non-empty value is passed through correctly (`-DEMPTY=7` gives `7`), so the fault
is specifically the empty right-hand side falling back to the no-value default.

## Impact

`-DNAME=` is the idiomatic way to compile a source twice with a decoration switched
off -- `-DATTR='__attribute__((aligned(64)))'` versus `-DATTR=` -- and every such
build fails with a syntax error at the expansion site rather than compiling the
plain form.  A configure-style probe that spells `-DHAVE_X=` also silently becomes
`-DHAVE_X=1`, which is a wrong answer rather than a refusal.

Found 2026-08-29 while building a negative control for #681, which did not depend
on it.

---

## forgejo#693 — c: an attribute after a bit-field width is read as part of the width

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/693

An attribute list written after a bit-field's width is swallowed into the
bit-width token range, so the width stops being a constant and the aggregate's
layout stops resolving. Found 2026-08-29 while fixing #688 (PR 692), which
oracled the member-declarator `packed` positions against clang; this is a
separate, pre-existing gap and reproduces identically on the pre-change
compiler.

```c
#include <stdio.h>
struct s { char c; int b : 5 __attribute__((packed)); char t; };
int main(void) { printf("%zu\n", _Alignof(struct s)); return 0; }
```

```text
cc: error: n.c:3:18: in function 'main': could not lower logical expression core
```

`offsetof(struct s, t)` on the same declaration reports
`invalid __builtin_offsetof type or member designator`, while
`sizeof(struct s)` folds and is correct. clang and gcc both accept the
declaration -- clang gives size 4, alignment 4, `t` at 2 -- and clang requires
this spelling: the attribute has to follow the width, `int b
__attribute__((packed)) : 5` is a syntax error there.

## Where it is

`c_type_parse_aggregate_segment_step` in
`src/buster/lib/compiler/frontend/c/c_parse.c`. When it sees the `:` it takes
everything to `frame->declarator_end` as the width:

```c
bit_width_token_start = declarator;
bit_width_token_count = frame->declarator_end - declarator;
```

so the attribute's tokens are part of the width expression. The
`bit_width_token_count == 1 && ... C_TOKEN_PREPROCESSING_NUMBER` fast path
that folds a plain literal therefore does not fire, `bit_width` stays 0, and
the token-range evaluation that runs later fails on the attribute tokens. The
fix is presumably to trim the trailing attribute list off the width range the
way the parenthesized declarator already trims one with
`c_parse_trailing_attribute_start`, and to let the trimmed tokens reach the
member's `packed`/`aligned` scans -- `packed` on a bit-field is meaningful and
already has an implementation, and `aligned` on one is already diagnosed.

## Done when

The declaration above computes clang's size, alignment and offset, a `packed`
written there packs the field, an `aligned` written there is diagnosed the way
one on a plain bit-field declarator already is, and
`tests/basic_c_packed_layout.c` gains the shape. `./build.sh build --config
Release -t test_all` and `./build.sh test_self_host --config Release` green.

Oracle: `sizeof`/`_Alignof`/`offsetof` compared against clang.

---

## forgejo#696 — c: an aligned attribute on a typedef is dropped

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/696

An `aligned(N)` attribute written on a typedef is parsed and dropped: the
name it declares keeps the alignment of the type it aliases, in both
directions, with no diagnostic. GCC and Clang both give the alias the
requested alignment, and for a typedef they honour it *below* the natural one
as well -- which is the one place `aligned` lowers without `packed`, because
it is setting the alignment of a type rather than raising a declaration's.
Found 2026-08-29 while fixing #689 (PR 695); measured on `main` at `c5c4ccf6`
and unchanged by that PR.

```c
typedef int raised __attribute__((aligned(16)));
typedef int lowered __attribute__((aligned(2)));
struct r { char c; raised a; };
struct l { char c; lowered a; };
char probe_size_r[sizeof(struct r)];
char probe_align_r[_Alignof(struct r)];
char probe_size_l[sizeof(struct l)];
char probe_align_raised[_Alignof(raised)];
```

| probe | clang | gcc | buster |
| --- | --- | --- | --- |
| `sizeof(struct r)` | 32 | 32 | 8 |
| `_Alignof(struct r)` | 16 | 16 | 4 |
| `sizeof(struct l)` | 6 | 6 | 8 |
| `_Alignof(raised)` | 16 | 16 | 4 |

Silent in every row, which is the same failure mode #689 had before the fix:
a header that hands out an aligned scalar type through a typedef -- the
ordinary spelling for a SIMD-aligned or cache-line-aligned scalar -- gets a
smaller alignment than it asked for, and an object of that type is
underaligned rather than diagnosed.

## Where it is

The specifier-position spelling is rejected by name already:
`c_parse_declaration_type_derive` (and `c_parse_local_declarations` for a
block-scope one) reports *alignment specifier cannot be applied to a typedef*
for `_Alignas(16) typedef int T;`, which is the C rule -- `_Alignas` may not
appear in a typedef declaration. The GNU attribute after the declarator is
the different question, and it reaches `c_parse_layout_attributes`, which
appends the record to the *declaration*; `declaration->alignment_count` is
then cleared for a `C_DECLARATION_TYPEDEF` beside that same diagnostic, so
nothing carries the request to the type the typedef names, and neither layout engine ever sees it: `CType` has no
alignment of its own, and both `c_ir_alignment_evaluate` and
`c_parse_layout_alignment_specifiers` read alignment runs off members,
declarations and aggregate attributes only.

So the fix is not in the evaluators -- they already compute
`BUSTER_MAX(alignment, requested)` over whatever run they are handed, and
after #689 they agree on which requests may be ignored. What is missing is a
place for a *type* to carry a requested alignment, and the rule that this one
replaces the natural alignment rather than raising it. The `noreturn` side
table added in #672 and the packed/aligned side table in #674 are the shape
this would follow: the population is a handful of typedefs per translation
unit against a type table of tens of thousands of entries.

Worth checking in the same change: `__attribute__((aligned(N)))` on a struct
tag used as a type name, and whether an object declared with the aligned
typedef gets the alignment as well as the aggregates that embed it.

## Done when

The four probes above compute clang's numbers, an object declared with an
aligned typedef is placed at the typedef's alignment,
`tests/basic_c_packed_layout.c` gains the typedef shape in both directions,
and `./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release` are green. Oracle:
`sizeof`/`offsetof`/`_Alignof` against clang, which gcc agrees with on every
row above.

---

## forgejo#697 — c: a packed bit-field narrower than its declared type is refused instead of read through a narrower unit

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/697

A packed bit-field is always read through a whole storage unit of its declared
type. When the aggregate is smaller than that type the unit hangs off the end
of the object, there is nowhere to slide it to, and the declaration is refused
rather than laid out. clang and gcc read the field through a narrower unit and
lay the object out fine, so this is a refusal on a declaration both reference
compilers accept.

```c
struct s { char c; int b : 5 __attribute__((packed)); char t; };
int main(void) { return (int)sizeof(struct s); }
```

```text
cc: error: n.c:1:24: packed bit-field 'b' straddles every storage unit of its declared type
```

clang and gcc both give size 3, alignment 1, `t` at offset 2. All three
spellings of the same layout reproduce it identically -- the attribute after
the width, the shared-specifier `__attribute__((packed)) int b : 5`, and
`struct __attribute__((packed))` on the aggregate -- so it is a lowering gap
rather than a parsing one, and it is pre-existing: it reproduces on a compiler
built before the change that closed #693, which is what turned the first spelling
from a parse failure into this one.

## Where it is

The slide loop at the end of the aggregate case of `c_ir_lower`, in
`src/buster/lib/compiler/frontend/c/c_gen.c` (search for the diagnostic's
text). Every packed bit-field whose unit does not already lie inside the
aggregate is slid back until it does, and the loop reports when it cannot:

```c
if (size < unit || lowest > highest)
```

For the declaration above the aggregate is three bytes and the declared type is
four, so no position for a four-byte unit exists and the whole definition is
refused. PR 674 introduced the slide deliberately -- a read-modify-write
through a unit that reaches past the object clobbers whatever follows it -- and
left this case diagnosed rather than silently wrong.

## What it would take

The access unit is the declared type's size everywhere, because a bit-field
member access emits a load and a store through the member's declared IR type at
`field->offset`; `IrField` carries no separate access width. clang picks the
narrowest unit that covers the field's bits and fits inside the object. Adding
that means an access-size field on `IrField` chosen by the same loop, plus the
five places that read a bit-field through its declared type:

- the runtime extraction in `c_ir_emit_load_place` (the shift/mask pair already
  promotes to 32 bits, so it only needs the raw load narrowed),
- the read-modify-write in `c_ir_emit_store_place`,
- the two constant-storage folds that OR a value in at `task.bit_offset`,
- the two designator initializer folds, which also bound `bit_offset` by
  `child->layout.size * 8`.

A narrower unit does not remove the refusal entirely -- a field spanning three
bytes at the very end of a three-byte object still has no power-of-two unit --
so the diagnostic and its test stay, over a shape that genuinely has none.

## Done when

The declaration above computes clang's size, alignment and offset, its fields
round-trip values, the shared-specifier and aggregate-level spellings of the
same layout do too, and the `straddling` case in `c_test_packed_and_aligned_layout`
still reports for a shape with no unit at all.
`./build.sh build --config Release -t test_all` and
`./build.sh test_self_host --config Release` green.

Oracle: `sizeof`/`_Alignof`/`offsetof` and value round-trips compared against
clang, which `tests/basic_c_packed_layout.c` already compiles cleanly under.

---

## forgejo#701 — c: an aligned attribute on one object declarator raises the whole list

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/701

An `aligned(N)` written on one declarator of an object declarator list is read
as the whole declaration's: every name in the list gets the alignment, not just
the one that carries the attribute. GCC and Clang both give it to that
declarator alone. Found 2026-08-29 while fixing #696 (PR pending); measured on
`main` at `6996a76b`.

```c
int c __attribute__((aligned(64))), d;
int main(void) { return (unsigned long)&d % 64 == 0; }
```

| probe | clang | gcc | buster |
| --- | --- | --- | --- |
| `&c % 64` | 0 | 0 | 0 |
| `&d % 64` | non-zero | non-zero | 0 |

The typedef spelling of the same list is worse than silent -- it drops the
whole declaration:

```c
typedef int a16 __attribute__((aligned(16))), plain_after;
```

> error: alignment specifier cannot be applied to a typedef

Both halves have one cause. `c_parse_declaration_type_derive` scans
`[declaration->token_start, name_index)` for the declaration's alignment
specifiers, and for a declarator that is not the first of its list that range
covers the *previous* declarators, their own attribute lists included. So the
second declarator inherits the first one's `aligned`, and in the typedef case
it inherits it into the specifier-position run that
`alignment specifier cannot be applied to a typedef` rejects by name -- the
diagnostic is right about the run it is handed and wrong about where the
attribute was written.

## Where it is

This is the object-declarator sibling of #680, which fixed exactly this for a
struct member: `c_type_parse_aggregate_segment_step` pushes one alignment frame
over a whole segment, and `c_parse_member_alignment_run` partitions the run by
`CAlignmentSpecifier.token_start` against the frame's `shared_specifier_end`
and each `[declarator_start, declarator_end)`. The shape to copy is that
partition; what is missing at file scope is the equivalent of
`shared_specifier_end`, which the derivation already knows -- the first
declarator's start -- but does not use to bound the specifier scan.

The block-scope path in `c_parse_local_declarations` bounds its specifier scan
at `declarator_start` already and scans each segment separately, so only the
file-scope path is wrong; a fix should keep them agreeing.

Worth checking in the same change: whether the *shared*-specifier spelling
(`__attribute__((aligned(64))) int c, d;`) still reaches every declarator after
the partition, which is the one case where it must, and whether the same
range is what `packed` and `noreturn` read at file scope.

## Done when

`&d` is not 64-byte aligned while `&c` is, the typedef list above compiles and
gives `a16` alone the alignment, `tests/basic_c_packed_layout.c` gains both
shapes, and `./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release` are green. Oracle: object
addresses and `_Alignof` against clang, which gcc agrees with.

---

## forgejo#703 — c: an array of an over-aligned element type is accepted instead of diagnosed

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/703

An array whose element alignment exceeds its element size is accepted and laid
out; GCC and Clang both refuse it. The shape became reachable with #696, which
gave a typedef an alignment of its own: before it, no scalar type in the
frontend could be aligned above its own size.

```c
typedef int cache_line __attribute__((aligned(64)));
typedef cache_line cache_array[2];
```

| compiler | answer |
| --- | --- |
| clang | error: size of array element of type 'cache_line' (aka 'int') (4 bytes) isn't a multiple of its alignment (64 bytes) |
| gcc | error: alignment of array elements is greater than element size |
| buster | accepted; `sizeof` 8, `_Alignof` 64 |

Eight bytes is the honest product of the element size and the count, and it is
also why the shape has to be refused rather than laid out: the second element
starts four bytes in, at an address the element type says it may not occupy, so
every access through `a[1]` is misaligned for the type that reaches it. The
same declaration through an object declarator (`cache_line a[2];`) and through
a struct member has the same problem.

## Where it is

The array type is built by `c_parse_array_suffixes`, which knows nothing about
layouts, so the check belongs to whoever does: `c_parse_type_layout` in
`c_parse.c` folds the size and `c_lower_to_ir` in `c_gen.c` builds the
`IrType`. `c_parse_type_layout` is the awkward one -- it is a query, it runs
inside speculative parses that get rolled back, and a diagnostic raised there
would be reported for a declarator the parse later abandons. The IR mapping
runs once over a settled type table and already has the element layout in hand
(the `C_TYPE_ARRAY` branch of the mapping pass), which makes it the cheaper
place to be right, at the cost of diagnosing a type only when something is
lowered through it.

Note that only a *lowered or raised scalar* can trip this: an aggregate padded
by `__attribute__((aligned(N)))` on its tag has its size rounded up to the
alignment, so `struct __attribute__((aligned(16))) s { char c; } a[2];` is
well-formed and thirty-two bytes. `CParseResult.type_alignments` names exactly
the types that can, which bounds the check.

## Done when

Both spellings above are diagnosed by name -- naming the attribute, the element
size and the alignment, the way both reference compilers do -- the object and
member spellings are diagnosed too, aggregates whose size is rounded up by
their own alignment keep compiling, `tests/basic_c_packed_layout.c` (or a
diagnostic fixture) gains the shape, and `./build.sh build --config Release
-t test_all` plus `./build.sh test_self_host --config Release` are green.
Oracle: clang's and gcc's own diagnostics.

---

## forgejo#705 — c: an aggregate initializer zeroes a member sharing a bit-field's storage unit

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/705

A brace initializer for a local aggregate writes each bit-field's storage unit
whole, so a plain member that shares that unit is overwritten with zero. It
only happens where a bit-field's unit reaches a non-bit-field member, which
packing is what makes possible.

```c
struct __attribute__((packed)) s { char c; int a : 5; int b : 7; char t; };
int main(void) { struct s v = { 16, -10, 63, 17 }; return v.c; }
```

clang and gcc both return 16; `ide cc` returns 0. `a` and `b` read back
correctly, and so does `t`; only `c` is lost.

The same declaration is right through every other path -- member assignments
(`v.c = 16; v.a = -10; ...`) and a static object with the same initializer both
give 16 -- so it is the local aggregate value, not the layout. It is
pre-existing: it reproduces identically on a compiler built before PR 704.

## Where it is

`machine_x64_select_bit_field_unit` in
`src/buster/lib/compiler/codegen/machine_x86_64.c`, and the identical
`machine_a64_select_bit_field_unit` in `machine_aarch64.c`. Lowering an
`IR_OPCODE_AGGREGATE` into the value's stack slot, they accumulate every
bit-field sharing a unit in a zeroed register and then **store the whole
unit**:

```c
u32 unit_register = machine_x64_synthesize_register(selector);
...
*zero_row = 0;
```

`a` and `b` here take the four-byte unit at offset zero -- three bytes hold no
`int` anywhere, so the unit slides back over `c`, which is exactly what the
slide loop is for -- and the unit store then wipes the byte `c` was written
into. The comment above the function states the assumption that no longer
holds: "Initializers materialize every member, so the accumulated word is the
whole unit." That is true of a unit containing only bit-fields; a slid or
narrowed unit can also contain an ordinary member.

## What it would take

Either seed the unit register with the bytes already in the slot instead of
zero (a load, an or, a store, which is what the store path does for one
field), or mask the store to the unit's bit-field bits. The second keeps the
single store but needs the members' spans, which the loop already walks.

Note the order dependence is not the fix: emitting the bit-field units before
the plain members would only move the loss to whichever member is written
first.

## Done when

The declaration above returns 16 under `ide cc` for x86-64 and AArch64, the
plain member survives whichever side of the bit-fields it is declared on, and
the bit-fields still read back their own values.

---

## forgejo#706 — c: a packed union sizes a bit-field member to its declared type

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/706

A packed union sizes to its widest member's declared type even when that member
is a bit-field narrower than the type. clang and gcc size it to the bits the
member actually occupies.

```c
union __attribute__((packed)) u { char c; int b : 5; };
int main(void) { return (int)sizeof(union u); }
```

clang and gcc both give 1; `ide cc` gives 4. The struct spelling of the same
question is right -- `struct __attribute__((packed)) { char c; int b : 5; }` is
two bytes under all three -- so it is the union branch of the layout alone. It
is pre-existing: it reproduces identically on a compiler built before PR 704.

## Where it is

The union arm of the member loop in the aggregate case of `c_ir_lower`, in
`src/buster/lib/compiler/frontend/c/c_gen.c`. A struct advances `bit_position`
by the member's bit width and a union takes the maximum, but the union arm is
only reached for a non-bit-field:

```c
bit_position = BUSTER_MAX(bit_position, field_type->layout.size * 8);
```

A bit-field goes down the struct path above it, which for a packed member sets
`offset`/`bit_offset` from the running `bit_position` -- fine for a struct,
wrong for a union, where every member starts at zero and the size is the widest
one. The size then falls out of the widest *declared type*, not the widest
member.

`c_parse_type_layout` in `c_parse.c` folds `sizeof` for the same declaration
and has to agree with whatever this becomes; the two layout engines disagreeing
is the failure mode the packed work has repeatedly hit.

## What it would take

Give a union bit-field its own arm: `offset` and `bit_offset` zero, and the
size candidate the field's own bits (`(bit_width + 7) / 8` under packing, the
declared type's size otherwise), maxed with the other members. Both engines
need it.

## Done when

`sizeof(union u)` above is 1 under `ide cc`, the same folded through
`_Static_assert`, `u.b` round-trips its 32 values, and the unpacked union
`union { char c; int b : 5; }` still sizes to 4 the way clang does.

Oracle: `sizeof`/`_Alignof` and value round-trips against clang, which
`tests/basic_c_packed_layout.c` already compiles cleanly under.

---

## forgejo#709 — c: a packed bit-field with no single storage unit is refused instead of split

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/709

A packed bit-field whose bits fit in no power-of-two storage unit that lies
inside the aggregate is diagnosed rather than laid out. Clang and GCC lay it
out and read it through a split access.

```c
struct __attribute__((packed)) s { long long b : 40; };
union __attribute__((packed)) u { long long b : 40; };
```

Both are five bytes under clang 22 and gcc; `ide cc` reports

```
packed bit-field 'b' straddles every storage unit of its declared type
```

for either spelling. Measured 2026-08-29 on the tree at PR 708 (`c: size a
packed union to the bits its bit-fields occupy`); the struct spelling has
been refused since PR 704 landed the narrowing slide, and the union spelling
became visible when PR 708 stopped oversizing packed unions -- before it, the
union above measured eight bytes, which fit the declared unit by accident and
disagreed with clang about the size instead.

## The set

For a bare bit-field of width W the aggregate is `ceil(W / 8)` bytes and the
unit has to be a power of two that both covers W bits and fits inside it, so
the refused widths are exactly those where `ceil(W / 8)` is not a power of
two. Measured against clang over a packed union of one `long long` field:

| width | clang | ide cc |
| --- | --- | --- |
| 8, 9, 16 | 1, 2, 2 | same |
| 17, 20, 24 | 3 | refused |
| 25, 32 | 4 | same |
| 33, 40, 48, 56 | 5, 5, 6, 7 | refused |
| 57, 64 | 8 | same |

A struct reaches the same widths with a leading member -- `struct
__attribute__((packed)) { char c; int b : 20; }` is four bytes with `b` at
bit 8, which no `int` unit covers either.

## Where it is

`c_lower_to_ir` in `src/buster/lib/compiler/frontend/c/c_gen.c`, the slide
loop that runs after the aggregate's size is known (search for `packed
bit-field '{S8}' straddles`). It tries the declared type's unit first and then
the ascending power-of-two candidates, and reports when none of them both
covers the field and fits inside the object. `IrField.access_size` carries the
narrowed width, zero meaning the declared type's size, and
`ir_field_access_size` is what its readers ask.

## What it would take

A field with no single unit needs more than one access: clang emits an `i40`
load, which LLVM lowers to a four-byte plus a one-byte access. That is a new
shape for every reader of `ir_field_access_size` -- the load and the
read-modify-write in `c_gen.c`, the four constant-initializer folds there, the
`IR_OPCODE_AGGREGATE` selectors in `machine_x86_64.c` and
`machine_aarch64.c`, and the two canonical emitters in `codegen.c` -- and for
`ir_place_narrow_bit_field_access`, which is the validation exception that
admits a `LOAD`/`STORE` disagreeing with its place's type. Refusing was
deliberate while the single-unit model was the whole model (AGENTS.md says
so); this issue is the other half of it.

## Done when

`sizeof` and a value round-trip of the two declarations above match clang for
every width in the table, including a neighbouring member that a wide store
must not clobber, under every allocator through `driver_test.c`. The refusal
and its test in `tests/basic_c_packed_layout.c` go away with it.

Oracle: `sizeof`/`_Alignof` and value round-trips against clang, plus the
cross-linked pair in `tests/basic_c_packed_layout_caller.c` /
`_callee.c`, which pins the layout to the platform's rather than to Buster's.

---

## forgejo#710 — c: a named bit-field of zero width is accepted

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/710

A bit-field with a name and a zero width is accepted. C requires the width of
a named bit-field to be greater than zero (C23 6.7.3.2p4), and both reference
compilers refuse it:

```c
struct s { char c; int b : 0; };
```

```
error: named bit-field 'b' has zero width          (clang 22)
error: zero width for bit-field 'b'                (gcc)
```

`ide cc` compiles it, laying `b` out as the zero-width member it is not
allowed to be: the struct measures 4/4, and `v.b = 0;` followed by a read of
`v.b` compiles and answers zero. The union spelling
`union __attribute__((packed)) { char c; int b : 0; }` is accepted the same
way and measures one byte.

Measured 2026-08-29 on the tree at PR 708. It is not specific to packing or to
unions -- the plain struct above is enough -- and it predates that PR: the
width is read the same way by both layout engines and neither asks whether the
member has a name.

## Where it is

The two arms that already distinguish a named zero-width bit-field for
alignment purposes are the places that know both facts at once:
`c_parse_type_layout` in `src/buster/lib/compiler/frontend/c/c_parse.c` and
the aggregate case of `c_lower_to_ir` in `c_gen.c` both guard the alignment
they take from a bit-field with `if (member.name.length)`, immediately above
the zero-width arm. The diagnostic belongs where the width is evaluated, so
the declaration fails rather than being laid out; `C_DIAGNOSTIC_INVALID_*` is
the kind, and the member's own location is what names it.

## Done when

The declaration above produces a structured C diagnostic and a failed driver
result, an unnamed `int : 0;` still lays out as it does today (it is what
`struct __attribute__((packed)) zero_width_bits` in
`tests/basic_c_packed_layout.c` covers), and a module test in `c_test.c`
pins both. A negative fixture belongs with the other rejected-declaration
cases rather than in `basic_c_packed_layout.c`, which has to keep compiling
under clang.

---

## forgejo#713 — c: an over-aligned array element escapes through a parenthesized declarator and an array type name

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/713

Two spellings of an array of an over-aligned element type escape the check
#703 added, because neither reaches the parse type table as a `C_TYPE_ARRAY`
at all. Measured against Clang 22 and GCC on 2026-08-29, on the branch of
#712.

```c
typedef int cache_line __attribute__((aligned(64)));

cache_line (*p)[2];                        // 1: parenthesized declarator
int f(void) { return (int)sizeof(cache_line[2]); }   // 2: array type name
int *g(void) { return (cache_line[2]){1, 2}; }       // 2: compound literal
```

| spelling | clang | gcc | buster |
| --- | --- | --- | --- |
| `cache_line (*p)[2];` | error at 2:16 | error | accepted |
| `sizeof(cache_line[2])` | error at 2:44 | error | accepted, folds 8 |
| `(cache_line[2]){1, 2}` | error at 2:34 | error | accepted |

Clang's message is the one #703 quotes: `size of array element of type
'cache_line' (aka 'int') (4 bytes) isn't a multiple of its alignment (64
bytes)`.

## Why they escape

The check added by #712 is a scan at the end of the type-mapping rounds in
`c_lower_to_ir` (`c_gen.c`), over every `C_TYPE_ARRAY` in `parse.types`. It
reads the *element's* mapping rather than the array's, which is why an array
whose own bound never resolved -- `extern cache_line inc[]`, a flexible array
member, a VLA -- is diagnosed anyway.

Instrumenting that scan to print every array type it visits shows **no array
type at all** for either spelling above, while the ordinary `cache_line
obj[2]` prints one. So this is not a gap in the scan's condition; the array
type is never added to the settled table:

- the parenthesized declarator `cache_line (*p)[2]` is modelled without a
  `C_TYPE_ARRAY` for the pointee. `sizeof(*p)` does not fold either -- it
  reports `static assertion expression is not an integer constant expression`
  -- which is a second, pre-existing symptom of the same thing.
- an array type name in an expression is resolved at lowering time by
  `c_ir_sizeof_operand_type_attempt` in `c_gen.c`, which builds `IrType`s
  directly and never goes through `c_parse_array_suffixes`. Note that path
  runs inside attempts that get rolled back, so a diagnostic raised there
  would need the same care `c_parse_type_layout` would have.

## Severity

Neither spelling allocates an object through the form that escapes: the
pointer declaration places no array, and the type name only sizes one. The
misalignment #703 is about needs an array to exist, and every spelling that
makes one is now refused. So this is a missing diagnostic rather than wrong
code, which is why #712 filed it instead of folding it in.

## Where to start

`c_gen.c`, the scan whose comment begins "An array element has to be
addressable at its own alignment in every slot" -- the condition to reuse is
`element->layout.size % element->layout.alignment != 0`, and
`CParseResult.type_alignments` being empty is what makes the whole question
skippable. The first question to answer is the parse-side one: whether
`cache_line (*p)[2]` should build a `C_TYPE_ARRAY` for its pointee at all,
since `sizeof(*p)` wants one too and that is the larger of the two defects.

## Done when

Both spellings are diagnosed with the message #712 introduced, at Clang's
columns; `sizeof(*p)` folds for a pointer to a well-formed array; the
well-formed neighbours in `tests/basic_c_packed_layout.c` still compile and
run; and `./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release` are green. Oracle: clang's and
gcc's own diagnostics.

---

## forgejo#714 — c: a qualified aligned typedef loses its alignment

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/714

Qualifying an aligned typedef drops the alignment the typedef asked for. The
alias is `_Alignof` 64, and `const` of it is `_Alignof` 4. Measured against
Clang 22 and GCC on 2026-08-29, on `claude/issue-696-pr-c79e3a` at `5afb094e`
(the branch of #707).

```c
typedef int cache_line __attribute__((aligned(64)));
struct s { char c; const cache_line f; };
_Static_assert(_Alignof(cache_line) == 64, "alias");        // passes
_Static_assert(_Alignof(struct s) == 64, "const member");   // fails on buster
_Static_assert(sizeof(struct s) == 128, "const member");    // fails on buster
```

| probe | clang | gcc | buster |
| --- | --- | --- | --- |
| `_Alignof(cache_line)` | 64 | 64 | 64 |
| `_Alignof(const cache_line)` | 64 | 64 | 4 |
| `_Alignof(volatile cache_line)` | 64 | 64 | 4 |
| `_Alignof(struct { char c; const cache_line f; })` | 64 | 64 | 4 |

This is the defect #696 was filed about, reached through a qualifier: a header
handing out a cache-line-aligned scalar produces an underaligned object the
moment a program writes `const` in front of it, with no diagnostic. Both layout
engines agree on the wrong answer, so `sizeof` folds to it as well.

## Where it is

`#707` records the request in `CParseResult.type_alignments`, keyed on a *copy*
of the type the typedef declarator arrived at, and the mapping pass in
`c_gen.c` turns that copy into an `c_ir_add_aligned_type` result. Instrumenting
the settled type table for `const cache_line q[2] = {1, 2};` prints four types:
the builtin `int`, the aligned alias copy, a second copy for the qualified
form, and the array. The IR mapping of that third type has size 4 and
**alignment 4** -- so the qualified copy is built from something that is not
the alias, or is built before the alias is remapped and never revisited.

`c_parse_type_layout` in `c_parse.c` answers the same way, which is why the
`_Static_assert` above folds rather than diagnosing.

## Why it is filed here rather than in #712

#712 refuses an array whose element size is not a multiple of its element
alignment. `const cache_line a[2]` is one such array in Clang and GCC, and
#712's scan does not report it -- correctly, on the numbers it is given: the
element it reads is alignment 4, so nothing is over-aligned. The shape starts
reporting on its own once this is fixed, with no change to #712.

## Done when

`_Alignof` and `sizeof` agree with Clang and GCC for `const` and `volatile` of
an aligned typedef, in a declaration, as a struct or union member and as an
array element, in both the raising and the lowering direction, in both layout
engines; `const cache_line a[2]` is then refused by the check #712 added, at
Clang's column; `tests/basic_c_packed_layout.c` gains the qualified shapes; and
`./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release` are green. Oracle: clang's and
gcc's `_Alignof`, `sizeof` and `__builtin_offsetof`.

---

## forgejo#715 — c: an aligned attribute among a typedef's specifiers is rejected

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/715

A GNU `aligned` attribute written among a typedef's declaration specifiers is
rejected by name, and the whole declaration is dropped:

```c
typedef int __attribute__((aligned(16))) t5, t6;
```

> error: alignment specifier cannot be applied to a typedef

Clang 22 and GCC both accept it and give *every* name of the list the
alignment, which is what a specifier-position attribute means. Measured
2026-08-29 against `main` at `ba43b46a`, plus the branch of #701.

| probe | clang | gcc | buster |
| --- | --- | --- | --- |
| `_Alignof(t5)` | 16 | 16 | (declaration refused) |
| `_Alignof(t6)` | 16 | 16 | (declaration refused) |

The declarator-position spelling of the same request works:
`typedef int t __attribute__((aligned(16)));` is honoured (#696, PR 707), and
a declarator list of them is partitioned correctly (#701).

## Where it is

`c_parse_declaration_type_derive` in
`src/buster/lib/compiler/frontend/c/c_parse.c` collects the declaration's
specifier-position alignment run and then rejects it by *position* alone:

```c
if (declaration->alignment_count && (declaration->kind == C_DECLARATION_FUNCTION || declaration->kind == C_DECLARATION_TYPEDEF))
```

The diagnostic is right about `_Alignas` -- it may not appear in a typedef
declaration at all, and Clang and GCC both refuse `typedef _Alignas(16) int t;`
-- and wrong about a GNU `aligned` attribute, which is an ordinary type
attribute in that position. The run does not carry its spelling, but it does
not have to: `c_alignment_specifier_is_standard` (same file) reads it back
from the token stream, and is already how #689's `_Alignas`-below-natural rule
tells the two apart.

The fix is to reject only the standard-spelled records and hand the rest to
the type, the way the declarator-position run is handed over in
`c_parse_declaration_type` -- and the reason those two ends are separate today
is that the specifier run is collected before the declarator's type is known,
so the move has to happen at the same place the declarator-position one does.
Note the shared run reaches *every* declarator of the list, which the
declarator-position one deliberately does not (#701), so
`c_parse_add_type_alignment` has to be called for each of them rather than
once.

The same question for a **function** is separate and buster is right: neither
reference compiler lets `aligned` change a function's alignment through this
spelling (they have `__attribute__((aligned))` on the function declarator for
that), so only the typedef arm of the condition is in question here.

## Done when

`typedef int __attribute__((aligned(16))) t5, t6;` compiles and gives both
names `_Alignof` 16; `typedef _Alignas(16) int t;` is still diagnosed by name;
`tests/basic_c_packed_layout.c` gains the shape beside the
`typedef_list_raised` group that #701 added; `./build.sh build --config
Release -t test_all` and `./build.sh test_self_host --config Release` are
green. Oracle: `_Alignof` against clang, which gcc agrees with.

---

## forgejo#719 — c: a store through a dereferenced pointer to an array is dropped

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/719

A store whose target is reached by dereferencing a pointer to an array is
accepted, compiles, and does nothing. Reads through the same expression are
correct, and the equivalent subscript spelling stores correctly. Measured
against Clang 22 on 2026-08-29.

```c
static int rows[2][3];

void store(void)
{
    int (*p)[3] = &rows[1];
    (*p)[1] = 7;        // dropped
    p[0][1] = 7;        // stores
}
```

| spelling | clang | buster |
| --- | --- | --- |
| `(*p)[i] = v` | stores | **dropped** |
| `*(*p + i) = v` | stores | **dropped** |
| `(*(p + 0))[i] = v` | stores | **dropped** |
| `p[0][i] = v` | stores | stores |
| `(*p)[i]` read | correct | correct |
| `*q = v` for `int *q` | stores | stores |
| `(*r).a = v` for `struct s *r` | stores | stores |

It is a silent wrong-code bug: no diagnostic, and the program keeps running
with the old value.

## Where it goes wrong

`void f(int (*p)[3], int v) { (*p)[1] = v; }` compiles to a copy of the whole
array into a stack temporary, and stores into the copy:

```text
mov    (%rcx),%rax           ; load rows[1][0..1]
mov    %rax,-0x20(%rbp)      ; into a frame temporary
mov    0x8(%rcx),%eax        ; load rows[1][2]
mov    %eax,-0x18(%rbp)
lea    -0x20(%rbp),%rdx      ; index the temporary
...
mov    %edi,(%rax)           ; store lands in the temporary
```

The sibling `p[0][1] = v` indexes `%rdi` itself and stores through it, which is
what the deref should have produced. So unary `*` on a pointer to an array is
being lowered as an rvalue load of the array rather than as a place, and the
subsequent subscript indexes that materialized copy. The array-to-pointer
decay that follows a correct `*p` never has to load anything.

## Severity and reachability

Pre-existing, and independent of the base type's spelling: it reproduces with
`int (*p)[3]` at block scope, which has always parsed. #713 makes it easier to
reach, since `T (*p)[3]` and `struct s (*p)[3]` at file scope only started
being declarations at all with that fix, but it is not caused by it.

`tests/basic_c_packed_layout.c` covers the well-formed pointer-to-array shapes
through the subscript spelling and reads for exactly this reason; switching
those lines to `(*p)[i] = v` is the regression test this wants.

## Done when

All three dropped spellings above store, `tests/basic_c_packed_layout.c` uses
the deref spelling for its pointer-to-array store, and
`./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release` are green. Oracle: Clang.

---

## forgejo#721 — abi: an aggregate holding a bit-field is passed in memory instead of registers

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/721

An aggregate that fits in two eightbytes and holds a bit-field is passed and
returned in memory rather than in registers, which is not what Clang and GCC
do with it. The disagreement is invisible inside one program and becomes a
wrong answer as soon as one half of a call is built by another compiler.

```c
struct __attribute__((packed)) a2 { char lead; int value : 20; char tail; };
struct a2 make2(void) { struct a2 r; r.lead = 7; r.value = 0x12345; r.tail = 9; return r; }
```

Five bytes, so System V classifies it INTEGER and returns it in `rax`; `ide cc`
returns it through a hidden pointer. Linking a Buster caller against a Clang
callee prints garbage where both halves built by the same compiler print
`7 12345 9`. Measured 2026-08-29 on `7a97299a` (`c: merge a bit-field's storage
unit instead of storing it whole`), so it predates the split-access work in
#709; the shapes that work today are the ones whose members happen to be
byte-aligned, which is why `basic_c_packed_layout`'s existing by-value records
pass.

## Where it is

`ir_system_v_abi_classes` in `src/buster/lib/compiler/ir/ir.c` walks the
aggregate one field at a time and asks each field's *declared* type where it
sits: it rejects the whole classification when `task.offset % field_alignment`
or when `task.offset + type->layout.size > 16`. A bit-field's declared type is
not what it occupies -- `int value : 20` occupies twenty bits at bit eight, not
four bytes at whatever the unit offset is -- so a bit-field whose storage unit
packing slid, or whose declared type is wider than the bits it holds, fails a
test that System V never applies to it. The rule the walk implements is the
one about *unaligned fields*, and bit-fields are not fields for that purpose:
the classifier should merge the classes of the eightbytes a bit-field's bits
fall in and leave its declared type out of it.

A split bit-field (#709) makes this reachable for every packed record with a
wide field, which is why `basic_c_packed_layout_caller.c` /
`_callee.c` pass the split record by address rather than by value.

## Done when

A packed record holding a bit-field is passed and returned the way the
platform compiler passes and returns it, checked by the cross-linked pair in
`tests/basic_c_packed_layout_caller.c` / `_callee.c` in both directions and
under every allocator, with the by-address form kept alongside so a
classification change cannot silently take the layout with it.

Oracle: the mixed link in `driver_test.c`, plus `objdump` of the callee's
return sequence against Clang's for the same declaration.

---

## forgejo#726 — c: _Atomic in a type name drops an aligned typedef's request, so one type has two alignments

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/726

`_Atomic` written directly in a type name is dropped, so the same type answers
two different alignments depending on how it is spelled. Measured against Clang
22 and GCC on 2026-08-29, on `claude/issue-714-qualified-aligned-typedef`
(the branch of #714) and on `main` at `d62acd21` alike — this is pre-existing
and #714 neither caused it nor changed it.

```c
typedef int cache_line __attribute__((aligned(64)));
typedef _Atomic cache_line atomic_alias;

_Static_assert(_Alignof(_Atomic cache_line) == _Alignof(atomic_alias), "same type");
```

| probe | clang | gcc | buster |
| --- | --- | --- | --- |
| `_Alignof(_Atomic cache_line)` | 4 | 64 | **64** |
| `_Alignof(const _Atomic cache_line)` | 4 | 64 | **64** |
| `_Alignof(atomic_alias)` | 4 | 64 | **4** |
| `_Alignof(struct { char c; _Atomic cache_line f; })` | 4 | 64 | 4 |

Clang and GCC each answer one number for all four; buster answers two. Whichever
of the two is chosen, one spelling of the type is wrong, and a program that
writes the inline form in one translation unit and the typedef in another gets
two layouts for one object.

## What is known

The reference compilers genuinely disagree about the *value*, so the value is a
second question and the two should be separated:

- **Clang** treats `_Atomic T` as constructing a type rather than qualifying
  one: it gets the alignment an atomic of that width gets, and the alias's
  `aligned` request does not travel into it.
- **GCC** keeps the request.
- Both agree that `_Atomic` written on a typedef whose base was *already*
  atomic keeps the request: `typedef _Atomic int a __attribute__((aligned(64)))`
  is 64 in both, and buster agrees (fixed on #714's branch).

#714 chose Clang for the paths it touched, because AGENTS.md names Clang the
oracle where the two disagree, and because it was already the answer this
frontend gave through a typedef. The inline spelling was left alone as
out of scope, which is what this issue is for.

## Where it is

`c_parse_add_qualified_type` in `src/buster/lib/compiler/frontend/c/c_parse.c`
is the one funnel that builds a qualified copy of a type, and after #714 it
declines to inherit the alignment record when the step *adds* `_Atomic`
(`atomic_applied`). The typedef spelling reaches it and answers 4. The inline
spelling in a type name answers 64, which is the alias's own number — the
number you get by never building a copy at all, so the `_Atomic` is most likely
being skipped before the copy is made rather than being applied and then
losing the record. `c_parse_atomic_declaration_prefix_token` and
`c_parse_skip_alignment_specifiers` are the two token-skipping helpers on that
path; start by instrumenting whether a copy is built for
`_Alignof(_Atomic cache_line)` at all.

Both layout engines agree on the wrong answer here — `c_parse_type_layout` in
`c_parse.c` folds the `_Static_assert` and `c_lower_to_ir` in `c_gen.c` folds
the same expression in a function body — so the fix belongs where the type is
built, not in either engine.

## Done when

`_Alignof` and `sizeof` give one answer for `_Atomic cache_line` however it is
spelled — inline in a type name, through a typedef of it, as a struct or union
member, and as an array element — and that answer is Clang's, matching the
choice #714 made. `tests/basic_c_packed_layout.c` gains the `_Atomic` shapes
next to the `const`/`volatile` ones #714 added, where a comment currently
records that `_Atomic` is deliberately absent because the references disagree;
that comment is what this issue replaces. `./build.sh build --config Release -t
test_all` plus `./build.sh test_self_host --config Release` green. Oracle:
Clang's `_Alignof`, `sizeof` and `__builtin_offsetof`, with GCC recorded beside
it as the second opinion rather than as the target.

---

## forgejo#728 — compat: attribute the five libc-test wrong answers

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/728

Part of #583. These five are the **only** `fail` state anywhere in libc-test
today, and the only unfinished clause of that issue's "reduce compiler defects
into focused tests" criterion. Nothing in the suite is blocked on a compile or
a link any more, `LIBCTEST_BLOCKER` prints nothing, and the work list this
stage generates for itself is otherwise empty.

Measured 2026-08-29 on `main` at `3dfa3653`, gate
`LIBC_TEST_EXPECTED_PASSING 238` / `LIBC_TEST_EXPECTED_STATE_HASH
0x0ccacdf0c3b8c365` in `build.c`.

```
./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test
```

## The five

| unit | what it exercises | first thing to look at |
| --- | --- | --- |
| `functional/tgmath` | `<tgmath.h>` type-generic macros: `lrint`, `sqrt`, `pow`, and `sizeof` of a call expression | the likeliest pure-codegen defect of the five — `sqrt(2.0f)` must be the `double` overload and `sizeof pow(sqrt(8),0.5f)` must be `sizeof(double)`; a wrong generic selection or a wrong float/double narrowing shows up here first |
| `functional/strftime` | musl's `strftime` against a table of formats and a file-scope designated-initializer `struct tm` | compare per-format, not per-program: `checkStrftime` reports every mismatching format on its own line |
| `functional/fcntl` | `struct flock` through `F_SETLK`/`F_GETLK`, `fork` + `waitpid` | a struct laid out or passed wrong across the syscall wrapper would read as a lock that is not seen by the child |
| `functional/mntent` | `getmntent`/`getmntent_r` parsing over a `tmpfile`, i.e. `sscanf` and string scanning | |
| `regression/sem_close-unmap` | `sem_open` twice, `sem_close` once, then `sem_post` on a mapping that must still be live | 19 lines; upstream's own comment names the invariant |

Each was blocked on a link until `vfprintf` compiled, so none is a regression
against an earlier pass, and none of the 22 units that came in with the x87
static-initializer work joined this list.

## Method

The reference is a Clang-built musl of the same configuration and it passes all
five, so the difference is in Buster-generated code, not in the workspace.
Per unit:

1. Run both sides by hand out of the run directory under
   `build/musl-v1.2.6-<pid>/` and diff the transcripts. A libc-test program is
   green when it exits zero and prints nothing, so `t_error` output names the
   failing assertion directly.
2. Narrow to one object: relink the unit with Clang-built musl members dropped
   in ahead of `libc-buster.a` until the transcript flips. That is what named
   `mmap`'s return in the `t_vmfill` hang and it is the fastest way to turn a
   whole-libc failure into one translation unit.
3. Reduce that unit to a `tests/basic_c_*.c` fixture, pinned under all four
   allocators the way `tests/basic_c_integer_to_pointer.c` is.
4. File the root cause as its own issue if it is not a one-line fix.

## Done when

Every one of the five either passes or is attributed to a filed defect with a
focused fixture, `LIBC_TEST_EXPECTED_PASSING` and
`LIBC_TEST_EXPECTED_STATE_HASH` are rebaselined for each unit that moves, and
`./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release` are green.

---

## forgejo#729 — driver: accept assembly input and build musl's x86-64 assembly units

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/729

Follow-on to #583. **Beyond that issue's acceptance criteria** — they ask that
excluded target-specific components be *reported*, and the harness reports every
one of these individually. This issue is about raising the ceiling they impose.

## What is excluded today

The Buster driver takes no assembly input, so `test_musl` reports 39 files it
cannot build, in two groups:

- **7 `assembly-only` units** — the `.c` file is empty because x86-64 supplies
  the implementation in assembly: `crt/crti`, `crt/crtn`,
  `src/setjmp/setjmp`, `src/setjmp/longjmp`, `src/signal/sigsetjmp`,
  `src/thread/syscall_cp`, `src/thread/tls`. There is no portable fallback;
  they are simply absent from `libc-buster.a`.
- **32 `architecture-assembly` files** — musl would prefer the `.s` over a
  portable C unit. The harness compiles the portable C instead and lists each
  one, so the archive is complete but is not the musl a native build produces.

## Why it is worth doing

The gap is what caps libc-test, on *both* sides: `fenv` is a stub and `clone`
is absent because they are architecture assembly in neither archive, which is
why **144 of 199 `src/math` units and the thread tests are
`excluded-reference`** rather than compared. Those runs are also most of the
suite's 62,6 seconds: the thread tests wait out the ten-second deadline. Every
unit this unblocks is a unit that starts comparing Buster's code generation
against Clang's instead of being held out of the comparison.

The encoder is already there. `assembly_encode` assembles module-level
`__asm__` with relocations (#645) and inline templates that name a symbol
(#685); what is missing is a front door and the directive vocabulary a real
`.s` file uses — `.globl`, `.hidden`, `.weak`, `.type`, `.size`, `.section`,
`.align`, local and numeric labels, and the `.cfi_*` family, which may be
accepted and ignored to start.

## Suggested shape

1. Teach the driver to take a `.s` (and `.S`, preprocessed first) input and
   emit an object, reusing the existing encoder and relocation plumbing.
2. Refuse — with a diagnostic naming the directive and its line — anything the
   vocabulary does not cover, the way every other unsupported construct in this
   tree is reported rather than silently dropped.
3. In `test_musl`, prefer musl's own `.s` over the portable C for the 32, build
   the 7, and give the Clang side the same treatment so the two archives stay
   the same configuration. Both counts and both archives move, so
   `MUSL_EXPECTED_COMPILED_UNITS`, `MUSL_EXPECTED_FAILURE_HASH`,
   `LIBC_TEST_EXPECTED_PASSING` and `LIBC_TEST_EXPECTED_STATE_HASH` are all
   deliberate rebaselines.

## Done when

The 39 files are built rather than reported, `libc-buster.a` is musl's own
x86-64 configuration, the `src/math` units that check floating-point exception
flags and the thread tests leave `excluded-reference`, and the four gates are
rebaselined with the new classification. `./build.sh build --config Release -t
test_all` and `./build.sh test_self_host --config Release` stay green.

---

## forgejo#730 — compat: link and run musl dynamically

Closed 2026-08-29. Original: https://code.buster14a.com/buster/buster/issues/730

Follow-on to #583. **Beyond that issue's acceptance criteria**, which ask for a
static hello-world and static libc-test programs. Everything in the harness
links `-static` today; nothing links dynamically.

## What is excluded today

Nine libc-test units are classified `excluded-dynamic`: upstream ships a
sibling `.mk` for them, which means they want shared objects, `-rdynamic`, or
an explicit do-not-run rule. They are held out of the comparison entirely, so
whatever Buster would do with them is unmeasured.

## Why it is reachable now

The pieces that used to block it have landed. `crt/rcrt1.c`, `ldso/dlstart.c`
and `ldso/dynlink.c` all **compile** since the inline-assembly relocation work
(#685) — they were the units that stopped on a template carrying a
RIP-relative `lea` with an output operand. `Scrt1.o` is produced beside
`crt1.o`. `__attribute__((alias))` reaches the object writer (#642), so the
250 weak symbols a dynamic musl publishes are present.

What is untested is the other half: producing a shared object at all
(`-shared`, PIC code generation, a `.dynsym`/`.dynstr`/`.rela.dyn` the loader
accepts), and then whether musl's own loader can relocate and run against it.
A compile inventory cannot see any of this — that is the standing lesson from
the alias work, where the archive's linkability gap moved the unit count by
exactly zero and only the freestanding probe found it.

## Suggested shape

1. Build `libc.so` from the same object set on both sides and link
   `tests/basic_musl_freestanding.c` dynamically against each, requiring the
   byte-identical transcript the static probe already requires.
2. Then let the nine `excluded-dynamic` units classify normally, and expect a
   new `LIBCTEST_BLOCKER` list to appear — the point of generating that list
   rather than maintaining it.

## Done when

A Buster-built `libc.so` loads and runs the freestanding probe under musl's own
`ldso`, the nine units are classified rather than excluded, and
`LIBC_TEST_EXPECTED_PASSING` / `LIBC_TEST_EXPECTED_STATE_HASH` are rebaselined.

---

## forgejo#731 — c: _Atomic of an aggregate is not padded to a power of two the way Clang pads it

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/731

`_Atomic` of an aggregate whose size is not a power of two keeps that size and
its natural alignment here, where Clang pads it to the next power of two and
aligns it there. Measured on 2026-08-29 against Clang 22.1.8 and GCC 16.2.1,
on `claude/optimistic-wilbur-b5de7d` (the branch of #726) and on `main` at
`f0790f7f` alike — this is pre-existing and #726 neither caused it nor changed
it. #726 found it while oracling the `_Atomic` shapes and left it alone
because it has nothing to do with an aligned typedef: it reproduces with no
attribute anywhere.

```c
struct three { char a, b, c; };
typedef struct three t3;
typedef _Atomic t3 at3;
```

| probe | clang | gcc | buster |
| --- | --- | --- | --- |
| `sizeof(_Atomic t3)` | 4 | 3 | **3** |
| `_Alignof(_Atomic t3)` | 4 | 1 | **1** |
| `sizeof(at3)` | 4 | 3 | **3** |
| `_Alignof(at3)` | 4 | 1 | **1** |

Buster agrees with itself in every spelling and agrees with GCC, so this is a
value question rather than the consistency question #726 was. It is filed
because AGENTS.md names Clang the oracle where the two references disagree, and
because the divergence is an ABI one: an `_Atomic struct` passed or laid out
across an object boundary is three bytes on one side and four on the other.

## What is known

- Clang rounds an atomic aggregate's size up to the next power of two and sets
  its alignment to that size, which is what makes the object lock-free-eligible
  for the `__atomic` builtins: a three-byte record has no three-byte atomic
  instruction, and a four-byte one does. The padding is part of the type, so
  `sizeof` sees it.
- GCC does not, and neither does this: `_Atomic T` is a qualified copy of `T`
  with `T`'s layout, which is what `c_ir_add_qualified_type` in `c_gen.c`
  builds (`IrType qualified = *base;`) and what the parse-side engine folds.
- Only sizes that are not already powers of two differ. `_Atomic` of a
  one-byte struct is 1/1 in all three, which is why `tests/basic_c_packed_layout.c`
  can pin the aggregate member shape it added in #726 against Clang today.
- The reachable range is small: 3, 5, 6, 7, and 9..15 bytes, then 17..31 and so
  on. Clang stops promoting above the target's maximum lock-free width and
  leaves larger aggregates alone, so the rule is not "round every aggregate
  up"; that ceiling is the part worth measuring before implementing.

## Where it is

`c_ir_add_qualified_type` in `src/buster/lib/compiler/frontend/c/c_gen.c` copies
the operand's layout wholesale, and `c_parse_type_layout` in `c_parse.c` folds
`sizeof`/`_Alignof` from the same layout. **Both layout engines have to agree**
or a folded `sizeof` contradicts the object it measures, which is the rule
AGENTS.md already states for `packed`/`aligned`. The System V classifier reads
the result as well: a promoted four-byte record is one INTEGER eightbyte where a
three-byte one already is, so the argument side may not move, but that has to be
checked rather than assumed.

## Done when

`sizeof` and `_Alignof` of `_Atomic S` match Clang for every aggregate size the
promotion reaches, in a type name, through a typedef, as a member, and as an
array element, in both layout engines, and the promotion stops where Clang's
does rather than growing every aggregate. Gate with `./build.sh build --config
Release -t test_all` plus `./build.sh test_self_host --config Release`; oracle
against Clang's `_Alignof`, `sizeof` and `__builtin_offsetof`, recording GCC
beside it as the second opinion. `tests/basic_c_packed_layout.c` is where the
`_Atomic` shapes live and where the promoted sizes belong next to the one-byte
aggregate that already agrees. Worth confirming first that musl, libc-test and
the compat harnesses have such an aggregate at all — if the population is empty,
the value of matching Clang here is the ABI statement rather than any program.

---

## forgejo#735 — frontend: assigning an aggregate to a volatile-qualified object is refused

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/735

Newly reachable now that `test_musl` builds musl's x86-64 assembly (#729).
`libc-test`'s `functional/setjmp` no longer compiles:

```
cc: error: src/functional/setjmp.c:33:11: in function 'main':
cannot convert IR type 23 (kind 11) to IR type 241 (kind 11)
```

Line 33 is `oldset = set2;`, where the destination is
`volatile sigset_t oldset` and the source is a plain `sigset_t set2`.
Assigning an aggregate to a `volatile`-qualified object of the same type
produces two distinct IR types for what is one struct, and the conversion is
refused.

Five-line repro:

```c
typedef struct { unsigned long bits[16]; } Set;
int main(void)
{
    volatile Set destination;
    Set source = {0};
    destination = source;
    return 0;
}
```

This unit was `excluded-reference` before: the Clang-built archive could not
run it either, because `setjmp`/`longjmp`/`sigsetjmp` are architecture
assembly and were in neither archive. Both archives hold them now, so the
reference passes and this is the only thing between Buster and the same
result.

---

## forgejo#736 — frontend: pthread_cancel-points' file-scope designated initializer is refused

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/736

Newly reachable now that `test_musl` builds musl's x86-64 assembly (#729).
`libc-test`'s `functional/pthread_cancel-points` no longer compiles:

```
cc: error: src/functional/pthread_cancel-points.c:103:3:
unsupported C global initializer for 'scenarios'
```

Line 103 is the initializer of a file-scope array of structs whose members
are function pointers and string literals, written with designators. The
diagnostic names the initializer rather than the construct inside it that the
folder cannot reduce, which is the first thing to narrow.

This unit was `excluded-reference` before: the Clang-built archive could not
run it either, because `clone` and `syscall_cp` are architecture assembly and
were in neither archive. Both archives hold them now, so the reference passes
and this is the only thing between Buster and the same result.

---

## forgejo#737 — codegen: three robust-mutex and semaphore libc-test units fail against the Buster archive

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/737

Newly reachable now that `test_musl` builds musl's x86-64 assembly (#729).
Three `libc-test` units compile, link and run against the Buster-built archive
but do not match the Clang reference:

- `functional/pthread_robust`
- `regression/pthread-robust-detach`
- `regression/sem_close-unmap`

Each exits non-zero where the reference exits zero, so by the harness's own
classification this is generated code rather than reach: `fail` is the only
state that means a defect in what Buster emitted.

All three are robust-mutex and semaphore paths, which share `__pthread_mutex_
timedlock`, the `__vm_lock` pair and the `pthread` cleanup stack. They were
all `excluded-reference` before, because `clone` is architecture assembly and
was in neither archive, so the thread tests hung out the ten-second deadline
on both sides.

Reproduce with the harness:

```sh
./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test
```

and look for the `LIBCTEST_UNIT ... state=fail` lines. The archive, the test
programs and both transcripts are left under
`build/musl-v1.2.6-<pid>/libc-test/`.

---

## forgejo#741 — c: a parenthesized group still copies the aggregate a member walk goes through

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/741

Follow-on to the fix for #737. That change stopped the expression walk in
`c_gen.c` from loading an intermediate aggregate member: in
`((T *)p)->a.b` the member `a` is only the route to `b`, so the walk now
keeps its place when the token after the member identifier is a `.`. The
two parenthesized spellings of the same walk still emit the copy, because
the `.` that follows is outside the group the load was emitted in.

## What still copies

```c
struct Inner { long v[5]; int x; };
struct Outer { int a; struct Inner in; };

int a(struct Outer *o) { return (*o).in.x; }              /* copies *o, 56 bytes */
void *b(void) { return &(((struct Outer *)0)->in).v[3]; } /* copies ->in, 48 bytes */
```

Both answers are right — `c_ir_emit_field_place_from_value` recovers the
place from the load's own operand, which is why the copy is dead rather
than wrong — and both are a real read of memory. Check with:

```sh
build/Release/ide cc -O2 -c -o /tmp/group.o /tmp/group.c
objdump -d --no-show-raw-insn /tmp/group.o
```

A dead load of a 48-byte aggregate is six `mov`s that nothing consumes.
`a` above is the shape a program is most likely to write; `b` is the
parenthesized offsetof, which would fault on the null pointer the way
#737 did.

## Where it comes from

The group is lowered by its own expression frame and hands back a value;
the member arm that follows it sees a loaded value rather than a place,
and the arm inside the group has already emitted the load by the time the
`.` is read. The fixed arm is the `C_PUNCTUATOR_DOT`/`C_PUNCTUATOR_ARROW`
block in `c_ir_lower_expression_core_step` (search for `member_place`), and
the recovery it leans on is the `IR_OPCODE_LOAD` branch of
`c_ir_emit_field_place_from_value`.

Fixing it means the group frame knowing that its result is about to be
walked into — a place-or-value request the frames do not carry today —
rather than another token peek.

## Definition of done

`tests/basic_c_member_chain_place.c` already runs both spellings for their
answers (cases 8 and the `pair` case); extend it with the parenthesized
offsetof, which faults when the copy is there. No lowered function should
hold a load of a struct or union type that nothing consumes, for either
spelling, under all four register allocators. Measured 2026-08-29.

---

## forgejo#743 — c: sizeof(void) is 0 and void * arithmetic does not step

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/743

GNU C gives `void` a size of 1 so that `void *` arithmetic steps by bytes, and
both reference compilers implement it. `ide cc` gives it 0, and the pointer
arithmetic that follows from it therefore does not move the pointer at all —
a silently wrong answer rather than a diagnostic.

```c
int printf(const char *, ...);
char buffer[16];
int main(void)
{
    void *p = buffer;
    printf("sizeof(void)=%d\n", (int)sizeof(void));
    printf("void* +3 steps %d\n", (int)((char *)(p + 3) - buffer));
    printf("sizeof(const void)=%d\n", (int)sizeof(const void));
    return 0;
}
```

```
clang    sizeof(void)=1   void* +3 steps 3   sizeof(const void)=1
ide cc   sizeof(void)=0   void* +3 steps 0   sizeof(const void)=0
```

Found while attributing `functional/tgmath` (#728): musl's `<tgmath.h>`
selects a type with `__typeof__(*(0 ? (t *)0 : (void *)!(c)))`, so `void` is
what the *unselected* arm of every one of its return casts evaluates to.
`sizeof` of it is not on that path — PR #742 asserts the selection with
`__builtin_types_compatible_p` for exactly that reason — but the probe that
established the conditional's result type turned this up beside it.

Nothing in musl or libc-test is blocked on it today; the second line is the
part that matters, because arithmetic on a `void *` is common GNU C and it
comes out as a no-op rather than an error.

---

## forgejo#746 — c: a compound literal used for its value is not folded in a static initializer

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/746

Found while implementing #736, which closed the *address* of a file-scope
compound literal (`&(int){0}`). The other half of the same construct is still
refused: a compound literal used for its **value** in a static initializer.

## Repro

Every one of these compiles with Clang and is refused by `ide cc`:

```c
static int          v  = (int){5};                    // scalar object
static void        *p  = (void *){0};                 // pointer-typed literal
static const char  *cs = (const char *){"lit"};       // pointer to a string literal

struct H { void *a; void *b; };
extern int target;
static struct H h = { (void *){0}, (void *){&target} };  // as an aggregate element
```

```
cc: error: v.c:1:16: C IR lowering: cannot fold a compound literal in a static initializer
```

The diagnostic already names the construct and its column, because #736 added
the element-naming fallback; what is missing is the fold.

A smaller sibling in the same family: a **subscript** after a compound literal,
`static int *p = &(int[]){1,2}[1];`. That one is a valid address constant and
gets its own refusal, `only a member designator after a compound literal is
folded in a static initializer`, from
`c_ir_static_compound_literal_address`.

## Why it is separate from #736

They are different mechanisms. #736 synthesizes a static object and hands back
its *symbol*, because `&(T){...}` is an address constant. These need the
literal's *bytes* folded into the object being initialized — no new symbol, no
relocation to the literal — which is the path
`c_ir_global_initializer` already takes for an aggregate object initialized by
a compound literal (`static struct S s = (struct S){1, 2};` works today via
`c_ir_initializer_compound_literal_info` and the `compound_aggregate_initializer`
branch). What is missing is the same treatment for a **scalar** destination.

## Where to start

- `c_ir_global_initializer` in `src/buster/lib/compiler/frontend/c/c_gen.c`.
  The aggregate case is the `compound_aggregate_initializer` branch; the
  non-aggregate paths below it never look for a compound literal, so
  `(int){5}` falls through every one of them.
- `c_ir_initializer_compound_literal_info(builder, start, end, &open, &close,
  &type_start, &type_end)` recognizes the shape and already strips redundant
  parentheses; `c_ir_compound_literal_type` resolves its type.
- The scalar leaf of an aggregate initializer is
  `c_ir_constant_initializer_bytes_legacy_core`, which brace-strips a `{...}`
  for a non-aggregate destination but does not know about the `( type-name )`
  in front of one.
- The likely fix is to narrow `start`/`end` to the literal's brace body once
  the literal's type is compatible with the destination, and let the folder
  that already exists run on it — the same rewrite the aggregate branch does.

## Do not retry

`c_ir_static_compound_literal_address` deliberately returns
`C_IR_STATIC_COMPOUND_LITERAL_ABSENT` for a non-array literal without an `&`
(see the comment there): those are values rather than addresses, and making
that helper claim them turns a shape it cannot handle into a refusal of its
own. Fix this in the value paths, not by widening that helper.

## Validation

`tests/basic_c_static_compound_literal.c` is the existing runtime fixture for
the address form; extend it (or add a sibling) so the value form is read back
at run time under all four register allocators, and cross-check the emitted
bytes against Clang with `llvm-objdump -s -r`. `c_test_static_compound_literal`
in `src/buster/tests/compiler/frontend/c/c_test.c` pins the refusals that are
left, by line and column, and will need its expected `diagnostic_count`
adjusted as shapes move from refused to folded.

## Definition of done

The four declarations above compile, run and produce Clang's answers, and no
libc-test or musl inventory count moves backwards
(`./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test`).

Measured 2026-08-29 on `claude/intelligent-feistel-e89b55`.

---

## forgejo#751 — codegen: emit initial-exec and general-dynamic thread-local models

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/751

Part of #583. Local-exec is the only thread-local model the compiler emits, and
it is what the last six non-passing libc-test units have in common. Measured on
`main` at `6dbdab27`, gate `LIBC_TEST_EXPECTED_PASSING 381` /
`LIBC_TEST_EXPECTED_STATE_HASH 0x0e973f6c67fa1195`, which this tree reproduces
exactly.

## What only exists today

`CODEGEN_MODULE_RELOCATION_X86_64_TPOFF32` is the whole vocabulary
(`src/buster/lib/compiler/codegen/codegen.h:209`, emitted at
`codegen.c:9238` and `codegen.c:10490`, carried through
`OBJECT_RELOCATION_X86_64_TPOFF32`). There is no `R_X86_64_GOTTPOFF`
(initial-exec) and no `R_X86_64_TLSGD` / `R_X86_64_DTPMOD64` /
`R_X86_64_DTPOFF64` with a `__tls_get_addr` call (general-dynamic). A
thread-local access is therefore always a constant offset from `%fs`, which is
correct only for the main executable's own TLS block.

## The six units

Two are a wrong answer:

```
LIBCTEST_UNIT unit=functional/tls_align       state=fail detail=non-zero exit
LIBCTEST_UNIT unit=functional/tls_align_dlopen state=fail detail=non-zero exit
```

Both read a shared object's thread-local storage through local-exec offsets and
get the wrong addresses. The program runs to completion and reports mismatches
rather than faulting, which is why these are `fail` rather than `blocked-*`.

Two are a link the linker refuses outright:

```
ld: .../147.o: relocation R_X86_64_TPOFF32 against symbol `tls' can not be used
    when making a shared object; local-exec is incompatible with -shared
ld: .../420.o: relocation R_X86_64_TPOFF32 against symbol `v' can not be used
    when making a shared object; local-exec is incompatible with -shared
```

— `functional/tls_init_dso` and `regression/tls_get_new-dtv_dso`, the two
`.mk` units that build a shared object of their own.

Two more fall out of those:

```
LIBCTEST_UNIT unit=functional/tls_init_dlopen  state=blocked-link
  detail=the sibling shared object tls_init_dso was not built
LIBCTEST_UNIT unit=regression/tls_get_new-dtv  state=blocked-link
  detail=the sibling shared object tls_get_new-dtv_dso was not built
```

## Scope

- Initial-exec: `R_X86_64_GOTTPOFF`, a GOT slot the loader fills with the
  offset, for a thread-local in a library loaded at startup.
- General-dynamic: the `__tls_get_addr` sequence with `R_X86_64_DTPMOD64` /
  `R_X86_64_DTPOFF64`, for one in a library `dlopen`ed later — which is what
  `tls_align_dlopen` and `tls_get_new-dtv` actually test.
- A model choice: local-exec for an executable's own definitions, and one of
  the two above under whatever spelling `-fPIC` grows in #752.

The Clang reference builds and passes all six, so each one is a direct
differential: the harness already links each side's own shared object and runs
both.

## Gate

`./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test`.
Closing this takes 381 to 387 of 424 and leaves `functional/dlopen_dso` as the
only non-passing unit that is not the reference's own ceiling. Rebaseline
`LIBC_TEST_EXPECTED_PASSING` and `LIBC_TEST_EXPECTED_STATE_HASH` in `build.c`
and record what moved, the way every entry in that comment block does.

Do not weaken the local-exec path to get here: `tests/` pins `@TPOFF` output in
`driver_test.c:1798` and the executable case is the common one.

---

## forgejo#752 — codegen: -fPIC is accepted and ignored, so no object can be interposed

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/752

Part of #583. `-fPIC` is accepted and ignored, so the compiler has one code
model and it is the executable's. Measured on `main` at `6dbdab27`, gate
`LIBC_TEST_EXPECTED_PASSING 381` / `LIBC_TEST_EXPECTED_STATE_HASH
0x0e973f6c67fa1195`.

## Where the flag goes

`src/buster/lib/compiler/driver/driver.c:1278` puts `-fPIC`, `-fpic`, `-fPIE`,
`-fpie`, `-fno-pic` and `-fno-pie` in `compatible_codegen_option` — the list of
flags accepted so that one flag set can drive this compiler and the reference
one. For `-fno-stack-protector` that is honest: Buster emits no canary either
way. For `-fPIC` it is not — the emitted code is the same non-PIC code, and the
difference only shows up at the link.

## What it costs

```
ld: .../86.o: relocation R_X86_64_PC32 against symbol `i' can not be used when
    making a shared object; recompile with -fPIC
```

`functional/dlopen_dso` is `blocked-link` on exactly that, and it is the last
non-passing libc-test unit outside the thread-local group (#751) and the
reference's own ceiling. What `ld` refuses is a PC-relative reference to a data
symbol another object could interpose; a real `-fPIC` routes those through the
GOT (`R_X86_64_GOTPCREL` / `REX_GOTPCRELX` — the object writer already names
them, `object.c:3555`, but nothing emits them).

## What it would retire

The shared-musl stage carries two workarounds that exist only because there is
no `-fPIC`, both written down in `AGENTS.md`:

- `-Bsymbolic` on every `ld -shared`, taking the whole symbol set that musl's
  own build binds selectively through `--dynamic-list`. That costs the copy
  relocations that list exists to preserve, and the harness gets away with it
  only because its probe references no libc data object.
- A read-only global that carries a relocation is laid out with the writable
  data instead of in `.rodata`. Twenty-one musl units hold one —
  `__ctype_b_loc`'s `ptable`, the `FILE *const stdout` trio, the locale tables
  — and without that move each is a write-when-relocated word on a read-only
  page, a `DT_TEXTREL` musl's loader will not undo for the file it was itself
  started from.

Neither has to be removed by this change, but both stop being forced by it, and
`MUSL_SHARED` would then be a statement about position-independent code rather
than about code that happens to link.

## Scope

- Honor `-fPIC`/`-fpic` as a code model rather than absorbing it: GOT-indirect
  addressing for a symbol that could be interposed, PC-relative kept for
  hidden and local ones.
- `-fPIE`/`-fpie` may stay a no-op or share the path; say which and why.
- The four allocators all have to produce it — the harness gates the dynamic
  probe's transcript byte for byte under each.

## Gate

`./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test`
takes `functional/dlopen_dso` from `blocked-link` to a state the reference can
be compared against; rebaseline `LIBC_TEST_EXPECTED_PASSING` and
`LIBC_TEST_EXPECTED_STATE_HASH` in `build.c` and record what moved.

---

## forgejo#753 — compat: take musl's architecture C sources, or report every one replaced

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/753

Part of #583, and the one acceptance clause of that issue still unmet: "report
every excluded target-specific component."

## The gap

`test_musl`'s manifest applies musl's replacement rule for **assembly** only:
every `.c` in a source directory and every `.s` in its architecture
subdirectory, with the `.s` winning where both name the same unit
(`build.c:16095`). musl's own `ARCH_SRCS` also covers the architecture
subdirectories' **`.c`** files, and there are eighteen of them for x86-64:

```
src/math/x86_64/{fabs,fabsf,fabsl,fma,fmaf,fmodl,llrint,llrintf,llrintl,
                 lrint,lrintf,lrintl,remainderl,remquol,rintl,sqrt,sqrtf,sqrtl}.c
```

The harness still builds the portable sibling for each. That is not a
substitution the run reports anywhere — there is no `MUSL_UNSUPPORTED` line, no
`MUSL_ASSEMBLY`-style inventory line, nothing — because the manifest never
looks for them. `MUSL_SUMMARY` says `unsupported=0` and it is true of what the
manifest contains rather than of what musl's build would produce.

## Why it is worth closing rather than documenting

All eighteen are inline-assembly implementations over x87 and SSE: `sqrtl` is a
bare `fsqrt`, `fmodl` a `fprem` loop, `remquol` reads the x87 condition codes
back out, the `lrint` family is a single `cvt`. They exercise the inline-asm
path and the x87 register model harder than anything else in the tree, against
a reference that compiles all eighteen. Today none of it is measured.

## Scope

- Extend the manifest to musl's full `ARCH_SRCS` rule, so an architecture `.c`
  replaces its portable sibling the way an architecture `.s` already does.
- Report each replacement, the way `MUSL_ASSEMBLY` reports the 32 assembly
  units — inventory, not exclusion.
- Anything that then fails to compile becomes a `MUSL_UNSUPPORTED` line and its
  own reduced fixture, which is what the rest of this harness is for.

## Gate

`./build/build test_musl --config Release ~/dev/musl-v1.2.6`. The unit count
moves off 1356 and the failure hash may stop being the hash of nothing:
rebaseline `MUSL_EXPECTED_COMPILED_UNITS` and `MUSL_EXPECTED_FAILURE_HASH` in
`build.c` and record what changed, per the comment block above them. Re-run with
a libc-test path too — `src/math` is 168 passing today and these are its
implementations.

---

## forgejo#754 — compat: musl and libc-test compile under one register allocator only

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/754

Part of #583, acceptance clause "Add FAST and NONE coverage where applicable."
Half of it is met; this is the other half.

## Where each mode is exercised today

- **Both compile inventories** — 1356 musl units and all 424 libc-test units —
  run under one register allocator, `fast`
  (`MUSL_COMPATIBILITY_ALLOCATOR_MODE`, `build.c:711`; the libc-test side builds
  the same flag at `build.c:17422`). `MUSL_COMPILE allocator=fast` and every
  `LIBCTEST_SUBSET` line describe that single mode.
- **The two freestanding probes** — static and dynamic — run all four
  (`fast`, `none`, `mir-stack`, `quality`), each required to reproduce the
  Clang reference transcript byte for byte. That is one program.

So NONE's coverage against a whole libc is a single ~2 KB transcript, while
FAST's is 424 test programs. A NONE-only code-generation defect anywhere in
musl is invisible here.

## Scope

Pick one and say why:

- Run one libc-test subset — `src/functional` is 77 units and 2,2 s of run time
  — under a second allocator mode, and gate it separately.
- Or compile the musl inventory a second time under `none` and gate the unit
  count, without linking or running it, which costs a compile pass and no run
  time.

The first is real coverage and costs ~11 s of the run; the second is cheap and
catches only refusals. The whole-suite-under-four-modes version is not
proposed: libc-test compile time is 33 s of the 210 s run and this would
quadruple it.

## Notes

- The mode is a per-compile flag, so this is a loop, not a new stage; the
  gate needs its own expected count and state hash rather than folding into
  `LIBC_TEST_EXPECTED_*`, or a mode difference and a real regression become
  the same failure.
- `MUSL_COMPATIBILITY_ALLOCATOR` (`build.c:706`) is musl's allocator
  (`mallocng`) and is unrelated — the two names sit two lines apart and are
  easy to confuse.

---

## forgejo#755 — c: a subscript after a compound literal is not folded in a static address constant

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/755

Found while closing #746, which folded a compound literal used for its
*value*. This one is the address family that #736 opened, and it is a
different mechanism from either: a **subscript step after a compound
literal** in a static address constant.

## Repro

Both compile with Clang and are refused by `ide cc`:

```c
static int *ip = &(int[]){1, 2}[1];

struct S { int arr[2]; };
static int *m = &(struct S){{1, 2}}.arr[1];
```

```
t.c:1:31: C IR lowering: only a member designator after a compound literal is folded in a static initializer
t.c:1:62: C IR lowering: only a member designator after a compound literal is folded in a static initializer
```

The refusal names what is supported rather than claiming the program is
wrong, which is correct as far as it goes -- these are valid address
constants and the compiler simply does not fold them.

## Why it is separate from #746

#746 was the *value* half: the literal's bytes are folded into the object
being initialized, no symbol and no relocation, and it was fixed by
narrowing the initializer range to the literal's brace body
(`c_ir_initializer_narrow_compound_literal_value`). That rewrite cannot
reach this shape at all. A subscript after the literal is an *address*: the
same synthesized literal object, at a different offset. It belongs to
`c_ir_static_compound_literal_address`, which already synthesizes the
object and already computes an addend.

## Where to start

All in `src/buster/lib/compiler/frontend/c/c_gen.c`.

- `c_ir_static_compound_literal_address` is the whole mechanism. After it
  has the literal's type and its `close` brace, it walks the tokens from
  `close + 1` to `end` in a loop that steps `index += 2` over `. identifier`
  pairs, accumulating `field->offset` into `addend` and following
  `field->type` into `current`. The refusal above is that loop's `else`
  arm.
- The extension is a second step shape in the same loop: a
  `C_PUNCTUATOR_LEFT_BRACKET`, its matching `]` through
  `c_ir_matching_delimiter`, a constant index from
  `c_ir_integer_constant_evaluate`, and `addend += index * element size`
  with `current` following `element_type`. `current->kind` must be
  `IR_TYPE_ARRAY` for it, the way the member arm requires a struct or
  union, and the loop's fixed `index += 2` stride has to become a cursor
  the arm advances itself.
- Keep the overflow guards the member arm has: it checks
  `field->offset > INT64_MAX` and `addend > INT64_MAX - (s64)field->offset`
  before it adds, and a subscript multiplies, so the product needs its own
  bound.
- A negative or out-of-range subscript is not a reason to fold something
  else: refuse it with the existing `c_ir_constant_initializer_fail` rather
  than wrapping.

## Do not retry

The carve-out at the top of `c_ir_static_compound_literal_address` stays:
it returns `C_IR_STATIC_COMPOUND_LITERAL_ABSENT` for a non-array literal
without an `&`, because those are values rather than addresses and #746's
value fold is what handles them. Widening that test turns a shape this
helper cannot handle into a refusal of its own.

## Validation

`tests/basic_c_static_compound_literal.c` is the runtime fixture for both
halves of the construct and runs under all four register allocators; it
already holds `&(struct Inner){...}.y`, so the subscript rows belong beside
it, read back through the pointer rather than trusted from a diagnostic. A
wrong addend still produces a program, so cross-check the relocation with
`llvm-readelf -r` against Clang's: the symbol is the literal object and the
addend is the offset.

`c_test_static_compound_literal` in
`src/buster/tests/compiler/frontend/c/c_test.c` pins the refusals that are
left by line and column, and pins that the module holds exactly the four
literal objects the address form needs. Both counts move when a shape stops
being refused.

## Definition of done

Both declarations above compile, run and produce Clang's answers, and no
libc-test or musl inventory count moves backwards
(`./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test`).

Measured 2026-08-30 on `claude/elastic-jennings-66967f`.

---

## forgejo#756 — c: a compound literal is not folded when it converts, or when it is an operand

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/756

Left open by #746, which folded a compound literal used for its value into
the object being initialized. That fix narrows the initializer range to the
literal's brace body and lets the existing folder run, which is exact only
while the literal's type is *compatible* with the destination. Two families
are still refused.

## Repro

All four compile with Clang and are refused by `ide cc`:

```c
static long     w  = (int){5};      // t.c:1:17
static unsigned u  = (int){5};      // t.c:1:21
static double   dd = (float){0.1};  // t.c:1:20
static int      a  = (int){5} + 1;  // t.c:1:16
```

```
C IR lowering: cannot fold a compound literal in a static initializer
```

The first three are one conversion each: the literal's value is converted
to the destination type on the way in (C 6.7.9p11). The fourth is the
general case -- the literal is an operand of a constant expression, and
nothing about the construct says it has to be the whole initializer.

## Why the narrowing cannot do it

`c_ir_initializer_narrow_compound_literal_value` in
`src/buster/lib/compiler/frontend/c/c_gen.c` drops the `( type-name )` and
hands the brace body to the folder that was going to run anyway. Once the
type name is gone the conversion has nowhere to happen, and folding the
body straight into the destination gives the wrong answer where the
literal's own type is narrower than the destination: `(unsigned char){300}`
is 44 in Clang and would be 300, and `(float){0.1}` widened to `double` is
0.10000000149011612 and would be 0.1. The predicate is therefore
deliberately `c_ir_types_compatible`, and an incompatible literal is left
alone rather than folded wrong. It is also what keeps `(int[]){1, 2}`
initializing an `int *` -- an array decaying to a pointer, an address
rather than a value -- reaching `c_ir_static_compound_literal_address`.

## Where to start

The general answer is a compound-literal operand in the constant
evaluator, which is where `(int){5} + 1` has to be handled anyway and where
the conversion has a type to happen at.

- `c_ir_constant_evaluate` -> `c_ir_constant_evaluate_attempt` ->
  `c_ir_constant_evaluate_impl` in
  `src/buster/lib/compiler/frontend/c/c_gen.c` is a shunting-yard
  evaluator over `CIrConstantValue`, driven by the suspendable query
  machine (`C_IR_QUERY_FRAME_CONSTANT`, dispatched in `c_ir_query_step`).
  An operand arm that recognizes `( type-name ) { ... }` needs a
  sub-query for the type name -- `c_ir_compound_literal_type` is already
  a query frame, `C_IR_QUERY_FRAME_COMPOUND_TYPE` -- and a nested constant
  evaluation of the brace body, both of which must resume the way the
  existing sub-queries do rather than recurse.
- `c_ir_constant_cast` is the conversion, applied once at the literal's
  type; the destination's own conversion is the one the callers already
  apply to whatever the evaluator returns.
- `c_ir_initializer_compound_literal_info` recognizes the shape and strips
  redundant parentheses; it is what the narrowing uses.

## What the narrowing must keep

Do not delete `c_ir_initializer_narrow_compound_literal_value` when the
evaluator grows the operand. It covers three shapes the evaluator does not
reach, because they are not `CIrConstantValue`s:

- `(const char *){"lit"}`, which folds through
  `c_ir_global_string_pointer_initializer`.
- an 80-bit x87 literal, which folds through `c_ir_ext80_global_literal`
  and carries more significand than the `f64` the evaluator works in.
- the C23 empty literal `(int){}`, which is a zero initializer rather than
  an expression.

An aggregate literal initializing an aggregate object is a fourth: it goes
through `c_ir_constant_initializer_context_begin`, which does the same
narrowing for its own reasons.

## Validation

`tests/basic_c_static_compound_literal.c` reads the value form back at run
time under all four register allocators; the converting rows belong there,
with the answers Clang produces rather than the ones the literal's body
spells -- `(unsigned char){300}` reading back as 44 is the row that fails
if the conversion is skipped rather than applied.
`c_test_static_compound_literal` in
`src/buster/tests/compiler/frontend/c/c_test.c` pins the remaining
refusals by line and column and pins the count of synthesized literal
objects, which must not move: a value form that started taking the address
path would show up as an extra object.

## Definition of done

The four declarations above compile, run and produce Clang's answers, and
no libc-test or musl inventory count moves backwards
(`./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test`).

Measured 2026-08-30 on `claude/elastic-jennings-66967f`.

---

## forgejo#760 — c: __typeof__ of a conditional expression resolves to no type

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/760

`__typeof__` over a conditional expression resolves to no type at all, so a
declaration written on it declares nothing and every later use of the name is
an undeclared-identifier error. There is no `void` in the shape and no
diagnostic naming the `__typeof__`:

```c
int main(void)
{
    __typeof__(*(0 ? (double *)0 : (char *)0)) a = 1.5;
    return (int)sizeof(a);
}
```

```
clang   compiles (warns -Wpointer-type-mismatch), sizeof(a) == 8
gcc     compiles, sizeof(a) == 8
ide cc  error: use of undeclared identifier 'a'
```

The conditional is the whole trigger — `__typeof__(*(double *)0)` beside it
resolves fine, and so does `__typeof__` of an ordinary identifier. Measured
2026-08-30 against clang 21 and gcc 15 on x86-64 Linux, on `main` at
`756ee3c2`.

This is the shape musl's `<tgmath.h>` is written in:
`__typeof__(*(0 ? (t *)0 : (void *)!(c)))`, where the conditional selects `t *`
because the other arm is a null pointer constant, and the `*` names the type
the macro is generic over. It is a plausible first thing to look at for
`functional/tgmath`, the unit #728 lists first and describes as the likeliest
pure-codegen defect of its five — this is not codegen at all, and a macro that
selects no type would explain a wrong overload rather than a wrong narrowing.

Found while fixing #743 (`sizeof(void)` was 0, so `void *` arithmetic did not
step); PR #759 fixes that and leaves this untouched, because giving `void` a
size does not make the conditional resolve — the spelling without any `void` in
it fails identically.

## Where to start

`c_ir_sizeof_operand_type_attempt` and the `__typeof__` operand walk in
`c_gen.c` are what resolve a type name in an expression; the conditional's
result type is `c_parse_conditional_expression_type` in `c_parse.c`, which is
the engine `c_test_conditional_type_prediction` already covers. The question is
which of the two gives up: whether the conditional's composite pointer type is
never computed, or whether it is computed and the `__typeof__` walk cannot take
a type out of an expression that is not a type name.

## Done when

`__typeof__` over a conditional declares an object of the selected type, in
both the pointer-mismatch spelling above and the null-pointer-constant spelling
musl writes, oracled against clang with gcc recorded beside it, with a runtime
fixture under `tests/` and a frontend module test for the type the walk
resolves. Re-run `functional/tgmath` afterwards and record whether it moved —
it may well have a second cause.

---

## forgejo#761 — c: _Atomic before a struct, union or enum keyword never reaches the type

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/761

`_Atomic` written as a qualifier in front of a `struct`, `union` or `enum`
keyword is dropped: the type the parser builds is the plain aggregate, so the
object is not atomic, no atomic access is emitted for it, and `sizeof` and
`_Alignof` answer the aggregate's own numbers. Measured 2026-08-30 on the
branch of #731 and on `main` at `6dbdab27` alike; it is pre-existing and the
#731 layout work neither caused it nor changed it.

```c
struct three { char a, b, c; };
typedef struct { char a, b, c; } three_alias;

_Static_assert(sizeof(_Atomic struct three) == 4, "clang: 4");    /* fails: 3 */
_Static_assert(sizeof(_Atomic three_alias) == 4, "clang: 4");     /* passes */
_Static_assert(sizeof(struct three _Atomic) == 4, "clang: 4");    /* passes */
_Static_assert(sizeof(_Atomic(struct three)) == 4, "clang: 4");   /* passes */
```

Three of the four spellings build the atomic type; only the leading qualifier
before a tag keyword does not. It is not a layout question -- the qualifier
never reaches the type at all, so `_Atomic struct three x; x = y;` compiles
into an ordinary aggregate copy where the program asked for an atomic store.

## Where it is

`c_parse_scalar_type_core_begin` in `c_parse.c` scans the specifier prefix
looking for the aggregate keyword:

```c
    u32 aggregate_index = start;
    while (aggregate_index < end && preprocess.tokens[aggregate_index].kind == C_TOKEN_IDENTIFIER)
    {
        ...
        if (c_token_in_well_known_set(..., C_PARSE_AGGREGATE_KEYWORDS)) { break; }
        if (!c_parse_type_word_for_dialect_token(preprocess, preprocess.tokens[aggregate_index])) { break; }
        aggregate_index += 1;
    }
```

The qualifier words it steps over are never collected, and the aggregate
branch returns the tag's own type. The trailing position is handled by
`c_parse_apply_trailing_qualifiers` at the two returns of that branch, which is
the helper the leading run needs as well; the typedef branch beside it
(`c_parse_qualified_typedef_type`) already collects a leading run the same way.

## What has to happen first

Fixing the parse gap alone turns a currently-compiling construct into a hard
failure, because an atomic *aggregate* has no code generation:
`typedef struct { long long a; } eight; typedef _Atomic eight a8; a8 g; g = v;`
fails today with `C code generation failed with error 2 ... opcode 9`
(`IR_OPCODE_ATOMIC_STORE`) whichever spelling built the type -- see the sibling
issue for that. The layout is not involved: the shape above is eight bytes
aligned eight with and without the promotion. So the order is codegen first,
then this.

`const struct S` and `volatile struct S` ride the same prefix run. `const` is
diagnosed today through the declaration rather than the type
(`const struct three cx; cx.a = 5;` is rejected), so collecting the run must
be checked against those two as well rather than assumed to be an `_Atomic`
question.

## Done when

All four spellings above build one type, `sizeof`/`_Alignof` agree across them
in both layout engines, and an object declared with the leading spelling takes
atomic accesses. Oracle against Clang, recording GCC beside it. The `_Atomic`
shapes in `tests/basic_c_packed_layout.c` are where the three working
spellings are already pinned; the leading one is named there as absent for
this reason. Gate with `./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release`.

---

## forgejo#762 — c: an atomic aggregate cannot be loaded or stored

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/762

Assigning an atomic aggregate -- reading one, or writing one -- fails code
generation. Measured 2026-08-30 on the branch of #731 and on `main` at
`6dbdab27` alike; it is pre-existing, and the #731 layout work neither caused
it nor changed it.

```c
typedef struct { long long a; } eight;
typedef _Atomic eight a8;
static a8 g;
int main(void) { eight v = {5}; g = v; eight r = g; return (int)r.a; }
```

```
cc: error: C code generation failed with error 2, function 1 ('main', ...),
    instruction 7, opcode 9, operation 73, referenced symbol '<none>'
```

Opcode 9 is `IR_OPCODE_ATOMIC_STORE`; the read fails the same way through
`IR_OPCODE_ATOMIC_LOAD`. The layout is not involved: the shape above is eight
bytes aligned eight both before and after #731's promotion, and a three-byte
record -- which the promotion does move, to four aligned four -- fails
identically.

## What is known

- The frontend is doing the right thing. `c_ir_emit_load_place_raw` and its
  store counterpart in `c_gen.c` choose the atomic opcode from the place's
  `IrType::is_atomic` and give the loaded value the *unqualified* type, which
  is the same pairing `ir_validate_canonical_module` admits for every atomic
  opcode. The IR validates; it is the machine layer that has no lowering for
  an atomic load or store whose type is a struct or union.
- `IR_OPCODE_ATOMIC_READ_MODIFY_WRITE` and `IR_OPCODE_ATOMIC_COMPARE_EXCHANGE`
  are already restricted to integer, boolean and pointer operands by the
  validator, so this is only about the plain load and store.
- Clang lowers a lock-free-sized atomic aggregate as an integer access of the
  promoted width -- which is exactly what the #731 promotion exists for: a
  three-byte record is padded to four so that a four-byte access covers it --
  and calls `libatomic` above the target's maximum lock-free width. There is
  no `libatomic` here, so the width-covered case is the one to lower and the
  wider one needs a decision of its own (a diagnostic is a defensible answer).
- Nothing in the compat corpus reaches this: `_Atomic` appears in QuickJS over
  integer scalars only, and musl, libc-test, sbase, SQLite, zlib, cJSON and
  doomgeneric contain no `_Atomic` at all (measured 2026-08-30). buster's own
  `AtomicU64` is `_Atomic u64`. So this is an ABI/semantics completeness
  question rather than a program that is failing.

## Done when

An atomic aggregate whose promoted size is at most the target's maximum
lock-free width (`TargetDataLayout::atomic_max_width`, 128 bits on x86-64 and
AArch64) loads and stores through an integer access of that width, under all
four register allocators, and a wider one gets a diagnostic rather than an
internal code generation failure. Oracle the values against Clang. #761 waits
on this: `_Atomic` before a tag keyword is dropped today, and making it reach
the type would turn programs that compile into programs that hit this failure.
Gate with `./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release`.

---

## forgejo#763 — abi: Clang passes a record containing an atomic member in memory, GCC and this do not

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/763

Clang classifies **any record that contains an atomic member** as MEMORY for
the System V argument ABI, and so does it for an atomic record passed by value.
GCC does not, and neither does this compiler: both pass such a record in
registers when its size and fields otherwise allow it. Measured 2026-08-30
against Clang 22.1.8 and GCC 16.2.1 on `main` at `6dbdab27`; it is
pre-existing, and the #731 layout work neither caused it nor changed it -- the
promotion changes the record's *size*, which is a separate statement from its
class.

```c
struct three { char a, b, c; };
struct outer { char c; _Atomic struct three v; };
struct atomic_int_member { char c; _Atomic int v; };
char f(struct outer x)             { return x.c; }
char h(struct atomic_int_member x) { return x.c; }
char g(_Atomic struct three x)     { struct three y = x; return y.a; }
```

Clang gives all three `ptr noundef byval(...) align 8` -- the record rides
memory. GCC passes each in `%edi`. `_Atomic int` alone as a member is enough;
the size is not the reason.

The cause on Clang's side is structural rather than deliberate:
`X86_64ABIInfo::classify` asks `Ty->getAs<RecordType>()`, and an `AtomicType`
is not sugar over its value type, so the walk falls through to the "everything
else is Memory" tail. The psABI text has no such rule -- an `_Atomic` object
has the size and alignment of the type it is built from and no special class --
so this is the one place where the oracle looks like an accident.

## Why it is filed rather than fixed

AGENTS.md names Clang the oracle where the two references disagree, and the
divergence is an ABI one: a `struct { char c; _Atomic int v; }` argument sits
in a register on one side of a translation-unit boundary and in memory on the
other. But following Clang here means adding a rule to
`ir_system_v_abi_classify` that the psABI does not state and that GCC does not
implement, which would make this compiler disagree with GCC where it agrees
today. That is a decision worth taking deliberately rather than as a side
effect, and it should be taken with the AArch64 side measured too (the AAPCS
classifier asks the same question and Clang was only measured here for
System V).

## Done when

The argument class of a record containing an atomic member, and of an atomic
record, is decided one way with the reasoning written down, both references
recorded, and the choice pinned by a cross-linked pair the way
`tests/basic_c_packed_layout_callee.c` and its caller pin the `packed` and
`aligned` classes -- noting that the `_Atomic` shapes are currently kept out of
`basic_c_packed_layout_shapes.h` because the host half of that pair may be GCC.
Measure both x86-64 and AArch64. #762 is in the way of the by-value case: an
atomic aggregate cannot be loaded or stored at all today, so a callee that
reads its parameter does not compile.

---

## forgejo#765 — c: an SSE register class as an inline-assembly operand

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/765

Split out of #753, which applied musl's whole `ARCH_SRCS` rule to the
`test_musl` manifest and made this measurable for the first time.

## The gap

The frontend's inline-assembly operand classes are the fixed general registers,
`r` and `m`, and nothing else (`c_ir_inline_assembly_constraint`,
`src/buster/lib/compiler/frontend/c/c_gen.c:28491`). GNU's `x` names the SSE
register class, and musl's own x86-64 math is written in it.

Two refusals, because the parser reaches the two positions differently:

- As an **output** (`"=x"`, `"+x"`) the switch has no case and reports
  `unsupported asm output constraint`.
- As an **input** (`"x"`) the switch has no case either, so the operand falls
  through to the matching-constraint parser and is reported as
  `malformed asm matching constraint`, which names the wrong thing.

## What it holds

Eight of the sixteen `MUSL_UNSUPPORTED` units `test_musl` reports:

- output: `src/math/sqrt`, `src/math/sqrtf`, `src/math/fabs`, `src/math/fabsf`
- input: `src/math/llrint`, `src/math/llrintf`, `src/math/lrint`,
  `src/math/lrintf`

Each is a single SSE instruction -- `sqrtsd %1, %0`, `cvtsd2si %1, %0`, a
`pcmpeqd`/`psrlq`/`andps` triple -- so the operand class is the whole of the
work.

## Fixtures

`tests/basic_c_asm_sse_output.c` and `tests/basic_c_asm_sse_input.c` are the
reductions, one construct apiece, each asserting its exact diagnostic in
`compiler_driver_tests`. Implementing the class turns both into positive
fixtures and moves `MUSL_EXPECTED_COMPILED_UNITS` off 1340.

## Gate

`./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test`.
The archive stops taking `MUSL_SUBSTITUTED` fallbacks for these eight, so it
holds musl's own implementation the way the Clang reference already does.

---

## forgejo#766 — c: the x87 register stack as an inline-assembly operand and clobber

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/766

Split out of #753, which applied musl's whole `ARCH_SRCS` rule to the
`test_musl` manifest and made this measurable for the first time.

## The gap

The frontend's inline-assembly operand classes are the fixed general registers,
`r` and `m`, and nothing else (`c_ir_inline_assembly_constraint`,
`src/buster/lib/compiler/frontend/c/c_gen.c:28491`), and its clobber table has
no x87 register file (`src/buster/lib/compiler/frontend/c/c_gen.c:29561`). GNU
spells the top of the x87 stack `t` and the one below it `u`, and spells the
whole stack as a clobber `st`.

This is the register file an x86-64 `long double` already lives in, so these
templates are read-modify-write of `ST(0)` with no move around them:

- `"+t"` output, `"u"` input -- `sqrtl` is a bare `fsqrt`, `rintl` a bare
  `frndint`, `fabsl` a bare `fabs`; `fmodl` is an `fprem` loop and
  `remainderl` an `fprem1` loop, both reading `fnstsw` back into `%ax`.
- `remquol` goes further and decodes `C0`/`C1`/`C3` out of the status word for
  the low bits of the quotient.
- `"st"` clobber with a `"t"` input -- `llrintl` and `lrintl` are `fistpll`,
  which pops.

## What it holds

Eight of the sixteen `MUSL_UNSUPPORTED` units `test_musl` reports:
`src/math/fabsl`, `src/math/fmodl`, `src/math/remainderl`, `src/math/remquol`,
`src/math/rintl`, `src/math/sqrtl`, `src/math/llrintl`, `src/math/lrintl`.

The clobber is checked before the operands are, so `llrintl` and `lrintl`
report `unsupported GNU inline assembly clobber` while the other six report
`unsupported asm output constraint`; both halves are this one register file.

## Fixtures

`tests/basic_c_asm_x87_output.c` and `tests/basic_c_asm_x87_clobber.c` are the
reductions, one construct apiece, each asserting its exact diagnostic in
`compiler_driver_tests`. Implementing the class turns both into positive
fixtures and moves `MUSL_EXPECTED_COMPILED_UNITS` off 1340.

## Gate

`./build/build test_musl --config Release ~/dev/musl-v1.2.6 ~/dev/libc-test`.
The archive stops taking `MUSL_SUBSTITUTED` fallbacks for these eight. Worth
watching the `src/math` subset with it: `remquol`'s quotient bits are the one
place the two implementations could disagree observably.

---

## forgejo#767 — llvm: an atomic aggregate's bitcode type is short by its padding

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/767

The LLVM bitcode writer maps an atomic type onto the LLVM type of its operand.
That is exact for every atomic scalar -- `_Atomic int` and `int` are one LLVM
type -- and it is short by the padding for an atomic aggregate, whose C type is
padded up to the next power of two (#731):

```c
typedef struct { char a, b, c; } three;
typedef _Atomic three at3;
at3 g;
```

```
ide cc -emit-llvm : @g = global { i8, i8, i8 } zeroinitializer, align 4
clang -emit-llvm  : @g = global { %struct.three, [1 x i8] } zeroinitializer, align 4
```

The alignment is right and the *native* object is right -- both compilers put
`g` in `.bss` with size 4 and `llvm-nm --print-size` agrees -- so this is the
bitcode output alone. LLVM reading that module sizes `g` at three bytes where
the C type is four.

## Where it is

`llvm_bc_add_ir_type` in `src/buster/lib/compiler/llvm/bitcode.c`:

```c
        if (type->is_atomic && type->unqualified_type.value < context->program->types.count &&
            context->ir_type_ids[type->unqualified_type.value] != LLVM_BC_INVALID_ID)
        {
            context->ir_type_ids[type_index] = context->ir_type_ids[type->unqualified_type.value];
            return true;
        }
```

An atomic type whose `layout.size` exceeds its operand's needs a struct type of
its own -- the operand followed by a byte array of the padding, which is what
Clang emits -- rather than the operand's id. `llvm_bc_type_dependencies_ready`
and the type-table walk beside it are where that record would be built.

## Done when

`-emit-llvm` for an atomic aggregate produces a type whose size is the C type's,
for a global, a local and a member, and the shape matches what Clang emits for
the same source. The layout itself is already pinned by
`tests/basic_c_packed_layout.c`; this needs a bitcode-level check of its own.
Measured 2026-08-30. Note that an atomic aggregate cannot be loaded or stored
yet (#762), so the reachable shapes today are declarations rather than
accesses.

---

## forgejo#771 — frontend/codegen: __attribute__((constructor)) is accepted and dropped

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/771

`__attribute__((constructor))` is parsed, accepted, and dropped. Nothing in the
tree emits `.init_array`, so a function that carries it is never called, and
when it is the only function in a translation unit it is not even emitted.

Found while closing #751. Measured on `claude/objective-curie-5762dd` at
`11eb76a2`, libc-test gate `LIBC_TEST_EXPECTED_PASSING 381` /
`LIBC_TEST_EXPECTED_STATE_HASH 0x0e973f6c67fa1195`.

## The evidence

`libc-test`'s `src/functional/tls_align_dso.c` is eight `__thread`
definitions, a table `t`, and one constructor that fills the table:

```c
__attribute__((constructor)) static void init(void)
{
	entry(0, xchar)
	entry(1, xshort)
	entry(2, xint)
	entry(3, xllong)
}
```

Compiled the way the harness compiles it:

```
ide cc -fPIC -DSHARED -c -target x86_64-unknown-linux-gnu tls_align_dso.c -o ta.o
readelf -SW ta.o | grep -c init_array   # 0
objdump -d --section=.text ta.o         # empty
readelf -sW ta.o | grep FUNC            # only the .text section symbol
```

There is no `.init_array` section, and because `init` is `static` and nothing
calls it, the unused-static elimination drops the only function in the file.
Clang's object has `.init_array` with a `.rela.init_array` entry pointing at
`init`.

`grep -rn 'init_array\|INIT_ARRAY\|"constructor"' src/buster/lib/compiler/`
returns nothing: the attribute is swallowed by the general attribute parser
and has no consumer anywhere.

## What it costs

`functional/tls_align` and `functional/tls_align_dlopen` are the last two
`fail` units in libc-test, and this is why. Both read `t` out of that shared
object; with the constructor gone every `t[i].name` is null and the test
reports four missing names. Their cause was previously credited to the
thread-local model, which #751 has now closed -- the models are right and the
answer is still wrong, because the table is never filled.

`__attribute__((destructor))` is the same gap and should be settled with it,
as should the interaction with `.fini_array`.

## Scope

- Emit `.init_array` (ELF), and whichever section COFF and Mach-O spell it as,
  with one relocation per constructor. Priority (`constructor(101)`) orders
  within the array; ELF spells the ordered form as `.init_array.NNNNN`.
- A function carrying the attribute is reachable by definition: the unused
  static elimination has to stop dropping it.
- The linker has to keep the array and, for a static executable, run it.
  A shared object's is run by the loader.
- Say what happens for a target where the concept does not exist, rather than
  accepting the attribute silently the way this does today.

## Validating a fix

- `tests/` fixture under all four allocators: a constructor that writes a
  global, a `main` that checks it, and a second constructor with a priority to
  pin the order. Exit non-zero per case.
- `./build/build test_musl --config Release <musl> <libc-test>`: this should
  take `functional/tls_align` and `functional/tls_align_dlopen` from `fail` to
  `pass`, 381 to 383, and leave no `fail` unit in the suite. Rebaseline
  `LIBC_TEST_EXPECTED_PASSING` and `LIBC_TEST_EXPECTED_STATE_HASH` in `build.c`
  with an entry in the comment block above them.
- `./build.sh build --config Release -t test_all` and
  `./build.sh test_self_host --config Release`.

## Done

A constructor runs before `main` in a program this compiler builds and links,
and before `dlopen` returns in a shared object it builds, under all four
allocators; the two libc-test units pass; the destructor half is either
implemented or explicitly deferred with a reason.

---

## forgejo#779 — link: Mach-O and UEFI images refuse __attribute__((constructor)) instead of running it

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/779

`__attribute__((constructor))` reaches the object file on every target now
(issue 771), and the ELF and PE writers call the registered functions from the
entry stub they synthesize. The Mach-O and UEFI writers do not: they refuse
the link with `LINK_ERROR_UNSUPPORTED_FEATURE`, naming the first initializer,
rather than placing an array nothing would call.

Measured on `claude/inspiring-hypatia-6f3376`.

## Why they refuse rather than run

Neither writer synthesizes an entry point. A Mach-O executable's `LC_MAIN`
carries the file offset of `main` and dyld calls it directly; a UEFI
application's entry is the firmware's call to `UefiMain`. Every other writer
in `link.c` builds a `_start` of its own -- that stub *is* the startup, which
is why `link_x86_build_elf_entry_stub` and `link_x86_build_pe_entry_stub`
exist -- and `link_initializer_plan_build` hands each of them the list of
functions to call and an object with the two array sections removed.

`link_initializer_plan_empty` is the refusal, and it is deliberate: a writer
that places every non-debug section it is given would otherwise write an array
with no offset of its own over the image header, and dropping the arrays
without saying so is the state issue 771 found the whole attribute in.

```
$ ide cc -target aarch64-apple-macos ctor.c -o ctor
cc: error: native C link failed with unsupported feature: first
```

The object itself is correct on both targets. Mach-O carries
`__DATA,__mod_init_func` and `__mod_term_func` with the
`S_MOD_INIT_FUNC_POINTERS`/`S_MOD_TERM_FUNC_POINTERS` types, so an object this
compiler produces links and runs correctly through the system linker; only an
image *this linker* writes is affected.

## What a fix looks like

Two shapes are available and they are not the same amount of work.

- **Mach-O, through dyld.** dyld runs `__DATA,__mod_init_func` for the main
  executable, so the writer could keep the section instead of stripping it:
  give it a section command in the `__DATA` segment with the right type and
  address, place it beside `OBJECT_SECTION_DATA` in
  `link_native_executable_mach_o64`'s explicit per-kind layout, and let the
  loader call the entries. That is the idiomatic answer and needs no stub.
  `__mod_term_func` is the destructor half.
- **Mach-O, through a stub.** Point `LC_MAIN` at a synthesized function placed
  beside the import stubs (`stub_offset` already reserves text there), have it
  save the four arguments dyld passes, call each constructor, tail-call `main`
  and run the destructors after it. This matches what the ELF and PE writers
  do and reuses `LinkInitializerPlan` whole, but it needs a new x86-64 and a
  new AArch64 prologue/epilogue.
- **UEFI.** A firmware image has no C runtime and its entry takes the image
  handle and the system table. Whether initializers should run there at all is
  the first question; if they should, the writer needs an entry stub that
  forwards both arguments.

## Validating a fix

`tests/basic_c_constructor.c` is the fixture and it is already written: a
constructor that writes a global, two more with priorities pinning the order,
and a destructor `main` must not have seen. `driver_test.c` runs it under all
four allocators and currently guards that block with `#if !BUSTER_APPLE`;
removing the guard is the test. The macOS runner is the only Mach-O coverage
in CI, so the fix has to be verified there rather than by cross-compiling.

## Done

`__attribute__((constructor))` runs before `main` in a Mach-O executable this
linker writes, under all four allocators, with the `#if !BUSTER_APPLE` guard
removed from `driver_test.c`; or the UEFI half is closed the same way, or
explicitly deferred with a reason.

---

## forgejo#781 — link: a destructor does not run when the program terminates through exit()

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/781

`__attribute__((destructor))` runs when `main` returns in an image this linker
writes, and does not run when the program terminates by calling `exit`
instead. GNU runs it either way.

Measured on `claude/inspiring-hypatia-6f3376`, which is where the attribute
started reaching the image at all (issue 771).

```c
#include <stdio.h>
#include <stdlib.h>
__attribute__((destructor)) static void bye(void) { printf("bye\n"); }
int main(void) { printf("main\n"); exit(3); }
```

```
$ ide cc dtor_exit.c -o dtor_exit && ./dtor_exit
main
$ clang -no-pie dtor_exit.c -o r && ./r
main
bye
```

Returning from `main` is correct in both:

```
$ ide cc dtor.c -o dtor && ./dtor   # main returns
ctor
main
last
early_d
```

## Why

An image this linker produces has no libc startup object -- the entry stub
`link_x86_build_elf_entry_stub` writes *is* its startup -- so nothing registers
`_dl_fini` or anything like it, and the stub owns the whole sequence: it calls
the constructors, calls `main`, calls the destructors in reverse, and then
calls `exit`. A program that reaches `exit` from inside `main` never comes
back to the stub, so the destructor calls are skipped.

glibc gets the other behaviour by registering the fini walk as an exit
handler: `__libc_start_main` hands `_dl_fini` to `__cxa_atexit` before it
calls `main`, so the handler runs whichever way the program ends, and it runs
*after* every handler the program registered itself because it was registered
first.

## What a fix looks like

The hosted ELF and PE stubs would register a runner with `atexit` instead of
calling the destructors inline:

- Synthesize the runner beside the entry stub -- a function that calls each
  `LinkInitializerEntry` of `plan.destructors` in order and returns. The plan
  and the displacement patching already exist (`link_x86_patch_initializer_calls`,
  `link_aarch64_patch_initializer_calls`); this needs a second patch target
  and a prologue/epilogue per architecture.
- Resolve `atexit` the way `exit` already is. `link_elf_hosted_exit_symbol`
  appends a synthetic undefined `exit` when the program has none, and its
  comment states the constraint a second such symbol has to respect: the
  AArch64 dynamic writer re-derives the x86-64 writer's import numbering from
  the same symbol table, so the two must agree on how many imports there are.
  `link_elf_libc_runtime_object` already supplies an `atexit` stub for glibc
  links where the shared object exports only `__cxa_atexit`.
- The freestanding ELF shape has no `exit` to call and no libc to register
  with, so it keeps the inline sequence. That is not a gap: a `-nostdlib`
  program that calls a raw exit syscall never runs an atexit handler either.

## Validating a fix

Extend `tests/basic_c_constructor.c`, or add a sibling: a destructor that
writes a file or a pipe, and a `main` that calls `exit` rather than returning,
compared against Clang's build of the same source. The current fixture
deliberately checks only that `main` has *not* seen the destructor yet, which
is true under both behaviours.

## Done

A destructor runs when the program terminates through `exit`, in a hosted ELF
and a PE image this linker writes, under all four allocators, and the ordering
against handlers the program registered with `atexit` itself matches Clang's.

---

## forgejo#782 — object: constructor priority orders within a translation unit, not across one

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/782

`__attribute__((constructor(101)))` orders correctly within a translation unit
and does not order against another translation unit's. GNU runs every
prioritized constructor before every unprioritized one, ascending, across the
whole program; this tree gets that inside one object only.

Measured on `claude/inspiring-hypatia-6f3376`, where the attribute started
reaching the object file at all (issue 771).

## What happens today

`ld` gets the GNU order by *section name*: it sorts `.init_array.NNNNN` ahead
of the unsuffixed `.init_array` and concatenates the result. Clang emits one
section per priority -- an object with `constructor(101)`, `constructor(150)`
and a plain `constructor` has `.init_array.101`, `.init_array.150` and
`.init_array`.

This object model has **one section per kind**: `ObjectFile.sections` is
indexed by `ObjectSectionKind` and every reader ends with
`result.section_count = OBJECT_SECTION_COUNT`. So a translation unit's whole
array is one `.init_array`, and `object_from_canonical_codegen_module` sorts
the entries into it instead -- ascending priority, an attribute that named
none last, equal priorities in declaration order. Inside one object that is
GNU's order exactly; between objects the order is the linker's concatenation,
which is all `ld` would give two unprioritized initializers anyway.

The observable gap:

```c
/* a.c */ __attribute__((constructor))      static void late(void)  { ... }
/* b.c */ __attribute__((constructor(101))) static void early(void) { ... }
```

`ld a.o b.o` runs `early` before `late` for Clang's objects and after them for
Buster's, because Buster's `b.o` contributes to `.init_array` rather than to
`.init_array.00101`.

Images this linker writes are not affected: `link_initializer_plan_build`
reads the plan and the entry stub calls the functions directly, and the merge
concatenates in the same order either way. This is about objects consumed by
an external linker, which is what `test_musl` and every compat harness do.

## What a fix looks like

The object model has to carry more than one section of a kind. `ObjectSection`
already has both a `name` and a `kind`, so the shape exists; what does not is
anything that indexes past `OBJECT_SECTION_COUNT`:

- Readers: all three end at `OBJECT_SECTION_COUNT` and merge every input
  section of a kind into one. The ELF reader would have to keep
  `.init_array.NNNNN` apart, or sort as it merges (which preserves order
  within one input file and is enough for a round trip).
- `link_objects`: `section_offsets` is `object_count * OBJECT_SECTION_COUNT`
  and the output has one section per kind. Extra sections need their own
  index space.
- The eight native writers each lay out sections by explicit kind, so an
  extra section needs a placement rule in each -- unless the linker folds
  them, which it can, because it runs the initializers itself.
- The JIT and the disassembly printer walk `section_count` already.

A cheaper first step, if the full model change is not wanted: let the **ELF
writer alone** split one `OBJECT_SECTION_INIT_ARRAY` into one ELF section per
priority group at write time, remapping the relocation section indices as it
goes. The writer already builds its own section table (`relocation_targets`,
`section_name_offsets`) and would need the per-entry priorities carried beside
the section -- the sort order is already by priority, so the groups are
contiguous runs. That fixes every external link without touching the model,
the readers, the linker or the other two formats.

## Validating a fix

Two translation units as above, linked with the system `ld`, comparing the
observed order against Clang's build of the same two files.
`tests/basic_c_constructor.c` covers the within-unit order and stays as it is.
`readelf -SW` on a multi-priority object is the direct check: it should show
`.init_array.00101`, `.init_array.00150` and `.init_array`.

## Done

Two translation units whose constructors carry different priorities run in
GNU's order when linked by an external linker, and a Buster object with mixed
priorities has one ELF section per priority group.

---

## forgejo#784 — c: _Atomic ( T ) as a sizeof operand does not fold in an enum constant

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/784

`_Atomic ( T )` written as the operand of a `sizeof` or `_Alignof` inside an
**enum constant** resolves nothing, whatever `T` is, and the failure takes the
whole enumerator with it: the enum type fails, and every enumerator it declares
is then undeclared everywhere the file names it. Measured 2026-08-30 on the
branch of #761, which fixed the leading and trailing qualifier runs in the same
walk and left this spelling untouched.

```c
struct three { char a, b, c; };
typedef struct { char a, b, c; } three_alias;

enum { A = sizeof(_Atomic struct three) };   /* 4, fixed by #761 */
enum { B = sizeof(struct three _Atomic) };   /* 4, fixed by #761 */
enum { C = sizeof(_Atomic three_alias) };    /* 4, already worked */
enum { D = sizeof(_Atomic(struct three)) };  /* fails to compile */
enum { E = sizeof(_Atomic(three_alias)) };   /* fails to compile */
enum { F = sizeof(_Atomic(int)) };           /* fails to compile */
```

It is not a tag question -- `_Atomic(int)` fails identically -- so it is the
operator spelling itself that the walk has no branch for.

## Where it is

`c_parse_machineless_sizeof_operand_layout` in `c_parse.c` is the operand walk,
and `c_parse_machineless_base_type` beside it is what resolves the operand's
base type. The walk exists because the enum-constant evaluator runs *inside* a
step of the type-parse machine, which is an explicit frame stack with one
shared result slot and cannot be reentered; the machine-bearing path uses
`c_parse_scalar_type` and handles the operator spelling in
`c_type_parse_scalar_step`, which is why `_Static_assert(sizeof(_Atomic(int)))`
folds and the enum constant beside it does not.

`c_parse_machineless_base_type` tries a qualified typedef name, then a tag,
then a primitive spelling. `_Atomic` followed by `(` matches none of them: the
typedef walk steps over `_Atomic` as a qualifier and then finds a `(` where a
name should be, and the tag walk sees `_Atomic` where a keyword should be.
`c_parse_atomic_type_specifier_at` (added by #761) is the predicate that names
the shape; the branch it would gate has to resolve the parenthesised operand
recursively -- base type, pointer chain, array suffixes -- and then qualify the
result atomic, refusing the array, function and already-qualified operands the
machine path diagnoses at `C_DIAGNOSTIC_INVALID_ATOMIC_TYPE`.

## Done when

All four spellings of `_Atomic` over a tag and over a typedef name fold in an
enum constant to the same numbers `_Static_assert` folds for them, oracled
against Clang. `tests/basic_c_packed_layout.c` already holds the enum-constant
fold of the other three spellings, in the `atomic_leading_folded_size` block
next to the tagged atomic shapes; the operator spelling goes there. Gate with
`./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release`.

---

## forgejo#786 — c: an atomic aggregate parameter fails code generation in every spelling

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/786

An **atomic aggregate passed or received by value** fails code generation in
every spelling that builds the type, with an internal-sounding message rather
than a diagnostic:

```c
typedef struct { char a, b, c; } three;
typedef _Atomic three at3;
int takes(at3 p) { return p.a; }
```

```
cc: error: ...:3:5: in function 'takes': cannot convert IR type 20 (kind 11) to IR type 19 (kind 11)
```

The trailing spelling (`struct three _Atomic p`) and the leading one
(`_Atomic struct three p`, which #761 made buildable) fail identically, so it is
the parameter position rather than any one spelling. Clang and GCC both accept
the declaration; Clang passes the *promoted* four-byte object. Measured
2026-08-30 on the branch of #761; pre-existing, and #761 neither caused it nor
changed it.

The two types the message names are the atomic type and its operand: the
promotion (#731) makes `_Atomic three` four bytes where `three` is three, so
they are two `IrType`s of two different sizes, and the argument lowering treats
one as convertible to the other.

## What is already settled

- The layout is not in question: `_Atomic three` is four bytes aligned four on
  every path, pinned in `tests/basic_c_packed_layout.c`.
- The *access* is settled too (#762): a load or a store of an atomic aggregate
  is one integer access of the promoted width, refused with a diagnostic naming
  the width when the target has no lock-free access that wide.
- AGENTS.md already records the argument-side layout answer: "On the argument
  side the promotion moves nothing: a promoted four-byte record is one INTEGER
  eightbyte where the three-byte one already was." That is the classification;
  what is missing is the lowering that builds the parameter's frame object and
  the conversion between the value and the object.
- A **return** of an atomic aggregate should be checked in the same change; it
  was not measured separately.

## Done when

A function may take and return an atomic aggregate by value in all four
spellings, the bytes match Clang's (including the padding the promotion added,
which Clang zeroes), and a width past the target's lock-free ceiling is refused
with the #762 diagnostic rather than an internal failure.
`tests/basic_c_atomic_aggregate.c` is the fixture -- it runs under all four
register allocators and bakes in Clang's bytes -- and the parameter and return
round trips go there. Gate with
`./build.sh build --config Release -t test_all` plus
`./build.sh test_self_host --config Release`.

---

## forgejo#789 — link: constructor priority does not order across objects in Buster's own linker

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/789

`ld` runs every prioritized constructor before every unprioritized one,
ascending, across the whole program. PR #788 gave Buster's **objects** that
shape: `object_write_elf64` splits `OBJECT_SECTION_INIT_ARRAY` into one
`.init_array.NNNNN` ELF section per priority group, so an external linker
orders two Buster translation units exactly as it orders Clang's.

Buster's **own** linker still does not. `link_objects` concatenates each input
object's whole array in link order, and `link_initializer_plan_build` then reads
that concatenation off the merged `OBJECT_SECTION_INIT_ARRAY` slot by slot. The
priority is gone by then: `object_read_elf64` merges every `.init_array*`
section of an input into the one section of the kind (in `ld`'s order within
that input, as of #788, but still one section), so nothing downstream can tell
a `constructor(101)` from an unprioritized one.

This is **not a regression** and **not producer-specific**. Measured on
`claude/suspicious-chaplygin-0558e7` (PR #788), 2026-08-30, with

```c
/* a.c */ __attribute__((constructor))      static void a_plain(void) { note("a_plain"); }
          __attribute__((constructor(120))) static void a_120(void)   { note("a_120"); }
          int main(void) { return 0; }
/* b.c */ void note(const char* name) { printf("%s\n", name); }
          __attribute__((constructor))      static void b_plain(void) { note("b_plain"); }
          __attribute__((constructor(101))) static void b_101(void)   { note("b_101"); }
          __attribute__((constructor(150))) static void b_150(void)   { note("b_150"); }
```

| link | order |
| --- | --- |
| Clang objects, system `ld` | `b_101 a_120 b_150 a_plain b_plain` |
| Buster objects, system `ld` (after #788) | `b_101 a_120 b_150 a_plain b_plain` |
| Buster objects, `ide cc a.o b.o` | `a_120 a_plain b_101 b_150 b_plain` |
| **Clang** objects, `ide cc a.o b.o` | `a_120 a_plain b_101 b_150 b_plain` |

The last row is the point: Buster's linker gets Clang's objects wrong in exactly
the same way, so this is a linker gap rather than anything about how Buster
writes objects.

## What a fix looks like

This is the model half that #782 deferred. Either:

- Carry the priority through the read. `ObjectFile` would need to keep more than
  one section of a kind, or keep the per-entry priorities the way
  `ObjectFile.initializer_priorities` already does for the converter's output --
  `object_read_elf64` would fill it from the section names it now parses with
  `object_elf_initializer_section_priority`, and `link_objects` would merge the
  arrays by priority instead of concatenating them. `section_offsets` there is
  `object_count * OBJECT_SECTION_COUNT`, and the entries of the two initializer
  kinds would need their own index space or a stable-sorted merge. The priority
  array is per-entry, so a merge has to move the matching relocations with it.
- Or sort in `link_initializer_plan_build`, which is where the plan is built and
  where the order stops being a section layout and becomes a call sequence. That
  needs the plan entries to carry a priority, which means the merge above still
  has to preserve one per entry.

The second is the smaller change if the priorities can be threaded that far; the
first is the one that also makes a Buster-written *relocatable* output (`-r`)
correct, if that ever exists.

## Validating

The two files above, built with `ide cc -c` and linked with `ide cc a.o b.o`,
must print `b_101 a_120 b_150 a_plain b_plain` -- the same as the system `ld`
gives for either producer's objects. The same must hold for Clang-built objects
fed to `ide cc`, which is the cheapest oracle here because it does not depend on
Buster's own object writer at all. `tests/basic_c_constructor.c` already covers
the within-translation-unit order through both link paths (`driver_test` links
it directly and, since #788, from an object) and must stay passing.

## Done

`ide cc a.o b.o -o prog` runs a `constructor(101)` from one translation unit
before an unprioritized constructor from another, for objects written by Buster
and by Clang alike.

---

## forgejo#792 — link: a program cannot call atexit on a PE target

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/792

A C program that calls `atexit` does not link for a PE target:

```c
extern int atexit(void (*handler)(void));
static void handler(void) {}
int main(void) { return atexit(handler); }
```

```
$ ide cc -target x86_64-pc-windows-msvc -o t.exe t.c
cc: error: native C link failed with unresolved symbol: atexit
```

Measured on `claude/destructor-exit-781` (PR #791), on a Linux host with an
`ucrtbase.dll` on the library path so the export table is actually read; the
same shape links on Linux.

## Why

`atexit` is not an export of `ucrtbase.dll`. `winedump -j export
ucrtbase.dll` lists `_crt_atexit`, `exit`, `_exit`, `_Exit` and
`quick_exit`, and no `atexit`: UCRT keeps `atexit` in the static import
library, where it is a call to `_crt_atexit`, exactly the way glibc keeps
`atexit` in `libc_nonshared.a` as a call to `__cxa_atexit`.

The ELF side of that split is already handled.
`link_elf_libc_runtime_object` in `link.c` supplies weak `atexit` and
`at_quick_exit` stubs that forward to their `__cxa_` forms, and the driver
adds that object the way it selects an archive member -- only when something
references one of its stubs and nothing defines them
(`compiler_driver_archive_member_needed`, two call sites in `driver.c`).

Windows has the same seam and no stub in it.
`link_windows_runtime_object`, right above `link_elf_libc_runtime_object`,
already exists for exactly this class of thing: it supplies `_fltused`, the
marker UCRT does not export either. It is selected through the same
`compiler_driver_archive_member_needed` path.

## What a fix looks like

Give `link_windows_runtime_object` an `atexit` thunk -- a weak function that
tail-calls the imported `_crt_atexit` with the same argument -- the way
`link_elf_libc_runtime_object` builds its two. The x86-64 body is a `jmp
rel32` with an `OBJECT_RELOCATION_X86_64_PC32` against an undefined
`_crt_atexit`; the AArch64 body is a `b` with
`OBJECT_RELOCATION_AARCH64_JUMP26`. Keep it weak so a program with its own
definition wins, and keep it out of images that do not reference it, which
the existing selection already does.

`_onexit` and `at_quick_exit`/`_crt_at_quick_exit` are the same shape and
worth checking in the same pass; `_crt_at_quick_exit` is exported.

## Not a blocker for the entry stub

The linker's own destructor runner does not need this. It registers with
`_crt_atexit` directly (PR #791), because it synthesizes the call itself and
is not resolving a name the program wrote.

## Validating

`tests/basic_c_destructor_exit.c` compiles its `atexit` half out under
`_WIN32` for exactly this reason; with the thunk in place, that `#if` can
go and the fixture checks one contract on every target. A Windows host --
or a Linux host with `-L` pointing at a directory holding `ucrtbase.dll` --
runs it through the same driver_test loop the other `basic_c_*` fixtures use.

## Done

`extern int atexit(void (*)(void));` links and runs for
`x86_64-pc-windows-msvc` and `aarch64-pc-windows-msvc`, a program that
defines its own `atexit` still uses it, and a program that never names it
gains no import.

---

## forgejo#795 — object: a relocatable COFF or Mach-O object cannot state a constructor priority

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/795

A constructor priority reaches this linker two ways: straight from the
converter, when one `ide cc` invocation compiles several sources, or off an
ELF object's section names, `.init_array.NNNNN` per priority group, which is
`ld`'s own convention (#782 writes them, #789 reads them back and orders the
merged array by them).

A **relocatable COFF or Mach-O object states no priority at all**, so the
second way is ELF-only and `ide cc a.o b.o` falls back to concatenating the
two arrays in link order on Windows and macOS.

- COFF: `object_write_coff` emits one section under this model's neutral name,
  `.init_array`. MSVC's own convention for the same job is the
  lexicographically ordered `.CRT$XC*` group, which nothing here writes or
  reads, so a suffix would be this compiler's private convention -- which is
  fine, since its own linker is the only consumer of these sections in a COFF
  object.
- Mach-O: a section name is 16 bytes and `__mod_init_func` is already 15, so
  there is no room for a suffix. A priority would need a different carrier
  entirely.

Measured on `claude/gracious-pike-ec797f` (the #789 branch), 2026-08-30, with
`tests/basic_c_constructor_order.c` and `tests/basic_c_constructor_order_second.c`
cross-compiled to `x86_64-pc-windows-msvc` and run under wine. The fixture
returns the position that ran out of order:

| link | exit |
| --- | --- |
| `ide cc order.c order_second.c` (one invocation) | 0 |
| `ide cc order.obj order_second.obj` | 2 -- `constructor(120)` ran before `constructor(101)` |

The same split is why `driver_test`'s cross-translation-unit object route is
`#if !BUSTER_WINDOWS && !BUSTER_APPLE`: the source route runs on every host,
the object route only where the format can state a priority.

Note what is *not* broken any more: both readers now classify the arrays
(#789 -- the COFF reader by this model's neutral name, the Mach-O reader by
`S_MOD_INIT_FUNC_POINTERS`/`S_MOD_TERM_FUNC_POINTERS`), so a program linked
from an object runs its constructors, in the order the object states them,
on all three formats. What is missing is only the cross-object *priority*.

## What a fix looks like

`object_elf_split_initializer_priorities` is already generic over the section
count and names, and `object_write_coff` is generic over both, so the COFF
half is mostly the reader: it would need the same `ld`-order merge
`object_read_elf64` grew in #788, keyed on the same
`object_elf_initializer_section_priority`. The Mach-O half needs a carrier
proposal first.

## Done

`ide cc a.o b.o` runs a `constructor(101)` from one translation unit before an
unprioritized constructor from another for COFF objects as it already does for
ELF ones, or the Mach-O half is closed with a stated reason.

---

## forgejo#798 — link: a Mach-O destructor does not run when the program terminates through exit()

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/798

A `__attribute__((destructor))` function does not run when a Mach-O image
this linker writes terminates by calling `exit`. The constructor half of the
same contract works.

## Measured

On `claude/destructor-exit-781` (PR #791), on the `aarch64-macos-mini` runner
(Apple M1, macOS 26.5 SDK, Apple clang 21.0.0), 2026-08-30.
`tests/basic_c_destructor_exit.c` fails under all four allocators:

```
Launched [33491]: ".../buster-c-destructor-exit-33302"
os_process_wait_sync(...).result == PROCESS_RESULT_SUCCESS failed at
  src/buster/tests/compiler/driver/driver_test.c:compiler_driver_tests:6067
```

Four assertions, one per allocator, and nothing else in the whole matrix
failed -- `x86_64-linux`, `x86_64-linux-dedicated` and `x86_64-windows-znver5`
were all green on the same commit.

What that narrows it to:

- The **build** assertion passed, so the fixture compiles and the Mach-O image
  links.
- The **spawn** assertion passed, so the image runs.
- Only the **exit status** was wrong.
- `tests/basic_c_constructor.c` passed on that same run, under all four
  allocators, so `__DATA,__mod_init_func` is honoured.

The exit status itself was not captured, and that is the first thing to get:
the fixture encodes its verdict there. `1` means no destructor ran at all
(`main` calls `exit(1)`); `2` means the wrong number of steps ran; `3 + index`
names the first step that came out in the wrong place.

## Why nothing caught this before

`tests/basic_c_constructor.c` deliberately checks only that `main` has **not**
seen its destructor yet, which is true whether or not a destructor ever runs.
So no test in this tree had ever observed a destructor actually running, on any
target, until `tests/basic_c_destructor_exit.c` was added. Issue #779 made the
Mach-O writer keep both arrays and stated that "dyld runs the main executable's
`__DATA,__mod_init_func` before it enters `main` and its `__mod_term_func` in
reverse on the way out"; the first half is now pinned by a passing test and the
second half is not.

## What to find out first

Build the fixture with Clang on a macOS host and run it. That decides which
side is wrong, and the two answers want different fixes:

```sh
clang -o /tmp/dtor-clang tests/basic_c_destructor_exit.c && /tmp/dtor-clang; echo $?
build/Release/ide cc -o /tmp/dtor-buster tests/basic_c_destructor_exit.c && /tmp/dtor-buster; echo $?
```

- **Clang's build also fails** -> macOS does not run a main executable's
  `__mod_term_func` at exit the way GNU runs `.fini_array`, and the fixture is
  asserting a contract the platform does not offer. Then the fix is in the
  fixture (a platform-conditional expectation, the way it already drops its
  `atexit` half under `_WIN32`) plus a line in `AGENTS.md` recording the
  divergence.
- **Clang's build passes and Buster's does not** -> the Mach-O writer has a
  real gap, and `otool -l` on both images is the next step: section type
  (`S_MOD_TERM_FUNC_POINTERS`), placement inside the writable `__DATA` segment,
  the link-time address in each slot, and whether the entries are named by the
  rebase stream. `link_test.c` already checks exactly those four things
  structurally (issue #779), so a structural pass with a runtime failure would
  point at something the structural check does not cover.

Worth checking in the same pass: whether `__mod_term_func` runs when `main`
**returns** on macOS, which is a different question from the one this issue
names and which no test covers either.

## Not a blocker for #781

PR #791 is about the entry stub's own registration, and no Mach-O image has an
entry stub -- LC_MAIN hands `main` straight to dyld. That PR excludes Apple
from this fixture with a comment pointing here; the ELF and PE halves it does
change are covered by the Linux and Windows runners, which pass.

## Done

The macOS behaviour is established against a Clang oracle and written down. If
it is a writer gap, a destructor runs when a Mach-O image terminates through
`exit`, and `tests/basic_c_destructor_exit.c` runs on Apple with the exclusion
removed. If it is a platform divergence, the fixture states it and `AGENTS.md`
records it beside the `__mod_init_func` paragraph.

---

## forgejo#800 — link: dyld_info rejects a Mach-O image's bind opcodes as mis-aligned

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/800

`dyld_info` refuses every Mach-O image this linker writes:

```
$ dyld_info -fixups /tmp/fixture-buster
/tmp/fixture-buster [arm64]:
   mis-aligned LINKEDIT content 'bind opcodes'
```

The same command on a Clang-linked image prints the fixup table. dyld itself
accepts our images -- they load and run on the macOS runner, and CI is green --
so this is a tools-only refusal today, but it costs the fixup table on the one
platform where `otool`/`dyld_info` is the only way to see what the loader will
do, and it is what a code-signing or notarisation pass would look at next.

## Where it comes from

In `link_native_executable_mach_o64` (`src/buster/lib/compiler/link/link.c`)
the `__LINKEDIT` pieces are laid one straight after another:

```c
bind_offset = linkedit_file_offset + rebase_size;
```

`rebase_size` is however many uleb128 bytes the rebase stream happened to
need, so the bind opcodes start at an arbitrary offset. `symbol_table_offset`
just below it *is* aligned (`align_forward(bind_offset + bind_size, 8)`), which
is why the symbol table draws no complaint and the bind stream does.

## What to do

Align `bind_offset` (`align_forward(linkedit_file_offset + rebase_size, 8)`)
and check the rest of the `__LINKEDIT` chain for the same pattern -- export
info, function starts and the code-signature blob if they grow one later. The
gap bytes are already zero: the image buffer is `memset` to zero before
anything is written.

## How to validate

`dyld_info -fixups` on a buster-written image must print the table rather than
the refusal, and `otool -l` must still agree with the `LC_DYLD_INFO_ONLY`
offsets. Both need a macOS host, so this rides the `aarch64-macos-mini` runner:
a one-job probe branch (replace `ci.yml` with a single `runs-on:
aarch64-macos-mini` job that builds `ide` -- `./build.sh generate` first, then
`./build.sh build --config Release -t ide` -- links `tests/basic_c_constructor.c`
and runs `dyld_info` on it) turns this around in a few minutes. Found while
working #798.

---

## forgejo#806 — x86-64: the exact-form XCHG has no byte variant, so width-1 sequential atomic stores encode-fall-back

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/806

**Task for an agent.** x86-64 machine encoder vocabulary gap, small and sharply scoped.

## What is wrong

A sequentially consistent atomic store of a **one-byte** value selects on the x86-64 machine path (`MACHINE_X64_ATOMIC_STORE_XCHG`, payload width 1) but fails at the **encode stage** and falls back to the canonical emitter. Measured 2026-08-30 on main + PR 805:

```c
_Atomic char gc; void store_char(char v) { gc = v; }
```

`ide cc -v -c -fregister-allocator=fast` reports `CODEGEN_FALLBACK_STAGES verify=0 placement=0 encode=1`. Widths 2/4/8 encode fine. The same happens for one-byte atomic *aggregates* since PR 805 (its `keep_low_bytes` + XCHG path), and pre-existed on the scalar path — this is not a selection bug.

## Where to work

`src/buster/lib/compiler/codegen/machine_x86_64.c`: the exact-form registry entry `[34]` (`MACHINE_X64_XCHG_EXACT_FORM_ID = 9837`) resolves its operand widths from the row payload (`MEMORY_BASE_PAYLOAD_SIZE` / `GPR_PAYLOAD_SIZE`). The metadata form behind id 9837 evidently has no 8-bit variant (`xchg r/m8, r8` is opcode 0x86, a *different form* from 0x87), so the width-8-bit lookup fails and the encoder fails closed ("Migrated DIRECT rows have no handwritten byte fallback"). Follow how CMPXCHG spells its variants for the pattern.

## How to validate

The probe above at every allocator: `CODEGEN_FALLBACK_STAGES` disappears and `fallback_functions=0`. Then `tests/basic_c_atomic_aggregate.c` natively (its one-byte shapes stop encode-falling-back) and `ide test`. Byte XCHG needs a REX prefix for SIL/DIL/SPL/BPL sources — make sure the variant table covers those registers, an allocator is free to pick them.

## Definition of done

- Width-1 ATOMIC_STORE_XCHG encodes on the machine path; no encode-stage fallbacks left in `basic_c_atomic_aggregate.c` / an `_Atomic char` probe.
- `ide test` and `./build.sh test_self_host --config Release` green.

---

## forgejo#811 — x86-64: catch the machine selector up to the AArch64 i128 body subset

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/811

**Task for an agent.** x86-64 machine-selector catch-up: the AArch64 selector now carries a larger bare-__int128 body subset than x86-64, and every piece has a direct x86 spelling.

## What x86-64 refuses that AArch64 now selects (measured 2026-08-30, FAST)

Probe (each function alone, `-v -c -fregister-allocator=fast`, count CODEGEN_FALLBACK):

```c
typedef unsigned __int128 U128; typedef __int128 S128;
U128 add(U128 a, U128 b) { return a + b; }            // refuses (BINARY)
U128 sub(U128 a, U128 b) { return a - b; }            // refuses
U128 land(U128 a, U128 b) { return a & b; }           // refuses (and | ^)
U128 shl64(U128 a) { return a << 64; }                // refuses (left + signed-right shifts; only constant unsigned >> selects)
S128 ashr65(S128 a) { return a >> 65; }               // refuses
U128 mul(U128 a, U128 b) { return a * b; }            // refuses (needs MULX/ADC or mul+umulh-equivalent lowering)
```

Also refusing on x86-64: **i128-typed constants** (`(I128Wrapper){0}` in `tests/basic_c_aarch64_i128_abi.c` — `wrapper_after_one` falls back at opcode 14) and the **ordered i128 comparisons** in `check_i128`-shaped code (opcode 31). Equality and integer↔integer casts already select.

## Where to work

`src/buster/lib/compiler/codegen/machine_x86_64.c`. The AArch64 versions to mirror (same file layout on the a64 side): `machine_a64_select_constant`'s slot-backed halves branch, `machine_a64_select_i128_shift` (all three directions, constant amounts), `machine_a64_select_i128_binary` (bitwise per half; add/sub with the carry as a SETcc boolean off the low halves — x86 could instead use its native ADC once a flags-pairing row exists, but the boolean spelling needs no new rows; equality; the eight ordered comparisons as strict-high blended with unsigned-low behind high-equality). The x86 slot-classification list already covers CAST/BINARY for INTEGER-128; CONSTANT_INTEGER needs adding there.

## How to validate

The machine-subset section of `tests/basic_c_aarch64_i128.c` (checks 37–60) is target-independent and exercises every listed op at the half boundaries; today the driver tests only run it cross-compiled to aarch64 under qemu, so wire a native x86 lane across the four allocators alongside the fix, and break one expected value once to prove that lane gates. `ide test` + `./build.sh test_self_host --config Release`.

---

## forgejo#814 — aarch64: no 16-byte atomic access exists, so _Atomic __int128 fails to compile

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/814

**Task for an agent.** AArch64 canonical-emitter gap that fails compiles outright: no 16-byte atomic access exists on the a64 path, so `tests/basic_c_int128.c` cannot build for aarch64-linux at all.

## What is wrong (measured 2026-08-30)

```
build/Release/ide cc -c -target aarch64-linux tests/basic_c_int128.c
cc: error: C code generation failed with error 2, function 'test_atomic_wide' ... opcode 9 ...
```

`_Atomic unsigned __int128` (and any 9–16-byte atomic aggregate, e.g. `wide_round_trip`/`takes_nine`/`gives_nine` in `tests/basic_c_atomic_aggregate.c`, which are x86-gated today) has no lowering: the canonical AArch64 atomic load/store blocks in `codegen.c` accept widths 1/2/4/8 and return `CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION` at 16. x86-64 has its CMPXCHG16B path; AArch64 needs the LDXP/STXP exclusive-pair loop (baseline; LSE2's aligned LDP/STP single-copy atomicity is a model-gated improvement, not the starting point).

## Where to work

`src/buster/lib/compiler/codegen/codegen.c`, the canonical a64 `IR_OPCODE_ATOMIC_LOAD`/`ATOMIC_STORE` blocks (search `atomic_width != 1 && atomic_width != 2`), plus the RMW/CAS blocks if the fixture reaches them. The a64 machine selector should keep refusing width 16 (canonical fallback), like x86's machine path does around CMPXCHG16.

## How to validate

- The fixture compiles and runs under qemu-aarch64 across all four allocators (qemu executes exclusives faithfully).
- Un-gate the aarch64 side of the 16-byte shapes in `basic_c_atomic_aggregate.c` if they are target-gated, and check the bytes against clang's answers like the rest of that fixture.
- Cross-check against clang with the freestanding probe recipe; note clang's own seq-cst 16-byte atomics on baseline aarch64 also use CASP/LDXP loops — compare semantics, not bytes.
- `ide test` + `./build.sh test_self_host --config Release`.

---

## forgejo#818 — c: a positional initializer stores into anonymous bit-fields instead of skipping them

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/818

A positional brace initializer for a struct containing anonymous bit-fields
assigns initializers *to* the anonymous fields instead of skipping them, so
every named field at or after the first anonymous one receives the wrong
value (usually its right-hand neighbour's, the last ones zero). C11 6.7.9p9:
unnamed members of structure objects do not participate in initialization.

```c
struct S { unsigned a : 3; unsigned : 5; unsigned b : 7; };
int main(void) { struct S x = { 1, 2 }; return x.a * 10 + x.b; }
```

clang and gcc return 12 (`a`=1, `b`=2); `ide cc` returns 10 (`a`=1, the
anonymous field swallows the 2, `b`=0). Both register-allocator modes agree
with each other and disagree with clang. Found on `55dfe872` (2026-08-30).

Differentiation:

- member assignment (`x.a = 1; x.b = 2;`) is right,
- a designated initializer (`{ .b = 2 }`) is right,
- `= {0}` is indistinguishable (zero lands in the anonymous field),
- a zero-width separator diverges the same way:
  `struct { unsigned a : 3; unsigned : 0; unsigned b : 5; } x = { 1, 2 };`
  reads back `b` as 0.

Found by `tools/differential_c_harness.py --families bit_field`: of the 143
divergent units in a 120-seed run, 115 stop diverging when the brace
initializer is rewritten to `= {0}` and nothing else changes; the remaining
28 are the attributed-struct typedef alignment bug, filed separately.

---

## forgejo#819 — c: a typedef in a declaration defining an attributed struct takes the attribute operand as its alignment

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/819

A typedef declared in the same declaration as a struct definition that
carries an alignment-affecting attribute records the struct-level attribute
operand verbatim as the alias's alignment, instead of the struct's computed
alignment. The struct tag answers correctly, so one type has two alignments
— and both `_Alignof(alias)` and **objects declared through the alias** read
the wrong one.

```c
typedef struct __attribute__((aligned(4))) T { long long f : 13; } T;
int main(void) { return _Alignof(T) * 10 + _Alignof(struct T); }
```

clang and gcc return 88 (GNU `aligned` on a struct only raises: the type's
alignment is max(natural 8, 4) = 8). `ide cc` returns 48: `_Alignof(T)` is
the literal 4. `sizeof` agrees with clang through both spellings. Found on
`55dfe872` (2026-08-30).

The alias also ignores member contributions, in both directions:

```c
typedef struct T2 { _Alignas(32) signed char m; } __attribute__((packed, aligned(8))) T2;
/* clang: _Alignof(T2) == _Alignof(struct T2) == 32; ide: _Alignof(T2) == 8 */

typedef struct __attribute__((packed, aligned(2))) T3 { char a, b __attribute__((aligned(16))), c; } T3;
/* clang: both spellings 16; ide: _Alignof(T3) == 2 */
```

Objects declared through the alias are then genuinely under-aligned, locals
and globals both:

```c
typedef struct __attribute__((aligned(4))) T { unsigned int m __attribute__((aligned(16))); } T;
int main(void) { T loc; return (int)((unsigned long long)&loc % 16u); }
/* clang 0; ide (FAST) returns 8 -- the frame slot honors the alias's 4 */

typedef struct T5 { signed char m __attribute__((aligned(32))); } __attribute__((aligned(2))) T5;
static T5 g;  /* (unsigned long long)&g % 32u is nonzero under ide */
```

Whether a given object lands misaligned depends on the surrounding frame or
data layout, so FAST and `-fno-register-allocator` each show it on different
seeds; the guaranteed alignment is what is wrong. A typedef of a struct with
*no* definition-attached attribute is right (member `_Alignas` alone is
honored), and both attribute positions (between `struct` and the tag, after
the closing brace) are affected equally.

Found by `tools/differential_c_harness.py --families packed_aligned,bit_field`:
this single record explains all 101 divergent packed_aligned units of a
120-seed run (64 through the `_Alignof` value, 37 through the placement of
alias-declared objects) and the 28 bit_field divergences that are not the
anonymous-bit-field initializer bug, filed separately.

---

## forgejo#820 — c: a statement expression cannot be the operand of a lazily lowered control expression

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/820

A GNU statement expression as the operand of a lazily lowered control
expression is refused. Every control position fails; only the shapes that
lower eagerly work. Found on `55dfe872` (2026-08-30).

```c
int main(void) { if (({ 1; })) { return 7; } return 3; }
/* cc: error: in function 'main': unsupported C function-body statement or expression near '{' */

static int c;
int main(void) { if (({ c += 1; c % 2 == 1; })) { return 7; } return 3; }
/* cc: error: in function 'main': could not lower logical expression core */

int main(void) { int n = 0; while (({ n < 3; })) { n += 1; } return n; }
/* unsupported C function-body statement or expression near '{' */

int main(void) { int v = ({ 2; }) ? 5 : 9; return v; }
/* could not lower initializer expression for local 'v' */

static int hits;
int main(void) { int v = ({ 1; }) && (hits = 4); return v * 10 + hits; }
/* could not lower initializer expression for local 'v' */
```

clang compiles and runs all five. The eager contexts are fine, which is what
isolates the root cause to the lazy-operand prepass rather than statement
expressions generally:

```c
int main(void) { int t = 0; for (int i = 0; i < ({ 3; }); i += 1) { t += i; } return t; }  /* ok */
int main(void) { int v = ({ 5; }) == 5; return v; }                                        /* ok */
```

Initializer, call-argument, subscript, nested, struct-valued, and
comma-tail positions all work too (`tools/differential_c_harness.py
--families stmt_expr` runs clean once the condition shape is set aside; the
condition shape alone accounts for all 46 rejected programs of a 120-seed
run).

---

## forgejo#821 — c: compound assignment and increment on an atomic float are refused

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/821

Compound assignment and increment/decrement on an `_Atomic` float or double
are refused; the same operators on atomic integers work, and plain
load/modify/store of atomic floats works. Both are valid C11 (6.5.16.2p2
covers compound assignment on atomic types; the RMW is a CAS loop). Found on
`55dfe872` (2026-08-30).

```c
static _Atomic float gf = 2.0f;
int main(void) { gf += 1.5f; return (int)gf; }
/* cc: error: in function 'main': unsupported C function-body statement or expression near '1.5f' */

static _Atomic double gd = 2.0;
int main(void) { gd++; return (int)gd; }
/* cc: error: in function 'main': could not apply increment or decrement */
```

clang compiles both (lock cmpxchg loop) and returns 3 in each case.

Controls that already work:

```c
static _Atomic float gf = 2.0f;
int main(void) { gf = gf + 1.5f; return (int)gf; }   /* ok: plain RMW */

static _Atomic int gi = 5;
int main(void) { gi += 3; gi ^= 1; gi++; return gi; } /* ok: integer RMW */
```

Found by `tools/differential_c_harness.py --families atomic_qual` (31 of the
120-seed run's programs reject through these two spellings; everything else
in the family — all four `_Atomic` spellings, atomic aggregates at 8 bytes
and below with by-value pass/return, the power-of-two padding — runs clean).

---

## forgejo#822 — c: the ~ complex conjugation operator has no lowering

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/822

The GNU `~` complex conjugation operator is refused on `_Complex` operands:

```c
int main(void)
{
    double _Complex z = 3.0 + 4.0i;
    double _Complex w = ~z;
    union { double _Complex z; double xy[2]; } u;
    u.z = w;
    return (int)u.xy[0] * 10 + (int)(u.xy[1] < 0.0 ? -u.xy[1] : u.xy[1]);
}
```

`cc: error: in function 'main': this unary operator has no complex form`

clang and gcc compile it (`~` on a complex value is the GNU spelling of
conjugation, C99 Annex G `conj`) and return 34. All three complex types are
affected the same way. Found on `55dfe872` (2026-08-30) by
`tools/differential_c_harness.py --families complex_arith`; the rest of the
family (imaginary literals, `__real__`/`__imag__`, arithmetic including
division, by-value crossings for float/double/long double `_Complex`,
structs with complex members) runs clean.

---

## forgejo#823 — c: __builtin_complex is unbound, so musl's CMPLX macros cannot compile

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/823

`__builtin_complex` is not bound:

```c
int main(void)
{
    double _Complex z = __builtin_complex(3.0, 4.0);
    return (int)(double)z;
}
```

`cc: error: in function 'main': could not lower unbound identifier '__builtin_complex'`

clang compiles it and returns 3. This is the builtin behind musl's `CMPLX`,
`CMPLXF`, and `CMPLXL` macros (`math.h`), so any code constructing a complex
value from parts through the standard C11 macro fails to compile. The
`float` and `long double` forms are missing the same way. Found on
`55dfe872` (2026-08-30) by `tools/differential_c_harness.py --families
complex_arith`.

---

## forgejo#824 — c: the aligned attribute on a bit-field is refused where clang lays it out

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/824

The GNU `aligned` attribute on a bit-field member is refused; clang and gcc
accept it and it changes the layout:

```c
struct S { unsigned a : 3; unsigned b : 5 __attribute__((aligned(4))); };
int main(void) { struct S s = { 1, 2 }; return (int)sizeof(struct S) * 10 + s.a + s.b; }
```

`cc: error: alignment specifier cannot be applied to a bit-field`

clang returns 83 (`aligned(4)` pushes `b`'s storage unit to the next 4-byte
boundary: size 8). The refusal reads like a deliberate diagnostic, but it
diverges from the reference compilers on accepted-and-meaningful input, so
either the acceptance or the divergence needs deciding. `packed` after the
width is accepted fine (the width/attribute split of #693 holds); only
`aligned` is refused. Found on `55dfe872` (2026-08-30) by
`tools/differential_c_harness.py --families bit_field` (17 rejected programs
in a 120-seed run).

---

## forgejo#825 — c: a [*] forward declaration conflicts with its own definition

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/825

A `[*]` variable-length array declarator in a function's forward declaration
is treated as a different type from the definition's `[n]`, so the pair is
refused as conflicting:

```c
static int sum(int n, int a[*]);
static int sum(int n, int a[n]) { int t = 0; for (int i = 0; i < n; i += 1) { t += a[i]; } return t; }
int main(void) { int d[3] = { 1, 2, 3 }; return sum(3, d); }
```

`cc: error: conflicting declaration of 'sum' (previous type 4, new type 9)`

clang compiles it and returns 6. C11 6.7.6.2p4: `[*]` declares a VLA of
unspecified size and is only valid in declarations with function prototype
scope that are not definitions — it is the standard way to forward-declare
exactly this definition, and both parameter types are pointers after
adjustment anyway. (The diagnostic's "type 4 / type 9" internal indexes also
leak into the message where a type spelling should be.) Found on `55dfe872`
(2026-08-30) by `tools/differential_c_harness.py --families param_decl`; the
rest of the family — `[n]`, `[static n]`, qualifier spellings, 2-D VLAs with
runtime `sizeof`, expression bounds, local VLAs — runs clean.

---

## forgejo#829 — c: the GNU __atomic_* builtin family is not implemented

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/829

CPython's configure probes `__atomic_load_n`/`__atomic_store_n` for HAVE_BUILTIN_ATOMIC; the frontend implements the `__c11_atomic_*` family only, so the probe fails and pyconfig.h diverges from the Clang reference configure (Clang: `#define HAVE_BUILTIN_ATOMIC 1`).

Reproducer:

```c
int main(void)
{
    int v = 0;
    __atomic_store_n(&v, 5, 5 /* __ATOMIC_SEQ_CST */);
    return __atomic_load_n(&v, 5) == 5 ? 0 : 1;
}
```

`ide cc` refuses with `use of undeclared identifier '__atomic_store_n'`; clang and gcc accept.

The GNU family differs from `__c11_atomic_*` in that it takes ordinary pointers (no `_Atomic` qualification) and has the `_n` value forms plus sized generic forms. Beyond CPython's probe, glibc headers and many projects (QuickJS uses `__c11`; CPython, mimalloc's fallback paths, and most Linux userland use `__atomic_*`) reach for it. CPython 3.13 itself builds without it (the C11 stdatomic path in pyatomic covers it), so this is a divergence-and-breadth gap rather than a build blocker there.

Found by the test_cpython harness work (v3.13.9 configure differential).

---

## forgejo#830 — c: incompatible function pointer assignment is accepted silently

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/830

Assigning a function of one prototype to a pointer of an incompatible prototype compiles silently. Clang makes this an error by default since Clang 16 (`-Wincompatible-function-pointer-types` promoted); GCC diagnoses it too. The silent acceptance flips autoconf feature probes that rely on the diagnostic: CPython's readline check compiles

```c
typedef int rl_hook_func_t(void);
extern rl_hook_func_t *rl_startup_hook;
extern int test_hook_func(const char *text, int state);
int main(void)
{
    rl_startup_hook = test_hook_func;  /* int(const char*,int) into int(*)(void) */
    return 0;
}
```

and reads the failure as "rl_startup_hook takes no arguments". `ide cc` exits 0, so CPython configures `Py_RL_STARTUP_HOOK_TAKES_ARGS 1` on GNU readline where the Clang reference configure does not — the one readline-module divergence left after the other configure fixes.

This is a type-checker breadth item: assignment/initialization/argument-passing of function pointers needs prototype compatibility checking (return type, parameter list, variadic-ness), with the same carve-outs the reference compilers apply (unprototyped `()` compatibility, C11 6.2.7).

Found by the test_cpython harness work (v3.13.9 configure differential).

---

## forgejo#831 — codegen: inline-asm refusals report opcode numbers instead of naming the rule

Closed 2026-08-31. Original: https://code.buster14a.com/buster/buster/issues/831

An inline-assembly template the codegen refuses reports an internal-shaped error instead of a named source diagnostic:

```c
int main(void)
{
    unsigned int x;
    __asm__ __volatile__ ("movl %%eax, %0" : "=m" (x));
    return 0;
}
```

```
cc: error: file.c: C code generation failed with error 2, function 0 ('main', state 1, blocks 1, instructions 5), instruction 2, opcode 37, operation 73, source 1:33, referenced symbol '<none>'
```

The refusal itself is by design (a literal general register in a template stays refused outside the documented exceptions — AGENTS.md's inline-assembly model), but the report should name the rule the way the frontend's refusals do ("asm names a literal register the emitter could also hand to an operand", with the register's spelling), not leak opcode/operation numbers. The unsupported-mnemonic case (a template naming an instruction outside the allowlist) reports the same opaque shape.

This is the same class the end-of-body refusal fix (PR 507) closed for the frontend: a refusal must name itself. CPython's configure hits it via the `HAVE_GCC_ASM_FOR_X64` probe (`__asm__("movq %rcx, %rax")`), where the refusal is correct but the diagnostic is unreadable.

Found by the test_cpython harness work (v3.13.9 configure differential).

---

## forgejo#833 — c: a call returning a function pointer cannot be called directly (f(x)(y))

Closed 2026-08-30. Original: https://code.buster14a.com/buster/buster/issues/833

A call expression whose callee is itself a call — a function returning a function pointer, called directly — fails call preparation:

```c
typedef int (*fn_t)(int);
static int double_it(int x) { return x * 2; }
static fn_t pick(int unused) { (void)unused; return double_it; }
int main(void) { return pick(0)(5) == 10 ? 0 : 1; }
```

```
cc: error: file.c: in function 'main': could not prepare C calls
```

clang and gcc accept and return 0. The related chain shape `f(x)->member(args)` lowers since the call-member-call fix (the token->call chain link `token_index == open_index` is followed from both ends there); this direct `f(x)(y)` shape fails earlier, in `c_ir_prepare_calls_discover` itself, and predates that fix. Workarounds in source are `(pick(0))(5)` or binding the pointer to a local first — both compile.

Found by the test_cpython harness work while reducing `Py_TYPE(self)->tp_free(self)`.
