# Tier 1: inlining the unit's small functions, whatever their control flow (#2541): design

Status: step 0 (the splice, offered to `always_inline` callees) is
implemented: [the inlining guide](ir-inlining.md)'s "Callees with control
flow", measured [below](#step-0-measured). Step 1 (the unit stage, summaries
and the small class) is implemented: [the Tier 1 guide](tier1.md)'s
"Inlining", measured [below](#step-1-measured), three of its five gates not
met. Steps 2 and 3 are design only. The
measurements other than steps 0's and 1's are main's at `c5f0efb2`, and an experiment
patch kept in the record (not for merging) prototypes the central
transformation and was run on the three programs
([below](#what-the-prototype-measured)). It is
[#138](https://github.com/buster14a/buster/issues/138)'s code-quality design:
the change that the measurements below rank first among those nothing owns.
The issue is [#2541](https://github.com/buster14a/buster/issues/2541).
The names of existing code are main's at `c5f0efb2`, and every figure not
attributed otherwise is that revision's on the 9700X (CPU 2, under the lock,
clang 23.1.1), from
[the measurement record](https://github.com/buster14a/buster-performance/tree/2ceb92ffb3386b67da48bb6555e5ac9107bb8aed/docs/measurements/tier1-quality-138-2026-11-12).

**In one paragraph.** `--tier1` takes 1.42 times clang -O2's time on
speedtest1 and 1.39 on Lua's five scripts (1.60 and 1.56 at #1189), and 2.8
times on a third program measured here, the compiler itself compiling
`sqlite3.c`. With LLVM's own pieces taken one at a time, the largest single
piece of what is left is inlining: at Tier 1's level of IR on LLVM's back end,
LLVM's inliner alone closes 35% of the gap on both programs and 15 to 19% on
the compiler, and on LLVM's weakened back ends (no coalescing or machine
passes; a basic or a local allocator) it still saves 7.5 to 12% of the cycles. `--tier1` makes
68.9 M calls into speedtest1's own code where clang -O2 makes 25.6 M, and
91.8 M against 54.2 M on Lua; its inliner takes the always_inline wrappers that
are one straight line. No issue owns the rest: #139 names the pass and #1282
delivered the wrappers. The design expands the unit's small functions, whatever
their control flow and their own calls, bottom-up from summaries built once
between the emission's two halves, by rebased copies of the callee's columns,
and drops the functions left without a caller. A prototype of the splice
compiles speedtest1, Lua and the whole compiler correctly; on the 9700X it
measures 12.0% fewer cycles on speedtest1, faster at every one of eight
placements of its code, and 7.6% fewer on Lua.

## Where `--tier1` stands

### The two programs

`measure-tier1-programs` ([the command guide](commands/tier1.md)), 11 rounds,
medians, Lua's being the sum of its five scripts'; all 330 runs succeeded and
every output was checked:

| Program | Tier 0 | `--tier1` | clang -O0 | clang -O2 | tcc | `--tier1` / -O2 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| speedtest1, ms | 255.0 | 186.2 | 294.4 | 131.2 | 271.6 | 1.42 |
| speedtest1, user cycles, M | 1,313.9 | 931.0 | 1,510.0 | 630.8 | 1,394.8 | 1.48 |
| speedtest1, instructions, M | 5,673.6 | 3,208.4 | 3,921.9 | 2,010.0 | 4,832.3 | 1.60 |
| Lua's scripts, ms | 337.2 | 232.2 | 375.2 | 166.6 | 395.6 | 1.39 |
| Lua's scripts, user cycles, M | 1,836.4 | 1,257.8 | 2,031.9 | 897.7 | 2,148.9 | 1.40 |
| Lua's scripts, instructions, M | 10,770.4 | 6,114.0 | 8,332.0 | 4,283.3 | 9,392.2 | 1.43 |

Tier 1 compiled all 2,709 of speedtest1's functions and 1,087 of Lua's 1,088
(`luaD_rawrunprotected` calls `setjmp` and takes Tier 0). Since #1189's
measurement at `eeeafb6d`, `--tier1` went from 207.0 to 186.2 ms and from 260.8
to 232.2 ms; clang -O2 is where it was. speedtest1's `.text` is 1,072,656 bytes
by `--tier1`, 1,316,224 by Tier 0 and 1,072,139 by clang -O2; Lua's 216,336 and
199,472.

### A third program: the compiler itself

Both programs above turn on an interpreter's loop: `sqlite3VdbeExec` and
`luaV_execute` hold 13.5% and 48.7% of their gaps. The work asked for wider
evidence from what the frozen corpora and the
repository already hold, without fetching or building anything new on the
9700X. What they hold besides: musl is a library, not a program, and SQLite's
shell and testfixture are correctness suites of the code speedtest1 already
times; the c-testsuite's programs run for microseconds. Those serve the
refusal inventory [below](#what-stands-between-tier-1-and--o1-and-above). The
one large program left is the repository's own: **the compiler**, 43 units and
24,872 functions, which the self-host qualification (`tools/ci/selfhost.c`)
already builds by itself. It is unlike an interpreter: scanners, table lookups
and bit manipulation in many small functions, with no dispatch loop. Its work
is real, compiling the frozen `sqlite3.c`, and its output is checked to the
byte: every build below writes the same object.

Each unit was compiled at `BUSTER_HOST_ARCH=x86-64`, the scalar host, so that
the comparison is of scalar code (#1344 has the vector code), by Tier 0, by
`--tier1`, and by clang with the build's own flags at `-O0` and `-O2`, and
linked by clang. Two workloads, one lane, three rounds, medians:

| The compiler built by | `-c sqlite3.c`, ms | user cycles, M | `--tier1 -c sqlite3.c`, ms | user cycles, M |
| --- | ---: | ---: | ---: | ---: |
| Tier 0 | 1,989.0 | 10,904 | 3,062.5 | 16,819 |
| `--tier1` | 1,610.0 | 8,795 | 2,388.4 | 13,070 |
| clang -O0 | 1,799.7 | 9,767 | 2,790.8 | 15,193 |
| clang -O2 | 570.0 | 3,054 | 954.2 | 5,161 |
| `--tier1` / -O2, cycles | | 2.88 | | 2.53 |

Tier 1 compiled 24,855 of the 24,872 functions; the 17 it left are listed
[below](#refusals-and-fallbacks). So the gap on this program is twice the
interpreters'. Most of the difference is in two places, each measured
[below](#the-compiler-by-function): the scalar host's 128-bit values, which
Tier 1 keeps in memory (#1344), and small constant `memcpy` calls (#1373).
Its `always_inline` helpers with loops stay calls besides: the Tier 1 build
has 25,323 functions in its executable and clang -O2's 10,045.

## The gap by cause

### Instructions, by cause and by Tier 1's origin

#1189's method again, with its record's tool (`gap.c`) and its tags re-applied
to current main (`kinds-instrumentation.patch`; the tagged compiler's
executables are the timed ones byte for byte): callgrind's exact count of every
instruction (`--dump-instr=yes`), the C library's left out, each of Tier 1's
tagged by the record that made it. The 9700X has no callgrind, so the counts
are of the 9700X's executables run in the session's container through the
9700X's own C library; whole-process totals agree with the 9700X's counters
within 0.4% on Lua and 2.5% on speedtest1, whose difference is the C library's
`rep` loops.

The program's own code runs 3,078.7 M instructions by `--tier1` against
1,918.4 M by clang -O2 on speedtest1, a gap of **1,160.3 M** (1,856 M at
#1189), and 5,577.0 M against 3,797.3 M on Lua, **1,779.8 M** (3,460 M).

| Cause | speedtest1, M | share | Lua, M | share |
| --- | ---: | ---: | ---: | ---: |
| stack loads and stores | 273.4 | 23.6% | 300.7 | 16.9% |
| register moves | 215.8 | 18.6% | 478.6 | 26.9% |
| compares and branches | 152.4 | 13.1% | 290.2 | 16.3% |
| address arithmetic | 146.7 | 12.6% | 271.3 | 15.2% |
| calls, returns and frames | 131.8 | 11.4% | 60.8 | 3.4% |
| other loads and stores | 101.8 | 8.8% | 148.0 | 8.3% |
| constants | 101.7 | 8.8% | 38.3 | 2.2% |
| zero and sign extensions | 30.9 | 2.7% | 7.8 | 0.4% |
| arithmetic and the rest | 5.8 | 0.5% | 184.1 | 10.3% |

Tier 1's instructions by the record that made them, as shares of all of them:

| Origin | speedtest1 | Lua |
| --- | ---: | ---: |
| a node's own code | 56.7% | 58.7% |
| a block's branch | 12.3% | 12.3% |
| call sequences (argument and result moves, the call) | 8.0% | 5.1% |
| frames (prologue, epilogue, return) | 6.7% | 6.0% |
| saves and restores around calls | 6.2% | 5.1% |
| moves on edges | 2.9% | 8.4% |
| spills and reloads on edges | 4.5% | 1.5% |
| recomputed values | 1.6% | 1.75% |
| spills and reloads | 0.9% | 0.7% |

A call costs more than the row that bears its name. The call sequences, the
frames and the saves and restores around calls are 20.9% of speedtest1's
instructions and 16.2% of Lua's, and `--tier1` makes 68.92 M calls into
speedtest1's own code against clang -O2's 25.62 M, and 91.79 M against 54.20 M
on Lua (39% of Lua's calls go to another unit, which no compiler inlines
without link-time work).

Patterns counted in Tier 1's executed instructions (the record's
`tools/patterns.py`), which the next levers [below](#the-other-levers-without-an-owner)
come from:

| Pattern | speedtest1, M | Lua, M |
| --- | ---: | ---: |
| two registers swapped on an edge through r11 (3 moves) | 24.9 | 247.1 |
| a constant made, tested and branched on (a truth value across blocks) | 51.5 | 69.6 |
| `setcc` in one block, `test` in the next | 22.5 | 65.7 |
| a constant through a register for the next instruction's store | 31.4 | 37.2 |
| a shift by zero | 0.2 | 56.4 |
| a jump to a jump | 0.5 | 55.1 |

### Per hot function

The interpreters' loops still lead, with smaller shares than at #1189:
`sqlite3VdbeExec` is 13.5% of speedtest1's gap (21% then) and `luaV_execute`
48.7% of Lua's (64%).

| Function | `--tier1`, M | clang -O2, M | Gap, M | What it is |
| --- | ---: | ---: | ---: | --- |
| `sqlite3VdbeExec` | 555.6 | 398.6 | 157.1 | stack 55.6, address 46.5, moves 39.5; 98.0 M of spills and reloads |
| `sqlite3Parser` | 78.7 | 0 | 78.7 | clang inlines it into its one caller |
| `substrFunc` | 97.5 | 48.4 | 49.1 | compares and branches 19.3, moves 10.1 |
| `yy_reduce` | 80.5 | 60.4 | 20.2 | |
| `sqlite3BtreeIndexMoveto` | 60.3 | 44.2 | 16.1 | call saves 9.1 |
| `luaV_execute` | 2,961.3 | 2,094.9 | 866.4 | moves 340.1 (424.4 M of edge moves), address 219.0 |
| `luaV_mod` | 106.6 | 0 | 106.6 | clang inlines it (4.01 M calls) |
| `index2value` | 104.5 | 0 | 104.5 | clang inlines it (9.30 M calls) |
| `luaV_idiv` | 83.7 | 0 | 83.7 | clang inlines it (3.49 M calls) |
| `prepCallInfo` | 74.5 | 0 | 74.5 | clang inlines it (4.38 M calls) |
| `luaD_precall` | 241.4 | 192.1 | 49.3 | |
| `luaH_getshortstr` | 82.3 | 47.5 | 34.8 | |

The record's tables have every function with a gap, and each one's split.

### Time: LLVM's pieces, one at a time

Instructions are not time: the gap is 1.60 and 1.47 in the programs' own
instructions and 1.48
and 1.40 in cycles. To rank by time, the two programs were built by clang and
LLVM's tools in 58 configurations, each timed on the 9700X (5 rounds, every
build's outputs checked first; a cell's run-to-run spread has a median of 1.2%
and a 90th percentile of 3.6%, and each build is one code layout, which moves
a Lua script by 5 to 10%).

**From the top**, clang -O2 with one thing taken away, as user cycles over
-O2's:

| clang -O2 without | speedtest1 | Lua |
| --- | ---: | ---: |
| inlining (`-fno-inline`) | 1.122 | 1.102 |
| CodeGenPrepare (address sinking) | 1.056 | 1.056 |
| the greedy allocator (basic) | 1.029 | 1.082 |
| register coalescing | 1.036 | 1.025 |
| jump threading and correlated propagation | 1.021 | 1.010 |
| block placement | 1.019 | 1.038 |
| GVN | 0.994 | 1.018 |
| loop rotation | 1.006 | 1.013 |
| SROA | 1.005 | 1.012 |
| LICM; InstCombine; early CSE; DSE; LSR; unrolling; vectorizers; tail duplication; machine LICM, CSE and sinking; strict aliasing; the stack protector | 0.986 to 1.002 | 0.994 to 1.008 |
| everything (`-O0`) | 2.419 | 2.288 |

clang -O1 is at 1.016 and 1.028, `-O3` at 0.992 and 1.008, gcc -O2 at 0.971
and 1.010.

**From the bottom**, `B` is Tier 1's level of IR on LLVM's back end: clang's
front end, then `mem2reg`, SCCP, dead code and CFG cleanup, which are
`--tier1`'s passes, then `llc -O2`. It has every call `--tier1` has (68.92 M
and 91.79 M). User cycles, M, and the share of the gap from `--tier1` to
clang -O2 that each closes:

| Build | speedtest1 | closes | Lua | closes |
| --- | ---: | ---: | ---: | ---: |
| `--tier1` | 933.0 | 0 | 1,275.3 | 0 |
| `B` with a weak back end (basic allocator; no coalescing, machine passes, tail duplication, CodeGenPrepare or LSR) | 837.2 | 0.32 | 1,179.7 | 0.26 |
| `B` | 786.4 | 0.49 | 1,107.3 | 0.45 |
| `B` + LLVM's inliner | 680.5 | 0.84 | 978.1 | 0.80 |
| clang -O2 | 632.9 | 1 | 902.7 | 1 |

`B` plus one other pass moves it by 2% or less on speedtest1 and 4% or less on
Lua (GVN 0.6% and 3.7%, early CSE 1.0% and 1.5%, jump threading 2.0% and
-2.0%, SROA, InstCombine, LICM and loop rotation within the spread), and `B`
with SROA, early CSE and the inliner together reaches 654.0 M and 987.6 M.
So the gap splits, in cycles:

| Segment | speedtest1 | Lua | The compiler |
| --- | ---: | ---: | ---: |
| `--tier1` to a weak LLVM back end, the same IR and calls | 32% | 26% | 64% together |
| a weak back end to LLVM's -O2 back end | 17% | 19% | |
| **LLVM's inliner** | **35%** | **35%** | **15 to 19%** |
| every other IR pass of -O2 | 16% | 20% | 17 to 20% |

The compiler's column is from the same builds of its units (`B` 5,101 M and
7,974 M cycles on its two workloads, `B` with the inliner 4,045 M and 6,762 M).

Inlining is the largest single piece from either end on the two programs:
taking it from -O2 costs 12.2% and 10.2%, and adding it to `B` gains 13.5% and
11.7%. A removed call is worth 2.5 cycles on speedtest1 and 3.5 on Lua, at 5.2
and 8.1 instructions: an instruction that inlining removes costs two to four
times what an instruction of the back end's gap costs, since calls and returns
are taken branches, and saves and restores around them are loads from just
written memory.

### Tier 1 against LLVM's back end on the same IR

The first two segments are the back end's alone: `B` has the same calls and
the same passes. Their instructions, `--tier1`'s less `B`'s, are 810.3 M on
speedtest1 and 1,428.1 M on Lua:

| Cause | speedtest1 | Lua | Owner |
| --- | ---: | ---: | --- |
| stack loads and stores | 32.7% | 20.3% | #1116 (call saves and restores are 43% and 68% of Tier 1's stack traffic); edge spills and reloads: none since #1216 closed |
| register moves | 22.7% | 32.9% | edge moves: none; call sequences' moves: #1116 |
| address arithmetic | 19.0% | 21.6% | #1217 |
| compares and branches | 12.7% | 21.1% | none (#1218 is closed); sparse switches are #1371's |
| constants | 9.5% | 6.8% | none |
| other loads and stores | 5.4% | 2.4% | #139's value numbering of memory |
| zero and sign extensions | 2.5% | 0.3% | none since #1219 closed |
| calls, returns and frames | -5.3% | -12.1% | (LLVM's stack protector and its pushes) |
| arithmetic and the rest | 0.7% | 6.6% | |

### The compiler, by function

callgrind cannot run the compiler (its arenas reserve more address space than
valgrind maps), so the compiler's own split is by sampled cycles per function,
`--tier1`'s build against `B`'s (the same calls), on `-c sqlite3.c`. The gap
between them is 3,673 M cycles, 64% of the gap to clang -O2:

| Function | `--tier1`, M | `B`, M | What it is |
| --- | ---: | ---: | --- |
| `intern_hash_bytes_scalar`, `intern_lane_rounds` | 1,722 | 94 | **#1344, #1375:** `__m128i` values passed and returned by value live in frame slots, written by two 8-byte stores and read back by one 16-byte load, which cannot forward (a dozen such pairs in `intern_lane_rounds` alone) |
| the C library (`memcpy`, `memset`) | 404 | 31 | **#1373:** `memcpy` of 8 and 16 constant bytes is a call |
| `pp_count_lines` | 618 | 484 | |
| `pp_named_ucn_candidates` | 305 | 199 | |
| `host_aesenc` | 1,361 | 1,369 | the same in both; `always_inline` with loops, so a call in both |
| every other function | 4,364 | 2,924 | |

The first two rows are 55% of this gap and belong to open issues. What the
inliner adds on top (`B` to `B` with the inliner, 1,056 M cycles) is 18.5% of
the compiler's gap to clang -O2; taking inlining from clang -O2 costs it 19.2%.

By cause, the compiler's split is of sampled operations, not instructions (AMD's
IBS op sampling on the 9700X, joined with the same tags; on speedtest1 its
shares agree with callgrind's within a point, except where an instruction is
not one operation: fused compares and branches count less, calls and returns
more). `--tier1`'s build runs 3.18 times clang -O2's operations, and the
difference is: compares and branches 21.7%, register moves 19.0%, stack loads
and stores 15.2%, calls, returns and frames 10.6%, constants 10.6%, other
loads and stores 10.3%, address arithmetic 7.4%, arithmetic 3.5%, extensions
1.7%. Three quarters of `--tier1`'s operations here are its nodes' own code
(74.2%, against 57 to 59% of the two programs' instructions): this program's
gap is less the allocator's and more the selection's and the missing passes'
than the interpreters' is.

## The ranking

By share of the gap to clang -O2 now, in cycles where the ladder gives them
(the first figure of a pair is speedtest1's, the second Lua's) and otherwise
as the cause's share of the back end's instructions times the back end's share
of the gap:

| # | Cause | Share of the gap | Owner |
| --- | --- | --- | --- |
| 1 | calls that clang inlines | 35% and 35% (26% and 29% from the top); 15 to 19% on the compiler | **none.** #139 names "inlining of small static functions"; #1282 (closed) delivered always_inline straight-line bodies, #1398 is Tier 0's |
| 2 | values in memory around calls and across blocks (stack loads and stores) | about 16% and 9% | #1116 for call saves; **none** for edge spills and reloads |
| 3 | register moves on edges and in call sequences | about 11% and 15% | **none** for edge moves; #1116 for call sequences |
| 4 | address arithmetic | about 9% and 10% | #1217 |
| 5 | LLVM's other IR passes together, each 2 to 4% at most alone | 16% and 20% | #139 (value numbering with memory, loop motion); #1382 (selects) |
| 6 | compares and branches | about 6% and 9% | **none** (#1218 is closed); sparse switches are #1371's |
| 7 | constants | about 5% and 3% | **none** |
| - | on the compiler: 128-bit values through memory | 28% of its gap | #1344 (and #1375's pattern) |
| - | on the compiler: small constant `memcpy` and `memset` as calls | 6.5% of its gap | #1373 |

Rows 2 to 4, 6 and 7 are the back end's 49% and 45% split by the instruction
table above, which assumes each of its instructions costs the same; the
ladder's own bounds for the group are firmer: the whole distance from LLVM's
fast allocator to its greedy one is 26% of `B`'s cycles on both programs, and
`--tier1` sits 18.6% and 15.2% above `B`.

**The first-ranked cause without an owner is inlining**, on both programs from
both ends of the ladder, and it is one mechanism, where rows 2 and 3 are
several with different owners. Its ownership, exactly: #139's body lists the
pass among seven and holds a claim from 2026-10-23 on "the SSA optimization
passes", whose delivery (PR #337) is the bounded inliner that #1282 then
widened; nothing since designs or claims the wider scope, and #1189's and
#139's later comments record only that the scope is too narrow to pay. The
next causes without an owner are measured, not designed,
[below](#the-other-levers-without-an-owner).

## The lever

### What today's inliner reaches, and what the calls are

`ir_optimize_inline.c` ([the inlining guide](ir-inlining.md)) expands a callee
whose optimized body is one straight line of blocks without calls of its own:
a leaf without control flow. `--tier1` runs it on each body's `always_inline`
callees of internal linkage alone (`c_check_tier1_inline.c`, #1282), the x86
intrinsics' wrappers above all. speedtest1 and Lua have none, so `--tier1`
inlines nothing in them (`inlined=0`).

An experiment switch that offers the existing inliner every internal function
without an inline attribute, at its cost limit, calibrates what a removed call
is worth in Tier 1's own code. On speedtest1, eight placements of its code by
five rounds each, as 100 ln(variant / base) of user cycles with its 95%
interval over the placements:

| Cost limit | sites | calls removed | cycles | instructions | faster at |
| ---: | ---: | ---: | ---: | ---: | --- |
| 32 | 626 | 7.43 M (10.8%) | -2.62 (-3.25 to -1.99) | -1.44 | 8 of 8 placements |
| 64 | 884 | 8.69 M (12.6%) | -3.31 (-3.88 to -2.74) | -1.88 | 8 of 8 |
| 128 | 911 | | -3.21 (-3.63 to -2.79) | -1.94 | 8 of 8 |

That is 3.3 to 3.5 cycles and 6.2 to 6.9 instructions for each removed call:
Tier 1's code gains from a removed call what LLVM's gains (2.5 and 3.5 cycles).
The scope is the limit, as #1189 found: straight-line leaves receive an eighth
of speedtest1's calls and 2% of Lua's. By shape of callee, with sizes in
Tier 1's machine instructions and callees in a cycle of the call graph left
out (16% of speedtest1's calls go to those):

| Callees | speedtest1's calls | Lua's calls |
| --- | ---: | ---: |
| straight-line leaves of at most 32 instructions (today's scope) | 14.8% | 1.7% |
| and leaves with control flow, at most 64 | 27.6% | 13.5% |
| and callees with calls of their own, at most 64 | 62.3% | 37.9% |
| what clang -O2 removes | 62.8% | 41.0% |

So reaching clang's count needs two things the inliner refuses: control flow
in the callee, and callees that call. A quarter of speedtest1's internal
functions (630 of 2,415: 24% of its instructions, 17.7% of its calls) have one
call site and no address taken: expanding those removes their calls and adds
no code, the function itself going away (`sqlite3Parser`, `yy_shift`,
`btreeParseCell`).

### What the design does

1. **Calls.** After the lowering half of the emission, every body's direct
   calls are a column already: `Tier0Function.call_targets`, one symbol a
   call. Through the unit's symbol-to-body table they are the unit's call
   graph (2,709 functions and 9,025 distinct direct edges in speedtest1's
   unit).
2. **Candidates and levels.** A candidate is a function a call could be
   replaced by: defined in the unit, by the policy's flags and a bound on its
   lowered size. The candidates' subgraph's strongly connected components
   (the inliner's Tarjan) give each candidate outside a cycle a level, one
   above its highest candidate callee's. speedtest1's candidates of at most
   32 instructions have 9 levels and those of at most 64 have 17 (Lua's 6
   and 19).
3. **Summaries, bottom-up.** Level by level, each candidate's summary is
   built once for the unit: its lowered IR, with its calls of lower levels'
   candidates expanded from their summaries, promoted to SSA and optimized
   (SCCP, dead code, CFG cleanup). A level's functions are independent, so
   the lanes take them in parallel and meet between levels. A summary is
   immutable once its level is done, and is both what callers copy and what
   the candidate's own code is selected from: the candidate's pipeline is
   moved, not repeated.
4. **Decisions.** Each direct call of a candidate is expanded or kept by the
   policy: a comparison of the callee's summary's cost with a limit, and the
   caller's and the unit's growth budgets in call order.
5. **The splice.** In the code half, a body that is not a candidate is
   lowered as now, and each of its expanded calls replaced by the callee's
   summary: the callee's blocks placed where the call was, its columns copied
   with their indices rebased, its parameters bound to the call's arguments,
   its returns joined in a phi at the continuation. Then SROA with #1282's
   options, promotion and the rest of the pipeline, unchanged.
6. **Dead functions.** An internal candidate with no kept call and no address
   taken gets no code.

The transformation is the textbook one. What is designed here is where it
sits, so that it costs a compile what it adds to the code and no more, and how
its parts are data-parallel.

### Where it sits

```
lowering half      every part's typed stream, on the lanes       (exists: CheckEmitRun.tier1_inline)
  unit stage       calls -> graph -> levels -> summaries -> decisions -> dead set   (new)
code half          every part but the candidates that may end without a caller, on the lanes:
                   lowered IR -> splice -> SROA -> promotion -> wide split -> truth values
                   -> if-conversion -> SCCP, dead code, CFG cleanup, layout -> selection -> emission
  last pass        those candidates that some caller still calls: selection -> emission
```

The emission already runs in two halves when the unit has an `always_inline`
callee: it lowers every part before any part's code, "so that a body's lane
reads any callee's typed stream" ([the Tier 1 guide](tier1.md)'s "Inlining").
The unit stage is what happens between them. Four things change from #1282's
arrangement:

- **One preparation per callee, not one per lane.** Today each lane prepares
  each callee it meets in a cache of its own, so a callee is lowered, promoted
  and optimized up to once a lane. The summaries are the unit's.
- **One rewrite per function, not one per reach.** Today each caller builds a
  unit of itself and up to 64 callees it reaches, and `ir_optimize_inline`
  rewrites all of them bottom-up for that caller alone. With summaries a
  callee's own expansions are done once, and a caller's splice copies finished
  bodies.
- **Before promotion, as now.** The caller is in the lowering's memory form
  when it is spliced, so that SROA's `flagged` option sees the by-value
  aggregates' and the address-taken locals' slots of caller and callee
  together and promotion makes values of what no longer escapes: a local
  whose address was passed to an expanded callee is the commonest case.

- **A last pass for the candidates that may die.** Whether an internal
  candidate keeps a caller is known only when every caller is done: a body
  that Tier 1 refuses is compiled by Tier 0 with its calls, and a splice
  that fails keeps them. So the candidates whose every call the decisions
  expand are held back, the lanes count the calls that were kept after all,
  and a short last pass emits the ones still called, from their summaries.
  The others get no code. The object's order of functions does not change:
  parts are published in their own order whenever they are emitted.

What the per-caller arrangement costs is measured, by the calibration
experiment above: `--tier1 -c sqlite3.c` takes 427 ms without it, 602 ms at a
cost limit of 32 and 680 ms at 64 (+41% and +59%), for 3% of run time and no
growth of the code. That is the cost the design is built to avoid.

### Summaries

A summary is an `IrFunction` in optimized SSA form, in an arena of the unit,
with three columns beside it, by body: its state (ready, or refused with the
reason), its cost, and its level.

- **Two forms of a candidate.** Bottom-up expansion can grow a wrapper past
  the limit: `sqlite3_value_int64` is 4 instructions that call
  `sqlite3VdbeIntValue`, 33; once the callee is expanded into it, the
  prototype no longer inlines the wrapper below a limit of 256. LLVM's
  bottom-up inliner has the same effect and a deferral heuristic against it.
  Summaries allow the direct answer: a candidate keeps the cost of its body
  *before* its own expansions too, and where the expanded form is past a
  caller's limit and the plain form is within it, the caller copies the plain
  form, whose calls of the deeper callee stay. The plain form is the
  candidate's lowered IR promoted and optimized without the splice, built
  only when the expanded form's cost is past the largest limit.
- **Cycles.** A function in a cycle of the candidates' graph is no candidate
  (LLVM expands such a function into callers outside the cycle; the measured
  16% of speedtest1's calls that go to cyclic callees say what that scope
  would add, and it is left for a later step). It still receives expansions
  of acyclic candidates, as now.
- **Refusals** are the inliner's, by reason, and each leaves the call a call:
  a variadic definition, a label whose address is taken, a dynamic stack
  object, a call of a function that returns twice, `noinline`, an asm body, a
  callee whose target has a feature its caller's lacks, a call whose ABI
  records are not the callee's own.
- **Determinism.** A summary is a function of its body and of lower levels'
  summaries, and the decisions are functions of summaries and call order, so
  the output does not depend on the lane count (#153), as now.
- **Memory.** Summaries exist for candidates alone, the small functions: on
  speedtest1's unit the functions of at most 64 instructions are 16.6% of the
  code (41.4% of Lua's). Non-candidates' IR is made and freed on its lane as
  now.

### The splice

`ir_optimize_inline.c`'s rewrite computes every destination before it writes
anything, in two passes: sizes, then fills. The general expansion keeps that
shape. For a caller with K expanded sites:

- **Sizes.** Per site, from the callee's summary: instructions (its nodes
  less its parameters and returns, plus one branch per return and one at the
  call, plus the result's phi), blocks (its blocks plus one, the
  continuation), `extra` words (its phis', calls' and switches' slices, plus
  two a return for the result's phi), and rows of each side table (constants,
  addresses, slots, vector constants, machine rows, call descriptors).
  Prefix sums over the sites give each site's base in every column and the
  caller's own nodes' and blocks' new indices.
- **Blocks.** The call's block splits at the call: its head ends in a branch
  to the callee's entry, the callee's blocks follow in their own order, and
  the continuation takes the rest of the block, starting with the result:
  a phi of the returned values where the callee returns in two places or
  more, a copy for one, an undefined value for none (a callee that never
  returns). Each `ret` becomes a branch to the continuation. The caller's
  later blocks shift by the callee's blocks plus one. Branch targets into the
  split block go to its head; the phis of its successors name the
  continuation as their predecessor.
- **Columns.** The four byte columns are copied as they are. The three operand
  columns are copied with a base added by the operand's kind, which the
  operation fixes: a value (the site's value base), a block (its block base),
  a row of a side table (that table's base), an immediate (nothing). The
  callee's operands that name a parameter take the argument's destination
  instead.
- **Slices.** Phi slices' predecessors take the block base and their values
  the value base; switch slices' constants and targets theirs; each kept call
  of the callee gets a fresh descriptor and slices, as kept calls do now.
- **Slots.** The callee's stack objects are appended to the caller's. Today
  an expanded callee's slots "live throughout" the caller unless it marked
  them; an expansion now brackets its callee's unmarked slots with `lifetime`
  markers, a start at its entry and an end at each return, so that the
  frame's layout shares the bytes of two expansions' objects (#177's sharing
  is by those markers).
- **The result** is verified as now (structure, types, reachable-use
  dominance) before anything consumes it, and any failure keeps the caller as
  it was lowered: "inlining is an optimization, never a refusal".

### What it needs from the IR

Nothing the IR lacks; the prototype added no operation. What changes is in the
inliner and its driver:

- `ir_inline_body`'s admission takes any reducible or irreducible body whose
  operations it can clone: blocks, phis, `br`, `cbr`, `switch`, `ret`,
  `unreachable`, and calls without ABI records (then with the callee's own
  records, as #1282's by-value aggregates).
- The rewrite's block and predecessor remaps (`block_first` and `block_last`
  in the prototype) and the result's phi.
- The driver's unit stage replaces the per-lane cache
  (`CheckTier1Lane.inline_cache`, `check_tier1_inline_prepare`) and the
  per-caller reach (`check_tier1_inline_reach`), and
  `CheckTier1Inline` gains the summaries and the decisions.
- `IrInlineOptions` gains the policy's limits. `--tier1-no-inline` keeps
  every call, as now. Under `-fno-inline` and `-fno-inline-functions`, which
  the option table already follows for `__NO_INLINE__`, the `always_inline`
  class alone stays on, as in clang. `noinline` is a refusal.
- **`-g`.** Today "inline callee instances do not acquire invented source
  identities". An expanded callee's instructions keep their own statement
  rows (their file and line are the callee's, which is true), and its locals
  have no locations in the caller, as today's wrappers' don't. Lexical
  inlined-subroutine entries are a later step with its own gate; until then
  a breakpoint on an expanded function's line stops in each caller's copy
  and a backtrace shows the caller alone.
- **Unwinding** is the caller's frame's, unchanged.

### What it needs from the allocator and the frame

No new interface: an expanded body is ordinary IR. What matters is whether
Tier 1's allocation, per block with hints and edge moves
([the allocation guide](tier1-allocation.md)), holds the larger functions
without giving back in spills what the calls saved. The prototype answers
with Tier 1's own tagged instructions, speedtest1 without inlining and at
cost limits of 64 and 256, in M (callgrind's counts of the 9700X's builds):

| Origin | none | limit 64 | limit 256 |
| --- | ---: | ---: | ---: |
| call sequences | 252.2 | 209.9 | 158.5 |
| saves and restores around calls | 190.4 | 157.3 | 120.3 |
| frames | 205.7 | 198.2 | 175.2 |
| nodes | 1,745.3 | 1,707.4 | 1,654.5 |
| edge moves | 89.7 | 96.8 | 90.3 |
| edge spills and reloads | 137.7 | 135.8 | 156.4 |
| spills and reloads | 27.0 | 31.1 | 36.3 |
| recomputed values | 50.1 | 52.0 | 55.8 |
| branches | 379.8 | 380.8 | 386.8 |
| all of the program's code | 3,086.0 | 2,977.1 | 2,841.5 |

What the allocator gives back is a tenth of what the calls return at a limit
of 64 (12 M against 121 M) and a seventh at 256 (41 M against 285 M). Three
things are still asked of it:

- **Parameters are copies.** A parameter bound to an argument is the same
  value; the splice maps the operand and adds no node, so there is nothing
  to coalesce.
- **Pressure.** A value live across what was a call sat in a callee-saved
  register or was saved around it (#1116); across an expanded body it
  competes with the body's values. Where the body's blocks need more
  registers than are free, the per-block allocator spills on the edges into
  it. The counters above show it small at these sizes; the growth budget per
  caller bounds it, and the gate of each step counts spills and edge moves.
- **Memory.** #1353's and #1384's tables are dense in blocks by promoted
  locals and blocks by values, and expansion adds both to a caller. The
  decisions therefore refuse a caller past a bound on blocks times locals
  until those issues are fixed; their shapes are measured
  [below](#memory-on-pathological-functions).

### The policy

Costs are the inliner's own units: an expansion's cloned instructions plus the
rows it copies. One unit is about a quarter of a machine instruction on these
programs: at a limit of 128, callees of about 20 to 40 instructions are taken.
A call is expanded when its callee's summary is ready and one of these holds:

| Class | Rule | Why |
| --- | --- | --- |
| `always_inline` | whatever its cost, control flow or calls | what the attribute means, and what clang does; today a wrapper with a loop stays a call |
| shrinking | cost at most the call's own sequence | the caller gets smaller |
| single site | internal, one direct call in the unit, no address taken, cost at most a large bound | no code is added: the function goes away |
| small | cost at most the limit T | the measured trade below |

and the caller's growth and the unit's growth stay inside their budgets, sites
taken in call order. An external definition is a candidate of the "small"
class alone and keeps its own code; a weak definition, and any external one
under `-fsemantic-interposition`, never is.

A limit by the call site's loop depth, the usual static refinement, does not
pay here and is not part of the policy: the eligible sites inside a loop of
their function are 30% of speedtest1's sites and make 32% of its calls, so
depth does not tell hot sites from cold. The hot calls are chains of small
functions under a loop far up the stack.

T, the single-site bound and the budgets are set by measurement at each step.
Code growth is inherent to the reach, not to a policy: clang -O2's own text
grows by 52% on speedtest1 and 23% on Lua by its inlining (`-fno-inline`
against not), and ends at Tier 1's size only because it started 34% and 25%
smaller.

### Data-parallel first: what is vector and what is ordered

- **Calls to edges** (vector): the unit's `call_targets` gathered through the
  symbol table, the misses and the non-candidates compressed out, the pairs
  sorted and made unique for the graph and counted by callee for the single
  sites. One pass over a column of the unit.
- **Flags and bounds** (vector): candidates by a masked compare of the
  per-body flag and size columns, as the inliner's cost sweep does now (16
  costs a vector, with its scalar twin).
- **Components and levels** (ordered and scalar, for a measured reason):
  Tarjan's traversal over the candidates' subgraph, the inliner's tested
  one, then one sweep in component order for the levels. The data-parallel
  form is level-synchronous rounds: an edge waits while its callee has no
  level, and a round gathers each waiting edge's callee's state, compresses
  the edges that still wait and scatters a mark to their callers, 16 edges a
  vector. On the 9700X, on speedtest1's graph (2,712 functions, 9,025 edges;
  the record's `unit_stage_bench.c`, each form checked against the other),
  the traversal and the sweep take 66,500 cycles, 5.7 a function and edge,
  and the rounds 205,400, three times more (20,600 against 43,200 on
  Lua's): a gather and a scatter per edge and round lose to a traversal
  that touches each edge once. Either is under 0.01% of the unit's compile.
  The whole unit stage is gated below 1% of it.
- **Summaries** (parallel across functions, each one's passes as vector as
  those passes are): a level's functions on the lanes.
- **Decisions** (vector, then ordered): the costs against the limits by class
  in one masked compare a vector of sites; the growth budgets are prefix sums
  over a caller's sites in call order, cut at the budget.
- **The splice's sizes** (vector): prefix sums over the sites' size rows.
- **The splice's columns** (vector): 64 operations a vector are mapped to
  their three operands' kinds by the table lookup the IR's verifier already
  uses (`ir_lookup_bytes` over `ir_operand_table`), and each operand column,
  16 a vector, gets its kind's base by an in-register permute of the site's
  base row and one add. A callee's parameter operands are found by a compare
  with the parameter count and replaced through a permute of the argument
  row (a gather past 16 arguments). The caller's own operands move by a step
  function of the sites before them: K masked adds for a few sites, a gather
  through the destination table for many; which, by K, is measured in
  step 1.
- **The scalar twin** is the existing rewrite's loop, an operand at a time
  through the same tables, and the two must write identical columns.

On the 9700X the vector rebase takes 0.27 to 0.59 cycles a node (16 to
65,536 nodes) against 2.4 to 2.8 for the twin, five to ten times faster, both
writing the same columns (the record's `unit_stage_bench.c`). The splice is
not where the time goes either way: speedtest1's unit at a limit of 64 copies
tens of thousands of nodes (its records grow by 31,000), under 100,000 cycles
by the twin, in a compile of 2,260 M. It is vector because it is a column
copy, and its twin is its test.

### Verification

- **The IR's verifier** after every rewrite, and after SROA, promotion and
  each pass, as now.
- **The twins:** the splice's vector and scalar paths write the same columns
  on every function of the corpora and on generated units; the graph's and
  the decisions' paths the same audit columns (`IrInlineResult`'s).
- **The interpreter:** `test-ir-inline` runs generated units before and after
  inlining, after SROA and after promotion (6,160 executions now, on
  straight-line callees); its generator gains callees with diamonds, loops,
  switches, several returns, no return, calls of their own and stack
  objects, and callers that call them from loops and from phis' blocks.
- **Mutants:** dropping the result's phi operand of one return, a block
  base, a predecessor remap, a slice's rebase or a parameter's binding must
  each fail a test.
- **Programs:** `test-checker-driver`'s inlining fixture by each path, with
  and without inlining, against clang -O0's; speedtest1's verification hash,
  SQLite's shell and testfixture suites, Lua's scripts' outputs and the
  c-testsuite under `--tier1`, all in `correctness.tests` already.
- **The differential campaigns:** Csmith and YARPGen programs by `--tier1`
  with inlining, by `--tier1 --tier1-no-inline`, by Tier 0 and by clang -O0
  and -O2 must agree. Generated programs are mostly small static functions
  called in loops, which is this transformation's input; the campaign's size
  is a gate of each step.
- **The certificates** check what they check now, per function after the
  splice: the allocation's and the emitted code's proofs are against the
  function's own IR, so they cover an expanded body like any other. They do
  not prove the splice; the interpreter and the campaigns do.
- **Self-host:** the compiler built by `--tier1` with inlining must build
  itself to the same bytes, as the qualification requires now.

### What it costs a compile

What the design adds to `--tier1`'s compile, by part:

| Part | Cost |
| --- | --- |
| the unit stage's graph, levels and decisions | one pass over the unit's calls; gated below 1% of the compile |
| summaries | none net for a candidate that is emitted (its own lowering, promotion and optimization, moved); one promotion and optimization for a candidate that ends dead |
| the splice | proportional to the copied nodes; under 0.1% of the compile |
| the larger bodies through SROA, promotion, optimization, selection, allocation and emission | proportional to the growth of the code, and the dominant term |
| dead functions | negative: no selection, allocation or emission for them |

The growth is the price, and the prototype measures it without the last
row's credit and without the summaries. `--tier1 -c sqlite3.c`, one lane,
medians of five runs:

| Variant | ms | against none | records | `.text` | peak memory |
| --- | ---: | ---: | ---: | ---: | ---: |
| none | 427 | | 271,136 | 1,037,736 | 299 MB |
| `s64` | 685 | +60% | -0.1% | -0.4% | 314 MB |
| `c64` | 786 | +84% | +11.4% | +10.8% | 340 MB |
| `c128` | 1,056 | +147% | +30.2% | +30.6% | 342 MB |
| `x128` | 1,275 | +199% | +33.4% | +33.7% | 342 MB |
| `x256` | 2,196 | +414% | +63.5% | +64.7% | 354 MB |

The second row is the lesson: straight-line leaves add no code and 60% of the
compile. The per-caller arrangement's cost grows six to eight times faster
than the code it adds. With summaries the expected cost is the code's growth
through Tier 1's own pipeline, which is 299 ms of the 427 by the stage log of
#2538's record (the default tier's whole compile is 124 ms): about +35 ms (+8%) for `c64`'s code and
+190 ms (+45%) for `x256`'s, before the dead functions' credit. Step 1's gate
is that figure.

The pipeline's own speed is the sibling design's (#2538, one allocation call
a function); what is given here is the lever's cost relative to whatever that
pipeline costs.

### What the prototype measured

The record's `inline-cfg-prototype.patch` (403 lines over
`ir_optimize_inline.c` and `c_check_tier1_inline.c`, behind environment
switches that leave main's code unchanged when unset) adds the splice to the
existing inliner and offers it the unit's functions by a cost limit. It is the
per-caller arrangement still, without summaries, single-site rules, the two
forms or dead functions, so its reach and its run times are the design's lower
bound and its compile times an upper one. `c` variants take internal
functions; `x` variants the unit's external definitions too; `s64` is the
calibration above (straight-line leaves) by the same compiler.

Every variant's speedtest1 prints the verification hash at each of eight
placements of its code, and its Lua prints each script's expected output in
each of four link orders, before any timing. As 100 ln(variant / none) of user
cycles, five rounds a placement, with its 95% interval over the placements
(eight and four; a placement's rounds are not independent draws of a layout),
the placements at which the variant's median is the faster, and the growth of
the objects' `.text`:

| Variant, by cost limit | speedtest1's cycles | faster at | `.text` | Lua's cycles | faster at | `.text` |
| --- | ---: | --- | ---: | ---: | --- | ---: |
| `s64`, straight-line leaves | -3.28 (-3.90 to -2.66) | 8 of 8 | -0.4% | +0.54 (-1.10 to +2.17) | 1 of 4 | -0.1% |
| `c32` | -4.26 (-5.11 to -3.40) | 8 of 8 | +5.2% | +2.32 (-8.41 to +13.04) | 2 of 4 | +4.2% |
| `c64` | -6.55 (-7.19 to -5.90) | 8 of 8 | +10.5% | -2.37 (-3.44 to -1.30) | 4 of 4 | +11.4% |
| `c128` | -9.09 (-9.75 to -8.44) | 8 of 8 | +29.7% | -6.30 (-9.13 to -3.48) | 4 of 4 | +25.7% |
| `x128` | -10.33 (-11.01 to -9.65) | 8 of 8 | +33.7% | -7.76 (-10.12 to -5.40) | 4 of 4 | +31.0% |
| `x256` | -12.82 (-13.40 to -12.23) | 8 of 8 | +64.6% | -7.89 (-10.38 to -5.41) | 4 of 4 | +66.3% |

At its largest limit the prototype's speedtest1 runs 823.7 M cycles where
`--tier1`'s runs 936.3 M, 12.0% fewer, and its Lua 1,140.2 M for 1,236.3 M,
7.6% fewer (7.5% at half the growth, `x128`). That is what LLVM's own inliner
gains on its weak back ends (11.9% and 8.2%), from a policy with one rule, and
it would put `--tier1` at about 1.30 times clang -O2's cycles on both
programs.

The calls into the programs' own code, counted by callgrind in the 9700X's
builds:

| | none | `s64` | `c32` | `c64` | `c128` | `x128` | `x256` | clang -O2 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| speedtest1, M | 68.92 | 60.23 | 55.45 | 48.52 | 40.29 | 38.34 | 29.72 | 25.62 |
| Lua, M | 91.79 | 90.64 | 90.26 | 87.25 | 77.50 | 68.38 | 58.50 | 54.20 |

Each removed call is worth 2.8 to 3.0 cycles on speedtest1 at every limit
(2.8 to 5.3 on Lua), the calibration's and LLVM's figure again: the gain is
the calls', and it does not fade as the callees grow.

The compiler itself, built by the prototype (scalar host; the same two
workloads as above, three rounds, every build's objects the same bytes):

| The compiler built by | `-c sqlite3.c`, cycles, M | returns executed, M | `--tier1 -c`, cycles, M | `.text`, MB |
| --- | ---: | ---: | ---: | ---: |
| `--tier1` | 8,807.6 | 345.4 | 13,030.9 | 39.89 |
| the prototype: `always_inline` callees with control flow | 8,738.6 (-0.8%) | 299.7 | 13,002.8 (-0.2%) | 40.99 |
| the prototype: and small internal functions, limit 64 | 8,535.5 (-3.1%) | 223.4 | 12,633.7 (-3.0%) | 42.62 |
| clang -O2 with `-fno-inline` (`always_inline` alone) | 3,643.8 | 204.6 | 5,990.5 | |
| clang -O2 | 3,054.9 | 21.7 | 5,153.5 | |

The prototype removes 122 M of the compiler's 345 M calls for 3.1% of its
cycles, 2.2 cycles a call, and does not reach this program's hottest ones:
`host_aesenc` and `intern_lane_rounds` take and return 128-bit values by
value, calls with ABI records, which the prototype's general path refuses
(the straight-line path has taken them since #1282). They stay a third of
the build's cycles. Step 0 takes them as #1282 does for straight lines; what
that is worth also depends on #1344, since the values an expansion exposes
are the ones Tier 1 keeps in memory.

### The steps and their gates

Each step lands behind `--tier1` as today's inlining does, with
`--tier1-no-inline` as its control, and each gate is measured on the 9700X on
speedtest1 over eight placements, Lua over four link orders and the compiler's
two workloads, with `--tier1 -c sqlite3.c` for the compile.

| Step | What lands | Gate |
| --- | --- | --- |
| 0 | The splice in `ir_optimize_inline.c`, with its twin and tests; the driver offers it today's callees only, so `always_inline` functions are expanded whatever their control flow | the interpreter's generated callees with control flow agree before and after (at least as many executions as today's 6,160), the mutants fail, the twins write the same columns; the compiler built by `--tier1` reproduces itself and, on `-c sqlite3.c`, executes at most 215 M returns in no more cycles (345 M now; clang -O2 with `-fno-inline`, which expands `always_inline` alone, 205 M; the prototype, without by-value 128-bit calls, 300 M); speedtest1's and Lua's objects are unchanged |
| 1 | The unit stage: calls, graph, levels, summaries, decisions; the per-lane cache and the per-caller reach removed. Internal callees, the `always_inline`, shrinking and small classes at the limit of the prototype's `c64` | objects equal at 1 and 8 lanes and by both twins; speedtest1 at least 6% fewer cycles at every placement and Lua not slower at any (the prototype: 6.3% and 2.3%); the unit stage under 1% of `--tier1 -c sqlite3.c`, and the compile's growth no more than its code's growth plus 3 points (the prototype: +84% of compile for +11% of code) |
| 2 | The two forms, the single-site class, dead functions, external definitions; the limits and budgets chosen by a sweep on the three programs | speedtest1 at least 12% and Lua at least 7.5% fewer cycles (the prototype's largest limit) with no more `.text` than the prototype's, the target being that gain at half its growth; the compiler at most 60 M returns and at least 8% fewer cycles (the prototype: 223 M and 3.1%); a Csmith and YARPGen campaign of at least 1,000 programs by each of `--tier1`, `--tier1 --tier1-no-inline` and clang agrees |
| 3 | Inlined-subroutine entries under `-g`; callees in cycles into callers outside them; the vector splice's rule for a caller's own operands | gdb's backtrace shows an expanded callee; each of the others by its own measurement |

Steps 0 and 1 carry the risk. Step 0 is small and already separable: it is the
prototype's splice with tests. Step 1 is where the design's claim about
compile cost is tested, at a small limit, before anything depends on it.

### Step 0, measured

Step 0 landed as `IrInlineOptions.control_flow` (#2613): the splice with a
vector rebase of the callee's operand columns and its scalar twin, lifetime
brackets for the callee's unmarked slots and the aggregate arguments' copies,
and the callee's calls with ABI records, offered by `--tier1` to its
`always_inline` callees. Its gate, on the 9700X against main `df85213e`
([the record](https://github.com/buster14a/buster-performance/tree/9f3680a0b6dbb3de7b1662b4f769b6a75d455213/docs/measurements/tier1-unit-inlining-step0-2541-2026-11-13)):

| Gate | Result |
| --- | --- |
| the interpreter's generated callees with control flow agree before and after | 14,256 executions (6,432 before): nine callee shapes into six callers at two types, before and after inlining and after SROA and promotion, and a by-value callee with two returns |
| the mutants fail | each of ten: a return's phi operand, a block base, a predecessor remap, a slice's rebase, a parameter's binding, the vector shift, a kind table's entry, the scalar `cbr` base, the lifetime ends' place, a caller branch's remap |
| the twins write the same columns | on every unit of `test-ir-inline` |
| the compiler built by `--tier1` reproduces itself | the self-host qualification passes with every unit by `--tier1`; on the 9700X the `--tier1` build's objects are clang -O2's build's |
| at most 215 M returns on `-c sqlite3.c`, in no more cycles | **283.8 M** (345.0 M on main; 521.7 M without inlining), in 1.1% fewer cycles: not met |
| speedtest1's and Lua's objects unchanged | Lua's and `speedtest1.c`'s are; `sqlite3.c` has one `always_inline` function with control flow, `allocateSpace`, whose two calls are now expanded, as clang expands them; speedtest1 over eight placements +0.15 (-0.33 to +0.63), no change measurable |

The returns gate assumed that the expanded callees' own calls go away. In the
scalar host's build, the hot ones are `host_aesenc`'s 16-byte `memcpy`s of its
by-value `__m128i`s, which stay calls of libc (#1373): sampled call records put
them at about 42% of the remaining calls. Expanding `host_aesenc` also moves its
argument into its caller's memory by two 8-byte stores, at a target without
AVX-512, which `memcpy`'s 16-byte load cannot forward from (#1344's pattern),
so libc's share of the cycles grows from 3.8% to 14.7% and gives back most of
the 26% that `host_aesenc` and `intern_lane_rounds` took as calls. Both are
the next steps' preconditions on this program, as the design's expected case
already said of #1344. The compiler's own `--tier1` build expands 4,097 calls
where main's expands 795, for 6.6% more records and 3.0% more user time.

### Step 1, measured

Step 1 landed as the unit stage and summaries in `c_check_tier1_inline.c`
(#2641; [the Tier 1 guide](tier1.md)'s "Inlining"), with the per-lane cache and
the per-caller reach removed. Where it departs from the design above:

- **A summary is Tier 1's passes' result.** Its lowering, its calls' expansions
  and the passes from promotion to the verified optimizer (`check_tier1_passes`,
  factored out of the function's own pipeline: wide and vector splits, truth
  values, threading, if-conversion, the optimizer with layout), with the
  optimizer's verified analysis. Its own code goes on from it at selection, so
  its optimization is moved, not repeated; callers copy that same function. A
  first version that optimized each small summary for its callers apart from
  its own pipeline paid 442 M instructions of sqlite3.c's compile twice.
- **Levels are claims.** Rather than lanes meeting between levels, the first
  lane to need a summary claims it by a compare-and-swap of its state word and
  builds its callees' first (an explicit stack); a lane that needs one another
  is building waits. Summaries depend only on their acyclic callees', so no wait
  waits on itself, and the objects are the same by any lane count.
- **The classes are two.** `always_inline` (no limit) and small (a summary of
  at most 256 instructions, each call at an inliner cost of at most 64, which
  takes every shrinking callee too); the caller's growth is the inliner's
  existing bound alone. The inliner takes a limit per callee
  (`IrInlineUnit.limits`), rewrites the caller alone (`IrInlineUnit.only`) and
  trusts its verified inputs (`IrInlineOptions.trusted`), and the caller's runs
  of nodes between its expansions are copied 16 a vector by their operand
  kinds' table lookup and two masked gathers, with the per-node loop as the
  scalar twin.
- **SROA after an expansion only where it copied an aggregate.** SROA after
  every expansion of a function with an address-taken slot split 449 of
  sqlite3.c's functions for 662 M instructions and changed its `.text` by 0.05%:
  what it split was the callers' own aggregates. It now runs where an expansion
  copied an aggregate argument or result, or on vectors, as before.

Its gate, on the 9700X against main `26f0bb0e`
([the record](https://github.com/buster14a/buster-performance/tree/eb8d0c4dbd89a4e90dde5e7537b2a05fd936b5e9/docs/measurements/tier1-unit-inlining-step1-2541-2026-11-13)):

| Gate | Result |
| --- | --- |
| objects equal at 1 and 8 lanes and by both twins | 72 of 72 pairs the same bytes (sqlite3.c, speedtest1.c and Lua's 34 units, by each path) |
| speedtest1 at least 6% fewer cycles at every placement | **5.34% fewer** (4.86 to 5.82), faster at 8 of 8, 4.45 to 6.08 each: not met |
| Lua not slower at any link order | **1.51% fewer** (-4.18 to +1.16), faster at 3 of 4, the fourth +0.14: not met |
| the unit stage under 1% of `--tier1 -c sqlite3.c` | 0.02% of its cycles |
| the compile's growth at most its code's plus 3 points | **+17.3% user cycles** for +12.8% `.text` (+19.7% instructions, peak memory +4.4%): not met by 1.5 points |

speedtest1's expansions remove 16.9 M of its 76.2 M calls, 2.6 cycles each, the
prototype's figure; its `.text` grows by 12.5% where the prototype's c64 grew it
by 10.5%, since summaries that went through every pass are smaller and more of
them fit the class. Lua's hot functions are external, which step 1's classes
don't take: its scripts lose 4.6 M of 110.7 M calls. The gate's figures are the
prototype's against main of 2026-11-12, which ran speedtest1 in 936.3 M cycles
and Lua in 1,236.3 M; today's main runs them in 882.4 M and 1,117.6 M, and
step 1's 836.5 M and 1,101.0 M are below the prototype's c64 (876.9 M and
1,207.3 M), speedtest1's below its c128 too (854.9 M).

The compile's growth is the larger IR's through the backend, at each stage's
own share: the inliner is 1.6% of the compile and the unit stage 0.02%, but
the IR grows about twice as fast as the code (on sqlite3.c locally, the
optimized functions' instructions +23.4% and blocks +37% for `.text` +10.3%),
since an expanded callee's IR is all copied where its call was one instruction.
Merging the splice's head and continuation into their neighbours would remove
about a third of the added blocks, but the backend's cost follows instructions,
not blocks; dead functions (step 2) are the design's credit against it.

The compiler built by `--tier1` reproduces clang -O2's build's objects, and its
own build loses 104 M of its 283 M calls for 4.9% of its cycles. The driver's
fixtures that measure a frame across a call (a backtrace's depth, a value kept
in a callee-saved register across one) mark their callees `noinline`, since a
small static helper is now expanded.

### Expected gains

As changes of `--tier1`'s user cycles, with the measurements each rests on:

| | speedtest1 | Lua | The compiler | Rests on |
| --- | ---: | ---: | ---: | --- |
| conservative | -9.8% | -7.5% | -3% | the prototype at a limit of 128, a third more code (on the compiler, at 64) |
| expected | -12% | -8% | -8 to -11% | the prototype at its largest limit, which the classes should reach with less code; LLVM's inliner on its weak and fast-allocator back ends gains 10.4 to 11.9% and 7.5 to 8.2% |
| optimistic | -13.5% | -11.7% | -15 to -21% | LLVM's inliner on its -O2 back end, at clang's call counts |

At the expected case `--tier1` would take about 1.30 times clang -O2's cycles
on both programs, from 1.48 and 1.40. A check from another side: clang's call
counts mean 43.3 M and 37.6 M calls removed, and at the 2.9 cycles that the
prototype measures for each removed call at every limit, that is 13% and 9% of
`--tier1`'s cycles.

On the compiler, `--tier1`'s build executes 345 M returns where clang -O2's
executes 22 M and clang -O2 with `-fno-inline` (which still expands
`always_inline`) 205 M: step 0 alone is worth about 141 M calls there, and
the later steps about 183 M. The expected case is those 324 M calls at the
2.2 to 2.9 cycles measured for a removed call; the conservative one is the
prototype's, which leaves the 128-bit calls.

### What would refute it

- **The prototype's own result.** If no limit of the prototype were faster
  than no inlining at every placement on speedtest1, Tier 1's allocation would
  be giving back what the calls save, and the allocation across blocks would
  be the first lever instead. It is not: every limit is faster at eight of
  eight placements, by 4.2% to 12.0%, and Lua at four of four from a limit
  of 64 up.
- **Step 1's compile gate.** If the unit stage with summaries does not bring
  the compile's growth down to about the code's growth (the per-caller
  arrangement costs +41% for no growth), the design's central claim is wrong
  and the stage has to move again.
- **Step 2's reach.** If bottom-up expansion with the two forms and the
  single-site class leaves speedtest1 above 40 M calls into its own code
  (clang 25.6 M, the prototype 29.7 M at its largest limit), the policy's
  classes are not the ones that matter.
- **Growth without gain.** If a limit's `.text` growth exceeds 50% for less
  than the conservative case's gain, the limit comes down; if no limit meets
  both, the "small" class is dropped and the lever is the first three classes
  alone.
- **A mismatch the tests cannot explain** in the campaigns stops a step, as
  it would any pass.

### Risks and open questions

- **Pressure in large callers.** `sqlite3VdbeExec` and `luaV_execute` already
  spend 98 M and 109 M instructions in spills and reloads. The growth budget
  per caller is the control; whether those two should receive expansions at
  all is a measurement of step 2 (the prototype's largest limit gives back on
  Lua's arithmetic and strings scripts part of what a limit of 128 gains:
  216 M and 265 M cycles against 211 M and 254 M).
- **Memory.** #1353 and #1384 are open; expansion makes their shapes likelier.
  The bound on blocks times locals refuses such callers meanwhile.
- **Compile time.** The lever's cost is the code's growth through the whole
  back half, on a pipeline that already takes 3.45 times Tier 0's time. It is
  worth less the slower that pipeline is; #2538's design owns that.
- **Debugging.** Until step 3, an expanded callee has no frame in a backtrace
  and its locals no locations. `-g` users who need them have
  `--tier1-no-inline`; `-O1` and above selecting Tier 1 would need step 3 or
  an `-Og` that does not inline.
- **Code size.** `-Os` and `-Oz`, if they ever select Tier 1, want the
  shrinking and single-site classes alone.
- **Recursion's stack.** Expanding into a recursive function grows its frame
  in every activation; the lifetime markers bound it to the largest
  expansion's objects, not their sum.
- **Tier 0's callers.** A caller that falls back to Tier 0 keeps its calls, so
  a callee it calls cannot be dropped: the dead set is decided after every
  caller's outcome is known (the last pass of "Where it sits"), and dead
  functions wait for step 2.
- **Open:** whether summaries should be selected from as they are (one
  promotion per candidate) or re-optimized after the caller's SROA exposes
  more; whether a callee's `cold` attribute or a call's `__builtin_expect`
  context should lower a site's limit; link-time inlining across Lua's units
  (39% of its calls), which is outside a compiler that compiles one unit.

## What stands between Tier 1 and `-O1` and above

Tier 0 is the default for every `-O` (#1132, closed with that decision
recorded in [the Tier 1 guide](tier1.md)). Changing it is the owner's decision.
This is the evidence for it, at main `c5f0efb2` on the 9700X; nothing here
changes the default.

### Refusals and fallbacks

| Corpus | Functions | By Tier 1 | By Tier 0 | Resource failures |
| --- | ---: | ---: | --- | ---: |
| speedtest1 with `sqlite3.c`, one unit | 2,709 | 2,709 | 0 | 0 |
| `sqlite3.c` and `shell.c`, the 9700X job's options | 3,214 | 3,214 | 0 | 0 |
| Lua 5.4.8, 33 units | 1,088 | 1,087 | `luaD_rawrunprotected` (calls `setjmp`) | 0 |
| musl, 1,355 units, every unit compiles | 2,778 | 2,777 | `start` of `timer_create.c` (calls `setjmp`) | 0 |
| the compiler, 43 units, scalar host | 24,872 | 24,855 | 17 | 0 |
| the compiler, native flags | 38,544 | 38,527 | 17 | 0 |
| c-testsuite's subset, 220 programs, all pass | 430 | 430 | 0 | 0 |

The compiler's 17 are 15 functions: six naked test fixtures, one that calls
`setjmp`, and eight of `assembler_driver.c` around an aggregate passed by
value in memory (four refused as functions, four at a call with an ABI
record). A fallback is correct code at Tier 0's speed; none of these is a
failure to compile. What is missing for a default is a statement of which
refusals are permanent (naked, returns-twice) and a test that pins the count
on these corpora.

### The differential campaigns

#142 is closed, and CI runs no campaign: `correctness.tests` runs the
harness's own tests, the c-testsuite, SQLite's shell and testfixture suites
and `test-ssa-source`'s generated programs under `--tier1`, and no Csmith or
YARPGen program. Campaigns have been run by hand per pull request (80 programs
in #1116's record). This session ran one the same way, both tiers against
clang -O0 and -O2: of 76 programs, 63 agree, and the reference itself refuses
the other 13 (clang -O0 fails to link YARPGen's largest arrays, passes the
output limit, or runs past the 30 s timeout); no mismatch. That is evidence
of nothing wrong at a size that proves little. A default needs a standing
campaign, in the
nightly job or on the 9700X, with its seeds advancing.

### `-g`

`--tier1 -g -c sqlite3.c` writes the same `.text` as without `-g`, and
`llvm-dwarfdump --verify` reports no errors on it (10,521 warnings, all of an
entry that declares children and has none). It takes 681 ms against 426 ms
without `-g` (Tier 0: 157 ms against 124 ms), `.debug_loclists` is 821 KB
against Tier 0's 183 KB, and peak memory 337 MB against 299 MB. What a
debugger shows in optimized code (which locals have locations where) is
[the Tier 1 guide](tier1.md)'s "Debug information"; no measurement here
covers how much of it a user gets. `-Og` has no Tier 1 meaning yet.

### Memory on pathological functions

Both issues are open and reproduce. One function each, peak memory and time:

| Shape | Tier 0 | `--tier1` | clang -O2 |
| --- | ---: | ---: | ---: |
| 4,000 `for` loops (#1353) | 68 MB, 0.05 s | 686 MB, 0.11 s | 162 MB, 24.2 s |
| 8,000 `for` loops | 83 MB, 0.02 s | 2,491 MB, 0.36 s | 228 MB, 108.7 s |
| 2,000 statements of a statement-expression `MAX` | 69 MB | 687 MB | 124 MB |
| 4,000 such statements | 102 MB | 2,495 MB | 141 MB |
| one `\|\|` of 16,000 operands (#1384) | 76 MB | 1,126 MB | crashes (stack overflow) |

Every `--tier1` run succeeds here, and none falls back: memory grows with the
square of the function, 2.5 GB at 8,000 loops. Where the memory is not
there, the policy makes it an error: a resource failure fails the compile,
"no implicit Tier 0 retry" ([the outcomes guide](tier1-outcomes.md)), so a
function that Tier 0 compiles in 83 MB would stop a build. A default would
meet such functions in generated code. Either the two issues are fixed, or a
function past a bound takes Tier 0 by rule, counted as a fallback, or a
resource failure retries by Tier 0.

### Compile time

`-c sqlite3.c` takes 124.0 ms by the default tier and 427.5 ms by `--tier1`:
3.45 times the time, 3.77 times the user cycles (2,259 M against 599 M) and
1.26 times the peak memory (299 MB against 237 MB). The compiler's own 43
units take 27.3 s of user time by `--tier1` against 19.8 s. clang -O2 takes
8.48 s for the same unit on the same core, 6.69 s at -O1 and 0.56 s at -O0.
This is the sibling design's subject (#2538); for the decision, `-O1` and
above selecting Tier 1 today would make every such build 3.4 times slower to
compile than Tier 0's, for code that takes 27 to 31% less time to run, and
still 20 times faster to compile than clang -O2's.

### Objects that depend on the compiler's host

When this was written, `--tier1 -c sqlite3.c` wrote one object at 1 and at 8
lanes, and another by the scalar host path (`--scalar`): `.text` was 1,037,736
bytes by the vector path and 1,037,688 by the scalar one (Lua's `lvm.c`: 30,812
and 30,788), while Tier 0's object was the same by every path. That was #1395:
the vector cover and its scalar twin picked different instructions for some
multiplies. #1715 gave both paths the same costed cover; the frame colorer's
scalar twin then still gave spilled values other slots (#2606), and now
computes the vector colorer's colors exactly. Both paths now write the same
bytes for `sqlite3.c`, Lua's 34 units and c-testsuite's 220 tests (the vector
path's objects unchanged), and `test-ssa-source --tier1` requires the same
executable from both. A default tier whose output depended on whether the build
machine has AVX-512 wouldn't be reproducible across machines.

### What `--verify` covers

`--tier1 --verify -c sqlite3.c` certifies the emitted code of 2,153 of 2,668
functions (#939's certificate), leaves 68 uncertified by reason (dynamic
frames, variadic definitions, long doubles, indirect jumps to several labels)
and does not check the 447 that the straight-line emitter writes; the object
is the same bytes with and without it, at 596 ms against 426 ms. It is off by
default. Whether a default tier runs it in CI on the corpora, or always, is
part of the decision.

### The rest

- **`-O` levels are not distinguished.** `-O1`, `-O2`, `-O3`, `-Os`, `-Oz`,
  `-Og` and `-Ofast` would all be the same pipeline. clang's -O1 is within 3%
  of its -O2 on both programs, so one level is a fair first mapping; `-Os`
  and `-Og` want policies (inlining, above all) that do not exist.
- **`__OPTIMIZE__` already changes the source.** The levels define it today
  without changing the code, so headers already take their optimized paths
  under Tier 0.
- **The stack protector.** The 9700X's clang defaults to
  `-fstack-protector-strong` (938 protected functions in its speedtest1); this
  compiler emits none at any tier. Not Tier 1's, but a `-O2` build that
  replaces clang's loses it.
- **Unwind tables** are 241 KB for `sqlite3.o` by Tier 1 against clang's
  88 KB (#1377, open).
- **Tier 1's own code quality gates**, which the guide names with #1116's:
  the ranking above is their state.
- **A run-time regression gate.** `measure-tier1-programs` runs daily and on
  pull requests that touch Tier 1; it reports and does not fail on a slower
  result. A default would want a threshold.

## The other levers without an owner

Measured here, not designed. The first is #2553 and the next three are #2554.

- **Allocation across blocks.** Moves, spills and reloads on edges are 7.4% of
  speedtest1's instructions under `--tier1` and 10.0% of Lua's, after #1216.
  In `luaV_execute`, 424 M instructions are edge moves, 247 M of them swaps of
  two registers through r11: each block is allocated alone and the edge
  repairs the difference. An assignment that follows the dominator tree, so
  that a value keeps its register from its definition to its uses' blocks
  and only phis move, removes the moves that are not phis'. The ladder's
  bound for the whole allocator's group is in "The ranking".
- **Truth values across blocks.** A constant made, tested and branched on
  (51.5 M and 69.6 M instructions) and `setcc` then `test` in the next block
  (22.5 M and 65.7 M): a branch on a phi of constants should be a branch
  from each predecessor (jump threading over the truth-value phis that
  `ir_truth_simplify` leaves). LLVM's jump threading is worth 1 to 2% from
  the top.
- **Lua's dispatch.** Its computed `goto` funnels through one indirect jump
  (55 M jumps to a jump): each handler's tail should hold its own copy of the
  dispatch, as clang's tail duplication leaves it.
- **Constants and shifts.** A constant through a register for the next
  store (31 M and 37 M), where a store of an immediate does it, and a shift by
  zero (56 M on Lua): peepholes of selection.
- **Loads the back end repeats** (5.4% and 2.4% of the back end's
  instructions) need #139's value numbering to see memory.

## Related issues

- #138: the area; this design's claim and result are on it. #2538: the
  sibling design, what Tier 1's pipeline costs a compile.
- #139: the SSA passes, whose list names this pass; #1282 (closed): the
  always_inline wrappers and SROA's options that this design reuses; #1398:
  Tier 0's `always_inline`.
- #1189 (closed): the method and the previous breakdown; #1116 and #1217
  (open) and #1216, #1218 and #1219 (all three closed): the back end's
  causes.
- #1344, #1373, #1375: the compiler's own largest causes.
- #2553 and #2554: the next causes without an owner, filed from this
  measurement.
- #1353, #1384, #1395, #1377: the inventory's open items; #1132 (closed): the
  default's decision.
