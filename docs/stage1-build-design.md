# A faster stage 1 build: design

This is a design with its measurements, not an implementation. It says what
would most reduce the time to build the compiler with clang (`./build.sh`, the
stage 1 build) in two cases: the **edit loop** (one file changed, a new
compiler wanted) and the **clean build** (a fresh clone, CI, a branch switch).

Every time below was measured on the Ryzen 7 9700X under its lock, at main
`c5f0efb2` with the host's clang 23.1.1 and the build's eight jobs, unless the
text says otherwise. The record
[stage1-build-design-2040-2026-11-12](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/stage1-build-design-2040-2026-11-12)
has the raw runs, the scripts and the prototype. Part of
[#2040](https://github.com/buster14a/buster/issues/2040); the design's issue is
[#2559](https://github.com/buster14a/buster/issues/2559).

## In brief

- **The change: the optimizer in parts.** A unit's `-O3` pipeline is one serial
  clang process today (56.6 s for the checker's unit), and any edit of the unit
  repeats all of it. The design stops a unit's compile before LLVM's optimizer,
  deals the unit's functions into parts, and optimizes and generates each part
  as a job of its own. The parts run at once, and a rebuild repeats only the
  parts whose bitcode changed.
- **It arrives as a third configuration**, called Develop here, beside Release
  and Debug: Release's flags and `-O3`, line tables for debug information, the
  tests' own functions unoptimized, the benchmark units built on demand.
  **Release stays as it is, byte for byte**: it is the product every benchmark
  measures.
- **Measured** on a prototype that builds the whole compiler through the real
  `./build.sh`:

  | Scenario | Today (Release) | Develop |
  | --- | ---: | ---: |
  | An edit of the checker's unit (`c_check_input.c`) | 72.6 s | 16.7 s |
  | An edit of the backend's unit (`differential.c`) | 46.5 s | 6.9 s |
  | An edit of the front end's unit (`c_parser_impl.c`) | 26.0 s | 4.9 s |
  | An edit of a test file (`c_checker_driver_test.c`) | 72.5 s | 12.3 s |
  | An edit of a header 42 units read (`host_isa.h`) | 83.0 s | 16.7 s |
  | Clean build | 83.1 s | 36 s (54.7 s with the benchmark units) |

- **What it costs.** The Develop compiler runs the compile path 1.1% to 1.9%
  slower than Release (user cycles; `-c` of `sqlite3.c`, `-fsyntax-only` of the
  checker's unit). The tests take 5% longer, from their own functions being
  unoptimized and not from the parts. Its debug information has functions and
  lines, not variables. Release and Debug are untouched.
- **Later steps, each a decision of its own:** Release itself in parts (at the
  limit where its speed measured +0.12% and +0.14%, inside the floor, a clean
  build takes 77.5 s and an edit 35.8 s); a unit compiled by the compiler
  itself (2 s, for a compile path 3.4 to 5.4 times slower); the units' sources
  split so that front ends run in parallel too.
- **Found on the way:** CI's toolchain on the 9700X can't build #2058's parts
  (lever 7): a clean Release build with it compiles the units whole and takes
  97.1 s, where the same clang with the parts takes 68.9 s.

## Today

`./build.sh` as it is (Release, `-g`), each build its own hold of the lock:

| Scenario | Wall | CPU | Commands rerun, and their work |
| --- | ---: | ---: | --- |
| Clean build (`rm -rf build`) | 83.1 s (twice) | 500 s | 69: 43 compiles 371 s, 2 splits 6.6 s, 18 backend parts 105 s, 2 joins, link, bake |
| Nothing changed | 0.03 s | | none |
| One product file of the checker's unit (`c_check_input.c`) | 72.6 s | 135 s | 14: the unit 56.8 s, its split 4.3 s, 9 parts 63.4 s (the longest 10.3 s), join, link, bake 1.0 s |
| One product file of the backend's unit alone (`differential.c`) | 46.5 s | 86 s | 14: the unit 35.9 s, split 2.2 s, 9 parts 40.8 s (7.5 s) |
| One code-generation file, which both units read (`tier0.c`) | 75.9 s | 246 s | 28: 4 compiles 118 s (60.1 s), 2 splits, 18 parts 105 s, 2 joins, link, bake |
| One product file of the front end's unit (`c_parser_impl.c`) | 26.0 s | 36 s | 4: the unit 25.3 s, `c_parser_benchmark.c` 9.7 s, link, bake |
| One test file of the checker's unit (`c_checker_driver_test.c`) | 72.5 s | 135 s | 14, as the product file's |
| One shared test root (`c_checker_test.c`) | 75.7 s | 236 s | 27: 3 compiles 108 s, 2 splits, 18 parts 105 s, 2 joins, link, bake |
| One shared header (`host_isa.h`) | 83.0 s | 499 s | 66: 42 compiles and what follows them |

Each edit appends an emitted function to the file, so every unit that reads
it has new code.

These are runs of 14:23 UTC and later. The campaign's first runs of the same
scenarios, from 11:23 to 12:36, shared the host with another session's stray
process ([#2555](https://github.com/buster14a/buster/issues/2555)): the clean
build read 87.9 s and 534 s of CPU beside it, and the edits, which are one
clang process on an otherwise idle host, read the same (72.6 s). Every build
time in this document is from a run after that process ended (13:51 UTC),
except where a row says otherwise; the record keeps both and marks each.

**Where a clean build's time goes.** Its 69 commands are 485 s of work: the
eleven `main.c` units' compiles 249 s (the checker's unit and the backend's
stop at optimized bitcode), their 18 backend parts 105 s, the 32 units of
their own (benchmarks, measurements, tools) 122 s, and the splits, joins, link
and bake 9 s. On eight jobs that work is at least 61 s. The build takes 83 s
because one chain is longer than that: the checker's unit compiles for 67.2 s
(56.6 s when it compiles alone, as after an edit), its split takes 4.2 s, its
longest part 10.3 s, and the join, link and bake 1 s.

**An edit waits for one clang process.** Every rebuild above is one unit's
front end and `-O3` pipeline, serial, then its parts. Any edit regenerates all
nine parts of a unit, not only those it touched: each part's bitcode holds the
whole module's numbering of string literals and, with `-g`, the edited file's
checksum. A test file costs what a product file costs.

**The optimizer's time by function** (clang's `-ftime-trace` at 10 µs, each
unit compiled alone on CPU 2, every pass's own time given to the function it
ran on):

| Unit | Front end | Optimizer | Product functions | Test functions | Benchmark | No function |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Checker's (to bitcode, 58.9 s) | 6.6 s | 52.0 s | 42.6 s (82%) | 1.3 s (3%) | 0.6 s | 7.5 s |
| Backend's (to bitcode, 37.4 s) | 4.0 s | 33.3 s | 14.2 s (43%) | 14.9 s (45%) | 0.5 s | 3.7 s |
| Front end's (to its object, 26.0 s) | 3.0 s | 11.6 s | 5.7 s (49%) | 4.8 s (41%) | 0.3 s | 0.8 s |

"No function" is the module passes' own time (writing the bitcode, 2.7 s of
the checker's) and events shorter than the trace's granularity outside any
function's. The front end's unit also generates its code in that process:
11.3 s, of which the test functions are 5.9 s (52%).

- **The checker's unit's tests already compile unoptimized** (`#pragma clang
  optimize off`, #681), which is why they are 3% of its optimizer. Its 52 s are
  the product's: 8,390 functions of 1.92 million unoptimized instructions.
- **No function stands out.** The costliest function of the checker's unit is
  1.5% of its optimizer's time (`tier1_select_cover_munch`, 0.77 s); a quarter
  of the time is in 50 functions, half in 320, three quarters in 1,264 of
  9,990. There is no one function to fix: the work is spread evenly, which is
  what parallel parts need.
- **The backend's and the front end's units still optimize their tests**, and
  the tests are nearly half of their optimizer's time.
- **The backend's unit is its tests' unit.** By what the product reaches from
  each unit's entry points, the product needs 76% of the checker's unit's
  unoptimized instructions, 47% of the front end's and 10% of the backend's:
  the backend's unit compiles its own copies of the code-generation files for
  its tests, and the compile path's copies are the checker's unit's.

## The levers, each measured

Each lever says what it does to the build, what it costs elsewhere, and what
evidence it needs if it changes Release's code: **byte identity** (Release's
executable is the same bytes before and after) or an **A/B on the 9700X**.

### 1. The optimizer in parts

**What it is.** A unit's compile stops before LLVM's optimizer. A splitter
deals the unit's functions into parts; each part's job runs the `-O3` pipeline
and the code generator on its part; the parts' objects are joined as #2058's
are. #2058 split after the optimizer, so the optimizer stayed one serial job;
this moves the split in front of it. The prototype is 1,200 lines of C derived
from `tools/codegen/split.c` (`prototype/` in the record, with its patch to
the build), built and measured through the real `./build.sh` on all eleven
`main.c` units and the eight longest benchmark units (48 parts for the
checker's unit, 32 for the backend's, 16 for the front end's, 8 for the
others). The compiler it builds passes the tests it was given (21 commands of
the correctness inventory) and writes `sqlite3.c`'s object byte for byte as
today's does.

**What a part sees of the others**, since each part is optimized alone:

- **Small functions' bodies.** A part reads, without emitting them, the bodies
  of the functions it calls that another part defines, up to a size (100
  unoptimized instructions in the build measurements, and every
  `always_inline` one), so the inliner still inlines them. Without this the
  compiler is 29% slower (below): the limit matters more than anything else
  in the design.
- **Single callers.** A static function that one function alone calls goes to
  its caller's part: the whole unit's inliner inlines such a function whatever
  its size.
- **Constants.** A part reads the initializers of the constants it refers to,
  so loads from tables still fold.

What a part still loses is what LLVM does across a whole module: it can no
longer prove that a static variable two parts share is never written, change
the signature of a function another part calls, or infer attributes from a
body it doesn't read.

**What makes an edit cheap.** A part's job reruns only when the part's bitcode
changed, so the split must give an unchanged function the same bytes:

- a function's part depends on its file's name and on the names of the
  functions near it (content-defined chunks), not on a count of what came
  before, so an edit moves no other function;
- clang numbers string literals and same-named statics in the order it meets
  them (`.str.1207`), which one new literal shifts for the rest of the unit:
  the split names them by their contents instead;
- an inline-assembly statement records its offset in the whole unit
  (`!srcloc`), which the split drops;
- debug information decides the rest. DWARF 5 records every source file's
  MD5, so with plain `-g` an edit changes every part that mentions the file.
  With DWARF 4 and line tables only, a part holds line numbers of its own
  functions alone (the bodies it reads come without theirs), and a file's
  functions stay in parts of that file's.

Checked on the checker's unit before the timed runs (a count, on the
development machine): an edit of `c_check_input.c` changes 1 of 25 parts'
bitcode without debug information, wherever in the file it is; with line
tables, 1 of 25 for an appended function and 4 or 5 for one that moves every
line of the file; with plain `-g`, all 25.

**The build, measured** through `./build.sh` with the prototype, at three
levels of debug information (the bodies-up-to-100 limit; one to three builds a
cell):

| Scenario | Today | Parts, `-g` as today | Parts, line tables | Parts, none |
| --- | ---: | ---: | ---: | ---: |
| Clean build | 83.1 s | 59.3 s | 54.7 s | 46.3 s |
| `c_check_input.c`, a function appended | 72.6 s | 27.3 s | 16.7 s (19.2) | 10.1 s (9.6) |
| `c_check_input.c`, every line moved | | | 17.0 s | 9.6 s |
| `differential.c` | 46.5 s | 10.7 s | 6.9 s | |
| `tier0.c` | 75.9 s | 33.2 s | 15.8 s | 10.0 s |
| `c_parser_impl.c` | 26.0 s | 8.1 s | 4.9 s | 3.9 s |
| `c_checker_driver_test.c` | 72.5 s | 19.6 s | 12.3 s | 9.6 s |
| `c_checker_test.c` | 75.7 s | 25.1 s | 12.3 s | |
| `host_isa.h`, a function added | 83.0 s | 22.2 s | 16.7 s | 17.0 s |

- **With `-g` as today** an edit recompiles every part whose debug
  information mentions the file: all 49 of the checker's unit for
  `c_check_input.c`, 17 for `c_checker_driver_test.c`, 5 of the backend's 33
  for `differential.c`. The rebuild is still 2.7 times faster than today's
  because the parts run at once: the checker's unit's front end 7.2 s, the
  split 4.1 s, its 49 parts 14.5 s on eight jobs (105.6 s of work; the longest
  5.4 s), the join 0.9 s, the link and the bake 0.6 s.
- **With line tables** (DWARF 4, a file's functions in its own parts, read
  bodies without theirs, single callers with their callers) the appended
  function recompiles one part and the line-moving edit five or six: front end
  6.6 s, split 3.5 s, the file's largest part 5.4 s, join, link and bake 1.2 s.
- **Without debug information** one part recompiles either way: front end
  5.4 s, split 2.3 s, the part 0.7 s to 1.3 s, join 0.8 s, link and bake 0.4 s.
  Three quarters of the rebuild is now clang's front end and the split reading
  its bitcode, which no part can shorten (lever 4).
- **The clean build** no longer waits for one chain: it is bound by its work
  on eight cores, which is today's with `-g` (497 s of CPU against 500 s). On
  15 jobs it takes 52.1 s (lever 8).
- **A rebuilt executable is the clean build's.** After eleven edits, the
  executable is byte for byte the one a clean build of the same sources makes
  (the same SHA-256).
- **Every front end again, no part changed** (the header's time alone
  changed, as after a branch switch that changes nothing a part holds):
  14.2 s. That is the floor of a Develop build that has its parts. The rest of
  a clean build's 54.7 s is the parts' jobs, which a cache of parts' objects
  keyed by their bitcode could supply to a fresh clone or a CI job (#2032's
  subject, not designed here).

**What real changes dirty** (a count on the development machine, not a
timing): replaying the last 24 commits to the sources through the split, a
commit changes a median of 3 of the checker's unit's 49 parts; 21 of the 24
change 7 or fewer, and two that change a shared structure change 31 and 46.
The last 13 merged pull requests change a median of 13.

**The compiler's speed, measured** as the 9700X job compares a pull request
with its base: four placements of the checker's unit (`./build.sh shifts`),
two fresh copies of each, `time-runs` on CPU 2, the baseline's executables
again as the floor. User cycles against today's build:

| Build | `-c sqlite3.c` | `-fsyntax-only main_unit_checker.c` | The clean build's CPU |
| --- | ---: | ---: | ---: |
| Today's again (the floor) | +0.02% (-0.24 to +0.48) | -0.03% (-0.55 to +0.26) | 1.00 |
| Parts, no bodies read | +29.3% | +35.7% | 0.84 |
| Parts, bodies up to 100 instructions | +3.3% | +3.0% | 1.01 |
| ... and single callers with their callers | +2.7% | +2.8% | 1.06 |
| ... and each file's functions in its own parts | +1.1% (+1.0 to +1.5) | +1.9% (+1.4 to +2.4) | 1.06 |
| Parts, bodies up to 300, single callers, by file | **+0.12% (-0.16 to +0.68)** | **+0.14% (-0.12 to +1.07)** | 1.24 |
| Parts, bodies up to 1,000, single callers, by file | +0.42% | +0.21% | 1.61 |

The ranges are over the placements. The last column is each variant's clean
build's CPU time against today's in the same round (all with `-g`, all beside
the stray process, which is why it is a ratio; measured again on the clear
host, the third and the sixth rows are 0.99 and 1.24). So the split costs the
compiler nothing measurable once a part can inline what the whole unit's
optimizer would have: at the 300 limit the two workloads are inside the floor.
They execute 0.2% to 0.3% more instructions, so a cost of that size is likely
real and under what cycles can tell. It costs the build instead: a quarter
more CPU than today's at the 300 limit, 6% more at the 100 limit with a
compiler 1% to 2% slower. The tests' run time doesn't notice (21 commands:
44.0 s against 43.7 s at the 100 limit).

**Release in parts**, at the 300 limit with `-g` as today: a clean build
77.5 s (619 s of CPU) and an edit of `c_check_input.c` 35.8 s (47 of its 49
parts), against 83.1 s and 72.6 s.

**Evidence.** As a configuration of its own, this lever changes nothing of
Release: byte identity of Release's executable is its gate. For Release
itself it is a code change, and the table is its A/B on two workloads; the
9700X job's whole measure would decide it.

### 2. Unoptimized tests in the units that still optimize them

#681 put the checker's unit's own tests between `#pragma clang optimize off`
and `on`. The other units still run `-O3` on theirs. Measured by marking the
test files' functions of the three largest units `optnone` and `noinline` on
clang's command line (`-mllvm -forceattrs-csv-path`, the same effect without
touching a source):

- **The units' compiles**, alone on CPU 2: the backend's 35.9 s to 20.9 s, the
  front end's 24.9 s to 15.2 s, the checker's 56.6 s to 55.5 s (its tests were
  already unoptimized).
- **Today's build:** the clean build's CPU falls 12% (500 s to 441 s). Its wall
  time hardly moves (80.8 s against 83.1 s): the checker's chain is still the
  longest. An edit of the backend's unit or the front end's follows its
  compile.
- **With the parts**, whose build is bound by its work: 59.3 s to 54.9 s with
  `-g`; the eleven units alone with line tables 40.0 s to 35.7 s.
- **The compiler's speed:** unchanged. `sqlite3.c`'s `-c` over four placements
  and two copies: -0.01% and +0.11% user cycles in two comparisons (where the
  base against itself reads -0.13% and +0.02%), and the same instruction count
  to one part in 100,000; `-fsyntax-only` of the checker's unit -0.06%.
- **The tests' run time**, which the same executable serves: 21 commands took
  45.7 s against 43.7 s (+4.6%; 46.0 s against 44.0 s in the first round). The
  tests whose own code lost `-O3` pay: `test-x86-encoder` 1.88 s to 2.29 s,
  `test-refinement src` 2.05 s to 2.88 s, `test-tier0` 2.30 s to 2.84 s.
  `test-checker`'s part didn't move (26.7 s): its tests were already
  unoptimized.
- **Tests compiled apart instead** (units of their own) could save a product
  edit no more than the tests' share above, nothing for the checker's unit,
  and would need 870 of that unit's product functions to have external
  linkage for the tests that call them. The parts already keep a test's edit
  from recompiling the product (12.3 s above).

**Evidence.** Release's code changes: 204 of the checker's unit's 3,182
emitted product functions come out as other bytes, a third of the backend's
and over half of the front end's, because what a function inlines can depend
on what the tests that share its helpers looked like. So this lever needs
what #681 had, an A/B on the 9700X; the comparison above is one for
`sqlite3.c` and the checker's unit. In the Develop configuration it needs
none.

### 3. Benchmark and tool objects: on demand, or sharing objects

Beside the eleven `main.c` units the build compiles 32 units of their own: 29
`*_benchmark.c`, `tier1_model_measure.c` and two test units. Compiled alone
they take 21.7 s on eight jobs and 120 s of CPU, a quarter of the clean
build's. An edit of a file one of them reads recompiles it beside the unit
(`c_parser_impl.c`: `c_parser_benchmark.c`, 9.7 s).

- **What they hold** (counted in every unit's unoptimized bitcode): 94.5% of
  their 1.81 million unoptimized instructions are functions a `main.c` unit
  also compiles (`c_parser_benchmark.c` 98%, `tier1_select_benchmark.c` 99%),
  and those copies are nine tenths of the two longest ones' optimizer and
  code generator time. The benchmarks' own code is 5%.
- **Who runs them.** The correctness inventory (`tools/ci/correctness.tests`)
  has no `benchmark-*` command. 29 of the 32 units serve those commands only:
  the 9700X job and a session that measures need them, an edit loop and a
  correctness run don't.
- **Built on demand:** today's clean build goes from 83.1 s to 79.2 s only
  (CPU 500 s to 366 s), since the checker's chain bounds it. With the parts it
  is the work that counts: 59.3 s to 44.6 s with `-g`, 54.7 s to 40.0 s with
  line tables, 46.3 s to 34.9 s without debug information. (The eleven units'
  objects alone: the link and the bake add 0.4 s.)
- **What it costs:** a compiler without its benchmark commands, which must say
  so instead of failing to link, and a second target for whoever wants them.
  **Evidence:** none for Release if Release keeps building them all, which the
  9700X job needs anyway: byte identity.
- **Sharing objects instead** (a benchmark calling the `main.c` units' copies
  of the code it times) would remove nine tenths of those 120 s for everyone,
  the 9700X job included. It isn't in the design: it changes what every
  benchmark measures (its own copy, inlined into its own loops, becomes the
  product's), and it gives product functions external linkage, which changes
  Release's code. Each benchmark would need its A/B and a new baseline.
- **Among the `main.c` units** a quarter of the work is further copies too
  (24% of their 5.56 million instructions; a product function is compiled 1.5
  times on average): the backend's unit's copies of the code-generation files
  above all. Who owns which copy is the units' ownership, which #2046 to #2051
  settled and this design doesn't reopen.

### 4. The units' sources split: front ends in parallel

After lever 1 an edit's fixed cost is the unit's front end and the split:
7.7 s of the 10.1 s without debug information, 10.1 s of the 16.7 s with line
tables. Both are serial and both follow the unit's size, not the edit's.

- **What the front end's time is** (the checker's unit, alone on CPU 2):
  parsing and semantic analysis of everything, 2.2 s (`-fsyntax-only`); of the
  declarations alone, every function body skipped, 0.9 s; generating and
  writing the unoptimized bitcode is the rest of the 5.4 s to 7.2 s.
- **What a split would give.** A unit divided along its files into eight
  pieces, each compiled apart, parses the declarations eight times and each
  body once. An edit then pays one piece: about 0.9 s and an eighth of the
  rest, 1.5 s to 1.7 s, and its split an eighth of 2.3 s to 3.5 s. That would
  take the edit of the checker's unit from 10.1 s to about 4 s without debug
  information and from 16.7 s to about 9 s with line tables. This is an
  estimate from the measured parts of the front end, not a measured build.
- **What it costs.** The pieces are optimized apart exactly as lever 1's parts
  are, so the compiler's speed is lever 1's measurement. The cost is in the
  sources: a `static` function called from another piece needs a declaration
  and external (hidden) linkage, and 3,813 of the checker's unit's 9,990
  functions are referred to from another of its 653 files. On eight jobs a
  clean build gains nothing: its work rises by the repeated declarations
  (seven more parses of 0.9 s for the checker's unit).
- **Evidence:** an A/B, as for Release in parts.

It is the next step after lever 1, not the first: it takes a large source
change to save 6 to 8 s of a rebuild that lever 1 has already taken from
72.6 s to 10 to 17 s.

### 5. Other configurations: less debug information, `-O1`, Debug

Today's build structure at other settings, and the parts beside them. The
compiler's speed is user cycles against Release, one placement, seven runs of
`-c sqlite3.c` and five of `-fsyntax-only` of the checker's unit (the parts'
row is lever 1's comparison over four placements); the tests are the same 21
commands, once.

| Configuration | Clean build | Edit of `c_check_input.c` | `-c sqlite3.c` | `-fsyntax-only` | The tests |
| --- | ---: | ---: | ---: | ---: | ---: |
| Release (`-O3 -g`) | 83.1 s | 72.6 s | | | 43.7 s |
| Release, line tables in the `main.c` units | 72.0 s | 63.7 s | | | |
| Release, no debug information | 66.5 s | 59.3 s | | | |
| `-O1`, no debug information | 43.1 s | 38.4 s | +6.4% | +6.9% | 44.6 s |
| Debug (`-O0 -g`) | 13.5 s | 11.7 s | +295% | +242% | 85.6 s |
| Parts (`-O3`), line tables | 54.7 s | 16.7 s | +1.1% | +1.9% | 44.0 s |
| Parts (`-O3`), no debug information | 46.3 s | 10.1 s | | | |

- **Debug information is a quarter of the build's CPU.** Without it the clean
  build takes 373 s of CPU against 500 s; the checker's unit's nine backend
  parts take 37.9 s against 63.4 s, and its compile to bitcode 51.0 s against
  56.6 s. Line tables in the `main.c` units keep most of that: 403 s.
- **It is not quite the same code.** The front end's unit compiled three ways
  (a count on the development machine, clang 23.1.6): with line tables and
  with no debug information its `.text` is the same bytes, and 64 bytes longer
  than with `-g`; the data and the read-only data are identical in all three.
  So even less debug information in Release needs an A/B, not a claim of
  identity.
- **`-O1` without `-g`** takes a third off Release without debug information,
  for a compiler 6% to 7% slower and tests that take the same time. The parts
  at `-O3` beat it where it matters: their edit is 2.3 to 3.8 times shorter
  than `-O1`'s and their compiler 1% to 2% slower than Release, not 6%. `-O1`
  in the parts' jobs shortens the clean build further (the eleven units
  without debug information: 25.6 s against 34.9 s) and no edit, whose time is
  the front end's.
- **Debug** is the fastest of today's builds and the slowest compiler: the 21
  commands take 85.6 s against 43.7 s (`test-lexer` 11.4 s against 3.1 s), so
  an edit loop that runs tests gives back in the tests what it saved in the
  build. It stays what it is for: a debugger.

**Evidence.** `-O1` and Debug aren't Release. Less debug information in
Release is a code change, a small one: A/B.

### 6. The compiler building itself

Stage 1 (the Release compiler) compiling the build's own units with the
build's flags, less `-O3`, `-Wall` and `-Werror`, as the self-host gate does:

| | Default tier | Tier 1 (`--tier1`) | clang today |
| --- | ---: | ---: | ---: |
| All 43 units, eight at once | 5.05 s (35.5 s of CPU) | 6.03 s (56.4 s) | 83.1 s (500 s) |
| The checker's unit alone | 1.75 s | | 56.6 s to bitcode, 72.6 s to the executable |
| The link | 0.07 s | 0.06 s | 0.1 s |
| The compiler it makes: `-c sqlite3.c` | 1,047.9 ms (8.2 times) | 595.3 ms (4.7 times) | 127.5 ms |
| ... and the 21 test commands | 129.9 s (3.0 times) | 90.4 s (2.1 times), one failing | 43.7 s |
| With the checker's unit alone self-built: `-c sqlite3.c` | 694.4 ms (5.4 times) | 436.4 ms (3.4 times) | |
| ... and the 21 test commands | 54.2 s (1.24 times) | 50.4 s (1.15 times) | |

- **It builds 14 to 16 times faster than clang and makes a compiler 5 to 8
  times slower.** All five executables write the same `sqlite3.o`.
- **One unit self-built, the rest clang's** (the build's own link with one
  object replaced) is the fastest edit loop measured: 1.75 s and a link for
  the checker's unit. Both hybrids pass the 21 commands; only the commands
  that run the checker's unit's code slow down (`test-checker`'s part 33.8 s
  against 26.7 s, `test-check-units` 5.8 s against 2.6 s).
- **The compiler built wholly at Tier 1 fails `test-tier0`:** 1,824 of its
  14.6 million checks, all in the two unsigned-floating groups, where the
  floating-point flags of the test's own C conversion, which Tier 1 compiled
  there, differ from the emitted code's. The default tier's passes all 21.
- **What must be fixed first.** That failure, for any build that is Tier 1
  throughout. The code quality of the compiler's own tiers, which decides how
  much slower a hybrid's tests run, is #1344's, #1398's and #138's. For the
  build: the unit must be compiled by a stage 1 that clang built and that
  passed its tests, not by the hybrid under edit, or a wrong edit compiles the
  next one; the hybrid's warnings are stage 1's, not clang's (`-Wall -Werror`
  is clang's gate and stays in the clang build); and a hybrid's failure can be
  the tier's and not the edit's, so a failing test is repeated on the
  clang-built compiler before it is believed.

**Evidence.** Not Release. Its own gate is the self-host gate's: the hybrid's
outputs are the clang-built compiler's, byte for byte.

### 7. A faster clang

Measured only with what the host already has: the official LLVM release
(23.1.4) that CI stages in its cache, whose `clang` is one statically linked
executable that carries BOLT's sections, against the distribution's clang
(23.1.1, linked to a shared libLLVM), which a session's `./build.sh` finds on
`PATH`. No clang was built or trained here: a clang with a profile of buster's
own units would mean building LLVM on the host, which is the owner's call.

| Compile, alone on CPU 2 | Host's clang 23.1.1 | CI's official 23.1.4 |
| --- | ---: | ---: |
| The checker's unit, to bitcode | 56.6 s | 45.4 s (-20%) |
| The backend's unit, to bitcode | 35.9 s | 28.2 s (-22%) |
| The front end's unit, to its object | 24.9 s | 20.6 s (-17%) |
| `c_parser_benchmark.c` | 9.4 s | 7.7 s (-18%) |

| Build | Host's clang | CI's clang |
| --- | ---: | ---: |
| Today, clean | 83.1 s | 68.9 s |
| Today, `c_check_input.c` | 72.6 s | 59.7 s |
| Parts with line tables, clean | 54.7 s | 44.3 s |
| Parts with line tables, `c_check_input.c` | 16.7 s | 15.0 s |

- **The builds in the second table are a mixed toolchain.** CI's staged tree
  has no `llvm-nm`, no `llvm-objcopy` and no LLVM library the splitter can
  link, so in the measurement clone the splitter was built against the host's
  LLVM 23.1.1 and the build's check that those tools are the compiler's
  version was relaxed to its major and minor version (`prototype/mixed.py`).
  clang 23.1.4 compiled every unit and every part.
- **With CI's tree as it is, the build has no parts at all.** Its configure
  step says `parallel native generation is off: no llvm-nm 23.1.4,
  llvm-objcopy 23.1.4, LLVM libraries llvm-config can link, shared or static`,
  and a clean build takes 97.1 s, the checker's unit alone 96.7 s, and an edit
  of `c_check_input.c` 87.8 s: 28 s longer than the same clang takes with the
  parts. This was measured with CI's environment (the staged tree first on
  `PATH`, its resource directory and library path), not read from a CI job's
  log. `tools/ci/setup-benchpress.sh` now stages the two tools, not the
  libraries; the hosted install stages `libLLVM*.a`.

**What it costs.** Nothing is fetched: CI's job already builds with it, so a
session that uses it builds what CI builds, and a session that doesn't builds
with another compiler than the one whose code CI measures. The staged tree is
CI's to replace; a session must only read it.

**Evidence.** CI's Release is unchanged: byte identity. A session's Release
becomes CI's.

### 8. More jobs

`./build.sh` runs one compile a core, because two compiles on one core's
threads slow each other and today's build waits for its longest (#270). With
the parts the build waits for its work, and the second threads help: the clean
build takes 59.3 s on eight jobs and 52.1 s on 15, for 47% more CPU time. The
eleven units alone with unoptimized tests gain less, 35.7 s to 33.4 s: there
the checker's unit's front end, split and longest part are a chain of their
own again. Under the 9700X's lock nobody else pays for those threads.

**Evidence.** Byte identity: the executable doesn't depend on the jobs.

### The levers side by side

| Lever | Edit of the checker's unit | Clean build | What it costs | Release's code |
| --- | ---: | ---: | --- | --- |
| Today | 72.6 s | 83.1 s | | |
| 1. Parts, as Develop (line tables) | 16.7 s | 54.7 s | Develop's compiler 1.1% to 1.9% slower; lines, not variables, in its debug information | unchanged: byte identity |
| 1. Parts, in Release (`-g`, the 300 limit) | 35.8 s | 77.5 s | a quarter more build CPU | changes: A/B, +0.12% and +0.14% here |
| 2. Tests unoptimized everywhere | as today | 80.8 s (CPU -12%); 54.9 s from 59.3 s with parts | the tests 4.6% slower | changes: A/B, -0.01% to +0.11% here |
| 3. Benchmark units on demand | as today | 79.2 s; 40.0 s from 54.7 s with parts | benchmark commands need a second target | unchanged: byte identity |
| 4. Sources split | about 9 s from 16.7 s (an estimate) | none | 3,813 functions' linkage in the checker's unit | changes: A/B |
| 5. Line tables, or no debug information | 63.7 s, 59.3 s | 72.0 s, 66.5 s | less to debug with | changes a little: A/B |
| 5. `-O1`, no `-g` | 38.4 s | 43.1 s | compiler 6% to 7% slower | not Release |
| 5. Debug | 11.7 s | 13.5 s | compiler 3.4 to 4 times slower, tests twice | not Release |
| 6. One unit self-built | about 2 s | 5.1 s for all | compile path 3.4 to 5.4 times slower; rests on the tiers' code | not Release |
| 7. CI's clang | 59.7 s | 68.9 s | a session reads CI's staged tree | CI's: unchanged |
| 8. 15 jobs | as today | as today; 52.1 s from 59.3 s with parts | 47% more CPU | unchanged: byte identity |

## The design

### The structure

Lever 1 does most of what can be done, and it can be had without touching the
product. So the design is a third configuration built in parts, and Release
left alone until the owner decides otherwise with the full measure in hand.

```text
unit.c --clang, front end only--> unit.bc --splitter--> part0.bc ... partN.bc, data.bc
                                                        (a part is rewritten only when it changed)
partK.bc --clang -O3 -x ir--> partK.o                   (one job a part)
part*.o --join, as #2058's--> unit.o --link--> compiler.link --bake--> compiler
```

| | Release | Develop (new) | Debug |
| --- | --- | --- | --- |
| For | the product; every timing | edits, tests, correctness work | a debugger |
| Units | whole, `-O3`, as today | in parts before the optimizer, `-O3` | whole, `-O0` |
| Debug information | `-g` | DWARF 4 line tables | `-g` |
| The tests' own functions | as today | unoptimized | `-O0` |
| Benchmark units | built | on demand | built |
| Its code | today's, byte for byte | 1.1% to 1.9% slower on the compile path | 3.4 to 4 times slower |

The name is the implementation's to choose. `./build.sh` keeps building
Release by default until the owner changes that; a session asks for Develop as
it asks for Debug today. Sanitizer, fuzzing and profile-guided builds keep the
whole-unit compile, as they do with #2058.

### The splitter's rules

What the implementation must keep of the prototype, each for a measured
reason:

1. **A function's part comes from names, not counts.** Each source file's
   functions go to a run of parts of the file's own, as many as its share of
   the unit; within the file, a function whose name's hash is 0 modulo an
   anchor starts a chunk, and the chunk's first name selects the part. An edit
   then moves no other function, and an edit that moves a file's lines
   changes that file's parts alone (5 or 6 of 49 for `c_check_input.c`).
2. **A single-caller static function goes with its caller**, until the
   caller's tree is 150,000 instructions. With rule 1 it takes the compiler's
   cost on `sqlite3.c` from +3.3% to +1.1%.
3. **A part reads the bodies it may inline**: every callee of at most the
   limit (100 unoptimized instructions in Develop) and every `always_inline`
   one, through those bodies in turn, `available_externally` and without
   their debug information. It takes the cost from +29% to +3.3%.
4. **A part reads the constants it refers to**, as #2058's parts do, and a
   static variable more than one part uses lives once, in the data part.
5. **Names don't depend on the rest of the unit**: string literals and
   same-named statics by their contents, no `!srcloc`, DWARF 4 so that no
   file's checksum is in a part.
6. **A function whose inline assembly has text stays in the first part and
   nobody reads its body**: such text can define a label another function's
   text names (`x86_semantics_test_return`, which failed to link until then).
7. **The same input gives the same parts**, whatever the jobs and the order.
   The gate is that a rebuilt executable equals the clean one.
8. **A part is about 50,000 unoptimized instructions** (48 parts for the
   checker's unit). Its job then takes a median of 2 s and at most 5 to 10 s.
   The count wasn't swept: 48 is the one measured.

### Expected times

On eight jobs on the 9700X, every figure measured on the prototype:

- **Conservative:** full `-g` as today, so an edit recompiles every part
  whose debug information mentions the file (every part of the checker's unit
  for `c_check_input.c`). It is also what an edit of a body every part reads,
  or of a structure every function uses, does in Develop. The benchmark units
  are built and the tests optimized.
- **Expected:** Develop as designed, built by the host's clang: line tables,
  an edit that changes what the measured edits changed (one part for a new
  function, five or six for one that moves its file's lines), and for the
  clean build the benchmark units on demand and the tests unoptimized.
- **Optimistic:** the same built by CI's clang (lever 7's mixed toolchain).

| Scenario | Today | Conservative | Expected | Optimistic |
| --- | ---: | ---: | ---: | ---: |
| Clean build | 83.1 s | 59.3 s | 36 s | 30 s |
| Nothing changed | 0.03 s | 0.04 s | 0.04 s | 0.04 s |
| `c_check_input.c` | 72.6 s | 27.3 s | 16.7 s (to 19.2) | 15.0 s |
| `differential.c` | 46.5 s | 10.7 s | 6.9 s | 5.8 s |
| `tier0.c` | 75.9 s | 33.2 s | 15.8 s | 13.6 s |
| `c_parser_impl.c` | 26.0 s | 8.1 s | 4.9 s | 3.9 s |
| `c_checker_driver_test.c` | 72.5 s | 19.6 s | 12.3 s | 10.7 s |
| `c_checker_test.c` | 75.7 s | 25.1 s | 12.3 s | 10.7 s |
| `host_isa.h` | 83.0 s | 22.2 s | 16.7 s | 14.1 s |

The expected and the optimistic clean builds are the eleven units' 35.7 s and
29.3 s and the link's and the bake's 0.4 s; with the benchmark units they are
54.7 s and 44.3 s. On 15 jobs they are 33.4 s and 27.4 s. The edits' rows were
measured with the benchmark units built. Without debug information the
expected edits of the first, third and fourth files are 10.1 s, 10.0 s and
3.9 s.

### Steps, each with its gate

1. **The splitter and the Develop configuration.** `tools/codegen/` gains the
   splitter (C, with LLVM's C API and the one C++ file `split.c` already has
   for what the C API lacks); CMake gains the configuration; Release's and
   Debug's commands don't change.
   - *Identity:* Release's and Debug's executables are the same bytes before
     and after the change.
   - *Determinism:* after the record's series of edits, Develop's rebuilt
     executable is the clean build's (the same SHA-256), checked in hosted CI
     without a timing.
   - *Correctness:* every row of the correctness inventory passes on Develop's
     executable, and it writes the objects Release's writes.
   - *Time*, on the 9700X on eight jobs: `c_check_input.c` at most 20 s,
     clean build at most 60 s, nothing changed at most 0.1 s.
2. **The benchmark units on demand in Develop.** Its executable answers a
   `benchmark-*` command by naming the configuration that has it.
   - *Identity:* Release's executable and the 9700X job's inputs unchanged.
   - *Time:* Develop's clean build at most 45 s.
3. **The tests' own functions unoptimized in Develop**, marked by the
   splitter from each function's file, so no source changes.
   - *Correctness:* the inventory passes. *Time:* the 21 commands take at
     most 10% longer than Release's; the clean build at most 40 s.
   - The same for Release is #681's change for the other units: its own pull
     request with its A/B from the 9700X job.
4. **The guides.** `docs/commands/build.md` gains the configuration; AGENTS.md
   says which configuration suits which job (Develop where edits dominate,
   Release for every timing, Debug for a debugger). Whether sessions build
   with CI's clang, and whether Develop becomes the default, are the owner's.
5. **Release in parts**, if the owner wants its edit loop and accepts a
   quarter more build CPU: the 300 limit, single callers, by file.
   - *A/B:* the 9700X job's whole measure, every benchmark over its
     placements, inside its floor. The two workloads measured here are.
   - Without this step Release's clean build stays bound by the checker's
     unit's chain: 83 s by the host's clang, 69 s by CI's once CI's toolchain
     can build the parts.

Not steps of this design, and why: one unit self-built (lever 6) waits for
the tiers' code, which is others' work; the sources' split (lever 4) is a
large change for the 6 to 8 s lever 1 leaves; a cache of parts' objects for
fresh clones and CI jobs is #2032's subject, which the parts make possible
because a part's object depends on nothing but its bitcode and its command.

### What would refute it

- **Real edits dirty many parts.** The replay says a commit changes a median
  of 3 of 49 parts. If, in use, the median rebuild of the checker's unit
  takes more than 25 s (the conservative case is 27.3 s), the placement rules
  are wrong for this code and the expected column doesn't hold.
- **A rebuilt executable differs from the clean one** and no rule fixes the
  class. One class turned up and was fixed (inline assembly's labels).
- **Develop's compiler disagrees with Release's** on a test or an object, and
  the cause is the split's semantics and not a bug in the splitter.
- **The front end and the split grow with the unit** until the fixed 8 to
  10 s is most of every rebuild. Then lever 4 or lever 6 becomes the first
  step, not a later one.
- **For step 5:** any gated benchmark outside its floor at the 300 limit.
  Then Release stays whole, and the design is Develop alone.

### Risks

- **A splitter's bug is a miscompile**, in Develop alone. The correctness
  rows on Develop and the comparison of its outputs with Release's are the
  guard; Release is what ships and what is measured.
- **LLVM's interfaces.** The splitter clones modules through LLVM's C++ API,
  as `split_bitcode.cpp` already writes bitcode through it. A new LLVM can
  break it; the build then compiles the units whole, as #2058's `auto` does.
  LLVM's own ThinLTO wasn't measured: it optimizes modules in parallel, not
  the parts of one, so it needs the same split first; its importer and cache
  could then stand in for rules 2 to 4, which is the implementation's choice.
- **Debug information.** Develop's has lines, not variables or types: `perf`
  and backtraces work, a debugger's `print` doesn't. Debug remains for that.
- **The worst edit is the conservative case**, not today's: a body every part
  reads, or a structure every function uses, recompiles every part (27.3 s
  for the checker's unit).
- **Hosted runners weren't measured.** With two to four cores the parts don't
  shorten a clean build, which is bound by its work there; only steps 2 and 3
  cut that work.
- **Not measured:** other part counts than 48; the parts' memory with eight
  or fifteen jobs at once; the build directory's size.

### What this design leaves to others

- **#2053** (fixtures as data): once a fixture is a file the tests read, an
  edit of it builds nothing, and the test-file rows above apply to the tests'
  logic alone. **#2032** (reuse between fresh jobs): see the parts' cache
  above. Both are counted in and not redesigned.
- **#1344, #1398 and #138** own the code quality of the compiler's own tiers;
  lever 6 measures what a self-build costs today, and found the Tier 1
  compiler's `test-tier0` failure, which is reported with this design.
- **#2524 and #672** own the compile path's throughput.
- **CI's toolchain on the 9700X** (lever 7) is CI's: staging what the
  splitter links there would give its Release build #2058's parts.
- **#2040's harness:** the scenarios here are plain scripts in the record
  (`scripts/run.sh`), for the versioned harness and its hosted `ninja -n`
  checks to reuse; those stay unclaimed.
