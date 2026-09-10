# Self-host SIMD parity: source translation

Related current GitHub issues: [#297](https://github.com/buster14a/buster/issues/297),
[#129](https://github.com/buster14a/buster/issues/129),
[#61](https://github.com/buster14a/buster/issues/61), and parent
[#128](https://github.com/buster14a/buster/issues/128).

## Frozen source and classification

This source review starts at main
`ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae`. The translator change is an
**enablement-gap correction**, not an accepted compiler-throughput optimization.
Source availability does not prove which instructions execute in a particular
binary. Final-binary disassembly and tests are required separately.

The selected production consumer is the C source translator's bounded 64-byte
load/classify/copy loop and its plain-run scanner. The population is consecutive
source bytes; CR, LF and backslash equality masks classify exceptional work. The
existing ordered scalar processing handles CR normalization, escaped newlines,
window crossings and tails. This change does not vectorize checkpoint creation,
change its representation, or repeat the previously rejected checkpoint-batching
experiment documented in `performance-audits/2026-08-30T182357Z.md`.

## Feature tiers and builtin path

`BUSTER_SIMD_512_BASE` requires non-MSVC x86-64 with AVX-512F and AVX-512BW.
It enables only the four public operations needed by this consumer: `simd512_load`,
`simd512_store`, `simd512_splat_byte`, and `simd512_equal_byte`. Trusted host builds
use the existing intrinsics; self-built stages use the corresponding existing
`__builtin_buster_simd_*` operations, without vendor headers. Unsupported targets
use the scalar implementations.

`BUSTER_SIMD_512` retains the existing stronger F/BW/VBMI/VBMI2 requirement. It must
not replace the translator's weaker feature test: doing so would remove an
existing F/BW-only host fast path. Advanced operations still fall back below that
full tier. Their private byte views work with either the scalar structure or the
basic-tier vector representation; arguments to all public SIMD macros must remain
side-effect-free.

The basic operations already pass through the frontend builtin registry and
canonical SIMD validation into both native paths. Relevant symbols include
`c_ir_simd_builtins`, `ir_simd_operation_shape`, `ir_canonical_simd_valid`,
`codegen_canonical_x64_simd_supported`, `machine_x64_simd_supported`, and
`machine_x64_select_simd`. The existing machine operations are `VLOAD_PTR`,
`VSTORE_PTR`, `VSPLATB`, and `VPCMP_MASK`. This change adds no opcode, persistent
instruction stream, IR, mask-register bank, allocator policy or backend pass.
Masks remain ordinary `u64` values. Instruction availability and useful lowering
must still be verified in final binaries; a function name is not ISA evidence.

## Remaining guards and independent gaps

| Consumer | Trusted host eligibility | Self-built eligibility after this slice |
| --- | --- | --- |
| Source translation and plain-run scanning | F/BW basic tier | Same feature tier through existing builtins |
| Compact C lexing | Full F/BW/VBMI/VBMI2 tier and not `__BUSTER__` | Still excluded; existing scalar/reference lexer |
| Delimiter-sidecar vector scan | Existing compact-lexer guard | Still excluded with compact lexing |
| x86 encoding-metadata SIMD decode | Existing host-only feature/compiler guard | Existing fallback; not changed here |
| Literal/preprocessor classification using `simd.h` | Existing full-tier guards | Already expressible through the vocabulary where target features enable it |
| Canonical-row and FAST ownership scans using `simd.h` | Existing full-tier guards | Existing builtin path, not new enablement in this change |

Short inputs and final partial blocks deliberately retain scalar/SWAR processing;
64-byte loads and stores occur only with at least 64 valid input bytes. No new
runtime dispatch or CPU/OS feature-detection mechanism is introduced.

#129's additional dword vocabulary requests are not prerequisites for byte
translation. Their status must be judged operation by operation against current
source. #61's historical code-size and throughput observations concern generated
compiler code, which can remain inflated even when a SIMD kernel is enabled.
No historical timing is treated as a current-main measurement, and this slice
does not attempt inlining, stack-slot optimization or a new optimizing backend.

## Regression and acceptance evidence

`force_scalar` previously disabled the fused translator but could still enter the
AVX-512 plain-run scanner. Its test-only path now walks bytes, making the existing
scalar differential seam independent of both optimized translation scans.
The existing differential test additionally checks lengths 0 through 193 ending
at an inaccessible page, plain bytes and CR/LF/backslash boundary patterns. It
compares translated bytes, source mapping/checkpoints and token metadata through
the existing test helpers.

`tests/basic_c_simd_translate.c` includes the production header. Its `main` is not
feature-guarded: it checks every byte value at every lane, exact copies, unaligned
input/output and output canaries, masks, crossing pairs, and advanced-operation
fallback smoke cases. `buster_simd_translate_block` is a stable symbol for ISA
inspection. The existing driver suite executes it in `none`, `mir-stack`, `fast`
and `quality`, and separately compiles baseline, F/BW-only `skylake-avx512`, and
full-feature `znver5` targets with a non-vacuous SIMD-operation-count assertion.
These are newly registered checks, not a claim that they have passed.

Before accepting the change, require the submitted revision's ordinary self-host
fixed point, repeated-generation audit, full tests, supported mode/platform and
sanitizer gates, and inspect emitted instructions and fallback binaries. Keep the
PR draft until that evidence is available.

Performance must use the existing native throughput harness with frozen identical
inputs and flags. Report trusted Clang-built baseline/candidate separately from
self-built baseline/candidate, and distinguish both from building Buster itself
and generated-program runtime. Record feature eligibility, exact CPU/OS, compiler,
affinity/SMT, input hashes, raw paired samples, uncertainty, phase/whole times,
memory and unavailable counters. Hosted CI is not verified physical Zen 5 or Zen 4
evidence. No speedup, size-budget acceptance or physical-hardware measurement is
claimed by this document; execution results and exact run links belong to #297
and the implementation PR.
