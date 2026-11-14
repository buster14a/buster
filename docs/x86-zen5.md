# The Zen 5 processor model

`x86_zen5.c` is [#181](https://github.com/buster14a/buster/issues/181)'s model of
the Ryzen 7 9700X: for every case Tier 0 and Tier 1 emit, a row of the 9700X's
own measurements, which the backend's decisions read through byte IDs and
in-register lookups. Its purpose is relative choices (which of two sequences,
which record first), not cycle-exact prediction: every decision that uses it is
still confirmed by measuring generated programs.

## Cases and IDs

A **case** is a form (`X86Form`) and an operand kind (`X86CostOperands`):

| Kind | Records |
| --- | --- |
| `REGISTER` | every operand a register, or none |
| `ZERO` | an idiom of two equal sources (xor, sub, pxor, xorps, vpxord, vxorps of a register with itself; pcmpeqd's all ones), for the forms that have such a row |
| `MEMORY` | a memory operand of a base and a displacement, of RIP, or of a displacement alone |
| `MEMORY_INDEX` | a memory operand with an index (a gather's vector index too) |
| `LEA_BASE` | LEA of one component and a displacement (`[base + disp]`, `[rip + disp]`) |
| `LEA_INDEX` | LEA of `[base + index]` |
| `LEA_SCALED` | LEA of `[base + index * 2, 4 or 8]` |
| `LEA_THREE` | LEA of `[base + index * scale + disp]`; RBP or R13 as the base force a displacement and make `[base + index]` one |
| `MASKED` | an EVEX record of registers merging into its destination under an opmask |
| `MASKED_MEMORY` | an EVEX record of a memory operand (a base, no index) under an opmask: a masked store (merging) or a zeroing masked load |

A record whose form has no row of its kind, an index without a base, or a
32-bit address has no case (ID 0, every column unknown): a merging opmask on a
form measured without one, and a load merging into its vector destination, are
such records. Some things don't change a record's case: its immediate's value,
an EVEX zeroing opmask on registers or rounding decoration, a segment override
and a fixup mark. The immediates and decorations are measured (below, "What
doesn't change a case"); a segment override (TLS) and a fixup mark are assumed
alike, as the instruction's execution is the form's. A merging opmask, and any
opmask on memory, do change a case, which the sweep found: they have their own
kinds.

`x86_zen5_rows.c` has 1,349 rows: the cases Tier 0 and Tier 1 emitted in their
tests (with CI's options: `test-tier0 --cross-check`, `test-ir-lowering`'s
programs, `test-tier1-masks` for x86-64-v2, -v3 and -v4) and in compiling
SQLite's amalgamation and speedtest1 and Lua 5.4.8 (both tiers, the default
target and `-march=znver5`), and their families: every width and operand kind
(registers, memory, memory with an index) of the integer ALU operations, the
tests, the unary operations, the shifts, the moves and extensions and `imul`;
all 16 conditions of jcc, setcc and cmovcc, which a program's conditions choose;
every width and element of the broadcasts, `vpmovm2*`, `vpternlog` and `vpxor`
Tier 0's vector code emits; and the 57 cases of the first model. A generated
program can take any width of a family, which only some seeds show. `x86_zen5_initialize` gives the rows that agree in every column and
mask one dense ID (fewer than 256: the initialization fails past them). A
record resolves to its ID once (`x86_zen5_id`); consumers carry the byte as a
column (`Tier1SelectCode.cost_ids`) and look up 32 at a time
(`x86_zen5_lookup`): four banks of 64 16-bit entries, eight ZMM registers per
column, a `vpermi2w` per bank and two blends by the ID's bits 6 and 7. Every
byte indexes a slot, the slots past the rows unknown, so no ID wraps. The
scalar twin (`x86_zen5_lookup_scalar`) is a load per ID.

## Columns

Cycle quantities and op counts are fixed point, 256 a unit (`BUSTER_X86_COST_SCALE`).

| Column | What it is |
| --- | --- |
| `LATENCY` | the longest latency from a source operand to the register result: a register source's, an address register's for a memory kind, a read-modify-write's through its memory |
| `THROUGHPUT` | reciprocal throughput: cycles per instruction among independent instances |
| `RETIRED_OPS` | AMD PMCx0C1, retired macro-ops per instruction |
| `FLAGS_LATENCY` | the latency from a register source to the status flags |
| `LOAD_EXTRA` | a memory kind's extra latency from the address to the result, over the register path (or the flags' path, for a compare) |
| `FP_OPS` | AMD PMCx0AB (umask 4): macro-ops dispatched to the floating-point unit per instruction |
| `PIPES` | `BUSTER_X86_PIPE_*` bits: the blockers of known pipes this case measurably contends with |

Each column of each row is one of three (`measured`, `derived`, `absent`
masks), and `x86_zen5_accounts` asks whether a row accounts for a set:

- **measured**: this repository's harness observed it directly on the 9700X;
- **derived**: computed from its observations with an assumption this document
  states (a bridge's share of a round trip, a helper's one macro-op);
- **absent**: the case has no such path (a constant's move has no source, a
  store no register result, a form that doesn't write the flags no flag
  latency, a register case no load), or the harness has no experiment that
  separates it (listed below). Its value is unknown.

An absent or unknown value is `BUSTER_X86_COST_UNKNOWN`, never zero; consumers
don't treat it as a cost. Latencies within a tenth of a cycle are stored as the
whole cycle, others to an eighth; throughputs to a 64th; op counts to an
eighth. The coarse steps let equal costs share an ID.

## The harness

`measure-x86-zen5` builds every fixture from the encoder's form table and the
semantics module's effects (`x86_effects`, `x86_effects_role`), not from
per-mnemonic code: a case's roles (ModRM.reg, ModRM.rm, VEX.vvvv, an is4
register), each one's register file (general, vector, opmask, x87) and access
(read, written, merged), its implicit registers, flags, and memory access give
its experiments. A program is a loop of 240 units (a unit is a target
instruction and the records around it), 64-byte aligned, wrapped in a function
that saves the callee-saved registers, sets every register it reads from the
data's constants, and returns with the x87 stack empty and the upper vector
state clean. Both encoders must agree on every byte before the mapping becomes
executable; every target record must resolve to the case it measures
(`x86_zen5_program_targets`), so that no fixture measures a neighbouring case
(an index shape, a forced displacement, an idiom) by accident.

**Registers and values.** Written general registers come from twelve (RAX, RDX,
R8 to R11, RBX, RBP, RCX, RDI, R13, R15) less those the form names implicitly;
R14 holds the constant 3, RSI the data, R12 the iteration count. Vector
registers 0 to 11 are written and 13 to 15 are constants, opmasks 1 to 5 and 6
and 7. A masked case's target runs under k7, every other element selected:
merging, or zeroing for a load into a vector register. A floating-point case's registers start at 1.0 of its element and its
constant sources are 1.0, or 0.0 for an addition, subtraction or fused
multiply-add, so that every chain keeps the value 1.0 and never reaches a
denormal, infinity or NaN; integers are 3. Divisions divide a 57-bit dividend
(`0x0123456789abcdef`) by 3 with RDX zero: their latency is that operand
range's, not a small quotient's.

**Data.** The targets' memory operands are consecutive slots of the operand's
size (at least 8 bytes) in the data's first 2 KiB, as a function's spills to
adjacent stack slots are; the blockers' are 2,176 bytes on modulo 4 KiB, so that
no access aliases another in the low 12 address bits, which the store queue
compares first (in a first trial, loads 16 KiB from read-modify-writes took 12
cycles a pair).

**Experiments** (`X86CostExperiment`), each where the case allows it:

- *independent*: twelve independent instances (fewer for a smaller pool), whose
  cycles, retired ops and dispatched ops per unit are the throughput and op
  counts; and *independent-six*, six, the mixes' reference;
- *chain-destination*: one register (or a read-modify-write's memory) the form
  reads and writes, through every instance;
- *chain-reg*, *chain-rm*, *chain-vvvv*: a ring of twelve registers, each
  instance reading in that role the previous instance's result; where the role's
  file isn't the result's, a bridge record moves it (`movq` between general and
  vector registers, `kmovq` between general registers and opmasks, both through a
  general register between opmasks and vector registers);
- *chain-index*: LEA's index (and a gather's), as *chain-rm* is its base;
- *chain-address*: a memory kind's result (through a bridge, or a `cmov` for a
  flags result) `and`ed with 0 and added to the base the next instance
  addresses, so that each address waits for the previous result;
