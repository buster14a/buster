# The one-core path: 73 cycles a token, on records the vector lanes can use (#2850): design

Status: design, with its first two steps, step 2's first three pieces and
its tests' loop on the window, the first part of step 3 and steps 4's and
5's censuses measured, step 2's ceiling in a compile, and the first four
pieces of the redesign that ceiling led to: the body boundary, measured on a
scaffold, the machine with no node of the walk's, and a body's entries from its
controls and its tapes after the pipeline and then before the walk, all three
in the shadow.
Of all this, only what step 5's allocation kept as the kernel's own is in a
compile: three changes to those rows, merged.
[#2850](https://github.com/buster14a/buster/issues/2850) has the design whole:
the budget, the data design and what would refute it. It is
[#672](https://github.com/buster14a/buster/issues/672)'s per-core research
target's design, beside [the node machine](node-machine.md) (#2524), which it
builds on, and [the all-core path](all-core-path.md) (#2536), whose windows it
uses. This page keeps the summary and each step's result; the measurements are
in buster-performance, linked at each step.

**In one paragraph.** 1 GB/s on one core is 52 M cycles for `-c sqlite3.c`: 73
a token, where main takes 952 and tcc 307. The design gives the compile's
middle (`check-input` to `lower`) 18 of them, about 38 a body node, and gets
there by making what a rule decides on a byte its producer wrote: T's, C's and
X's rules as arithmetic over byte columns, 64 records to a register, and the
leaves and lookups, which no lane helps, from words that are one load away.
Step 0 met its 20-cycle gate on the dumped nodes with leaves and lookups given.
Producing the packed template key in T and repairing distant operand lanes
before X gave 13.76 cycles per taken SQLite node and 18.05 on musl on repeated
passes, or 18.29 and 19.37 after scalar work. Step 1
measured the leaves and lookups: every record is T's, a leaf takes about 5
cycles, and a lookup takes 18 to 63 by its operator, 25 to 46 on average,
where the gate was 10 and the design's own refutation line 20. T is bound by
its lookups as its rules are today. What a lookup's record costs is its rule's
hundred instructions and the record's width, not its loads. The follow-up
measured both ends of that: a lookup that makes only its record's first half
takes 21 to 45 cycles by its operator, and its load alone 12 to 16. The rest
is the lookup's facts, which the design gives the lanes. Step 2's first piece
made the rest of T's records from the same words, a loop a class: exact, and
T whole at 38 cycles an evaluated node on `sqlite3.c` (32 with lean records)
where the shadow's own T takes 89.6 and the piece's gate was 25. An
operator's whole record costs 56 cycles made that way, where step 0's lanes
made its fact bytes for 12. Its second piece counted what of a record
anybody reads, by changing each field and running its readers again. Of the
operators that are no lookup, 84% on `sqlite3.c`, 82% on musl and 59% on Lua
have no wide field read: their bytes are the whole record. A tenth need a
copy a selector names. 5% and 6% need a value made at the record, and 24% of
Lua's, every one a constant's arithmetic. C and X decide on a type's
identity for one thing, a callee's parameters, at every call. Its third
piece ran the kernel at reader time, where the machine sits, with each
window's inputs made from the lean front's records and the reader's columns
alone: exact, and every input a compile needs is there. The kernel makes a
record in 16.4 TSC ticks where T takes 49.2 on the same expressions. Making
its inputs as a copy of the front's records costs 28.4 more, a record at a
time or by passes, so T by the kernel is 44.8 ticks a record where the
piece's gate was 35. The front holds those values when it makes a record, so
the piece after it has the front write the window as it makes each record,
and the kernel read it there: 29.9 ticks a record on `sqlite3.c` with its
inputs, 14.4 of them the inputs where the copy took 30.4, and every
expression T takes read in place. On musl it costs more than the copy: the
front writes for every record it makes, and a small unit's names are first
uses. The set's and the silence tests, which decide what the machine takes,
then run on that window, their loop 64 records a step over bytes read out of
the kernel's records: exact, and 55 ticks a record on `sqlite3.c` where the
tests as a pass over T's columns read 87. The loop itself is 0.7 of them,
and 38 are the tests that read T's records still. What a compile gains for
all of it has a ceiling, read on main with each taken expression replaced
by one leaf: 31 ms of `sqlite3.c`'s 112 with every whole tape taken, 11 with
those that hold no call, where the machine itself costs about 10; and with
no taken expression left the stages from `check-input` to `lower` are still
57 ms. So the redesign takes whole bodies outside the reader, and its first
piece is the boundary where such a body comes back: its lowered function
supplied at the lowering, its tokens masked from the reader. With a recording
of an ordinary compile as the supplier it is exact by default flags on four
corpora, and `sqlite3.c` with 2,589 of its 2,668 bodies supplied compiles in
64.9 ms where the ordinary compile takes 110.9. The piece's gate was 56: the
host alone, with every body free, costs about that.
Step 4's census counted Tier 0's row: 117 cycles
and 295 instructions a typed entry in `-c sqlite3.c`, 64.7 cycles a token,
Tier 0 itself 38 of them and the unwind table, the atoms and the emission's
own driver 17. A key of eight bytes or fewer decides the records of 99.8% of
three corpora's entries with no conflict, from tables of thousands of rows.
The encoder, branch sizing and the object are 8.3 cycles a token in a compile
and 6.3 at their own benchmarks' rates: under the census's gate of 10, and
most of the row's allocation of 10 before Tier 0 runs. Step 5's census
counted the kernel's 121 cycles a token by cause: 82% page faults, 58% the
clearing of fresh 2 MB pages alone. What a compile would still take from the
kernel with every arena reused or windowed is 5.8 to 6.4 cycles a token at
warm-cache costs and 10.3 to 12.5 at the costs the compile pays, and beside
it 5.2 to 6.7 more that no arena removes (the include search, the
executable's own pages, the arenas' calls): the budget's 6 for the kernel
doesn't hold. Step 5's allocation gave every row of that census, and of one
of musl's units, to the issue that removes it, and kept 12.1 cycles a token
as the kernel's own on `sqlite3.c`, of a row of 14.3 to 15.0. Three changes
then took what they could of the kept rows: the executable's code in 2 MB
pages with a start order from real compiles' traces, the arenas' calls
halved, and a search directory asked once for a subdirectory it lacks. The
row stands at about 12 cycles a token, and a unit of musl's compiled as its
own process takes 817 kernel cycles a token where it took 1,034.

## The budget

`-c` of the frozen `sqlite3.c` (9,507,037 bytes, 716,136 preprocessed tokens,
266,707 evaluated nodes in 2,674 bodies) on the 9700X at 5.49 GHz, cycles a
token; the issue has each row's source:

| | today | best measured | has to be |
| --- | ---: | ---: | ---: |
| read, lex, intern, directives, expansion | 60 | 52 | 23 |
| parse | 33 | 17 | 12 |
| `check-input` to `lower` | 682 | 37 | 18 |
| Tier 0 to the code's atoms, the object and the rest | 78 | 78 | 10 |
| the kernel and the process's fixed costs | 120 | 120 | 6 |
| refused code, on today's pipeline | | | 4 |
| **total** | **973** | **about 300** | **73** |

## The steps and their gates

| step | what | gate | state |
| --- | --- | --- | --- |
| 0 | The middle's passes on the real nodes, outside the compiler | every entry the stream's; 20 cycles a taken node or less; 0.2 mispredicted branches a node or fewer | met on the frozen SQLite and musl streams: 13.76 and 18.05 cycles on repeated passes, 18.29 and 19.37 after scalar work, with the largest of the 20 round means at 19.59 |
| 1 | The fact words, in the shadow: T's leaves and lookups from one load each | every record the oracle's; 10 cycles a leaf or lookup or less, a body's expressions a run | measured: exact; 15.8 to 26.5 cycles; a leaf about 5, a lookup 25 to 46. Followed up: a lean record 14.1 to 20.8, the loads alone 8.8 to 15.2. On the later call-argument rules, completing a call in one visit gave 27.00 on SQLite in lean mode. |
| 2 | The machine on these records where it runs (#2524's steps 2 to 4) | the bodies' middle at 38 cycles a node or less in `-c sqlite3.c` | first piece measured, in the shadow: T whole from the fact words, exact; 38 cycles an evaluated node with whole records and 32 with lean ones, where its own gate was 25. Second piece counted: of the operators that are no lookup, 5.4%, 24.2% and 6.1% need a value made at the record (`sqlite3.c`, Lua, musl; its line was 15%), and C decides on a type's identity for one question of a call's callee. Third piece measured: the kernel at reader time, its inputs from the front's records, exact; 16.4 TSC ticks a record where T again takes 49.2, and 44.8 with its inputs' making, where its gate was 35. The tests on the window measured: exact; 54.1 TSC ticks a record on `sqlite3.c` where their pass over T's columns reads 83.0 (gate 40, refuted at 65): the loop 0.7, its inputs 8.9, and 37.2 the tests that read T's records still. Their second half's first piece: an operator a constant sends for nothing is left out of the loop's list (20,898 of 48,645): 48.7. Its second: the tree's tests listed by the operator above (6 operators for 27,794 records), and an expression with nothing listed has its verdict from the window alone (21,157 of 50,993): 42.0. Its third: a call with no test to make is left out of the list (12,064 of 12,918) and a statement that discards nothing is not listed (35,069 expressions with nothing listed): 35.8. Its fourth: a pointer's cast to void or to a pointer, its subscript by a constant and its sum with one are left out of the list (9,744 of 15,688 listed operators; 41,456 expressions with nothing listed): 31.2. C on the window measured: C's templates, contexts and targets for the expressions with no call, logical operator, update or comma (63% of `sqlite3.c`'s placed expressions), exact; 12.6 TSC ticks a record of theirs where C on T's columns reads 40.2, and 13.9 with its bytes, where its gate was 10. A call on the window measured: the kernel makes each argument's conversion (every one for 99.6%, 98.8% and 98.0% of the calls) and C's answers for a call are made on the window from them, exact; 86%, 84% and 86% of the placed expressions are decided there, at 8.2 TSC ticks a record of theirs on `sqlite3.c` and 9.2 with its bytes (gate 10); the kernel in place reads 1.2 ticks a record more, where its gate was 1.0. Its ceiling in a compile, measured: with each whole tape's expression replaced by one leaf the middle falls from 87.6 ms to 56.7 on `sqlite3.c` (to 60.2 with the tapes the tests decide on the window alone, to 76.3 with those that hold no call, label or temporary), against about 10 ms for the machine: step 2 whole is about 1.23 times, and a first compile that takes no call gains nothing |
| the redesign, 1 | The body boundary: a body's lowered function from outside the reader (#2524's step 3's boundary) | exact; `-c sqlite3.c` in 56 ms with its bodies supplied (refuted at 62); nothing moves with none supplied | measured on a scaffold, with a recording as the supplier: exact on `sqlite3.c`, Lua, `main.c` and musl, with the default warnings and with `-Wall -Wextra`, masked whole and with the reader's resolution kept for a supplied body; 64.9 ms with 84% of the bodies' tokens supplied and masked, a line that reaches 56.5 at 100%: refuted; 73.6 ms with their resolution kept, so a taken body token is worth 295 TSC ticks; 1,215.957 M and 1,215.946 M instructions with none supplied. Its gate on a masked unit's reader is merged (#3272), and the unit-end warnings' fix its runs found (#3274) |
| the redesign, 2 | The machine with no node of the walk's: the reference's sweeps on the front's records alone, a body at a time before the walk acts at it | every root's results the ones the machine makes of the walk's nodes; no root taken before a warning of the walk's; a body's verdict before the walk; 180 ticks a body token or fewer (refuted at 295) | measured, in the shadow: exact on the four corpora (159,318 roots in 31,039 bodies, with `-g` and with `-Wall -Wextra` too), none taken before an event of the walk's or with a token its actions left; a body's every root taken in 35.6% of `sqlite3.c`'s body tokens, 29.7% of Lua's, 31.8% of musl's; 270 ticks a body token on `sqlite3.c`, 216 on Lua, 157 on musl: over its gate, under what refutes it |
| the redesign, 3 | A body's entries from the program's controls and its roots' tapes | every attempted body's entries the lowered function's, kind, class and value | measured, in the shadow: exact on the four corpora with the default warnings, with `-g` and with `-Wall -Wextra` (12,768 bodies); 44.35% of `sqlite3.c`'s body tokens attempted, where the machine after the pipeline takes every root of 48.58% |
| the redesign, 4 | The same comparison before the walk: the entries from the tapes kept there, and the controls from the tokens | no attempted body differs, in either | measured, in the shadow: exact on the four corpora in the three flag sets (11,145 bodies from the kept tapes, 11,120 from the twin); 32.79% and 32.18% of `sqlite3.c`'s body tokens attempted, of the 35.62% whole before the walk; no body attempted where the program folded an `if` |
| 3 | The front's row, into the window | 35 cycles a token or less | first part measured, in the shadow: the node machine's front writes the kernel's window and the kernel reads it in place, exact; the kernel with its inputs 29.9 TSC ticks a record on `sqlite3.c` (gate 35), 38.4 on Lua, 61.5 on musl. The lexer's and the parser's rows are not started |
| 4 | Tier 0's row | 10 cycles a token or less, the object's bytes unchanged | census measured: the row 64.7 cycles a token (117 a typed entry), Tier 0 38.4 of them; a key of 8 bytes or fewer decides 99.83% of the entries; the encoder, branch sizing and the object 8.3 in a compile, 6.3 at their own rates. Designed (#2874): a body's row at 14 cycles a typed entry. Its first piece, Tier 0's back half on byte templates, measured in the shadow: exact, and refuted on its cost (62.8 cycles an entry where today's back half takes 43.7). Its second, in the compile: a fixed frame's unwind descriptor from Tier 0's frame, the objects unchanged, 403 cycles a body where main's frame events take 1,512 (its gate 300), 4.0 cycles a token less on `sqlite3.c`. Its third, in the compile: a call's records from a memo of the thread's, the objects unchanged, `tier0` 0.26 ms less on `sqlite3.c` (its gate 0.30); the VEX scan without gathers, 0.15 ms less; and the deferred entries in loops of their own, 0.33 ms less, and emission's after its blocks, 0.16 ms less; a body's columns in a few pushes, 0.05 ms less; fusion's and the labels' lanes in lists, 0.03 ms less |
| 5 | Fresh memory and the fixed costs | 6 cycles a token or less | census measured: the kernel 121 cycles a token; its floor 5.8 to 6.4 at item 3's costs, 10.3 to 12.5 at the compile's, and 5.2 to 6.7 more beside it: the 6 refuted. Allocated: 14.3 to 15.0 on `sqlite3.c` and 25.0 on musl as one command, each row to the issue that removes it or kept. The kept rows optimized in the compile since (#2885, #2903, #2941): the row about 12 on `sqlite3.c`. Whether the budget's line moves to it is not settled |

## Step 0: the lanes on the real nodes

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/per-core-step0-2850-2026-11-15)
(`98c03367a`). A prototype outside the compiler ran T, C, the template's lookup
and X over a dump of every evaluated node of `sqlite3.c` and musl, on byte
columns with operands as byte distances, in blocks of 64 records, with the
leaves' and the lookups' facts given. Its first path, an expression at a time,
is its twin.

- Every entry is the stream's (346,998 and 152,052 entries, over poisoned
  arrays).
- Cycles a taken node, `sqlite3.c` and musl: the first path 110 and 116; the
  lanes with their rules read from learned lists 23.5 and 29.4; with the rules
  baked as straight code 16.7 and 20.6, 57 and 68 instructions, 0.025 and 0.045
  mispredicted branches.
- By part on `sqlite3.c`: T 2.6, C 2.4, the keys 1.1, the template's lookup 3.7,
  X 6.8.

The follow-up recomputes distant operands on the scalar path, applies parent-independent
context rules once per block and checks a packed 64-bit template key against one
table row. Both paths took exactly the same nodes. All 346,998 SQLite entries
and 154,132 musl entries matched, including those from expressions whose operands
cross a block boundary. The table lookup also matched the original dictionary
for every learned key and over 300,000 unknown queries on each stream.
[Implementation and controls](https://github.com/buster14a/buster-performance/tree/f73e875502710c7f95f24bb4be11adc4c6c63dde/docs/measurements/per-core-step0-2850-2026-11-21/packed-template-keys)

| Locked 9700X, per taken node | SQLite cycles | SQLite branch misses | musl cycles | musl branch misses |
| --- | ---: | ---: | ---: | ---: |
| Repeated passes | 14.17 | 0.019 | 18.72 | 0.045 |
| After 200 ms of the scalar twin | 19.17 | 0.020 | 19.94 | 0.059 |

Each cell is the median of five paired round means, with two fresh processes per
variant per round and 21 timed passes per process. The five musl after-scalar
rounds ranged from 19.41 to 20.61 cycles per taken node. Input production and
ordinary source compilation are measured separately.
[Raw counters, ranges and exact sources](https://github.com/buster14a/buster-performance/tree/162b41a41bec79bec1b71941c8b1960f4c11a665/docs/measurements/per-core-step0-2850-2026-11-21/qualification/qualpack)

A second five-round run put musl after scalar work at 20.14 on that version.
For expressions spanning a distant operand, the revised version repairs the
scalar type facts and contexts before the shared template lookup and output
sweep. Musl measured 18.40 cycles on repeated passes and 19.86 after scalar work
(19.36–19.94 across those five rounds). SQLite measured 14.12 and 19.15. Every
one of the 20 round means met the 20-cycle gate. The highest median branch
miss rate was 0.055 per taken node and every entry still matched.
[Paired rerun, full counters and sanitizer results](https://github.com/buster14a/buster-performance/tree/67d089abc0fa325b3bd76435e4d937ee5426fed1/docs/measurements/per-core-step0-2850-2026-11-21/qualification/qualrepair)

T can also produce the packed template key while its deciding bytes are live.
Repairing the distant lanes before X retained all 346,998 SQLite entries and
154,132 musl entries. SQLite measured 13.7625 cycles per taken node on repeated
passes and 18.2908 after the scalar twin. Musl measured 18.0476 and 19.3685.
The largest of the 20 round means was 19.588 and the largest median branch-miss
rate was 0.0555 per taken node.
[Producer and full qualification](https://github.com/buster14a/buster-performance/tree/3272eaf9c4bfc10d14f7bdfc1dc4970acacafcc3/docs/measurements/per-core-step0-2850-2026-11-21/qualification/qualproduced)

## Step 1: the fact words

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/per-core-step1-2850-2026-11-15).
`measure-node-machine --shadow --facts`
([the guide](commands/lowering.md#the-fact-words-2850s-step-1)),
`c_check_node_machine_facts.c`.

### What it is

T types a leaf from the graph's leaf record, which the reader made from seven
columns at the token and at its declaration, and a member, a dereference, a
subscript and a call by a dozen queries of the type table. The experiment
makes the same records from three kinds of words:

- **A declaration's line** (32 bytes): its leaf's record's first half, as the
  record has it. It is made from the declaration's own columns as the reader
  had them at a use, never from the use's leaf; a declaration whose line would
  change (a type completed since, a weak pragma since) gets a new one.
- **A type's row** (64 bytes, by type index): its word, where it is complete,
  its value type and promotion, its element with the element's word, an
  array's or a function's pointer type, a function's return type, and as flag
  bits what each lookup's rule asks of the type.
- **A member's slot** (32 bytes, by a hash of the record type and the name):
  the member's type as the access qualifies it, its word, offset and flags.

A window is the unit's machine-taken expressions, a body's at a time and cut
where the next would take it past 384 records, as records in postorder with
their operands as distances back. The records of the
operators that are no lookup are given, as T made them, and so is a literal's
leaf. The kernel lists a window's identifiers' leaves and its lookups sixteen
records a step, makes the leaves, then makes the lookups (`->`, `.`, `*`, a
subscript, a call's value and a discard) a class at a time. Each lookup has a
short path for the shapes nearly all have and, behind it, its rule: T's own
rule's steps with every query of the type table replaced by a field of a row
or a slot. What neither takes is left to T, and counted. T's columns are the
oracle: every field T writes of a record is compared, over records the kernel
finds poisoned, so that a record it doesn't write shows.

### What it measured

On the 9700X, CPU 2, under the lock: Release builds of main `1b8231ea` with
this step's sources, medians of three runs, each the least of five passes over
the unit. The kernel's figures are a pass less the same windows' given records
put in place alone.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records of the windows | 291,021 | 73,998 | 130,291 |
| identifiers' leaves, and those the words take | 93,682, all | 21,795, all | 42,850, all |
| lookups, and those the words take | 88,737, 99.73% | 19,792, 99.72% | 27,566, 99.34% |
| records that differ from T's | 0 | 0 | 0 |
| **a body a window:** cycles a taken leaf or lookup | 26.5 | 17.2 | 15.8 |
| instructions | 73.3 | 75.8 | 71.3 |
| mispredicted branches | 0.37 | 0.00 | 0.00 |
| a leaf, cycles | 7.9 | 7.9 | 6.6 |
| a lookup, cycles | 46.1 | 28.1 | 30.0 |
| **windows packed to 384 records:** cycles a taken leaf or lookup | 22.5 | 15.0 | 14.9 |
| mispredicted branches | 0.24 | 0.01 | 0.01 |
| a leaf, cycles | 5.3 | 5.2 | 5.3 |
| a lookup, cycles | 39.9 | 25.3 | 28.4 |
| **windows packed to 4,096 records:** cycles a taken leaf or lookup | 21.8 | 15.7 | 15.0 |
| mispredicted branches | 0.15 | 0.01 | 0.01 |

A leaf's and a lookup's cycles are the two parts' TSC ticks; a leaf's has the
listing of all the window's records and, at a body a window, three reads of
the counter for some 35 leaves. Lua's and musl's passes repeat a sequence
short enough for the predictor to learn, so their mispredicted branches read
as none: `sqlite3.c`'s are the ones a compile would see.

By part on `sqlite3.c`, windows packed to 384 records, from a profile of the
kernel's functions (`perf record`, 121 runs of the unit):

| part | of them | cycles each | instructions | mispredicted |
| --- | ---: | ---: | ---: | ---: |
| the lists, the classes' mask and the batches, a record of the window | 291,021 | 2.5 | 8.9 | 7.9 a window |
| a leaf: its line's load and its record's store | 93,682 | 3.0 | 15 | |
| a discard | 36,724 | 17.8 | 54 | 0.20 |
| `->` | 23,433 | 25.3 | 92 | 0.15 |
| `.` | 7,257 | 31.9 | 124 | 0.24 |
| a call | 13,323 | 50.6 | 138 | 0.58 |
| a subscript | 6,812 | 62.6 | 227 | 0.56 |

- **Every record is T's**, on all three: no difference in any field, and the
  kernel's lists are their twin's. The first version differed on 80 of musl's
  leaves: a weak declaration's address may be null, which a line now holds as
  the reader had it at the use.
- **The words take every identifier's leaf** the machine takes, and 99.3% to
  99.7% of the lookups. Left to T are 200 discards, 35 calls and 5 subscripts
  on `sqlite3.c`; 50 discards, 5 subscripts and a dereference on Lua; 151
  subscripts, 17 arrows, 12 discards and 3 calls on musl. `sqlite3.c` has 4,740
  rows, 18,260 lines and 2,229 member slots, and no declaration of the three
  corpora needed its line made again.
- **A leaf meets the gate; a lookup doesn't.** A leaf is one load of its
  declaration's line and one store: 3 cycles, about 5 with its share of the
  listing. A lookup is 18 cycles as a discard, 25 as `->`, 32 as `.`, 51 as a
  call and 63 as a subscript on `sqlite3.c`. The design's line was "lookups
  above 20 cycles each: T is then bound by them whatever the rounds cost".
  All but the discard are above it.
- **With step 0:** the leaves and lookups are 63%, 56% and 54% of the windows'
  records. At these costs they add 14 to 17 cycles a record of the window on
  `sqlite3.c` and about 8 on musl to step 0's 16.7 and 20.6: 29 to 33 a node,
  inside the 38 the budget gives a body node, with 5 to 9 left for the
  statements and the front's own pass.

### Operand placement and call arguments on the later rules

On `f4f8dd9a1`, the lean kernel checks the call argument conversions used by
the later C pass. Two experiments on that revision measured improvements over
six-operation dispatch. The price includes building the lists for each body.

| Locked 9700X, cycles per taken leaf or lookup | SQLite reference | SQLite candidate | Paired difference | Rounds faster |
| --- | ---: | ---: | ---: | ---: |
| List each lookup with its first operand | 28.365 | 27.470 | −1.250 | 5/5 |
| Complete the callee and its arguments together | 28.5125 | 26.995 | −1.4675 | 4/4 |

Both changes also improved the complete timed pass in every SQLite lean round.
The first reduced it by a paired median of 0.610 cycles per window record and
the second by 0.515. Every run across SQLite, Lua and musl matched the original
record, order and decoy checks. These comparisons used separate executables
and rotations. The 10-cycle Step 1 gate remains open.
[Lookup layouts](https://github.com/buster14a/buster-performance/tree/31d3ef51e03e3a6e5a1ab0fc54d1469466aef321/docs/measurements/per-core-step1-2850-2026-11-21/lookup-list-layouts)
· [Call conversions](https://github.com/buster14a/buster-performance/tree/296c5d0edbcb3d6fe287f9a28d2d91c8e04f6371/docs/measurements/per-core-step1-2850-2026-11-21/call-conversions)

The public compiler at `fbef2d68d` took 113.044 ms for fresh `-c` on the
9,507,037-byte frozen SQLite file. That gives 84.10 MB/s. The separate counter
series measured 870.34 user and kernel cycles per preprocessed token. The target
is 9.507 ms for the complete object.
[Seven-round public baseline](https://github.com/buster14a/buster-performance/tree/296c5d0edbcb3d6fe287f9a28d2d91c8e04f6371/docs/measurements/window-compile-2850-2026-11-21/ce67967f5/sqlite)

### The figures are a difference of two passes (#2872)

Every figure of the fact words' kernels on this page (a taken leaf or
lookup's cycles; T whole's cycles a record or an evaluated node) is a timed
pass over a unit's windows, each window's given records put in place and
then the kernel, less a second pass that only puts the given records in
place. [#2872](https://github.com/buster14a/buster/issues/2872) found that
the second pass is not always what that copying costs inside the first. On
musl, whose 1,336 units are 97 records a pass, a build of #2524's read it
10% dearer (12.57 against 11.41 cycles a record, with the same instructions)
while the first pass stayed within 0.3%, and the same kernel's figure read
13% to 30% lower. It is some 120 cycles a unit's pass that a build fixes.
What it is was not found.

- **Which figures here carry it.** musl's do, and Lua's a twentieth of it
  (34 units). In this page's own records the second pass on musl read 11.41
  to 11.53 cycles a record in every build, so their musl figures compare
  with each other. A musl figure from another build has to be read with its
  second pass beside it. `sqlite3.c` is one unit and carries none of it, and
  the gates were read from `sqlite3.c`. Its second pass moves by up to a
  cycle a record from run to run, which the medians of three are for.
- **What to compare by.** The command prints the two passes apart, a record
  of the windows, with their TSC ticks
  ([the guide](commands/lowering.md#the-fact-words-2850s-step-1)). Two
  builds of one kernel are compared by the first pass's own figure.
  Rebuilt with that line, the two builds the issue names read 20.11 and
  19.93 cycles a record on musl in the first pass (0.9% apart) and 11.41
  and 11.46 in the second: the added line moved the code again, and the
  constant with it. Main with the line and step 2's second piece's branch
  read 20.31 and 20.58 (1.3% apart), and 39.30 and 39.32 with the
  operators.

### A record is one line: what a store a column costs

The first kernels wrote T's record as T's columns have it, a store a field,
and a leaf took 10 to 11 cycles for 35 instructions. `stores.c` (in the record)
stores a record's nine values three ways on the 9700X, for records one in three
as leaves are:

| a record's nine values | cycles a record | instructions |
| --- | ---: | ---: |
| nine columns (T's layout, staggered) | 7.98 | 27.1 |
| one 64-byte line, nine stores | 3.93 | 17.1 |
| one 64-byte line, one vector's store | 1.53 | 7.6 |

A store a column lands about one a cycle; a line's stores land two a cycle;
and a record whose first half is its declaration's line is one load and one
store. The kernel's record is therefore one line (`NodeFactsRecord`), and a
leaf fell from 10.4 cycles to about 5, the listing included. An operand's
type, word, flags and standing are then one line's loads too. The lanes of
step 0 read byte columns, 64 records to a register, and a line a record gives
them 1: which of a record's bytes are columns (the few the lanes read) and
which are its line is the layout's open question, and each column costs a
record about a cycle.

### The lookups: what moved them, and what didn't

Cycles a taken leaf or lookup on `sqlite3.c`, a body a window, by the kernel's
versions (each one's patch and runs are in the record):

| | the kernel | `sqlite3.c` | Lua | musl |
| ---: | --- | ---: | ---: | ---: |
| 1 | T's rules' steps on rows and slots; T's columns, each by a pointer of the kernel's state | 45.9 (140) | 31.7 (145) | 31.9 (140) |
| 2 | the columns in one block, the window's inputs in locals | 43.0 (122) | 29.5 (127) | 29.9 (123) |
| 3 | the lists by compress, a record's pairs stored together | 33.0 (94) | 20.8 (94) | 18.1 (86) |
| 4 | a short path a lookup, its rule behind it | 29.1 (75) | 17.8 (77) | 15.4 (76) |
| 5 | a record a line, a leaf a copy; lookups by class, their levels made by the kernel | 34.3 (91) | 21.4 (94) | 19.1 (86) |
| 6 | a path's tests as one word; a loop an operator | 34.5 (91) | 22.1 (96) | 19.7 (88) |
| 7 | the class with the record; a call's first arguments with no branch | 26.5 (73) | 17.2 (76) | 15.8 (71) |

Instructions are in parentheses. The first two rows are whole passes, with
the given records' copy (about 5.5 cycles a taken leaf or lookup on
`sqlite3.c`); the first also differed on 80 of musl's leaves. A leaf was 10.4
to 10.7 cycles through version 4 and 6.7 to 8.0 from version 5 on, at a body
a window.

- **A rule is a hundred instructions, not a load.** The first kernels ran T's
  rules' steps on rows and slots: 158 instructions a member access and 278 a
  subscript on the compiler's own source, counted exactly (`stephist`, a
  single-stepped run). The loads are six or seven; the rest is the operand's
  conversion, the guards, the flags and the conversions' entries. Short paths
  that test one word of "what makes this operand unfit" and select what
  depends on the operand took a fifth off a record's instructions (94 to
  75); they are still 54 instructions a discard, 92 an arrow, 124 a dot, 138 a
  call and 227 a subscript.
- **The dispatch.** A lookup's operator is a branch the predictor misses: 0.43
  mispredicted branches a record on `sqlite3.c` in version 4, where Lua's and
  musl's passes repeat and show 0.06 and 0.02. Making the lookups a class at a
  time (a level of operands and an operator, one loop each) trades it for the
  loops' ends: 0.37 at a body a window, 0.24 at 384 records, 0.15 at 4,096.
  It pays with the windows packed, and only if the class comes with the
  record: the kernel computing each lookup's level itself cost 30
  instructions a lookup and gave the gain back (versions 5 and 6).
- **A compiler's vectorizer.** Clang made the calls' argument loops, of two or
  three arguments, vectors of loaded and inserted lanes, and spilled the loops
  around them. They are marked to stay as written; the first four arguments
  are read with no loop. (The source read them with no branch either; the
  compiled kernel had one on the count, which [the
  follow-up](#a-short-paths-choices-a-correction-to-step-1) found and
  removed.)

### What stays scalar, and why

- **The leaves.** A leaf is one load of 32 bytes and one store of 64 by the
  line's number. Sixteen at once would gather their lines: `vpgatherdd` is 64
  uops and 12.6 cycles for 16 lanes on Zen 5 ([the coding
  style](coding-style.md)), where the scalar loads are two a leaf.
- **The lookups.** Each reads its operands' records, one or two rows and
  perhaps a slot, at places only its operands' types give, and writes one
  record. The lists and the classes are the vector part: sixteen records a
  step, compressed in the register.
- **The levels.** A lookup's level is one more than its operands' highest: a
  chain through memory, a lookup at a time. It belongs to whatever emits the
  postorder, which has the operands at hand.

### What it means for the design

- **The fact word holds for a leaf**, a third of the windows' records, once a
  record is a line: one load, one store, about 5 cycles with its listing.
- **It doesn't hold for a lookup as T's rules are.** The gate asked 10 cycles
  and the refutation line was 20. What would move it is the rule and the
  record, not the lookup's loads: a lookup that writes only what later rules
  decide on (the design's fact bytes and the type) and leaves the conversions'
  entries to C, as the design's C has them; and the discards, 41% of
  `sqlite3.c`'s lookups, as a context byte on their operand's record, which the
  design already says they are. T's whole record was this step's oracle;
  [the follow-up](#step-1s-follow-up-a-lean-record-and-a-lookups-load-alone)
  measures both.
- **For the front** (#2524's step 1b): a call's rule reads its arguments from
  the call's side, as a list of distances back, where the structural bytes
  have each argument's place and its consumer's distance; and the classes
  want each lookup's level with its record.

## Step 1's follow-up: a lean record, and a lookup's load alone

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/per-core-step1-lean-2850-2026-11-15).
`measure-node-machine --shadow --facts-lean` and `--facts-loads`
([the guide](commands/lowering.md#the-fact-words-2850s-step-1)).

Step 1 made T's whole record of a lookup and found the rule, not the loads,
its cost. It left two things unmeasured, which this measures on the same
windows with T still the oracle: a lookup that makes only what later rules
decide on, and what of a lookup is its load.

### The variants

- **Lean** (`--facts-lean`). A leaf is its declaration's line alone: 32 bytes,
  one load and one store of half a line. A lookup writes its record's first
  half (its type with that type's word, its flags and object, its payload and
  standing) and a member's offset, and no conversion's entry: those are C's,
  from the operator, its operands' records and their types' rows. A discard is
  not made. It is a context of its operand, as the design has it, and its
  record is given.
- **Loads** (`--facts-loads`). Of a lookup, only what its load gives: its type,
  that type's word and a member's offset, from its operand's type's row and,
  for a member, the slot; and only the tests the load itself makes (the row's
  flags, the type complete at the node, the member found and no bit-field, a
  call's prototype and count). Its standing, flags, object and payload are
  given, as lanes over its operands' bytes would give them, and so is whether
  its operand is fit for the short path: a byte a record, the lean kernel's
  own answer. No compile could run this kernel alone. It says what the load
  costs.

What a variant leaves out is checked, not assumed. In the lean kernel's
checked pass each taken lookup's two conversions' entries are made by its rule
from the kernel's own records and must be T's, or the command fails; and each
discard's whole record is made by the discard's rule from its operand's
record and counted as T's or not. The loads' kernel is checked after the lean
one, over records whose given fields are T's and whose loaded fields are
poisoned.

### What it measured

On the 9700X, CPU 2, under the lock: Release builds of main `fdf0de74` with
this follow-up's sources, medians of three runs, each the least of five
passes over the unit; a kernel's figures are a pass less the same windows'
given records put in place alone. Cycles a taken leaf or lookup, and in
parentheses the same a record of the window: the figure the variants share a
denominator of, since a lean kernel makes no discard and its taken lookups
are fewer and dearer than the whole kernel's.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records of the windows | 291,021 | 73,998 | 130,291 |
| identifiers' leaves, all taken | 93,682 | 21,795 | 42,850 |
| lookups a lean kernel makes, and those it takes | 52,013, 99.92% | 13,289, 99.95% | 17,046, 99.00% |
| conversions' entries left out, and those their rule derives | 51,973, all | 13,283, all | 16,875, all |
| discards left out, and those their rule derives | 36,724, 36,524 | 6,503, 6,453 | 10,520, 10,508 |
| records that differ from T's | 0 | 0 | 0 |
| **a body a window:** whole | 22.7 (14.2) | 17.6 (9.9) | 15.9 (8.6) |
| lean | 20.8 (10.4) | 16.0 (7.6) | 14.1 (6.4) |
| loads | 15.2 (7.6) | 10.8 (5.1) | 8.8 (4.1) |
| mispredicted branches: whole, lean, loads | 0.25, 0.21, 0.20 | 0.01, 0.01, 0.01 | 0.01, 0.01, 0.01 |
| **windows packed to 384 records:** whole | 18.9 (11.9) | 14.9 (8.4) | 14.7 (7.9) |
| lean | 17.4 (8.7) | 13.5 (6.4) | 13.0 (5.9) |
| loads | 11.8 (5.9) | 8.3 (3.9) | 7.8 (3.6) |
| mispredicted branches: whole, lean, loads | 0.12, 0.09, 0.09 | 0.01, 0.02, 0.01 | 0.01, 0.01, 0.01 |

By part on `sqlite3.c`, windows packed to 384 records, from profiles of each
kernel's functions (`perf record`, 481 runs of the unit): cycles each, with
the instructions and the mispredicted branches in parentheses. A part's
cycles read to 3 to 7 percent; the loads' dereferences are too few to read.

| part | of them | whole | lean | loads |
| --- | ---: | ---: | ---: | ---: |
| the lists, the classes' mask and the batches, a record of the window | 291,021 | 2.4 (10; 0.01) | 1.8 (6; 0.01) | 1.5 (5; 0.01) |
| a leaf | 93,682 | 3.1 (15) | 2.4 (12) | 2.4 (11) |
| a discard | 36,724 | 13.4 (53; 0.06) | given | given |
| `->` | 23,433 | 23.6 (92; 0.13) | 20.9 (79; 0.11) | 15.8 (49; 0.12) |
| `.` | 7,257 | 31.2 (122; 0.19) | 27.6 (118; 0.12) | 14.4 (56; 0.08) |
| `*` | 1,188 | 30.1 (105; 0.19) | 25.9 (94; 0.16) | |
| a subscript | 6,812 | 53.6 (228; 0.24) | 44.6 (189; 0.15) | 14.0 (52; 0.09) |
| a call | 13,323 | 35.5 (147; 0.20) | 28.0 (130; 0.10) | 11.7 (46; 0.09) |

- **Every record is T's** in what each variant makes, on all three, and what
  a lean kernel leaves out follows from what it makes: every taken lookup's
  conversions' entries are its rule's on the kernel's records, and so is
  every discard but the 200, 50 and 12 the whole kernel leaves to T.
- **The same lookups go by a short path** in the lean kernel and by their
  load alone in the loads' kernel: 99.67%, 99.83% and 97.92% of those made.
  The rest go by their rule (131, 17 and 184) or are left to T.
- **Lua's and musl's mispredicted branches read as none**, as in step 1:
  their passes repeat. `sqlite3.c`'s are the ones a compile would see.

### A short path's choices: a correction to step 1

Step 1 said a call's first four arguments are read with no branch. Its source
read them so; its compiled kernel didn't. Clang compiles a choice that has a
load behind one side as a branch (a conditional move with a memory operand is
unfolded into one), and so the choice "this argument's record, or the
callee's past the count" became a tree of branches on a call's argument
count; a subscript's "an array or a pointer", "a constant or not" and "an
lvalue or not" a branch each; and a dot's "an address constant or not"
another. None is one the predictor learns on `sqlite3.c`. The follow-up loads
what a choice may need whichever way it goes and chooses between registers,
by masks. On `sqlite3.c` with the windows packed:

| the lean kernel's | cycles before | after | mispredicted branches before | after |
| --- | ---: | ---: | ---: | ---: |
| `->` | 23.5 | 20.9 | 0.15 | 0.11 |
| `.` | 37.6 | 27.6 | 0.29 | 0.12 |
| a subscript | 52.2 | 44.6 | 0.46 | 0.15 |
| a call | 43.4 | 28.0 | 0.54 | 0.10 |
| the lean kernel, a taken leaf or lookup | 18.9 | 17.4 | 0.20 | 0.09 |
| the whole kernel, a taken leaf or lookup | 20.9 | 18.9 | 0.21 | 0.12 |

Lua and musl read 0.2 to 0.7 cycles a taken leaf or lookup more: their
repeated passes had those branches right, and the selects' instructions are
all they see of the change. Against main, whose kernel is step 1's, the whole
kernel on `sqlite3.c` went from 26.5 cycles a taken leaf or lookup to 22.7
with a body a window and from 22.4 to 18.9 with the windows packed. Two
builds of one kernel differ by up to a cycle on `sqlite3.c` (the record has a
second build of these sources, on the merge queue's candidate: 16.5 and 10.8
for the lean and the loads' kernels with the windows packed, where this one
reads 17.4 and 11.8).

### What it means

- **What a lean record leaves out is a quarter of the whole kernel, most of
  it the discards.** With the windows packed the whole kernel is 11.9 cycles
  a record of `sqlite3.c`'s windows and the lean one 8.7. Of the 3.2, the
  discards no longer made are 2.3: their records, 36,724 at 13 cycles, and
  their part of the lists and classes. A leaf's second half is 0.2 and the
  conversions' entries 0.9, between 3 and 9 cycles a lookup by its operator.
- **A lookup's load alone is 12 to 16 cycles**, 46 to 56 instructions, by its
  operator: its operand's record, the type's row, a member's slot, four or
  five tests and one or two stores. With the lists and the classes (1.5
  cycles a record of the window) the loads' kernel is 11.8 cycles a taken
  leaf or lookup on `sqlite3.c` with the windows packed and 15.2 with a body
  a window; 8.3 and 10.8 on Lua; 7.8 and 8.8 on musl. Step 1's gate was 10, a
  body's expressions a run: the load alone meets it on musl, misses it by 1
  on Lua and by 5 on `sqlite3.c`, where each body's classes end their loops
  at a branch the predictor misses (0.20 a taken record, against 0.09 with
  the windows packed, where `sqlite3.c` misses the gate by 2).
- **The rest of a lean lookup is its facts**: its standing, flags, object and
  payload, and whether its operand is fit, scalar code a lookup at a time. On
  `sqlite3.c` that is 5 of an arrow's 21 cycles, 13 of a dot's 28, 16 of a
  call's 28 and 31 of a subscript's 45. They are what the design gives the
  lanes, which made every other operator's facts for 2.6 cycles a node in
  step 0. A lookup's lanes would read, with its operands' bytes, the bytes
  its load brings (a slot's flags, what the row says of the type): step 2's
  to measure.
- **Against the design's line** ("lookups above 20 cycles each: T is then
  bound by them"): a lean lookup is above it by every operator, 21 to 45
  cycles; its load is under it, 12 to 16, and 21 to 28 with its part of the
  lists and classes.
- **With step 0**, whose 16.7 and 20.6 cycles a node had the leaves and
  lookups given: the lean kernel adds 8.7 to 10.4 cycles a record of the
  window on `sqlite3.c` and 5.9 to 6.4 on musl, 25 to 27 a node, where the
  whole records make it 29 to 31 and the budget gives a body node 38. The
  loads alone add 5.9 to 7.6 and 3.6 to 4.1: 23 to 25 a node before the lanes
  that would make the lookups' facts.
- **One body for every operator didn't pay.** The lookups made in the list's
  order by one loop with no class and no dispatch, each operator's choices as
  masks, read 18.8 cycles a taken leaf or lookup on `sqlite3.c` against the
  classes' 10.7 with the windows packed, and 20.2 against 15.0 with a body a
  window (the record's `l3`). Its body is 150 instructions where a class's
  loop is 46 to 56, with the loop's seventeen values on the stack and a
  branch clang kept on arrow against dot; and a chain such as `a->b->c` waits
  in it for each link's store, where the classes make a level's lookups
  independent.
- **For step 2:** the class comes with the record (step 1), and so would the
  byte that says a lookup's operand is fit, which is the lanes' own from the
  operand's standing and flags. Packed windows are worth 3 to 4 cycles a
  taken leaf or lookup on `sqlite3.c` to every variant.

## Step 2's first piece: T's other operators on the fact words' records

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/per-core-step2-operators-2850-2026-11-16).
`measure-node-machine --shadow --facts-operators`
([the guide](commands/lowering.md#the-fact-words-2850s-step-1)).

Step 1 made T's leaves and lookups from the fact words and left the rest of
T's records given: the operators that are no lookup, a quarter of a window's
records, and a literal's leaf. A machine can't take records from the T it
replaces. This piece makes them from the same words, in the same kernel, with
T still the oracle, so that T whole can be read against the shadow's own T on
the same windows.

### What it is

- **A literal's leaf is a line of its own**: the reader's record of it, 32
  bytes, which a compile would write as it reads the literal. The kernel
  copies it as it copies a declaration's line. A literal's lines are read
  once each, in the window's order, where a declaration's is read at every
  use.
- **The other operators are T's rows' and rules' steps on the kernel's
  records.** A row's operator (arithmetic, a comparison, a shift, a bitwise
  operator) reads T's own baked table by its operands' type codes; what a
  row asks of an operand's standing is one load of a 256-byte table. Behind
  the table are the constants' fold, a pointer plus or minus an integer and
  a pointer's equality with a null pointer constant. Then `=`, a conversion
  and an initialization; a control's condition and `!`; `&x`; a cast; `&&`
  and `||`; the increments and the compound assignments; a comma; `sizeof`
  and `_Alignof`; and `?:`. T's rules make a value whole for an update, a
  conditional and the conversions its columns don't hold; the kernel's make
  the same record from its operands' records alone, wherever no operand is a
  constant the conversion would change.
- **Two more words.** A type's side row (32 bytes) has the pointer types to
  it, from the machine's memo as T fills it, and its size and alignment. A
  pair's slot (16 bytes, by a hash of two pointer types) says whether one
  converts to the other as T's rule takes it and whether they point at the
  same type: a memo of the type table's answers, as the members' slots.
- **The classes are a round and a code.** Step 1's classes were a level of
  operands and an operator, and a level a scan of the window's lookups.
  With fifteen codes that is too many classes for a body, whose expressions
  are five levels deep with classes of their own at each. A round makes its
  classes in a fixed order of the codes, each class's operators in the
  window's order. An operator's round is then the highest of its operands'
  rounds, and one more only where an operand's code comes after its own: an
  operand of an earlier code is the round's earlier class, and one of its
  own code is before it in its class (a chain of `->`, a call's call). The
  order is the one that gives `sqlite3.c`'s expressions the fewest classes:
  `->`, a subscript, `.`, `&x`, a cast, the rest, the rows, a call, `&&` and
  `||`, an update, `*`, `=`, a conversion, a discard, a truth. Nine
  operators in ten are then of the first round, and a body has 1.6 rounds
  and 10 classes where it had 5 levels and 14.5 classes.
- **A class's loop lists what its path doesn't take** and makes it after the
  loop by its rule, as step 1's. It marks the record wide, so that a later
  operator of the class that reads it is listed too.
- **The check.** Every record the kernel makes is compared with T's over
  poisoned records, the lists with a record-at-a-time twin, and what a lean
  kernel leaves out with its rule's, as before. Poison reads as no value a
  path takes, so an operator made before its operand is only left there, to
  its rule or to T, and nothing differs; and in a timed pass the records
  hold T's own values, which would hide it too. Two checks are for that. A
  second checked pass gives the kernel decoys for what it is to make: T's
  record of each node with one inherited flag the other way. A decoy reads
  as the operand's own value, and what is made from it carries the flag and
  differs. And the twin checks the order itself: an operand the kernel makes
  by class is on a list no later than its operator's. `=`, a call and an
  update set the decoys' flag themselves, so only the order's check sees one
  of them made too early.

### What it measured

The 9700X, CPU 2, under the lock, Release; main `be6515fb` and the same with
the piece's sources; medians of three runs, each the least of five passes.
The record has two builds of the kernel: its main run, and a second with the
twin's order check, which no timed pass runs; a third, earlier build of the
same kernel read 38.6. Cycles an evaluated node (the shadow's own T's
denominator: 256,292, 57,625 and 109,639), T whole from the fact words:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records of the windows | 291,021 | 73,998 | 130,291 |
| literals, each from its line | 38,961 | 11,041 | 17,324 |
| the other operators, and those the kernel takes | 69,641, 97.97% | 21,370, 91.82% | 42,551, 95.34% |
| … by a class's loop; by their rule, one at a time | 86.89%; 11.08% | 55.86%; 35.96% | 82.71%; 12.64% |
| records that are no literal, and those the kernel makes | 252,060, 99.34% | 62,957, 97.13% | 112,967, 98.08% |
| records that differ from T's; over decoys | 0; 0 | 0; 0 | 0; 0 |
| the shadow's own T (main's) | 89.6 | 81.0 | 72.0 |
| **a body a window:** whole records, the first build | **37.7** | 40.6 | 32.6 |
| … the second build | **38.2** | 41.5 | 33.8 |
| instructions; mispredicted branches a record | 118; 0.30 | 179; 0.02 | 146; 0.01 |
| lean records: the first build, the second | **31.7, 31.9** | 36.2, 36.2 | 28.9, 29.4 |
| **windows packed to 384 records:** whole | 33.2, 33.9 | 38.1, 39.1 | 31.3, 32.7 |
| lean | 28.2, 28.2 | 34.4, 34.5 | 27.7, 28.2 |

**Against the piece's gate** (every record the oracle's; 95% of the records
that are no literal made by the kernel; T whole at 25 cycles an evaluated node
or less on `sqlite3.c`, a body's expressions a run; refuted above 38, or with
more than a tenth of these operators left to T):

- **Exact: met**, on the three corpora, with whole and with lean records, and
  over decoys.
- **Coverage: met.** The kernel leaves T 2.0%, 8.2% and 4.7% of these
  operators, mostly constants' casts, casts of other than builtin integers
  and pointers, `?:` of a constant condition or of pointers, comparisons of
  two pointers, and a unary plus of no constant.
- **25 cycles: missed.** T whole is 38 with a body a window, 2.3 to 2.4
  times less than the shadow's own T and half again the gate. Whole records
  are at the refutation line: 37.7, 38.2 and 38.6 in the three builds, above
  it in two. Lean records are under it, 31.7 to 32.6, and 28.2 to 28.6 with
  the windows packed.

The kernel's time has nothing for the records it leaves to T (1,655, 1,805
and 2,164 that are no literal: at T's own average, 0.6, 2.5 and 1.4 cycles an
evaluated node more), nor for making the words, as in step 1; a literal's
line is a copy of the reader's record of it, which T reads too.

By part on `sqlite3.c`, whole records, a body a window, from profiles of the
first build's kernel's functions (160 passes a run): cycles each
(instructions; mispredicted branches).

| part | of them | cycles each | cycles a pass |
| --- | ---: | ---: | ---: |
| the lists, the classes and the dispatch, a record of the window | 291,021 | 6.1 (20; 0.05) | 1.77 M |
| a leaf, an identifier's or a literal's | 132,643 | 3.6 (14; 0.02) | 0.48 M |
| the lookups: `->` 23.8, `.` 36.7, `*` 37.0, a subscript 60.4, a call 42.2 | 52,013 | 35.4 | 1.84 M |
| a discard | 36,724 | 17.9 (58; 0.17) | 0.66 M |
| a row's operator, by the table | 21,679 | 52.2 (180; 0.30) | 1.13 M |
| the rows' operators the table doesn't type (folds, pointers) | | | 0.51 M |
| `=` | 16,057 | 58.9 (209; 0.46) | 0.95 M |
| a conversion or an initialization | 3,846 | 62.5 (212; 0.61) | 0.24 M |
| a condition or `!` | 12,938 | 24.4 (75; 0.20) | 0.32 M |
| `&x` | 3,404 | 30.0 (115; 0.21) | 0.10 M |
| a cast | 2,949 | 70.1 (216; 0.73) | 0.21 M |
| `&&` or `\|\|` | 3,105 | 46.1 (144; 0.29) | 0.14 M |
| an update | 3,788 | 77.3 (247; 0.74) | 0.29 M |
| the rules an operator at a time (a comma, `sizeof`, `?:`, what a loop listed) | | | 0.30 M |
| all | | | 8.94 M |

The profiles' 160 passes teach the predictor more of the unit than a timed
run's five (66,129 mispredicted branches a pass against 88,153): the parts
sum to 8.94 M cycles where a timed pass is 9.67 M. With lean records the
lists are the same, a row's operator 47.4, `=` 55.8, a condition 17.3, and
the discards are not made: 7.81 M. With the windows packed and whole records
the lists are 3.7 a record and the loops between the same and a fifth less:
7.96 M.

### What moved it, and what didn't

Cycles an evaluated node on `sqlite3.c`, whole records, a body a window and
packed (the record's "The versions" has each build):

| | the kernel | a body a window | packed |
| ---: | --- | ---: | ---: |
| 1 | levels, an expression's last record apart; a vector compress a level, then one a class; every operator by its rule's one path | 49.6 | 42.9 |
| 2 | rounds of the codes, with chains; one scalar pass to the classes' lists | 42.5 | 38.0 |
| 3 | short paths for `=`, a conversion, a truth, `&&` and `\|\|`; a row's operands' standings by a table; the rows the table doesn't type in a loop of their own | 40.1 | 36.2 |
| 4 | `=` as two straight sides (a value, a constant); the rows' loop without a branch; the lists made before the leaves | 38.6 | 34.3 |
| | the same kernel, the record's first build | 37.7 | 33.2 |
| | the same kernel, the record's second build | 38.2 | 33.9 |

- **The classes by rounds** are the 7 cycles between the first two: 42,306
  classes and 14,418 level scans a pass became 29,803 classes and 4,653
  rounds.
- **A vector compress lost to the scalar pass, twice.** A compress a class is
  a scan of the window a class: 44.8 against 40.1. A compress a code while
  listing is fifteen compresses a vector of sixteen records: 39.1 against
  38.6, and worse in every setting. The scalar pass is about 6 cycles a made
  operator, bound by its counts' store and load.
- **Instructions are a poor guide.** The kernel runs at three a cycle on
  `sqlite3.c`: the compress a class has 3 more instructions a node and 4.7
  more cycles, the compress a code 10 more and 0.6.
- **Software prefetch** of the next window's columns didn't pay: within what
  a build moves the figure by, and the lookups' kernel alone read 24.8
  cycles a taken leaf or lookup with it against 22.9.
- **Step 1's kernels on the new driver** (no `--facts-operators`): the same
  or better on `sqlite3.c` (22.4 against 23.2 cycles a taken leaf or lookup
  whole, 20.4 lean, 15.0 the loads) and 0.4 to 2.0 cycles dearer on Lua and
  musl (whole 18.0 and 16.5 against 17.5 and 15.9; lean 17.3 and 15.3 against
  the follow-up's 16.0 and 14.1; the loads 12.8 and 10.4 against 10.8 and
  8.8).
- **A whole compile** doesn't run the kernel: `-c sqlite3.c` has the same
  instructions on both builds.

### What stays scalar, and why

- **The pass to the classes' lists**: measured against both vector ways
  above.
- **The operators' loops.** Each is T's rule's steps for one operator: its
  operands' records and their types' rows at places only the records give,
  32-bit flags by masks that differ by operator, a payload's fold or
  conversion, one record's store. This piece measured exactly that
  alternative to the lanes, and it loses: an operator that is no lookup is
  24 to 77 cycles in its loop, 56 on average, where step 0's lanes made
  these operators' seven fact bytes for 11.7 cycles a node they typed. The
  loops are not what the middle's T should be; they are what T's whole
  record costs an operator at a time, and the scalar twin that lanes for
  these operators would be checked against and fall back to.

### What it means

- **T whole on these records is exact, and 2.3 to 3.2 times cheaper than
  T's rules** as the shadow runs them: 9.67 M cycles a unit against 22.96 M
  on `sqlite3.c` with whole records, 8.13 M lean (the record's run).
- **It is not within the budget.** The middle has 38 cycles a body node. T's
  whole record by a loop a class takes 33 to 34 cycles a record of the
  window by itself (38 an evaluated node). Lean records take 28, and with
  step 0's C, keys, template and X (14.1 a node on `sqlite3.c`) the middle
  would be 42 with a body a window and 39 with the windows packed: a sum of
  parts measured apart, as the follow-up's 25 to 27 was, which had the other
  operators' facts from step 0's lanes.
- **What costs is the record, not the load.** The other operators are 24% of
  the records and 43% of the kernel's cycles. A row's operator, `=`, a
  conversion, a cast and an update are 180 to 250 instructions each for a
  whole record, with a third to three quarters of a mispredicted branch.
  That is step 1's finding for the lookups again, on operators that have no
  load of their own to make.
- **Three things are worth what they measured:** lean records 6 cycles an
  evaluated node; packed windows 3.5 to 4.6; and the lists, the classes and
  the dispatch 6.1 cycles a record with a body a window (3.7 packed), about
  half of it the scalar pass, which a front that gives each record its class
  and its place in the class's list would not need. Together: lean records
  with the windows packed are 28.2, and about 26 without the scalar pass, by
  these parts' sum and not by a measurement. The gate's 25 would still be
  missed.
- **For step 2:** the other operators' facts belong to the lanes, as the
  design has them, with a record's wide fields (its type and that type's
  word, its object and payload) written only where a later rule or X reads
  them. This kernel is then their oracle and their scalar path on the fact
  words' records. What that leaves to measure first is which of T's fields
  C and X read of each operator's record on these windows, beyond the seven
  fact bytes step 0's lanes made: the second piece, below.

## Step 2's second piece: what reads T's record

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/per-core-step2-reads-2850-2026-11-16).
`measure-node-machine --shadow --facts-reads`
([the guide](commands/lowering.md#what-reads-ts-record-2850s-step-2)),
`c_check_node_machine_reads.c`.

The first piece ended on a question. The design gives the operators' facts
to the lanes, as bytes a register holds 64 of, and says that what is only
carried (a type's identity, an object, a payload) stays in side columns the
deciding passes never load. That holds only if nothing decides on those
values, and it leaves open which of them have to be made at an operator's
record at all. This piece counts both, on every record of the same windows:
which of a record's wide fields a later reader reads, how, and where the
field's value comes from. It is a count in the shadow. Nothing is timed.

### What it is

- **A read is found by changing the field and running its reader again**,
  not by reading the reader's code. One field of one record is changed, the
  reader makes its result again, and the result is compared with what it
  made before.
- **Three readers.** A later rule of T is the record's consumer's rule: the
  fact words' kernel's, T's exact twin by the first piece's check, on the
  window's records. A call's rule is the machine's own, since the kernel's
  leaves each argument's conversion to T and that conversion reads the
  argument's type. C and X are the machine's own sweeps on the expression's
  columns; X runs on C's unchanged columns, so that what it reads itself is
  apart from what reaches it through C.
- **What a field is changed to says what of it is read.** A type becomes
  another type of the unit with the same word and the same row's facts, so
  that its identity alone differs, and a type of another word. Of the types
  with its word, the other is one whose element and whose size or stride
  differ from its own where there is one: `int*` becomes `long*` before it
  becomes `volatile int*`, which no reader of a pointee's size tells from
  it. An object becomes another of its kind (a local, a global) and one of
  the other kind. A payload takes the values its type holds, of six, and a
  member's offset two. Each of the flags' 25 bits is flipped, one at a time.
  For C and X each operand's conversion's type is changed the same two
  ways, and its flags' 12 bits.
- **What changed says how it was read.** A byte-level answer changed (a
  consumer's standing, flags, type's word or conversions' flags; C's
  context, template or target's word; the tape's kinds, classes or length;
  a refusal): the field *decides*. Only a wide value changed (a type's
  identity, an object, a payload, the tape's value): it is *carried*.
- **A carried value is a read only where it arrives.** A rule that copies
  its operand's object into its own record reads nothing if nobody reads
  that record's object. So what a rule carries is kept as an edge, from the
  operand's field to the consumer's fields that changed, and counted only
  where one of those is itself read, from the last record back. Where C
  alone carries a value (a target's identity), X runs on C's changed columns
  and the tape says whether it arrives. A flag a consumer only hands on, the
  same bit of its own flags, is no read of it either.
- **Where a read field comes from:** a leaf's line; a builtin type, which a
  code names; an operand's same field; a type an operand's type's row gives
  (its value type, its promotion, its element, the pointer to it, a
  function's return type) or that type's size; the node's own type name (a
  cast's); or none of them: new at the record. A payload that equals an
  operand's counts as that operand's only where its rule was seen to copy
  it. One that is computed (`1 << 0`) is new. So is one that a conversion
  copies only where the value fits its type, and one whose rule is T's own
  and couldn't be run again: those two are counted apart.
- **A record's class is what has to be made at it:** nothing wide is read;
  what is read a code names; what is read is a copy a selector could name;
  or something read is new at the record, a wide step no lane makes.
- **The census checks itself.** Before a consumer's or an expression's
  changes and after they are all taken back, the reader runs with nothing
  changed and has to make what it made. Each record's place in the windows
  has to be its place in the machine's expressions.

### What it counted

The 9700X, under the lock, Release, main `3d0d56ed7` with the piece's
sources, on the first piece's three corpora and windows. The counts are the
same on every run; `sqlite3.c` takes 2.0 s, Lua 0.7 s and musl 2.7 s with the
pipeline.

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records of the windows | 291,021 | 73,998 | 130,291 |
| measured: every reader run again | 288,697, 99.20% | 71,719, 96.92% | 126,899, 97.40% |
| runs with a field changed: a rule; the sweeps | 8.18 M; 15.00 M | 2.12 M; 3.85 M | 3.64 M; 6.74 M |
| runs with nothing changed; those that differ | 415,598; 0 | 99,580; 0 | 179,014; 0 |
| records misplaced | 0 | 0 | 0 |
| **the operators that are no lookup**, measured | 69,205 | 20,988 | 41,675 |
| nothing wide is read | 83.99% | 59.45% | 82.36% |
| what is read a code names (a builtin type) | 0.85% | 1.40% | 1.81% |
| what is read is a copy | 9.76% | 14.91% | 9.71% |
| **something read is new at the record** | **5.41%** | **24.24%** | **6.12%** |
| **the lookups**, measured | 88,250 | 19,421 | 26,724 |
| nothing wide is read; a code; a copy; new | 59.3%; 0.2%; 9.9%; 30.7% | 60.1%; 0.2%; 5.2%; 34.6% | 67.2%; 0.8%; 15.9%; 16.1% |
| **the leaves**, measured | 131,242 | 31,310 | 58,500 |
| nothing wide is read; a copy of its line | 0.33%; 99.67% | 0.49%; 99.51% | 1.05%; 98.95% |
| **records C or X decide on an identity of** | 14 | 42 | 206 |
| records whose type has no other of its word | 0 | 0 | 535 |

What is read of an operator that is no lookup, and by whom (records, and
their share of the measured ones):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| its type's identity | 5,538, 8.0% | 2,806, 13.4% | 3,706, 8.9% |
| … from: the pointer to its operand's type or another type that type's row gives; a builtin; its own type name; an operand's | 2,794; 1,331; 881; 532 | 1,216; 505; 425; 660 | 996; 1,521; 468; 721 |
| its payload | 6,191, 8.9% | 5,746, 27.4% | 4,178, 10.0% |
| … from: a type's size; an operand's, copied; new | 742; 1,703; 3,746 | 257; 401; 5,088 | 676; 951; 2,551 |
| its object, an operand's | 107 | 39 | 258 |
| its operands' conversions' types (an operand's and a builtin) | 269 | 586 | 469 |
| a wide field read by a later rule of T | 9.1% | 28.0% | 10.2% |
| … by C | 0.4% | 2.8% | 1.1% |
| … by X | 7.6% | 15.0% | 9.3% |

- **Every field that is new at an operator's record is a payload**: no
  type and no conversion's type is. They are constants computed at the
  record: on `sqlite3.c` a negative literal (`-x`, 1,261), an `|` of flags
  (736), a constant's cast (532), `<<` (279), `~` (263), `-` (211), `+`
  (136), `*` (115); on Lua its macros' arithmetic, `<<` (1,574), `+`
  (1,038), `~` (711), `|` (527), a cast (453), `-x` (380); on musl `-x`
  (945), a cast (288), a constant condition's truth (239), `-` (234), `+`
  (181).
- **Two parts of the new payloads are not folds.** A conversion to a type
  that doesn't hold every value copies the constant where it fits, a test
  of the value and a copy: 93, none and 239 records. And a constant's cast is
  T's own rule, which the census can't run again, so its payload is counted
  new although it equals its operand's: 515, 505 and 295. Without the two
  the folds are 4.5%, 21.8% and 4.8% of these operators.
- **Of all the measured records** the new ones at an operator that is no
  lookup are 1.3%, 7.1% and 2.0%.
- **A lookup's new fields are the lookups' own:** a member's type and
  offset from its slot, an element's type, a call's return type. A discard,
  41% of `sqlite3.c`'s lookups, has nothing read.
- **C and X decide on an identity for 14, 42 and 206 records**, all by C,
  all a type's, and all the callee of a call. C asks whether every parameter
  of the callee's type is a plain scalar before it takes a call
  (`node_machine_classify_call`), which neither the type's word nor its row
  holds. The census sees that only where the callee's other type differs in
  it. **C asks it of every call it takes**: the windows have 13,323, 4,047
  and 5,348 calls, 4.6%, 5.5% and 4.1% of their records.
- **What they decide on of a payload.** X decides on whether it is zero:
  for a member of an address constant (715 records on `sqlite3.c`, 70 on
  Lua, 343 on musl), a subscript (58, none, 12) and `&x` (15, 1, 4). C
  decides on a literal's value for 141, 4 and 97 literals, nearly all where
  the value eight changes its answer.
- **The flags.** T's rules decide on 16 of the 25 bits of a record's flags:
  volatile-part, bit-field, vector-duplicate, null-pointer, register,
  vector-element, register-name, gnu-ice-form, ice-unevaluated,
  fp-environment, inactive-union-address, ice, float-literal, literal-zero,
  inactive-union-read and static-object. C decides on four (side-effect,
  fp-environment, bit-field, vector-element) and on two of a conversion's
  (to-union, transparent-union). X decides on none.
- **A later rule of T decides on a type's identity** for 62% of
  `sqlite3.c`'s identifiers, 36% of its `->`, 82% of its `&x`, 36% of its
  casts and 10% of its `+`. The census counts the record that is read, not
  its consumer. By the rules, the consumers are the lookups, which load by
  their operand's type, and the conversions of pointers, which compare two
  types. Each of those types is a leaf's, a lookup's or a copy.

**Against the piece's gate** (the census exact; at most 15% of the operators
that are no lookup need a wide step; C and X decide on a type's or an
object's identity for no more than 2% of the records; refuted above 30%
wide steps, or above 2% identities):

- **Exact: met.** No run with nothing changed differs, of 694,192 on the
  three corpora, before a consumer's or an expression's changes or after
  them; no record is misplaced; and the kernel's records are T's, as the
  first piece's check has them.
- **15% of the operators: met on `sqlite3.c` and musl, missed on Lua.**
  5.4% and 6.1% need a value made at the record; 24.2% of Lua's do. Lua's
  operators are a quarter constant arithmetic, from its instruction
  formats' macros. The refutation line, 30%, is not crossed: with every
  operator the census left counted as needing one, Lua is at 25.6%.
- **Identities: met by the census's count, crossed by what the count is
  of.** The census finds 0.005%, 0.06% and 0.16% of the measured records.
  They are one question of a callee's type, and C asks it at every call
  it takes: 4.1% to 5.5% of the records, over the 2% line. What the claim
  said would
  follow does not: no lane has to carry a wide value for it. It is one bit
  of a function type, which the type's row can hold, and a call is a lookup
  that loads that row.

### What the counts can't say

- **The records it left**: 0.8%, 3.1% and 2.6% of the windows' records are
  operands of a consumer the kernel left to T (1,621, 1,805 and 2,171
  consumers, the first piece's: constants' casts first) or of a call T
  types by its action. No expression was past the 256 records C and X are
  run again on.
- **One other type.** A type's identity is changed to one type of its word.
  Two types that give a reader the same answer hide the read: two records
  with the same member at the same offset, or two function types whose
  parameters are all plain scalars. And musl's small units have 535 records
  whose type has no other of its word. For C and X the readers' code bounds
  what that hides. They ask the type table four things by a type's
  identity: a pointer's stride, which the census sees as a carried value; a
  member access's record's kind, which the row's facts already hold; a
  segment override, which no corpus has; and the callee's parameters, above.
- **Upper bounds.** A change that takes a record out of its rule's reach
  counts as a decision; a wide value that only a type of another word
  changes counts as the type's identity's read; and a byte-level answer of
  a consumer's record counts whether or not anybody reads that byte.
- **The tests are a fourth reader, and the census doesn't run them.**
  Before a compile takes an expression, the census's set and its silence
  tests say whether the action would have warned. #2524's last piece wrote
  them from T's records
  ([its note](https://github.com/buster14a/buster/issues/2850#issuecomment-6912816238)).
  They decide on the operator, the type's class and kind and the flags'
  bits for every record; on bits of the object and of the declaration for
  the leaves; and on a property of a constant operand's payload (zero,
  negative, fits a width) for a tenth of the records. They read no identity
  but in the 0.3% of the records they send on (5.2% with `-Wall -Wextra`).
- **A statement's own checks** of its expression's value are not run
  either.

### What it means

- **The design's premise holds for C and X, with one bit to add.** What
  they take of a wide field they carry to the tape. The one thing they
  decide on by a type's identity is whether a callee's parameters are all
  plain scalars, at every call: a bit for a function type's row. With it
  their rules can be arithmetic on bytes, with the tape's values written
  from the side columns.
- **For four operators in five the fact bytes are the whole record.**
  Nobody reads the type's identity, the object or the payload of 84% of
  `sqlite3.c`'s operators that are no lookup, 82% of musl's and 59% of
  Lua's. A lane that makes their bytes has made them.
- **One in ten needs a selector, not a value.** What is read of it is its
  operand's field, the pointer to its operand's type, its own type name, a
  builtin type or a type's size: a byte that says which, and a copy where
  the reader is. The first piece's kernel made all of these as T's whole
  record, 56 cycles an operator.
- **The constants need a pass of their own.** One operator in twenty on
  `sqlite3.c` and musl, and one in four on Lua, is arithmetic on constants,
  whose value no byte holds. Step 0 had every value given, and the first
  piece's kernel made these by their rule one at a time (11%, 36% and 13%
  of its operators). A fold's operands are constants and its consumer reads
  its payload, so it is a pass over the constants' records by operator and
  class, eight 64-bit payloads to a register or one at a time: 1.3%, 7.1%
  and 2.0% of the windows' records.
- **T's own later rules are where an identity decides**: a lookup's load by
  its operand's type, and a pointer's conversion by the pair of types. The
  design has both as loads, and the types they need are a leaf's, a
  lookup's, or one of the copies above.
- **For step 2:** T's lanes for the operators that are no lookup need, per
  record, the fact bytes, the flags' bits the rules decide on, and a
  selector for a tenth of them; the folds are a pass beside them. That, with
  step 1's leaves and lookups, is the T to measure next against the 25
  cycles the first piece missed. It is not claimed here.

## Step 2's third piece: the kernel where the machine sits

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/per-core-step2-reader-2850-2026-11-17).
`measure-node-machine --shadow --front-facts`
([the guide](commands/lowering.md#the-kernel-where-the-machine-sits-2850s-step-2)).

The first two pieces ran after the pipeline, on windows made from T's own
columns and the walk's nodes. A compile that skips the walk for an expression
has neither. This piece runs the same kernel at reader time, where
[#2524](node-machine.md#t-where-it-sits) put the machine, and makes each
window's inputs a second time from what a compile has there: the lean front's
records and the reader's columns.
[Its specification](https://github.com/buster14a/buster/issues/2850#issuecomment-6933299446)
has step 2's remaining pieces by size, and why this one came first.

### What it is

- **A batch is a chunk's expressions.** The reader's hook types a chunk's
  roots with T. Each expression T takes is appended to the batch's windows,
  and after the chunk's roots the batch is run: the kernel on each window as
  it finds the words; again over poison, every record compared with T's;
  again over decoys; and a timed turn, the kernel and T again on the same
  expressions, the kernel first in every other batch.
- **The words are the unit's as it stands there.** A type's row is made
  before the first expression that reads the type is appended, for the walk's
  types and for T's own derivations. A declaration's line is made at its
  first use. An enumerator's value comes from the reader's own column: the
  list the pipeline reads it from is made when the reader ends. A member's
  and a pointer conversion's slot is filled when the kernel first meets it.
- **A window's inputs from the front's records**
  (`node_facts_front_expression`), with no node of the walk's and no record
  of T's:
  - an operand is a distance back in the postorder, and a root's operand the
    record before it;
  - an identifier's leaf is its declaration's line, a literal's a line of its
    own (an integer's as the reader decoded it, any other decoded again), a
    string's of its type and its object;
  - a member's name is the token after the operator's, a cast's type its
    token's query's, a return's conversion's type the function's;
  - a call's arguments are the records before it, each found by the
    subtree's size of the one after it;
  - a record's kind and class come from its operator and its operands'
    classes, as after the pipeline.
- **Their twin is the making from T's columns** (`node_facts_append`, the
  first two pieces'). The two are compared field for field, a literal's line
  by its content and a call's arguments one by one. The checked run takes the
  front's inputs, so what the kernel makes of them is T's record or a
  difference.
- **What the front's records don't give is counted by why:** an expression
  some of whose records a chunk's end cut off (the lean front keeps a waiting
  value's place across chunks, not its record), an operand past a byte's
  distance, a call of 255 operands or more. Those expressions take the twin's
  inputs.

### What it measured

The 9700X, CPU 2, under the lock, Release; main `178c9e2` with #2894's and
#2890's sources and the piece's; medians of five readings in turn. TSC ticks
at 3,792.9 MHz, 1.45 cycles each at the core's 5.49 GHz, a record of the
windows:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions T takes at reader time; their records | 50,993; 289,838 | 10,839; 76,304 | 43,415; 224,326 |
| records that are no literal, and those the kernel makes | 250,805, 98.84% | 64,965, 96.75% | 198,338, 96.57% |
| … in the expressions the whole machine takes there | 243,928, 99.39% | 59,894, 97.25% | 182,939, 98.03% |
| … after the pipeline (the first piece) | 99.34% | 97.13% | 98.08% |
| records that differ from T's; over decoys; lists | 0; 0; 0 | 0; 0; 0 | 0; 0; 0 |
| expressions whose inputs the front's records give | 50,094, 98.24% | 10,605, 97.84% | 42,685, 98.32% |
| their records' inputs that differ from the twin's | 0 | 0 | 0 |
| not given: cut by a chunk's end; any other reason | 899; 0 | 234; 0 | 730; 0 |
| **T, its first run** (T alone on the lean front; beside the kernel) | 100.4; 101.9 | 118.3; 122.2 | 184.9; 185.7 |
| **T again** on the batch's expressions | 49.2 | 52.0 | 52.6 |
| **the kernel:** its first run; a later run | 27.9; **16.4** | 125.6; 20.6 | 337.6; 19.6 |
| **the inputs from the front's records:** as each expression came; again, a batch in one loop | 46.7; **28.4** | 57.9; 27.9 | 78.9; 34.2 |
| the kernel's later run and the inputs' second making | **44.8** | 48.5 | 53.7 |
| the rows' making; the lines' | 0.9; 2.5 | 8.8; 15.9 | 44.9; 23.1 |
| the twin's making, from T's columns | 68.1 | 116.1 | 355.0 |

- **The two shares are of different sets.** At reader time T takes
  expressions the census's tests then refuse, and the front leaves some the
  shadow takes after the pipeline (`sizeof`, a statement expression). The
  second row of shares is of the expressions that pass the tests and C and X
  at reader time, which is the set the first piece's was nearest to.
- **What the kernel leaves** is what it left after the pipeline: constants'
  casts and casts of other than builtin integers and pointers first (1,090,
  1,389 and 1,425 casts), then `?:`, pointers' comparisons and, on musl, 1,038
  conversions.
- **Lua's and musl's first runs are their units' starts.** Lua is 34 units
  and musl 1,336, and the shadow starts each unit's kernel on fresh memory.
  The kernel's first run, the rows, the lines and the twin's making all read
  several times `sqlite3.c`'s there, and the later runs are within 5 ticks of
  each other on the three. What of it is page faults was not counted;
  `sqlite3.c`, one unit, is the figure of a first run.
- **A compile doesn't run it.** `-c sqlite3.c` retires 140 more instructions
  of 1,362.1 M with the piece in the executable, and its wall time reads
  -0.12% and +0.01% in two comparisons of 21 runs a build, where the base
  against itself reads -0.11% and +0.06%.

**Against the piece's gate** (every record the oracle's and every window the
twin's; the kernel makes at least the share it makes after the pipeline; 35
ticks a record or less on `sqlite3.c`, the window's making in it, where T's
first run read 71.2 in #2887's record; refuted above 53):

- **Exact: met**, for every record the kernel makes and every input the
  front's records give, on the three corpora and the compiler's own source,
  with the tests and C and X beside it and without.
- **Every window from the front's records: missed by 1.7% to 2.2% of the
  expressions**, each one cut by a chunk's end. No other reason occurs on the
  corpora.
- **The share: met** on `sqlite3.c` and Lua, and 0.05 point under on musl.
- **35 ticks: missed.** The kernel's later run and the inputs' second making
  are 44.8 ticks a record on `sqlite3.c`. As a compile would run them, once
  each as the expressions come, they are 46.7 and 27.9, with 3.3 for the
  words: 77.9, where T's first run beside them is 101.9.
- **The refutation line is crossed** by the first runs on every corpus, and
  by the later runs on musl. What the line was to test did not happen: the
  kernel keeps its gain where the machine sits. It makes a record in 16.4
  ticks where T takes 49.2 on the same expressions, and in 27.9 on its first
  run where T's takes 101.9. What crosses the line is the inputs' making,
  which is dearer than the kernel.

T's first run reads 102 ticks a record on the lean front, and read 71.2 in
#2887's record on the whole front (`--front-types` alone). T, C and X
together are as far apart: 156.8 on the lean front in #2890's record, 134.1
on the whole one in #2887's. What of the lean front's chunk makes T's first
run dearer was not measured. This piece's comparisons are within one run,
and the kernel reads no node of the walk's.

### The inputs' making: what moved it, and what didn't

Ticks a record, the batch's expressions in one loop (the record's "The
versions" has each build; three readings, five from `m` on):

| | the inputs' making | `sqlite3.c` | Lua | musl |
| --- | --- | ---: | ---: | ---: |
| `a` | a record at a time, a branch on its operator | 32.4 | 31.5 | 42.0 |
| `h` | by passes over the batch: every record's columns, the lists by counters, a pass a list, the classes last | 51.2 | 55.0 | 72.8 |
| `l` | … every load made and every choice a mask; a tag a record and a vector compress to the lists; the outputs' places in locals | 34.3 | 39.9 | 55.9 |
| `m` | … the sparse columns cleared a batch; the class of the record before kept at hand; what an operand's class asks from a table | 33.6 | 38.0 | 53.7 |
| `n` | a record at a time again, without the tests the front's own check has made | 30.5 | 29.3 | 35.9 |
| `q` | … an enumerator's leaf from the reader's column, and one test fewer | 29.4 | 28.9 | 34.7 |
| | the commit: `q` without a taking back its caller makes anyway, with its tests | 28.4 | 27.9 | 34.2 |

The passes of `h`, `l` and `m` on `sqlite3.c`, ticks a record:

| pass | `h` | `l` | `m` |
| --- | ---: | ---: | ---: |
| every record: its operator, distances, position, kind, code and tag | 24.1 | 12.8 | 13.6 |
| the identifiers' leaves, a line each | 2.2 | 2.2 | 2.2 |
| the other leaves | 2.7 | 2.7 | 2.8 |
| the members, the casts and the returns | 2.2 | 2.4 | 2.4 |
| the calls and the conditions | 2.9 | 2.9 | 2.9 |
| the made operators' classes | 17.1 | 11.2 | 9.5 |
| all | 51.2 | 34.3 | 33.6 |

- **The first pass went from 24.1 ticks a record to 12.8** by two things.
  A choice with a load behind it is a branch: clang compiles
  `condition ? value : load` and `a && load` as one, and a record's operator
  is what the predictor would have to guess. Every load is made, and the
  choice is between registers. And seven lists' lengths with a dozen
  columns' pointers in one loop spill: 25 loads and stores of the stack a
  record. One tag byte a record and a vector compress to each list, as the
  kernel lists a window, took them out.
- **A class read back from the column just stored is a chain through
  memory.** The classes' pass reads its operands' classes, and the operand is
  most often the record before. Keeping that one at hand, and what an
  operand's class asks as a table's byte, took the pass from 17.1 to 9.5.
- **A store a column a record is about a cycle,** as step 1 measured. The
  columns most records have nothing in are cleared for the batch and written
  by the lists' passes.
- **The reader's per-token columns are sparse to a record.** A cast's type
  and a return's are read for the casts and the returns alone: a pass that
  read them for every record touched lines the walk had not just read.
- **A profile of `m` has no hot spot:** about 125 instructions a record,
  spread over the passes.

### What stays scalar, and why

- **The inputs' making is a record at a time.** By passes with no branch a
  record it is 33.6 ticks on `sqlite3.c` where the loop with a branch on the
  operator is 30.5, and 38.0 and 53.7 against 29.3 and 35.9 on Lua and musl,
  whose batches are short. Why the passes lose was not measured beyond
  their profile: no hot spot, and about 125 instructions a record for a
  record's nine fields, a table's byte or two and eight stores. No counter
  was read for misses, so nothing here says the reads bound it. The passes'
  version is in the record (`patches/facts-m.patch`, what the 9700X built),
  with its twin's check.
- **The kernel** is the first piece's, unchanged.

### What it means

- **The kernel's gain is a reader's chunk's to keep.** In #2524, T at reader
  time was 9% over its tight loop's first pass and 25% over its best. The
  kernel after the pipeline read 22.9 ticks a record of the windows (the
  first piece's 33.2 cycles); at reader time its first run is 27.9 and a
  later one 16.4, a third of T's 49.2 on the same expressions.
- **A window made as a copy takes the gain back.** 28.4 ticks a record at
  best, more than the kernel itself, and 46.7 as the expressions come. T by
  the kernel is then 44.8 ticks a record where T again is 49.2, and 77.9
  where T's first run is 101.9: 9% and 24% less, not the half the gate asked.
- **So the window should not be a stage between the front and the
  kernel.** The front already holds each record's operator, its operands'
  distances and its leaf's declaration when it makes the record, and has
  already branched on what the record is; the kinds, the codes and the tags
  are a table's bytes by the operator. This piece made all of it a second
  time. Step 3's row is where it belongs: the front writes the window's
  columns as it makes each record, and keeps a waiting record whole at a
  chunk's end, which is also the 2% of the expressions this piece could not
  give. That it costs less there is this piece's reading of its own
  figures, not a measurement: [the next piece's
  specification](https://github.com/buster14a/buster/issues/2850#issuecomment-6939344308)
  has its gate and what would refute it.
- **What a compile has at reader time is enough.** Every input the kernel
  reads comes from the front's records and the reader's columns, exactly,
  for every expression not cut by a chunk's end: no walk's node is needed to
  type an expression this way. That was the open question of T at reader
  time, which reads the walk's nodes.
- **The words cost 3.3 ticks a record on `sqlite3.c`,** and 24.7 and 68.0 on
  Lua and musl, where every unit makes the rows of its headers' types again:
  265,765 rows for musl's 224,326 records. That is the environment's side
  (#2536): a row and a line made once, where the type and the declaration
  are made.
- **For step 2:** the order of its remaining pieces stands (the tests where
  their inputs are; C and X on these records; the splice; T's lanes), with
  one change. The kernel's inputs are step 3's to write. Until the front
  writes them, T by the kernel at reader time is worth a quarter of T's
  first run, where the kernel with its inputs given would be worth two
  thirds (31.2 ticks a record with the words, against 101.9).

## The front writes the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/front-window-2850-2026-11-17).
`measure-node-machine --shadow --front-window`
([the guide](commands/lowering.md#the-front-writes-the-window-2850)).

The piece before made the kernel's window as a copy of the front's records,
and the copy cost more than the kernel. Here the lean front writes a record's
inputs as it makes the record, and the kernel reads them where they are
written. It is the first part of step 3's row (the node machine's front; the
lexer's and the parser's rows are not started) and the missed gate of step
2's third piece.
[Its specification](https://github.com/buster14a/buster/issues/2850#issuecomment-6939344308)
had each expression's columns appended to a window as blocks. That was built
and measured first, and is not what the piece ends as: nothing is appended.

### What it is

- **The lean front writes the window's columns** beside its own
  (`NodeFront.window_kinds` and the rest), a record at a time, right after it
  makes the record: its kind and code from a table by its operator, its class
  from its operands' classes, its operands' distances whole (a root's is to
  wherever its value waited), a member's name, a cast's or a return's type, a
  call's arguments' distances in a list of the front's, its source position.
- **A literal's line is written with its record.** A batch's first lines have
  the front's records' numbers, so a literal's leaf's line is its record's.
- **A name's line is found by a pass of its own** over the chunk's records,
  right after the front's (`node_facts_window_names`). The front writes the
  declaration and asks the cache for its line's number. The pass finds the
  names by a compare of the kinds and a compress of the lanes, sixteen
  records a step; asks for each one's line; then reads each line and writes
  the leaf's kind and entry. A declaration's line is made there at its first
  use.
- **A chunk's end keeps a waiting value's records whole,** in a row, where
  the lean front keeps its place alone: a control's condition waits for its
  body, and its expression is read when its root comes. The rows take no
  more of the arrays than a record a waiting value and a chunk's records
  leave. A value whose records have no room keeps its place alone and is cut,
  for good, with every record made above it: its expression is not read in
  place.
- **The kernel reads the front's columns in place.** The records the front
  holds after a chunk are one window. The kernel's inputs are the front's
  arrays themselves, exchanged for the batch's while it runs: over poison
  with every record compared with T's, over decoys, and a timed run. In the
  shadow it makes the records of the expressions T took and reads every
  other record as given, since T's records are the oracle for those alone.
- **Both other makings are its twins.** Every input the front wrote is
  compared with the one from T's columns and, where the copy gives the
  expression, with the copy's: a leaf's line by its number or its words, a
  call's arguments one by one. With rows kept whole the copy gives the
  expressions a chunk's end used to cut.
- **The same lean front without the window runs beside it,** on the same
  chunks, which of the two first by the chunk's parity. Its ticks are what
  the window's are read against, and the two must make the same records.

### What it measured

The 9700X, CPU 2, under the lock, Release; main `d876a5e0f` with the piece;
medians of five readings in turn. TSC ticks (1.45 cycles each) a
record of the expressions T takes at reader time:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions T takes there; their records | 50,993; 289,838 | 10,839; 76,304 | 43,415; 224,326 |
| expressions the kernel reads in place | 50,993, 100% | 10,839, 100% | 43,415, 100% |
| inputs that differ from either twin's | 0 | 0 | 0 |
| records the kernel makes in place; of the batch | 247,886; 247,886 | 62,855; 62,855 | 191,527; 191,527 |
| records that differ from T's, over poison or decoys; lists | 0; 0 | 0; 0 | 0; 0 |
| chunks in which the front without the window made other records | 0 | 0 | 0 |
| waiting values kept whole at the chunks' ends; cut | 4,226; 0 | 653; 0 | 58,118; 0 |
| **the kernel in place,** a later run | **15.6** | 20.1 | 18.6 |
| **the names' pass,** less the lines it made | **4.6** | 5.9 | 17.5 |
| **the front's rise** with the window | **9.7** | 12.4 | 25.4 |
| **their sum** | **29.9** | 38.4 | 61.5 |
| the readings of the sum | 29.0 to 30.3 | 38.1 to 40.9 | 55.4 to 66.6 |
| with the front's arrays touched beforehand (`--front-warm`) | 29.9 | 38.1 | 63.4 |
| the piece before, same build: the kernel's later run and the copy again | 16.0 + 30.4 = 46.4 | 20.5 + 29.3 = 49.7 | 19.6 + 35.6 = 55.2 |
| T again on the same expressions; T's first run | 49.8; 101.7 | 51.8; 123.8 | 52.4; 186.7 |
| the lines the names' pass made; the rows | 2.7; 0.9 | 25.0; 8.9 | 42.4; 45.4 |
| the front, ticks a record of its own: with the window; without | 53.0; 44.2 | 70.5; 60.2 | 87.4; 78.1 |
| names; ticks a name in the pass | 97,472; 13.8 | 25,359; 17.7 | 198,354; 19.8 |

- **The front's rise is 8.8 to 10.3 ticks a record of the front's,** on the
  three corpora. It makes 1.09 records for each one T takes on `sqlite3.c`,
  1.19 on Lua and 2.76 on musl, whose units' tables it reads and T does not;
  the table charges all of it to the records T takes.
- **musl is above the copy.** The window's sum is 61.5 where the kernel with
  the copy is 55.2, and its readings spread from 55 to 67 (the build before
  this one read 68.2). The front writes the window for every record it makes,
  and the names' pass runs on every name, 0.88 of them a record T takes; the
  copy was made for the taken expressions alone. A third of musl's names are
  their declaration's first use in their unit (66,227 lines for 198,354
  names), and each of its 1,336 units starts with nothing in the cache.
- **The kernel reads the front's columns as fast as the batch's:** 15.6
  ticks a record in place, 16.0 on the batch in the same run.
- **A compile doesn't run it.** `-c sqlite3.c` retires the base's
  instructions to within 41 of 1,362.1 M with the piece in the executable.
  Its wall time reads -0.14% and -0.09% in two comparisons of 21 runs a
  build, where the base against itself reads +0.06% and +0.07%.
- With the census's tests and C and X beside it (one reading), the kernel
  makes 99.39%, 97.25% and 98.03% of the records that are no literal in the
  expressions the whole machine takes there, as in the piece before.

**Against the piece's gate** (every record the oracle's and every input both
twins'; every expression T takes there given by the front; 35 ticks a record
or less on `sqlite3.c` for the kernel with its inputs; refuted if the inputs
cost no less than the copy's 28.4):

- **Exact: met,** on the three corpora and the compiler's own source, with
  the tests and C and X beside it and without.
- **Every expression: met.** No waiting value is cut on the corpora. The
  compiler's own source has 1,151 cut, none of them in an expression T
  takes there. A text made to leave the rows no room has 64 expressions
  cut, and the tests pin them.
- **35 ticks: met on `sqlite3.c`, 29.9.** The inputs are 14.4 of it, the
  names' pass and the front's rise, where the copy took 30.4 in the same
  build. It was set on `sqlite3.c`; Lua reads 38.4 and musl 61.5.
- **Not refuted on `sqlite3.c` and Lua** (14.4 and 18.3 against the copy's
  30.4 and 29.3). **On musl the inputs cost more than the copy,** 42.9
  against 35.6, for the reason above.

The gate's sum is of later runs, as the piece before's was. As a compile
would run it, once, the lines the pass makes and the rows are in it: 3.6
ticks a record more on `sqlite3.c`, 33.9 on Lua and 87.8 on musl. The
kernel's first run in place was not timed; on the batch it reads 26.8, 47.1
and 77.1.

### What moved it, and what didn't

Each version is a build in the record. Ticks a record on `sqlite3.c`, medians
of three or five readings:

| | the window | the kernel | the window's making | the front's rise | sum |
| --- | --- | ---: | ---: | ---: | ---: |
| `a` | the front writes the columns; an expression's are appended as blocks, its leaves' lines and its calls' arguments made over the copy | 16.0 | 16.9 | 10.7 | 43.6 |
| `b` | … consecutive expressions appended as one run | 16.1 | 16.5 | 9.9 | 42.5 |
| `c` | … the leaves' lines found and made in the front's own pass | 16.0 | 8.8 | 23.8 | 48.5 |
| `d` | no append: the kernel reads the front's columns; the names' lines by a pass after the front's | 16.1 | 10.2 | 13.2 | 39.5 |
| `e` | … the pass by three sweeps, the line's number asked for where the leaf is made | 15.7 | 4.9 | 12.4 | 33.0 |
| `g` | … without a test of each operator's operands' rows that no text can fail | 15.7 | 4.9 | 9.3 | 29.9 |
| `h` | the commit: … a cut value's calls' arguments not kept; a literal's writer without two tests no text can fail | 15.6 | 4.6 | 9.7 | 29.9 |

- **An append costs by the expression, not by the record.** `a`'s append
  retires about 55 instructions a record, and `sqlite3.c`'s expressions are
  5.7 records each. Runs of consecutive expressions barely merge: a
  control's condition is in the front's arrays before its body and in the
  batch after it, so every `if` breaks a run twice.
- **A load that misses the cache is dear in the front's own pass.** With the
  names' lines found there (`c`), the front's rise went from 10.7 to 23.8
  ticks a record: 39 ticks a name, where the same loads in the append's loop
  were part of 24. The front retires some two hundred instructions a
  record, so the next name's loads are not in flight while this one's wait.
  The profile of `d`'s pass has its samples on the load of the line's number
  by the declaration.
- **Asking for a load before reading it halved the pass.** The front asks
  for the line's number as it makes the leaf, and the pass asks for each
  line in a sweep before the sweep that reads it: 30 ticks a name to 15.
- **The kernel needs no copy.** In place it reads a root's operand at its
  distance, whatever lies between, and makes the same records.
- **A test no input can fail was a third of the rise.** Each operator
  tested that its operands' records lie in a row before it. They do by the
  front's own order, and the three texts written to break it could not: the
  walk acts at no token within an expression whose value nothing takes.
  Without the test the rise went from 12.4 to 9.3.

### What stays scalar, and why

- **The front's pass** is the lean front's, a token at a time. The window's
  columns are written in it because that is where their values are: the
  piece before measured what reading them back costs.
- **A name's line** is two loads behind one another, by the declaration and
  by the line's number. The names are found by a vector compare and
  compress; each line is then read by itself. A gather of sixteen was not
  tried: on Zen 5 it costs what sixteen loads do (`docs/coding-style.md`).
- **A chunk's end** moves each kept row a record at a time. `sqlite3.c`'s
  710,061 tokens are 694 chunks, whose ends keep 10,258 records in all.

### What it means

- **The window is the front's to write, and it costs half the copy.** On
  `sqlite3.c` the kernel with its inputs is 29.9 ticks a record where T again
  is 49.8 and T's first run 101.7. With the words made as it goes (33.5) it
  is a third of T's first run, where the piece before reached three
  quarters.
- **What the front writes should be what the kernel reads, where it is
  written.** Every version with a stage between them paid for it by the
  expression. That is #2850's sixth item, measured from the other side.
- **Loads that miss belong in loops of their own.** The front's pass can
  write what it holds for 10 ticks a record, and nothing that waits on memory:
  a declaration's line cost three times as much there as in a sweep. For the
  design this is the leaves' loop of item 6, and it says where a
  declaration's fact word should live: one load from the leaf, where two cost
  15 ticks a name with both asked for ahead.
- **The cost is by the front's records.** A compile's kernel would make
  every record the front makes and refuse by expression, so musl's figure is
  what its units cost, not an artifact: 9 ticks a record of the front's for
  the columns, and 20 a name. The names are the larger part there, and they
  are the environment's (#2536): a small unit's names are nearly all first
  uses of its headers' declarations.
- **For step 2:** the tests where their inputs are come next, on these
  columns, and then C and X. **For step 3:** the flat front
  ([#2894](node-machine.md)) writing the same columns by passes is its next
  part; the lean front's 44 ticks a record of its own are now three times the
  window's cost.

## The tests on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-2850-2026-11-17).
`measure-node-machine --shadow --window-tests`
([the guide](commands/lowering.md#the-tests-on-the-window-2850)).

The set's and the silence tests decide what the machine takes. As a pass
over T's columns they cost 86 TSC ticks a typed record on `sqlite3.c`, more
than the kernel with its inputs. This piece runs them on the window, once
the kernel has made a chunk's records in place. [The node machine's
page](node-machine.md#the-tests-on-the-window) has it whole: what changed
part by part, the counts, and what moved it and what didn't.
[Its specification](https://github.com/buster14a/buster/issues/2850#issuecomment-6951406678)
had four parts. This is the first two, the inputs where the record is made
and the loop 64 records a step. A constant operand's tests in the kernel's
rule and a call's tests from the window are the piece after it.

### What it measured

The 9700X, CPU 2, under the lock, Release; main `9a1422eab` with the piece;
medians of seven readings in turn, musl's of three. TSC ticks a record of
the expressions the tests ran on:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the tests as a pass over T's columns, at the base | 83.0 | 112.4 | 158.4 |
| **the tests on the window** | **54.1** | **69.5** | **111.8** |
| of which the records' bytes | 8.9 | 9.8 | 16.6 |
| the loop | 0.7 | 0.9 | 1.9 |
| an expression's part | 7.2 | 7.7 | 13.6 |
| the tests that read T's records still | 37.2 | 51.1 | 78.8 |

### Against its gate

- **Exact: met.** Every expression's bits, root's kind and span of tokens
  are the census's code's, on the three corpora and the compiler's own
  source, with the default warnings and with every warning, and on
  `test-checker`'s fixtures.
- **No walk for the loop: met.** An expression is walked only where a
  record is sent to the census's code: 2.06%, 4.11% and 3.01% of them, the
  pass's own counts.
- **40 ticks a record on `sqlite3.c`: not met.** 54.1. It would have been
  refuted at 65.

### What stays scalar, and why

- **The tests that read T's records:** the tree's, the operators' and an
  expression's own, 37.2 of the 54.1. They are the piece after this one, on
  the kernel's records.
- **A declaration's line's byte's first making,** once a line, by the
  census's code: 18,295 on `sqlite3.c`.
- **An operand further back than the block before its record's:** 0.33% of
  the records, a load each.
- **A loop a record for the records' bytes was tried, and lost:** 14.2 ticks
  a record at its best where six gathers a step of sixteen read 9.4. (Four of
  the gathers went later without a loop a record: "The records' bytes without
  their four gathers".)

### What it means

- **The loop was not what its 27 ticks bought.** On byte columns it is 0.7.
  The pass's cost was reading each record's inputs where they were.
- **The inputs are now the loop's cost:** 8.9 ticks a record to read four
  fields out of each 64-byte record, a line's byte and a role. The kernel
  holds the word, the standing and the flags in registers when it writes a
  record. Having it write these bytes then was tried next, and lost: the
  bytes' pass read 6.9 and the kernel in place 19.5 for 15.6 ("The constants
  with no test to make").
- **Two thirds of what is left is the next piece's.** With the operators'
  tests on the kernel's records, T's second run in the shadow goes too, and
  the gate is read again.

### The constants with no test to make

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-second-half-2850-2026-11-17);
[the node machine's page](node-machine.md#the-constants-with-no-test-to-make)
has it whole.

The second half's first piece. It took the tests on T's records apart: half
is the operators' tests, and 27,241 of `sqlite3.c`'s 48,645 listed operators
are listed for a constant operand alone, 97% of them for a test that
concludes nothing. The loop now leaves such an operator out of its list
where its operands' kinds say so: two plain integers, a small constant, a
pointer against zero.

- **A record says its constant's value,** in the exponent only a floating
  value used, written where the record is made: no gather of the payload.
- **A kind a record and one table by the pair:** three bits a record as an
  operand, in a byte the tests had, and a 64-byte table by the two operands'
  kinds, one byte permute a step of 64 records.
- **Checked under `--verify`:** an operator left out has its tests run all
  the same and they find nothing; a value said is its payload's.

The 9700X, CPU 2, under the lock, Release; main `aa09d1a04` and the piece in
turn; medians of seven readings, musl's of fifteen (with every warning of
five, musl's of fifteen). TSC ticks a record of the expressions the tests
ran on:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the tests on the window, main | 54.7 | 70.0 | 111.9 |
| **with the rule** | **48.7** | **63.2** | **108.7** |
| of which the tests that read T's records | 31.0 | 43.9 | 75.4 |
| operators the loop lists, main | 48,645 | 16,513 | 36,260 |
| with the rule | 27,750 | 9,486 | 21,783 |
| with every warning, main | 73.0 | 81.7 | 131.1 |
| with the rule | 70.1 | 76.9 | 130.4 |

- **Exact,** on the three corpora and the compiler's own source, with the
  default warnings and with every warning.
- **The gate (40 on `sqlite3.c`) is 9 away.** What is left on T's records:
  the calls' tests, an expression's fixed part, the tree's tests, the root's
  own, and T's second run's residue.
- **With every warning it saves 2.9, 4.8 and 0.6.** `-Wsign-compare` and
  `-Wself-assign` list every comparison and assignment whatever its
  operands are, and on `sqlite3.c` the rule then leaves out half as many
  operators.
- **musl is read with its spread:** one build's fifteen readings spread 11%
  to 16% there, and three builds of this form read 0% to 3% under main.
- **What stays scalar:** the table's making, 64 entries a unit.

### The operator above, and an expression with nothing listed

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-listing-2850-2026-11-18);
[the node machine's page](node-machine.md#the-operator-above-and-an-expression-with-nothing-listed)
has it whole.

The second half's second piece takes two things out of the tests that read
T's records.

- **The tree's tests' list.** A test of the parser's tree reads its record's
  token's parent's role, and the loop listed every record one reads: 27,794
  on `sqlite3.c`. A test fires only where that parent is the record's
  consumer's token, so the loop lists the consumer, by what its operands
  are: 6 operators. The role's gather in the records' bytes' pass goes with
  the list.
- **Reading the parents' roles was tried first, and bought nothing:**
  sixteen candidates a step, two gathers each: 48.5 where the piece before
  read 48.8, the bytes 2.0 up for 2.1 off the tests.
- **The call itself.** An expression with no operator in a list, no write
  out of sequence, no root with tests of its own and no warning of T's
  actions has its verdict from the window alone: 21,157 of `sqlite3.c`'s
  50,993, 28% of Lua's and 51% of musl's. Neither T nor the tests run on it.
- **Checked under `--verify`:** a record the tree's tests read that no
  listed operator has as an operand has its test run all the same, with
  nothing; an expression with nothing listed is run through the tests all
  the same, with the same verdict.

The 9700X, CPU 2, under the lock, Release; the piece before and this one in
turn; medians of seven readings, musl's of fifteen (with every warning of
five, musl's of fifteen). TSC ticks a record of the expressions the tests
ran on:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the tests on the window, before | 48.9 | 63.3 | 111.3 |
| **with the piece** | **42.0** | **59.1** | **100.0** |
| of which the tests that read T's records | 23.2 | 38.6 | 61.9 |
| an expression's part, before | 7.4 | 7.7 | 13.9 |
| with the piece | 9.5 | 9.9 | 16.7 |
| with every warning, before | 70.2 | 76.6 | 128.8 |
| with the piece | 67.3 | 74.3 | 122.3 |

- **Exact,** on the three corpora and the compiler's own source, with the
  default warnings and with every warning.
- **The gate (40 on `sqlite3.c`, refuted at 44) is neither met nor
  refuted:** 42.0. What is left on T's records is the calls' tests first:
  12,918 of the 27,750 operators still listed.
- **The listing costs every expression, and pays at two in five.** An
  expression's part rises by its listing and its verdict; with every
  warning a sixth of `sqlite3.c`'s expressions have nothing listed, and the
  piece saves less.
- **What stays scalar:** an expression's listing and verdict, one
  expression at a time, four loads at its root.

### The calls with no test to make, and a statement that discards nothing

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-calls-2850-2026-11-18);
[the node machine's page](node-machine.md#the-calls-with-no-test-to-make-and-a-statement-that-discards-nothing)
has it whole.

The second half's third piece takes the calls out of the tests that read
T's records, and the statements listed for their top alone.

- **A call was listed whatever it was,** and 99.9%, 95% and 99.5% of the
  calls' tests found nothing on `sqlite3.c`, Lua and musl. The loop leaves a
  call out where its type is no aggregate's, its callee gives a call no
  semantics of its own, and its arguments have no aggregate and no constant
  but an address's or an integer's of zero or 1 to 126 that each parameter
  takes unchanged: 12,064 of `sqlite3.c`'s 12,918 listed calls.
- **Each input is made where its record is read already.** The arguments'
  part is three ORs an argument in the kernel's call rule, and a bit of the
  call's record; the parameters' part a bit of the callee's type's row; the
  callee's part a bit of its declaration's line's byte.
- **A statement that discards nothing isn't listed.** `sqlite3.c`'s
  `assert`s are `((void)0)`, and 5,667 statements were listed for a cast to
  void alone. A statement is now listed for its top where that is no write,
  no cast to void and no call left out whose callee asks for no result.
- **Whether a callee asks for its result is asked of few.** The loop
  compresses the casts to void over a call left out into a list, and one counted
  pass reads each one's callee's line's byte: 6,145 casts on `sqlite3.c`,
  1,144 declarations asked.
- **As specified it cost more than it saved off `sqlite3.c`,** with each
  callee's facts made at its line's first leaf and each loud argument
  marking its consumer: 37.8 for 41.7 there, 59.7 for 58.4 on Lua and 112.5
  for 96.6 on musl. One executable with each input behind a switch showed
  which, and each moved to where its record is read.

The 9700X, CPU 2, under the lock, Release; main `8d4025fbf` and the piece on
it in turn; medians of seven readings, musl's of fifteen (with every warning
of five, musl's of fifteen). TSC ticks a record of the expressions the tests
ran on:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the tests on the window, before | 41.8 | 58.0 | 98.0 |
| **with the piece** | **35.8** | **53.0** | **96.4** |
| of which the tests that read T's records, before | 23.1 | 37.8 | 63.1 |
| with the piece | 15.1 | 30.2 | 55.9 |
| the records' bytes and the loop, before | 9.4 | 10.6 | 18.5 |
| with the piece | 10.9 | 13.0 | 23.0 |
| with every warning, before | 67.7 | 73.9 | 120.6 |
| with the piece | 63.8 | 70.8 | 121.1 |
| expressions with nothing listed, before | 41% | 28% | 51% |
| with the piece | 69% | 53% | 62% |

- **Exact,** on the three corpora and the compiler's own source, with the
  default warnings and with every warning. The compiler's own source had
  no call at reader time, before this piece as with it
  ([#2968](https://github.com/buster14a/buster/issues/2968): one
  `no_builtin` function, and the fronts left every call of such a unit).
  With that lifted it has 12,484 calls left out, each checked, and no
  expression differs.
- **The gate (40.0 on `sqlite3.c`, refuted at 41.0) is met.** Its inputs'
  line (a tick a record) is not: the records' bytes and the loop read 1.5
  more, and the kernel in place 0.8 more (16.4 for 15.6), where its call
  rule gathers the arguments' part.
- **In one executable, the rule on and off** (its inputs made either way):
  35.8 for 44.5 on `sqlite3.c`, 54.3 for 62.5 on Lua, 102.5 for 111.4 on
  musl.
- **The front with the window read a third more from this build's file**
  (71.7 for 52.8 on `sqlite3.c`) **and main's from fresh copies of it,**
  and from the file once its pages were dropped from the page cache: one
  file's pages, not the build
  ([#2967](https://github.com/buster14a/buster/issues/2967)).
- **musl gains least:** its 43,415 expressions come in 2,975 chunks of two
  blocks, each after another unit's compile, and each sweep reads twice
  `sqlite3.c`'s ticks a record there.
- **What stays scalar:** the asks, a chain of five loads a cast, 2% of
  `sqlite3.c`'s records; and the declaration's fact, once a line.
- **What is listed still** is casts first (7,370 of `sqlite3.c`'s 15,688
  listed operators, each with a test that finds nothing), subscripts by a
  constant (3,181), and an initialization's root (3,310 expressions listed
  for it alone).

### The casts, a pointer's subscripts and its sums

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/window-tests-casts-2850-2026-11-18);
[the node machine's page](node-machine.md#the-casts-a-pointers-subscripts-and-its-sums)
has it whole.

The second half's fourth piece takes three kinds of operator out of the
tests that read T's records, each sent there by a pointer for a test that
finds nothing.

- **Half of `sqlite3.c`'s listed operators were casts** (7,370 of 15,688),
  none with a test that finds anything on any corpus. Most are a
  statement's discard, a cast to void, over a top of a pointer's type
  (`p = q;`): the pointer sent it to the pointer cast's test, which asks
  whether the cast's own type is an integer narrower than a pointer.
- **A cast is left out where a pointer sent it and its own type is void or
  a pointer,** by its type's byte. Nothing sends a cast for an enumeration
  operand any more: the enumerations' test reads two operands.
- **A plain pointer's subscript by a constant and its sum with one are the
  quiet rule's table's:** the integer is promoted, which narrows none. A
  subscript whose pointer is a constant a test may read stays listed, for
  its null dereference's test; an array's does too, for its bound.
- **No new byte, no new sweep, nothing scalar:** two pairs of kinds in the
  table, one operator's class, and two masks in the loop.
- **The counts were predicted before the code,** from a census of each
  listed operator put through the three rules, and came out the same to the
  expression.

The 9700X, CPU 2, under the lock, Release; #2973's head `cc9c84a54` and the
piece on it in turn; medians of seven readings, musl's of fifteen (with
every warning of five, musl's of fifteen). TSC ticks a record of the
expressions the tests ran on:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| the tests on the window, before | 35.5 | 53.4 | 99.9 |
| **with the piece** | **31.2** | **49.4** | **95.8** |
| of which the tests that read T's records, before | 15.0 | 30.6 | 56.7 |
| with the piece | 10.8 | 26.5 | 52.7 |
| the records' bytes and the loop, before | 10.9 | 13.1 | 24.0 |
| with the piece | 10.9 | 13.2 | 24.4 |
| with every warning, before | 63.4 | 71.1 | 126.9 |
| with the piece | 59.7 | 67.2 | 123.0 |
| expressions with nothing listed, before | 69% | 53% | 62% |
| with the piece | 81% | 64% | 66% |

- **Exact,** on the three corpora and the compiler's own source, with the
  default warnings and with every warning; every operator left out has its
  test run beside `--verify`, and none finds anything.
- **The gate on `sqlite3.c` (31.0, refuted at 32.0) is missed by 0.2,** at
  31.15 and 31.18 in two builds; Lua's (50.0) is met at 49.4 and 49.0. The
  counts' gate is met (9,540, 3,932, 14,546 and 26,250 expressions listed).
- **The tests on T's records fell by less than their records:** 45% of
  `sqlite3.c`'s listed records left the list and 28% of the sweep's ticks.
  Two of five expressions still listed there are initializations.
- **What is listed still** is an initialization's root first (3,439 of
  `sqlite3.c`'s 9,540 listed expressions for it alone, 13,027 of the
  compiler's own 26,250), then an assignment of a constant (1,250, none
  with a test that finds anything), an array's subscript by a constant
  (893) and the calls (854).

## C on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/c-on-the-window-2850-2026-11-18).
`measure-node-machine --shadow --window-contexts`
([the guide](commands/lowering.md#c-on-the-window-2850)).

C gives every evaluated record its template and its operands their contexts
and targets, a record at a time from each root down, over T's 25 columns.
This piece makes its answers on the window, once the kernel has made a
chunk's records in place and the tests' sweep their bytes: 64 records a
step, for the expressions with no call, no `&&`, `||` or `?:`, no update and
no comma. C on T's columns decides the others as before, and is the
reference. No compile takes any of it. [The node machine's
page](node-machine.md#c-on-the-window) has it whole: the census it rests on,
the four sweeps, the ten builds, and what exactness took. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-6983596165)
had four gates.

### What it measured

The 9700X, CPU 2, under the lock, Release; main `521d5b63d` and the piece on
it in turn; medians of seven readings, musl's of fifteen. TSC ticks:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions the kernel placed and C ran on | 50,996 | 10,839 | 43,415 |
| **decided on the window** | **63%** | **52%** | **71%** |
| **C's three sweeps, a record of the decided expressions** | **12.6** | **19.3** | **37.0** |
| with the bytes' five columns: the gate's figure | 13.9 | 22.0 | 43.9 |
| the reference's C on the same expressions | 40.2 | 36.2 | 43.0 |
| C's three sweeps, a record they ran over | 5.44 | 6.77 | 10.96 |
| of which the loop | 2.44 | 3.82 | 6.75 |
| the targets | 1.24 | 1.43 | 2.20 |
| an expression's part | 1.75 | 1.51 | 2.05 |
| the same three sweeps again over the chunk | 4.60 | 4.52 | 5.09 |

### Against its gates

- **Exact: met.** No expression's verdict and no record's template, context
  or target differs from C's on T's columns: the three corpora and the
  compiler's own source, with the default warnings and with every warning,
  `test-checker`'s fixtures, and 266 statements run one at a time. The
  comparison itself is tested: the fixture runs again with one of the
  reference's answers changed for each expression, and it has to count
  every one.
- **The counts: met.** 32,300, 5,644, 30,863 and 24,659 expressions decided
  (the compiler's own source last), for 32,000, 5,500, 30,000 and 24,000.
- **Under 10.0 ticks a record of the decided on `sqlite3.c`: not met.**
  13.9 (13.8 to 14.5; 14.2 in the three builds before). It was to be
  refuted at 15.0, and the first five builds read 15.2 to 17.2.
- **What is not the piece's reads the same: met but for the front.** The
  tests on the window, the kernel in place and `-c sqlite3.c` read main's;
  the front with the window reads 0.4, -0.1 and 0.9 ticks a record more
  (up to 0.4, 1.2 and 3.9 over the builds), the forward distance's store.

### What stays scalar, and why

- **A pointer's stride,** its type's row's: 6,965, 1,074 and 3,850 records,
  2% of those swept or fewer, where the step's other bytes are masks.
- **C's rule on T's two conversions,** for a sum or a binary operator whose
  operands' own classes don't say what their conversions' are (a 128-bit
  integer beside a double): none on `sqlite3.c` and Lua, 128 records on
  musl.
- **A consumer past the block after and a first operand before the block
  before:** 1,356, 192 and 351 loads. A control's root far from its
  condition is most.
- **The records under one that isn't evaluated,** by its subtree's size.
- **An expression's part,** two ranges of a block's masks an expression:
  1.75 ticks a record swept on `sqlite3.c`, a third of the sweeps.

### What it means

- **Exact on every corpus was not exact.** The sixth build had dropped the
  terms a fault campaign's survivors read as dead, and still matched C on
  the three corpora, the compiler's own source and the fixture. A cast to
  an atomic type, a half-precision wrapper and a 128-bit integer beside a
  double are in none of them. Listing is always safe; a refusal has to be
  C's own, so the loop now has each of C's terms, tested one statement at
  a time.
- **C's answers cost a third of C's on `sqlite3.c`** and nine tenths of C's
  on musl, where a chunk's first run is twice its second.
- **Two thirds of what is listed is a call's** (12,179 of `sqlite3.c`'s
  18,696): an argument's target is its conversion's type, and the kernel's
  call rule makes none of a call's arguments' conversions, which are T's
  still. That, the updates and the branches are the pieces after this, and
  then X on these answers: until X reads them the sum of step 2 does not
  move.

## A call on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/a-call-on-the-window-2850-2026-11-19).
`measure-node-machine --shadow --window-contexts`
([the guide](commands/lowering.md#a-calls-arguments-in-the-kernel-and-cs-call-2850)).

Two thirds of what C on the window listed was a call's. An argument's target
in C is its conversion's type, and the kernel's call rule made the call's
own record and none of its arguments' conversions. This piece makes them in
the kernel, each in its argument's record, and C's answers for a call on the
window from them. No compile takes any of it. [The node machine's
page](node-machine.md#a-call-on-the-window) has it whole: the arguments'
census, the rule and its slots, the builds, the probes and the faults. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-6995139458)
had seven gates, three for the kernel's part and four for C's.

### What it measured

The 9700X, CPU 2, under the lock, Release; main `bc66290c5` and the piece on
it in turn; medians of seven readings, musl's of fifteen. TSC ticks:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| calls with every argument's conversion made in the kernel | 99.57% | 98.78% | 98.01% |
| **expressions decided on the window: before, with** | **63%, 86%** | **52%, 84%** | **71%, 86%** |
| **the kernel in place, a record: before, with** | **16.31, 17.52** | **20.69, 22.36** | **18.89, 20.36** |
| **C's three sweeps, a record of the decided: before, with** | **12.47, 8.17** | **18.88, 11.68** | **34.09, 26.55** |
| with the bytes' columns, the gate's figure | 9.18 | 13.42 | 31.20 |
| the reference's C on the same expressions | 38.5 | 36.6 | 40.6 |
| C's sweeps, a record they ran over: before, with | 5.38, 5.79 | 6.63, 7.14 | 10.09, 12.16 |

### Against its gates

- **Exact: met**, for the kernel's records and conversions and for C's
  answers: the three corpora and the compiler's own source, with the default
  warnings and with every warning, `test-checker`'s fixtures, and 608
  statements run one at a time.
- **The kernel keeps its calls: met.** Every argument's conversion is made
  for 99.57%, 98.78%, 98.01% and 83.84% of the calls (the compiler's own
  source last; 98, 95, 97 and 80 asked), and 0, 0, 25 and 0 calls are left
  to T for an argument.
- **The kernel's cost: not met.** At most 1.0 tick a record over main on
  `sqlite3.c` was asked, to be refuted at 2.0: it reads 17.52 for
  main's 16.31, 1.21 over (each reading's own difference 0.96 to 1.27), and
  1.69 over with C off.
- **Decided: met.** 43,876, 9,135, 37,304 and 32,853 expressions, for
  43,000, 8,800, 36,500 and 32,000.
- **C's cost: met.** 9.18 ticks a record of the decided on `sqlite3.c`
  with the bytes' columns (10.0 asked, refuted at 12.0; 13.9 before).
- **Nothing else moves: met on `sqlite3.c` and Lua, not on musl.** The
  tests on the window read 31.4 and 31.1, and 49.7 and 49.0, and the front
  with the window the same within 0.3. On musl the tests on the window
  read 5 to 7 ticks a record more in the piece's executable (91.6 and
  96.6). One build with a switch reads them no lower with the arguments'
  pass, its tables' clearing and the types' answers off (95.8 to 97.5)
  than with them on (94.9 and 98.8), so it is the executable's and not
  what the piece runs; what in the executable is not established.

### What stays scalar, and why

- **The arguments' loop:** an argument's key and its slot are eight loads
  at computed addresses and a store, one argument after another, about
  10 ticks an argument. By gathers they would be nine 8-lane gathers and a
  scatter for eight arguments, and a gather's lane costs about what a load
  does here (C's targets' sweep, a gather and a store, reads 1.2 to 1.4
  ticks a record). It was not built.
- **The rule,** on a miss of the slots: 1,146 keys on `sqlite3.c` for
  33,150 arguments.

### What it means

- **Six in seven expressions the kernel places have C's answers from the
  window on `sqlite3.c`**, at 8.2 ticks a record of theirs where C on
  T's columns reads 38.5.
- **The kernel pays a tick a record for its arguments,** over its gate. A
  unit's first answers are not in that figure: 0.4, 1.8 and 4.6 ticks a record of
  the window more, which the shadow's timed run, its slots filled, does not
  have.
- **Exact on every corpus was not exact, again.** A fault round's survivor
  read as a dead term: the mark of a call the loop leaves. Without it
  `sqlite3.c`, Lua, musl and the fixture still matched, and
  `fi(two(1e100, b));` did not: the two calls are in one batch, and the outer one was made from the
  kernel's record of the inner. Calls run one statement at a time found
  it, as they had found the two calls main made otherwise than T.
- **The probes found two things that are not this piece's:** a zero that is
  no literal passes the reader-time tests as a null pointer constant
  ([#3012](https://github.com/buster14a/buster/issues/3012)), and an
  integer constant made of a floating one says its exponent as its value
  ([#3014](https://github.com/buster14a/buster/issues/3014)). Both are
  fixed since, with a third the first one's rows showed
  ([#3017](https://github.com/buster14a/buster/issues/3017)):
  [the node machine's document](node-machine.md#a-null-pointer-constant-that-is-no-literal)
  has what each was, and that a call's quiet arguments now have none that
  may be such a constant to a pointer's parameter.
- **What is listed still** is an update (3,535 expressions on `sqlite3.c`)
  and `&&`, `||` and `?:` (2,721). Those, then X on these answers: until X
  reads them the sum of step 2 does not move.

## An update on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/an-update-on-the-window-2850-2026-11-19).
`measure-node-machine --shadow --window-contexts`
([the guide](commands/lowering.md#an-update-on-the-window-2850)).

The largest part of what C on the window still listed was an update's:
3,535 of 7,120 expressions on `sqlite3.c`. This piece makes C's answers for
`++`, `--` and the compound assignments on the window, by the loop's masks.
No compile takes any of it. [The node machine's
page](node-machine.md#an-update-on-the-window) has it whole: C's plan term by
term, the form that was refuted, the builds, the switch, the probes and the
faults. [Its specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7011006204)
had four gates.

### What it measured

The 9700X, CPU 2, under the lock, Release; the build before (#3023's code)
and the piece in turn; medians of seven readings, musl's of fifteen. TSC
ticks:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| updates the masks take, of those C asks its plan of | 3,734 of 3,736 | 861 of 861 | 3,969 of 3,969 |
| **expressions decided on the window: before, with** | **86.04%, 92.69%** | **84.28%, 91.12%** | **85.92%, 94.29%** |
| **C's sweeps with the records' bytes, a record of the decided: before, with** | **9.12, 8.90** | **12.93, 13.39** | 30.42, 27.54 |
| the reference's C on the same expressions | 38.4 | 36.8 | 41.0 |
| the loop, a record swept: before, with | 2.66, 2.97 | 3.98, 4.77 | 8.03, 8.78 |
| one executable, the update's block on less off | +0.19 | +0.26 | +0.14 |
| the kernel in place, a record: before, with | 17.56, 17.41 | 22.43, 22.43 | 20.49, 20.37 |

### Against its gates

- **Exact: met.** The fixtures, 250 statements run one at a time, and the
  three corpora and the compiler's own source with the default warnings and
  with every warning.
- **Decided: met.** 47,266, 9,877, 40,937 and 38,020 expressions on
  `sqlite3.c`, Lua, musl and the compiler's own source, for 47,100, 9,800,
  40,700 and 38,000 asked.
- **C's cost: met on `sqlite3.c`, at its limit on Lua.** 8.90 ticks a record
  of the decided for 9.1 asked; 13.39 for 13.3, below where it is refuted
  (13.8).
- **Nothing else moves: met.** The kernel in place and the tests on the
  window read what they did.

### What it means

- **Nine in ten expressions the kernel places have C's answers from the
  window** on each corpus, at 8 to 12 ticks a record of theirs on
  `sqlite3.c` and Lua where C on T's columns reads 37 to 38.
- **A rule a record was refuted, and masks were not.** The specified form
  asked C's plan of each update by a rule: 75 ticks an update on
  `sqlite3.c`, 10.14 and 14.60 at the gate for 9.1 and 13.3. The masks read
  the same terms from bytes the loop has, and three new bits.
- **Two builds did not measure the piece.** Build against build the loop
  rose 0.79 ticks a record on Lua. One executable with the update's block on
  and off reads 0.26, and with the block off it still reads half a tick over
  the build before. What that half tick is was not established; the piece's
  work is the 0.26.
- **Most of the first masks' terms were not needed.** Trimming and three
  rounds of faults left eight vector operations a step of the first masks'
  nineteen: a term of C's plan that no accepted program separates is the
  checker's, the front's or another byte's already, and a fault in it
  survives.
- **What is listed still** is `&&`, `||` and `?:`: 2,744 of 3,730
  expressions on `sqlite3.c`. Those, a comma, then X on these answers: until
  X reads them the sum of step 2 does not move.

## A condition's branches on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/a-conditions-branches-on-the-window-2850-2026-11-19).
`measure-node-machine --shadow --window-contexts`
([the guide](commands/lowering.md#a-conditions-branches-on-the-window-2850)).

After the updates, `&&`, `||` and `?:` were most of what C on the window
still listed: 2,744 of 3,730 expressions on `sqlite3.c`. They are two cases
in C, and this piece is the first: a statement's CONDITION over `&&`, `||`
and `!`, which C evaluates as branches and no value. No compile takes any of
it. [The node machine's page](node-machine.md#a-conditions-branches-on-the-window)
has it whole: C's rule term by term, the switch, the builds, the loop's
frame, the probes and the faults. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7016320819)
had four gates.

### What it measured

The 9700X, CPU 2, under the lock, Release, TSC ticks; one executable with the
piece's block on and off, each of seven readings' own difference, and the
build before (#3025's code) against the piece's, medians of seven readings
(musl's of fifteen):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records evaluated as branches | 5,613 | 570 | 2,364 |
| **expressions decided on the window: before, with** | **92.69%, 96.70%** | **91.12%, 92.99%** | **94.29%, 96.19%** |
| **one executable, the loop a record swept: on less off** | **+0.24** | **+0.27** | -0.12 |
| **C's three sweeps, a record of the decided: on less off** | **-0.36** | **-0.20** | -1.91 |
| build against build, C's sweeps with the records' bytes, a record of the decided: before, with | 8.86, 8.38 | 13.55, 13.78 | 29.58, 30.72 |
| the reference's C on the same expressions | 38.7 | 37.2 | 41.4 |
| the kernel in place, a record: before, with | 17.66, 17.37 | 22.40, 22.40 | 20.50, 20.39 |

### Against its gates

- **Exact: met.** The fixtures, 257 statements run one at a time, and the
  three corpora and the compiler's own source with the default warnings and
  with every warning.
- **Decided: met.** 49,313, 10,079, 41,759 and 41,314 expressions on
  `sqlite3.c`, Lua, musl and the compiler's own source, for 49,200, 10,050,
  41,650 and 40,900 asked.
- **Cost, by one executable: met.** The block lowers C's sweeps a record of
  the decided on `sqlite3.c` and Lua, and the loop reads 0.24 and 0.27 ticks
  a record swept more, for 0.5 asked.
- **Nothing else moves: met.** The kernel in place and the tests on the
  window read what they did, on and off.

### What it means

- **Nineteen in twenty expressions the kernel places on `sqlite3.c` and musl
  have C's answers from the window**, and thirteen in fourteen on Lua, at 8 to
  14 ticks a record of theirs on `sqlite3.c` and Lua where C on T's columns
  reads 37 to 39.
- **One level of bytes was enough.** C branches a record from the root down.
  Each record says it of itself from its own bytes, its first operand's and
  its consumer's class, because whatever is not branched is a value, and a
  value lists its expression here: a provisional answer is read by no one.
- **Implied terms asked no mask again.** The int's class of a `!`, an `&&`
  and an `||`, a truth's own class and its operand's class's bounds are the
  checker's or another record's: the piece is fourteen vector operations a
  step, and a truth's operand's test two fewer than it was.
- **The loop's frame is the next cost.** The piece's build has 73
  instructions a step more than the one before, 37 of them frame traffic,
  and 256 of a step's 819 instructions have an operand in the frame. On Lua
  the switch's executable reads the loop above the build before with the
  block skipped, 5.03 for 4.82, as the update's did.
- **The probes found a fault that is not this piece's:** a condition on an
  lvalue at an absolute address was compiled as true
  ([#3028](https://github.com/buster14a/buster/issues/3028), fixed in the
  checker's truth rule). The loop lists such an expression still.
- **What is listed still** is `&&` and `||` as values and `?:`: 674 of 1,683
  expressions on `sqlite3.c`. Those, a comma, then X on these answers: until
  X reads them the sum of step 2 does not move.

## The records' bytes without their four gathers

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/the-records-bytes-without-gathers-2850-2026-11-19).
`measure-node-machine --shadow --window-tests` and `--window-contexts`
([the guide](commands/lowering.md#the-records-bytes-columns-2850)).

The sweep that makes the byte columns the tests' loop and C's read took four
columns of a chunk's records, sixteen a step, by four gathers: 64 uops each
on Zen 5. It takes them now by sixteen loads of the lines' first halves and
twelve permutes. The same bytes; no compile takes any of it. [The node
machine's page](node-machine.md#the-records-bytes-without-their-four-gathers)
has it whole. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7020847483)
had four gates.

### What it measured

The 9700X, CPU 2, under the lock, Release, TSC ticks a record, seven
readings: the build before (#3030's code) against the piece's, and one
executable that reads the columns either way, each reading's own difference:

| the records' bytes | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| build against build: before, with | 9.16, 7.58 | 10.78, 9.33 | 19.67, 19.48 |
| **one executable: loads less gathers** | **-2.41** | **-2.35** | **-3.00** |
| the tests on the window, their sum, by the same | -2.52 | -2.26 | -1.58 |

### Against its gates

- **S1, the same bytes: met.** No pin moves, and the shadow's counts are the
  build before's on every corpus.
- **S2, cost: met.** 1.65 and 1.49 ticks a record lower on `sqlite3.c` and
  Lua build against build, 2.41 and 2.35 by the one executable.
- **S3, nothing else moves: met.** By the one executable each other part is
  within 0.1 of itself. Build against build an expression's part reads 0.27
  and 0.34 higher, and the tests' sum 1.23 and 0.50 lower.
- **S4, faults: met.** 37 of 37 caught, three of them only once the reader's
  verify counted a record the kernel didn't place that has a byte.

### What it means

- **A gather of a column is no gather.** Where the lanes' indices are a fixed
  stride, the loads are sixteen and the rest is permutes in registers: a
  quarter of the sweep on `sqlite3.c`. "A loop a record ... was tried, and
  lost" (above) compared the gathers with a scalar loop, not with this.
- **The sweep's one gather left** is of bytes at entries' numbers, which no
  permute replaces.

## X's rows on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/x-rows-on-the-window-2850-2026-11-19).
`measure-node-machine --shadow --window-rows`
([the guide](commands/lowering.md#xs-rows-on-the-window-2850)).

C's answers on the window had no reader there: X, which writes each
evaluated record's entries, ran on T's columns still. This piece makes the
first of X on the window: the kinds and the classes of a record's own
entries and of its conversion's, for the expressions with no update and no
operator evaluated as branches, 64 records a step. An entry's value, an
update's rows and the branches' are next. No compile takes any of it. [The
node machine's page](node-machine.md#xs-rows-on-the-window-first-their-shapes)
has it whole: the shapes, the three sweeps, the builds, the probes and the
faults. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7023470094)
had five gates.

### What it measured

The 9700X, CPU 2, under the lock, Release, TSC ticks, seven readings; one
executable with the rows on and off, on, off, off, on in each reading:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C takes on the window | 49,155 | 9,909 | 39,712 |
| their tapes made here | 43,719 (88.94%) | 8,961 (90.43%) | 35,194 (88.62%) |
| tapes that differ from X's on T's columns | 0 | 0 | 0 |
| **the three sweeps and the part, a record swept** | **4.11 (4.02 to 4.23)** | **5.17 (5.09 to 5.33)** | **10.54 (9.22 to 13.20)** |
| a second run over the same chunk | 3.29 | 3.14 | 4.04 |
| a record of the taken expressions | 5.84 | 8.66 | 24.68 |
| X on T's columns, a record of theirs, with its values | 24.34 | 22.99 | 27.54 |

### Against its gates

- **R1, exact: met.** No tape made differs from X's on T's columns in its
  number of entries, a kind or a class, on the three corpora and the
  compiler's own source.
- **R2, how much: met on `sqlite3.c` and Lua, not on musl.** 88.66% and
  88.91% of the expressions C decides have their tape made here (85% asked),
  and 84.28% on musl, where C refuses 2,047 of the 41,759 it decides.
- **R3, cost: met on `sqlite3.c`, not on Lua.** 4.11 and 5.17 ticks a record
  swept (5.0 asked, refuted over 8.0).
- **R4, nothing else moves: met, but for the records' bytes on Lua.** The
  kernel, the tests' other parts and C's sweeps read within 0.12 with the
  rows on; the records' bytes' sweep, which makes the types' column for
  them, +0.15 and +0.35 (0.3 asked).
- **R5, faults: met.** Four rounds' survivors (22) each took a term out or
  put a line in the fixture; the fifth round's 167 faults, on the code as it
  is, are all caught.

### What stays a record at a time, and why

The reads' two loops, for 24.6% of `sqlite3.c`'s records swept, 20.3% of
Lua's and 14.2% of musl's. An iteration is a load
by a type's or an object's number, or of a record's line, and a store of
bytes, with no branch but the loop's. A gather of sixteen such loads is 64
uops on Zen 5, what sixteen loads are, and its results would be stored to the
same bytes. What the vector does there is the lists: the masks that say which
records to read for, compressed, 64 records a step.

### What it means

- **X's first two parts are shapes.** A record's own entries and its
  conversion's are each a byte, from masks over C's answers; four lookups a
  part give 64 records' kinds and classes, and one compress joins the parts
  and the rows. No walk up a tree, no dispatch a record, no entry written a
  record at a time.
- **Bytes stored one at a time are not read back as a register at once.**
  The first build's loops stored a block's bytes and its vector part read
  them: 4.35 ticks a record, and 0.80 with the reads a sweep before. A loop a
  block mispredicted its end; a loop a chunk over a compressed list does not.
- **A rule's term that no record separates is no mask.** The fault rounds
  and 241 one-statement programs took the shapes' sweep from the rule's
  terms one by one to those a C program reaches on the window: the shapes'
  sweep read 0.88 ticks a record on `sqlite3.c` before the rounds and reads
  0.62. And the corpora decide what a program reaches: one term went out on a reading
  of the code and thirty-one probes, five of musl's tapes differed, and it
  went back with its line (`a = b = 0`).
- **A chunk's first run reads more than its second.** Run again right after,
  the same chunk reads 3.29 ticks a record on `sqlite3.c` and 3.14 on Lua,
  for 4.11 and 5.17 the first time. What the first run reads more is not
  taken apart yet; Lua's chunks are 379 records for `sqlite3.c`'s 526.
- **Until the values and the two listed kinds are made, the sum of step 2
  does not move.** What X on T's columns spends on the taken expressions is
  24.34 ticks a record of theirs on `sqlite3.c`, 22.99 on Lua and 27.54 on
  musl, with its values; the rows here are 5.84, 8.66 and 24.68.
  Next: an update's rows, the branches', then the entries' values.


## An update's rows on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/x-update-rows-2850-2026-11-20).
`measure-node-machine --shadow --window-rows`
([the guide](commands/lowering.md#xs-rows-on-the-window-2850)).

X's rows on the window listed every expression with an update: 3,394 of
`sqlite3.c`'s 49,155 that C takes there. This piece makes an update's two rows
(the old value's read after its destination's row, and the operation, the
conversion back and the store at its own record) as shapes of the rows'
table, in the slots its two records have free, written over the shapes'
sweep's by a sweep and a loop of their own. No compile takes any of it. [The
node machine's page](node-machine.md#an-updates-rows-on-the-window) has it
whole: the rows, the six builds, the probes and the faults. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7030649938)
had five gates.

### What it measured

The 9700X, CPU 2, under the lock, Release, TSC ticks, seven readings; in each
one executable with the update's rows on and off (an experiment's switch),
and the piece before's executable between them:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C takes on the window | 49,155 | 9,909 | 39,712 |
| their tapes made here | 47,027 (95.67%) | 9,689 (97.78%) | 38,705 (97.46%) |
| the piece before's | 43,719 (88.94%) | 8,961 (90.43%) | 35,194 (88.62%) |
| tapes that differ from X's on T's columns | 0 | 0 | 0 |
| **the rows' sweeps and the part, a record swept** | **4.71 (4.61 to 4.93)** | **6.11 (5.99 to 8.12)** | **11.97 (9.87 to 14.39)** |
| the piece before's executable, in the same readings | 4.12 | 5.15 | 10.59 |
| **more than the piece before's** | **+0.67 (+0.47 to +0.81)** | **+0.94 (+0.87 to +1.99)** | **+1.27 (-1.11 to +4.23)** |
| the updates; with none to make | 0.52; 0.11 | 0.81; 0.27 | 1.55; 0.69 |
| a second run over the same chunk; the piece before's | 3.81; 3.29 | 3.54; 3.15 | 4.70; 4.04 |
| a record of the taken expressions | 6.22 | 9.51 | 25.20 |
| X on T's columns, a record of theirs, with its values | 26.64 | 24.78 | 29.25 |

### Against its gates

- **U1, exact: met.** No tape made differs from X's on T's columns in its number of entries, a kind or a class, on
  the three corpora and the compiler's own source.
- **U2, how much: met.** 3,308, 728 and 3,511 tapes more (2,900, 600 and 3,400 asked): 95.67%, 97.78% and 97.46% of the
  expressions C takes.
- **U3, cost: not met.** +0.67 ticks a record swept more than the piece before's on `sqlite3.c` and +0.94 on Lua (0.5
  asked, 1.0 refutes); +0.52 and +0.40 on a chunk's second run.
- **U4, nothing else moves: met.** The kernel, the tests' parts and C's sweeps read within 0.12 with the update's
  rows on (0.3 asked).
- **U5, faults: met.** The first three rounds left six faults, one of them twice: one changes nothing a program reaches, one's term went
  out, and four got their cases in the fixture. The fourth round's 181 faults, on the code as it is, are all caught.

### What stays a record at a time, and why

The updates' loop, for one record in some ninety. An update's two records
are apart by a distance only its own record has, its computation's class is
a load by its line's type's number, and its shapes are bytes stored at the
two records. The fourth build had the two rows' shapes as masks in the
shapes' sweep, with the loop carrying only what crosses the two records:
the shapes' sweep read 0.22 ticks a record more on `sqlite3.c` and 0.49
on Lua with them, 0.18 and 0.47 of it with no update to make; the loop of its own reads 0.52 and 0.81, and leaves
the sweep where it was.

### What it means

- **An update's rows are shapes of the same table**, in slots its two records have free: no slot more, no column
  more, and the write's compress joins them as it joins any row.
- **A rare case's terms go in a loop of their own.** As masks in the shapes' sweep (the fourth build), an update's
  terms made that sweep read 0.22 ticks a record more on `sqlite3.c` and 0.49 on Lua, 0.18 and 0.47 of it with no
  update to make. As a sweep and a loop of their own they read 0.52 and 0.81, and the shapes' sweep reads what it
  read before.
- **What it reads more, against what it takes from the reference.** The rows read 215,361 ticks more over `sqlite3.c`'s
  records swept, 85,777 over Lua's and 479,027 over musl's (a chunk's first run); X on T's columns spends 506,203, 117,814 and 597,853 on the
  16,813, 4,158 and 18,109 records of the expressions the update's rows take from it.
- **A sweep a chunk is read more a chunk's first time.** The updates' list's sweep alone, with no update to make,
  reads 0.11 ticks a record on `sqlite3.c`, 0.27 on Lua and 0.69 on musl a first run, and 0.09, 0.12 and 0.30 a
  second; their chunks are 526, 379 and 127 records.
- **Next:** the branches' rows, then the entries' values.

## The branches' rows on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/x-branch-rows-2850-2026-11-20).
`measure-node-machine --shadow --window-rows`
([the guide](commands/lowering.md#xs-rows-on-the-window-2850)).

X's rows on the window listed every expression with an operator evaluated as
branches: 2,046 of `sqlite3.c`'s 49,155 that C takes there. X
writes such an operator no entry, and each value it tests its test and a
branch by its sense after the value's row. This piece makes those as the
tested value's conversion's part's row (four rows of the rows' table), in a
chunk where a condition branches: the update's sweep compresses the chunk's
`!`, `&&` and `||` evaluated as branches into a second list, and a loop down
that list gives each operand its sense and each tested value its shape. No
compile takes any of it. [The node machine's
page](node-machine.md#the-branches-rows-on-the-window) has it whole: the
rows, the builds, the probes and the faults. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7037879405)
had five gates.

### What it measured

The 9700X, CPU 2, under the lock, Release, TSC ticks, seven readings; in each
one executable with the branches' rows on and off (an experiment's switch),
and the piece before's executable between them:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C takes on the window | 49,155 | 9,909 | 39,712 |
| their tapes made here | 49,073 (99.83%) | 9,888 (99.79%) | 39,495 (99.45%) |
| the piece before's | 47,027 (95.67%) | 9,689 (97.78%) | 38,705 (97.46%) |
| tapes that differ from X's on T's columns | 0 | 0 | 0 |
| operators with a turn | 3,415 (1.06% of the records swept) | 340 (0.37% of the records swept) | 1,487 (0.39% of the records swept) |
| **the rows' sweeps and the part, a record swept** | **5.10 (5.01 to 5.28)** | **6.39 (6.31 to 9.88)** | **12.46 (10.37 to 14.48)** |
| the piece before's executable, in the same readings | 4.80 | 6.08 | 13.34 |
| **more than the piece before's** | **+0.32 (+0.17 to +0.40)** | **+0.34 (+0.07 to +2.13)** | **-0.45 (-1.82 to +0.17)** |
| the lists and the two loops; with the branches' rows off | 0.83; 0.53 | 1.05; 0.83 | 1.94; 1.79 |
| a second run over the same chunk; the piece before's | 4.08; 3.79 | 3.83; 3.54 | 5.12; 4.71 |
| a record of the taken expressions | 6.09 | 9.48 | 24.98 |
| X on T's columns, a record of theirs, with its values | 25.53 | 23.55 | 28.34 |

### Against its gates

- **B1, exact: met.** No tape made differs from X's on T's columns in its number of entries, a kind or a class, on
  the three corpora and the compiler's own source.
- **B2, how much: met.** 2,046, 199 and 790 tapes more (2,046, 199 and 790 asked): 99.83%, 99.79% and 99.45% of the
  expressions C takes.
- **B3, cost: met.** +0.32 ticks a record swept more than the piece before's on `sqlite3.c` (0.6 asked, 1.0 refutes)
  and +0.34 on Lua (0.4 and 0.8); +0.29 and +0.29 on a chunk's second run.
- **B4, nothing else moves: met.** The kernel, the tests' parts and C's sweeps read within 0.07 with the branches'
  rows on (0.3 asked).
- **B5, faults: met.** Three rounds. The first's 160 faults left 7: four change nothing a program reaches, and three found code to
  change (the chunk's guard's count, and the listed expression's reason, which reads no consumer now). The second's
  159 left 2, in that reason: one changes nothing, and one got its line in the fixture. The third's 158, on the code
  as it is, are all caught.

### What stays a record at a time, and why

The branches' loop, a turn for each `!`, `&&` and `||` evaluated as branches:
1.06% of `sqlite3.c`'s records swept, 0.37% of Lua's and 0.39% of
musl's. An operator's first operand is at a distance only its own record
has, and an operand's sense is its consumer's, a turn after it. The loop reads +0.29
ticks a record swept on `sqlite3.c` and +0.22 on Lua by the switch (27 and 59 a turn), and the rows with it +0.32 and
+0.34 over the piece before's: within its gate, so that build was not made. What the update's piece read of a rare
case's terms in a big sweep stands against it: as masks in the shapes' sweep an update's terms made that sweep
read 0.22 ticks a record more on `sqlite3.c` and 0.49 on Lua, 0.18 and 0.47 of it with no update to make, and an
operator with a turn is one record in 94 of `sqlite3.c`'s and one in 268 of Lua's.

### What it means

- **A tested value's test is a shape of the same table**, in the conversion's part its record has free: no slot
  more, and one column more, a byte a record for a sense.
- **What it reads more, against what it takes from the reference.** The rows read 102,859 ticks more over `sqlite3.c`'s
  records swept and 31,026 over Lua's (a chunk's first run; musl's readings are -1.82 to +0.17 a record); X on T's columns
  spends 763,708, 75,194 and 306,651 on the 25,806, 2,920 and 9,043 records of the expressions the branches' rows take from it.
- **A chunk's first run is the dearer one.** The rows' sweeps read 5.10 ticks a record swept on `sqlite3.c`, 6.39 on
  Lua and 12.46 on musl a chunk's first run, and 4.08, 3.83 and 5.12 its second; their chunks are 526, 379 and 127
  records. The fourth build's one copy of a step was read there, and not on a second run.
- **The tapes' kinds and classes are made for all but a few expressions:** 99.83%, 99.79% and 99.45% of those C takes on
  the three corpora, and 98.15% of the compiler's own source's, where a qualified bool's read keeps 560 listed
  ([#3185](https://github.com/buster14a/buster/issues/3185)).
- **Next:** the entries' values.

## The entries' values on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/x-values-2850-2026-11-20).
`measure-node-machine --shadow --window-rows`
([the guide](commands/lowering.md#xs-rows-on-the-window-2850)).

An entry of X's tape is a kind, a class and a value, and X's rows on the
window made the first two. This piece makes the third, a quadword an entry.
The stream starts as zeros and its sums' ones; one sweep of masks puts each
record on the lists a table's byte by its shape says; and a loop a list
writes its values from two columns, a record's value (the tests' sweep makes
it as it loads each line) and a pointer's stride (C's loop keeps it). The few
expressions with two calls, two operators with a label or two postfix updates
through an address are numbered a record at a time. A tape the rows make is
then whole. No compile takes any of it. [The node machine's
page](node-machine.md#the-entries-values-on-the-window) has it whole: the
count before it, the builds, the counters, the probes and the faults. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7043553775)
had five gates, and its cost gate is refuted as it was specified.

### What it measured

The 9700X, CPU 2, under the lock, Release, TSC ticks, seven readings; in each
one executable with the values on and off (an experiment's switch), and the
piece before's executable between them:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C takes on the window | 49,155 | 9,909 | 39,712 |
| their tapes made here, whole | 49,073 (99.83%) | 9,888 (99.79%) | 39,495 (99.45%) |
| tapes that differ from X's on T's columns, in a value too | 0 | 0 | 0 |
| listed for a value | 0 | 0 | 0 |
| expressions numbered | 474 (0.97% of the tapes) | 163 (1.65% of the tapes) | 635 (1.61% of the tapes) |
| **the rows' sweeps and the part, a record swept** | **8.50 (8.27 to 8.66)** | **11.09 (10.98 to 11.58)** | **21.26 (19.89 to 22.95)** |
| the piece before's executable, in the same readings | 5.05 | 6.41 | 11.29 |
| **more than the piece before's** | **+3.48 (+3.13 to +3.69)** | **+4.78 (+4.28 to +4.91)** | **+10.16 (+8.10 to +12.23)** |
| with the values off; on less off | 5.15; +3.38 | 6.94; +4.19 | 13.81; +7.63 |
| with the values off, more than the piece before's | +0.12 (-0.04 to +0.21) | +0.49 (+0.20 to +0.75) | +2.53 (+0.98 to +3.72) |
| the values' part | 3.20 | 4.11 | 7.48 |
| a second run over the same chunk; the piece before's | 6.17; 4.05 | 6.17; 3.72 | 8.04; 4.83 |
| the values' part in it | 1.93 | 2.05 | 2.91 |
| the records' bytes, which make a record's value; on less off | 9.33; +0.30 | 11.88; +0.26 | 23.36; +0.42 |
| X on T's columns, a record of theirs | 25.61 | 23.69 | 28.64 |

### Against its gates

- **V1, exact: met.** No tape made differs from X's on T's columns in its number of entries, a kind, a class or a
  value, on the three corpora and the compiler's own source; 5,447 one-statement programs agree.
- **V2, how much: met.** None is listed for a value.
- **V3, cost: refuted.** +3.48 ticks a record swept more than the piece before's on `sqlite3.c` (1.5 asked, 2.5 refutes) and +4.78
  on Lua; +2.14 and +2.46 on a chunk's second run.
- **V4, nothing else moves: met.** The kernel, the tests' other parts and C's other sweeps read within 0.18 with the
  values on; the records' bytes and C's loop, which the piece gives work to, within 0.30 (0.3 asked).
- **V5, faults: met.** Two rounds. The first's 352 faults left 41: 8 terms no program separates, which are out of the
  code; 5 that change nothing; and 28 that found a control, a count or a fixture's line to add. The second's 89, on the
  code as it is, are all caught.

### What stays a record at a time, and why

The lists' loops, 0.48, 0.36 and 0.29 listed records a record swept: a value is a load from a column and a store at its
row's place, and the places are no register's eight in a row. And the numbering, for 0.97%, 1.65% and 1.61% of the
tapes: a call's row and a label are preorder numbers in the expression's tree. The by-slots build the specification
named against the loops was not made: the counters put a first run's cost in a cold start a chunk, not in the loops'
work ([#3196](https://github.com/buster14a/buster/issues/3196)).

### What it means

- **X's tapes are whole on the window** for 99.83%, 99.79% and 99.45% of the expressions C takes on the three corpora: a kind,
  a class and a value an entry, compared with X's on T's columns.
- **What they cost against the reference.** The rows' sweeps and the part read 10.16, 16.45 and 42.59 ticks a record of the
  taken expressions; X on T's columns reads 25.61, 23.69 and 28.64 a record of theirs. **On musl the window's rows now read more than the reference does:** its
  chunks are a quarter of `sqlite3.c`'s, and its rows' sweeps read 21.26 ticks a record swept a chunk's first run and 8.04
  its second.
- **The cost gate was written from work a record, and the window's sweeps pay by the chunk too.** The values' part
  reads 3.20, 4.11 and 7.48 ticks a record swept a chunk's first run and 1.93, 2.05 and 2.91 its second; their chunks are
  526, 379 and 127 records. A gate for a pass over the window is two numbers: cycles a chunk and work a record.
- **Next:** `&&` and `||` as values, `?:` and a comma, in C and in X on the window together.

## An operator's value and a comma on the window

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/logical-values-2850-2026-11-20).
`measure-node-machine --shadow --window-rows`
([the guide](commands/lowering.md#an-operators-value-and-a-comma-on-the-window-2850)).

`&&` and `||` as values, `?:` and a comma were what C on the window listed
most: it decided no expression with one, and X made no row of theirs. They
are made now in the sweeps that exist. In C's loop a comma and an operator's
value are masks, each record says of itself whether it is evaluated as
branches or is a value, and a record that has its consumer's answer reads it
from the next record, down a run of such records by two doubling steps. In
X's rows the branches' loop takes these operators' turns too: the
condition's test and branch, a conditional's first arm's jump, a logical
operator's own four entries; and the numbering counts labels, two an
operator's value. No compile takes any of it. [The node machine's
page](node-machine.md#an-operators-value-and-a-comma-on-the-window) has it
whole: the census before it, the rules, the three builds and the faults. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7049301495)
had six gates. It is exact and its faults are caught. It takes 11 expressions
fewer than asked on `sqlite3.c` and 1 on musl; its work a record is within
its gate for the rows and at C's bound; and its cost is a chunk's first run of
C's loop, which no gate named.

### What it measured

The 9700X, CPU 2, under the lock, Release, TSC ticks, seven readings; in each
the build, the piece before's executable twice, and the build again:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| expressions C decides on the window; the piece before's | 50,109; 49,310 | 10,715; 10,079 | 42,776; 41,759 |
| **expressions whose tape X makes, whole; the piece before's** | **49,842**; 49,073 | **10,248**; 9,888 | **40,444**; 39,495 |
| more of them | +769 (1.51% of the expressions) | +360 (3.32% of the expressions) | +949 (2.19% of the expressions) |
| their records; the piece before's | 278,867; 268,942 | 70,873; 61,509 | 204,846; 188,250 |
| answers of C's; tapes of X's that differ from the reference's | 0; 0 | 0; 0 | 0; 0 |
| operators' values C takes; of them listed for their target; commas | 715; 98; 330 | 414; 7; 283 | 940; 49; 184 |
| listed for a logical operator or a conditional; the piece before's | 135; 674 | 22; 290 | 115; 964 |
| listed for a comma; the piece before's (a selection among them) | 0; 264 | 5; 376 | 0; 169 |
| listed for X by a turn (an arm with no room, a narrow second operand) | 3 | 0 | 0 |
| expressions numbered; the piece before's | 428; 474 | 318; 163 | 1,071; 635 |
| **C's sweeps, a record of the decided, a chunk's second run; the piece before's; more** | 6.79; 6.40; **+0.36 (+0.34 to +0.44)** | 7.25; 7.61; **-0.35 (-0.38 to -0.30)** | 12.31; 10.95; **+1.53 (+1.09 to +2.51)** |
| **the rows' sweeps, a record of the taken, a second run; the piece before's; more** | 7.30; 7.32; **-0.04 (-0.52 to +0.31)** | 8.83; 9.14; **-0.29 (-0.44 to +0.09)** | 15.74; 16.05; **-0.20 (-0.96 to +5.21)** |
| C's sweeps, a record of the decided, a first run; the piece before's; more | 9.02; 7.67; +1.22 (+1.12 to +1.83) | 13.09; 12.01; +1.02 (-0.14 to +2.36) | 34.48; 29.55; +4.93 (-0.79 to +17.16) |
| the rows' sweeps, a record of the taken, a first run; the piece before's; more | 10.15; 10.00; +0.15 (-0.33 to +1.44) | 16.00; 16.36; -0.46 (-0.99 to +5.09) | 43.61; 44.22; -2.18 (-7.98 to +21.59) |
| C's sweeps, a record swept: a first run, more; a second, more | +1.32 (+1.24 to +1.84); +0.52 (+0.50 to +0.59) | +2.41 (+1.59 to +3.50); +0.71 (+0.68 to +0.75) | +4.12 (+1.06 to +11.21); +1.38 (+1.15 to +2.04) |
| the rows' sweeps, a record swept: a first run, more; a second, more | +0.44 (+0.03 to +1.55); +0.19 (-0.21 to +0.50) | +1.34 (+0.95 to +5.60); +0.71 (+0.65 to +1.01) | +0.81 (-2.07 to +13.62); +0.59 (+0.18 to +3.54) |
| **the rows' sweeps, a first run less a second, ticks a chunk; the piece before's; more** | 1299; 1171; **+116 (-63 to +558)** | 2065; 1859; **+237 (+74 to +1876)** | 1884; 1778; **+63 (-505 to +1653)** |
| C's sweeps, a first run less a second, ticks a chunk; the piece before's; more | 1021; 552; +410 (+379 to +668) | 1800; 1159; +630 (+324 to +1051) | 1635; 1246; +373 (-42 to +1162) |
| the kernel in place, more; the tests on the window, more | -0.33 (-0.80 to -0.24); +0.21 (-0.17 to +0.39) | -0.36 (-2.67 to -0.12); -0.59 (-2.58 to +1.80) | -0.17 (-3.27 to +3.05); -4.12 (-5.91 to +5.21) |
| the reference's C, a record of the decided; its X, a record of the taken | 40.50; 25.80 | 36.84; 23.50 | 43.70; 28.97 |
| records a chunk; chunks | 526; 611 | 379; 241 | 127; 2,975 |

### Against its gates

- **L1, exact: met.** No answer of C's and no entry of X's tape differs from the reference's on the three corpora and
  the compiler's own source; 6,414 one-statement programs agree.
- **L2, how much: met on Lua alone.** 769, 360 and 949 expressions more are taken where 780, 355 and 950 were asked.
  The rule for a conversion the tasks put into an operator's arms lists 16 and 6 expressions that the build before
  took, since the first fault round: its terms no test separated are out.
- **L3, work a record: met for the rows; C's is at its bound on `sqlite3.c`.** A chunk's second run of the rows' sweeps
  reads -0.04 and -0.29 ticks a record of the taken more than the piece before's on `sqlite3.c` and Lua (0.30 and
  0.40 asked); C's sweeps read +0.36 and -0.35 a record of the decided (0.35 asked, 0.70 refutes).
- **L4, a chunk: met on `sqlite3.c` and musl by their medians, not on Lua.** The rows' sweeps' first run less their
  second rose by +116, +237 and +63 ticks a chunk (150 asked, 300 refutes), and each corpus's range
  reaches past 300. C's sweeps', which the gate did not name, rose by +410, +630 and +373.
- **L5, nothing else moves: not as written.** The rows' reads are within 0.2 on `sqlite3.c` and Lua. The kernel in
  place, whose source the piece does not touch, reads -0.33 and -0.36 (less in each of the three builds), and the
  tests' sweeps +0.21 and -0.59: two builds read a sweep with no change a third of a tick apart or more.
- **L6, faults: met, with five faults no test separates whose terms stay.** Two rounds. The first's 308 faults
  left 46: 15 terms that what makes the records implies, which are out of the code; 18 of the first rule for
  the tasks' conversions, which is three terms now; 10 that found a line of the fixture or a pin to add; and
  3 that change nothing a program reaches. The second's 55, on the code as it is: 50 caught, and 5 that
  change nothing a program reaches.

### What stays a record at a time, and why

In C's loop, one record in 308, 131 and 336 of those swept, an operator's value or a comma: whether a comma's consumer
is of void's class, a discarded comma's context's byte, and the rule on an operator's value's target, which reads
its type's class. The first build had the vector's forms of the first two, a permute of the block's classes by the
consumer's lane in every block and a blend (and a second permute, for a `!`'s consumer's answer): its loop read 0.09
and 0.11 ticks a record swept more a second run than the build without them. In X, an operator's value's turn, a byte or two at its operands' places; and the numbering,
for 0.86%, 3.10% and 2.65% of the tapes.

### What it means

- **X's tapes are whole on the window** for 49,842, 10,248 and 40,444 of the three corpora's 50,993, 10,839 and
  43,415 expressions: 769, 360 and 949 more, with 3.7%, 15.2% and 8.8% more records.
- **What they cost against the reference.** By its averages a record, the reference's C and X read 658,028, 565,024 and 1,206,031
  ticks on the newly taken records. Over all the records swept the piece adds 86%, 61% and 154% of that
  a chunk's first run, and 35%, 23% and 62% its second. **On musl a first run costs more than the
  reference's work it replaces:** its chunks are 127 records.
- **The cost is C's loop's, by the chunk.** Its step has the new masks in every block: +1.02, +1.94 and +3.49
  ticks a record swept more a first run, +0.44, +0.53 and +1.24 a second. The specification gated a chunk's cost for the
  rows' sweeps and not for C's: the loop's cold start a chunk
  ([#3196](https://github.com/buster14a/buster/issues/3196)) grew with the masks its step took on.
- **Next:** a summary node's side facts, the flip's first piece, below; the window's sweeps run in a compile
  after it, where their cost by the chunk is settled.

## A summary node's side facts on the window

[The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/summary-facts-2850-2026-11-21).
`measure-node-machine --shadow --window-rows` ([the
guide](commands/lowering.md#a-summary-nodes-side-facts-on-the-window-2850)).

Step 2 ends with one summary node where a taken expression's nodes were. Every pass that reads below a full
expression's root then needs its answer from that node. A survey of today's code found what a compile with the
default warnings reads there: the static objects an expression's leaves name, its root's return fact, a branch's
condition's truth and a constant root's fold. This piece makes the first two on the window, for each expression
whose tape X makes whole: the references by one sweep of masks with a count of them through each record, so that an
expression's are a range of one list, and the return fact from a mask of the calls that are no plain function's,
asked a record at a time. The last two are the root's value's to answer, and the window lists the two cases where
the program knows a truth its value has not. No compile takes any of it. [The node machine's
page](node-machine.md#a-summary-nodes-side-facts-on-the-window) has it whole: the survey, the rules, the builds,
what a generator of programs found after the code was frozen, and the faults. [Its
specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7053609091) had six gates. It is exact
on what it makes and its faults are caught. This measurement predates the switch selector side fact. Its work a record is over its
gate and under what refutes it; and by the chunk it is past what refutes its gate on musl.

### What it measured

The 9700X, CPU 2, under the lock, Release, TSC ticks, 7 readings; in each the facts on, off twice by the shadow's
switch (`--no-window-summaries`), and on again, in one executable:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| records swept; chunks; records a chunk | 321,434; 611; 526 | 91,252; 241; 379 | 377,187; 2,975; 127 |
| expressions whose facts are kept; their references; the program's (a set an expression) | 49,839; 14,634; 14,502 | 10,246; 4,959; 4,918 | 40,441; 10,247; 9,391 |
| of them with an unknown for a return fact; with a stop | 250; 0 | 15; 112 | 64; 21 |
| records listed for a reference; calls asked a record at a time; expressions reduced | 14,911; 255; 0 | 5,128; 127; 0 | 11,522; 99; 0 |
| **the sweep, a record swept, a chunk's second run** | **0.27 (0.27 to 0.28)** | **0.31 (0.31 to 0.32)** | **0.31 (0.31 to 1.19)** |
| the sweep, a record swept, a first run | 0.41 (0.39 to 0.43) | 0.80 (0.78 to 0.81) | 1.70 (1.57 to 2.28) |
| the tests' records' bytes, a record swept, more with the facts (their one run) | -0.01 (-0.12 to +0.06) | -0.01 (-0.24 to +0.98) | -0.81 (-6.69 to +3.09) |
| the tests' loop, more (its one run) | +0.12 (-0.07 to +0.32) | +0.06 (+0.01 to +1.22) | +0.67 (-1.20 to +4.47) |
| **the three together, the sweep by its second run** | **+0.40 (+0.17 to +0.47)** | **+0.32 (+0.18 to +2.52)** | **+1.01 (-5.70 to +5.54)** |
| the three together, the sweep by its first run | +0.55 (+0.29 to +0.61) | +0.81 (+0.68 to +3.00) | +2.33 (-3.74 to +7.00) |
| **the sweep, a first run less a second, ticks a chunk** | **68 (63 to 80)** | **185 (178 to 186)** | **167 (86 to 249)** |
| the sweep, ticks a chunk: a first run; a second | 215 (207 to 226); 144 (143 to 147) | 304 (295 to 306); 119 (116 to 122) | 216 (199 to 290); 40 (39 to 151) |
| the tests' bytes and loop, ticks a chunk, more | +66 (-60 to +104) | +2 (-46 to +834) | +89 (-763 to +663) |
| **the rows' sweeps, the facts' among them, a record swept, a second run, more** | **+0.34 (+0.31 to +0.38)** | **+0.39 (-0.18 to +0.40)** | **+0.36 (-0.39 to +1.14)** |
| **the rows' sweeps, the facts' among them, a first run less a second, ticks a chunk, more** | **+23 (-44 to +64)** | **+115 (-11 to +372)** | **+232 (-1 to +248)** |
| the rows' other sweeps, a second run, more | +0.06 (+0.03 to +0.10) | +0.07 (-0.49 to +0.07) | +0.03 (-0.70 to +0.26) |
| of them the turns' lists and loops, where the listing is | +0.01 (-0.01 to +0.02) | +0.00 (-0.02 to +0.02) | -0.01 (-0.03 to +0.00) |
| the rows' other sweeps, a first run less a second, ticks a chunk, more | -47 (-117 to +0) | -70 (-189 to +188) | +60 (-156 to +81) |
| C's sweeps, a second run, more; a first run less a second, ticks a chunk, more | +0.01 (-0.03 to +0.06); +20 (-86 to +111) | +0.01 (-0.43 to +0.05); +7 (-37 to +61) | -0.06 (-1.40 to +0.28); +54 (-185 to +229) |
| the kernel in place, more | +0.01 (-0.05 to +0.14) | +0.01 (-0.41 to +0.40) | +0.01 (-0.78 to +0.10) |
| the tests' other parts, more | -0.02 (-0.27 to +0.20) | -0.43 (-2.81 to +0.71) | +2.14 (-1.49 to +2.94) |
| the rows' sweeps without the facts, a record swept: a first run; a second | 8.82 (8.58 to 9.11); 6.27 (6.25 to 6.30) | 12.42 (12.20 to 13.06); 6.71 (6.65 to 7.23) | 24.27 (23.53 to 25.94); 8.14 (8.03 to 8.77) |
| the timed region with no sweep in it, ticks a chunk | 27.0 (26.6 to 30.1) | 26.9 (25.6 to 28.6) | 27.4 (27.1 to 27.8) |

### Against its gates

- **F1, exact: met for what the piece makes.** Every kept expression's references and return fact are the program's
  own walks' on the three corpora and the compiler's own source (49,842, 10,246, 40,441 and 41,890 expressions), a
  kept branch's truth is its value's and a kept constant folds; 7,957 one-statement programs agree. A `switch`'s
  selector's type was excluded from that comparison. After #3209 the shadow also checks the selector's
  type before promotion against the warning pass. The control changes the type saved by the reader.
- **F2, how much: met.** The turns' listing leaves out 3, 2, 3 and 0 expressions the piece before took (5 a corpus
  asked); the cast's term lists none.
- **F3, work a record: met for the tests' sweep, over for the rows'.** A chunk's second run of the rows' sweeps
  reads +0.34 and +0.39 ticks a record swept more with the facts on `sqlite3.c` and Lua (0.30 asked, 0.60 refutes);
  the tests' bytes -0.01 and -0.01 (0.20 asked).
- **F4, a chunk: refuted on musl, not met on Lua, met on `sqlite3.c`.** The rows' sweeps' first run less their
  second rose by +23, +115 and +232 ticks a chunk (100 asked, 200 refutes), and by +46, +144 and +259 in a second
  set. The tests' sweep reads +66 on `sqlite3.c` here and +41 and +181 in the two builds before (50 asked): with the
  facts it is another copy of its code, and the switch reads the two copies' places.
- **F5, nothing else moves: met for the kernel and C's sweeps** (+0.01, +0.01 and +0.01 and +0.01, +0.01 and -0.06);
  the tests' other parts read -0.02, -0.43 and +2.14, one run a chunk, and +0.04, -0.13 and -0.14 in the second set.
- **F6, faults: met, with one fault no test separates.** The whole list on the code here, 142 faults: 141 caught,
  and one that changes nothing a program reaches. Nine terms no test separated in the rounds before are out of the
  code.

### What stays a record at a time, and why

The objects, one a reference, were a loop of a load and a store, a third of the sweep; a gather of eight is the
code, the same on `sqlite3.c` (+0.00 (-0.00 to +0.09) ticks a record swept against the loop, one executable) and
less on Lua and musl (-0.07 (-0.08 to -0.05) and -0.14 (-0.19 to -0.13)), whose chunks' few references ended the
loop on a mispredicted branch. A record at a time stay a call that is no plain function's (one record in 1,261, 719
and 3,810 of those swept) and the flow's reduction, for an expression with a call that stops under an `&&`, an `||`
or a `?:` (none in the three corpora, 18 in the compiler's own source).

### What it means

- **The side facts are exact on the window,** and the piece before's takes stand but for 3, 2, 3 and 0 expressions.
- **The sweep is a third of a tick a record** (0.27, 0.31 and 0.31 on a second run), and a chunk's cold start of its
  own (68, 185 and 167 ticks a chunk), which on musl's small chunks is past its gate. A compile that takes
  expressions is where a chunk's first run is to be read ([#3196](https://github.com/buster14a/buster/issues/3196)).
- **The corpora and the fixtures were not enough to call it exact:** a generator of programs and the regression
  programs found a truth through a cast and a constant of an object's record here, and in earlier pieces what the
  reader-time tests take with a warning ([#3201](https://github.com/buster14a/buster/issues/3201)), a `return` under
  a cleanup ([#3202](https://github.com/buster14a/buster/issues/3202)) and a multiversioned function's object
  ([#3203](https://github.com/buster14a/buster/issues/3203)). Those three are next, before the first compile that
  takes expressions.

## What the compiler's own tests had and no corpus

[The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tests-holes-2850-2026-11-21).
`measure-node-machine --shadow --window-rows` ([the
guide](commands/lowering.md#the-tests-holes-and-the-objects-side-2850);
[the design's
section](node-machine.md#what-the-compilers-own-tests-had-and-no-corpus)).

The pieces before were gated on four corpora, their fixtures and their one-statement programs: code that warns of
nothing. Every C string literal of the compiler's test files that is a program
(13,866) and every file under `tests/` (1,229), each through the
shadow under three warning sets, found 150 programs whose reader-time answers were not
the pipeline's, in fifteen kinds ([#3201](https://github.com/buster14a/buster/issues/3201),
[#3202](https://github.com/buster14a/buster/issues/3202), [#3203](https://github.com/buster14a/buster/issues/3203),
[#3205](https://github.com/buster14a/buster/issues/3205), [#3206](https://github.com/buster14a/buster/issues/3206),
[#3207](https://github.com/buster14a/buster/issues/3207)): four of them expressions taken with a warning, one a
tape that lost a segment's override. This piece closes them. No compile takes any of it.

### What it is

- **The tests:** a pointer's ordered comparison with an integer has a test of its own, and what converts to a
  pointer (an array, a function) is tested as one; a declaration that inherits a list of conditions is a callee to
  ask; a unit with x86's `interrupt` or `noderef` is refused whole; an initializer's tokens begin after its object's
  name at reader time too; a `return` under a cleanup has another control's root there; a value in a segment's
  address space is of a type the machine has no kind for. A test's verdict stays one piece of code at both times,
  and the window's loop only lists for it: what it gained is one mask, which leaves out an ordered comparison of two
  pointers, a loop's `p < end`.
- **The objects' side:** what the reader settles after the bodies is asked in a form both times have before them
  (whether the unit has an overload's candidates; an asm label the asm pass will ignore), or refused (a library
  builtin's call in a unit with a rename pragma). A weak name is the name's from a token on: the reader's list is in
  its output, searched eight records a step by the reader and by the kernel, and only a declaration whose name the
  unit notes has its line made again at every use.

### What it measured

**Its cost was not read** on the 9700X before the work was handed off (the design's section has what the code adds).

### What it means

- **Nothing a corpus took is lost, and nothing more is taken** (49,842, 10,246, 40,441 and 41,890 tapes whole, as before): the
  holes were in what the corpora do not have.
- **The inputs that say "exact" are five now:** the corpora, the fixtures, the one-statement programs, the
  generator's, and the compiler's own tests' programs with a trigger a warning. In the shadow the last is a
  campaign's sweep; with the first compile behind its option the tests' own drivers run through the machine.
- **Next:** the walk's truth through a cast ([#3204](https://github.com/buster14a/buster/issues/3204)), then the
  first compile that takes expressions, behind an option.

## Step 2's ceiling: the middle without its taken expressions

[The record](https://github.com/buster14a/buster-performance/tree/e5e5b4d20/docs/measurements/takeover-2850-2026-11-22)
(`e5e5b4d20`; main `d541430bf`; the 9700X, CPU 2, one lane, Release, under the lock). No pull request's code: the
counter is an experiment's patch on the shadow, and the rest is a transform of the source.

Step 2 ends with one summary node where a taken expression's nodes were. Every piece before this one was gated on
the machine's cost a record. None read what a compile gains for a record it takes, and the one option a compile has
(`--node-window`) prices its own discovery scan and switches the statement controls' path off, so it prices no node:
it costs `-c sqlite3.c` 14.3 M instructions at this main. This piece reads the other side, before any more of the
machine is built: what the stages from `check-input` to `lower` take when each expression the window takes costs the
compile one leaf.

### What it is

- **The slice count.** `measure-node-machine --shadow --window-rows --passes 0` with a counter that sorts each
  expression whose tape X makes whole on the window by what the tests said of it (nothing listed; listed, then silent
  on T's records; refused) and by what its tape names of its function's: a call's row, a label of its own, a
  temporary, a branch to its control's label.
- **The ceiling.** `sqlite3.c` preprocessed (`-E`: 716,136 tokens, and the ordinary object but for its file's name).
  The same counter prints each whole tape's expression's tokens and root, and a transform replaces each such full
  expression of a statement, a condition or a `switch` by one leaf, a volatile global, under the root the reader
  makes. A `return`'s value and an initializer stay as they are (7,670 expressions, 22,743 records). Every variant
  ends with one function that names each static function and object: without it the calls' removal leaves 179 of
  2,399 static functions referenced, and the selection lowers no other. Each variant is then compiled by main,
  seven `--time-stages` runs, in a rotating order.

### What it measured

In records, of 321,434 swept on `sqlite3.c` (49,839 tapes, 278,842 records):

| the tests | plain | with a call | with a label, a temporary or a control's branch | all |
| --- | ---: | ---: | ---: | ---: |
| nothing listed, silent | 126,473 (39.3%) | 77,522 | 20,255 | 224,250 (69.8%) |
| listed, then silent on T's records | 27,816 | 12,679 | 14,052 | 54,547 (17.0%) |
| refused | | | | 45 |

The stages' medians, ms; the middle is `check-input` to `lower`:

| | none replaced | plain | nothing listed | all whole tapes |
| --- | ---: | ---: | ---: | ---: |
| records replaced | 0 | 119,834 | 215,630 | 256,069 |
| `check-input` | 42.4 | 37.8 | 31.8 | 29.5 |
| `check-expressions` | 10.7 | 9.0 | 6.5 | 5.1 |
| `check-program` | 19.2 | 16.2 | 12.2 | 13.8 |
| `lower` | 11.8 | 9.9 | 7.0 | 5.8 |
| **the middle** | **87.6** | **76.3** | **60.2** | **56.7** |
| **less than none replaced** | | **11.3** | **27.4** | **30.9** |
| a record replaced, ns | | 95 | 127 | 121 |

### What it means

- **Step 2's ceiling on `sqlite3.c` is about 31 ms of 112**, with every whole tape taken at no cost: 27 with the
  tapes the tests decide on the window alone, 11 with the plain ones. A record a taken expression no longer makes is
  worth 95 to 127 ns, 520 to 700 cycles.
- **The machine's own cost is about a third of it.** The shadow's first-run readings on these chunks are about 120
  TSC ticks a record swept (the front 55, the kernel with its names' pass 22, the tests 27 to 38, C 8, X 9): 10 ms a
  compile, whatever it takes. Net, step 2 whole is about 21 ms, 1.23 times, as "What it says of step 2" summed it
  (1.22 to 1.27) from the other side; 17 ms with nothing listed; 1 ms with the plain tapes.
- **A first compile that takes no call gains nothing.** The slice that needs no call's row, no label and no
  temporary is 39% of the records and is the one that does not pay. What stands between the window and a call in a
  compile: an expression's placement and its root's target and sense without the graph's T and the reference's C,
  the calls' rows' contents and the labels' places in X's rows, and a local's read and write events for the passes
  that warn. None of it is on the window.
- **With every taken expression gone the middle is still 57 ms.** A profile of that variant: the reader's walks that
  are no expression's 15.5 ms (the declarations 5.5 of them, the names' resolution 3.7), the kernel 12.2 (its peak is
  185 MB where the whole unit's is 237: the memory follows the tokens, not the nodes), the program and its controls
  7.1, the lowering 5.0, the warnings 4.6. That part is #2524's steps 3 and 4 and the file scope (#2531), and it is
  six times the whole target.
- **The bound is rough to a millisecond or two either way.** The stand-in is one leaf where a summary node is one
  node and its rows' splice, and its own lowering and emission fall in the later stages, which the middle leaves out.

## The redesign's first piece: the body boundary

[The record](https://github.com/buster14a/buster-performance/tree/7694601a3/docs/measurements/takeover-2850-2026-11-22#the-body-boundary-with-a-recorded-supplier-boundary--boundary-b1)
(`7694601a3`; main `f1fcdf2a0`; the times on the 9700X, CPU 2, one lane, Release, under the lock; the exactness runs
on Develop builds). Its code is a scaffold on the branch `claude/2850-boundary-scaffold` and is not on main: of this
piece only the gate on a masked unit's reader is merged
([#3272](https://github.com/buster14a/buster/pull/3272)).
[The redesign](https://github.com/buster14a/buster/issues/2850#issuecomment-7078174378) and
[the boundary's specification, as amended](https://github.com/buster14a/buster/issues/2524#issuecomment-7079102369)
are on the issues.

Step 2 as written takes an expression at a time inside the reader, and its ceiling is the section above: 31 ms with a
machine that costs 10, and 57 ms of middle left. The redesign takes whole function bodies outside the reader instead,
in windows (the stream), and leaves the reader the file scope and the bodies the stream refuses. Its first piece is
not the stream. It is the place where a body taken that way comes back into the unit, built and measured with a
supplier that costs nothing to write, so that what the host costs when bodies are free is read before anything is
built on that figure.

### What it is

- **The boundary.** A supplied body's tokens are masked from the reader as a lazily deferred body's are
  (`-flazy-system-headers`' mask), but its function stays defined. Its opening stays among the program's bodies with a
  record that has no controls. The lowering takes its `Tier0Function` whole, after its part's ABI wiring and before
  `check_lower_finish`. From the supplier come also the objects it references (which the symbols' numbering and the
  selection read), its string literals' tokens (whose records the reader then makes in their order, for the data
  emission) and its jump tables (numbered among the unit's where its switches' would have been).
- **Why the whole function.** The first specification spliced a body's entries between the lowering's prologue and its
  end, with each call's types. Type ids are not the same when a body is masked: a file-scope expression after a body
  that makes types has its operands at type 608 in a whole read and 568 in a masked one. A lowered function names
  nothing by type, only symbols, and a symbol's id is its object's rank among the needed objects in token order.
- **The supplier** is a recording: an ordinary compile writes each unit's bodies (`BUSTER_BOUNDARY_RECORD`), and a
  compile of the same unit takes them (`BUSTER_BOUNDARY_SUPPLY`, all of the eligible ones or a selection). The
  recording names every token by its index in the unit's own parse, as a command of several units reads each through
  a view without its attested declarations and a mask holds the view off.
- **What refuses a body,** which is then read as ever: a block-scope `static` or `extern` (its objects are data the
  reader makes); a label whose address the code takes; a helper's, an initializer pool's or `errno`'s symbol (each
  numbered from the unit's nodes); any asm statement (its number names the unit's asm helper even where its function
  has no row for it); a tail call; a diagnostic in it; `-g`.
- **The mask's own gate (B4, merged).** A lazy mask turned the reader's coded walk, its packed simple declarations and
  its statement heads off for the whole unit. They stay on now, so that a unit with some bodies taken reads the rest
  as an untaken unit does.

### What it measured

Exactness (B1), Develop builds: the ordinary compile, the recording, and the compile with every eligible body
supplied and with each half of them.

| by default flags | bodies | supplied | their tokens | refused |
| --- | ---: | ---: | ---: | --- |
| `sqlite3.c` | 2,668 | 2,589 | 480,204 | 79 with a block-scope `static` or `extern` |
| Lua, 34 units | 1,096 | 1,081 | 156,805 | 14 with `static` or `extern`, 1 with labels as values |
| `main.c` | 1,498 | 1,358 | 285,353 | 114 `static` or `extern`, 11 asm, 10 objects, 4 helpers, 1 pool |
| musl, 1,336 units | 2,756 | 2,161 | 249,484 | 510 asm, 68 `static` or `extern`, 17 with a diagnostic |

Every variant's objects and diagnostics are the ordinary compile's. With `-Wall -Wextra` the objects are and the
diagnostics are not: the reader's unused analysis lacks a supplied body's uses of its parameters and of the unit's
declarations, and warns of them. And a unit of four lines shows a hole by default flags that the corpora don't have: a
`static` function used and never defined warns at the unit's end (`-Wundefined-internal`), the driver gives a unit
with a mask no unit-end warnings, and the supplied compile is silent where the ordinary one warns.

The time (B2), `--time-stages` medians of seven rounds, ms; all 43 objects of a run are one:

| frozen `sqlite3.c` | ordinary | supplied, 76% of the bodies' tokens | supplied, 84% (with jump tables) |
| --- | ---: | ---: | ---: |
| `check-input` | 41.49 | 28.11 | 26.54 |
| `check-expressions` | 10.50 | 2.95 | 2.06 |
| `check-program` | 18.83 | 6.58 | 5.24 |
| `lower` | 11.48 | 3.35 | 2.70 |
| `check-input` to `lower` | 85.76 | 43.07 | 38.58 |
| Tier 0 and emission | 8.77 | 8.91 | 8.92 |
| total | 110.93 | 69.50 | 64.92 |
| peak, MB | 238.4 | 230.7 | 230.8 |

With half of the eligible bodies the totals are 91.2 to 94.1. Reading the recording is 1.7 ms of the supplied
compile. With nothing recorded or supplied (B3) the scaffold reads 111.32 ms against main's 111.73 in seven ABBA
rounds, 1,215.946 M instructions against 1,215.957 M.

What the stream must cover, from the recording's census of `sqlite3.c`'s bodies: they use 9 of the 22 control kinds
and 56 of the 69 expression ops, and

| constructs supported | bodies whole | their tokens |
| --- | ---: | ---: |
| every op that 2% of the bodies have, with END, EVALUATE, RETURN and INITIALIZE | 24.8% | 4.6% |
| and BRANCH_FALSE | 47.3% | 13.3% |
| and JUMP | 86.3% | 53.1% |
| and the rest but SWITCH and CASE | 96.9% | 79.8% |

Of the 2,589 supplied bodies, 2,587 have a reference list equal to the set of objects their function names. The other
two name one more: `memmove` and `memset`, which the unit declares and the lowering's own copies call.

Profiles of the compile with every body masked, 243.8 M cycles a run: the kernel is 25.3% of it and
`clear_page_erms` alone 18.2% (7.7 ms), `memset` 4.4%. Its peak is 216 to 222 MB against the whole unit's 238.

### What it means

- **The boundary is real and exact where it was built.** A unit takes its bodies' code from outside the reader, in
  whole or in part, and its object is the ordinary one to the byte on four corpora. What it lacks: the uses for the
  unused analysis; the unit-end warnings, which the driver gives no unit with a mask; and block-scope statics, which
  are 15% of `sqlite3.c`'s body tokens (`sqlite3VdbeExec` among them).
- **Its gate on the total is refuted, and what refutes it is the host.** 0%, 76% and 84% of the bodies' tokens are a
  line of 0.544 ms a percent: 56.5 ms with every body supplied and costing nothing, where the gate allowed 56 for
  everything. The bodies' checking and lowering is 46 ms of today's 111 for the bodies that were supplied. What the
  line leaves at its end is the reader's passes over every token (21 ms with every body masked), the front (16),
  Tier 0 and emission (9), the file scope's own checking and the supply, on memory that barely moves.
- **A masked token costs by being there: how much is an estimate.** Two readings that are not one experiment: the
  bodies' tokens removed from a preprocessed source left 14.2 ms between `check-input` and `lower`, at main
  `d541430bf`; masked in place in the frozen source they leave 25.5, at `0b150b71b`. The profile says where the
  difference would be: pages the reader's columns touch for tokens nothing reads. A reader that reads a view of the
  unit without the taken bodies' tokens, as it reads a view without a unit's attested declarations, is the order's
  third step. It is not built, and what it saves is not measured.
- **There is no small first slice of the stream.** Coverage by constructs is nothing until the expression language is
  there whole with branches and jumps. What takes bodies takes nearly all of them or nearly none, so the stream's
  first piece is a second middle for the language as code uses it, and whether that may be on main beside the first
  is [#2924](https://github.com/buster14a/buster/issues/2924)'s question, asked on the issue and not answered.
- **A supplier that makes the function has the references.** They need no walk of their own on that path.
- **Where this leaves the target, as an estimate.** Nothing past the boundary is built. If a stream took every body
  at no cost and a view saved what the two readings above differ by, `sqlite3.c` would compile in about half of
  today's time. The front (16 ms) and the kernel's share (13 to 14 ms of the ordinary and of the supplied compile) are
  each more than the 9.5 ms the whole compile may take, and Tier 0 with the emission (9) is nearly all of it. Each is
  another row's redesign (steps 3 to 5 above).

## The redesign's second piece: the machine with no node of the walk's

[The record](https://github.com/buster14a/buster-performance/tree/664fcc0a8/docs/measurements/takeover-2850-2026-11-22#the-machine-with-no-node-of-the-walks-machine-alone-buster-branch-claude2850-machine-alone)
(`664fcc0a8`; the times on the 9700X, CPU 2, Release, under the lock; the exactness runs on Develop builds and on
the 9700X's). `measure-node-machine --shadow --front-alone`
([the guide](commands/lowering.md#the-machine-with-no-node-of-the-walks-2850)); no compile takes it.
[Its specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7081094449) has its five gates, and
[the node machine's page](node-machine.md#the-machine-with-no-node-of-the-walks) the piece whole.

### What it is

The boundary takes a body's function from outside the reader; nothing made one. The machine that could is the
reference (T, the census's tests, C and X), exact on every expression it takes, and it reads the walk's nodes: a
compile that takes a body from it would have no such node, since the walk is what it saves. This piece runs the
reference where that compile would. Before the coded walk acts at a function body's first token:

- a front of its own makes the body's records from the body's postorder positions, a chunk at a time, as the walk's
  hook runs it over the unit's;
- each record is written once to a graph of the piece's own, by the graph's own calls: its operator, its token, its
  operands, a leaf's value, a member, a type, a call's arguments, its context, and what the walk's flush gives a node
  of its token alone (its floating-point environment, its warnings' suppression);
- the machine at reader time runs on the chunk's roots with that graph and results of its own, the reader's own graph
  without its columns of nodes while it does. A condition's root is made a body after its operand, and the graph
  still holds the operand's records (the specification had the root made where its value is complete; keeping the
  records gives the same);
- a body's verdict is kept as it stands then: every root taken, or the first thing that stops it.

After the pipeline each root is paired by its token with the one the machine made of the walk's nodes, and every
field of the two must be the same.

### What it measured

| | bodies read before the walk | roots compared | differ | a body's every root taken, of its tokens |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 2,674 | 51,306 | 0 | 35.62% |
| Lua | 1,096 | 10,900 | 0 | 29.71% |
| the compiler's own source | 8,685 | 53,916 | 0 | 3.62% |
| musl | 18,584 | 43,196 | 0 | 31.76% |

The 9700X, medians of 7 runs, musl's of 3, TSC ticks at 3,792.9 MHz:

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| its reading: the compaction and the entries, the front, its graph's writing, ms | 13.97 | 4.19 | 25.65 |
| of it the front; the graph's writing | 5.61; 4.76 | 1.64; 1.64 | 13.38; 5.15 |
| the machine's first run on the roots, ms | 26.59 | 7.39 | 33.20 |
| **ticks a body's postorder position** | **270.5** | **216.4** | **156.7** |

### Against its gates

- **V1, exact: met.** No root's records, refusal, tests, tape or side differs, on the four corpora, with `-g` and with
  `-Wall -Wextra`, and none is run by one of the two alone. The reader's graph has no column of nodes while the
  machine runs, and nothing faults.
- **V2, no diagnostic missed: met on the corpora.** No root was taken before an event of the walk's at one of its
  tokens, and no root it took has, among its expression's tokens, one the walk's class actions left to its visit (the
  walk marks them; the piece counts after the walk's last chunk). A reading of the reader's 114 warning sites says
  why: 32 can run while the walk acts at a token, each behind a literal the front leaves, a token of the walk's own
  visit, or a string the resolution has decoded. The fixture has each of the three, and a front that makes a record
  of such a literal fails it.
- **V3, by body: met.** A body's verdict exists before the walk acts at it, and the front lost its waiting values in
  no body of the four corpora. The verdict is about the machine's roots: a braced initializer's elements are roots of
  the lowering's and none of the front's (4 of `sqlite3.c`'s bodies, 542 of musl's), which the statements' piece
  refuses.
- **V4, its cost: not met, not refuted.** 270 ticks a body token on `sqlite3.c` where the gate was 180 and 295
  refutes. Its parts read separately had 141 to 178: the reading is 14 ms where the front alone was 5, and the
  reference's sweeps with the census's tests 27 where they were 16 to 22.
- **V5, nothing else moves: met.** The ordinary compile at this piece's head against main's (`0a408b6a3`), seven
  paired rounds on each of the four workloads, the objects and diagnostics one set: `sqlite3.c` -0.165 ms (-0.508 to
  +0.476) and 0.573 M instructions fewer of 1,215.937 M; `main.c` -1.395 ms (-3.135 to +3.640) and 2.456 M fewer of
  8,929.771 M; Lua -0.028 ms and 0.218 M fewer of 724.553 M; musl -0.138 ms (-1.909 to +17.103) and 1.393 M fewer of
  6,635.856 M. The walk's loop has the front's hook in another shape, and nothing of the piece runs in a compile.

### What it means

- **The machine can run with no node of the walk's, exactly.** What the design asked of the walk's graph is a copy of
  the front's records and what a token's own columns give.
- **As it stands it pays nothing.** A taken body token is worth 295 ticks and this machine costs 270 a token swept:
  25 a token taken, less 270 a token swept and refused. With a third of the tokens taken it loses.
- **Where its 270 go is measured in part, and each part has a cheaper form already in the shadow:** the tests in the
  machine's own form are half the census's and on the window a fifth; the window's kernel, C and X are 39 ticks a
  record; and a T that read the front's records would write no graph (4.8 ms).
- **It retires the specification's forecast for the compile that takes bodies.** That forecast (5.6 to 8.5 ms at 53%
  of the tokens taken) was built on 141 to 178 ticks a token. At 270 a token swept and 295 a token taken, every
  token of `sqlite3.c`'s bodies taken would give 3.7 ms, under that step's gate of 4. And a refusal before the sweeps
  has to come before the front: the compaction, the entries and the front are 55 ticks a position by themselves, where
  that step's gate was 20 ("A scan of a body's tokens before its reading", below).

### The machine's run by sweep

`measure-node-machine --shadow --front-alone-split` reads the clock before each of the machine's sweeps and after the
last, in its first run (five reads a root; the run's whole reads the same with them, 100.8 M ticks on `sqlite3.c`).
[The record](https://github.com/buster14a/buster-performance/tree/b9f4f4de7/docs/measurements/takeover-2850-2026-11-22#the-machines-run-by-sweep-machine-alone9700x-split-branch-claude2850-alone-split-at-2aeb81306)
(`b9f4f4de7`; the 9700X, CPU 2, Release, under the lock; medians of 7 runs, musl's of 3; ms, and ticks a body's
postorder position):

| | `sqlite3.c` | | Lua | | musl | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| T | 6.20 | 41.3 | 2.04 | 38.1 | 9.79 | 26.1 |
| the census's tests | 13.37 | 89.2 | 3.65 | 68.1 | 15.44 | 41.1 |
| C | 2.90 | 19.3 | 0.78 | 14.5 | 3.69 | 9.8 |
| X | 2.44 | 16.3 | 0.59 | 11.0 | 2.90 | 7.7 |
| the rest of the run (its loop, the roots' ranges, the settling) | 1.67 | 11.2 | 0.40 | 7.5 | 2.04 | 5.4 |
| the machine's first run | 26.56 | 177.3 | 7.48 | 139.9 | 34.01 | 90.6 |

- **The tests are half the machine's run on each corpus,** and a third of the piece whole: 89 of `sqlite3.c`'s 270
  ticks a position, where T is 41, the front 37 and the graph's writing 32.
- **Their cheaper forms do not run without T today.** The census's code on T's records is the only form of the tests
  that runs without the window: the pass over T's columns that read half of it ("The tests in the machine's own form")
  became the window's loop, which reads a fifth, needs the window's front and kernel, and sends every listed
  expression (an initialization's root always is) back to T's records.
- **No figure follows for the piece with them.** Their cost before the walk is not read; and a forecast of a compile
  has to carry what the bodies refused after their sweeps cost (the bodies T, the tests or C and X stop hold 15.4% of
  `sqlite3.c`'s body tokens), which the forecast this piece retired left out.
- **The order of the pieces after this one is on the issue,** in
  [one comment](https://github.com/buster14a/buster/issues/2850#issuecomment-7083506418), and nowhere on this page.

### Its ticks by a body's verdict

The same option keeps each lowered body's ticks beside its verdict: its reading's, its machine's first run's, and
those spent by the end of the chunk in which it is first stopped.
[The record](https://github.com/buster14a/buster-performance/tree/5a44ff50d/docs/measurements/takeover-2850-2026-11-22#the-pieces-ticks-by-a-bodys-verdict-and-what-a-scan-of-the-tokens-would-refuse-machine-alone9700x-verdicts-branch-at-b3bb0de89)
(`5a44ff50d`; the 9700X, CPU 2, Release, under the lock; medians of 7 runs; ms), on `sqlite3.c`'s 568,438 body tokens:

| a body's verdict before the walk | bodies | its tokens | its reading | the machine's first run |
| --- | ---: | ---: | ---: | ---: |
| every root taken | 1,856 | 35.6% | 4.65 | 9.89 |
| a root the front lost | 514 | 49.0% | 5.39 | 11.98 |
| T refuses a root | 52 | 2.7% | 0.33 | 0.83 |
| the tests refuse one | 130 | 6.9% | 0.87 | 1.93 |
| C or X refuses one | 116 | 5.9% | 0.69 | 1.60 |

- **Half the body tokens are in bodies the front loses a root of, and 17.4 of the piece's 38.2 ms go to them.**
- **The bodies refused after their sweeps are 15.4% of the tokens and 6.25 ms.** A machine that takes bodies pays that
  for nothing, a term the retired forecast did not have. Stopping a body at its first refused root saves little of
  it (5.77 of the 6.25 are spent by the end of the chunk that stops it): a refused root is late in its body as often
  as early.

### A scan of a body's tokens before its reading

What refuses the first of those classes before any work on it is in the tokens. `node_alone_scan` reads a body's
tokens' kinds and parents (the parser's) and the walk's visit bytes (the reader's), before any of the body is
compacted: a token the front leaves (a `sizeof` or `_Alignof` by its kind, a token of the walk's own visit by its
class, a character constant with an escape or of several characters by its spelling) loses a root where its value
reaches a root's token (a statement's terminator, a condition, a return, an initialization) through the parents that
take values. Under a declarator or a label it reaches none, and the body may still be whole. In the shadow the scan
refuses nothing: every body is read and compared as before, and the report counts, by a body's verdict, the bodies the
scan finds a lost root in, with its own ticks.
[The record](https://github.com/buster14a/buster-performance/tree/5a44ff50d/docs/measurements/takeover-2850-2026-11-22#a-scan-of-a-bodys-tokens-for-a-root-the-front-would-lose-machine-alone9700x-scan-branch-at-90c42e997)
(the 9700X, CPU 2, Release, under the lock; 7 runs, musl's 3):

| | `sqlite3.c` | Lua | musl |
| --- | ---: | ---: | ---: |
| bodies whose root the front lost that the scan finds one in | 513 of 514 | 279 of 279 | 584 of 584 |
| whole bodies it finds one in | 0 of 1,856 | 0 of 630 | 0 of 1,871 |
| tokens it read (it stops at the first it finds) | 374,620 | 121,860 | 619,925 |
| its ms | 0.383 | 0.146 | 2.294 |
| its ticks a body's postorder position | 2.56 | 2.73 | 6.11 |

- **So a refusal before the reading costs 0.4 ms on `sqlite3.c` where reading and sweeping those bodies costs 17.4,**
  and loses no body the machine takes. The specification's gate for it was 20 ticks a body token.
- The one body of `sqlite3.c` it leaves (`sqlite3OpcodeName`, 404 tokens) is a block's static table of strings, whose
  lost root is no token's of the three kinds. The compiler's own source: 748 of 748, none of its 222 whole bodies.
- It is not a rule of the language: a body it passes may still lose a root (the front's own stack, a constant out of
  range), and is then read and refused as today. What it must not do is find one in a body the machine takes, which
  the fixture and the four corpora count.

### Whether its cost is its place's

Two readings on the 9700X
([the record](https://github.com/buster14a/buster-performance/tree/132739984/docs/measurements/takeover-2850-2026-11-22#is-the-references-cost-its-places-every-body-first-and-its-memory-touched-first-machine-alone9700x-first-9700x-touch)).
With every body read before the walk's first chunk (below) the machine costs 273.6 ticks a position on `sqlite3.c`
against 271.5 in turn in the same job: reading the bodies in a row, with no walk between them, changes nothing. With
every page the piece reserves touched before its first body (an experiment's patch, not here) it costs 261.9 against
272.5: first touches of its own memory are 1.4 ms of its graph's writing and 0.3 of the tests. So the reference costs
about 262 ticks a position it sweeps, where a taken token is worth 295.

### Every body read before the walk's first chunk

The comparison reads a body before the walk acts at it, with the bodies before it walked already. A compile that
takes bodies in a row walks none of them, so `measure-node-machine --shadow --front-alone-first` reads every body of
the unit before the walk's first chunk. The same roots on `sqlite3.c`, Lua, the compiler's own source and musl
(51,306, 10,900, 53,916 and 43,196), no difference, none run by one of the two alone, none taken before an event of
the walk's or with a token its actions left
([the record](https://github.com/buster14a/buster-performance/tree/5a44ff50d/docs/measurements/takeover-2850-2026-11-22#every-body-read-before-the-walks-first-chunk-machine-alonedevelopfirst-18fd6b349-branch-at-18fd6b349)).
Nothing the machine reads of a body is left there by the walk of another.

## The redesign's third piece: a body's entries from its controls and its tapes

[Its specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7083062068).
`measure-node-machine --shadow --bodies`
([the guide](commands/lowering.md#a-bodys-entries-from-its-controls-and-its-tapes-2850);
[the node machine's page](node-machine.md#a-bodys-entries-from-its-controls-and-its-tapes)
has how each value is made).

A stream that takes a body has to write the function the lowering writes. The machine's tapes were only ever compared
with the entries their expressions' nodes own, relative to the expression's first: nothing had shown that tapes and
controls make the whole function, with its controls' own entries, its labels and its temporaries' locals where the
lowering puts them. This piece makes each lowered body's stream again, after the pipeline, from the program's controls
and the tapes of the body's roots, and compares it with the lowered function's, entry for entry. It is the part every
variant of the stream needs, whatever makes the tapes.

### What it measured

Develop builds on this session's machine (counts), `scripts/bodies.sh` of
[the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/takeover-2850-2026-11-22):

| | `sqlite3.c` | Lua | the compiler's own source | musl |
| --- | ---: | ---: | ---: | ---: |
| lowered bodies | 2,668 | 1,096 | 1,498 | 2,756 |
| every popped root the machine's, of the body tokens | 48.58% | 38.35% | 4.82% | 33.15% |
| attempted: bodies; of the body tokens | 2,086; 44.35% | 671; 29.94% | 271; 4.18% | 1,447; 29.38% |
| the lowered function's, entry for entry; that differ | 2,086; 0 | 671; 0 | 271; 0 | 1,447; 0 |
| with `-Wall -Wextra`: attempted; that differ | 1,704; 0 | 602; 0 | 226; 0 | 1,286; 0 |

With `-g` the default row's bodies are attempted again and none differs.

### Against its gates

- **S1, exact: met.** 12,768 bodies in the three flag sets, each the lowered function's in kind, class and value at
  every entry, with a temporary's local absolute and every branch's target an entry.
- **S2, what it leaves out, counted:** of the bodies whose every root the machine takes, 48 of `sqlite3.c`'s 2,134 have
  a control of another kind (4.23% of the body tokens), 57 of Lua's, 8 of the compiler's own and 51 of musl's: a
  `switch`'s in 48, 57, 7 and 34 of them, an `asm` statement in 1 of the compiler's own and 17 of musl's. One body of
  musl's and one of the compiler's own have an initialization of another form, and one a result of another class.
- **S3, nothing else moves:** the ordinary compile at the piece's `eec7ca784` against the branch it stands on
  (`0dd1dfafa`), seven paired rounds a workload on the 9700X, the objects and diagnostics one set: `sqlite3.c` +0.247 ms
  of 111.4 (-0.189 to +18.056, one round's outlier; 1,215.364 M instructions both), Lua +0.035 of 64.1, musl -1.976 of
  663.1 (-3.547 to +1.988), and the compiler's own source +1.472 of 598.5 (+0.209 to +4.140, all seven above; 54
  thousand instructions more of 8,927 M). No compile runs the piece's code: what the last pair reads is the
  executable's layout with it in.

### What it says

- **The splice is settled before anything is built on it.** What the approach's refutation named did not happen: no
  body's locals or labels are numbered by something its tapes and its controls do not hold.
- **It reads three things a stream does not have yet:** the program's controls, whose folding of a constant `if` reads
  a node's value; the lowered function's locals for a declared object; and the machine after the pipeline, on the
  walk's nodes, with the tape's labels' places and calls' records at hand, which reader time only digests.
- **The function it equals is the shadow's pipeline's,** which is `-c`'s without the statement controls' path
  (`PPOptions.statement_controls`, which the driver sets for a compile and `measure-node-machine` does not): there an
  expression statement's value has its cast to void for a root. That path leaves a compile's objects as they were
  (#3267), so the entries are the same ones; the piece does not show it, and the boundary's recording, made in a
  compile, is what a producer is compared with.
- **It prices nothing.** It runs after the pipeline and makes no function a compile uses.

## The redesign's fourth piece: the same comparison before the walk

[Its specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7083525675).
`measure-node-machine --shadow --front-alone --bodies`
([the guide](commands/lowering.md#a-bodys-entries-from-its-controls-and-its-tapes-2850);
[the node machine's page](node-machine.md#the-same-comparison-before-the-walk) has both comparisons whole).

The third piece's function was made of the program's controls and of tapes the machine made after the pipeline. A
producer has neither: it has the tokens, and what the machine with no node of the walk's kept before the walk. This
piece replaces each of the two, one at a time, so that a difference names its cause: the bodies' entries again from
the tapes kept before the walk, each control finding its root by its own token; and the controls themselves from the
tokens, by a twin of the program's builder that reads no node, compared with the program's.

### What it measured

Develop builds on this session's machine (counts);
[the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/takeover-2850-2026-11-22):

| | `sqlite3.c` | Lua | the compiler's own source | musl |
| --- | ---: | ---: | ---: | ---: |
| whole before the walk, of the body tokens | 35.62% | 29.71% | 3.62% | 31.76% |
| the kept tapes: attempted; of the body tokens; that differ | 1,814; 32.79%; 0 | 580; 23.12%; 0 | 205; 2.84%; 0 | 1,287; 20.94%; 0 |
| the controls' twin: attempted; its controls; that differ | 1,791; 21,661; 0 | 572; 4,218; 0 | 205; 1,209; 0 | 1,308; 7,314; 0 |
| bodies the program folded an `if` of; attempted there | 9; 0 | 1; 0 | 2; 0 | 71; 0 |

With `-g` the same, and with `-Wall -Wextra` fewer bodies and none differing: 11,145 bodies from the kept tapes and
11,120 from the twin in the three flag sets.

### Against its gates

- **Both exact: met.** Twenty single faults of the piece each fail its fixture.
- **Counted:** the kept tapes leave out, of the bodies whole before the walk, a `switch`'s control (33 on
  `sqlite3.c`), a braced initialization (4) and a constant `if` (5); the twin a `switch` (32), a block's static or
  extern declaration (28) and a range the program dropped (5). Both attempt about a third of `sqlite3.c`'s body
  tokens, a fifth of Lua's and musl's.
- **Nothing else moves:** the ordinary compile at the piece's `f9f39dbd9` against the branch it stands on
  (`cbdcbc070`), seven paired rounds a workload on the 9700X, the objects and diagnostics one set: `sqlite3.c` -0.051
  ms of 111.2 (-0.147 to +17.929, one round's outlier; 1,215.364 M instructions both), the compiler's own source
  +1.646 of 598.5 (-1.395 to +2.626), Lua -0.102 of 64.1, musl -2.425 of 662.3. And the machine's own timed run with
  what it now keeps beside each tape: 274.6 ticks a position on `sqlite3.c` where it read 272.9, 220.3 on Lua where
  it read 219.2; the keeping is in the run that keeps the results, which is not timed.

### What it says

- **A control's token is enough to find its tape,** and the tape with what is kept beside it is enough to splice it.
- **The program's controls are its tokens' for these bodies,** but for the constant `if`, which only a root's value
  says: a producer refuses on any constant condition of an `if`, which is wider than what the program folds.
- **The first report a taken body would lose is proved absent from the same controls:** the return flow's, for all
  but one of the 1,791 bodies the twin attempts on `sqlite3.c` (their END is out of reach with every branch taken both
  ways), where a test of the tokens alone held for two thirds.
- **What a producer still does not have** is the locals' table, the calls' rows' locations, the references and the
  silence of the other reports the program would have made of the body.

## Step 4's first piece: a census of Tier 0's row

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier0-census-2874-2026-11-16)
(`a465b001`), [#2874](https://github.com/buster14a/buster/issues/2874). A
count on Tier 0 as it is, at main `3d0d56ed`: no object's bytes change and no
function of a compile does. The budget gives "Tier 0 to the code's atoms, the
object and the rest" 10 of the 73 cycles a token, 18 a typed entry, from 78.

### What it is

- **The row** is what `-c` runs from a function's lowered typed stream to the
  object's bytes: Tier 0, the immediates' form choice, branch sizing, the
  encoder, the unwind rows, the atoms and relocations and the object writer.
  `sqlite3.c` has 395,061 typed entries for its 716,136 tokens (0.55 a token)
  and 354,742 records; Lua 89,543 entries and musl 199,891.
- **The row by part.** User-mode samples of cycles, instructions,
  mispredicted branches and first-level fills, of each corpus's compiles and
  of `benchmark-backend-source`. A Release compiler inlines Tier 0's passes
  into two functions, so each sample is given to the pass its inline chain
  names; libc's samples go to their callers by the samples' branch stacks. In
  the benchmark a part is its profile less a control's, which runs the
  validation in the timed passes' place.
- **What an entry's records depend on.** `measure-tier0-entries`
  ([guide](commands/tier0.md#the-entries-census-2874)) compiles each function
  with Tier 0 as it is. A sample is an entry that makes records with the
  entries it absorbs. Its answer is its record pattern: forms and operands by
  role, with a template's constant, a symbol, a branch's target and a frame
  displacement as holes. Its inputs are 40 bytes a producer could write
  beside the entry: the kinds and classes of the three entries before, each
  value's class by magnitude, the homes' cases, the depths, a call's counts
  and flags. A head's kind at a time, it searches the smallest sets of those
  bytes that leave no key with two patterns.
- **What no table removes.** The encoder, branch sizing and the object: as the
  first count has them in a compile, and at the rates of
  `benchmark-x86-encoder`, `benchmark-branch-sizing` and
  `benchmark-object-writer` on the corpus's own records and bytes.

### What it measured

On the 9700X, CPU 2, under the lock.

**The row by part**, `-c sqlite3.c`, a typed entry; the last column is cycles
a token:

| part | instructions | cycles | mispredicted | fills | cycles a token |
| --- | ---: | ---: | ---: | ---: | ---: |
| the stream's decode | 27.1 | 9.1 | 0.060 | 0.21 | 5.0 |
| the selection | 71.8 | 31.2 | 0.233 | 0.64 | 17.2 |
| the homes and the frame | 19.8 | 8.9 | 0.102 | 0.07 | 4.9 |
| the records' emission | 57.7 | 20.4 | 0.136 | 0.41 | 11.3 |
| the encoder | 13.6 | 4.3 | 0.015 | 0.06 | 2.4 |
| branch sizing | 12.6 | 6.4 | 0.030 | 0.23 | 3.5 |
| the atoms and relocations | 24.1 | 7.4 | 0.043 | 0.10 | 4.1 |
| the object's sections | 13.9 | 4.4 | 0.056 | 0.10 | 2.4 |
| the unwind table | 32.8 | 10.7 | 0.044 | 0.10 | 5.9 |
| the emission's driver | 19.0 | 13.6 | 0.064 | 0.36 | 7.5 |
| the static data's atoms | 2.8 | 0.9 | 0.005 | 0.01 | 0.5 |
| **the row** | **295.1** | **117.3** | **0.788** | **2.30** | **64.7** |

- **The row is 117 cycles and 295 instructions a typed entry, 64.7 cycles a
  token**, in user mode. The stage clock's nine stages are 9.38 ms: 72.5
  cycles a token with the kernel's time in them.
- **Tier 0 itself is 70 of the 117 cycles** (the first four parts) and 176
  instructions. In `benchmark-backend-source` the four are 65.1 cycles and
  173.2 instructions, where its own counters read 64.9 and 174.4; the issue's
  153 were an older main's. By pass, cycles an entry in a compile: the
  fusion 6.4, the templates' words 9.6, measuring the homes' cases, lengths
  and positions 8.5, the immediates' forms 3.6, the expansion 12.9, the
  decode's three passes 9.1, the homes, the frame and its prologue 8.9.
- **The last three parts are not in the issue's list** and are 18.5% of the
  row's instructions: the unwind table, which costs as much as the encoder
  and branch sizing together; the emission's driver (the bodies' loop, the
  windows' copies, the inline callees' table); the static data's atoms.
- **The deferred entries** (calls, copies, a division whose fixed registers
  are taken: 3.5% of the entries, nearly all calls) are counted and written
  by code of their own: 9.0 of Tier 0's 69.7 cycles an entry, about 255
  cycles and 640 instructions each.
- **Outside the row,** Tier 0's validation of the stream, which `lower` runs,
  is 13.9 cycles an entry more.
- **Lua and musl** are a process a file (2,634 and 150 entries each), and a
  part holds its work a file and its first run in a new process: the row is
  273 and 732 cycles an entry, 42.8 and 19.4 a token. In the benchmark,
  where neither counts, Tier 0 is 84.7 and 98.0 cycles an entry (220.9 and
  266.6 instructions). musl's long double code adds the x87 values'
  transfers, 13 of the 98.

**What an entry's records depend on.** The share of the entries a key of that
many bytes decides with no conflict, the entry's kind being its first byte
(in brackets, the share whose pattern is their key's commonest):

| key bytes | `sqlite3.c` | Lua | musl | the three as one |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 13.49 (60.76) | 11.48 (62.57) | 11.93 (52.24) | 11.94 (57.92) |
| 2 | 40.87 (85.58) | 46.64 (87.04) | 28.62 (79.09) | 23.48 (82.91) |
| 3 | 61.28 (94.96) | 73.42 (96.15) | 59.48 (92.60) | 55.17 (93.85) |
| 4 | 86.17 (98.95) | 93.60 (99.30) | 84.61 (97.96) | 77.69 (98.49) |
| 5 | 95.62 (99.76) | 98.55 (99.80) | 94.70 (99.42) | 93.49 (99.58) |
| 6 | 99.47 (99.94) | 99.77 (99.95) | 98.81 (99.74) | 97.51 (99.82) |
| 7 | 99.88 (99.99) | 99.95 (99.99) | 99.49 (99.84) | 99.54 (99.92) |
| 8 | 99.99 (100.00) | 100.00 (100.00) | 99.68 (99.90) | 99.83 (99.95) |

- **A key of 8 bytes or fewer decides 99.83% of the three corpora's 684,495
  entries** as one population, and 99.99%, 100% and 99.68% of each alone.
- **The tables are not small, and a corpus's keys are mostly its own:** 5,906
  rows on `sqlite3.c`, 2,383 on Lua, 13,361 on musl and 24,650 on the three
  together, more than their sum. The patterns converge: 700, 456 and 827
  alone, 1,041 together.
- **What decides:** without the entry's own class the keys leave 48.6% of
  the three corpora's entries undecided; without the kinds and classes of
  the entries before it 37.5%; without a value's class 32.6%; without a
  depth 13.0%; without a home's case 5.9%; without a call's counts 2.6%.
- **No entry needs an operand's value or a type's identity.** The 334
  entries no key decides are musl's: 288 are long double values', whose
  records the x87 values' transfers rewrite by what reads the value later
  (52 of them constants, three stores of their own bits); 44 are `X86`
  entries, a row of the function's table of operations; 2 are `alloca`s.
- **One input came from a first census** that left 682 pointer additions
  undecided: `p + 3` and `p + 20` of 8-byte elements differ in their scaled
  constant's immediate alone. Its class is the fortieth input.
- **A third of the entries are absorbed** by another's template, and a tenth
  are in groups that make no record (a discard, a return the epilogue
  covers).

**What no table removes**, cycles a token on `sqlite3.c`:

| | in a compile | at its own benchmark's rate |
| --- | ---: | ---: |
| the encoder | 2.37 | 1.64 (3.32 cycles a record) |
| branch sizing | 3.52 | 3.32 (6.70 cycles a record) |
| the object | 2.45 | 1.36 (its build, 12.85 GB/s) |
| **the three** | **8.33** | **6.32** |
| the atoms and relocations | 4.09 | |
| the unwind table | 5.90 | |

The object's write is the kernel's (step 5: 1.7 cycles a token). Lua's three
are 6.88 in a compile and musl's 3.44, a token.

**A by-product.** The keys show a conditional branch's group as `CONST 0`,
`NE` and the branch, after a compare: the lowering tests a condition's value
against zero, so `if (a < b)` is `cmp; setl; movzx; test; je`. 8,915 of
`sqlite3.c`'s 16,876 conditional branches are that, 7.5% of its instructions
([#2877](https://github.com/buster14a/buster/issues/2877)).

### Against its gate

- **95% of the row's instructions in a part of item 1: missed as written.**
  The issue's eight parts hold 81.5% on `sqlite3.c` (75.0% on Lua, 66.8% on
  musl). Every instruction of the row is in a named part: the three the list
  doesn't have are the other 18.5%.
- **A key of eight bytes or fewer decides 95% of the entries with no
  conflict: met**, 99.68% at the least.
- **Item 3's parts at or under 10 cycles a token: met**, 8.33 on `sqlite3.c`
  in a compile.

**Step 4's allocation is not refuted** by the issue's two conditions: no
entry's selection needs a wide input (0.014% of the entries), and the
encoder, branch sizing and the object are 6.32 cycles a token at their own
benchmarks' rates.

### What it means

- **The 10 can't come from the selection alone.** Tier 0 is 38.4 of the
  row's 64.7 cycles a token. With Tier 0 at nothing the row is 26.3: the
  driver 7.5, the unwind table 5.9, the atoms and relocations 4.1, branch
  sizing 3.5, the object 2.4, the encoder 2.4, the static data 0.5.
- **What no table removes leaves 1.7 to 3.7 of the 10** for parts that take
  56.4 today. The conditions the issue set don't refute the allocation; these
  counts say it holds only if the unwind table, the atoms and the driver
  shrink with Tier 0, or the stream has fewer entries.
- **An entry's records are a table's answer on bytes,** as the design asks of
  every stage, but not a table two registers hold: thousands of rows that a
  corpus doesn't close. The answer is Tier 0's own rules, which make a
  three-byte key by lane arithmetic today and gather the template's words on
  it.
- **A key written beside the entry replaces at most 25 of Tier 0's 70
  cycles an entry:** the decode 9.1, the fusion 6.4, the templates' gather
  9.6. The other 45 come after an entry's template is known: measuring its
  lengths and positions 8.5, the immediates' forms 3.6, the expansion and
  the output's arrays 14.6, the deferred entries 9.0, the homes and the frame
  8.9.
- **The deferred entries are where an entry costs most.** A call's records
  are decided by bytes too (its class, its depth, its counts of register and
  stack arguments, its flags: 154 patterns in 444 rows on `sqlite3.c`), so a
  call can be a template's.
- **For step 4:** a design on #2874, from these rows. It has to say what the
  unwind table and the atoms become, since they are 10 cycles a token by
  themselves.

## Step 4's row on main, and calls from a memo

[The record of the row on main](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier0-row-main-2874-2026-11-23)
and [the memo's](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier0-calls-memo-2874-2026-11-23),
[#2874](https://github.com/buster14a/buster/issues/2874). Main `9d927943`,
`-c sqlite3.c`, the 9700X, CPU 2.

**The row then** is 8.27 ms, 63.8 cycles a token, and `lower.validate` 1.15 ms
more. Its stages take no fault and no system time.

- **Tier 0 is about 165 instructions and 0.52 mispredicted branches a typed
  entry.** Emission takes 33 of the instructions, measuring 26, fusion 21, the
  deferred entries' emitter 19 and selection 15.
- **A warm Tier 0 is 50.9 cycles an entry against 71.4.** Warm means the same
  body compiled again at once. The gap shows at every body size, so it is not a
  body's fixed part. It is not the output's fresh memory either (+1.3 cycles
  with it), nor the interleave with the lowering (`--emit-window=0` gives
  4.70 ms against 4.78). What remains is Tier 0's own predictors and caches
  between one body and the next.
- **The calls:** counting and emitting every call once more costs 2.27 M
  cycles, about 175 cycles a call, warm.

**Calls from a memo** (tier0.md's "The calls' memo"): a call's records from a
table of the thread's, keyed by every input of the emitter, with the save
area's base and displacements and the callee patched in.

- **Exact:** every object is main's on `sqlite3.c` (with and without `-g`),
  Lua, musl's units, `main.c` and `tests/**/*.c` (2,928 compiles). A call's
  key holds every input of its records, a `vzeroupper` before it (#3335)
  among them; `test-tier0` compiles each generated function again with an
  empty table and requires the same code.
- **Cost:** `tier0` from 5.032 to 4.774 ms on main `e5c975f1` (medians of 9
  in turn), 0.26 ms less; 0.20 less on `9d927943`. The piece's gate was
  0.30 ms less, refuted under 0.15: missed, not refuted.
- **Its first form, measured too,** kept a byte key built an argument at a
  time, hashed it a word at a time and compared and copied with `memcmp` and
  `memcpy`, and saved 0.09 ms: a call's cost is its chain of dependent loads and branches more than
  its instructions.

**The VEX scan without gathers** (tier0.md's "VEX forms"): #3388's rewrite of
a function's legacy SSE records at a target with AVX found them by two gathers
every 16 records; `x86_vex_find` reads 32 records' forms four records a load
and their entries from the table's window of 192 rows in six registers.
`tier0` from 4.779 to 4.633 ms on the memo's branch (medians of 11 in turn), the
objects unchanged ([its record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier0-vex-scan-2874-2026-11-23)).

**The deferred entries in loops of their own** (tier0.md's "The two paths"):
selection's touches and measuring's counts of the deferred entries (calls
above all) ran an entry at a time inside each pass's loop over the blocks,
whose end at a block's last deferred lane mispredicted and kept the blocks
behind it waiting. Both passes now list them and count them in a loop of their
own, measuring's prefix sums run in a third, and fusion counts a call's
arguments' stacks 64 classes at a time. `tier0` from 4.642 to 4.317 ms on the
VEX scan's commit (medians of 11 in turn), the objects unchanged. Emission's
deferred entries then went after its blocks too, their references merged with
the templates' by instruction: from 4.313 to 4.156 ms. And a body's columns
went into a few pushes of the arena, where each took one (about 30 a body, 10
cycles each): from 4.138 to 4.084 ms. The same loops over a block's lanes in
fusion (its global entries' accesses, its calls' effects) and in the labels
(their branches' targets) went into loops over lists too: from 4.081 to 4.053
ms.

**What the row needs** is still about 40 instructions an entry where Tier 0
takes 165. No single pass holds the rest: emission is one fifth of it.

## Step 4: what Tier 0's cycles wait on, and its passes over a window's bodies

[The census](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier0-causes-2874-2026-11-23)
and [the batches' record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier0-batches-2874-2026-11-23),
[#2874](https://github.com/buster14a/buster/issues/2874). Main `b7d0b5ed`,
`sqlite3.c`, the 9700X, CPU 2.

**The census** counts what the processor did with each pass's cycles. An
experiment patch stops Tier 0 after a pass and reads hardware counters around
`benchmark-backend-source`'s loop, so a pass's count is a difference of two
runs. The 9700X dispatches eight uops a cycle: a pass has eight slots a cycle.

| | cycles | instructions | uops | slots retired | front end empty | back end stalled |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| every function, an entry | 49.7 | 151 | 167 | 42% | 34% | 20% |
| functions under 64 entries, a body | 2,699 | 7,753 | 8,073 | 37% | 50% | 8% |
| functions of 512 entries or more, an entry | 35.4 | 126 | 147 | 52% | 8% | 38% |

- **A third of Tier 0's slots had no uop from the front end.** The passes
  after selection were the ones starved: 73% of the homes' and frame's slots,
  37% of measuring's and of emission's, 87% of the forms' choice.
- **The cause is the size of the code a function's compile walks:** about 690
  misses of the first-level instruction cache a function, 44 KB of code
  fetched from the second level where the first holds 32 KB, 9.2 misses of
  the op cache an entry, and 31% of the uops from the decoder.
- **About 29% of Tier 0's cycles are a body's fixed part.** A long body takes
  35.4 cycles an entry and the mean is 49.7: about 2,040 cycles a body. A body
  under 64 entries costs 2,699 cycles and 7,753 instructions whatever its
  size.
- **In a long body** the loops stay in the op cache, and what is left is uops
  (52%) and back-end stalls (38%). The three gathers are microcoded, 16 of
  the 147 uops an entry.

**The passes over a window's bodies** (tier0.md's "The two paths"): Tier 0's
passes after fusion are functions of a job, and `tier0_compile_batch` runs
each over every function of a batch before the next. The C driver's code half
prepares a window's bodies (each one's tier and its inline splice), has Tier 0
compile them together, then makes each body's code after Tier 0 in order.

- **Exact:** every object is main's on `sqlite3.c` (with and without `-g`),
  Lua, musl's units, `main.c` and `tests/**/*.c` (2,944 compiles), and at the
  AVX targets (4,478). `test-tier0` compiles each program's functions in
  batches and requires each one's own code.
- **Cost:** `tier0` from 3.971 to 3.662 ms (medians of 11 in turn), 0.31 ms
  less, and the row's stages from 6.74 to 6.37 ms: branch sizing, the encoder
  and the unwind rows run 0.09 ms faster with no Tier 0 between two bodies.
  The piece's gate was 0.40 ms less on `tier0`, refuted under 0.15: missed,
  not refuted.
- **The limits:** in one executable, a batch of one function reads 3.99 ms,
  of two 3.88, of four 3.76, of eight 3.71, and of 32 functions or 2,048
  entries 3.68. Larger batches read the same, and so do windows of 8,192
  tokens.

**After the batches**
([the fixed cost's record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier0-fixed-cost-2874-2026-11-23)),
on main `d4a6eb540`. In batches the back end stalls a third of Tier 0's slots,
so the question became what its loops wait on.

- **What moved nothing.** The three gathers as eight loads each: 10
  microcoded uops an entry gone, `tier0` 3.695 against 3.683 ms. Emission's
  choices, branches and references in scalar loops after its blocks: 0.11 ms
  slower. Four reorderings of the passes' chains, a knob each (the labels'
  bits from bytes, the depths' carry off the scan, the references in two
  tables, emission's hole blocks a block ahead): within 0.02 ms of none.
- **A memo of statements would not carry the front.** Keyed by what the
  front's passes read of a statement, 66.4% of `sqlite3.c`'s entries are in a
  statement seen before; with a table a unit, 51.8% of Lua's and 29.9% of
  musl's. And the homes, measuring and emission depend on the function's
  locals.
- **A function's fixed cost is its instructions.** A line through nine bins
  of `sqlite3.c`'s functions by size: 1,043 cycles a function and 34.9 an
  entry, so 16% of Tier 0 is the functions' fixed work. A function of five
  entries runs 5,119 instructions, 3.9 a cycle. The front's 64-entry blocks
  are 0.25 M of its 6.1 M cycles: sharing blocks across a batch's functions
  is not worth its machinery.
- **Samples at a waiting load overstate it.** In the functions under 16
  entries, `perf record` put 392 of 1,641 cycles a function at five loads,
  each right after stores it cannot be forwarded from (a masked store, a
  `memset` of under 64 bytes, a list stored a register at a time, a struct's
  two fields, a second copy of a struct). Each one removed alone, behind a
  knob, gained 9 to 28 cycles a function where it was sampled at 58 to 95,
  and moved `tier0` in a compile by nothing measurable: the processor works
  past the load, and retirement stops at it. Running the front's sub-passes
  each over the batch, so that every store is long in the cache when read,
  was 0.09 ms slower.
- **What landed from it** (#3486, every object's bytes unchanged; `-c
  sqlite3.c`, main, the first piece and both built from one tree, medians of
  30 with the order rotated every round): selection's words gathered a block
  ahead, `tier0` 3.660 to 3.616 ms; then the five loads without their
  stores, 3.616 to 3.582, with branch sizing 0.392 to 0.380 behind the
  driver's copies. The second piece's gate was 0.10 ms, from the samples:
  missed, and its premise refuted; it stays as the exact 0.03 ms it
  measured. The row is 6.33 to 6.23 ms.
- **An A/B in a fixed order reads the second executable 0.01 to 0.03 ms
  high on `tier0`.** The same executable read 3.644 as the second of a pair
  and 3.604 as the first, an hour apart; rotated among three it read 3.616.
  The pieces before this one were all the second of their pairs.

**Against the step's gate.** The row is about 6.2 ms on `sqlite3.c`, 47
cycles a token where the budget has 10: `tier0` 27 of them, and the stages
after it (branch sizing, the encoder, the unwind rows, the unit's parts and
the object) 20, twice the gate by themselves. Tier 0's cycles are spread over
a dozen passes at four uops a cycle with no pass above a quarter, a function's
fixed work is a sixth, and since the batches no local change has been worth
more than 2%. The gate needs fewer uops an entry by a factor of about five,
which is another machine for Tier 0's middle and for the emission after it,
not these passes made faster.

## Step 5's first piece: a census of the kernel's time and fresh memory

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/kernel-census-2873-2026-11-16)
(`ea733fbc`), [#2873](https://github.com/buster14a/buster/issues/2873). A
count on the compile as it is, `-c sqlite3.c` at main `3d0d56ed`; no
compile changes. The budget gives the kernel and the process's fixed costs 6
of the 73 cycles a token, from 120 today.

### What it is

- **The kernel's cycles by cause.** 24 compiles recorded on CPU 2 as a whole
  (`perf record -C 2`): the kernel's cycles with their call chains, every
  fault with its address and the size of the page it mapped, every mapping's
  record. A patch the record keeps and buster doesn't merge has
  `--time-stages` print its laps as CLOCK_MONOTONIC times, which put each
  sample in its stage. A sample's cause is the innermost way into the kernel
  on its stack; a sample in a fault's handler is that fault's.
- **Fresh memory by owner.** Each of the compile's 46 mappings is named by its
  `strace` stack's source line; the compiles run without address-space
  randomization, so each fault's address names its owner. gdb names the
  callers of the large `memset`s, and IBS op samples say which stages load
  each owner's pages again.
- **What a fresh page costs**, by how it is taken: a small program
  (`pagecost.c`), case by case, by its own counters.
- **The floor**: what would still come from the kernel with every arena
  reused or bounded by a window (#2536), at those costs and at the compile's.

### What it measured

On the 9700X, CPU 2, under the lock.

- **The kernel takes 86.5 M cycles a compile, 120.8 a token**, counted on the
  CPU. The process's own counters (`perf stat`, `measure-compile`) see
  79.8 M: Linux 6.18's `do_exit` stops a task's perf events before
  `exit_mm`, so no per-task count holds the address space's teardown, with
  the exit helper or without it. The teardown is 5.1 M of the difference.
- **By cause**, of the compile's own kernel cycles (each a token at the
  unrecorded total): page faults 81.9% (98.9), 57.9% the clearing of fresh
  2 MB pages alone; the teardown 5.6% (6.8); the arenas' own system calls
  4.7% (5.6: `munmap` 3.3, `madvise` 0.9, `mprotect` 0.8, `mmap` 0.6); the
  source's read 4.0% (4.8: the include search's 374 opens, 207 of them
  failed probes, 2.6, the source's populated mapping 2.2); the object's write
  1.4% (1.7); timer interrupts 1.2% (1.5); the start, exec to the first
  stage, 0.7% (0.8); the rest of the exit 0.5% (0.6). 0.07% is in no row.
- **By stage**, in a recorded compile's cycles: check-input 35 M of faults,
  parse 17.5 M, preprocess 7.8 M and 3.8 M of system calls (the source's
  read and the include search), check-expressions 5.6 M, check-program
  3.4 M; the backend's stages about 1 M each or less.
- **Fresh memory:** 305 MB first touched in 7,074 faults, for a 231 MB peak.
  The driver's scratch (96 MB: the parser's arrays, then the reader's), arena
  (80 MB: the reader's output) and output arena (48 MB: the graph) are 73% of
  the bytes and 60% of the faults' cycles. The reader's sparse columns are
  16 MB in base pages, 4,169 faults at 642 K cycles a MB, three times a huge
  page's; the parser's columns, the file cache's token arenas and the
  preprocessor's output are 12 to 16 MB each. The owners' faults are the
  faults counted: 6,907 against the 6,902 an unrecorded compile's counter
  counts.
- **Read again:** the parser's columns until Tier 0, the driver's arena and
  scratch until Tier 0 and the object, the type and intern tables; no sampled
  load of the reader's sparse columns, the lexer's tokens or the program's
  controls after the stages that write them.
- **A fresh page's cost:** a 4 KB page first written 2,252 kernel cycles
  (576 K a MB), a 2 MB page 461 K (230 K a MB, 95.6% of it clearing); a
  file's page with fault-around 179, populated 256, mapped alone 2,513; a page
  written over the last file on tmpfs 1,853. The compile pays two to four
  times as much for what it maps (703 cycles a page of the source, 10.9 K a
  fault of the executable's text), where the page cache's structures are cold.

### Against its gate

- **95% of the kernel's cycles in a named row: met**, 99.93%.
- **The owners' rows sum to the faults counted: met**, within 5 faults.
- **The floor at or under 6 cycles a token, the start and exit included: met
  only at the best case.** The source's mapping, the object's write, the
  tables that outlive a window (the intern and type tables, the fact words,
  the object's bytes: 3.6 to 5.8 MB, an estimate) with a window's working
  set, the stack, the start, the exit and its teardown are 5.8 to 6.4 cycles
  a token at the small program's costs, packed in huge pages, and 10.3 to
  12.5 at the compile's own costs in base pages.

**Step 5's allocation is refuted** by the issue's own two conditions. Beside
the floor, 5.2 to 6.7 cycles a token are neither fresh memory nor the start
and exit: the include search (2.6), the executable's own pages after its
start (2.5), the arenas' reservations and commits (up to 1.5). And at the
costs the compile pays, the source's mapping (2.2), the object's write (1.7)
and the start and exit (1.4) are 5.3 before any table.

### What it means

- **The kernel's row is about 11 cycles a token at the least** on this input,
  and 15 to 19 at today's costs: the design's 6 is out of reach by arena
  reuse and windows alone.
- **What would move the rest is not memory reuse:** the include search's
  probes and opens (a compile of 157 headers), the executable's touched code
  (177 faults, each mapping up to 64 KB of cold page cache), the source's
  mapping (a cold walk of the page cache a 4 KB page at a time) and the
  object's write.
- **Today's 121 is mostly the clearing of fresh huge pages**: 305 MB first
  touched where the floor is a few megabytes. That is #1701's list, and
  #2536's windows are its answer.
- **For measuring:** a compile's kernel cycles counted by its own task's
  counters leave out its teardown on this kernel; the CPU's count holds it.

## The redesign's fifth piece: beside a body's entries, and `-c`'s own path

The fourth piece's comparisons ran in a pipeline that was not `-c`'s own, compared a lowered function's entries and not
what its locals hold beside them, and said nothing of a body's references. This piece closes each against its oracle in
the shadow, and counts the two sets a producer would have ([`node-machine.md`](node-machine.md), "Beside a body's
entries").

### What it measured

Develop builds on this session's machine (counts);
[the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/takeover-2850-2026-11-22):

| | `sqlite3.c` | Lua | the compiler's own source | musl |
| --- | ---: | ---: | ---: | ---: |
| units the reader takes the statement controls' path in, as in `-c` | 1 of 1 | 29 of 34 | 1 of 1 | 196 of 1,336 |
| bodies compared there, after the pipeline and before the walk; that differ | 2,086 and 1,814; 0 | 671 and 580; 0 | 271 and 205; 0 | 1,447 and 1,287; 0 |
| bodies whose kept tapes name the program's references; that do not | 1,814; 0 | 580; 0 | 205; 0 | 1,287; 0 |
| a producer reads and sweeps: bodies; of the body tokens | 2,070; 45.90% | 741; 38.59% | 564; 12.11% | 1,470; 27.81% |
| a producer takes: bodies; of the body tokens | 1,786; 32.04% | 557; 22.01% | 200; 2.66% | 1,256; 20.31% |

A plain local's taken address and a temporary's class are the entries' in every attempted body (16,596 plain locals
after the pipeline, 472 with their address taken), a volatile local's left to the lowering's setup. With `-Wall
-Wextra` the same comparisons on fewer bodies, none differing, and 1,480, 504, 173 and 1,142 bodies taken.

### What it means

- **The comparisons hold in `-c`'s own pipeline,** so a compile's bodies are the ones compared.
- **A producer's price can be said from counts.** On `sqlite3.c` it sweeps 260,940 body tokens to take 182,105. With the
  reference machine a swept token costs about what a taken one saves (the whole bodies' reading and first run were
  14.66 ms for 202,451 tokens, 72 ns a token, against 77.7 ns saved), so the first compile that takes bodies is slower
  than main by about what the 78,835 tokens it sweeps and leaves cost, some 6 ms, before its own bookkeeping.
- **What is still not a producer's:** the body's own reasons are read from the program's controls and plans, and the
  twin's gates from the reader's columns as they stand after the walk. A producer decides before the walk.

## The redesign's sixth piece: the producer, deciding before the walk

The fifth piece's two sets were the oracle's: read after the walk, from the program's controls and the lowered
functions. A compile that takes a body has to decide before the walk reads it, and a taken body may have no diagnostic
of any stage, since every phase after the mask would skip it. This piece is the producer that decides there
([`node-machine.md`](node-machine.md), "The producer, deciding before the walk"), still in the shadow: no compile
changes.

### What it measured

Develop builds on this session's machine (counts), Release on the 9700X (ticks, 3,792.9 MHz);
[the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/takeover-2850-2026-11-22):

| the default warnings | `sqlite3.c` | Lua | the compiler's own source | musl |
| --- | ---: | ---: | ---: | ---: |
| lowered bodies | 2,668 | 1,096 | 1,516 | 2,756 |
| the set the comparisons give: bodies; of the body tokens | 1,760; 30.89% | 547; 21.48% | 179; 2.32% | 1,176; 17.67% |
| the producer takes, before the walk: bodies; of the body tokens | 1,747; 30.65% | 545; 21.42% | 0 | 1,096; 16.09% |
| of them outside the set; whose kept controls differ from the program's | 0; 0 | 0; 0 | 0; 0 | 0; 0 |
| bodies it does not read, as a compile would run it, of every body of the units | 610 of 2,674 | 354 of 1,096 | all 8,739 | 13,693 of 18,584 |

As a compile would run it on `sqlite3.c` (three rounds, medians, at `00ef81d5f`: before main's merge and the rules
added since, with 1,753 bodies taken): the bodies' reading 45.46 M ticks (11.99 ms), of
which the producer's own passes are 7.56 M before any reading and 7.43 M after it, the compaction and the entries
6.59 M, the front 10.28 M and its graph's writing 11.14 M; and the reference machine's one run 65.08 M (17.16 ms) for
258,955 positions, 251 ticks each. **110.54 M ticks in all, 29.14 ms.**

### Against its gates

- **P1, sound against the oracle: met.** On the four corpora, read as the walk reads them, as `-c` reads a unit and as
  a compile would run the producer: every body it takes is in the comparisons' set and its kept controls are the
  program's (twenty runs, each status 0); with `-Wall -Wextra` it takes none.
- **P1, sound against the diagnostics: met for what a log by location can see.** With the producer run beside every
  unit's ordinary read and nothing masked: 111,906 units of the checker's and the driver's tests, 74,783 of them
  with the producer run, 36,393 of 1,956,140 bodies taken, 1,191,188 diagnostics looked at, none at a token of a
  taken body; and none on the corpora (1,753, 545, 0 and 4,442 bodies taken). What it cannot see (a
  diagnostic reported outside the body that causes it, the object, a body another function's lowering reads) the next
  piece found three cases of by compiling small units with the bodies taken and without, and main's merge a fourth; each
  has a rule and a fixture here.
- **P2, counted:** the table above, with what it leaves by its reason in [`node-machine.md`](node-machine.md).
- **P3, its ticks:** above, three rounds on the 9700X at `00ef81d5f`, before main's merge and the rules added since,
  which leave six more of `sqlite3.c`'s bodies (1,753 taken then, 1,747 now).
- **P4, nothing else moves: not read.** No compile runs the piece. Its pair on the 9700X was queued with the next
  piece's and never ran: this session's forty-one build trees had filled that machine's disk, the build of the commit
  to time failed, and the job took the lock twelve times for runs that stopped at once. The trees are removed
  (47 GB) and the pairs run again on the merged heads.
- **P5, the next piece's price, said again:** about 16 ms slower than without the option, below.
- **Single faults:** 55 of 56 fail a fixture; the one that does not is in a form no path of this piece runs
  ([`node-machine.md`](node-machine.md), "Against the diagnostics").

### What it means

- **A producer can decide before the walk what the oracle decides after it.** It takes 1,747 of the 1,760 bodies of
  `sqlite3.c`'s set, 545 of Lua's 547 and 1,096 of musl's 1,176; what it leaves is a rule's doing, each a refusal that
  stands for a diagnostic of a later pass and refuses more than is warned of.
- **Its price is the reference machine's.** When it was timed it read and swept 2,065 of `sqlite3.c`'s 2,674 bodies
  to take 1,759; the program lowers 1,753 of those, with 175,797 tokens, which are worth 13.04 ms at the 74.2 ns a
  taken token saves (the other six are static functions nothing names). Against 29.14 ms, the first compile
  that takes its bodies is predicted about 16 ms slower than without them, where [the
  specification](https://github.com/buster14a/buster/issues/2850#issuecomment-7085227363) said 8 to 10 from the
  oracle's sets. The whole bodies alone cost 334 ticks a token to read and sweep (88 ns) against the 74.2 ns one saves:
  with the reference machine no coverage pays, and what pays first is its cost a position.
- **The corpora and the tests' units were not enough to find what it must refuse.** The compile built on it found
  three more things by compiling small units of one body each with the bodies taken and without, and this piece's
  rules have them since: a target that fuses a product and a sum, a naked function, and an `always_inline` wrapper.
  Main's merge brought a fourth, a volatile access, which the lowering's templates leave to its tasks since #3310.
  The machine is the lowering's mirror, and each change of the lowering's is a change it owes: the oracle on the
  corpora is what says so, and it has to be run on main's head before each piece lands.

## The redesign's seventh piece: a compile that takes the producer's bodies

`--node-bodies` on a compile to an object: the reader masks the bodies the producer takes before the walk reads any,
and the lowering writes each one's function from its kept controls and tapes
([`commands/lowering.md`](commands/lowering.md), "Compiling with the bodies taken"). It is the first piece in which a
compile does less of the old work for a body, and it is off by default: it is exact and slower.

### What it measured

The 9700X, Release, CPU 2, each pair the same binary with the option and without, at `06681bbdb`
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/takeover-2850-2026-11-22)):

| | without | with `--node-bodies` | paired; its least and most | bodies taken |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c`, seven rounds, ms | 117.382 | 140.247 | +23.064; +22.266, +23.419 | 1,753 of 2,674 |
| Lua's 34 units, five rounds | 65.663 | 76.331 | +10.680; +10.403, +10.776 | 545 of 1,096 |
| the compiler's own source | 617.975 | 636.013 | +18.549; +14.735, +21.119 | none of 8,739 |
| musl's 1,336 units | 662.998 | 757.900 | +95.027; +94.254, +95.739 | 4,442 of 18,584 |

On `sqlite3.c`: 95.7 M user cycles, 31.9 M kernel cycles and 207.8 M instructions more. Each pair has one set of
objects and one of diagnostics.

### Against its gates

- **C1, exact: met, by its outputs.** The corpora's objects, diagnostics and statuses are the ordinary compile's, plain
  and as position-independent code; so are 349 small units' and those units' under every option set that accepts one;
  and so is every output of the 43,914 commands of the tests' 34 blocks, replayed with the bodies taken and without.
  The tests pass with the option forced, with every body the producer takes and with each half of them. What found the
  piece's errors was the outputs, never the tests' programs: `commands/lowering.md` has the list and the counts.
- **C2, its price: 23.06 ms slower on `sqlite3.c`, and the prediction refuted.** The sixth piece said about 16, and
  that a reading over 22 would refute it. The producer's path was timed in ticks of its own code (29.14 ms) against
  what its tokens save (13.04 ms); the pair has 31.9 M kernel cycles more besides, some 6 ms, which a count of the
  path's own ticks does not see.
- **C3, the option off: nothing moves.** Against main's commit the branch stood on: -0.120 ms on `sqlite3.c`, -0.183
  on Lua, -4.950 on the compiler's own source, -2.637 on musl.

### What it means

- **The harness exists.** A piece that makes a taken body cheaper, or takes more bodies, is now priced by one paired
  reading with the option, and its exactness by the same nets.
- **A taken body costs more than it saves, and a unit pays before it takes any.** The compiler's own source takes no
  body and is 18.5 ms slower; musl's 1,336 small units are 71 µs slower each. What the producer sets up for a unit is
  to be paid for only by a unit that takes bodies, and its memory is part of its price.
- **The order of what follows is the cost's, not the coverage's:** the machine's run (251 ticks a position, 17 ms of
  `sqlite3.c`'s 29) in the forms that were measured at a fraction of that on the window, with a volatile access's bit
  first; the reading's two writes of the same positions; and the unit's fixed cost. Coverage (`sizeof`, a `switch`,
  the bodies refused after their sweeps) pays only once a taken token costs less than it saves.
- **A mirror is owed every change of what it mirrors.** Three things the lowering does and the machine did not were
  found under this piece: a sum fused with a product and #2983's wrappers, which were there before it, and #3310's
  volatile marks, which main merged during it. Each was a difference of an object with nothing said. The oracle on the
  corpora and the units' sweep are run on main's head before a piece of this redesign lands.

## The redesign's eighth piece: a unit that takes no body pays nothing

The seventh piece's pair read a cost that no taken body explains: the compiler's own source takes none and was 18.5 ms
slower with `--node-bodies`. The option began the reader's machine and the machine with no node of the walk's for
every unit, and both make and clear columns by the unit's tokens, before the producer asked the unit's own reason. A
compile asks that reason first now, and a unit with one has no front
([`commands/lowering.md`](commands/lowering.md), "Compiling with the bodies taken").

### What it measured

The 9700X, Release, CPU 2, paired, five rounds a pair and seven on `sqlite3.c`
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/takeover-2850-2026-11-22)):

| the option against without, ms | before, at `4fcadeb45` | after, at `b0ee1afaa` |
| --- | ---: | ---: |
| the compiler's own source (no body taken: a diagnostic pragma) | +18.549, at `06681bbdb` | -0.670; -2.062, +2.774 |
| `sqlite3.c` with `-Wall -Wextra` (no body taken: a warning option) | +5.820; +5.270, +5.955 | +0.289; -0.467, +0.429 |
| `sqlite3.c` (1,753 bodies taken) | +23.064, at `06681bbdb` | +23.659; +23.366, +24.051 |
| Lua (545) | +10.680, at `06681bbdb` | +10.650; +10.594, +10.987 |
| musl (4,442) | +95.027, at `06681bbdb` | +93.419; +92.641, +96.511 |

With the option in both arms, the head before against this one on `sqlite3.c`: -0.205 ms (-0.483 to +0.040, six of
seven rounds lower): the 0.6 ms between the two readings of `sqlite3.c` above is the two jobs', not the piece's. With
the option off, head against head: `sqlite3.c` +0.025 ms, the compiler's own source -2.273, Lua +0.093, musl +0.655.

### Against its gates

- **U1, the same verdicts: met.** The producer against the oracle on the four corpora, twenty runs, each status 0, the
  same bodies taken.
- **U2, exact: met.** The corpora's objects plain, with `-fPIC` and with `-Wall -Wextra`; the 349 units; the option
  sets' sweep (31,016 pairs); the driver's eight parts, with a unit that has a diagnostic pragma, whose line reads no
  body decided.
- **U3, its price: met.** The two units with a reason are within a millisecond of zero, and the three workloads that
  take bodies are not slower.
- **U4, the option off: nothing moves.**
- Two single faults (a unit with a reason still given a front; a diagnostic pragma that is no reason) each fail the
  option's test.

### What it means

- **A build with warning options no longer pays for the option at all.** Until the producer takes bodies under `-W`
  options, that is every unit of most builds.
- **What is left is a unit that takes bodies:** 23.7 ms on `sqlite3.c`. Its machine's columns are still by the unit's
  tokens, not by the bodies it reads; that, the machine's run and the reading are the next cuts, in that order of
  ease.

## The redesign's ninth piece: a unit pays for the bodies its producer reads

The eighth piece left a unit that takes bodies paying for its size. Stage tables and profiles of the same binary with
and without `--node-bodies` on `sqlite3.c` say where the option's 23.4 ms were: check-input +33.5 ms and the stages
after it -9.8; of the 34.5 ms the option adds, 5.3 the kernel's clearing of pages and 2.6 the setup's own code. An
experiment's patch (`mincore` on each range the setup pushes) read 367 MB reserved and 157 resident, 74.5 of them
before the producer reads a body: every column was made for the unit's nodes (a capacity of 780,125) where the bodies
the producer reads have 258,430 positions, and a second set of results and of the tests' state stood for the walk's
nodes, which no machine runs on in a compile.

The producer now asks what it asks before any reading of every body first, and the machine's columns are made for the
positions of the bodies that are left; a compile makes nothing that only the shadow reads
([`commands/lowering.md`](commands/lowering.md), "Compiling with the bodies taken"). The option's line under
`--time-stages` says the positions, the room and the bytes made, and the driver's test holds them: a unit of 300
thousand tokens outside its two small bodies takes 7.8 MB of the reader's arenas for the machine's state, where it
took 135.

### What it measured

The 9700X, Release, CPU 2, paired, seven rounds on `sqlite3.c` and five on the others
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/takeover-2850-2026-11-22)).
The option against without, before (`b0ee1afaa`) and after (`0f7795acd`), with what its specification predicted and
the line it named as refuting it:

| | before | predicted | refuted above | after; its least and most |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c`, the reader's peak added, MB | +145.6 | +55 | +75 | +60.2 |
| `sqlite3.c`, kernel cycles added, M | +31.0 | +12 | +18 | +12.5; +11.2, +13.5 |
| `sqlite3.c`, ms | +23.659 | +18.0 | +19.7 | +18.876; +18.291, +19.185 |
| musl, ms | +93.419 | +65 | +78 | **+80.265; +79.432, +81.342** |
| Lua, ms | +10.650 | +8.5 | +9.7 | +8.053; +7.969, +8.413 |

With the option in both arms, the head before against this one: `sqlite3.c` -4.598 ms (-5.166 to -4.329, seven of
seven lower), musl -12.794 (-13.396 to -12.056, five of five). The two units that take no body are unmoved: the
compiler's own source -0.938 ms with the option, `sqlite3.c` with `-Wall -Wextra` +0.163.

**musl's prediction is refuted:** 12.8 ms gained where 28 were predicted. Its profile at this head says why. Of the
79.8 ms the option still adds there, the kernel is 30.3 (`clear_page_erms` 20.5): a unit of a few thousand tokens has
3.3 MB of the machine's state whatever its bodies (two fronts' arrays, the machine's own columns, a tape's store), the
scratch's pages past what the driver keeps go back to the kernel after each unit's reader, and the next unit's state
takes them again, cleared. And the setup still reads the unit's tokens several times over (the bodies' list, the
objects' columns, the functions the unit defines, its enumerations): `node_alone_begin` is 5.9 ms and
`node_front_started` 2.7 over the 1,336 units, 1.7 and 0.5 on `sqlite3.c`. What the cut removed is what grew with
the unit's nodes; what a small unit pays is fixed state and passes over its tokens, which it did not touch.

With the option off, head against head: `sqlite3.c` +0.305 ms (two of seven lower), the compiler's own source -1.062,
Lua +0.042, musl +1.996 (none of five lower). Each of the four runs the same instructions to within 0.004% (musl
6,707.4 M and 6,707.1 M): the option's code is not on that path, and what differs between the two binaries is where
the rest of the code is placed. Pairs of other heads on musl have read -2.6 and +0.7 ms the same way.

### Against its gates

- **Z1, the same decisions: met.** The producer against the oracle on the four corpora, twenty runs, each status 0, none
  taken outside the set, every count the unit's cut read. Under the tests with the option forced the gate reads 36,510
  bodies taken in 4,985 units of 112,860, none with a diagnostic in one: the 36,485 in 4,962 of the unit's cut, the
  test's own two new compiles (4 bodies), and 88 more runs of the reservations' test (21 bodies), which fails each
  reservation of two units' compile in turn and so runs as many compiles as the compile makes reservations. No body
  is refused for room.
- **Z2, exact by its outputs: met.** The corpora's objects, diagnostics and statuses plain, with `-fPIC`, with
  `-Wall -Wextra` and with each half of the bodies; the 349 units; the option sets' 31,016 pairs; the driver's eight
  parts and the producer's fixtures; the 34 blocks forced with every body and with each half, and their journal's
  44,274 commands replayed, every output and status the same and 22 differing in `--time-stages`' text; the memory
  and the address rows forced, 34 blocks each and no report, and the sanitized compiler on 418 of the corpora's units;
  main.c compiled by itself; the no-recursion rule; the inventory, where no block fails that passes on main's own
  build.
- **Z3, its price:** `sqlite3.c` and Lua inside what was predicted, **musl refuted** (above).
- **Z4, the option off: the same instructions,** and times within what two builds differ by (above).
- **Z5, single faults:** seven single changes (a body's positions counted as one; a body's stored answer read for its
  neighbour; every other body left out of the count; the room without its slack; the columns made for the unit's
  nodes; the walk's report given the unit's nodes in a compile; the roots by token made in a compile), each built and
  run against the driver's test: each fails it. The last passed under the first bound the test had on the large
  unit's state (9,437,184 bytes, where the fault makes 9,046,976 of it); the bound is 8,912,896 now.

### What it means

- **The option is 4.6 ms cheaper on `sqlite3.c` and still 18.9 ms slower than without it.** What the producer adds is
  about 30 ms at this head (the tests 7.0, the sweeps and their keeping 7.8, the loop that reads a body and writes
  its graph 3.0, the front 3.3, the twin's controls 2.2, the kernel 2.2, the setup 2.2, what it asks before a reading
  0.9, the splice 1.1) against 10 to 11 saved. **It stays off by default and moves nothing on #2850's acceptance.**
- **What is left of the setup is small and named:** the passes over the unit's tokens, a small unit's fixed state on a
  lane of many units, and the 60 MB the swept bodies' columns still touch, which a machine that kept one body's
  records at a time would not.
- **Three tenths of what it sweeps is for nothing:** 77,372 of the 258,430 positions are of bodies the machine
  refuses after their sweeps, and a body of under 1,024 positions is swept to its end whichever root refuses it.
- **Main itself is 5 ms slower than at `e9cbc3d72`** (buster14a/buster#3296 +0.87 ms, buster14a/buster#3324 +1.79, the
  ten merges after it +2.42, paired), on the path every compile takes: that is the next piece, before more of this one.

## Step 5's allocation, and its kept rows in the compile

[#2873](https://github.com/buster14a/buster/issues/2873). After the census,
a second one counted musl's units, and
[an allocation](https://github.com/buster14a/buster/issues/2873#issuecomment-6917154449)
gave every row of both to the step or issue that removes it, or kept it as
the kernel's own. The rows it kept were then optimized, each a pull request
with its own measurement.

### The census of musl's units

[The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/kernel-census-musl-2873-2026-11-16)
(`9d50cfb`, `8fa9cbc`). The census's attribution over musl's 1,336 units
(5,039,639 tokens, 3,772 a unit) at main `dbf591b9`, two ways: a unit a
process, one after another, as a build tool runs them on one core, and as one
command on one lane, which pays a process's fixed costs once.

- **A unit a process, the kernel takes 1,022 cycles a token**, 3.86 M a unit:
  about as much as the compiles' own user time. **As one command, 212.5.**
- **By cause, a unit a process, cycles a token:** fresh memory 313; the
  teardown 191, and 33 more deferred to `ksoftirqd`; the arenas' system calls
  149; the executable's own pages after the start 123; the start 99; the
  include search 64; the rest of the exit 41; the stack 19; the object 9.
- **As one command:** fresh memory 108; the arena pool's discards between
  units (`madvise`) 70; the include search 19.5; the objects' writes 8.4.
- **A unit as its own process pays 3.05 M kernel cycles more** than inside
  the command: 2.19 M for the start, the exit and its teardown, the
  executable's own pages and the arenas' reservations and commits, 0.59 M for
  fresh memory cleared again.
- **Its gate:** 99.6% (a unit a process) and 99.3% (one command) of the
  kernel's cycles are in a named row; the faults by owner are the faults
  counted, 0.9% apart.
- **Counted besides:** a unit a process fails 37 include probes (its
  directories never reach the 32 misses at which #653's listings start); as
  one command, each unit stats its search directories again and each source
  three times, 6.6 cycles a token (#2879).

### The allocation

Cycles a token, as the censuses counted them and allocated:

| row | `sqlite3.c` | | musl as one command | | by |
| --- | ---: | ---: | ---: | ---: | --- |
| fresh memory | 96.2 | 1.9 to 2.6 | 107.5 | 0.5 | #2536's windows, #1701's lifetimes, #1167's sparse columns; on musl #1213's pages kept between units under its reset contract |
| the arenas' discards and unmaps | 4.2 | 0 | 69.6 | 0 | the same; on musl #1213 |
| the teardown | 6.8 | 0.1 | 1.2 | 0.1 | scales with the pages |
| the search directories' stats a unit | | | 6.6 | 0 | #2879 |
| the include search, the source's mapping, the object's write | 6.5 | 6.5 | 21.3 | 21.3 | kept: a unit's source and object are files |
| the executable's pages, the reservations and commits, the start, the exit, the stack | 5.6 | 5.6 | 1.3 | 1.3 | kept |
| timer ticks | 1.5 | 0.1 | 3.8 | 0.4 | scale with the compile's time |
| no named row | 0.1 | 0.1 | 1.4 | 1.4 | kept |
| **the kernel's row** | **120.8** | **14.3 to 15.0** | **212.5** | **25.0** | |

The allocation proposed 15 for the budget's line and 82 for the total. That
was not taken: the kept rows were to be optimized first.

### The kept rows, optimized

On the 9700X, CPU 2 counted as a whole (the exit's teardown and helper
included), fresh copies of each executable, rounds in ABBA order.

- **The executable's own pages**
  ([#2885](https://github.com/buster14a/buster/pull/2885),
  [the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/executable-pages-2873-2026-11-16)).
  The start order is made from exact traces of real compiles
  (`compiler start-trace`: the four small commands, musl's units,
  `sqlite3.c`), the link places the segments so that the kernel can map them
  by 2 MB pages, and the exit helper asks the kernel for huge pages of the
  ranges compiles use, once an executable
  ([the build guide](commands/build.md#the-starts-code-order)).
  - `-c sqlite3.c`: 86.18 to 83.99 M kernel cycles, 2.7% fewer in 21 rounds of
    21; 3.2 cycles a token.
  - musl a unit a process: 12.8% fewer kernel cycles and 3.6% fewer user
    cycles, 158 and 38 a token; 906,362 page faults to 752,195.
  - As one command: the same, 0.05% apart.
- **The arenas' calls**
  ([#2903](https://github.com/buster14a/buster/pull/2903),
  [the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/arena-calls-main-2873-2026-11-17)).
  One stretch of address space for the reservations, larger commits where the
  kernel charges none, one advice for every arena's huge pages, and a
  process's first small releases left for its exit: a small compile makes 49
  system calls for its arenas where it made 103.
  - `-c sqlite3.c`: 1.30% fewer kernel cycles, 17 rounds of 21.
  - musl a unit a process: 4.69% fewer, 5 rounds of 5.
  - As one command: 0.49% more kernel cycles and 0.36% fewer user cycles,
    both in every round, 4.8 M cycles fewer in all.
- **The include search**
  ([#2941](https://github.com/buster14a/buster/pull/2941),
  [the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/include-asks-2873-2026-11-17);
  [the per-file path](c-files.md), "A search directory asked for a
  subdirectory"). A directory that fails three probes under one subdirectory
  is asked for it, once, and one without it answers the rest.
  - A unit with only `<stdio.h>`: 24 failed opens where it made 52, and 0.85%
    and 1.07% fewer kernel cycles with the asks than without, one copy of one
    executable.
  - `-c sqlite3.c`: 101 failed opens where it made 207, for 11 `stat`s; 0.2
    to 0.5 cycles a token by the calls' costs, which its runs don't resolve.
  - musl's units: nothing gained and nothing lost; their directories mostly
    have what they are asked for.
- **Measured, and left as they are** (the same record, section 9):
  - The source's mapping. Mapped without `MAP_POPULATE`, its pages faulted in
    as the lexer reads them, `sqlite3.c` reads within 1% of the populated
    mapping's kernel cycles either way; read into an arena instead, 6% more.
  - The object's write: 0.4 M to 0.5 M kernel cycles of `sqlite3.c`'s 82.5 M,
    0.6 to 0.7 a token, one copy into pages the file already has.

### Where the row stands

Cycles a token on `sqlite3.c`:

| kept row | allocated | now | what took it |
| --- | ---: | ---: | --- |
| the executable's pages, the reservations and commits, the start, the exit, the stack | 5.6 | 3.1 to 3.3 | #2885, 2.0 of the executable's 2.5; #2903, 0.3 to 0.7 of the reservations' and commits' 1.5 |
| the include search, the source's mapping, the object's write | 6.5 | 6.0 to 6.3 | #2941, by the calls' costs |
| **the kernel's row** | **14.3 to 15.0** | **11.3 to 12.3** | |

- **The row is about 12 cycles a token, twice the budget's 6.** Every other
  row of it has an owner: fresh memory, the discards and the teardown are
  #2536's, #1701's and #1167's.
- **With 12 for the kernel the total is 79 cycles a token**, 56.6 M cycles
  for `-c sqlite3.c`, 0.92 GB/s. The budget's table keeps the design's lines
  until that is settled on #2873.
- **What is left of the kept rows is calls a file needs:** an open, an
  `fstat`, a read and a close a header; a probe a directory that lacks it,
  until its third under one subdirectory; the object's copy; an `mprotect` a
  commit; the start and the exit. The executable's data still takes 63 faults
  a process (#2886).
- **On musl as one command the row is 25:** a unit's two files, its source
  and its object, are about 80 K kernel cycles a unit that a large unit pays
  once.
- **A unit compiled as its own process has no allocation.** By the kernel's
  profile it took 1,034 cycles a token on musl's units, 871 after #2885 and
  817 after #2903; the asks leave it there. Small units reach the target only
  many to a process, as the one command runs them.
- **The census's per-fault attribution is high for reads:** #1167's step 1
  removed 1,093 of the reader's faults, mostly zero-page reads, for about 1 M
  kernel cycles, where the census's samples gave them about 2.8 M.

## The default path since `e9cbc3d72`: a warning's replay of what can only be 0 or 1

The redesign's pieces above are behind an option; this one is on the path every compile takes. Main compiled the
frozen `sqlite3.c` in 113.1 ms at `e9cbc3d72` and in 117.6 at `89362fd0f`, 24 merges later. Paired on the 9700X, each
merge against its parent ([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/takeover-2850-2026-11-22)):
[#3296](https://github.com/buster14a/buster/pull/3296), which folds a condition that reads a `const` object, +0.868 ms;
[#3324](https://github.com/buster14a/buster/pull/3324), the CFG builder's heuristics, +1.785; and the ten merges after it
+2.415, where [#2937](https://github.com/buster14a/buster/pull/2937), `-Wconstant-logical-operand`, is the one that adds
work to a default compile. Each is right. A build with counters says what each does on a unit that has none of what it
looks for:

| on `sqlite3.c` | calls | that answer |
| --- | ---: | ---: |
| the logical operand's replay, its right operand a comparison, a `!`, a `&&`, a `\|\|` or a condition | 2,524 | 0 |
| the same, any other right operand | 441 | 71 a literal |
| `check_program_branch_truth` | 16,964, of 16,573 conditions | 61 |
| its equality's check; its relational's | 15,347; 5,734 | 0; 0 |
| `check_program_const_integer` | 37,686, of 14,350 nodes | 0 |

Three cuts were specified, built and paired, a commit each, and one is kept.

- **R1, kept.** The warning's pass does not replay a right operand whose own operator can only give 0 or 1
  (`check_warning_logical_boolean`, [`c-checker.md`](c-checker.md)): the warning is of an integer constant that is
  neither. `BUSTER_TEST_DEFAULT_PATH_LOG=FILE` is the tests' switch for it: the operand is replayed all the same, and
  the command writes a line to the file of how many, and of them how many give the warning's event or make a type.
- **R2, reverted.** The CFG's equality and relational checks asking first whether an operand is a literal with a `&`, a
  `|` or a boolean value beside it, before they work out the operands' common type: 19,134 of the 21,081 comparisons
  answered without it, and 1.7 M instructions of 1,276 fewer.
- **R3, reverted.** `check_program_const_integer`'s no kept by node, a byte each: 23,336 of its 37,686 walks not
  walked, 3.2 M instructions fewer.

### What it measured

The 9700X, Release, CPU 2, paired; each cut against its parent on `sqlite3.c`, seven rounds, with what its
specification predicted and the line it named as refuting it:

| | predicted, ms | refuted under | paired; its least and most | lower | instructions, M |
| --- | ---: | ---: | ---: | ---: | ---: |
| R1, `96fac43e0` to `0e446262f` | -1.3 | -0.7 | -1.742; -1.869, -0.928 | 7/7 | -11.477 |
| R2, to `c24e2a52d` | -0.6 | -0.3 | **-0.122; -0.501, +0.160** | 5/7 | -1.700 |
| R3, to `e61c039fc` | -0.5 | -0.25 | **+0.415; -0.023, +1.411** | 1/7 | -3.234 |
| the three, `96fac43e0` to `e61c039fc` | -2.4 | -1.3 | -1.460; -1.819, -1.085 | 7/7 | -16.411 |

**R2 and R3 are refuted, and reverted.** R2 took 1.7 M of 1,276 M instructions away, and 0.12 ms; R3 took 3.2 M away
and ran 1.643 M more cycles, one of seven rounds lower. The three together read 0.28 ms worse than R1 alone.
The predictions came from a build with a clock's read around each call, which overstates a call made tens of thousands
of times for a few hundred ticks (R2's, R3's) beside one made 2,524 times for 3,776 (R1's): the next piece's are to
come from instructions or a profile without them.

The head, R1 alone (`0db36d75e`; the commit after it adds a line to a test's list), against main `96fac43e0`:

| | rounds | paired, ms; its least and most | lower | instructions, M |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 7 | -1.500; -17.143, -1.204 | 7/7 | -11.478 of 1,287.8 |
| `sqlite3.c -Wall -Wextra` | 5 | -1.738; -2.117, -1.025 | 5/5 | -11.478 of 1,427.4 |
| the compiler's own source | 5 | -10.050; -89.676, -7.411 | 5/5 | -50.347 of 9,152.2 |
| Lua | 5 | -0.061; -0.274, +0.004 | 3/5 | -1.270 of 744.2 |
| musl | 5 | -0.432; -0.915, +0.691 | 3/5 | -4.668 of 6,707.2 |

Main compiles the frozen `sqlite3.c` in 115.6 ms there where it took 117.4, and its own source in 616.7 where it took
628.7. Of the 5 ms this section began with, 1.5 are back; the CFG's heuristics' 1.8 and the constant conditions' 0.9
stand, and the two cuts that were to take from them did not.

### Against its gates

- **E1, exact: met.** The four corpora by main's compiler and by this one, plain and with `-Wall -Wextra`: every
  object, every diagnostic and every status the same (1,372 units; 251 of musl's say something plain, 858 with the
  warnings). The whole inventory, where no block fails that passes on main's own build; the memory and the address rows
  on the 34 blocks, no report; the driver's eight parts; main.c compiled by itself; the no-recursion rule.
- **E2, the rule against what it replaces: met.** With the switch the pass replays what it would not: 14,891 operands
  over the corpora and 243,095 over the whole inventory's 111,863 commands. None gives the warning's event and none
  makes a type.
- **E3, its price:** R1 inside what was predicted; R2 and R3 refuted (above).
- **E4, single faults:** the rule inverted, asked of the left operand's operator, and widened by a sum: each fails the
  driver's test.

One block of the inventory failed on the branch and not on main, and was the branch's: the test that every field of the
reader's state has a class did not know the counts' field. The rule's test is not a section of its own in the driver's
test: it runs in the warning's, so that the parts are dealt as on main
([#2263](https://github.com/buster14a/buster/issues/2263)).

## Related

- [#2850](https://github.com/buster14a/buster/issues/2850): the design, its
  steps' claims and results.
- [The node machine](node-machine.md) (#2524): the shadow this runs in, T's
  rules and columns, and the machine's own front.
- [The all-core path](all-core-path.md) (#2536): the windows.
- [Tier 0](tier0.md): the row's passes. Step 4's census's command is in
  [its guide](commands/tier0.md#the-entries-census-2874).
- [#2873](https://github.com/buster14a/buster/issues/2873): step 5's
  censuses, the allocation and the kept rows' pull requests.
