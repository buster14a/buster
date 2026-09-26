# Unnamed-field ABI exchange: independent follow-up to #1344

Experimental probes only; no compiler changes or merge request.

Compiler pin: ade6ac4b6ecb21f30b61b656439bac476c145e2f.
Tree: 4c5306221fdb22fccc929b55e333163742de17d0.
Status and outcomes belong on GitHub issue #1344, not a parallel ledger.

## Predeclared law and independent expectations

Caller and callee include the exact same C17 declarations. Each is compiled
separately, without LTO, using the same native target and semantic options.
The source has compatible prototypes and tags, fully initialized named fields,
unsigned shifts of 8/16/24 bits, no padding reads, no aliasing tricks, no
packed extensions, no pointers or lifetime hazards. Scalar observer storage
and declarations match the caller/callee definitions. Both observers check
CHAR_BIT=8 and 32-bit unsigned before observing.

Every ordinary nested case transfers named byte values 1,2,3,4 and an unsigned
sentinel 0x10203040. The expected packed payload is the literal 0x04030201;
its XOR result is 0x14233241. The single-record control transfers 2,3 and has
payload 0x00000302/result 0x10203342. `observer.c` does not include contract.h,
declare any suspect type, ask any Buster layout to compute an expectation,
or derive expectations from an independently compiled layout dump.

The reported producer/consumer sizeof values are diagnostic observations only:
they cannot change the observer's expected payload/tag/result or exit status.
The negative control deliberately changes only one literal expectation and
must fail with exactly one mismatch on a known reference/reference pair.

WG14 N1570 sections 6.2.7 and 6.5.2.2 define compatible declarations across
translation units and value transfer through a prototyped call. Section
6.7.2.1 permits the unnamed unsigned-int zero-width fields. C alone does not
prescribe every layout: bit allocation and ordinary-member alignment have
implementation-defined choices, and allocation-unit alignment is unspecified.
Padding contents and unused register bits are not observed or constrained.
The selected target ABI resolves the relevant layout/call rules.

Primary references:
- https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
- https://github.com/ARM-software/abi-aa/blob/2025Q1/aapcs64/aapcs64.rst
  Sections 5.10, 10.1.8 (including unnamed/zero-width container alignment),
  and 6.8 B.5/C.9/C.12 (rounding and aggregate/integer argument registers).

## Discriminating shapes

| Row | Shape | AAPCS64 required bytes | Pinned-source predicted bytes |
|---|---|---:|---:|
| 0 | ordinary nested chars | 4 | 4 |
| 1 | char, unsigned-int :0, char | 8 | 5 |
| 2 | char, row-1 inner, char | 16 | 7 |
| 3 | row-2 with inner's first char explicitly aligned to 4 | 16 | 16 |
| 4 | array of two row-1 records | 16 | 10 |

Row 1 can exchange correctly despite an incorrect object size: both shapes
consume one GPR and the observed members have the same offsets. Row 2 should
expose both member-offset and later-scalar register disagreement. Row 4 keeps
two GPRs on both sides but changes the second element's member offsets. Rows
0 and 3 must pass. x86-64 is a legitimate different-layout target control;
all exchange results must still preserve the same named values there.

These are predictions, not recorded executions. Retain contrary observations.
Do not infer a deterministic value for a wrongly read padding/register byte.

## Hosted experiment

Four reference configurations (Clang/GCC x O0/O2), eight Buster configurations
(NONE/MIR_STACK/FAST/QUALITY x direct SSA/memory frontend), and separately
Clang/GCC-compiled observers. Each Buster compilation uses real `ide cc`,
`-fverify-codegen`, and no MIR fallback for MIR modes. All combinations of the
four reference callers/callees are controls; Buster-to-Buster pairs use matching
configurations; mixed pairs cover both directions against all four references.

Per architecture: 176 ordinary observer executions, 5 rows and 15 checked
scalar cells each, plus two negative-control executions. Rows/cells do not
claim statistical or causal independence. Preserve compile/link failures,
timeouts, crashes, missing/incomplete summaries and nonzero observations.

`runner.c` is research-only hosted C process orchestration; the workflow uses
an isolated detached pristine production worktree, the documented hosted
Clang build-driver bootstrap exception and native GitHub-hosted executors.
No compiler execution or benchmark occurs on the investigator's desktop.
A green workflow means complete evidence collection with passing independent
controls, not Buster semantic acceptance. `semantic_failures` is explicit.
Disassembly is retained to explain executed behavior, never used as a
substitute for execution or as an acceptance oracle.

Compile the runner with hosted Clang and invoke:

```
exchange-probe /absolute/path/to/ide /absolute/path/to/this-directory /absolute/path/to/fresh/results aarch64-linux
```

The output directory must already exist and be fresh. Paths are limited to
ASCII letters, digits, slash, underscore, hyphen and dot by the runner.
Each launched process has a 45-second timeout and separate retained log.

No production ownership is claimed. Coordinate any later c_parse.c/c_gen.c
edits with #1247 / PR #1278. Preserve canonical validation, C-only production,
existing lane/SIMD contracts, self-hosting and platform gates. No retirement,
service, admission, deployment, generated binding, benchmark or default change.
Actual investigator: GPT-6 Astra Pro; no callable subagent tool was exposed.