- *chain-flags*: the flags a register source sets, read by a `cmov` (of a flag
  the form defines) that writes the next instance's source (and a bridge into a
  vector source);
- *mix-…*: the six instances interleaved one to one with a blocker of known
  pipes (`x86_zen5_blockers`): `mov r32, imm32` (the ALUs), `imul r64, r/m64,
  imm8` (the multipliers), `rorx` (the shifters), a load, a store, `vaddps`,
  `vmulps`, `vpaddd`, `vpshufb` and `vmovd r32, xmm` (the transfer from the
  vector unit), each write-only, so that six of them saturate their pipes. A
  form that reads the flags isn't mixed with `imul`, which writes them.

**Special families**, whose units the effects alone can't build:

- *implicit registers* (mul and imul of one operand, div, idiv, cqo, cdq, an
  accumulator's immediate, cmpxchg): the independent units first set RAX (a
  move of 3, or a load of the dividend), the chain runs through RAX, a divide's
  restoring the dividend's magnitude with an `or` (one ALU cycle, subtracted);
  RDX's zero idiom precedes each divide;
- *the stack*: 240 pushes and a LEA giving the stack back, or a LEA reserving it
  and 240 pops;
- *control*: a jmp to the next record (taken), a jcc to it with its condition
  false (the body starts with `cmp r14d, imm32` chosen per condition), a call of
  a stub whose ret returns, ret as that stub's, and an indirect jmp through a
  register a LEA sets;
