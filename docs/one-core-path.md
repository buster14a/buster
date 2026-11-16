# The one-core path: 73 cycles a token, on records the vector lanes can use (#2850): design

Status: design, with its first two steps, step 2's first piece and step 5's
census measured. Nothing here is in a compile.
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
made its fact bytes for 12. Step 5's census counted the kernel's 121 cycles a
token by cause: 82% page faults, 58% the clearing of fresh 2 MB pages alone.
What a compile would still take from the kernel with every arena reused or
windowed is 5.8 to 6.4 cycles a token at warm-cache costs and 10.3 to 12.5 at
the costs the compile pays, and beside it 5.2 to 6.7 more that no arena
removes (the include search, the executable's own pages, the arenas' calls):
the budget's 6 for the kernel doesn't hold.

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
| 2 | The machine on these records where it runs (#2524's steps 2 to 4) | the bodies' middle at 38 cycles a node or less in `-c sqlite3.c` | first piece measured, in the shadow: T whole from the fact words, exact; 38 cycles an evaluated node with whole records and 32 with lean ones, where its own gate was 25 |
| 3 | The front's row, into the window | 35 cycles a token or less | not started |
| 4 | Tier 0's row | 10 cycles a token or less, the object's bytes unchanged | not started |
| 5 | Fresh memory and the fixed costs | 6 cycles a token or less | census measured: the kernel 121 cycles a token; its floor 5.8 to 6.4 at item 3's costs, 10.3 to 12.5 at the compile's, and 5.2 to 6.7 more beside it: the 6 refuted |

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
  fact bytes step 0's lanes made.

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
- **By stage** (recorded): check-input 35 M cycles of faults, parse 17.5 M, preprocess
  7.8 M and 3.8 M of system calls (the source's read and the include
  search), check-expressions 5.6 M, check-program 3.4 M; the backend's stages
  about 1 M each or less.
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

## Related

- [#2850](https://github.com/buster14a/buster/issues/2850): the design, its
  steps' claims and results.
- [The node machine](node-machine.md) (#2524): the shadow this runs in, T's
  rules and columns, and the machine's own front.
- [The all-core path](all-core-path.md) (#2536): the windows.
