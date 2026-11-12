# Attested declarations: a file-scope declaration is read once per command (#2531): design

Status: design only. Nothing below is implemented, and its gains are estimates
from an emulation that its first two steps exist to confirm or refute. It is
[#672](https://github.com/buster14a/buster/issues/672)'s one-core target's
design for what [the node machine](node-machine.md)
([#2524](https://github.com/buster14a/buster/issues/2524)) doesn't cover:
everything a compile does outside the function bodies' path from the parser's
tree to the typed stream. The issue is
[#2531](https://github.com/buster14a/buster/issues/2531). The names of
existing code are main's at `ecaf57b5`, and every figure not attributed
otherwise is that revision's on the 9700X (CPU 2, under the lock), from
[the measurement record](https://github.com/buster14a/buster-performance/tree/18047f95f4d780404e240f3710e1180068fba703/docs/measurements/file-scope-design-672-2026-11-12).

**In one paragraph.** Outside the bodies' path, the largest part of a compile
is reading the file scope's declarations: 32% of the cycles of musl's 1,336
units compiled as one command, 12% of `-c sqlite3.c`'s. A command's units read
the same declarations again and again (musl's 297,004 are 5,156 distinct
texts), and a unit names almost none of what its headers declare. So before a
unit's file scope is read, three cheap passes give each declaration a hash of
its tokens and of everything its reading depends on, look the hash up in a memo
of the declarations the lane's earlier units read without a diagnostic, and
find the declarations the unit's own code reaches by name. A declaration that
the memo attests and nothing reaches isn't read. Everything else is read as
today, by the same reader, which is also the exact twin. An emulation on the
real units skips 87% of musl's file-scope declaration tokens with every object
and diagnostic the same bytes. Expected on musl's command: about 1.3 times
faster; on a command of one unit, nothing.

## The problem

### Where the rest of a compile goes

Two workloads: musl's 1,336 units as one command on one lane (`-c --lanes 1`:
1,003 ms of CPU time, 5,039,639 tokens, 61% of them file scope outside braces
and 28% in function bodies), and `-c` of the frozen `sqlite3.c` (716,136
tokens, 79% in bodies). The counters are read in the process at each lap of
the stage clock, the units of the command sharing one clock (a measurement
patch). Each lever below is a part of the work outside #2524's scope, with the
compile's cycles of both modes if that part were free:

| lever | musl's command: M cycles | share | without it | `sqlite3.c`: M cycles | share | without it |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| **reading the file scope's declarations** | 1,796 | 32.1% | **1.47 times** | 81 | 11.8% | 1.13 times |
| the kernel: pages and system calls | 1,061 | 19.0% | 1.23 | 86 | 12.6% | 1.14 |
| the preprocessor and the lexer | 892 | 16.0% | 1.19 | 53 | 7.8% | 1.08 |
| the asm matcher's index, a unit with asm | 556 | 10.0% | 1.11 | 0 | | |
| a unit's own start and end | 523 | 9.4% | 1.10 | 2 | 0.3% | 1.00 |
| the parser and the tree's columns | 321 | 5.8% | 1.06 | 40 | 5.8% | 1.06 |
| the emission, Tier 0 to the object | 219 | 3.9% | 1.04 | 56 | 8.2% | 1.09 |
| *#2524's, for reference: the bodies between the parser and Tier 0* | *1,318* | *23.6%* | *1.31* | *453* | *66.0%* | *2.94* |

The compiles are 5,587 M and 686 M cycles. The file scope's reading is the
checker's stages from `type-table` to `select` on the units compiled without
their function bodies, less the same stages of as many empty units; the
kernel's row is every kernel cycle, and overlaps every other row; a unit's
start and end is the empty units' whole compile. The first row is first on
musl by a wide margin and second on `sqlite3.c`, a point behind the kernel's.
The other rows are in "What it leaves".

### The same declarations, again and again

A file-scope token costs the checker 407 user cycles in musl's units without
bodies (1,477 M for 3,630,633 tokens in `check-input` alone), where the parser
takes 38 for the same token, and tcc parses, checks and generates code for
about 131 ([the node machine's record](node-machine.md)). The reader's
declaration steps (`check_input_declarator_step`, `check_input_declaration`,
`check_input_specifiers` and their kin) are 784 M of those units' 2,057 M
sampled user cycles, with flat profiles.

What those steps read is the same text, over and over. musl's command, by a
count of its preprocessed units' file-scope declarations, bodies cut:

| kind | declarations read | distinct texts | tokens read | times over |
| --- | ---: | ---: | ---: | ---: |
| prototypes | 233,215 | 2,079 | 2,643,136 | 98.5 |
| typedefs, with and without a record's body | 34,149 | 268 | 307,980 | 70.8 |
| record and enum definitions | 5,508 | 233 | 191,160 | 29.0 |
| objects with an initializer | 1,072 | 370 | 212,871 | 1.0 |
| all, with the definitions' headers and the rest | 297,004 | 5,156 | 3,615,194 | 13.5 |

And a unit names almost none of them: by the emulation below, the unit's own
code reaches 4% of the declarations that an earlier unit already read.

### What the stage's own work found

- **Local cuts are spent
  ([#1671](https://github.com/buster14a/buster/issues/1671)).** The
  declaration steps' profile is flat; batches of declarations, a cache of the
  specifiers and a prototype's parameters left unrecorded were each measured
  or audited and ruled out. "Further single-thread gains in the reader need
  something other than local cuts."
- **Skipping what nothing uses was measured for one unit, and set aside for
  the default mode** (#529, #155). All of `sqlite3.c`'s system-header lines
  cost the reader 3.85 ms, and the default mode checks every declaration as
  clang does: a declaration with an error must report it whether or not
  anything uses it. `-flazy-system-headers` may skip bodies, and
  [costs more than it saves](https://github.com/buster14a/buster/issues/1364),
  since its plan replays the unit through the scalar parser.
- **What a lane's units share stops at the tokens**
  ([the units' guide](translation-units.md), "What the units share"). A header
  is read, lexed, refined and given its directive table once on a lane. Its
  declarations are then read by every unit that includes it.

This design takes the second finding's objection as its rule: a declaration
isn't skipped because nothing uses it, but because an identical reading of it
already reported nothing, and nothing can observe its result.

## The design

### The rule

Before the reader's walk in source order, for the unit's file-scope
declarations (each a subtree whose root is the parser's root's child):

1. **A semantic hash for each declaration.** A hash of its own tokens, of the
   current hash of every name its reading looks up or declares (ordinary names
   and tags apart), and of the pragmas before it. A name's current hash is
   made from the hashes of the declarations that declared it so far. So two
   declarations with one hash are the same tokens, read where every name they
   depend on was declared by the same tokens in the same way, back to the
   unit's start.
2. **The memo.** The lane keeps the set of semantic hashes of the declarations
   that its earlier units read *silently* (no diagnostic, no recovery) and
   *purely* (nothing changed but the bindings of the names they declare and
   the type table: no definition, nothing with a consequence at the unit's
   end). A declaration whose hash is in the set is **attested**: reading it
   here would report nothing and define nothing.
3. **The reach.** The declarations the unit's own code reaches by name. The
   roots are every function definition, every pragma and every declaration
   that isn't attested. A reached declaration reaches (a) every earlier
   declaration of a name it spells, and (b) every later declaration of a name
   it declares (a tag's completion, a redeclaration).

**A declaration that is attested and not reached is skipped**: the walk
doesn't visit its tokens. Every other declaration is read as today. When the
unit ends without an error, the hashes of the declarations it read silently
and purely are added to the memo.

An example, with the unit's own function last:

```c
typedef unsigned long size_t;                           /* D1 */
typedef struct _IO_FILE FILE;                           /* D2 */
struct _IO_FILE { int fd; size_t count; };              /* D3 */
int puts(const char *);                                 /* D4 */
size_t fwrite(const void *, size_t, size_t, FILE *);    /* D5 */
int fclose(FILE *);                                     /* D6 */
int hello(void) { return puts("hello"); }               /* D7 */
```

D7 is a root. It spells `puts`, so it reaches D4, which spells nothing
declared. If an earlier unit read D1 to D6 from the same headers, D1, D2, D3,
D5 and D6 are attested and not reached, and only D4 and D7 are read. A unit
that calls `fclose` reaches D6, which spells `FILE` and reaches D2; D2
declares the tag `_IO_FILE`, so it reaches the tag's later declaration D3 (b);
D3 spells `size_t` and reaches D1. D5's hash is of its tokens and of the
hashes of `size_t`, `FILE` and `fwrite` (none yet) at D5: in a unit whose
`size_t` is another type, or that declared `fwrite` before, it is another
hash.

### Why the skip is exact

The claim: the object and the diagnostics of a unit that compiles without an
error are the same whether a skipped declaration is read or not. It rests on
three properties, each checked (below) rather than assumed, and on one rule
for the units that don't.

- **A reading is a function of what the hash covers.** What the reader reads
  while it reads a file-scope declaration D: D's tokens (kinds, names, literal
  spellings, and the flags it consults, such as a system header's); the
  bindings of the names D looks up and declares; and the ambient state (the
  pragmas in force, the options, which a command's units share). The
  semantic hash covers each, the bindings through their own hashes. So a
  reading with the same hash does the same thing: no diagnostic, and the same
  changes up to where they are in the unit.
- **A pure reading changes only what a name can reach.** By the memo's test,
  D's reading changed the bindings of D's declared names and the type table,
  and nothing else: no object or function to emit, no alias, no tentative
  definition, no declaration of internal linkage (whose unused and
  undefined-internal warnings come at the unit's end), nothing in a table that
  a later pass walks.
- **Nothing that is read can name a skipped declaration.** A lookup of a name
  happens at a token that spells it, in a declaration or a body that is read;
  that declaration is reached or a root, and by (a) it reaches every earlier
  declaration of the name. A later declaration that redeclares or completes
  one of D's names spells the name: if it is read, D is reached by (a); and
  if D is read, (b) reads it too, for the code that names D's other names
  (`FILE`, whose users need `struct _IO_FILE`'s members). The types D would
  have interned are made again by whoever needs them: the type table is
  addressed by content, and its ids are opaque.

**A unit that reports an error is read again.** The three properties cover
lookups by name. A query over every declared name sees which declarations
exist: clang's typo correction for an undeclared identifier, which the checker
gives as clang does (`c_check_typo.c`), suggests a name the unit may never
have named. So a unit whose reading reports an error, or runs the typo
corrector for a warning, discards that reading and is read again with nothing
skipped, as the deferred reader repeats source order after a merge it can't
make. Its diagnostics are in its own stream until then
([the units' guide](translation-units.md), "Diagnostics"), so nothing was
printed. A build that succeeds never pays it; a unit that fails pays one more
reading. A unit whose parse failed, read through a prefix or a repair view,
skips nothing in the first place.

Position is not an input: a skipped declaration's tokens stay in the unit,
every token keeps its index, and nothing is renumbered. (The emulation, which
blanks text, found the one place an index reaches an object: a string
literal's symbol, `.Lbuster_string_N`, is named by its token's index.)

What isn't claimed: that checking is lazy. A declaration is checked the first
time a lane meets it under a hash, and never again under the same hash,
because the check's inputs are the same. That is the objection #155 and #529
settled, kept: every declaration's diagnostics are the default mode's, clang's.

### The names' lists

The hash and the reach need, for each declaration and before any reading:
the names it **declares** at file scope (declarator names, tags it defines or
first mentions, enumerators), the names its reading **looks up** (a type
specifier's typedef name; every name in an array bound, a bit-field's width,
an enumerator's value, a `typeof`'s or an attribute's operand; a tag it
mentions), and the names it **spells**. Parameters' and members' names are
neither declared at file scope nor looked up.

They come from the parser's tree, which has already decided what the reader
would: a typedef name is a `SPECIFIER`, a declarator's name an `OPERAND`; a
parameter list is `OPEN_FUNCTION` and a parenthesized declarator `OPEN_GROUP`;
`OPEN_STRUCT` and `OPEN_ENUM` are bodies; `TAG` introduces a tag. One pass
over kinds and roles with the brackets' nesting gives each identifier its
class.

The chain is sound only if the declared list holds every name a reading
declares. So the list is a superset by construction (every `OPERAND` name
outside a parameter list, a body's members and an expression), and **the
reader checks it at every reading**: each `DECLARE` event of file scope that a
declaration's reading appends, and each tag it declares or completes, must be
for a listed name. A declaration that declares anything else is never
attested, and the name's hash is poisoned for the rest of the unit (set to a
value no other unit computes), so that nothing depending on it is attested or
hits. The emulation met this failure once, in its own rules: its list of type
keywords held `_Float128`, which glibc declares by typedef for a compiler
without the keyword; the typedef then declared nothing, was skipped, and one
unit failed with `unknown type name '_Float128'`. The parser has no such list
to get wrong, and the check is what catches the next one.

A function definition is a root and is never attested. For the chain, every
name its body spells counts as declared by it (a block's `extern`
declaration, an implicit function's), which is the superset without reading
the body. A pragma between declarations is a root, is hashed into the ambient
state, and counts as declaring the names it spells (`#pragma weak`); a
declaration with a pragma inside is never attested.

**Names reached without a token.** The compiler looks some names up on its
own: the library function behind a builtin the unit spells
(`__builtin_memcpy`'s `memcpy`), the functions its lowering calls without the
source naming them (the catalogue's helpers and library functions: an
aggregate's copy), and the symbols inside the strings of asm statements, asm
labels and alias attributes. Each counts as spelled by a root, so a
declaration of one is always read. The emulation has no such rule, and musl's
and the generated units' objects didn't need one; step 0 lists the by-name
lookups outside the reader's walk, each of which is in this set or a reason to
widen it.

### Where it sits

In `check_input_run`, after the columns are reserved and the per-token
pre-passes ran, before `check_input_resolve_range`:

- **The skip is two marks.** A skipped declaration's tokens get no visit mark
  (`CheckInputReader.visits`), so `check_input_resolve_next` steps over them
  as it steps over tokens no step acts at; and they are `ignored`, as a
  declaration's specifiers and declarators are once read, so that the
  expressions' walk acts at none of them. That is how a body deferred by
  `-flazy-system-headers` is passed (`deferred_tokens`), for a declaration's
  whole range.
- **The record is a comparison.** At a file-scope declaration's start the
  reader notes its counters (reports, warning events, recoveries, binding
  events, and the tables whose entries have consequences later: objects to
  emit, aliases, sections, strings, static data, pending initializers); at
  the next declaration's start it knows whether the reading was silent and
  pure, and whether its events were for listed names.
- **The memo is the lane's**, beside its intern table and file cache
  (`CheckUnitsLane`): a set of 128-bit hashes, 16 bytes a distinct
  declaration (2,826 for musl's command). Names' ids are the lane's intern
  table's, which a lane's units share, so a name's hash is an array entry.
  The per-name state is validated by a unit's stamp, so that no unit clears
  an array as long as the lane's names.
- **A command of one unit doesn't run the passes**: it has no memo to hit.
- **A count over the unit's declarations counts the skipped ones** (a unit
  with none is `-pedantic`'s empty translation unit).

Nothing else changes: a skipped declaration has no binding events, no types,
no columns, and the passes after the walk read what the walk wrote.

### The passes

| pass | what | order |
| --- | --- | --- |
| lists | each identifier's class from kinds, roles and the brackets' nesting; the declarations' ranges | 64 tokens' masks at a time, with a stack for the nesting |
| own hashes | each declaration's tokens, two an AES round, keyed | four declarations at a time |
| chain | each declaration's semantic hash from its own and its names' current hashes; the memo's probe; its declared names' new hashes | source order |
| reach | from the roots, by two cursors a name | a worklist |
| marks | the skipped ranges' visit and ignored marks | 64 tokens at a time |

The reach is linear. A name's declarers are a list in source order with two
cursors: one rises from its first declarer as declarations that spell the name
are reached (every declarer before the last of those is reached), one falls
from its last as declarations that declare it are reached. Each cursor passes
a declarer once. In a unit without a hit every declaration is a root and the
reach doesn't run.

## The twin, the oracle and the checks

- **`--scalar` never skips**, as it never runs the coded walk: the reader that
  reads every declaration is the twin, and every existing comparison of the
  default path with `--scalar` (objects byte for byte, diagnostics) is a
  comparison of the skip with its oracle.
- **A unit's object in a command is its object alone.** `test-check-units`
  already requires each unit's object of a command of several units to be the
  unit's alone at every lane count. A unit alone has no memo and skips
  nothing, so that test is the claim's own test, on every fixture it has.
- **`--verify` reads what it would skip.** The passes decide; the reader reads
  every declaration; for each one that would have been skipped it fails on a
  diagnostic, an event for an unlisted name, or a change to any counter but
  the bindings' and the types'. Step 1 is this mode run on the corpora.
- **The lists against the events**, at every reading, in every mode (above).
- **Fixtures** for each rule: a redeclaration and a tag's completion out of
  reach and in it; a typedef that another unit declares otherwise; a
  declaration after a `#pragma pack`; a prototype that differs by a system
  header's flag; an alias, a tentative definition, a `static` declaration, a
  static assertion; a declaration with an error in the second unit only. And
  the corpora as many-unit commands (musl, the libc corpus, Lua, `sqlite3.c`
  with `shell.c`, the compiler's own units), each unit's object against its
  own alone.

## Data-parallel first: what is vector and what is ordered

The vector parts, each with the scalar twin it gets: the identifiers' classes
(compares of kinds and roles, 64 tokens at a time, as
`check_input_count_tokens` reads them); the declarations' own hashes (VAES,
four chains at once); the marks (a prefix XOR of the ranges' ends, 64 tokens a
word).

The chain and the reach stay one declaration at a time, and this is why:

- **The dependency is the unit's order.** A declaration's hash needs the
  hashes of the names declared before it, most often by the declaration just
  before (`typedef` then use). Rounds over a sorted (name, position) table
  would cost a gather a name a round; on Zen 5 a `vpgatherdd` of 16 lanes is
  12.6 cycles against three first-level loads a cycle.
- **They are already small.** A prototype of the passes over the command's
  real units (`chain.c` in the record: the units' tokens as kinds and interned
  names, one memo for the command, its skips the emulation's to the
  declaration):

  | cycles a token | musl, 1,336 units | `sqlite3.c`, one unit | `sqlite3.c`, a warm memo |
  | --- | ---: | ---: | ---: |
  | lists, by the prototype's own scan of the syntax | 15.6 | 11.7 | 11.9 |
  | own hashes (one chain, not four) | 1.9 | 3.0 | 3.1 |
  | chain and the memo's probes | 2.7 | 3.2 | 3.1 |
  | reach | 1.1 | 0 | 5.8 |
  | marks and the memo's additions | 1.0 | 0.1 | 0.2 |
  | all | 22.3 | 18.1 | 24.1 |

  22 cycles a token is 2.0% of musl's command, and the two ordered passes are
  3.8 of them, against the 407 a file-scope token costs the reader. The
  prototype's scan stands for the lists' pass with more work than that pass
  would have: it works out from kinds alone what the parser's roles say.

If the chain became the limit, the lever is several declarations' own hashes
and probes in flight (more chains at once), not wider vectors.

## The evidence so far: an emulation on the units

The rule was emulated on the units' preprocessed text (`attest.py` in the
record: its own lexer and its own rules of C's declaration syntax, a memo
filled as the command goes), and each unit written twice: with the skipped
declarations' tokens blanked, and with each kept as a `;` at the token's own
place (an empty declaration a token, so that every other token keeps its
index). It assumes what the memo would record: that an eligible declaration
read silently.

| | musl, 1,336 units | 512 generated units over glibc's headers |
| --- | ---: | ---: |
| file-scope declarations | 297,006 | 298,334 |
| skipped | 263,177 (87.4% of the tokens outside function definitions) | 272,276 (88.2% of all tokens) |
| hits the unit's code reaches | 11,082 | 17,129 |
| misses: read, then in the memo | 2,826 | 1,661 |
| not eligible | 1,337 | 3,742 |
| objects, a `;` a skipped token | 1,336 the same bytes | 512 the same bytes |
| diagnostics | the same bytes (321 warnings) | the same (none) |
| `-c` of the preprocessed units, CPU ms | 810.7 | 582.1 |
| … a `;` a skipped token: more than the design leaves | 587.7 | 246.5 |
| … the skipped tokens gone: less than the design leaves | 537.8 | 161.1 |

- **The objects.** With the tokens blanked, 109 of musl's objects differ, each
  in a string literal's symbol alone; with the placeholders, none. With a
  warm memo (a second command), 265,754 declarations are skipped and the
  1,336 objects are again the same bytes, and so is `sqlite3.c`'s.
- **The two times are brackets.** The design lexes and parses a skipped
  declaration's tokens as they were and reads nothing; the placeholder units
  also read an empty declaration a token, and the blanked units also drop the
  tokens' lexing and parse. `check-input` is 1,930 M user cycles for the units
  as they are, 897 M with the placeholders and 761 M with the tokens gone.
- **What stays ineligible** in musl: `static` declarations (212,270 tokens,
  most of them the units' own tables' initializers), initializers (10,240)
  and aliases (5,166); in the glibc units, asm labels (54,675), objects
  without `extern` (38,378) and tags first declared in a parameter list
  (16,145), each a rule the emulation keeps wider than the reader's own test
  would.

## The steps and their gates

Each step is measured on the 9700X before the next begins. `--scalar` keeps
today's path throughout.

| step | what is built | gate |
| --- | --- | --- |
| 0. Census | `measure-attested`: the pipeline as it is, with the lists, the chain, a memo over the command's units and the reach computed beside it, and after each declaration's reading what it did (silent, pure, its events against its list). Counts by reason, per declaration and per token. | No reading declares an unlisted name on the corpora. The reader's own test makes at least 75% of musl's and of the glibc units' file-scope declaration tokens skippable (the emulation: 87% and more). The passes cost at most 25 cycles a token in the compile. |
| 1. Shadow | `--verify`'s mode, on by an option: the passes decide and everything is read; each would-be skipped declaration's reading is checked. No compile skips. | No failure on the corpora as many-unit commands, with `-g` and with `-Wall -Wextra`, on 1 and 8 lanes, with the bodies deferred. |
| 2. Skip | The walk passes the skipped declarations; the units' ends fill the memo. | musl's command on one lane at least 1.15 times faster (expected 1.29) and not slower on 8; every unit's object and diagnostics its own alone and `--scalar`'s; `-c sqlite3.c` and a small unit's process within the A/A floor. |
| 3. Out of the unit-wide passes | A skipped declaration's tokens leave the per-token passes that still read them and the tree's derived columns; the reach is offered to `-flazy-system-headers`' plan (#1364), which needs the same graph without a second parse. | At least 1.25 times cumulative on musl (expected 1.35). |
| 4. A memo that outlives the process | Opt-in, and the owner's decision: the set of hashes kept in a file a build names, keyed by the compiler's build and the options. | A process a unit: musl at least 1.15 times (the emulation's bound is 1.27); `-c sqlite3.c` at least 1.04 (bound 1.07). |

Step 4 is where a command of one unit gains. It is last because it is a
decision about files a compiler writes, not because it is hard: the memo is a
set of hashes, and a wrong or stale entry can only skip the checking of a
declaration nothing names.

## Expected gains

From the emulation's measured compiles, on the two many-unit workloads
(`gains.py` in the record), in times faster for `-c` of the sources as one
command on one lane:

| | musl, 1,336 units | 512 units over glibc's headers |
| --- | ---: | ---: |
| conservative | 1.20 | 1.51 |
| expected | 1.29 | 1.80 |
| optimistic | 1.35 | 2.06 |
| the bound: the file scope's reading free | 1.47 | |

- **Conservative:** the placeholder units' saving at 85% of the emulation's
  coverage (the reader's test may refuse more than the emulation's rules),
  less the prototype's passes as measured, 22.3 cycles a token.
- **Expected:** the blanked units' saving, with the skipped tokens' parse and
  lexing put back (134 M and 248 M cycles, from the stage counters), 20 cycles
  a skipped token for the unit-wide passes that still see it, and the passes
  at 12 cycles a token with their lists from the roles.
- **Optimistic:** step 3: the lexing alone put back, and the passes at the
  prototype's 6.8 cycles a token without its scan.
- **`-c sqlite3.c`, and any command of one unit: 1.00.** Nothing is skipped
  and nothing runs. With step 4's memo warm, the bound is 1.07 (46,014 of its
  tokens skipped; 115.3 to 107.4 ms with those tokens gone).
- **Lanes.** Each lane has a memo, so a lane's first units read what another
  lane's did; at 167 units a lane the hits barely change. What a skipped
  declaration doesn't write is also memory the eight-worker compile doesn't
  move, which is #672's other budget; it isn't measured here.

## What would refute it

- **The census (step 0)** finds that the reader's own test of a reading makes
  under 60% of musl's file-scope declaration tokens skippable: the emulation's
  assumption (an eligible declaration reads silently and purely) was then
  wrong at scale, and step 2 falls under 1.15.
- **The census** finds readings that declare unlisted names which no rule over
  the tree can list: the chain then has no sound list, and nothing is skipped.
- **The shadow (step 1)** finds a would-be skipped declaration whose reading
  matters by a path no test names (an object or a diagnostic that differs
  with every check passing).
- **Step 2's compile** gains less than the placeholder bracket says: the
  reader's cost was then not in reading declarations.

Each ends the work with a record.

## Risks and open questions

- **An input of a reading that the hash doesn't cover.** The worst failure is
  silent: a declaration skipped that would have reported something here. The
  candidates are state the reader consults that is neither token, name nor
  pragma: a diagnostic given once a unit, an error limit already reached, a
  table indexed by something other than a name. Step 0 lists every field of
  `CheckInputReader` and `CheckInput` by class (the declaration's own tokens,
  a name's entry, the type table, a table with consequences, ambient), and a
  test fails when a new field has no class. An entry is added only at the end
  of a unit without errors.
- **Purity is the reader's word.** A new kind of declaration with a
  consequence at the unit's end, and no counter the record compares, would be
  attested. The guards are `test-check-units` on every fixture and the rule
  that a declaration's site names the tables it writes.
- **A consumer that no name leads to.** A pass that enumerates declarations,
  or looks a name up that no token spells, sees a skipped one's absence. The
  known ones have rules above (the typo corrector, the compiler's own
  library names, strings that hold symbols); a new one needs its own, and the
  corpus comparison is what finds it.
- **`-g`.** A unit's DWARF must not describe a skipped declaration, as clang's
  describes no unused one; if `check_debug_type_reference`'s rule reaches
  them some other way, `-g` units don't skip until it doesn't. Step 1 runs
  with `-g`.
- **Hash collisions.** 128 bits, keyed; a collision could only skip the
  checking of a declaration nothing names.
- **Order of the lane's units.** A unit's output can't depend on the memo
  (that is the claim), so the costliest-first order and the lanes' takes
  change only who reads a declaration first.
- **Reach by spelling is wider than by binding.** A body that names a local
  `count` reaches a file-scope `count`. It costs hits, not correctness; the
  emulation's 4% reached is with it.
- **Open:** whether the lists' pass is the parser's own (it has the nesting in
  registers) or the reader's; whether a reached, attested declaration's
  reading can be replayed from a record (3% of musl's tokens, and a second
  representation of the reader's writes: not planned); whether the memo is
  shared read-only across lanes after a first round.

## What it leaves

After step 3 the expected command is 4,100 to 4,300 M cycles on musl, and the
ranking's other rows are what is left, none of them designed here:

- **The kernel's 19%.** musl's command makes 28,599 `madvise` calls and takes
  343,028 page faults, 21 and 257 a unit: a unit's arenas given back and
  faulted in again by the next, a megabyte each time. Page faults are 47% of
  the kernel's cycles and `madvise` 35%; a quarter of them fall between two
  units. An empty unit in a command costs 72 µs, 149,000 user and 246,000
  kernel cycles. [#1213](https://github.com/buster14a/buster/issues/1213) has
  the lanes' side of it; on one lane a lane that keeps its small units' pages
  is the lever.
- **The preprocessor's 16%.** 705 M user cycles for the command. 116 M of
  them are what 1,336 empty units' preprocessing costs (the predefined macros
  and the stage's own setup, a unit); the rest is each inclusion's directives
  and expansions over the cached tokens.
- **The asm matcher's index, 10%**:
  [#2532](https://github.com/buster14a/buster/issues/2532). Made once a
  process, musl's command takes 929 ms where it takes 1,029.
- **The declarations still read**, about 480 M cycles: the reached, the
  unattested and the ineligible, the units' own tables first.
- **A unit's own setup in the reader.** Each unit clears seven arrays as long
  as the lane's intern table, which grows with the command
  (`check_input_run`'s `parameter_seen`, `member_seen`, the linked and
  library declarations, the builtins' names and the binding table's heads: 34
  bytes a name a unit); `memset` is 112 M of the command's user cycles. A
  stamp a unit, as the passes' own per-name state has, would leave them
  alone.
- **The parser (6%) and the emission (4%; 8% of `sqlite3.c`)**, which no
  measurement here puts above the others.

## With the node machine (#2524)

- **The interface is unchanged.** A function body is a root of the reach, so
  every declaration a body can name is read, in source order, and is in the
  file-scope environment that `c_environment_lookup` answers from at the
  body's opening. The deferred reader's order (`check_deferred_cut`: the top
  level first, with every body cut out, then the bodies) is the order the
  skip already assumes: the top level is what gets shorter. #2524's step 4
  needs nothing from this design, and this design needs nothing from the
  machine; either can land first.
- **What each leaves the other.** The machine takes the bodies' tokens out of
  the reader's columns; this takes most of the headers' declarations out of
  the reader's walk. A header-heavy unit of a command then reads its own code
  and what that code names.
- **Together.** On `-c sqlite3.c`: #2524's expected 2.0 times, unchanged (this
  design does nothing for one unit before step 4, whose bound there is 1.07).
  On musl's command: this design's expected 1.29 alone. #2524 puts its own
  step 4 at about 1.1 there, which together is about 1.45; these counters put
  the bodies' part between the parser and Tier 0 at 23.6% of the command, so
  with the coverage #2524 expects on `sqlite3.c` (three quarters of that part)
  the two together are about 1.7, and about 2.0 with #2532's index made once.

What this design is not:

- **Not lazy checking.** Nothing is skipped because nothing uses it; it is
  skipped because the same check already passed, and nothing can observe the
  result.
- **Not a precompiled header, and not modules.** There is no prefix, no order
  of inclusion, no file to build first and no result to load: the memo holds
  no declaration's effect, only that its reading had none to report.
- **Not a second reader.** The declarations that are read are read by
  `check_input_declaration` as today; the passes read the parser's kinds,
  roles and names, and type and check nothing.

## Related issues

- [#672](https://github.com/buster14a/buster/issues/672): the tracker.
  [#2524](https://github.com/buster14a/buster/issues/2524): the bodies' path.
- [#1671](https://github.com/buster14a/buster/issues/1671): the reader's
  remaining time, whose file-scope part on a command this takes.
- [#1364](https://github.com/buster14a/buster/issues/1364), #155, #529: lazy
  checking and the default mode's contract; step 3's reach is the plan #1364
  asks for.
- [#1213](https://github.com/buster14a/buster/issues/1213),
  [#1444](https://github.com/buster14a/buster/issues/1444),
  [#1701](https://github.com/buster14a/buster/issues/1701): the units' kernel
  time, a small unit's start, and memory, which "What it leaves" ranks.
- [#2532](https://github.com/buster14a/buster/issues/2532): the asm matcher's
  index.
- [#152](https://github.com/buster14a/buster/issues/152), #776: many units in
  one process and what a lane's units share.