- *the x87*: the stack holds five 1.0s and a constant; a form that pushes is
  paired with `fstp st(0)`, one that pops with `fld`, one on st(0) alone with an
  `fxch` that rotates the values;
- *the gather*: `kxnorw` sets its mask before each, the chain through its index;
- *traps* (`ud2`): never run, every column absent.

**Counters.** One pinned perf group: core cycles, retired instructions, retired
macro-ops (PMCx0C1), and macro-ops dispatched to the FP unit and to the integer
unit (PMCx0AB, umasks 4 and 8; the host's `perf list --details` names them
`ex_ret_ops` and `de_dis_ops_from_decoder.any_fp_dispatch` and
`.any_integer_dispatch`). The command refuses other processors, failed reads,
multiplexed groups and a retired-instruction count that isn't the loop's. Passes
interleave every experiment of a case in a rotating order, each pass starting
with the empty loop, whose counts (not cycles) every sample subtracts; each
value is the passes' median.

**References**, measured first in every process: the bridges' round trips
(`movq xmm, r64` then `movq r64, xmm`; `kmovq` both ways; `test` then `cmovne`)
and each blocker alone.

### Reduction (`x86_zen5_reduce`)

- The throughput, retired ops and FP ops are the independent experiment's,
  less its helpers' (a helper record is one macro-op: a move, a zero idiom, an
  x87 push or pop, a mask's `kxnorw`; derived where there are any).
- The latency is the longest register chain's cycles per unit, less its
  bridge's share (derived), or the address chain's where longer: its cycles less
  the `and` and the `add` (an ALU cycle each) and the result's bridge's share
  (derived).
- A bridge's share is half its measured round trip: **the two directions are
  assumed to cost the same**. A chain can't separate them: every round trip
  through a vector register or an opmask has both. The model's latencies across
  register files and its memory kinds' vector and opmask latencies rest on it.
- The load's extra latency is the address chain's latency over the register
  paths' (the flags' for a flags result).
