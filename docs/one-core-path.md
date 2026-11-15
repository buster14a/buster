# The one-core path: 73 cycles a token, on records the vector lanes can use (#2850): design

Status: design, with its first two steps measured. Nothing here is in a compile.
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
is the lookup's facts, which the design gives the lanes.

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
| 2 | The machine on these records where it runs (#2524's steps 2 to 4) | the bodies' middle at 38 cycles a node or less in `-c sqlite3.c` | not started |
| 3 | The front's row, into the window | 35 cycles a token or less | not started |
| 4 | Tier 0's row | 10 cycles a token or less, the object's bytes unchanged | not started |
| 5 | Fresh memory and the fixed costs | 6 cycles a token or less | not started |

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

## Related

- [#2850](https://github.com/buster14a/buster/issues/2850): the design, its
  steps' claims and results.
- [The node machine](node-machine.md) (#2524): the shadow this runs in, T's
  rules and columns, and the machine's own front.
- [The all-core path](all-core-path.md) (#2536): the windows.
