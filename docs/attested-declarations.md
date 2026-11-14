# Attested declarations: a file-scope declaration is read once per command (#2531): design

Status: step 0, the census, is built and measured
([below](#step-0-the-census)), and so are steps 1 and 2, the shadow and the
skip in the compile ([below](#steps-1-and-2-the-skip-in-the-compile)): a
command of several source files skips by default (`--attested=on`). Step 3 is
built in three parts, and its gate is held by its third, the view
([below](#step-3-its-third-part-the-view): 1.266 and 1.737 times against
`--attested=off`; a skipped token's cost to the reader at the gate on musl's
command, where the measurements disagree, and far under it on the glibc
units'). Step 4, the owner's choice, is built as an option
(`--attested-memo`) and its gates aren't held
([below](#step-4-a-memo-kept-across-processes)). It is
[#672](https://github.com/buster14a/buster/issues/672)'s one-core target's
design for what [the node machine](node-machine.md)
([#2524](https://github.com/buster14a/buster/issues/2524)) doesn't cover:
everything a compile does outside the function bodies' path from the parser's
tree to the typed stream. The issue is
[#2531](https://github.com/buster14a/buster/issues/2531). The names of
existing code are main's at `ecaf57b5`, and every figure not attributed
otherwise is that revision's on the 9700X (CPU 2, under the lock), from
[the measurement record](https://github.com/buster14a/buster-performance/tree/c679fea7246dbea68cb312d132a60af79cf9deb2/docs/measurements/file-scope-design-672-2026-11-12).

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
real units skips 87% of musl's file-scope declaration tokens; with those skips
made in the compiler's own reader, every object and diagnostic is the same
bytes and musl's command is 1.25 times faster. Expected with the passes' cost:
1.23 times, and 1.30 once the unit-wide passes skip those tokens too; on a
command of one unit, nothing.

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
| a unit's own start and end | 523 | 9.4% | 1.10 | 2 | 0.3% | 1.00 |
| the asm matcher's index, a unit with asm | 443 | 7.9% | 1.09 | 0 | | |
| the parser and the tree's columns | 321 | 5.8% | 1.06 | 40 | 5.8% | 1.06 |
| the emission, Tier 0 to the object | 219 | 3.9% | 1.04 | 56 | 8.2% | 1.09 |
| *#2524's, for reference: the bodies between the parser and Tier 0* | *1,431* | *25.6%* | *1.34* | *453* | *66.0%* | *2.94* |

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
about 131 ([the node machine's design](node-machine.md), "Two references").
The reader's declaration steps (`check_input_declarator_step`,
`check_input_declaration`, `check_input_specifiers` and their kin) are 784 M
of those units' 2,057 M sampled user cycles, with flat profiles.

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
   that its earlier units read *silently* (no diagnostic of the unit, from any
   stage, lies in their tokens, and no recovery) and *purely* (nothing changed
   but the bindings of the names they declare and the type table: no
   definition, nothing with a consequence at the unit's end). A declaration
   whose hash is in the set is **attested**: reading it here would report
   nothing and define nothing.
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
widen it. Step 2 drops the lowering's runtime functions from the set (the
census still counts what they reach): a runtime call takes a declaration's
symbol only for an object the selection numbered
(`check_lower_declared_runtime`, `check_program_needed_number`), and the
selection numbers only the objects its selected code references, a reading's
that spells the name or its builtin, which reaches the declaration. A runtime
call of a name whose declaration nothing read takes the lowering's own symbol
whether the declaration was skipped or not.

### Where it sits

In `check_input_run`, after the columns are reserved and the per-token
pre-passes ran, before `check_input_resolve_range`:

- **The skip is two marks.** A skipped declaration's tokens get no visit mark
  (`CheckInputReader.visits`), so `check_input_resolve_next` steps over them
  as it steps over tokens no step acts at; and they are `ignored`, as a
  declaration's specifiers and declarators are once read, so that the
  expressions' walk acts at none of them. That is how a body deferred by
  `-flazy-system-headers` is passed (`deferred_tokens`), for a declaration's
  whole range. The measurement patch does exactly this from a list made
  outside ("The skip in the compiler's own reader" below), and every later
  pass took what it left.
- **The record is a comparison.** At a file-scope declaration's start the
  reader notes its counters (reports, warning events, recoveries, binding
  events, and the tables whose entries have consequences later: objects to
  emit, aliases, sections, strings, static data, pending initializers); at
  the next declaration's start it knows whether the reading was pure and
  whether its events were for listed names. Silence is settled at the unit's
  end, when every stage has reported: a diagnostic whose place is in a
  declaration's tokens, a warning of the program's passes among them, keeps
  it out of the memo.
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
- **`--verify` reads what it would skip** (`--attested=verify`). The passes decide; the reader reads
  every declaration; for each one that would have been skipped it fails on a
  diagnostic, an event for an unlisted name, or a change to any counter but
  the bindings' and the types'. Step 1 is this mode run on the corpora.
- **The lists against the events**, at every reading, in every mode (above).
- **Fixtures** for each rule: a redeclaration and a tag's completion out of
  reach and in it; a typedef that another unit declares otherwise; a
  declaration after a `#pragma pack`; a prototype that differs by a system
  header's flag; an alias, a tentative definition, a `static` declaration, a
  static assertion; a declaration with an error in the second unit only. For
  the reach's two edges from a root: an inline function whose later
  declaration without `inline` makes its definition external, and a later
  declaration that gives a called function an attribute (`noreturn`). For
  the names no token spells: a library function's prototype in a unit that
  spells only its builtin (`__builtin_memcpy`), and one with an asm label.
  For the lists: an enumerator a body names, and a tag first mentioned in a
  prototype's return type. For the memo's silence: a declaration that warns
  (`int f();` under `-Wstrict-prototypes`), which every unit must report
  again. And the corpora as many-unit commands (musl, the libc corpus, Lua,
  `sqlite3.c` with `shell.c`, the compiler's own units), each unit's object
  against its own alone.

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

## The evidence so far

### An emulation on the units

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
| … a `;` a skipped token | 587.7 | 246.5 |
| … the skipped tokens gone: less than the design leaves | 537.8 | 161.1 |

- **The objects.** With the tokens blanked, 109 of musl's objects differ, each
  in a string literal's symbol alone; with the placeholders, none. With a
  warm memo (a second command), 265,754 declarations are skipped and the
  1,336 objects are again the same bytes, and so is `sqlite3.c`'s.
- **The two times.** The design lexes and parses a skipped declaration's
  tokens as they were and reads nothing; the placeholder units read an empty
  declaration a token instead, and the blanked units drop the tokens' lexing
  and parse too. `check-input` is 1,930 M user cycles for the units as they
  are, 897 M with the placeholders and 761 M with the tokens gone. The
  placeholders were meant as the design's upper bracket and aren't one: the
  reader's own skip, next, takes 613.0 ms on musl's preprocessed units and
  273.4 on the glibc units', since a token the reader passes still costs the
  unit-wide passes more than an empty declaration costs to read. The blanked
  units remain the floor of steps 2 and 3.
- **What stays ineligible** in musl: `static` declarations (212,270 tokens,
  most of them the units' own tables' initializers), initializers (10,240)
  and aliases (5,166); in the glibc units, asm labels (54,675), objects
  without `extern` (38,378) and tags first declared in a parameter list
  (16,145), each a rule the emulation keeps wider than the reader's own test
  would.

### The skip in the compiler's own reader

The emulation replaces tokens. A measurement patch then made the skip as
"Where it sits" describes it: a side file lists each unit's skipped
declarations (the emulation's), and the reader clears their tokens' visit
marks and sets them ignored before its walk. The tokens stay, lexed and parsed
as they were. The decisions are made outside, and no pass of the design runs.
The patch's own count of what it passed is the emulation's to the token.

| | musl, 1,336 units | 512 units over glibc's headers | `sqlite3.c` preprocessed, a warm memo's skips |
| --- | ---: | ---: | ---: |
| declarations the reader passed | 263,177 | 272,276 | 2,579 |
| their tokens | 2,954,264 | 4,737,766 | 46,014 |
| objects, read against skipped | 1,336 the same bytes | 512 the same bytes | the same bytes |
| diagnostics | the same bytes (321 warnings) | the same (none) | the same (none) |
| `-c`, every declaration read: M cycles (CPU ms) | 5,547 (1,003.3) | 4,297 (775.7) | 641 (115.8) |
| … with the skip | 4,448 (803.7) | 2,594 (468.4) | 607 (109.6) |
| times faster | **1.247** | **1.657** | 1.056 |

- **What moved.** `check-input` goes from 2,035 M to 1,059 M user cycles on
  musl and from 2,138 M to 616 M on the glibc units; by function, the reader's
  declaration steps from 953 M to 245 M on musl, the type table from 99 M to
  23 M. The preprocessed units alone are 1.33 and 2.12 times faster.
- **What it adds to the emulation.** Every later pass ran on what the skip
  leaves (a skipped declaration's columns empty, its names unbound), and no
  object and no diagnostic of 1,849 units changed.
- **What it doesn't have.** The memo's own test of a reading, the lists from
  the parser's roles and the passes' cost in the compile; a unit with an
  error; `-g`; more than one lane. Steps 0 and 1 are for those.
- **The rounds compared are of one page state.** A run of musl's command in
  three is given no huge pages, by its randomized layout, and is 2.3% slower
  whichever way it compiles (the record, section 13). Every gate below is
  measured the same way, or with the randomization off.

### What a skipped token still costs

The preprocessed units with the reader's skip, less the same units with those
tokens gone from the text, by stage, in user cycles a skipped token:

| stage | musl | the glibc units |
| --- | ---: | ---: |
| its lexing | 9 | 11 |
| its parse | 30 | 30 |
| **the reader (`check-input`)** | **68** | **60** |
| the checker's other stages | 12 | 8 |
| the lowering and the object | 4 | 2 |
| all | 123 | 110 |

The design keeps the first two rows. The reader's row is its unit-wide passes,
which read every token of a unit whether the walk does or not:
`check_input_intrinsic_initialize` (12.5 cycles for each token of a unit: it
looks every operand's name up among the intrinsics', each unit again),
`check_input_run`'s own loops and fills (11), the warnings' graph, the token
counts, the controls and the system tokens' map (8 together), and the
columns' clearing (libc's `memset` is 108 M cycles of the skipped command).
Step 3 is this row and the two under it.

## The steps and their gates

Each step is measured on the 9700X before the next begins, on rounds of one
page state. `--scalar` keeps today's path throughout.

| step | what is built | gate |
| --- | --- | --- |
| 0. Census | `measure-attested`: the pipeline as it is, with the lists, the chain, a memo over the command's units and the reach computed beside it, and after each declaration's reading what it did (silent, pure, its events against its list). Counts by reason, per declaration and per token. | No reading declares an unlisted name on the corpora. The reader's own test makes at least 75% of musl's and of the glibc units' file-scope declaration tokens skippable (the emulation: 87% and more). The passes cost at most 25 cycles a token in the compile. |
| 1. Shadow | `--verify`'s mode, on by an option: the passes decide and everything is read; each would-be skipped declaration's reading is checked. No compile skips. | No failure on the corpora as many-unit commands, with `-g` and with `-Wall -Wextra`, on 1 and 8 lanes, with the bodies deferred. |
| 2. Skip | The walk passes the skipped declarations; the units' ends fill the memo. | musl's command on one lane at least 1.15 times faster (expected 1.23; the reader's skip alone measures 1.247) and the glibc units at least 1.4 (expected 1.62), neither slower on 8 lanes; every unit's object and diagnostics its own alone and `--scalar`'s; `-c sqlite3.c` and a small unit's process within the A/A floor. |
| 3. Out of the unit-wide passes | The design's passes run first in `check_input_run`, before `check_input_count_tokens`; the reader's unit-wide passes and its columns' fills then take the unit's read ranges (the complement of the marks) where they take every token, and the program's passes after them. Under `--verify` every column's skipped ranges hold a poison value, and a pass that reads one fails. The reach is offered to `-flazy-system-headers`' plan (#1364), which needs the same graph without a second parse. | A skipped token costs `check-input` at most 20 user cycles (68 after the skip alone, by the experiment); cumulative at least 1.26 on musl (expected 1.30) and 1.7 on the glibc units (expected 1.83). |
| 4. A memo that outlives the process | Opt-in, and the owner's decision: the set of hashes kept in a file a build names, keyed by the compiler's build and the options, its hashes over spellings where a lane's are over its intern table's ids. | A process a unit: musl's preprocessed units at least 1.08 times (expected 1.12; the emulation's bound with the tokens gone is 1.27); `-c sqlite3.c` at least 1.02 (the reader's skip alone measures 1.056). |

Step 4 is where a command of one unit gains. It is last because it is a
decision about files a compiler writes, not because it is hard: the memo is a
set of hashes, and a wrong or stale entry can only skip the checking of a
declaration nothing names.

## Step 0: the census

`measure-attested` (`c_check_attested.c`,
[the guide](commands/translation-units.md#attested-declarations-census-2531))
is step 0: a command's units compiled as they are, with the passes and a memo
over the units computed beside each, and after each declaration's reading what
it did, by the reader's own binding events and declaration bits. Nothing is
skipped. On the 9700X at `ee5b39b9`, one lane, rounds of one page state
([the record](https://github.com/buster14a/buster-performance/tree/1ff99765e33a2f287af30aca2862e71d4ce8d0a3/docs/measurements/attested-census-2531-2026-11-12)):

| | musl, 1,336 units | 512 units over glibc's headers |
| --- | ---: | ---: |
| file-scope declaring events outside the declaration's list (gate: none) | 0 of 303,705 | 0 of 386,867 |
| tokens outside definitions skipped: attested, not reached (gate: 75%) | 79.31% | 87.00% |
| … reached by a spelled name; by the lowering's runtime names alone | 6.69%; 6.41% | 7.52%; 4.35% |
| … learned (the memo's first readings); impure | 1.34%; 6.25% | 1.13%; none |
| the passes in the compile, cycles a token (gate: 25) | 35.35 | 32.48 |
| … the same passes again on the unit, the least of three | 21.54 | 21.49 |
| memo hits whose reading was not silent and pure | 0 of 274,654 | 0 of 292,253 |
| objects and diagnostics against the plain compile's | the same bytes | the same bytes |

**The gate's first two parts hold and its third doesn't.** No reading on
either corpus declares, completes or uses a name outside its list, and the
reader's own test leaves 79% and 87% of the tokens outside definitions
skippable. The passes cost 35 and 32 cycles a token where the compile runs
them, before each unit's reader; run again at once on the same unit they cost
21.5. The difference is the passes' first touch after the reader's previous
unit: five times the branch misses (0.44 a token against 0.08, 4 to 5 of musl's
13.8 extra cycles), the same count of L1D misses, and the rest uncounted
(where those misses are served from, the instruction side).

**What it means for step 2.** At the reader's skip's measured saving a token
(372 cycles on musl, 359 on the glibc units, "The skip in the compiler's own
reader"), the census's skipped tokens with its passes as measured project
1.16 times on musl and 1.46 on the glibc units, against step 2's gates of 1.15
and 1.4 and the 1.23 and 1.62 expected; on main since #2532's baked index,
1.17 on musl. Step 2 starts with less margin than designed, for two reasons
the census names:

- **The lowering's runtime names.** It looks up `memcpy`, `memset`, the libgcc
  helpers and the elementwise builtins' libm by name in every unit with a body
  (`check_lower_runtime_names`), and a runtime call's symbol comes from the
  name's first declaration, so the census reaches those declarations in every
  such unit. That rule is 216,403 of the 274,674 tokens the emulation skipped
  on musl and the census doesn't, and 224,854 of the glibc units' 239,753. Two
  narrower rules are step 1's to test: reach them only where the unit's
  lowering makes a runtime call, or only where a declaration changes the
  call's symbol (an asm label, a visibility). Step 2 found that no rule is
  needed ("Steps 1 and 2" below).
- **The passes in the compile.** The design expected 12 cycles a token from
  the roles. The lists are the costliest (11.0 on musl in the compile, 13.1 on
  the glibc units): they follow the parser's groups to classify each
  identifier, which the parser knows as it builds them (the open question in
  "Risks" below). The literals' identities are next: musl's units spell
  309,003 literals a run, 6.1% of the tokens, about 90 cycles each read back
  through the preprocessor's ranges, where the lexer has each one's bytes in
  hand. The chain and the reach cost twice as much in the compile as when
  repeated (7.0 and 5.5 cycles a token on musl against 3.0 and 2.5), with
  ten and five times the branch misses.

**What the census found about a reading.**

- **A body declares only its callees.** A function definition's file-scope
  `DECLARE` events are its implicit declarations and its block-scope
  `extern`s'. The lists give every name in a body the class "looks up", and
  "declares" to a callee's name (an identifier the parser marks as called)
  and, in a body that spells `extern`, to every name; no event fell outside.
- **The events need the reader's binding stream**, which `-c` doesn't keep
  past the reader; the census published the reader's environment for them,
  and every object and diagnostic stayed the same bytes. Since steps 1 and 2
  the reader tests them as it ends (`check_attested_events`), for the census
  too.
- **Silence and purity, as the reader sees them.** Each of musl's 321 warnings
  falls in a function definition, placed by its output position; no
  declaration's reading reported anything. The impure in musl are 374
  definitions by an initializer (205,089 tokens, most of them the units' own
  tables), 287 aliases, 90 tentative definitions and 20 internal
  declarations; in the glibc units, none.
- **Three lookups by a name no token spells**, each counted as spelled by a
  root: the runtime names above, a `__builtin_` name's library function, and
  the symbols in asm labels' and alias attributes' strings.

**What it doesn't do.** "Risks" below gives step 0 a test listing every field of
the reader's state by class; the census doesn't have it. Its evidence on that
risk is the zero contradictions over 566,907 hits, and step 1's shadow checks
each would-be skipped reading. The census runs one lane, without `-g` and
without deferred bodies (it refuses those options); step 1 runs with them.

## Steps 1 and 2: the skip in the compile

`--attested` ([the guide](commands/translation-units.md#attested-declarations---attested-2531),
`c_check_attested.c`) is steps 1 and 2: the census's passes run in the compile,
before each unit's reader, on every units lane of a command of several source
files, each lane with its own memo. `--attested=on`, the default, gives the
reader the marks of the declarations attested and not reached, whose tokens
its walk passes (`check_input_skip`, as the measurement patch did);
`--attested=verify` reads them and fails a unit where a hit's reading isn't
silent, pure and listed; `--attested=off` runs nothing. On the 9700X at the
branch's code (buster#2620), CPU 2, `setarch -R`, against main's `df85213e`
([the record](https://github.com/buster14a/buster-performance/tree/6f55383c38ca4657dd4ef21b27a5eea973dee8ba/docs/measurements/attested-skip-2531-2026-11-13)):

| `-c` of the command, one lane | musl, 1,336 units | 512 units over glibc's headers |
| --- | ---: | ---: |
| file-scope declarations skipped; tokens outside definitions skipped | 259,860 of 297,004; 85.72% | 272,239 of 298,334; 91.35% |
| objects and diagnostics against `--attested=off` and `--scalar` | the same bytes (321 warnings) | the same bytes |
| `--attested=verify`: contradictions; unlisted events; units read again | 0; 0; 0 | 0; 0; 0 |
| main, ms (M cycles of both modes), the median of 5 rounds | 1,003.9 (5,560.7) | 824.0 (4,567.3) |
| `--attested=on` | 858.4 (4,754.2) | 558.6 (3,095.4) |
| **times faster** (step 2's gate) | **1.170** (1.15) | **1.475** (1.4) |
| user instructions, main and `on` | 11,522.7 M; 9,061.9 M | 11,181.0 M; 6,748.4 M |

- **Step 1's shadow** found no failure: `--attested=verify` on musl's command
  by one lane and by 8, with `-g` and with `-Wall -Wextra` (1,867 warnings,
  85.11% would be skipped), and on the glibc units' by one lane and with `-g`:
  every object and diagnostic `--attested=off`'s bytes, no hit contradicted
  by its reading here and no event outside a list. A command of several source
  files reads its units' bodies in source order (`--deferred-bodies` is one
  source file's), so the gate's deferred bodies have nothing to run.
- **Step 2's gate holds**, at the design's conservative estimates (1.17 and
  1.45), under its expected 1.23 and 1.62. The difference is the passes: 5.0%
  of musl's user cycles with the skip, about 36 cycles a file-scope token as
  the census measured them (lists 1.5%, chain 1.0%, own hashes 0.7%, reach
  0.65%, literals 0.56%, the units' ends' records 0.33%, the system levels
  0.28%), where the design expected 12 from the parser's roles. `-c sqlite3.c`
  takes 123.2 ms where main takes 124.2, and a small unit's process 1.3 ms as
  main's, with main's instruction counts: a command of one unit runs nothing.
  `--attested=off` has main's instruction counts too, and its 1.4% more
  cycles on one lane are the executable's layout.
- **Eight lanes:** each lane's first units learn, so musl's command skips
  80.34% of the tokens outside definitions (17,264 hashes in the eight memos)
  and the glibc units' 85.51%, every object the same bytes. By wall time,
  musl's command takes 199.2 ms where main takes 216.8 (1.088 times) and the
  glibc units' 138.1 where 179.1 (1.297): neither is slower.
- **The runtime functions' names need no rule.** A runtime call takes a
  declaration's symbol only for an object the selection numbered
  (`check_lower_declared_runtime`, `check_program_needed_number`), and the
  selection numbers only the objects its selected code references: a reading
  that spells the name or its builtin, which reaches the declaration. So the
  compile's reach drops the census's rule, and skips 85.72% of musl's tokens
  outside definitions where the census's reach skipped 79.31% (91.35% of the
  glibc units' where 87.00%), every object the same bytes. A first build
  with the rule (and the reader's environment published for the events)
  measured musl's command 1.145 times faster and the glibc units' 1.442, two
  rounds each.
- **What else the compile needed.** `-g` skips nothing: the unit's DWARF
  describes every type its table holds ("Risks" below). A diagnostic of the
  unit's end (`-Wunused-function`'s) is published past its last token, so it
  is placed by its location; placed by its output position, a `-Wall` unit
  learned nothing. A unit that skipped is read again with nothing skipped,
  from its start (`check_units_compile`), after an error or any stage's
  failure, the typo corrector, an unlisted event or a contradiction; none of
  the corpora's units needed it. The binding events' test marks a
  declaration's list by key as its events start: scanning the list at each
  event took a large function body's square, and made the compiler's own 61
  unity units 2 s slower.
- **Not done:** the field-by-class test of the reader's state ("Risks"),
  whose evidence is still the corpora's zero contradictions; the passes from
  the parser's roles, whose cost is now the gap to the expected gains; step 3.

## Step 3, its first part: what a skipped unit still clears and scans

With the skip, the reader's passes outside its walk cost more than the walk.
A probe of `check_input_run`'s phases (a measurement patch, the TSC ticks of
musl's command in a cloud VM, each phase's ticks a token of the units) gave
the walk 126 and the rest 196: the bindings table's initialization 36 (its
heads' clear, three namespaces a name of the lane's intern table, which holds
every unit's names: 14,569 a unit against 3,772 tokens), the columns' clears
30, the expressions' walk 53 (its bodies', which the skip doesn't change), the
intrinsics' pass 17, the tree's derived columns 16, the reading's arena's
release 10, the bindings' pass 7, the regions 7 and the rest under 5 each.
Two of them now take only what the unit reads:

- **The intrinsics' pass leaves the skipped declarations' operands out**
  (`CheckInputReader.skips`): the walk never reads them, and their names'
  lookups were most of the pass (17 ticks a token to 7).
- **The bindings table's heads aren't cleared** in the reading's own arena,
  which is reserved for it and zero (`c_bindings_initialize_zeroed`,
  `arena_untouched`): their pages are touched only where a name binds (36 to
  0.2, part of which the walk then pays as its faults).

On the 9700X against main's `26f0bb0e` (CPU 2, `setarch -R`, the median of 5
rounds; [the record](https://github.com/buster14a/buster-performance/tree/c3974d9530378c80ba8edfd9e5b6ccb07b8cc0b7/docs/measurements/attested-step3-2531-2026-11-13)):

| `-c` of the command, one lane | musl, 1,336 units | 512 units over glibc's headers |
| --- | ---: | ---: |
| main with `--attested=off`, ms (M cycles) | 1,014.0 (5,616.6) | 835.3 (4,629.9) |
| main | 858.1 (4,752.6) | 560.2 (3,104.2) |
| the first part | 831.6 (4,605.9) | 546.5 (3,028.6) |
| against main; against `--attested=off` (step 3's gate) | 1.032; **1.219** (1.26) | 1.025; **1.528** (1.7) |
| user instructions, main and the first part | 9,062.8 M; 8,973.9 M | 6,748.7 M; 6,637.6 M |
| page faults, main and the first part | 331,098; 290,116 | 153,938; 151,431 |

- **Every object and diagnostic** is `--attested=off`'s and `--scalar`'s,
  by one lane and by eight, with `-Wall -Wextra` and by `--attested=verify`
  (with `-g`); `-c sqlite3.c` (124.1 against 123.2 ms) and a small unit's
  process (1.3 ms each) are main's.
- **By eight lanes** (seven rounds) the CPU time is main's or less, 1,450.0
  against 1,425.6 ms on musl and 969.0 against 963.6 on the glibc units, and the
  wall time main's (199.6 against 198.4 ms, 135.0 against 135.1): the heads'
  saved user cycles pay for more kernel ones there.
- **Each part apart** (seven rounds): the intrinsics' pass alone is 1.011 on
  musl and 1.024 on the glibc units by one lane, 1.017 on musl by eight; the
  heads add 1.020 on musl by one lane and nothing elsewhere.
- **Left out: the names' columns that start zero** (`parameter_seen`,
  `member_seen`, `linked_declarations`, `linked_types`,
  `library_declarations`: 20 bytes a name), moved into the same zero arena.
  They cleared half as much, but each unit then faulted in the pages it
  touched: 16,600 more page faults on the glibc units' command, 230 to 280 M
  more kernel cycles by eight lanes, and slower there.

**The gate isn't held.** A skipped token's cost to the reader is now its
share of the passes that still take every token: the columns' clears and the
tree's derived columns (about 45 ticks a token of the unit in the probe),
which some 20 files read by token, so that taking the read ranges needs the
design's poison check of every column under `--attested=verify` first; and
the passes' own cost (5.0% of musl's user cycles). The glibc units' gap is
beyond the reader: with the skip, their preprocessing is 46% of the command
("What it leaves" below). The run of main with `--attested=off` varied by 4%
between two batches of rounds that day (1,014.0 and 1,060.6 ms on musl, the
same instructions), which moves the cumulative figure but not the comparison
with main, whose rounds alternate with the first part's.

## Step 3, its second part: the passes, and the reader's tree from a lane's cache

After the first part, a probe of the 9700X (the TSC ticks of each pass and
of each phase of `check_input_run`, each a token of the units; [the
record](https://github.com/buster14a/buster-performance/tree/30ae9dfc5ef642e3c51479bbee109a98eebac522/docs/measurements/attested-step3b-2531-2026-11-13)'s
`dev/probe/`) gave the passes before the reader 27.8 ticks a token on musl's
command and 24.3 on the glibc units', a sixth and a third of what the reader
then took:
the lists 7.9 and 9.1, the chain 5.3 and 5.1, the reach 4.3 and 1.9, the
literals 3.9 and 2.7, the own hashes 3.6 and 3.5, the tokens' levels 2.0 and
1.8. In the reader, `parse_tree_columns` was 7.8 and 7.4 ticks a token over
every token of the unit, skipped or not. Seven changes, each exact:

- **A function definition's semantic hash is its own in the lane**, made from
  the unit's stamp and its place. It is never attested, so its hash is only
  what the names it declares take; a later declaration that lists one of them
  then misses and is read. That is the conservative side of every rule, and
  musl's command skips the same 259,860 declarations and learns the same 2,995
  hashes. The own hashes and the literals then pass a body by, and the chain
  its names: a third of musl's tokens.
- **The lists come from a lane's cache** (`CheckAttestedListSlot`,
  `check_attested_lists_cached`). A declaration's list is made from its tokens'
  kinds, roles and names alone, which its own hash covers at their places, so
  the vector path takes the declarations' bounds (64 tokens at a time), then
  their own hashes, then each one's list from the cache by its own hash and
  length, or makes it a token at a time and keeps it. musl's command takes
  92.7% of its declarations' lists from the cache (all but 3,036 misses and the
  18,584 definitions), the glibc units 98.3%. The walk over the whole unit is
  the scalar path, the twin: `measure-attested --twins` finds no difference.
- **A cached declaration's tree columns are kept beside its list**
  (`check_attested_tree_columns`). In a valid parse each file-scope
  declaration is one tree whose root is its last token and has no sibling, so
  its columns are its parents' alone, offset by its first token, and its
  postorder positions are its own run (every earlier token's subtree ends
  before it). A hit whose parents are the same takes its columns from the
  cache, 16 tokens at a time; any other has them made over its own run, as
  `parse_tree_columns` makes the unit's. A kept tree is a halfword a token and
  column, with its parents. With `--attested=verify` and `--twins` the unit's
  columns are made whole too and compared, a difference a contradiction: none
  over musl's 1,336 units and the glibc units' 512.
- **The reader's system tokens take the levels the passes mapped**
  (`check_attested_token_levels`), 64 tokens at a time, instead of mapping the
  preprocessor's levels again; the passes' map goes 16 tokens at a time.
- **A body's list holds each name and class once**, so that the reach and the
  events' test take a name once however often the body spells it (the first
  entry, on both paths).
- **The chain fetches a name's state eight entries ahead**, and the lists'
  cache a slot four declarations ahead.
- **The expressions' walk gives a skipped declaration's tokens no class**
  (`check_input_expression_classes`): an ignored token with children took one,
  and the coded walk passed its children's empty lists on.

The probe after them (the record's `final/probe-*`): the passes 20.6 ticks a
token on musl and 17.6 on the glibc units, the tree columns 6.2 and 3.3, the
system tokens' phase 1.4 and 1.3, the expressions' walk 28.6 to 24.5 and 11.9
to 7.0.

On the 9700X (CPU 2, `setarch -R`, the median of 7 rounds), against main at
the branch's base `b7a95eed` and, as main has it now, with #2638's
preprocessor change:

| `-c` of the command, one lane | musl, 1,336 units | 512 units over glibc's headers |
| --- | ---: | ---: |
| main with `--attested=off`, main, the head: ms | 1,003.5; 833.2; 817.3 | 835.8; 545.9; 521.6 |
| … with #2638: ms | 943.0; 764.6; 749.6 | 796.6; 498.9; 476.8 |
| the head against main | **1.019**; with #2638 **1.020** | **1.047**; with #2638 **1.046** |
| against `--attested=off` (step 3's gate) | 1.228; with #2638 **1.258** (1.26) | 1.602; with #2638 **1.671** (1.7) |
| user instructions, main to the head | 8,973.8 to 8,603.7 M | 6,637.6 to 6,075.6 M |

- **Every object and diagnostic** is `--attested=off`'s and `--scalar`'s, by
  one lane and by eight, with `-Wall -Wextra` and by `--attested=verify` (with
  `-g`, its trees compared whole); `-c sqlite3.c` (123.9 against 123.3 ms) and a
  small unit's process (1.3 ms each) are main's.
- **By eight lanes** the CPU time is main's within 0.3% (1,453.1 against
  1,455.6 ms on musl, 971.1 against 974.1 on the glibc units) and the wall time
  main's or less (209.2 against 205.7 ms, 136.2 each); the head's kernel cycles
  are 40 to 60 M more in every round, with 2,500 to 3,200 more page faults:
  each lane fills its own caches, whose arenas take huge pages past their
  first base pages.

**What a skipped token still costs** (the record's `final/gone/`): the
preprocessed units of [the design's
record](https://github.com/buster14a/buster-performance/tree/c679fea7246dbea68cb312d132a60af79cf9deb2/docs/measurements/file-scope-design-672-2026-11-12)
as they are, with the compile's skip, less the same units with the emulation's
skipped tokens gone, in TSC ticks a skipped token (1.46 cycles a tick):

| | musl, main | musl, head | glibc units, main | glibc units, head |
| --- | ---: | ---: | ---: | ---: |
| the reader | 47.0 | 34.0 | 33.1 | 21.9 |
| … its walk; its expressions' walk | 12.0; 11.4 | 10.4; 4.3 | 5.9; 8.8 | 6.1; 3.1 |
| … its tree columns; the events' test | 7.8; 4.4 | 4.9; 4.3 | 7.3; 1.5 | 2.5; 1.5 |
| the passes | 42.9 | 32.7 | 26.3 | 18.7 |

The compile reads 58,815 of musl's tokens that the gone units lack (the two
skips' rules differ), about 4.5 of the walk's 10.4 ticks; the glibc units'
sets differ by 6,894 tokens. In cycles the reader's residual is about 50 on
musl (43 without the difference) and 32 on the glibc units, where main's was
69 and 48.

**The gate isn't held.** A skipped token costs `check-input` about 43 and 32
cycles (20), and the cumulative factor with main as it is now is 1.258 on musl
(its rounds 1.210 to 1.262) and 1.671 on the glibc units (1.26 and 1.7).

What the gate still needs:

- **The walk's spread.** It visits no skipped token, but its read tokens'
  columns are spread among the skipped ones': on the glibc units a read token
  costs the walk 300 ticks among them and 257 with them gone (musl's 185 and
  176). Compacting the read tokens before the walk would take that, and needs
  the column audit the first part named.
- **The tree columns of a skipped declaration** are copied from the cache (2.5
  to 4.9 ticks a skipped token); leaving them unwritten needs the same audit of
  the passes that read the tree whole (the commas', the packs', the limits').
- **The passes** (19 and 33 ticks a skipped token): the chain and the reach
  read each name's lane state out of the first two caches, the lists' hits copy
  from a pool there, and a definition's body still takes the lists' walk.
- **The names' arrays' clears**, 2.3 ticks a token of musl's units (0.6 of the
  glibc units'): a lane's intern table holds every unit's names.
- **The glibc units' preprocessing**, beyond the reader, which the first part
  found 46% of their command with the skip (before #2638).

## Step 3, its third part: the view

The second part left a skipped token's cost where the design's first idea for
step 3 put it: in every pass of the reader, and of the stages after it, that
takes the whole unit, some 20 files of them, each reading its columns by token.
Taking their read ranges one by one needed the column audit first. A view takes
them all at once.

**A unit that skips is read as its view** (`check_attested_view`): its parse
without the skipped declarations' tokens. A skipped declaration is attested
and not reached, so that no read declaration names what it declares, its
reading would report nothing and leave nothing behind, and step 2's walk
passed it with no visit. A file-scope declaration of a valid parse is a tree of
its own, whose root is its last token and which has no sibling, so that
leaving whole declarations out leaves the rest a valid forest:

- each column (kinds, names, sources, flags, roles, depths, parents) is copied
  a run of read declarations at a time, between two skipped ones, one copy a
  column a run, so that a skipped token is neither read nor written; the
  parser's padding around each column is the parse's own;
- a read token's parent, its own declaration's or none, moves down by the
  tokens left out before its run, as the token does;
- each pragma's position moves down by the skipped tokens before it (a pragma
  inside a declaration makes it ineligible, so that none is inside a skipped
  one), and the system levels the passes mapped are copied with the tokens;
- each declaration's place in the view (`view_begins`, `view_ends`; a
  skipped one's empty, where the next read token is) is what the census's own
  arrays, which keep the parse's indices, take to place the reader's events
  and diagnostics and the tree columns from the lane's cache.

The reader takes the view (`check_attested_read`), and so does every stage
after it: `check_driver_unit`'s `checking` is the view from the reader on
(`check_attested_parse`). The view's sources are the parse's, so every
location, spelling and diagnostic is the same. Three local labels are named by
a token's index (a string literal's `.Lbuster_string_N`, a constant
CFString's, a label whose address is taken): the view keeps each read token's
index in the parse (`Parser.origins`, `parse_origin`), so that every object is
the parse's bytes. The census's step 0, `--attested=verify` and `--scalar` read
the parse whole, as before; a unit whose view has no room is read as step 2
read it. A unit with an error is read again whole, where its typo corrections
are made (the driver makes none on a view). `--time-stages` counts the units
read as views.

The passes' marks of the skipped tokens' bits are made only for a unit read
without its view (`check_attested_mark_bits`); the marks pass counts them.

On the 9700X against main at the branch's base `ca1ede6` (CPU 2, `setarch -R`,
the median of 7 rounds, each round's trees in turn in one hold of the lock;
[the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/attested-view-memo-2531-2026-11-14)):

| `-c` of the command, one lane | musl, 1,336 units | 512 units over glibc's headers |
| --- | ---: | ---: |
| main with `--attested=off`; main; the view: ms (M cycles) | 913.9 (5,060.6); 733.5 (4,061.3); 721.8 (3,996.6) | 770.7 (4,269.6); 467.2 (2,587.9); 443.6 (2,457.7) |
| the view against main | **1.016** (rounds 0.975 to 1.019) | **1.053** (1.051 to 1.057) |
| against `--attested=off` (step 3's gate) | **1.266** ✓ (1.26; rounds 1.218 to 1.272) | **1.737** ✓ (1.7; rounds 1.733 to 1.747) |
| user instructions, main and the view | 7,426.0 M; 7,165.1 M | 5,288.3 M; 4,900.7 M |
| page faults, main and the view | 289,560; 279,689 | 151,024; 139,255 |

- **Every object and diagnostic** is `--attested=off`'s and `--scalar`'s,
  by one lane and by eight, with `-Wall -Wextra` and by `--attested=verify`
  (with `-g`); `sqlite3.c` with `shell.c` too. Every unit that skips is read as
  its view (musl's 1,321, the glibc units' 511).
- **By eight lanes** (5 rounds, CPUs 0 to 15) the view takes 1.036 times fewer
  cycles than main on musl's command and 1.131 on the glibc units', and the
  wall time is 188.4 against 185.5 ms and 128.1 against 116.7 ms.
- **`-c sqlite3.c`** (684.0 against 684.9 M cycles) **and a small unit's
  process** (6.3 M each) are main's: a command of one unit runs no passes.
- **The kernel's part.** musl's command takes 24 M more kernel cycles than
  main's by one lane, with 10,000 fewer page faults: `clear_page_erms` is 10.4%
  of its kernel cycles against 7.8%, and `/proc/vmstat` counts 719 huge pages
  faulted in where main's command faults 318. The view's columns are in the
  lane's state, whose pages stay; what changes is how much each unit's own
  arenas take, which the units' lanes give back in whole huge pages after a
  unit (#777) and the next unit faults in again. Which arena it is isn't
  measured here.

**What a skipped token still costs** (the record's `gone` and `perunit`): the
measurement-only probe (`scripts/probe.patch`) sums the TSC ticks of the
passes, the view and the reader. The design record's preprocessed units as
they are, with the compile's skip, against the same units with the
emulation's skipped tokens gone, the least of two rounds each way (1.46 cycles
a tick):

| TSC ticks a skipped token | musl | the glibc units |
| --- | ---: | ---: |
| the reader, as measured | 22.8 | 4.8 |
| … less the tokens the two skips' rules read differently, at the gone units' cost a read token | 16.9 | 4.3 |
| … by a least-squares fit of each unit's reader ticks on its tokens by class, two rounds | 13.0, 22.8 | 0.6, 0.7 |
| the view's copies | 2.6 | 1.0 |
| the passes | 31.9 | 18.4 |

The compile reads 58,815 of musl's tokens that the gone units lack, and a
token of a read declaration outside a definition costs the reader 450 to 550
ticks by the fit, where a body's costs 150 to 160: so the first correction,
at the average read token's 287, is too small, and musl's residual is about
11.6 ticks (17 cycles) at the fit's price for those tokens. The fit itself is
unsteady on musl, whose units' skipped tokens rise with their read
declarations. On the glibc units, whose sets differ by 6,894 tokens, the
methods agree: under a cycle to about 6.

**The gate** is held: a skipped token costs `check-input` about 17 cycles on
musl's command by the corrected comparison, 19 and 33 by the fit's two rounds
(20), and 1 to 6 on the glibc units'; the cumulative factors are 1.266 and
1.737 (1.26 and 1.7). musl's factor is within its rounds' spread of the gate.
What a skipped token costs besides is the passes' (32 and 18 ticks), now the
largest part, and its lexing and parse; the census (`measure-attested`, the
record's `census-*`) gives the passes 27.6 cycles a token of the units on musl
and 24.6 on the glibc units: the lists 11.4 and 9.8, the chain 6.4 and 7.9,
the reach 5.1 and 1.8 (the census's reach takes the runtime names, which the
compile's doesn't), the literals 3.6 and 3.9.

Two changes after the view, each exact:

- **The reader's tree columns take a view's read declarations alone**
  (`view_reads`): they visited every declaration, a skipped one's list block
  in the lane's pool loaded and the trees a few declarations on fetched.
- **The view is copied a run at a time by one loop** that takes every column
  16 tokens at a time (`check_attested_view_run`), where each column's run was
  a call of its own (the view was 10.2 M cycles of musl's command).

## Step 4: a memo kept across processes

The owner chose step 4 on 14 November. `--attested-memo=DIRECTORY` keeps the
lanes' memos across processes, so that a command of one source file, and a
build that runs a process a unit, skip what earlier processes' readings
attested.

- **A file for each key**, in the directory the option names
  (`check_attested_memo_open`): the key is the compiler's build, its GNU build
  ID read from its own program headers in memory (the auxiliary vector's
  `AT_PHDR`; the baked build makes the ID from the executable's bytes), and the
  command's arguments but its inputs, outputs and dependency files, its lanes,
  the stages' report and the memo's directory (`check_attested_memo_key`). So
  every hash a file holds was made by this compiler reading as this command
  reads, and a build of other options keeps a file of its own. A build without
  an ID keeps no memo, and says so.
- **The file** is a header (`BUSTATT1` and the key) and 16-byte records, each
  a semantic hash that a reading attested. A lane loads it as it opens
  (`check_attested_memo_load`, under a shared `flock`, its memo's table sized
  once for the records) and appends what it learned as it closes
  (`check_attested_memo_save`, under an exclusive `flock`), so that a build's
  processes share it and a record two of them wrote is one hash. A file whose
  header isn't its key's (torn, or scrambled), whose last record is torn, or
  past 2^20 records is written again with the lane's memo whole.
- **Portable hashes.** A lane's own memo hashes a name by its id in the lane's
  intern table; another process's table gives the name another id. With the
  memo, a name's word is its spelling's 64-bit hash (`CheckAttested.id_words`,
  made once a name by `check_attested_ids`), in the declarations' own hashes,
  in the chain's names and declared keys and in the pragmas' words; and a
  function definition's own hash, which is never attested, takes the
  process's random nonce beside the lane's stamp, so that no other process's
  definition has it. A definition's hash is what the names it declares take, so
  a later declaration that lists one of them can't hit another process's
  record.
- **A command of one source file** runs the passes with the memo
  (`check_driver_one_unit`): it opens a state for its unit, whose arenas are
  base pages throughout (a huge page's first touch would clear 2 MB for a few
  kilobytes of a short process) and stay mapped to the exit as the command's
  own memory does, and which keeps no caches of lists or trees and maps none
  (a unit only fills them: `CheckAttested.alone`). Its memo is the hashes in
  the order loaded and learned, read from the file by one call, and a table of
  their indexes at most half full (`check_attested_memo_load_alone`): about 24
  bytes a hash for the process to fault in, where a table of the hashes
  themselves takes 32 to 64. It reads its unit as its view only where it skips
  a third of the unit's tokens or more: each page of a view is a fault for the
  process, so a large unit that skips little (`sqlite3.c`, 6%) would copy more
  than the view saves, and its walk passes the marks instead. Its diagnostics,
  the preprocessor's and the checker's, go to an in-memory stream of their
  own, and a unit that skipped and failed is read again from its start with
  nothing skipped, as a units' lane reads its units again, so that only the
  second reading's diagnostics print. A command of several source files loads
  the memo in each lane, so that its first units skip too.
- **The tests** (`test-checker --attested`): each of the compile fixture's
  units by a command of its own, with the memo cold and then warm, against
  `--attested=off` (every object and diagnostic the same bytes; a command
  skips what earlier commands learned, every unit when warm); its two units
  with errors, each by a command of its own with the memo warm, read again
  and printing `off`'s diagnostics once; the units as one command, whose first
  unit skips; a command of other options, a file of its own; and a scrambled
  file, a memo of nothing, written again.

On the 9700X (CPU 2, `setarch -R`, 5 rounds; the record's `stat-ppu-*` and
`stat-sqlite4-*`), the memo warm (a process each of musl's preprocessed units,
then `sqlite3.c` twice, all with the memo), against the same build without
it:

| | without the memo | with it | | gate |
| --- | ---: | ---: | ---: | ---: |
| musl's 1,336 preprocessed units, a process each: M cycles (user, kernel) | 8,240.6 (4,143.5, 4,097.0) | 8,149.3 (3,486.0, 4,663.3) | **1.011** | 1.08 |
| … page faults | 791,559 | 1,212,826 | | |
| `-c sqlite3.c`: M cycles | 683.4 | 684.2 | **0.999** | 1.02 |

Every object and diagnostic of the units' processes is the same bytes with
the memo and without it.

**The gates aren't held.** A process a unit saves 15.8% of its user cycles (490
thousand cycles a process, the reader's skip and its view), and lost them to
316 more page faults a process: the memo's table, sized at a quarter full with
slack, took 128 pages for 3,000 hashes, and its load copied the file. Even with
no cost of its own, the user cycles saved are 1.087 times the command's. And
`sqlite3.c`, which names most of what it declares, skips 2,156 of its 6,873
file-scope declarations, 40,639 tokens (6%): the passes over its 716,000 tokens
cost about what that saves.

A follow-up sizes a one-file command's memo at half full once for its records,
loads it a block at a time through a buffer on the stack, and makes its lists
by the vector path without the cache: in the cloud VM a process takes 43 more
faults than without the memo, where it took 247, and 6 to 7% fewer user
cycles.

## Expected gains

From the skip measured in the reader, the prototype's passes and what a
skipped token still costs (`gains.py` in the record), in times faster for `-c`
of the sources as one command on one lane:

| | musl, 1,336 units | 512 units over glibc's headers |
| --- | ---: | ---: |
| measured: the reader's skip alone, its decisions made outside | 1.247 | 1.657 |
| step 2, conservative | 1.17 | 1.45 |
| step 2, expected | 1.23 | 1.62 |
| step 3, expected | 1.30 | 1.83 |
| step 3, optimistic | 1.32 | 1.91 |
| the bound: the file scope's reading free | 1.47 | |

- **Step 2, conservative:** 85% of the measured saving (the reader's own test
  may refuse more than the emulation's rules), less the prototype's passes as
  measured with their own scan, 22.3 cycles a token.
- **Step 2, expected:** the measured saving, less the passes at 12 cycles a
  token with their lists from the roles.
- **Step 3, expected:** and the reader's part of what a skipped token still
  costs, 77 cycles of both modes on musl and 66 on the glibc units.
- **Step 3, optimistic:** the passes at the prototype's 6.8 cycles a token
  without its scan, and everything a skipped token still costs outside its
  lexing and its parse (100 and 80 cycles).
- **`-c sqlite3.c`, and any command of one unit: 1.00.** Nothing is skipped
  and nothing runs. With step 4's memo warm the reader's skip measures 1.056
  on it (46,014 of its tokens passed), before the passes' 4 to 17 M cycles:
  1.03 to 1.05.
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
- **Step 2's compile** gains under 1.15 on musl, where the reader's skip
  alone measures 1.25: the passes in the compile, or the memo's own test, then
  cost or refuse several times what the prototype and the emulation say.

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
  them some other way, `-g` units don't skip until it doesn't. Step 1 found it
  does: `c_check_debug_values.c` describes every type the unit's table holds
  (a header's unused `struct` among them, unlike clang), so the objects of
  `test-check-units`' `-g` programs differed. `-g` skips nothing; `--attested=verify`
  runs with it.
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

With the reader's skip alone musl's command is 4,448 M cycles where it is
5,547 M; after step 3 the expected command is about 4,200 to 4,300 M. The
ranking's other rows are what is left, none of them designed here. By the
stage counters of the command with the skip:

- **The kernel, 23% of that command** (1,038 M cycles). musl's command makes
  28,599 `madvise` calls and takes 343,028 page faults, 21 and 257 a unit: a
  unit's arenas given back and faulted in again by the next, a megabyte each
  time. Page faults are 47% of the kernel's cycles and `madvise` 35%; a
  quarter of them fall between two units. An empty unit in a command costs
  72 µs, 149,000 user and 246,000 kernel cycles.
  [#1213](https://github.com/buster14a/buster/issues/1213) has the lanes'
  side of it and what was tried there (keeping a unit's pages and clearing
  what the next unit takes was slower), and
  [#1444](https://github.com/buster14a/buster/issues/1444) a process's
  start. And a run in three is given no huge pages at all, by its randomized
  layout, for 75,000 more page faults and 2.3% of the command (the record,
  section 13); which mapping decides isn't known.
- **The preprocessor, 20%** (884 M), and 46% of the glibc units' command with
  the skip (1,177 M of 2,585 M): what is left of a header-heavy unit is its
  preprocessing, each inclusion's directives and expansions over the cached
  tokens, 175 and 219 cycles for each token it puts out. 116 M user cycles of
  musl's are what 1,336 empty units' preprocessing costs. The memo's idea
  applies one level up (an inclusion that reads the same macros' same
  definitions puts out the same tokens and makes the same definitions); it is
  not designed or measured here beyond the stage's size.
- **The asm matcher's index, 8%**:
  [#2532](https://github.com/buster14a/buster/issues/2532). Made once a
  process, musl's command takes 929 ms where it takes 1,008. Baked since
  (`docs/c-inline-assembly.md`): 991.4 ms where `0af27a30` took 1,064.2.
- **The declarations still read**, about 470 M cycles after step 3: the
  reached, the unattested and the ineligible, the units' own tables first.
- **The skipped tokens' parse**, 30 user cycles each: 89 M on musl (2% of the
  command with the skip) and 141 M on the glibc units (5%). A declaration's
  parse depends on its tokens and on which of its identifiers are typedef
  names, both of which the semantic hash already covers; whether the parse is
  attested too is open. 11 of the 30 are the tree's derived columns
  (`parse_tree_columns`), which could be made for the read ranges alone once
  the marks come before them.
- **A unit's own setup in the reader.** Each unit clears seven arrays as long
  as the lane's intern table, which grows with the command
  (`check_input_run`'s `parameter_seen`, `member_seen`, the linked and
  library declarations, the builtins' names and the binding table's heads: 34
  bytes a name a unit). A stamp a unit, as the passes' own per-name state has,
  would leave them alone. libc's `memset` is 108 M of the skipped command's
  user cycles, and 53 M in a command of as many empty units; these arrays'
  part of it wasn't measured. `check_input_intrinsic_initialize` is 63 M:
  every unit looks its operands' names up among the intrinsics' again
  ([#893](https://github.com/buster14a/buster/issues/893) has the lookup's
  cost in one unit), and the answer is a name's, which the lane's intern
  table could keep (step 3 takes the skipped tokens' share of it). The table
  keeps it now (#893's memo, `docs/builtin-intrinsic-filter.md`).
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
  design does nothing for one unit before step 4, which measures 1.056 there
  before its passes' cost). On musl's command: this design's expected 1.23 at
  its step 2 and 1.30 at its step 3, alone. #2524 puts its own step 4 at
  about 1.1 there, which with this design's steps is 1.4 to 1.5; these
  counters put the bodies' part between the parser and Tier 0 at 25.6% of the
  command, so with the coverage #2524 expects on `sqlite3.c` (three quarters
  of that part) the two together are about 1.7, and about 2.0 with #2532's
  index made once. The savings add: the two designs take disjoint tokens'
  work.

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