- The flags' latency is the flags chain's cycles less half the `test`-`cmovne`
  round trip (the `cmov`'s share) and the source's bridge's.
- A pipe bit is set where the mix with a blocker took at least half the cycles
  that full sharing would add over the mix's floor, and full sharing would add
  at least a 20th of a cycle: the floor is the greatest of the target's six
  instances alone, the blocker's alone, and both's macro-ops over 8 (the
  dispatch width); full sharing is the sum of the first two. A bit says the case
  contends with that blocker's pipes at one to one, not which physical pipe it
  runs on; a mix can hide a resource limit behind a dependency limit.

**Absent by method**: the flags' latency of an accumulator's or implicit
register's form, of a read-modify-write and of the x87's; the latency of a load
or store of the x87 and of push and pop (the stack engine's); a divide's load's
extra latency; control transfers' latencies.

## What doesn't change a case

`measure-x86-zen5 --immediate N` and `--decorations N` measure a case with a
fixture's immediate or EVEX decorations in place of its own. In each of three
processes on the 9700X (the record below), every value gave the same row:

| Case | Immediates or decorations | Row |
| --- | --- | --- |
| `shl r64, imm8` | 0, 1, 3, 63, 127 | latency 1, throughput 0.328, 1 op |
| `imul r64, r/m64, imm8` | 1, 3, 127, −128 | latency 3, throughput 0.328, 1 op |
| `add r/m64, imm8` | 1, 3, 127, −128 | latency 1, throughput 0.188, 1 op |
| `add r/m32, imm32` | 0, 4660, 2147483647 | latency 1, throughput 0.188, 1 op |
| `vpaddd zmm` | none; zeroing under k4 | latency 2, throughput 0.25 |
| `vaddps zmm` | none; zeroing under k4 | latency 2, throughput 0.5 |

A shift by 0 doesn't write the flags, but costs a shift's cycles. Merging under
an opmask is different: in all three processes a merging `vpaddd zmm {k4}`
took 3 cycles from its sources where the unmasked one took 2, so a merging
opmask has its own kind (`MASKED`), which a form has rows of where an emitter
merges (Tier 0's masked `vpaddd`, Tier 1's `vpblendm*` selections and masked
compares). Where the semantics' effects say merging makes the destination a
source, the harness chains through it, which measures whether the processor
waits for it: a merging `vpaddd` does (2 cycles), `vpblendm`'s selection
doesn't (0.25 cycles, its throughput); a compare into an opmask has no vector
destination. An opmask on memory is different too: a zeroing
`vmovdqu8 zmm {k3}{z}, m512` took 6.5 cycles from its address (derived) where
the unmasked load took 4.5, at the same throughput, so any opmask on a memory
operand is the `MASKED_MEMORY` kind: Tier 0's masked stores (merging) and
masked loads (zeroing, as the harness measures a load's row; a load merging
into its destination would read it too, and has no row).

## Coverage

Every record Tier 0 and Tier 1 emit has a complete row, which the tests check
on every program they compile, as #221's checks do for the semantics' covered
forms:

- `test-tier0` (`tier0_test_covered`): every program of its checks (the effects',
  the deep values', the 128-bit integers', the masks', the vectors', the
  x87's, and `--cross-check`'s), and the programs `test-ir-lowering` and
  `test-interpreter` compile through it (`tier0_initialize` initializes the
  model, so that a test of Tier 0 alone reads it);
- `test-tier1-select`: every record of the selections' plans and expansions (not
  the test's wrappers);
- `test-tier1-emit` (and `test-x86-target-selection`, which runs it for each
  target), `test-tier1-masks` and `test-tier1-pipeline`: every frame and body
  record of the complete functions. `test-tier1-masks` exempts the forms Zen 5
  doesn't run (AVX512-FP16's, which its fp16 fixtures emit): the 9700X can't
  measure them, and code with them doesn't run on Zen 5.

`test-x86-zen5` checks that every row accounts for every column, so a row can't
be committed half measured. **Adding a case**: add its row with `U` values and
zero masks to `x86_zen5_rows.c`, build, and on the 9700X run `measure-x86-zen5
--missing --table`, which measures the rows without values and prints the whole
file. CI reproduces three eighths of the rows on the 9700X (`--check --sample 8`
with `--offset` 0, 1 and 2).

## The 9700X record

The record, with the scripts, every process's output, the receipt and the
sweep: [buster-performance's `x86-zen5-181-2026-11-07`](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/x86-zen5-181-2026-11-07).
The rows come from four processes of the harness (CPU 2 under the host's lock,
7 passes of 10,000 iterations, built `BUSTER_HOST_ARCH=znver5
BUSTER_DEBUG_INFO=0` by clang 23.1.1): the first 849 rows' `--table`, and the
masked rows', the families' and the masked load's `--missing --table`. On
`30de081f`, the commit that completed them, two `--check` processes reproduced
all 1,349 rows (49 and 58 differ in a pipe bit, which `--check` reports and
doesn't require), and a fresh `--table` printed 1,270 of them exactly: the
other 79 differ only in throughput, by the column's 1/64-cycle step (`ktest`'s
and `kortest`'s by their 0.5 or 1 cycle passes), or in pipe bits. Their 1,349
rows share 156 IDs.

[#1840](https://github.com/buster14a/buster/issues/1840)'s width changes added
six register rows, `vextracti32x4` of a ymm and a zmm register, `vextracti64x4`,
`vinserti32x4` into a ymm and a zmm register and `vinserti64x4`, which Tier 1
selects for the IR's `extract` and `insert`. They were measured by one `--missing
--table` process on the 9700X (CPU 2, under the lock, built `BUSTER_HOST_ARCH=znver5
BUSTER_DEBUG_INFO=0` at `cb608c1` with the change): the extracts have a latency
of 2 cycles and a throughput of 0.25, the inserts 2 and 0.5, each one macro-op. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-width-changes-1840-2026-11-09)
(buster-performance#354) has the process's output. Its permutes then added 19 more,
measured the same way: `vpermb` of xmm and ymm registers, `vpermw` and
`vpshufb` at every width, `vpermd` of ymm and zmm registers, `vpermq` of zmm
ones, `valignd` and `valignq` at every width, and `vshufi64x2` of ymm and zmm
registers. Its conversions then added 88: `vpmovzx` and `vpmovsx` of every pair of lanes at
every width, and `vpmov`, `vpmovs` and `vpmovus` of every pair at every width. Its shifts
by a scalar count then added 53: `vpsllw` to `vpsraq` by an xmm count (EVEX) and by an
immediate at every width, each 2 cycles' latency, one macro-op and 2 per cycle
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-shifts-1840-2026-11-11),
buster-performance#388). Its masks' conversions then added 12: `vpmovb2m` to `vpmovq2m` at every
width, 3, 4 and 6 cycles' latency at 16, 32 and 64 bytes. Its lane operations then added 72:
`vpminub` to `vpmaxsq` and `vpaddusb` to `vpsubsw` at every width, each 2 cycles'
latency, the minima and maxima of bytes to doublewords 4 per cycle and the others 2. Its masked stores then added 11
`MASKED_MEMORY` rows, `vmovdqu8` to `vmovdqu64` of a register to memory under an opmask at every
width that had none (`vmovdqu32` of a zmm register's was measured): the 16- and 32-byte ones 2 per
cycle, one macro-op, and the 64-byte ones 1 per cycle, two macro-ops, as the unmasked stores are
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-masked-memory-1840-2026-11-11)).
A masked load merging into its destination still has no row (above). Its one lane's moves then added 14: `vpextrb` to `vpextrq` (6.5 cycles' latency, 2 per
cycle), `vpinsrb` to `vpinsrq` (4.5, 1 per cycle), and `vpmovmskb`, `vmovmskps` and `vmovmskpd` of
an xmm and a ymm register (4.5 and 6.5, 2 per cycle)
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-lane-moves-1840-2026-11-11)).
Its masked operations (Tier 1's `vselect` of an operation, item 5) then added 164: 155 `MASKED` rows of
every form `tier1_select_masked_binary` can make (the integer sums, differences, products and variable
shifts, the floating arithmetic and the lane operations, under a merging opmask, at every width that had
none), and the `REGISTER` rows of the EVEX variable shifts' doubleword forms, which their zeroing takes.
A merging opmask adds a cycle of latency to the integer sums (3 cycles, 4 per cycle, as `vpaddd`'s
64-byte row had it) and leaves the products', the floating operations' and the divisions' at their unmasked
values ([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-masked-operations-1840-2026-11-12)).
Its gathers, compressing stores and truncating stores then added 45: 9 `MEMORY_INDEX` rows of `vpgatherdd` to `vpgatherqq`
(16 to 25 cycles' latency by width, one every 3 to 7 cycles, 13 to 40 macro-ops; the harness's gather now takes quadword
indices too, the element's bytes its scale), 12 `MASKED_MEMORY` rows of `vpcompressb` to `vpcompressq` to memory (one every
3 cycles, 7 or 8 macro-ops, where the register form is one a cycle and 2) and 24 of `vpmov`, `vpmovs` and `vpmovus` to memory
(2 per cycle, 2 macro-ops) ([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-gathers-1840-2026-11-14)).

What the rows say, a few families (cycles; derived values in italics):

| Family | Latency | Throughput | Macro-ops |
| --- | ---: | ---: | ---: |
| ALU on registers (`add`, `and`, `neg`, `inc`, `cmp`'s flags …) | 1 | 0.19 | 1 |
| zero idioms (`xor r32, r32`; `pxor`, `vpxor` of a register with itself) | — | 0.14 (vector 0.17) | 1 |
| `mov r32/r64, r` (eliminated: a chain runs at rename's rate) | 0.125 | 0.14 | 1 |
| `mov r32, imm32`; `mov r64, imm64` | — | 0.17; 0.33 | 1 |
| load `mov r, [m]`; with an index | 4; 5 | 0.25 | 1 |
| `movsx` load; with an index | 5; 6 | 0.25 | 1 |
| store (consecutive slots) | — | 0.5 | 1 |
| read-modify-write `add [m], r` (through the memory) | 1 | 0.53 | 1 |
| `lea` of one or two components, or scaled | 1 | 0.17 | 1 |
| `lea [base + index * s + disp]` | 2 | 0.48 | 2 |
| `imul r, r/m(, imm)`; `mul r64` | 3; 3 | 0.33; 0.67 | 1; 2 |
| `div r64`; `div r32`, `idiv r32` (a 57-bit dividend by 3) | *16*; *13* | 9.3; 6 | 2 |
| shifts (by 1, CL, imm8), `bsf`, `tzcnt`, `bextr`, `bzhi` | 1 | 0.33 | 1 |
| `shld`, `shrd` | 2 | 1.0 | 4 to 5 |
| `cmovcc` and `setcc` but `a`, `be` (`cmova`, `cmovbe`: 2 ops) | 1 | 0.19 | 1 |
| `jcc` not taken; `jmp` to the next instruction | — | 0.34; 0.55 to 1.6 | 1 |
| `call` and its `ret` | — | 4 a pair | 3 a pair |
| `lock xadd`, `lock cmpxchg`, `xchg` with memory | 20 | 20 | 2 to 6 |
| `mfence` | — | 82 | 6 |
| SSE/AVX floating add, sub, compare; multiply | 2; 3 | 0.5 | 1 |
| `divsd`; `divpd` | 13 | 4 | 1 |
| vector integer `vpaddd` and logic; `vpmulld`; `vpermb` | 2; 3; 5 | 0.25; 0.5; 0.5 | 1 |
| vector load (any width); `zmm` store | *4.5*; — | 0.5; 1 | 1; 2 |
| `movq xmm, r64`; `movq r64, xmm` (half the 9-cycle round trip each) | *4.5* | 1; 0.5 | 2; 1 |
| opmask logic (`kandw`, `kxorq` …) | 1 | 0.5 | 1 |
| `kortest`, `ktest` (the passes are 0.5 or 1, either in one process) | — | 0.5 to 1 | 2 |
| `vpgatherdd zmm` | 30 | 12.6 | 64 |
| x87 `faddp`, `fmulp`; `fdivp`; `fld m80` | 6; 15; — | 1.2; 5; 4 | 1; 1; 10 |

The vector loads' 4.5 cycles and every latency between register files rest on
the even split of a round trip (above): a vector load's address chain is 11
cycles with the `and`, the `add` and a `movq r64, xmm` in it, and an integer
load's is 4 (measured, no bridge). Generated code doesn't change with these
rows: the objects of `sqlite3.c`, speedtest1 and Lua's 34 files at Tier 0 and
`--tier1`, at the default target and `-march=znver5`, are the same bytes from
`main` and this change (Tier 0's `inc` and `dec` still cost no more than `add`
and `sub`, and every rematerialized copy still costs no more than its reload).

## Consumers

- **Tier 0** asks the form choice for `inc` and `dec` where they cost no more
  than `add` and `sub` with an imm8 (`tier0_inc_dec`): latency, throughput and
  retired macro-ops, each known (below).
- **Tier 1's selection** carries each record's ID (`Tier1SelectCode.cost_ids`)
  and counts unknown cases.
- **The postallocation scheduler** (#172, `tier1_schedule.c`) weighs each record
  by its case's latency (docs/tier1-scheduling.md).
- **Rematerialization** (#178, `tier1_rematerialization_model_costs`) compares a
  copy's latency with an L1 load's (docs/tier1-allocation.md).

## Provenance

The implementation, fixtures and measurements are original project work. No
uops.info data, XED data, LLVM scheduling table, nanoBench implementation or
other third-party timing dataset is incorporated; this follows #181's option to
measure every entry with the harness, so that no third-party data enters the
repository. The hardware counters' identifiers are architectural interfaces;
Linux perf and Clang are external tools.
