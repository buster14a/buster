# Zen 5 instruction observations

`x86_zen5.c` is the first measured slice of [#181](https://github.com/buster14a/buster/issues/181).
It supplies an explicit unknown value, operand-sensitive IDs, bounded SIMD
lookups and a reproducible instruction microbenchmark. It is a partial model.
It does not yet satisfy #181's complete processor-model acceptance criteria.

## Coverage and consumers

There are 57 measured cases: selected 32- and 64-bit integer arithmetic,
immediates, unary operations, shifts, multiplication, two LEA addressing cases,
constant moves, zeroing XORs and an aligned L1 pointer load. A form with a
register operand and the same form with a memory operand are different cases.
Zeroing XOR and ordinary XOR are different too. The cases are intentionally
explicit; no mnemonic or operand-width rule assigns costs to unmeasured forms.

Tier 0 (#136) and Tier 1 (#141) have no emitted-form manifests on the starting
revision, `5ca41258f9922739143318367ad815f71825d045`. Their selectors, scheduler
(#172) and rematerializer (#178) do not exist there. The test's own fixtures
therefore cannot establish emitter coverage or count as a compiler consumer.

Remaining acceptance work:

- Attach each real emitter's independent manifest to `x86_zen5_missing`, and
  measure every missing operand case. An empty manifest proves nothing.
- Establish flag-result latency, executed micro-op counts, pipe constraints and memory-source
  latency increments. These columns currently remain **unknown**. The load
  observation is an address-to-data L1 dependency, not an additive surcharge
  that applies to every instruction with a memory operand.
- Add floating-point, vector, flag-result, branch, store and other cases that
  the actual emitters use. Data-dependent operations need operand ranges and
  multiple measurements, not one magic latency.
- Integrate a real selection, scheduling or rematerialization decision and
  compare generated-program performance with the decision disabled on the
  9700X. Neither a fast lookup nor absent consumers establishes that scheduling
  or a cost model pays, or that it does not pay.

The unrelated semantics work in #221 / PR #239 owns instruction effects and
legal substitutions. This model does not duplicate or change those rules.

## API and representation

Call `x86_zen5_initialize` once before worker threads. It does not change the
encoder or the generated-code ISA policy. Then:

- `x86_zen5_id(record)` accepts a validated 64-bit-mode instruction and resolves
  its case to a byte ID. Carry that ID alongside selected instructions; this
  scalar resolution is not part of the vector lookup timing.
- `x86_zen5_lookup(ids, count, column, output)` looks up 32 IDs per iteration
  with `vpermi2w` / `vpermt2w`. A column occupies two ZMM registers. The scalar
  twin is `x86_zen5_lookup_scalar`. Input and output are disjoint bounded slices.
  Any byte outside the known ID range returns unknown; it cannot wrap through
  the permute. Both tails use masks, including reads of the byte IDs.
- `x86_zen5_missing(records, count, required_columns)` counts records lacking
  any requested field. A known form with unknown pipes fails a request for
  pipes. Callers must not replace unknowns with zero or use them as numerical
  costs. `required_columns` contains bits for `X86CostColumn` only.

Row zero and every unmeasured field are `UINT16_MAX`. Cycle quantities and
retired macro-ops use unsigned fixed point with 256 units per cycle or op.
There is no silent saturation. The table's compile-time capacity check stops
growth past 64 IDs; expanding coverage will require either more lookup banks
or a measured, explicitly justified sharing of cost classes.
`x86_zen5_measured_fields[id]` marks the columns supplied by the project's
9700X observations; unmeasured fields have no bit set.

The measured immediate cases currently accept the exact recorded immediate.
In particular a shift by 0 or a multiple of the operand width never acquires
the cost of a shift by 3. Unsupported addresses, segment overrides, fixup
flags, decorations and unmeasured zeroing SUBs return unknown. The L1-load
case assumes an aligned, ordinary, cacheable L1 hit; record inspection cannot
prove that runtime condition. Tuning never grants permission to emit an ISA
feature: that remains `x86_target.c`'s job.

The simple-LEA case excludes RBP/R13 bases: those force an encoded displacement
even for displacement zero. An initial twelve-register sweep reported 1.1667
retired macro-ops per simple LEA because two of its twelve bases had that
encoding. The final throughput fixture keeps the base at R14 and varies the
index; the base and index dependency chains remain separate. The excluded
address cases return unknown rather than inheriting an average of two cases.

## Measurement method

`measure-x86-zen5` runs original C-generated fixtures through the existing
encoder. Each generated function preserves the SysV callee-saved registers,
aligns its loop to 64 bytes, runs 240 target instructions per iteration, and
returns. The code mapping is writable while being constructed and executable
only after construction. Both encoders must produce identical code before it
can run, and every record must pass `x86_validate`.

Six experiments run in rotating order over an odd number of passes:

1. Twelve independent register chains (an address-independent repeated L1
   load for the load case).
2. Six independent chains, matching the register budget used in the mixes.
3. A chain through the written operand, or through the base for LEA.
4. A ring of twelve registers carrying the other source's result into the
   next instruction, or the index for LEA. Single-source forms repeat the
   first chain. The block length is a multiple of twelve, preserving the
   dependency across the back edge.
5. Six target chains interleaved with six independent ADD chains.
6. Six target chains interleaved with six independent IMUL chains.

The ring avoids accidentally turning an ordinary XOR or SUB into its zero
idiom. IMUL with an immediate chains its source into the result. Loads use an
aligned word pointing to itself: the chain measures an L1 address dependency,
not a memory hierarchy or aliasing workload. Zeroing XOR and immediate moves
have no GPR input dependency; their repeated-destination measurements are
throughput observations, not evidence of a fractional dependency latency.
`X86_COST_GPR_LATENCY` concerns the GPR result. `X86_COST_FLAGS_LATENCY`
remains unknown so a flag dependency cannot accidentally acquire a GPR latency.

Counters are one pinned perf group: core cycles, retired instructions and AMD
PMCx0C1, **retired macro-ops**. The latter's meaning is exposed by the host's
`perf list --details` (`ex_ret_ops`). It is not an executed-micro-op counter.
The command refuses other processors, failed reads, unavailable counters,
multiplexed groups and unexpected retired-instruction counts. It does not
replace failures with zero. It checks the instruction count against the
generated loop's exact body count, allowing a small fixed wrapper overhead.

Cycles retain loop overhead, because subtracting empty-loop cycles from a
throughput loop would remove work that overlapped the measured instructions.
Retired macro-ops subtract a separately measured empty loop, including its
loop branch and wrappers. Mixed cycles and counts are per **pair**, not per
instruction; a mix reveals contention but cannot uniquely name physical
pipes. Compare each mix with the **six-chain** measurements of both the target
and blocker: six chains of a 3-cycle multiply alone cannot run faster than
0.5 cycles per instruction, even when twelve independent chains reach 0.333.
That dependency limit is not evidence of contention with an ADD. A mix can
hide resource limits behind its dependency limits, so it is only a diagnostic.
The command prints medians, min/max cycles and an FNV-1a hash of each
generated code block. These hashes audit generated bytes; they are not
cryptographic source identities.

Run every command separately under the shared host lock:

```sh
EXCLUSIVE_OWNER=issue-181 ~/buster-fast-bench/exclusive.sh ./build.sh
EXCLUSIVE_OWNER=issue-181 ~/buster-fast-bench/exclusive.sh ./build/compiler test-x86-zen5
EXCLUSIVE_OWNER=issue-181 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler measure-x86-zen5
EXCLUSIVE_OWNER=issue-181 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler measure-x86-zen5 --only IMUL_R64_RM64/REGISTER --passes 11 --iterations 20000
EXCLUSIVE_OWNER=issue-181 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-x86-zen5
EXCLUSIVE_OWNER=issue-181 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-x86-zen5 --only vector --after-scalar 200
```

The lookup benchmark checks equality before timing, reports 21-pass median
nanoseconds and core cycles per ID, and can run each implementation alone or after scalar
conditioning. It measures a hot 65,536-ID synthetic column and its outputs.
It is not a whole-compiler or generated-program speed measurement.

## Recorded observations

The [2026-10-22 evidence](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/x86-zen5-2026-10-22/README.md) retains
three complete 9700X processes, their source/binary identities, generated-code
hashes, reductions and lookup observations. All three agree on every code hash;
their independent-throughput medians differ by at most 0.0535%.

| Observed case | GPR latency (cycles) | Block throughput (cycles/instruction) | Retired macro-ops |
| --- | ---: | ---: | ---: |
| Regular integer ALU and simple LEA | 1 | about 0.18836 | 1 |
| Zeroing XOR | unknown | 0.14172 | 1 |
| Immediate shifts | 1 | 0.33475 | 1 |
| Two-/three-operand IMUL | 3 | 0.33336 | 1 |
| Scaled LEA plus displacement | 2 | about 0.45686 | 2 |
| MOV r32, imm32 | unknown | 0.16741 | 1 |
| MOV r64, imm64 | unknown | 0.33478 | 1 |
| Aligned L1 MOV load | 4 (address to data) | 0.25003 | about 1 |

These are the explicit fixtures' measurements, including loop overhead. The
table stores throughput and macro-ops rounded to 1/256, and dependent GPR
latencies rounded to whole cycles. It does not extrapolate them to all operand
values, addresses, flags or layouts. Of the 57 cases, all have measured
throughput and retired macro-ops; 53 have measured GPR dependency latency.
The other four columns remain unknown for every case.

## Validation and provenance

`test-x86-zen5` compares every byte ID for every length from 0 through 256,
every column, and tails ending at guard pages. It checks operand distinctions,
unknown fields and unsupported cases, then validates, cross-encodes and runs
every generated loop. Timing values are not hard correctness thresholds in CI.

The local ASan/UBSan run also executes these tests. Only Clang's function-type
metadata probe is disabled at the generated-code call boundary: the generated
entry point has no Clang metadata before it. Address checks and the other
undefined-behavior checks still cover the wrapper, fixture construction, model
and lookups.

The implementation, fixtures and measurement data are original project work.
No uops.info XML, XED data, LLVM scheduling table, nanoBench implementation or
other third-party timing dataset is incorporated. uops.info is background
research, not a source of table values. The hardware counter identifier is an
architectural interface; Linux perf and Clang are external tools. This follows
#181's option to generate observations on the owner's hardware without
choosing terms for a third-party timing-data import.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). The
[model](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/x86_zen5.c#L1)
produces operand-sensitive case IDs and cost columns consumed by
[tier1 scheduling](tier1-scheduling.md#boundary-audit-298), its verifier,
measurement harness and tests. Form identity alone is insufficient when the
operand/address case changes. Missing columns/cases retain the explicit unknown
sentinel; they must not become a zero-latency readiness edge.

`x86_zen5_id` classifies one validated physical record into a u8 case ID.
`x86_zen5_lookup` consumes a counted u8 ID column and a valid `X86CostColumn`,
writing disjoint u16 results: 32 lookups through two ZMM table registers, with
masked tails and no caller padding requirement. Natural alignment suffices.
Cycle costs use units of 1/256 cycle; `UINT16_MAX` means unknown, including
unsupported IDs. Startup initializes the 64-entry columns before workers;
they remain immutable while readers run. Caller ID/output storage must survive
the lookup and subsequent cost consumers respectively.

The scheduler retains case IDs and reports unknown/missing columns rather than
repeatedly querying names or inventing a port model. Its physical-record
producer is a fixture at this pin, not integrated source Tier 1 selection.
Disposition: retain measured case identity and conservative unknown handling.
`test-x86-zen5` checks every tail and guarded slices, and the generated harness
executes its loops. [#181](https://github.com/buster14a/buster/issues/181)/[#172](https://github.com/buster14a/buster/issues/172) own broader observations and actual producer +
lookup + scheduler + generated-program measurements; a lookup benchmark or
microbenchmark latency is not proof of whole-block or compiler improvement.
