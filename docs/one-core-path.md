# The one-core path: 73 cycles a token, on records the vector lanes can use (#2850): design

Status: design, with its first two steps, step 2's first three pieces and
its tests' loop on the window, the first part of step 3 and steps 4's and
5's censuses measured. Of all this, only what step 5's allocation kept as
the kernel's own is in a compile: three changes to those rows, merged.
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
Step 0 ran the lanes on the real nodes with the leaves and lookups given: 16.7
and 20.6 cycles a taken node on `sqlite3.c` and musl, at its gate of 20. Step 1
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
and 38 are the tests that read T's records still.
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
| 0 | The middle's passes on the real nodes, outside the compiler | every entry the stream's; 20 cycles a taken node or less; 0.2 mispredicted branches a node or fewer | measured: 16.7 and 20.6 cycles, 0.025 and 0.045 mispredicted |
| 1 | The fact words, in the shadow: T's leaves and lookups from one load each | every record the oracle's; 10 cycles a leaf or lookup or less, a body's expressions a run | measured: exact; 15.8 to 26.5 cycles; a leaf about 5, a lookup 25 to 46. Followed up: a lean record 14.1 to 20.8, the loads alone 8.8 to 15.2 |
| 2 | The machine on these records where it runs (#2524's steps 2 to 4) | the bodies' middle at 38 cycles a node or less in `-c sqlite3.c` | first piece measured, in the shadow: T whole from the fact words, exact; 38 cycles an evaluated node with whole records and 32 with lean ones, where its own gate was 25. Second piece counted: of the operators that are no lookup, 5.4%, 24.2% and 6.1% need a value made at the record (`sqlite3.c`, Lua, musl; its line was 15%), and C decides on a type's identity for one question of a call's callee. Third piece measured: the kernel at reader time, its inputs from the front's records, exact; 16.4 TSC ticks a record where T again takes 49.2, and 44.8 with its inputs' making, where its gate was 35. The tests on the window measured: exact; 54.1 TSC ticks a record on `sqlite3.c` where their pass over T's columns reads 83.0 (gate 40, refuted at 65): the loop 0.7, its inputs 8.9, and 37.2 the tests that read T's records still |
| 3 | The front's row, into the window | 35 cycles a token or less | first part measured, in the shadow: the node machine's front writes the kernel's window and the kernel reads it in place, exact; the kernel with its inputs 29.9 TSC ticks a record on `sqlite3.c` (gate 35), 38.4 on Lua, 61.5 on musl. The lexer's and the parser's rows are not started |
| 4 | Tier 0's row | 10 cycles a token or less, the object's bytes unchanged | census measured: the row 64.7 cycles a token (117 a typed entry), Tier 0 38.4 of them; a key of 8 bytes or fewer decides 99.83% of the entries; the encoder, branch sizing and the object 8.3 in a compile, 6.3 at their own rates |
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
  a record at its best where six gathers a step of sixteen read 9.4.

### What it means

- **The loop was not what its 27 ticks bought.** On byte columns it is 0.7.
  The pass's cost was reading each record's inputs where they were.
- **The inputs are now the loop's cost:** 8.9 ticks a record to read four
  fields out of each 64-byte record, a line's byte and a role. The kernel
  holds the word, the standing and the flags in registers when it writes a
  record. Having it write these bytes then is not tried.
- **Two thirds of what is left is the next piece's.** With the operators'
  tests on the kernel's records, T's second run in the shadow goes too, and
  the gate is read again.

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
