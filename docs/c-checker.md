# C semantic checking and typed expressions

`c_checker.c` implements the expression graph and two schedules for
[#133](https://github.com/buster14a/buster/issues/133). `c_check_actions.c`
supplies their shared C semantic actions; `c_check_input.c` is the current
parser and declaration adapter. The default C command and explicit checker share
the semantic driver:

```sh
./build/compiler check [--scalar] [--dump-typed] [--dump-static] [preprocessor options] file.c
./build/compiler [--scalar] -fsyntax-only [preprocessor options] file.c
./build/compiler [--scalar] [--lower-all] -c [-o file.o] [preprocessor options] file.c
./build/compiler [--scalar] [--verify] [--lower-all] [-o a.out] [preprocessor options] file.c
```

The compiler driver accepts `-pipe` in C compilation and preprocessing; it has no effect because its stages run in
one process. `--version` identifies a development build and prints its target, while `-dumpmachine` prints only the
target triple. These actions need no source file and accept `-target` or `--target=` for supported x86-64 Linux GNU
targets; without one, the build Clang's configured vendor is used. They do not implement `-v` or the other driver
compatibility options still tracked in [#805](https://github.com/buster14a/buster/issues/805).

Sanitizer instrumentation and OpenMP code generation are not implemented
([#1112](https://github.com/buster14a/buster/issues/1112)). Object emission
(including `check -c`) and direct linking reject an enabled `-fsanitize=...`,
instrumenting `-fsanitize-coverage=...`, or `-fopenmp[=runtime]` by name before
reading source files or creating/replacing output. This applies to both tiers
and to `--scalar`. The final resolved option state decides: disabling all
requested sanitizers or OpenMP with later negative options permits ordinary,
uninstrumented compilation; disabling only part of a sanitizer group does not.
Coverage follows the same rule: a later `-fno-sanitize-coverage=` (or `-fsanitize-coverage=0`) that leaves no
instrumenting feature permits compilation, and the refusal names the last `-fsanitize-coverage=` spelling still active.
The bare `-fsanitize-coverage`, which clang doesn't take, is refused by name in the semantic driver's actions (`-c`, a
link, `check` and the dumps), but not by the stand-alone preprocessor (`-E`)
([checker guide](commands/checker.md#unsupported-hardening-requests)).
Preprocessing, `check` without `-c`, and `-fsyntax-only` retain predefine modeling.
`-fsyntax-only` suppresses `-c` in either order. A sanitizer or `_OPENMP` macro
observed in a non-emitting action is not a promise that Buster can generate the
corresponding instrumentation or parallel runtime calls.

The driver gives Clang's common default and `-Wall`/`-Wextra` warnings for
constant conversions and bounds, shifts and division, parenthesization, unused
results and names, format calls, returned stack addresses, uninitialized reads
and enum switches, with Clang's messages, places, notes and
`#pragma clang diagnostic` policy; `-Werror` and its `=group` and
`-Wno-error=` forms promote them. The remaining gaps are the full
`-Wuninitialized` dataflow, the `scanf`/`strftime` `format` archetypes,
computed `case` expressions in `-Wswitch`, and several rarer
`-Wparentheses`/`-Wreturn-stack-address` shapes and `-Wunused-but-set-variable`,
so Clang remains a separate diagnostic check for source that relies on those. The implemented families,
their boundaries and their differential tests are described in "Common
warnings (#823)" below; the remaining work stays tracked in
[#823](https://github.com/buster14a/buster/issues/823).

`-c` and the direct path lower only the definitions that the unit's roots reach,
as clang does ([#129](https://github.com/buster14a/buster/issues/129),
`check_program_select`, [the lowering guide](c-lowering.md)): an unreferenced
static or inline function gets no code, and an object only such a function
refers to no data. `--lower-all` lowers every definition that may be emitted,
referenced or not, a test option for measuring what that saves.

`#pragma GCC visibility push(default|hidden|protected|internal)` and `pop`
retain a source-ordered state stack (#620). Declarations sample that state;
the first applied visibility follows the linked entity through later
declarations, and an explicit attribute on the same declaration takes priority
over the pragma. Internal visibility maps to hidden on this x86-64 target, as
Clang does. The object writer receives the ELF visibility on defined and
undefined global symbols; local symbols keep default visibility. Malformed
directives carry bounded syntax diagnostics, and an unmatched pop rejects the
unit. Unknown visibility names warn and leave the stack unchanged. Ordinary
units allocate no visibility history or object column.

`__private_extern__` declarations use extern linkage and hidden ELF visibility.
Uninitialized variable declarations warn under `-Wprivate-extern` with the suggested
`visibility("hidden")` attribute as an attached note; initialized variables
and functions retain hidden visibility without that warning. The
warning group, `-Werror`, source pragmas and suppression apply to the note
with its primary. Later declarations preserve the linked entity's visibility.
The visibility driver fixtures compare the resulting defined and undefined
symbols against Clang's ELF output.

Before C23, a direct call of an undeclared identifier creates the compatibility
declaration `extern int name()` in that block's source scope (#623). Repeated
calls in the same scope share it; later prototypes use the ordinary linked
entity compatibility rules. Arguments use the default integer and floating
promotions until a prototype is visible. C99 and later diagnose the first call
with its identifier and `-Wimplicit-function-declaration`, as Clang does;
`-Wno-error=implicit-function-declaration` demotes it, while
`-Wno-implicit-function-declaration` suppresses it. Source diagnostic pragmas
apply too. C23 keeps an undeclared-identifier error and no implicit declaration.
Parenthesized identifiers and non-call uses of ordinary names do not acquire an implicit type.

C89's implicit `int` is read too (#1121): a declaration whose specifiers give no
type has type `int`, at file scope and in a block, for objects, functions,
parameters (an identifier list's and a prototype's, as in `int f(*p)`,
`int g(const x)`, `int h(register r)` and `int k(int a, *p)`), members (`struct S { const x; };`), `register`/`auto`/`static`/
`const` declarations and typedefs. A file-scope declaration with no specifier at
all is such a declaration too (`x;`, `main() {}`, `f(a, b) {}`, `(x);`, `*p;`,
and a declarator whose name follows a pointer's qualifier or attribute, as in
`*const p`, `**const p`, `*__restrict p` and `*__attribute__((unused)) p`);
a block-scope declaration needs a specifier to begin it, as `x;` there is an
expression. A C89 type name may also hold only qualifiers, so `sizeof(const)`,
`(const)1` and `sizeof(volatile)` are `int`; `sizeof(register)` and
`sizeof(static)` still take the parser's error. C99 and later keep clang's
missing-type diagnostic, which the parser reports before the checker runs; an
old-style definition with a declared return type keeps its own
undeclared-parameter diagnostic. In C99 `f(a, b) {}` the parser's missing
return type at 1:1 stays before clang's first undeclared parameter at 1:6, as
documented for the existing C99 boundary.

Eligible library names are different (#804): their first undeclared use takes
the library prototype from the frozen Clang builtin catalogue, then uses the
ordinary C binding, argument-checking, ABI and external-symbol paths. The
catalogue's library attribute and language eligibility, not a list of example
names, control admission. GNU-only library names stay ordinary undeclared
functions in strict modes. A source declaration or local shadow always wins.
The lazy library declaration remains visible at file scope after its first
use's block closes. The reader retains its source token separately from the
lexical binding events and reactivates it only when a later use or declaration
needs it. A closed block's explicit prototype does not become the visible file
prototype; local objects, typedefs and enumerators can still shadow the library.
Later compatible declarations share the entity; an incompatible library
prototype warns under `-Wincompatible-library-redeclaration`. Static linkage
conflicts and malformed calls remain errors. Header-dependent descriptors such
as `FILE *` do not become invented types: a direct pre-C23 call falls back to
the ordinary implicit declaration and diagnoses `-Wbuiltin-requires-header`.

An implicit library declaration warns by default even in C89. The ordinary
implicit-declaration warning switches, promotion/demotion and source pragmas
apply. An earlier explicit linked function declaration of an eligible library
name suppresses its later first-use warning after that block closes; this
borrows the prior declaration's diagnostic fact, not its closed-block type.
`-fno-builtin` and exact `-fno-builtin-NAME` options disable this treatment
for plain library names, but do not disable explicit `__builtin_` spellings.
`-fbuiltin` reverses the global disabling option; freestanding mode separately
disables library builtins, matching the pinned Clang 23.1.4 oracle. Its
official build's table has `strlcat` and `strlcpy` as GNU library builtins, in
GNU C89's warning and GNU C99's and later modes' error alike, as every upstream
23.x release's does; Debian's and Ubuntu's clang packages (apt.llvm.org's too)
drop them with `debian/patches/23-strlcpy_strlcat_warning_removed.diff`, so the
oracle compares `clang --version` with 23.1.4's official build, prints both
builds when they differ (`found Ubuntu 23.1.6`), and skips only the plain names
the found build's vendor is known to classify differently (the named list
`cbuiltin_library_table_differences`: Debian's and Ubuntu's `strlcat` and
`strlcpy`), a difference no named build explains still failing. Calls of
`strlcat` and `strlcpy` are plain calls in every mode, by both paths: neither
is one of the plain names buster folds. A recognized library declaration can
still be formed in C23 when its diagnostic is suppressed; an unknown C23 name
cannot acquire the removed `int ()` fallback.

`test-checker-driver --executable PATH` compares the original #804 fixtures
against external Clang through syntax-only checking, object publication and
native execution, including scalar/default path agreement and pointer-sized
results with builtin options enabled and disabled. This is correctness
coverage, not a Zen 5 performance qualification.

The last form compiles and links an executable by the direct path (#145): the
checked program lowered to Tier 0, bound against the C runtime's snapshot
(`runtime_open`, `bind_begin`/`bind_end`), and written as a dynamic PIE
(`executable_write`). A binding that finds the snapshot damaged discards it and
links once more from a fresh import. `--verify` checks the reader's expression
graph and its retained scheduling/allocation facts against the independent
graph validator, and also checks every function's
records with `x86_validate`, as the c-testsuite harness asks
([the c-testsuite guide](c-testsuite.md)), with `-c` too. Without it, the
emission checks only each function's branch columns, which branch sizing reads
and writes by (`branch_sizing_validate_branches`): Tier 0's records are valid by
construction, and `x86_validate` of them all made branch sizing take 14 times
the encoder's time on `sqlite3.c`, against 1.33 times without it
([#532](https://github.com/buster14a/buster/issues/532),
[the branch sizing guide's contract](branch-sizing.md#contract)).

`-lNAME` (or `-l NAME`) links a shared library besides the C runtime's (the
value of an option that takes the next argument stays that option's, as
`pp_option_takes_value` lists them: `-o -lresult` writes `-lresult`), found as
GNU ld finds it: `libNAME.so`, or else `libNAME.a`, in the first of the `-LDIR`
directories, then the GCC installation the runtime selected and the native
prefix's `lib64`, then glibc's library directories, that has one; a static
executable considers only `libNAME.a`. A linker script (glibc's `libm.so`)
stands for the libraries it names,
by absolute path, relative path or `-l` name, and the direct path reuses the
selected `libgcc.a` and refuses other archives. Binding takes a name
from them before `libc.so.6`, where GCC's driver puts them on the line, and the
executable needs one only for a strong reference that binds to it, the
program's or a needed library's own that its `DT_NEEDED` doesn't cover and no
definition the executable exports satisfies, as GNU ld links them
`--as-needed` (the default of Debian's and Ubuntu's GCC);
`DT_NEEDED` lists them before `libc.so.6` (`runtime_libraries_import`, `bind.c`
and `dynamic.c`). A needed library's strong reference that nothing defines is
refused, naming the library, as GNU ld and lld refuse it; one that requires a
symbol version binds only to an export at that version, as both linkers bind it,
and version needs that don't read are refused. Each loaded library's version
needs must be versions that the library each need names defines (a named
library, `libc.so.6` or the loader), as glibc's loader checks them at startup;
neither linker checks them (`bind_library_versions`). An empty archive (glibc's
`libpthread.a`, `libdl.a` and `librt.a`) adds nothing; one with members is
refused by name, since the direct path doesn't link archives yet. A call of a
function that a system header declares and no linked library has gets a named
error that points at `-l` (`-lm` for libm's). A library's data, the C library's
or a named library's, is reached through its GOT slot (the access column,
below).

It preprocesses, parses, resolves supported declarations, checks expressions,
and reports errors with the preprocessor's source locations. A successful check
returns zero. `--scalar` selects both the scalar parser and the postorder checker
schedule and the independent scalar binding-event resolver. `--dump-typed`
prints the node types, categories, constants, faults and operand/store
conversion metadata. `--dump-static` materializes supported static
definitions as real `Link` atoms and prints their bytes and symbolic fixups.
Preprocessor options include the
existing language, include-path and macro-definition options.

The semantic driver refuses unsupported hardening, instrumentation and ABI
options by name rather than silently producing an object without them (#814).
This includes stack protection, CET, stack-clash checking, `-pg`, function
instrumentation and profile-generation forms, coverage flags, explicit TLS models, `-mno-red-zone`,
`-fno-plt`, and the unsupported structure-return, short-double and
wide-execution-character options. The last stack-protector and CET option
decides those families, so `-fno-stack-protector` and `-fcf-protection=none`
can turn them off. The other listed options are conservatively refused even if
a later argument would disable them. The separate `-E` preprocessor retains
its predefined-macro behavior; it emits no object. This list does not cover
sanitizer options.

Every body is parsed and checked by default. `-flazy-system-headers` optionally
skips semantic checking of provably unreferenced static and nonemitting inline
bodies defined in system headers, identified by `-isystem` or system-header
pragmas. Declarations, ordinary-source bodies, referenced bodies and all syntax
are still checked. This mode intentionally accepts a system header whose only
semantic error is in a deferred body; it departs from standard C diagnostics
and from Clang's default behavior, as authorized in
[#155](https://github.com/buster14a/buster/issues/155).
`-fno-lazy-system-headers` restores the default, and the last option wins.
Both modes use the same checked-reference selection for object publication;
tests require equal objects for valid inputs. Performance comparisons use the
default mode, with any lazy-mode measurements reported separately.

Unsupported source constructs produce an explicit diagnostic. `-c` also
requires the checked program to be representable by the current backend; a
successful syntax check alone does not establish executable support for every
type or operation. The explicit `parse` command retains the parser-only path.
The combined `-c --dump-static` command validates the object and renders the
same resolved `Link` before publishing the object file. Its data relocations
therefore share the actual emitted functions' symbols.

Semantic checking currently uses one C LP64 target model: 32-bit `wchar_t`,
80-bit x87 `long double` stored in 16 bytes, and floating arithmetic evaluated
in its source format unless a lexical `clang fp eval_method` pragma changes
its usual unary conversions, with plain `char` signed (x86-64's) or unsigned, as
`-funsigned-char` and its spellings say. Options that advertise a different
model are rejected before declaration elaboration. This includes short
`wchar_t`, 64/128-bit `long double`, command-line double/extended evaluation, and
default extended evaluation when SSE is disabled. Explicit source-format
evaluation is accepted. Assembly is outside this adapter, and the C language
option parser does not admit C++ modes.

Plain `char`'s signedness is the type table's (`TypeTable.char_unsigned`), which
`check_input_run` sets from the unit's options, the deferred reader on the
caller's table too, and `type_copy` keeps. Every phase reads it from there:
`type_integer_properties` answers it, so the folding, conversions and
comparisons, the initializers, a `mode` attribute's base (which keeps its
signedness, an unsigned one's too) and the lowering's classes (U8 where it was
I8) follow it, as does the convention's extension of a char argument
(`abi_extension`, zero for unsigned). An ordinary character constant of one
character is its char's value (`'\xff'` is 255 where char is unsigned, -1
where it's signed); multicharacter, `u8`, `u`, `U` and `L` constants don't
depend on it. `test-checker`'s fixtures check each against clang with
`-funsigned-char`, and its program fixtures build clang's oracle with the
fixture's own signedness (`-fsigned-char` but for those that ask otherwise),
one of them from the same source both ways.

The action families share the [semantic-context and provenance contract](checker-semantic-context.md): source visibility, receipt-read permission and runtime effects are separate.

## Implementation boundary

The source adapter handles nested pointer, array and function declarators;
prototype scopes and parameter adjustments; struct, union and enum definitions
in a declarator, in a parameter's specifiers or an array bound; typedefs and
local scopes;
arithmetic, pointers, calls and casts; records/unions, anonymous members,
bit-fields (with GNU attributes after a width, the member's, which the parser
hangs on the member's end, outside the width's tree, so that the width is
evaluated as any constant expression is) and flexible arrays (a struct whose only named
member is one too, a GNU extension clang takes, with clang's layout);
enumerations, C23's with a fixed underlying type among them, and source-point
completion.
It retains initializer lists, designators and ranges, strings and predefined
function-name arrays, compound literals, GNU statement expressions, generic
selection, `typeof`, and runtime array bounds. Increment/decrement and compound
assignments retain one destination evaluation and an explicit store conversion.
`if`, loops, `switch`/cases, labels/goto, break/continue, returns and static
assertions receive semantic checks. Assembly operands/constraints and source
attribute applicability have separate bounded consumers.

An enum with a fixed underlying type (`enum E : T`, C23's, which clang takes in
every mode) is complete at its base, so that its own list may take `sizeof` of
it. Its enumerators have the base's type in its list, and after it the enum's
in C23 but still the base's before it, as clang 23 types them (two such enums'
enumerators of one base are of one type before C23, and of two after it). Each
value is representable in the base as clang's `isRepresentableIntegerValue` has
it (a negative one by its significant bits, any other by its active bits, less a
signed base's sign bit) and converted to it; the base drops `const` and `volatile` (`restrict`, which
needs a pointer, is refused), written or a typedef's, with clang's
"'const' qualifier in enumeration underlying type ignored"
(`-Wunderlying-cv-qualifier-ignored`, #1952) once, at the base's first token
that isn't a qualifier; they aren't the declaration's own, so a declaration of
the enum alone gives no "'const' ignored on this declaration" for them. An
integer type of any width but `_BitInt` takes it. GNU attributes among the base's specifiers are the base type's, as
clang has them: ignored, but one that would make it a vector's (`vector_size`,
`ext_vector_type`), refused. Its redeclarations in its scope keep the base, a
declaration of it alone (`enum E;`) too; a reference to it (`enum E x;`) needs
none. A declaration of it with a declarator (`enum E : int x;`) is refused at
the `:`, and an enum without a base can't be given one, nor the other way round,
each where clang reports it. An `aligned` attribute after its body, which clang
applies after the base completed it, sets its alignment then (#1447,
`type_enum_realign`), as clang's does where nothing has asked its layout yet:
clang memoizes an enum's layout at its first query, after which the attribute
changes nothing. So it is taken where the enum completed at this body's `{` and
the body names it (`enum E`) only as a cast's type; where the body asks its
layout (`enum E` in a `sizeof`'s or an `_Alignof`'s operand) it changes
nothing, as clang's does, and a pointer to it (`sizeof(enum E *)`) asks
nothing; an array of it (`sizeof(enum E[2])`) asks. One whose body names it
otherwise (a member's type, `sizeof(struct { enum E e; })`, which this
doesn't read), or one completed by an earlier
declaration (`enum E : char;`), stays refused as unsupported. (Attributes
after a tag's name, before a `;`, make any tag's declaration a reference to the
visible tag, as clang has it, not a declaration of it alone.)

**Bit-precise constants** (#768, `check_literal_integer`): C23's `wb` and `WB`
suffixes, alone or with `u` or `U` before or after them, in every radix and with
digit separators (where the mode lexes them), typed as C23 and clang 23 type
them in every mode: `_BitInt(N)` of the smallest N above 1 that holds the value
and a sign bit, or `unsigned _BitInt(N)` of the smallest N above 0 that holds
it. An ordinary integer constant is read as before, in 64 bits: a spelling that
reader can't take goes to a cold reader of bit-precise constants
(`check_literal_bit_precise`), which reads the digits again in 128 bits, so
that every other constant's path is as it was. (A first version that read every
constant's suffixes for these, with a 128-bit value in every literal, cost
1.36 million more instructions on `sqlite3.c`'s `-fsyntax-only`, about 25 per
integer constant; this one about 196,000, in the literal's two new fields.) A
constant whose type is past 128 bits (#1447) — one that needs more bits, or a
`wb` one of 128 bits with its top bit set, which is `_BitInt(129)` — is read
once more, into limbs (`check_literal_bit_precise_wide`): the literal points to
them as a wide value does ("Constants past 128 bits" below). One past
`BITINT_MAXWIDTH` (8,388,608 bits) is refused by name. `_BitInt(129)`. The literal leaves its type to the reader, which makes the
`_BitInt` in its table (`CheckLiteral.bit_width`, `check_input_literal`); #658's
literal tables leave such constants to the ordinary path. `#if` reads them as
clang does, through the preprocessor's own constants. Before C23 the suffix
has clang's -Wc23-extensions warning, "'_BitInt' suffix for literals is a C23
extension", at the constant (#1447): once, from the reader
(`check_input_bit_int_suffix`), as the constant evaluator may read it again;
from the driver's scan of what a failed parse left unchecked
(`check_driver_bit_int_suffix`); and in `#if` from the preprocessor, with
its flag, mapped as clang maps it (`pp_warning_severity`, #1934): the
command line's `-W[no-]c23-extensions`, `-W[no-]c2x-extensions`,
`-W[no-]everything`, `-Werror`, `-Werror=`/`-Wno-error=` and
`-pedantic-errors`, under the source's `#pragma clang diagnostic` for the group,
which the preprocessor follows through push and pop, at any depth, for it
alone. Clang's -Wpre-c23-compat warning of
it in C23 isn't given. One difference from clang remains: clang takes an
imaginary one (`5wbi`, a `_Complex _BitInt(4)` that it doesn't let a program
spell otherwise), which the checker refuses, as GCC does.

What this compiler's own `main.c` needed (#129) the checker takes as clang 23
does, each rule checked against clang's answers on probes of it:

- **Loop hints** (`c_check_loop_input.c`): `#pragma clang loop` with its
  eleven options, and `#pragma unroll`, `nounroll`, `unroll_and_jam`,
  `nounroll_and_jam` and `GCC unroll`, as hints of the same categories, read in
  clang's three steps. Its pragma handler lexes the options' names and their
  arguments in parentheses: an unknown name, a missing `(` and an unclosed
  argument are errors, and a token after the options that isn't another one's
  name drops the pragma, with a warning (`unroll_count(4))`), so that nothing
  in it is placed or checked. Then each argument: a state takes its own words
  (`licm` only `disable`, `vectorize_predicate` `assume_safety` too), and a
  count must be a positive integer constant that fits an `int`
  (`vectorize_width` also `fixed` or `scalable`, alone or after the count).
  What follows the word or the constant expression clang reads is ignored,
  with a warning (`vectorize(enable x)`, `vectorize_width(scalable, 4)`); an
  empty argument is an error, and so is one no expression starts with
  (`unroll_count(;)`, `unroll return`). Last, no hint may come twice in one
  category, across the pragmas before one loop too, nor a count beside a state
  that rules it out (`unroll`'s every state, the others' `disable`), and a
  `for`, `while` or `do`-`while` loop must follow, in a function. Errors are
  reported on the pragma's line, but a misplaced one's at the statement after
  it, as clang reports them. `GCC unroll` is `unroll` (#480's review), whose
  count may be left out, in parentheses or the rest of the line, as clang's
  `ParseLoopHintValue` reads it; a pragma clang drops with a warning
  (`nounroll 4`, `unroll (4) x`) is no hint. A count here is an integer
  literal, alone or before a token no expression goes on with (`unroll 4)`,
  `unroll_count(4, x)`, as clang reads them): another constant expression
  (`4 + 4`, `-1`, an enumerator) is refused as unsupported, not guessed. In a
  function, clang checks a count before it reads the next option or pragma,
  and places the hints and checks them against each other only after the
  statement they precede, so the first error is the count's when it isn't a
  valid one (`int N`, `4 / 0`, an enumerator of 0), and a later pragma's or
  the place's only when it is (#480's fourth review): such a count leaves the
  loop's pragmas unsupported, including when a state or count of its category
  shares its pragma. At file scope, where clang reads no count, the pragma is
  an error, reported at the directive with the source spelling (`invalid
  '#pragma GCC unroll' directive`). Placement, duplicate and incompatible
  hints wait for the whole annotated statement (#503): its exclusive end is
  the reader's barrier and the ordering position of its report, so the
  diagnostic graph includes the statement's type errors too. The printed
  source is separate: conflicts at the later pragma's `loop` or hint name,
  placement at the statement's first token, with clang's directive names and
  messages (where `GCC unroll` is `unroll`). Nested statements finishing
  together report the inner hints first. The existing postorder subtree ends
  supply the boundary; the pragma reader follows parent links to the statement
  root without scanning its body. Only a rejected unit retains one cold
  `CheckInput.loop_diagnostic` record; successful loops retain no hint metadata.
  Dropped pragmas create neither a placement error nor a barrier. Counts not
  evaluated here remain unsupported rather than guessing which later error
  clang would report. At the same completion position, an error from the next
  pragma's handler precedes the current hints (the parser lexes that lookahead
  before Sema); an error parsing that next pragma's argument follows them.
  The rejected prefix still runs the scalar/vector control walk: duplicate
  labels and completed switch constraints inside the statement precede the
  hint. It collects the function-scoped names of forward labels from the tree,
  while deferring undefined-label and jump-scope obligations until function
  completion. Unfinished label references have canonical diagnostic-only
  identities for typing `&&name`; these can never be published or emitted.
  The ordinary control walk is specialized without the prefix checks.
  Tier 0 has no loop transformation for the hints to steer.
- **`#pragma STDC FP_CONTRACT`**, with `FENV_ACCESS`'s placement: at file scope
  or a compound statement's start. Its state changes nothing, since nothing
  here contracts `a * b + c` into a fused operation.
- **The clang and Microsoft floating-point spellings** (`c_check_fp_input.c`):
  `#pragma clang fp` accepts any sequence of `contract(on|off|fast)`,
  `reassociate(on|off)`, `reciprocal(on|off)`, `eval_method(source|double|extended)` and
  `exceptions(ignore|maytrap|strict)`. `#pragma float_control` accepts
  `precise` and `except`, with optional `on`/`off`, `push` modifiers, and
  standalone `push`/`pop`. The file-scope stack saves every floating setting;
  compound-scope settings restore at the closing brace. An empty `pop` reports
  Clang's `-Wignored-pragmas` warning with ordinary command-line, source and
  system-header policy. Illegal precision/exception combinations and stack
  operations inside a compound have Clang's diagnostic text and location.
  Permission to contract, reassociate, use reciprocals or relax precision does
  not require those transformations; generated evaluation remains precise.
  `strict` and `maytrap` preserve runtime operations and conversions through the
  existing floating-environment path, including discarded results and constant
  operands. The exception state is separate from STDC `FENV_ACCESS`, so changing
  one does not erase the other.

  `eval_method` changes ordinary scalar and complex evaluation through the
  existing conversions and lowering: `double` widens narrower floating
  operands to `double`, and `extended` to x87 `long double`. Literals retain
  their source rounding. Casts, assignments, returns and fixed prototype
  parameters retain their destination formats; increment/decrement retain
  their lvalue format. Compound assignment applies usual unary evaluation
  only to its right operand before finding the common arithmetic type.
  Variadic `float` arguments still pass as `double` under extended evaluation,
  while an original `double` argument widens. Vector element types do not
  change. Constant expressions, type queries and runtime expressions use the
  same state. The optional graph byte stores evaluation separately from
  runtime `FENV_ACCESS`, so wider constant evaluation does not itself require
  runtime execution.

  A later separate `reassociate(on)` or `reciprocal(on)` conflicts with an
  active evaluation pragma, even after `eval_method(source)` or a
  `float_control(pop)`. The semantic reader diagnoses conflicts with
  effective global approximation, reassociation and reciprocal permissions.
  The production driver retains its named refusal for these unsupported
  command-line modes before reading source.
  `__FLT_EVAL_METHOD__` expansion in an active evaluation-pragma scope is an
  error, including macro expansions consumed by `#if`; a closing compound
  restores the enclosing scope. Cold preprocessing events retain expansion
  provenance and their order among adjacent pragmas. Ordinary `-E` remains
  unaffected.

  With an explicit command-line evaluation method, a typedef marked
  `available_only_in_default_eval_method` cannot be used under a differing
  last pragma method. File-scope `float_t` and `double_t` receive the same
  implicit restriction. `float_control(pop)` restores arithmetic precision
  but retains that last method; a closing compound restores both. A cold
  pass over resolved uses handles redeclarations and constant-only type
  queries in source order, without applying later
  attributes to earlier uses. Command-line `double`/`extended` selection
  remains an explicit target-model refusal; lexical methods above are
  implemented within the existing LP64 model.
  The handler names are not macro-expanded. Semantic preprocessing expands
  their operands at the original source point, including `_Pragma`, function
  macros and subsequent redefinitions. Ordinary `-E` output keeps them raw;
  direct checker callers supplying that raw stream retain the conservative
  refusal for argument words that were macro names. Unsupported arguments,
  malformed forms and invalid placements are hard semantic-pragma errors.
  The unqualified `#pragma fenv_access` uses the unknown-pragma warning
  policy in the accepted Linux mode, as Clang 23 does. Its handler requires
  `-fms-extensions`, which the driver refuses. Use `#pragma STDC FENV_ACCESS`
  for runtime floating-environment semantics.
- **`#pragma clang section`** (`c_check_section_input.c`, #619), with which
  `main.c` puts the checker's own code in `.text.buster_checker` (#543): any
  number of `KIND = "name"`, KIND `bss`, `data`, `rodata`, `relro` or `text`,
  read as clang's `PragmaClangSectionHandler` reads them, adjacent ordinary
  strings joined, an empty name clearing its kind and a line of no settings
  doing nothing, with a setting before an error in its line kept. Its three
  errors are at clang's places with clang's messages: a kind that isn't one
  (also for anything after a complete setting), a missing `=`, and a name that
  isn't an ordinary string. Then the section type conflicts, as Sema's
  `UnifySection` takes a unit's uses of section names in order
  (`check_input_section_conflicts`, one pass after the walk over a unit's few
  section events): the settings, each function declaration's own `section`
  attribute and each definition of a variable with static storage, its
  attribute inherited from earlier declarations (a tentative one's at the
  unit's end, at its last tentative declaration, unless the unit defines it).
  Each section name has its first use's flags, and a use that gives it others
  is clang's conflict, at the use, with clang's message. Refused by name rather
  than guessed: a line with a name, where clang reads it macro-expanded (the
  first kind and every `=`), that was a macro's anywhere in the unit (the
  retained line keeps its tokens as written), and an error at the line's end,
  whose place the retained line doesn't keep. A setting applies to the
  definitions after it that have no section attribute of their own: text to
  the functions', the others by a static-storage variable's final section
  kind. The deferred reader leaves a unit with a setting to source order, and
  a lane refuses a body with a section attribute.
- **Named sections in `-c` and the direct path** (#619's part 2): a
  definition with a `section` attribute (the first within one declaration,
  or the latest earlier declaration's as clang passes it on), and a function a text setting places
  go in their sections (`CheckInputSections.placements`,
  `check_program_sections`). `-c`'s object has one section of each name and
  flags (`ObjectOptions.atom_sections`), of the kind LLVM gives a global with
  an explicit section: a function's text (`ax`); a variable's data (`aw`),
  read-only (`a`), read-only after relocation (`aw`) or thread-local data
  (`awT`), by its class, or `.bss`'s and `.tbss`'s zero fill (`NOBITS`) by
  its name's family. A zero-filled variable in another name's section is data,
  zeros in a section with bytes, as LLVM puts no global with an explicit
  section in BSS, and a function in another kind's section (`.data`) is a
  section of that name of its own, `ax`, as clang makes it. The direct path's
  executable merges each into its class's output section, as a linker's
  script merges `.text.*` and `.data.*`. Refused by name: conservatively, any
  emitted static-storage definition while any bss/data/rodata/relro setting
  is active ("a definition under active '#pragma clang section' settings"),
  until the emission selects a variable's final section kind, which clang
  decides through the pragmas' attributes its tentative definitions pass on:
  a bss-only or rodata-only setting still refuses `int x = 1`, although clang
  leaves it in ordinary `.data`, while a text-only setting and cleared object
  settings leave static objects alone; a definition's own or inherited
  `section` attribute takes it out of the refusal, into its section. Also
  refused by name: a name LLVM gives another kind than the definition's (an initialized variable in a `.bss`, which clang's code
  generation refuses too, or one not thread-local in a `.tdata`); and a name
  ELF gives another type (`.note*`, the init and fini arrays'). The entire
  `.llvm.*` family is conservatively refused too, including ordinary
  PROGBITS names such as `.llvm.custom`; `.llvm_custom` is outside this
  reserved family. The diagnostic names the unsupported ELF type or reserved
  family without claiming every reserved name has another ELF type.
  An empty section attribute clears inherited placement and overrides an
  active pragma setting while preserving the default section class, including
  BSS/TBSS zero fill. Its reader event still participates in section conflicts.
- **File-scope `asm`** (#619's part 2), which `main.c` makes
  `.text.buster_checker` with: its statements' templates in source order, a
  line each, assembled by `assembler.c` (`asm_assemble`), whose sections'
  atoms join the unit's link before its functions', as clang emits a module's
  `asm` first (`check_program_asm_plan_file`), each in its section in `-c`'s
  object; its labels and references join the unit's symbols as a template's do
  (#1848), and its sections are any the assembler makes (notes, init and fini
  arrays, merged strings, custom flags, CFI's frames). Refused by name: a
  template the assembler doesn't take, and section groups.
- **`#pragma clang optimize`** (`c_check_optimize_input.c`), whose `off` and
  `on` `main.c` puts around the checker unit's own tests (#681): one
  identifier, `on` or `off`, read as clang's `PragmaOptimizeHandler` reads it,
  with its two errors at clang's places with clang's messages (any other
  argument, and a token after it). An `off` makes the function definitions
  after it `optnone` and `noinline` until an `on`, which changes nothing a
  Tier 0 compile emits, so the reader only checks the line. Refused by name: an
  argument or a token after it that was a macro's name in the unit (clang reads
  both expanded), and a missing argument, an error at the line's end. The 9700X
  job checks each translation unit `build.sh` compiles, `main.c` among them,
  with `build.sh`'s flags (#619's part 3), so that a construct the build's
  sources take up and the checker doesn't fails the job rather than waiting
  for the next measurement of `main.c`.
- **`no_builtin`**, on a function's definition only: on another declaration of
  a function it is an error, and on anything else it is ignored, as clang has
  it.
- **`unsigned _BitInt(1)`:** the declaration's specifiers read a `_BitInt`'s
  width alone, so that their own `unsigned` decides its signedness. A typedef
  of an unsigned `_BitInt` stays unsigned where it is used, which main didn't
  have (`U8` of `typedef unsigned _BitInt(8) U8` was signed).
- **An array of unknown size with struct elements:** the reader infers its size
  before expressions have types, so whether an element initializes a whole
  struct, or starts brace elision, comes from the tree: the element's root's
  type, from an identifier's declaration, a compound literal's type, a `?:`'s
  third operand, a parenthesized expression or a call of a function by its name
  (of an intrinsic, by the prototype the reader keeps at the call's `)`), and a
  subscript's, a member's and an indirection's of these (`x[1]`,
  `bind->runtime->libc.soname`, `*p`), each step applied to its base's type from
  the innermost out. It came from the last typed token in the element, and so a
  compound literal with a member's value, `(struct L){ .target = n }`, read as
  `n`'s type, and `{ x[1], x[2] }` of structs as a struct's first two members:
  one element where clang has two. An intrinsic's call, which no declaration
  types, still read so: `{ __builtin_ia32_compressqi512_mask(a, w, m), a }` of
  byte vectors as its mask's scalar, one element where clang has two.
- **Compound literals in a static object's initializer**, in a function too:
  clang folds a block-scope compound literal's value, an automatic object's, as
  GNU C does, into a `static` array's element or a `static` struct, where its
  body's parts are constant (`(S8){ "a", 1 }`, not `(S8){ "a", n }`). The
  initializer copies the literal's parts, as it did only for a file-scope
  literal's. A scalar compound literal's value (`static int x = (int){ 1 };`,
  `(long){ 5 } + 1`, `(int){ (int){ 3 } }`), at file scope too, is its body's one
  value converted to its type, where that is a number or a static object's
  address constant (#1447, `check_init_scalar_literal`): the constant cache that
  folds a vector literal's lanes folds it, and the operations around it, a
  conditional's among them (`(int){ 5 } ? 6 : 7`). In a bound, an enumerator
  and the constant reader's other walks (#1447), a scalar literal whose one
  item is numbers, characters, operators and parentheses alone folds as a cast
  of it, but no integer constant expression (`check_constant_compound_folds`):
  `char e[(int){ 2 }];` is a constant array with clang's "variable length array
  folded to constant array as an extension", at file scope, in a member and in
  a block's `static` object, and `enum { E = (int){ 1 } }` is 1. One an `&`
  takes stays its object. Left: `_Static_assert((int){ 1 }, "")`, which clang
  folds quietly, is still "expression is not an integer constant expression".

Declaration specifiers and comma-separated declarators are elaborated in source
order, including the scope of a name in its own initializer. The reader
records declare/use/restore events as it elaborates, and each use's event
retains the declaration it sees; `c_check_binding.c` publishes those answers
directly, and `--scalar` replays the events independently
([the binding handoff](c-check-bindings.md), #336). The events are also the
history for source-dependent semantic environments. Every ordinary use retains
its exact declaration identity. It also retains the
composite declaration types, canonical object identities, linkage, storage
duration, actual and tentative definitions, and each selected initializer node.
Linked declarations also retain translation-unit identity across closed blocks.
An out-of-scope prototype constrains later declarations of the same entity but
does not silently change the type visible to an unprototyped call. A
declarator's parameter list is a scope of its own, a level deeper than the one
around it (C11 6.2.1p4): its parameters are declared in it, and so are the tags
and enumeration constants it declares, those of a struct, union or enum defined
in a parameter's specifiers or in an array bound among them, each read where it
is, in source order. A prototype's end at its `)`. A definition's are declared
again at its body's opening, as clang has them, since the body's scope is
theirs too: in `int f(struct T *p) { struct T *q = p; ... }` the body's
`struct T` is the parameter's (#435), and in
`int f(enum E { K = 7 } e) { return K; }` its `K` the list's (#438). Before
C23, a list's second definition of a tag only warns in clang, which makes it a
tag of its own that its name doesn't name, and the checker does the same. Under
C23 it is a redefinition as in any other scope (#1869): a record's is compared
with the first and declares its type when compatible, or gives clang's
"incompatible definitions" error; an enum's keeps the C23 enum limit below. A tag that a
declarator's array bound names or defines outside a parameter list is the
declarator's scope's: `int a[sizeof(struct V { int x; })]; struct V v;` is
valid. A prototype in a constant expression's type name (a typeof's, a
builtin's operand, a bound's `sizeof`) is a scope a level deeper as well, which
ends its tags and enumeration constants at its `)`, in the evaluator's binding
walks (`c_check_constant.c`): `typeof(int (*)(struct Z *))`'s `struct Z` isn't
a later one's (#437), and in `int f(int K, int a[sizeof(void (*)(enum E { K }))])`
the inner `K` is that prototype's alone. A bound's definitions are read beside
its statement expressions, before or after them; one inside a statement
expression is refused with its bound, as a statement expression that holds a
declaration is ("Variable-length arrays" below). A definition in a `typeof` is
still refused, there as anywhere.

Type-name prototypes check duplicate parameter names with a separate digital
search tree per prototype (`check_constant_parameter_unique`, #604). A root
and two child indices per existing parameter record consume interned 32-bit
names directly, low bit first. Each unequal record fixes another bit, bounding
an insertion by 32 unequal comparisons, independent of the parameter count.
Zero names are skipped. Nested and sibling prototypes have independent roots;
the tree is private to one `check_constant_type` scratch lifetime. It adds no
allocation or identifier-table initialization. Parameter records grow from
12 to 20 bytes; the scratch reservation grows by eight bytes per token of the
bounded type slice. Reordering the part record keeps it at 40 bytes.

The check stays after parameter type adjustment and before reading the next
parameter, preserving the existing failure order and binding activation.
The source-order insertions and their dependent bit decisions use scalar
loads; the full scalar/vector source readers exercise the same membership
rule. The affected path is declaration-time evaluation, for example
`enum { N=sizeof(int (*)(int p0,int p1)) };`; a body-level `return sizeof(...)`
uses the ordinary declarator reader and is a separate, already-linear control.

On the locked 9700X against main `92c025e7`, a 4,096-name prototype takes
11.27–11.41 ms as a whole `-fsyntax-only` process before this change and
2.01–2.09 ms afterward (5.46–5.61 times faster); following 100 ms of scalar
work it improves 5.15–5.33 times. Instructions fall 77.94%. SQLite's timing
stays within the identical copies' variation; a possible few-percent cost on
many small prototypes is unresolved. The 4,096-unnamed-parameter control
uses about 2 MiB more peak RSS, while the named control stays near 31 MiB.
[The pinned qualification and measurements](https://github.com/buster14a/buster-performance/tree/3e6cd7e2a88ae93b11797050f9fb8806be18ecc9/docs/measurements/prototype-parameter-names-604-2026-10-27)
include all named/unnamed sizes, source/binary identities, memory costs,
the passing target suites and the excluded setup run. These are measurements
of this reader change and its recorded controls, not whole-compiler acceptance.

Overloadable declarations retain source-position candidate sets. A call selects
its candidate from the checked argument types; a function address selects from
the destination function-pointer type. Initializers, casts, assignments,
callbacks and returns retain that selected object and conversion on the use
edge, without rewriting a shared identifier leaf. Candidate availability is
checked at the use's source position. Function calling conventions participate
in canonical type identity and compatibility; checking their types does not
claim backend support for emitting each convention.

Source-point type history prevents a later tag definition or declaration from
retroactively changing an earlier expression. Runtime VLA bounds have stable
extent identities and explicit captures; an unevaluated operand suppresses
runtime work except where the language requires VLA evaluation. Builtin calls
retain their exact formal conversions, evaluated-operand masks and runtime
requirements. An unsupported declaration or unresolved identifier never
acquires a guessed `int` type. Full-corpus and target qualification are tracked
separately from the existence of these interfaces.

Direct semantic actions keep strict integer folding unless the caller selects
`BUSTER_CHECK_EXTENSION_GNU_CONSTANT`. Source enables that explicit policy in
every C mode, as clang folds GNU's way in `-std=c99` and `-std=c17` too (only
`-pedantic` warns): wrapped overflow and Clang-compatible shift extensions
retain a warning witness and clear strict ICE eligibility. The other GNU
extensions clang accepts in every C mode (zero-length arrays, `sizeof` and
`__alignof__` of `void` and of functions, arithmetic on `void *` and function
pointers, a conditional with one void side, `return` of a void expression from
a void function, a cast of a record to its own type, `?:` without its middle
operand, designator ranges, static initialization of flexible array members,
`__extension__`) are accepted in every mode as well. Lexical ICE-form provenance remains distinct
from an arbitrary folded value, so `__builtin_choose_expr` does not accidentally
admit comma expressions or computed floating casts. Declaration-time warnings
are retained by token and merged with graph warnings before rendering. Warning
identity and default severity remain separate: an enum's GNU folding extension
is normally quiet, while the corresponding folded file-scope array bound warns.
As clang's do, a declarator's array bound that is no ICE but folds, wherever its
array can't be variable (file scope, a member, a typedef at file scope, a block's
`static` or `extern` object), warns "variable length array folded to constant
array as an extension" once at the declarator's name, whatever folded it; a
bit-field width, an enumerator's value, a static assertion, a case label or a
designator folds quietly, and under `-Wgnu-folding-constant` with `-Werror` or
`-pedantic` reads "expression is not an integer constant expression; folding it
to a constant is a GNU extension" at the expression's first token
(`c_check_vla_fold_driver_test.c`). A block's automatic array keeps such a bound
variable, as does a `sizeof`'s type name anywhere.
A variable-length array or variably modified type where C allows none takes
clang's error at the declarator's name (`check_input_vla_place`,
`check_input_vla_declaration`):
- at file scope, "variable length array declaration not allowed at file
  scope", or "variably modified type declaration …" where the declared type
  is no array (`int (*p)[n]`);
- a block's `extern` object, "… cannot have 'extern' linkage", by the same
  two kinds;
- a block's `static` array, "variable length array declaration cannot have
  'static' storage duration";
- a member, "fields must have a constant size: 'variable length array in
  structure' extension will never be supported".

A block's automatic variable-length array with an initializer other than `{}`
is clang's "variable-sized object may not be initialized"
(`CHECK_INPUT_VLA_INITIALIZER`): at the outermost bound's first token for a
list, as `InitListChecker` places it, and at the name for any other
initializer, as `InitializationSequence` does. It recovers: the declaration
stays valid, and its initializer is skipped unchecked, as clang checks it no
further.

A `sizeof` of a variable type name in a file-scope initializer
(`int s = 1 + sizeof(int[n]);`) is "initializer element is not a compile-time
constant", at the start of the initializer's element that holds it.
A cast's or an `_Alignof`'s variable type name at file scope (`(int(*)[n])0`,
`_Alignof(int[n])`) makes a variable array type whose bound is never evaluated,
as clang folds both (#1852): its `CheckInputVla` is not evaluated and captures
no bound.

These errors recover, as clang marks the declaration invalid and goes on
(#1447): a file-scope declarator's like a negative bound's
(`check_input_declarator_recover`), but keeping its group's later declarators,
as Sema's `CheckVariableDeclarationType` gives it after the declarator
(`CheckDeclaratorRun.parameter_invalid`); a block's `static` or `extern` one's
at the declaration, a stand-in `int` (one with an initializer still stops).

In a body, a cast to a variably modified type (`(int (*)[n])v`, in `sizeof`
too; #2321, #1852) evaluates its type name's bounds, outermost first through
pointers and arrays, before its operand, as clang's `EmitVariablyModifiedType`
does: the graph's cast follows a comma of the captured bounds
(`check_input_vla_cast_bounds`), which no -Wunused-value warns of, and a
`sizeof` of the cast's value reads the extent stored then rather than
evaluating the bound again. In an unevaluated `sizeof` operand the bound isn't
evaluated.

Differences left: a member's error still stops the reader, where clang marks
the field and its record invalid. A warning inside a `sizeof` or `typeof`
operand of variable array type (`sizeof(*(int (*)[(1, n)])v)`'s comma) is given
once, where clang, which transforms that operand again as potentially
evaluated, gives it twice.
A skipped VLA initializer's undeclared names and expression warnings
(`{q}`, `"y" + 1`) aren't reported; clang reports them, and an undeclared name
in a list makes the list invalid, with no "may not be initialized" error.
Immutable source-position diagnostic pragmas apply push/pop and exact warning
group policy to these witnesses. Promoted warnings participate in first-error
ordering and prevent object publication; `-w` suppresses them as Clang does.

Preprocessor-completed `#pragma message`, `#pragma GCC warning`, `#pragma GCC
error`, `#ident` and `#sccs` records are accepted without another semantic action
or diagnostic (#662). The checker recognizes their bounded canonical shapes;
unrelated or malformed records retain their own handler/refusal. A GCC error
still rejects the unit during preprocessing. `#sccs` is canonicalized to `#ident`.
The strings from `#ident` and `#sccs` are deliberately omitted from object
`.comment` sections; they affect neither program behavior nor source acceptance.

The action layer is broader than that adapter. It can check enum promotions,
bit-field restrictions, compatible record assignment, nested const members,
pointer qualification, function calls and their parameter conversions.
Anonymous member lookup and aggregate
modifiability use bounded iterative walks, including inherited qualification
on anonymous members.

Before record layout, `check_input_tag_definition` packs that record's direct
members for `check_input_record_names` (`c_check_record_input.c`). Direct names
have already passed the reader's generation-stamped duplicate check. While
packing, the reader records whether an unnamed non-bit-field member exists and
skips namespace allocation/validation entirely when none does. An anonymous
nonrecord still enters the defensive helper and fails as before. Named fields
introduce only their own name, even when their type contains another record's
definition. The temporary hash table starts from the direct member count and
grows geometrically at half occupancy as anonymous fields expose more names.
Its clearing and rebuilding therefore follow the visited namespace, rather
than the entire nested token span (#614). The explicit expansion stack starts
with one local frame and grows only with anonymous depth; the token span still
bounds that depth. Both scratch structures belong to this validation call and
restore its arena mark on success, duplicate or allocation failure. Duplicate
diagnostics retain the later containing direct member; layout receives the
same packed members. `test-checker` exercises colliding identifier IDs, repeated
table/stack growth, every scratch byte limit, named versus anonymous nesting,
and Clang-checked source layouts and first-error lines on both parser paths.

`CheckStream` holds expression results. `CheckProgram` adds checked control
bracketing, body references and shared object/function identities without
rechecking expressions or rebinding identifiers. The source body-extraction
benchmark verifies that an extracted body's stream equals its original slice
before comparing scalar and vector schedules. The corpora and their semantic
mutants are compared with clang in "Corpus comparison and semantic mutations"
below; 9700X measurements at the qualified revision are still required, and
historical evidence below applies only to its named checkpoints.

Unnamed parameters in definitions (#792) retain their positions in the complete
function signature. `CheckInput.parameters` stores the first named parameter at
a body-opening token and a parameter's full-signature ordinal at its name token;
these token roles are disjoint. `parameter_next` still links only named tokens,
so binding, prototype-scope restoration and deferred body readers never see an
invented identifier. The ordinal reuses existing initialized storage: no new
per-token column, allocation or scan is added, including for ordinary all-named
functions. K&R identifier lists publish their source-order ordinals too.

Lowering walks the contiguous type signature and consumes a named token only at
its ordinal. Every parameter gets an incoming ABI local, including unnamed
integer, SSE, aggregate and stack-passed parameters; only named parameters enter
the source-object map. This preserves the contiguous incoming-local prefix,
hidden result pointer, register-exhaustion rollback and fixed variadic offsets.
Named parameters keep their qualifiers, alignment and K&R entry conversions.
`test-checker` covers these paths against Clang, including a Clang-built caller
of a buster definition. This is a correctness change, not a throughput claim.

K&R function definitions (C17 6.9.1) are read as clang reads them, as #154's
decision has it. The declarator records an identifier list's names as the
definition's parameters, in the list's order, without a prototype
(`CheckDeclaratorPart.identifier_list`), and its scan ends before the
declaration list, since no specifier follows a group in a declarator. The
declaration list (`check_input_kr_declarations`) gives each name its type,
adjusted as a parameter's, under C17 6.9.1p6's constraints:
- no storage class but `register`, no function specifier or typedef;
- only the list's names, each declared once, and none with an initializer;
- every listed name declared in C99 and later; a name left undeclared in C89
  receives `int`.

The list's struct, union and enum definitions use the ordinary tag reader.
Their tags and enumerators remain visible throughout the declaration list,
then their header scope ends before the function body, as Clang treats an
old-style definition. The completed parameter types remain; an outer tag or
ordinary name becomes visible again in the body and following file scope.
These declarations are not republished into the body as prototype-definition
tags are. The extra visibility/redefinition warning sequence is not reproduced.
A variably modified parameter's bounds (`int f(n, a) int n; int a[n][n]; {}`)
are a prototype parameter's (#1447): the list's declarators read them as a
parameter's, each bound's record names the listed parameter, and the function's
entry evaluates them; the walk takes their tokens, the list's only ones it
takes. A `[*]` bound there stays an error, as clang's is. An identifier list
outside a definition is refused, as C17 6.7.6.3p3 has it; C23 rejects identifier lists.
Parenthesized `_Atomic(T)` starts the declaration list at the same boundary
as the qualifier form `_Atomic T`. Its type name uses the ordinary evaluator,
including typedefs and nested type queries; invalid atomic inner types retain
the ordinary constraint checks. Statement-expression type specifiers that
need the ordered declaration reader remain outside this list reader.

The definition retains a prototype of its parameters' default argument
promotions for compatibility checking and ABI lowering, as clang types it
(`float f(a) float a; { ... }` has that internal signature `float (double)`).
As in clang, `_Atomic` parameters retain their declared ABI types instead:
`_Atomic float` is not promoted to double, and an atomic aggregate keeps its
storage padding. Known calls retain these ABI types as well as their value conversions.
At expression uses its designator instead has a no-prototype type. Known
direct calls, including an addressed function, retain the definition's
argument conversions, but a wrong argument count is a warning rather than
a hard constraint failure. A later prototype or a pointer declared with a
prototype still checks its own argument count. Other no-prototype calls use
default argument promotions. The call graph carries the known signature and
source token explicitly; the type table does not grow a new function kind.

Argument counts are worded and placed as clang's (#1860, #1921, #1994,
`check_driver_call_count`). A call with a prototype says "too few/too many
arguments to function call, expected [at least] N, have M", too many at the
first extra argument, with "'h' declared here" where the callee is a
function's name (through parentheses and `&`); where that declaration names
its only parameter, clang's "single argument 'a' was not specified", "at
least argument 'a' must be specified" and "expected single argument 'a', have
M arguments". The constant reader's calls (a `typeof`'s, an array bound's, an
unevaluated operand's) say the same (`check_constant_failure_error`). A
builtin's check carries the count clang names in its diagnostic's operand
(`check_action_diagnose_count`): exact, "at least" where it has no maximum
(shufflevector, the legacy `__sync` builtins, prefetch's variadic prototype),
and "at most" for too many where its count is a range, at the call's `)` as
clang's `checkArgCountRange` puts it; a scoped atomic's count includes its
scope. The unary, overflow and complex part builtins, whose graph takes only
their count, are the reader's `CHECK_INPUT_BUILTIN_ARGUMENT_COUNT`. A K&R call's
warnings are clang's places: passing arguments without a prototype at the
call's `(`, naming a direct callee (`'knr'`) and "a function" otherwise; a
known definition's too few or too many at the `)`, with `[-Werror]` under
`-Werror`. Differences left: the diagnostics of an argument's own
subexpressions (a comma's `-Wunused-value`) are dropped with a call's count
error, and after a declaration constant's error in a body, the body's later
errors are not given.

The producer receives all incoming parameters in their promoted ABI types,
then converts changed ones into separate locals of their declared types.
That two-pass layout keeps the incoming ABI-local prefix contiguous, including
a hidden aggregate-result pointer, while body accesses and address-taking
use the declared local's type, alignment and volatile semantics. The reader's
`kr_parameter_count` bounds extra locals and entry instructions, including
unused parameters and `_Bool` truncation. Ordinary units reserve none of
this extra room. No throughput improvement is claimed by this correctness fix.

Definitions and no-prototype calls carry `-Wdeprecated-non-prototype`;
known direct calls with a wrong count have a separate arity warning.
Source-ordered diagnostic pragmas control them, including promotion to errors,
and quiet mode suppresses warnings. Calls in source-only `sizeof`/`typeof`
queries publish sparse source diagnostics; their deferred-lane path retries
in source order rather than mutating another lane's diagnostic storage. Executable oracle fixtures cover signed
and unsigned narrowing, float rounding, register and stack arguments, aligned
and volatile locals, hidden result pointers and prototype transitions. Source
fixtures check C89, C99, C17 and C23 against the matching Clang language mode.

Where clang takes a construct C's constraints exclude, the checker takes it
too:
- a declaration of no declarator, which declares its tag or nothing, at file
  scope, in a block and as a member, whatever its storage class, typedef,
  qualifiers and attributes (clang warns, `-Wmissing-declarations`, and this
  doesn't), though a function specifier there is an error;
- a union's flexible array members, anywhere among its members, each of size 0
  and its element's alignment (GNU's extension; `type_complete_record` takes
  them);
- an incomplete type, `void`'s too, in a `_Generic` association, which matches
  no control (C2y's extension);
- an association of array type, or of a qualified type (`const`, `volatile`,
  `restrict`, an address space, objc_gc's `__weak` or `__strong`, not
  `_Atomic`), which the controlling expression's lvalue conversion never
  selects: clang's `-Wunreachable-code-generic-assoc` (on by default, under
  `-Wunreachable-code`, #1447, `check_generic_unreachable`), "due to lvalue
  conversion of the controlling expression, association of type 'CI' (aka
  'const int') will never be selected because it is qualified", at the type
  name's first token past its qualifiers, once however often it is read
  (C2y's type operand, which clang doesn't warn for, the parser doesn't take);
- a static assertion of a floating constant, converted to `_Bool` as clang
  converts it (`check_graph_floating_assertion`).

`inline` and the `_Noreturn` keyword declare only functions, as clang has it.
Each is refused at its keyword on an object, a typedef (`_Noreturn` on a
function's type too) or a member, and so is an `inline main`. `CheckInputAttribute.keyword`
tells the keyword's record from a `noreturn` attribute's, which clang only
warns about there.

## Entry points and storage ownership

The implementation is included in the compiler's existing C translation units.
Its private entry points are:

```c
bool check_input(CheckInput* input, const Parser* parser, Preprocessor* pp,
                 TypeTable* types, Arena* output, Arena* scratch);

bool check_graph_initialize(CheckGraph* graph, Arena* arena,
                            uint32_t capacity, uint32_t argument_capacity);
uint32_t check_graph_add(CheckGraph* graph, CheckOp op, uint32_t token,
                         uint32_t operand0, uint32_t operand1, uint32_t operand2);
bool check_graph_run(TypeTable* types, const CheckGraph* graph,
                     Arena* output, Arena* scratch,
                     const CheckOptions* options, CheckStream* stream);

void check_action_scalar(TypeTable* types, const CheckAction* action,
                          CheckActionResult* result);
void check_actions(TypeTable* types, const CheckAction* actions,
                    CheckActionResult* results, uint32_t count);
```

The parser must retain its complete tree. The reader derives shared traversal
metadata when `tree_only` omits the parser's postorder; optional parser size
metadata is not required. Parser tokens and the preprocessor's interned identifiers remain alive
while adapting and diagnosing that source. The adapter puts its graph,
declaration identities and reports in the supplied output arena, and releases
its temporary scratch allocations before returning. `CheckInput.resource`
distinguishes capacity failure from the adapter's ordinary diagnostics. A false
adapter result is not a usable, successfully checked graph.

`CheckInput.binding_event_count` and `binding_statistics` describe the
reader's events in every binding mode; only the batch sort counts walks. Its declaration, type, object, linkage, definition, tentative
definition and initializer-node columns all have `token_count` entries. Only
declaration tokens (`declarations[token] == token`) define objects; uses map to
those tokens. An object identity is nonzero and stable across compatible
redeclarations. A definition flag belongs to that particular declaration;
seeing an earlier definition does not turn a later `extern` into a definition.
Event count includes restores, and identifier/declaration statistics count
events, including parameter reactivation, rather than distinct source tokens.
The independent scalar resolver has zero candidate-walk/fallback counters.

`check_graph_initialize` allocates all standard graph columns from an arena and
sets the logical count to zero. `check_graph_add` appends one node if capacity,
operation and earlier-operand checks permit it; otherwise it returns
`BUSTER_CHECK_NONE` without incrementing the count. Initialization rolls back
its allocations if it fails. Both node and argument capacities are bounded by
`1 << 30`; allocation limits normally intervene much earlier.

Graph storage, checker output and scratch storage belong to the caller. They
must be disjoint live regions; in particular `output` and `scratch` must be
different arenas. Type-table storage must also stay alive and writable. The
checker may intern derived types into that table, so concurrent invocations
must not mutate the same table without an external ownership arrangement.

Every column is a conventional C array with its element type's natural
alignment and `sizeof(element)` stride. There is no extra SIMD alignment,
sentinel or readable-padding requirement. Logical count, allocated capacity and
arena capacity are distinct: spare capacity does not authorize reading past a
logical tail. Graph inputs remain immutable during a run. Direct action inputs,
results and argument-conversion outputs must occupy separate live storage;
their arrays need no padding and are accessed only within their counts.

### Input graph

| Column | Logical extent and meaning |
| --- | --- |
| `ops` | `count` `CheckOp` entries; one operation per stable node ID. |
| `operand0`, `operand1`, `operand2` | `count` 32-bit entries each. Used operands are earlier node IDs; absent operands are `BUSTER_CHECK_NONE`. Arity is checked. |
| `tokens` | Optional `count` 32-bit source-token identities. If absent, diagnostics use node IDs. |
| `leaf_records`, `leaf_payloads`, `leaf_slots` | `leaf_count` 32-byte `CheckGraphLeaf` records, one per `VALUE` node in node order, and `count` 32-bit slots: a `VALUE` node's is its record's index, the next after the previous `VALUE` node's, and every other node's is `BUSTER_CHECK_NONE`. Only `VALUE` reads a record, so other nodes take none (#572). A record holds the value's type, category, flags, constant kind, object, overload set and its integer's low 64 bits; a value with more (`check_graph_leaf_rest`: a wider integer, a floating payload, an offset, a bit-field's place, a fault) also takes the next whole `CheckValue` row of `leaf_payloads`, at most one a leaf (#628). A hand-built graph may hold the leaves whole in `dense_leaves` instead. `check_graph_leaf_value` and its field accessors read either form, and `check_graph_set_leaf` (a leaf's first write) and `check_graph_update_leaf` (a write over one, in the row it has) write it. Both validators refuse a record whose row is past the graph's rows or holds another value than the record in the fields both hold, rows past their room, and rows without their column (`check_graph_leaf_valid_at`, `check_graph_validate_lists`). |
| `types`, `members` | Optional `count` columns containing a requested destination/query `TypeId` or member identifier. A missing column supplies zero. |
| `contexts` | Optional `count` bytes. The documented bits request an ICE, a true static assertion, or a static initializer. Unknown bits are invalid. |
| `argument_offsets`, `argument_counts` | Optional `count` 32-bit columns. A call owns a bounded slice of `argument_nodes`; its callee is `operand0`. |
| `argument_nodes` | `argument_count` 32-bit earlier node IDs, within `argument_capacity`. Nonempty slices belong only to calls. |

Nonempty call-argument slices are append-only in postorder, disjoint and
collectively cover `argument_count` entries. Each such call starts at the end
of the previous nonempty slice. This gives every argument-conversion record a
single owner. The source adapter produces exactly that layout. Arguments are
already checked expressions, not syntax that the call action must reparse.
`check_graph_run` validates topology, arity, slice bounds, operation values and
context bits before reading child results for graphs supplied by other
producers. The source reader publishes immutable graph facts (#674): one
purity byte per node, maximum call width, overflow-effect and builtin counts,
and argument/sideband consumption totals, including the compact VALUE leaf
count. `check_input_add` completes the
preceding node after its caller filled the leaf, requested type and call data,
before a later node can consume its purity. The reader flushes its final node
and publishes the sideband totals before returning, including diagnostic-only
prefixes. This costs constant work per node while its columns are still hot;
it avoids a later pass over those columns. Both reader paths use the same
protocol. Required dependency order makes each node's purity depend only on
its operator, requested type and at most three existing purity bytes.

Only this reader publishes `recorded_ready`; `check_graph_initialize` and
other graph producers leave it false. `check_graph_add` invalidates a published
summary, so a subsequent run falls back to validation. A caller that directly
changes a published graph must clear `recorded_ready` first. All columns and
sideband lists remain immutable while a summary is published. The retained
purity column has exactly `capacity` readable bytes and lives with the graph;
only `count` initialized bytes are copied to the scheduler's mutable marks.
Allocation failure follows the reader's ordinary resource failure path.

`CheckOptions.verify`, selected by the C driver's `--verify`, validates these
graphs too and checks every counter and purity byte against the reader's
facts. Mismatches return `CHECK_STATUS_INVALID_GRAPH` and roll back output;
a purity mismatch names its first node. Generated graphs and fixtures retain
the ordinary scalar/vector validators as independent oracles. `test-checker`
checks both recorded schedules with and without verification on its generated
graphs, compares both validators with each source fixture's recorded facts,
and mutates every counter and purity byte to check rejection and rollback.

[The frozen #674 qualification](https://github.com/buster14a/buster-performance/tree/4df43f7d7a685133b6beade655d9e9a306947081/docs/measurements/unclaimed-perf-2026-10-28/issue-674/target/README.md)
compares `bc1b4861` with `7274e1bc`. Eight physical workers compile the pinned
SQLite corpus at 190.958 versus 194.163 MB/s (+1.68% pooled), with positive
unpaired ranges across actual checker-section placements and fresh executable
copies, balanced predecessor work and retained faults. The one-worker gain
remains unresolved against placement/copy variation. Later compact-leaf and
reader adaptations preserve the contract but need their own performance
qualification; those frozen timings are not measurements of a rebased head.

Empty graphs need no node columns;
an empty action batch may use null input and output pointers.

Counted auxiliary records retain builtin calls, overload candidates/selections
and statement-expression proof regions. A region owns a contiguous bounded
slice of `EVAL`, `BRANCH`, `CASE`, `JUMP` and `END` steps over earlier typed
nodes. The source adapter builds these steps from the existing parser tree and
checked expression roots. A `CASE` retains both the switch expression and case
operand so comparison uses the promoted switch type. Validation checks node
order, ownership, slice coverage and branch targets before interpreting a
region. Body extraction includes these dependencies and remaps every retained
node reference while preserving branch targets within each compacted slice.

Overload declaration lookup uses transient signature buckets keyed separately
by name and lexical scope, and by name and translation-unit linkage. Each
fingerprint preserves canonical parameter identity, including nominal tags and
nested function ABIs, while ignoring alignment sugar and the outer function's
return type and ABI. Append-only type-DAG fingerprints are computed once per
type. Eight contiguous bucket keys are compared together, with an exact scalar
twin; collisions still undergo the original signature and compatibility checks.

Validated redeclarations carry accumulated attributes, definition state and
canonical object identity. Identical type representatives of the same function
can then be replaced in the transient index; another function of the identical
type, an overload whose enable_if conditions differ (#1447), stays. Ordinary declarations recorded before a name becomes
overloaded must first have their complete history checked. Merely compatible
return types cannot end that history walk: compatibility of old-style function
pointers is not transitive. The immutable source candidate chains and captured
call heads remain intact, so declarations in later or sibling scopes cannot
change an earlier call. Declaration-only scaling is measured separately from
call-site overload resolution (#612).

Function and object declarations after a definition do not add `used`, `retain`, `gnu_inline`, constructor, destructor,
or standard `noreturn` to that definition; `weak` is also dropped. The checker warns at a newly added attribute and
notes the definition. A `gnu_inline` declaration without `inline` instead gets clang's ignored-attribute warning without that
note. The checker builds the inherited-attribute mask on the first such declaration and updates it in source order. A
deferred body containing one of these attributes conservatively falls back to source-order checking, so its declaration
state cannot race another body's token columns. `test-checker-deferred` compares the fallback with the scalar and vector
readers.

### Typed output

`CheckStream.count` matches the input graph's count. Operators, source tokens
and operand edges stay in `CheckGraph`; the stream adds node-indexed semantic
columns without renumbering or expanding the graph:

- Each node's value (`CheckValue`: its `TypeId`, value category, constant
  representation, flags, optional object/addend, bit width and deferred
  evaluation fault) is in columns (#628). `value_types`, `value_flags`,
  `value_objects`, `value_categories` and `value_constants` hold the fields
  that most readers read alone. A node whose value has more (a constant's
  payload, an address constant's offset, a bit-field's place, an overload set,
  a fault) also has its whole value as a row of `payloads`, its row's index
  plus one in `payload_rows`, 0 for none. On `sqlite3.c` 12% of the nodes have
  a row, where 36% have an object. `check_stream_value` gives a node's whole
  value, and `check_stream_type`, `check_stream_flags`,
  `check_stream_category`, `check_stream_constant_kind` and
  `check_stream_object` its fields. A hand-built stream may hold a whole value
  per node in `dense_values` instead, which the accessors read the same way.
- `conversions` contains three operand records and a separate store record per
  node. Each records the
  destination type and the applicable lvalue, array/function decay, promotion,
  arithmetic, assignment, explicit-cast or null-pointer conversion flags.
- `argument_conversions` contains one record for each call-argument slot,
  including prototype conversions and default argument promotions.
- `member_offsets` stores a `MEMBER` or `ARROW` node's member's offset in bits
  from its record's start, through anonymous members, as the action's lookup
  found it (`docs/c-types.md`, "Member lookup"), and 0 for every other node:
  the lowering takes its member accesses' places from it rather than looking
  the members up again (#591).
- `diagnostics` stores the node's constraint diagnostic. `reports` is the
  compact diagnostic list, sorted stably by source token; `report_count` is its
  logical length and never exceeds the node count.

`CheckCategory` distinguishes rvalues, lvalues and function designators.
`TypeId` retains qualifiers and full semantic type identity. Conversion records
describe the conversion sequence and destination; they are not backend machine
instructions. The action result also carries a diagnostic operand index for
direct callers. The stream currently reports the expression node as its
diagnostic source location.

For compound assignment and increment/decrement, `operand0` is the sole graph
edge to the destination. Its conversion describes the read/promotion needed for
the calculation; `store` describes conversion back to the destination type.
The node remains a side effect and is not an ICE. This metadata does not by
itself implement runtime ordering or the old/new value choice in a backend.

`check_graph_run` returning true means that checking completed. It can still
produce semantic errors, so callers must inspect `report_count`.
`CHECK_STATUS_INVALID_GRAPH` means the producer violated the graph contract;
`CHECK_STATUS_RESOURCE` means output, scratch or type capacity was exhausted.
On either API failure the output arena is restored and the stream's pointers
and logical count are cleared; `status` and `failed_node` remain available.
Scratch is restored on every return. Type interning is append-only and is not
rolled back with the output arena: a failed run may leave valid, unused derived
types in the caller's table.

## Dependency scheduling and vector actions

Parser depths describe brackets, not expression readiness. In `a + b + c`,
the second addition depends on the first even when both tokens have the same
bracket depth. The checker computes heights from actual operand edges. A node
is ready only after all its operand values and types have been produced.

The scalar schedule processes graph nodes in postorder, one at a time. So
does the vector schedule by default, with its leaves and statements' discards
without their actions ("Node order or regions" below). With its regions
(`CheckOptions.regions`), it finds contiguous regions whose type behavior is
known to stay within builtin arithmetic types, and checks each region in
postorder batches (`check_graph_region_postorder`). From the first node not yet done, a batch
takes consecutive nodes that read none of each other, up to the batch size.
The alternative, the region's level order (`check_graph_region`,
`CheckOptions.level_order`), computes the nodes' relative dependency heights
and stably groups the ready nodes at each height. The benchmark measured both
on the same actions, and the postorder batches won ("Level order or
postorder" below). Operations that can create a pointer, decay an array or
function, or otherwise intern a derived type are postorder barriers. This
conservative rule preserves newly allocated `TypeId` numbers across fresh
scalar and vector type tables. Comparing both schedules in a table already
populated by one schedule would miss that requirement.

`CheckOptions.batch_size` is a logical batching limit: zero selects 16 and
values 1 through 16 are valid. A batch ends at a node that reads a member (in
level order, at every level) and at its region's end. The vector arithmetic
kernel uses eight 64-bit lanes, with per-lane 32-bit or 64-bit C arithmetic
semantics; the logical batch size is independent of that lane count. Eligible addition/subtraction, unary integer
operations, shifts, comparisons and bitwise operations use that kernel.
Irregular types and operations, integer multiplication/division/remainder,
128-bit arithmetic, pointer semantics, member lookup, calls and floating
folding use the scalar semantic action. Singleton frontiers execute directly
without packing a vector batch.

The vector path completes each leaf without an overload set, a `VALUE` that
reads no operand and interns no type (about 45% of `sqlite3.c`'s body nodes),
without running its action (`check_graph_leaf`, #530). The record is the one
the action gives: the leaf with its type as operand 0's conversion, or for an
invalid type, that type's diagnostic and a zero value. It writes the records in
node order: the barrier's leaves in the walk, a region's in its level pass,
which then schedules only the rest. A separate pass of leaves wrote each
output cache line twice. When validation is required (an external producer or
`--verify`), the vector path validates the graph 16 nodes at a time
(`check_graph_validate_vector`): the operators' classes come from one
`vpermi2b` table built from the scalar rules, and each argument slice's offset
from a prefix sum. A block with an invalid node, a `VARARGS` or x86 builtin
or a sideband entry goes to `check_graph_validate_node`, the scalar
validator's step. Most of that validation's time is the leaves' records it
reads, which since #572 lie one after another, as only `VALUE` nodes have one,
and since #628 are 32 bytes each, half a cache line, where a 96-byte value took
two. `test-checker` compares the two
validators on random graphs whole, with each rule broken, and with their
columns ending at `PROT_NONE` pages.

Both schedules call the same semantic-action contract. Scalar fixtures with
independently stated expected results and the external source oracle therefore
remain necessary: scalar/vector equality alone cannot validate shared C rules.
`CheckStatistics` reports nodes, levels, batches, actual scalar/vector node
counts, and arena bytes. The node counts describe the path executed, not merely
which operations could theoretically use SIMD: `vector_nodes` counts the vector
path's leaves and its batches' vector-eligible arithmetic. The schedule is currently
single-threaded; it does not establish useful multicore scaling.
`query_proof_nodes` counts visits across the three constant-query proof lanes;
`query_replay_nodes` counts the subsequent graph replay. These are additional
work, separate from the initial scalar/vector scheduling counts. Scratch bytes
are the peak live allocation above the caller's initial arena position,
including temporary overload, region and query allocations that are released
before return.

### Level order or postorder

#133 left open which schedule the vector path's regions should take, decided
by a benchmark on the same tables: their level order, or a vectorized walk in
postorder. `benchmark-checker-source` times both in one build, with the same
actions, type table and graph: its paths `regions` (the postorder batches,
`CheckOptions.regions`, which were `vector`, the vector path's default, until
#1050 made it node order: "Node order or regions" below) and `levels`
(`CheckOptions.level_order`), beside `scalar`. Before it times them, it checks
that each gives the scalar path's typed stream, and `test-checker` compares both
orders at every batch size with the scalar path. The measurements below, from
#133, name the postorder batches as they were then.

Level order computes each region node's height from its operands, counts the
heights, places the nodes by height, then checks each level in batches. The
postorder batches skip those passes. From the first node not yet done, a batch
takes the nodes after it until one reads a member, which in a tree's postorder
is most often the next node, since a parent follows its last child. So its
batches are smaller: 32,446 of them in `sqlite3.c`'s bodies, against level
order's 27,749 in 27,701 levels. A few more nodes take the scalar action alone
(54.17% of the vector path's nodes, against 53.95%), but the walk reads the
region's nodes once, in order.

On the 9700X, pinned to CPU 2 under the lock for each run: the frozen
`sqlite3.c`'s 2,608 bodies, 311,601 body nodes, as nanoseconds and cycles a
node, each a median of 21 passes, over three processes per build (main's over
both rounds' six):

| Build | Scalar | Level order | Postorder batches |
| --- | --- | --- | --- |
| main, `321f5976` | 45.35–46.23 ns, 249.5–255.3 | 34.27–35.00 ns, 189.8–194.0 | |
| level order the default, `0f26899e` | 44.60–44.89 ns, 243.8–246.5 | 33.56–33.91 ns, 184.9–187.0 | 31.60–31.84 ns, 173.4–175.3 |
| postorder the default, `fe35a8b5` | 44.08–45.30 ns, 242.9–247.8 | 33.29–33.98 ns, 184.1–186.8 | 31.47–32.15 ns, 174.0–176.5 |

Each path alone (`--only`, one process), and after 100 ms of scalar work
before each sample (`--after-scalar 100`), the state after lighter code in
which Zen 5's power cap slows heavy AVX-512 loops (#199):

| Build | Path | Alone | After scalar work |
| --- | --- | --- | --- |
| `0f26899e` | level order | 33.11 ns, 183.1 | 33.65 ns, 185.7 |
| `0f26899e` | postorder batches | 32.00 ns, 177.2 | 32.70 ns, 180.2 |
| `fe35a8b5` | level order | 35.13 ns, 194.6 | 33.57 ns, 185.4 |
| `fe35a8b5` | postorder batches | 32.97 ns, 182.6 | 32.58 ns, 180.2 |

The postorder batches took 5.3 to 6.2% less time a node than level order in
each process's interleaved passes, 3.4 and 6.1% alone and 2.8 and 2.9% after
scalar work. The second build makes them the default. Its other paths ran as
fast as the first build's. Both builds' scalar and level-order paths ran 1.6
to 2.8% faster than main's (by the processes' medians), from their other
changes or their code's placement. The comparison that decides is between the
two orders in one build, on one table.

Whole compiles of `sqlite3.c` (with `-DSQLITE_OMIT_LOAD_EXTENSION
-DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE`), by
`time-runs`, 21 runs of each build in a rotating order:

| Command | main, `321f5976` | `0f26899e` | `fe35a8b5` |
| --- | --- | --- | --- |
| `-c` | 127.95 ms, 599.5M cycles, 1,274.5M instructions | 127.39 ms (−0.44%), 595.8M (−0.61%), 1,275.3M | 126.46 ms (−1.16%), 591.3M (−1.37%), 1,269.2M |
| `-fsyntax-only` | 102.41 ms, 464.1M cycles, 988.9M instructions | 102.38 ms (−0.03%), 464.3M (+0.03%), 989.6M | 101.25 ms (−1.13%), 458.1M (−1.29%), 983.5M |

The switch to the postorder batches saved 6.08M instructions a whole compile
with these four SQLite defines. The graph benchmark above uses a different
preprocessing configuration, so its node count does not normalize that
instruction delta. The first build's other changes (the
catalogue's fixes, the comparison builtins, K&R definitions) cost 0.78M on
`-c` and 0.67M on `-fsyntax-only`. [The measurement record](https://github.com/buster14a/buster-performance/blob/f7fd1b498aa19706204b1b8e82f486db480d526c/docs/measurements/checker-schedules-133-2026-10-28/README.md) has
the commands, the trees and the logs.

### Node order or regions

#1050 measured what the regions do on `sqlite3.c`'s `-c`. A region's batch held
1.4 nodes: in a tree's postorder a parent follows its last child, so the next
node most often reads the one before it, and few of a region's nodes are the
integer folds that a batch takes 8 lanes at a time. What every region paid was
its machinery: a pass over its nodes for its leaves, then for each node a test
of its done mark and of each operand's, the batch's packing, and the scan for
the region's end. The vector path's default is node order now: each node in
turn, a leaf without its action (`check_graph_leaf`), a statement's discard of
an rvalue without its action (`check_graph_discard`), and any other node by its
action, the integer folds by the scalar fold. A region's nodes are pure (they
intern no type and have no effects), so in node order they make the same typed
stream, the same table and the same objects. The regions stay, opt-in
(`CheckOptions.regions`, which `CheckOptions.level_order` takes), for graphs
whose independent folds fill a batch, as `benchmark-checker`'s synthetic ones
do; `test-checker`, the graph tests' batch partitions and
`benchmark-checker-source`'s `regions` path keep them checked and timed.

The 9700X job of `232654c1` ([job 126383843829](https://github.com/buster14a/buster/actions/runs/41771140390/job/126383843829))
timed `benchmark-checker-source` on the frozen `sqlite3.c`'s 2,608 bodies
(311,601 nodes), as nanoseconds and cycles a node, medians of 21 passes:

| Path | Every path in one process | Alone | After 100 ms of scalar work |
| --- | --- | --- | --- |
| node order (`vector`) | 28.435 ns, 156.9 | 28.131 ns, 155.5 | 28.778 ns, 159.2 |
| regions | 31.597 ns, 174.8 | 31.123 ns, 172.5 | 31.454 ns, 174.3 |
| transitions | 34.191 ns, 189.4 | 32.254 ns, 178.7 | 34.358 ns, 190.3 |
| level order | 34.551 ns, 191.5 | 34.259 ns, 189.8 | 34.441 ns, 190.6 |
| scalar | 46.318 ns, 254.9 | 46.152 ns, 254.2 | |

Node order took 10.0% less time a node than the regions in one process, 9.6%
alone and 8.5% after scalar work. On musl's `regcomp.c` (8,881 nodes) it took
23.94 ns against the regions' 25.80, and the transitions 20.30. The 4-CPU Intel
VM, with no PMU, agreed: the main loop of a cold `-c` of `sqlite3.c` took
44.41M cycles in node order against 48.27M by the regions (−8.0%, medians of
24 compiles each in ABBA order, the objects the same bytes).

### Memoized transitions

#1050 found that a non-leaf node's action is, most of the time, a repeat: of
`sqlite3.c`'s 311,601 body nodes, 172,121 non-leaf nodes the table takes have
25,101 distinct keys. With `CheckOptions.transitions` (the driver's
`--check-transitions`), the vector path (`check_graph_transitions`,
`c_check_transitions.c`) checks those nodes by memoized transitions: a node
whose inputs repeat an earlier node's writes that node's answer without running
its action. They are opt-in (below). Graphs with overload sets take the regions
either way.

**Classes.** Each completed node's value has a class, a 32-bit id of its type,
flags, category, constant kind and payload (every field of the value but its
object), interned exactly on its first sight (`check_transitions_intern`). A
value without a payload row (`check_stream_value_rest`) is its columns; one with
a row is compared whole against its class's template, so that a constant, an
address constant's offset and a bit-field's place each are a class. A value
with a fault or an overload set has none (`BUSTER_CHECK_CLASS_UNKEYED`), and a
node waiting in a batch is `BUSTER_CHECK_CLASS_PENDING`. The classes live in a
column of the run's scratch (`levels`, which the later stages write before they
read), with an open-addressed table of 16-byte slots and the payloads'
templates.

**The key.** An ordinary action is a function of a key of four 64-bit words:
the node's operator, extensions (with the system-header bit), floating
environment, a call's argument count and the objects' pattern (which operands
have an object and which share one), its member, its requested type, its three
operands' classes and a hash of a call's arguments' classes, each with whether
it has an object; a call's arguments are compared whole beside the key. The
key holds no object and no node. An action copies an object from an operand
and reads one only for zero and for equality between operands, which the
pattern holds: outside a constant evaluation, which isn't memoized, no action
of the table's operators reads an object's identity
(`check_const_load` and `check_const_union_allows` are a constant evaluation's,
and the builtins that do are outside the table). An action reads a node only as
a new fault's place, and an answer with a fault isn't memoized. A node with an
operand of no class is a fold and runs its action.

The first node of a key runs its action (a miss) and inserts its answer into a
per-run table in the scratch arena. The answer is the value but its object (its
class, type, flags, category and constant kind, and a payload's template, its
class's), its conversions, diagnostic and its operand, warnings and a member's
offset, and a call's argument conversions; it also names the operand whose
object the value takes. A later node of the key (a hit) writes that answer and
its class with no `CheckAction`, no `CheckActionResult` and no interning.

A hit interns no type: every type the answer names was interned by its first
node. The misses run in node order, so a fresh table's `TypeId`s are the
scalar path's, which is what the regions' purity rule protected. Folds and the
operators the table doesn't take run their actions as before: the builtins,
varargs, the variable-length arrays' operators, statement values, constant
evaluations, vector members and plain library builtins' folds; their values are
interned after. Consecutive pure misses that read none of each other go as one
batch of `check_actions`, as the regions' batches did, so the integer folds
still go 8 lanes at a time.

An answer that read the source position keeps what it read. Each tag whose
completion `check_action_source_complete` asked about, with its answer, is a
guard (`CheckAction.guards`), and a hit asks it again at its own position. A
tag read before its completion and after it thus takes two misses, the second
writing over the first's entry. An answer that read a transparent union's
activation, or more than four tags, isn't memoized.

**The loop.** A pass first gives each node its static word, 16 nodes at a time
in AVX-512 with a scalar twin (`check_transition_statics`): a leaf, a node the
table doesn't take, or the key's first word but its pattern, with the
system-header bit gathered from the tokens' array (the lanes whose 4-byte
gather would pass its end go one at a time). The node order's loop then holds
only leaves and first-probe hits; every other node (a miss, a fold, a node
reading a batch member, an operator the table doesn't take) goes out of line
(`check_transition_other`), as do a payload's write and a call's argument
conversions, so that the loop keeps its values in registers. Inlined into
`check_graph_run`, the loop spilled its key and its columns' pointers to the
stack on every node; out of line, the same code took about 8% less time on the
VM below.

An entry is 128 bytes: its key and what the next nodes read (the class and the
object's operand) in one cache line, its conversions and member offset in the
next. A call's arguments and a second guard go in side arrays. The slots are
open-addressed, each an entry's index and its hash's high half, two per entry.
The table starts with room for an entry per 16 of the graph's nodes and a
class's slot per 32, and at least 16 entries, since `sqlite3.c`'s bodies took
an entry per 12 nodes and a class per 43, and grows by doubling within a
quarter of the room the run's scratch had left. Where its first size doesn't
fit, it starts with at most 1,024 entries, and then with 16; where even that
doesn't, the run takes the regions; where it can't grow, it stops inserting,
and where the classes can't, new values have none.

`CheckOptions.verify_transitions` runs each hit's action too, the loop's
first-probe hits' and those found after a batch ran (flushed hits), keeps its
answer and counts a difference (`CheckStatistics.transition_mismatches`), which
fails the run. The driver's `--verify` (with `--check-transitions`) verifies,
and so does `test-checker`, whose graphs and source fixtures the transitions
check against the scalar path too, each verified run's hits all verified
(`transition_verified`). `CheckStatistics` counts hits (and of them the flushed
ones), misses, folds, inserts and the table's entries, and `--time-stages`
reports them as `time-check transition-*` lines. `benchmark-checker-source`
prints them with each path's time.

`test-checker --transitions` runs the section alone. Each fixture is checked by
the scalar path, the oracle, and by the vector path's regions and its
transitions in batches of 32, 3 and 1, and every stream must be the oracle's.
Each fixture's hits, flushed hits, misses, folds and entries are pinned, and a
fixture's `(1 + 2) + 3` finds an earlier `3 + 3` only after `1 + 2`'s batch ran. Each node's class
must be shared exactly with the nodes of the same value but its object, and the
static words by 16 lanes must be the scalar path's, with system-header marks in
arrays from the tokens' whole count down to fewer than a gather's 4 bytes. A
source of 1,500 structs runs in a sweep of scratch arenas from too small for the
table's growth to room enough, then at the least room the run checks in, found
by bisection (in any less, it fails whichever path it takes), where not even 16
entries fit and the regions must run, and in steps of 512 bytes past it, which
must reach a table that takes entries. Each run that checks gives the oracle's
stream, and the sweep must reach a table that stops inserting and one that takes
every entry.

#### What a hit costs

On a 4-CPU Xeon (Sapphire or Emerald Rapids, 2.1 GHz) VM without a PMU,
`benchmark-checker-source` on `sqlite3.c`'s bodies, timed in check_graph_run's
phases and by a second pass of the node order on its own warm table (a
measuring patch, not kept): the run's validation took about 14 cycles a node and
its loop about 145 by either path. The loop's misses took about 70: 21,000
single misses of about 470 cycles of action each, and their interning and
insertion about 200 more. A warm leaf took about 58 cycles and a warm hit about
123: 42 to build its key, 38 to probe and check its guards and 44 to write its
answer. A repeated action costs the regions about 160 cycles, so a hit saves
about 40, which the misses' insertion and the leaves' interning spend again. An
ideal memo over the same graph (every node a hit of a compact table, writing
the same columns) took 18 ns a node, against the loop's 40 for its leaves and
hits: the rest is the loop's own work per node.

The class design keyed constants, so that the first design's 27,822 folds became
19,000 hits and 9,000 more misses: 146,916 hits and 25,205 misses, no fold. Per
process, the transitions took 1.00 to 1.05 times the regions' time on that VM
(medians of five processes), against 1.01 to 1.05 for the first design on the
same host; a VM host before it had measured the first design 7% faster than the
regions, the 9700X 4 to 7.5%.

#### On the 9700X, and the real compile

The 9700X jobs of #1198 ([job 125700333685](https://github.com/buster14a/buster/actions/runs/41568611005/job/125700333685),
[job 125709820070](https://github.com/buster14a/buster/actions/runs/41571004025/job/125709820070)) timed the
graph run by `benchmark-checker-source`, whose passes run it again and again:
`sqlite3.c`'s bodies took 32.33 to 34.29 ns a node by the transitions against
35.18 to 36.58 by the regions (34.01 to 34.36 against 35.99 to 36.49 after
100 ms of scalar work), and musl's `regcomp.c` 20.52 to 20.65 against 27.59 to
28.42.

A real compile runs it once, and there the transitions lose. `sqlite3.c -c`
on CPU 2, `measure-compile`'s medians of 11: `check-expressions` took 15.184 ms
(14.919 to 15.640) with `--check-transitions` against 14.339 (14.243 to
14.596) without; `time-runs`' medians of 21 put the compile at 113.66 against
112.72 ms; `perf stat -r 11` counted 535.9 million user cycles against 531.7,
though 1,130 million instructions against 1,206, and 2.72 million cache misses
against 2.48. The table (about 4.5 MB of entries, slots, classes and templates
for `sqlite3.c`), the class column and the static words are new lines to a
single run, which the benchmark's repeated runs keep in the caches; their
misses cost more than the instructions the hits save. Even without them, the
benchmark's gain is about 1 ms of a 14.3 ms `check-expressions`.

Writing the leaves' and hits' columns later, 16 nodes at a time with gathers
before an action or a pass needs them, took 6 to 16% more time on the VM than
writing them in node order, as each output line is then written twice, far
apart; direct open-addressed 64-byte entries without slots took 10% more.

#### The first design (#1146)

The first design keyed each operand by its type, flags, category and constant
kind in a 16-word key compared by one AVX-512 compare, and ran a node with a
payload operand as a fold. Its windows (`CheckOptions.transition_windows`)
keyed the ready nodes of 64-node windows 16 lanes at a time in rounds, as the
issue proposed. The rounds starved the lanes: on `sqlite3.c`'s bodies a window
took 11.7 rounds and a batch held 2.97 nodes, a postorder's chains and the
misses' dependents leaving most lanes empty, and on the 9700X they took 55.13 ns
a node against node order's 35.51; the class design removed them.

The 9700X job of #1146 ([job 125383291201](https://github.com/buster14a/buster/actions/runs/41473866134/job/125383291201))
timed the first design on `sqlite3.c`'s bodies:

| Path | ns a node | after 100 ms of scalar work |
| --- | --- | --- |
| scalar | 52.83 | |
| regions (`vector`) | 36.99 | 36.45 |
| transitions | 35.51 | 35.25 |
| levels | 39.35 | 39.00 |
| windows | 55.13 | |

The transitions took 4.0% less time a node than the regions (3.3% after scalar
work). The same job's `measure-compile` put `check-expressions` at 14.7 ms of a
116.9 ms compile. The whole compile showed no gain ([job 125470563590](https://github.com/buster14a/buster/actions/runs/41499213226/job/125470563590),
21 runs of each in a rotating order on CPU 2, the objects the same bytes): the
regions took 118.43 ms (117.96 to 119.12) and the transitions 118.69 ms (117.96
to 119.82). In the same job the graph run took 33.99 ns a node by the
transitions against 36.77 by the regions on `sqlite3.c`'s bodies, and 25.23
against 29.22 on musl's `regcomp.c`. The gain depended on the source: on the
first VM host, Lua 5.4.8's `lvm.c` took 15% less time, its `lparser.c`,
`lstrlib.c` and musl's `vfprintf.c` 3 to 5% more, as their keys repeated less;
on a Ryzen 9 7940HS (Zen 4), the review of #1146 measured `lvm.c` 10 to 20%
faster and `sqlite3.c` no faster.

### The per-node machinery (#1050)

#1050's last two rounds memoized the actions and found that what a hit skips
isn't where the time goes: what every path pays is the machinery around each
node. A profile of one cold `-c` of `sqlite3.c` (a VM's sampler and a
per-operator `rdtsc` table, measuring patches not kept) put the stage's time in:

- **building each action** (`check_graph_action`): copying each operand's
  96-byte value into the action from a temporary that `check_stream_value`
  cleared by two overlapping 32-byte stores, by a 32-byte load across both,
  which no store can forward: the most sampled instruction of the stage;
- **the warning pass** (`check_input_warning_graph`), a tenth of the stage's
  user time, almost all of it cold reads of each candidate's conversion record
  and whole value after the run;
- **calls**, about 650 to 850 cycles each, 15% of the main loop: a call built a
  408-byte action per argument to convert it;
- **statements' discards**, the casts to void every expression statement ends
  in, the most common operator after the leaves;
- **the stream's columns**, which every node writes: skipping the conversions,
  the diagnostics, their operands, the warnings and the members' offsets (a
  bound, not a valid output) took 11% of the main loop.

What the run does now, each step exact (both paths' streams, the objects and
the diagnostics are the same bytes):

- `check_stream_value_to` writes a node's value into its destination by each
  case's own stores, the columns' case as one 64-byte and one 32-byte store;
  the action's operands and arguments take it.
- `check_graph_store` writes the result's value from the action's fields
  (`check_stream_set_value_flags`) and its conversion records field by field.
- `check_action_operand_value` converts a value as an operand of an action
  whose context and guards alone it reads: a call converts each argument with
  it, as an operand of the call.
- The warning pass reads a source's conversion record and whole value only for
  an integer constant expression of value zero without a fault, which two narrow
  columns and the payload row say, and builds an ordered comparison's action
  only where an operand can be a pointer: the exact preconditions of its two
  warnings.
- In a fresh output (`CheckOptions.fresh_output`, the C driver's), the run
  clears no column and its main loop writes no zero (`CheckStream.zero_columns`):
  the diagnostics, their operands, the warnings and their ignored kinds take
  no memory where no node has one, and a first write reads no payload row.
- The vector path writes a discard of an rvalue without its action
  (`check_graph_discard`): the operand's value, its conversion to void and the
  cast's provenance, by the same helpers in the same order as the action, where
  the action would read nothing else (`check_graph_discard_taken`). The scalar
  path keeps the action, the twin whose streams the tests compare.
- `check_graph_constant_contexts` passes over 64 nodes that hold neither a
  context nor a mark at a time, and `check_graph_is_leaf` reads no leaf's record
  without overload candidates.
- The vector path takes its nodes in node order, its regions opt-in ("Node
  order or regions" above). With them, a region's end is found 64 marks at a
  time (`check_graph_marked_end`), whose exit, a node at a time, had been the
  main loop's most sampled branch on the 9700X, and a region batches only the
  operators whose integer folds `check_actions` takes 8 lanes at a time
  (`check_action_fold_operator`), another node whose operands are done checked
  at once by its own action.
- `check_stream_value` returns a value by assignment, as main's did: the
  lowering, its main caller, takes it into registers or a value of its own, and
  through `check_stream_value_to`'s stores clang had laid the lowering's
  functions out anew, which cost `lower` 1.2 to 1.9% on the 9700X.
- An action looks up only the kinds its operator reads: the comparisons' and
  the additions' pointer pairs, a complement's complex operand, a vector
  operation's types.

The 9700X job's step "Compare check-expressions with the pull request's base
over placements" compares a pull request's base and tree on `-c sqlite3.c`,
with each build's stages, counters, profile and forwarding failures. The job of
`232654c1` ([job 126383843829](https://github.com/buster14a/buster/actions/runs/41771140390/job/126383843829))
compared the base, `2fef25b0`, with the tree (with
`-DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5
-DSQLITE_ENABLE_RTREE`), each at four placements of its checker unit. Each
placement's stages, as `measure-compile`'s medians of 11 runs, in ms (base →
tree):

| Placement | `check-expressions` | `lower` | `check-program` | span |
| --- | --- | --- | --- | --- |
| 0 | 14.330 → 10.493 (−26.8%) | 12.959 → 13.308 | 10.099 → 10.163 | 108.877 → 105.814 |
| 1088 | 14.233 → 10.420 (−26.8%) | 12.973 → 12.963 | 10.183 → 10.042 | 109.198 → 105.093 |
| 2112 | 14.704 → 10.482 (−28.7%) | 12.968 → 12.919 | 10.150 → 10.127 | 109.527 → 105.783 |
| 3136 | 14.240 → 10.414 (−26.9%) | 13.010 → 12.946 | 10.186 → 10.122 | 109.344 → 105.354 |

- `time-runs`, 5 runs of two fresh copies of each placement of each build, with
  the base's executables again as the floor: the wall time went from
  110.333 ms to 106.625 ms (−3.36%; −3.70% to −3.08% over the placements) and
  the user cycles from 523.6M to 504.0M (−3.74%). The base again read −0.14%.
- `perf stat` counters, 11 runs at placement 0:
  - instructions: 1,219.5M → 1,172.9M;
  - branch misses: 3.13M → 3.04M;
  - failed store-to-load forwards (`ls_bad_status2.stli_other`): 5.96M → 4.24M.
- Memory: the wait4 peak went from 282,608–283,884 KiB to 278,316–278,368
  KiB over the placements. `check-expressions`' approximate peak growth went from 4,904 KiB
  to 872 KiB.
- `lower`:
  - At placement 0 it took 2.7% longer: 13.308 ms (13.251 to 13.546) against
    12.896 to 13.063 ms. Most of that is in `lower.statements`.
  - At the other three placements it took 0.1% to 0.5% less.
  - Since `check_stream_value` is by assignment again, the lowering's functions
    are the base's instruction for instruction, but for data addresses. The
    difference at placement 0 is the code's placement.
  - Over the four placements its median is 12.95 ms against 12.97.
- `check-program` read −1.4% to +0.6%.
- The objects are the same bytes, which the step requires.

`check-expressions` over the rounds, base → tree, in ms (placement 0; the
fourth row's are medians over the placements):

| Tree | `check-expressions` |
| --- | --- |
| `43b84486` | 14.346 → 12.744 (−11.2%) |
| `837e3638` | 14.211 → 12.528 (−11.8%) |
| `7da3876f` | 14.446 → 11.805 (−18.3%) |
| `1244e794` | 14.37 → 11.61 (−19.2%) |
| `232654c1` (node order) | 14.330 → 10.493 (−26.8%) |

What remains is spread over the actions. The forwarding failures are one lead.
One sample in 500 put 8.2% of the compile's failures in `check_graph_run`. A
quarter of those were at two wide copies of values just written field by
field: an operand's converted value returned whole (`check_action_operand`),
and a result copied into its payload row (`check_stream_set_value_flags`).

## Constant evaluation

Integer constants contain target-width bits in `unsigned __int128`. Promotions,
usual arithmetic conversions, narrowing, sign extension and overflow checks
use the x86-64 target type rules. Host signed overflow does not implement target
overflow. Integer widths through 128 bits have explicit arithmetic handling;
wider `_BitInt`s have their own (below), and a value that no path holds
produces a defined failure rather than truncation.

### Constants past 128 bits (#1447)

A constant of a `_BitInt(N)` past 128 bits (`c_check_wide.c`) is
`CHECK_CONSTANT_INTEGER` of its type, whose N bits are `ceil(N / 64)` 64-bit
limbs, the least significant first, the bits past N zero, as a narrow
constant's `integer` is masked to its width. The limbs are immutable and live
in one region of reserved address space for the process's lifetime, bump
allocated by an atomic add and committed as they are touched
(`check_wide_allocate`); the value points to them from `floating_storage[0]`,
which no integer constant uses, and keeps their low 128 bits in `integer`.
`CheckValue` stays 96 bytes, and its pointer stays valid wherever its row is
copied (lane outputs, the deferred bodies' merge, stream rows, initializer
plans, emission), so nothing relocates it. A value of a wide type whose pointer
isn't the region's is no constant (`check_wide_limbs`).

`check_action_integer_type` still refuses those widths, so every narrow fold,
reader and warning sees what it saw before; the paths that take them are cold
branches beside it: a value's truth (`check_wide_truth`), a conversion into or
out of a wide type (`check_wide_convert`: integers of every width, `_Bool`, the
pointers' absolute addresses, and `float`, `double` and `long double` rounded
once from the top 128 bits and a sticky bit; a `__float128` or a
half-precision value isn't folded), an operator whose operands are wide or a
narrow value shifted by a wide count (`check_wide_fold`), and an integer
constant expression read as an `int64_t` (`check_wide_i64`: array bounds,
enumerators). A const object's read takes its initializer's wide constant, or
a zero's limbs, as clang's folding does (`check_const_load`,
`check_const_zero`); the bit and alignment builtins fold them too (the
[builtins' guide](c-builtins-integration.md)). Faults are the narrow ones: signed overflow, division by zero,
shift counts and shifted values, with GNU's folding where the context takes
it (a count past the width shifts by width - 1, as clang's evaluator clamps
it).

The bitwise operations, sums, differences and negations, compares and shifts
take 8 limbs a zmm with masked tails, each with an exact scalar twin, which the
checker's scalar path takes (`test-checker`'s `check_test_wide` compares them
at every count to 80 limbs). A sum's carries cross the limbs through the compare
masks: where a limb's sum wrapped it generates a carry, where it is all ones it
propagates one, and the carries into the limbs are `((G << 1 | c) + P) ^ P`.
Shifts are `vpshldvq`/`vpshrdvq` of masked loads of neighbouring limbs.
Products (64 x 64 -> 128-bit limb products, which AVX-512 has no instruction
for), quotients and remainders (shift and subtract from the dividend's top bit)
and decimal printing (`check_wide_decimal`, repeated division by 10^19) stay
scalar: no unit of the corpus has a wide constant, and these run once a
folded node.

`float`, `double` and x86 `long double` folding uses those formats, including
rounding arithmetic results to their destination precision. Converting a
128-bit integer to `float` or `double` converts directly to that format: an
intermediate `long double` could round twice around a midpoint. Complex
constants retain real and imaginary components in their source format; GNU
integer-complex values retain separately masked components. A failed component
conversion clears the entire constant payload while preserving its fault.
Exotic floating formats retain explicit unsupported folding requirements.

Constraint diagnostics and evaluation faults are separate. An invalid operand
type is an error even in an unselected expression. A possible divide by zero,
overflow or invalid shift travels with its value and source node until an
enclosing context requires that evaluation. A floating value converted to an
integer type outside its range is undefined, and clang's folding takes it as
APFloat converts it, saturated to the type's range and a NaN to 0.
`check_action_convert_plain` folds it so: a GNU constant
(`BUSTER_CHECK_GNU_FLOAT_RANGE`), not an integer constant expression. clang
takes it where one is required (an enumerator's value, a case label, a static
assertion), and as a static initializer's value (`int b = (int)1e10;` is
`INT_MAX`), but not in an array bound, which it doesn't fold, and neither does
the checker (`check_input_vla_bound`'s folding, `check_input_integer_attribute`).
`__builtin_constant_p` of it is 1, as clang's is, with no proof of its own.
`&&`, `||` and `?:` discard faults in unselected operands; `sizeof` does not
evaluate its ordinary operand. Thus these two cases have different outcomes:

```c
_Static_assert(1 || (1 / 0), "not evaluated"); /* accepted */
_Static_assert(1 || (1.0 % 2), "invalid type"); /* constraint error */
```

ICE eligibility is tracked separately from whether a value happened to fold.
For example, an immediate floating literal cast to integer can be an ICE;
an ordinary floating arithmetic expression is not made an ICE merely by
folding. Source contexts explicitly request integer constant expressions,
static assertions or constant static initializers. A static assertion among a
structure's or a union's members (#2264) declares no member: the member reader
steps past it to its `;` and the walk checks it as a block's.

Constant-query answers keep source proof, speculative proof and runtime effects
separate. A source constant answer does not erase a floating-point exception or
rounding dependency in an evaluated operand. Statement-expression proofs follow
one known control path and refuse a revisited step or unsupported transfer;
runtime effect collection visits reachable steps once and combines unknown
branches. Both consume the existing checked values rather than interpreting a
second variable environment. The ordinary program body remains responsible for
runtime execution.

`__builtin_bit_cast` retains its destination type and raw source operand before
array decay or ordinary argument promotion. Type constraints, representation
availability and a usable constant payload are separate results. A valid cast
with unavailable representation data remains semantically valid and carries an
explicit downstream requirement; it does not acquire invented bytes or a
guessed result type. Floating representation transfer does not read a poisoned
or unavailable host floating payload.

Address constants retain an object identity and signed byte addend. The source
adapter marks static-storage objects and functions. Taking the address of an
automatic object retains identity for checking but does not create a static
initializer constant. Same-object pointer operations can fold when their
element size and addends permit it; absolute integer-to-pointer casts do not
invent same-object provenance for relational comparison or subtraction.

## Static-data handoff

`c_check_data_input.c` consumes the retained source declaration columns. It
selects one real or tentative definition per canonical object, keeps the latest
composite type, creates distinct local/global symbols and passes selected
`CheckDataObject` records to `check_data_emit` in `c_check_data.c`. It never
reparses declarations or binds identifiers again. An actual initializer wins
over later tentative or external declarations. Automatic objects and
unreferenced function bodies produce no data atom. An object's alignment is its
type's or its declarations' largest alignment attribute's, and without one at
least 16 for an array of 16 bytes or more, as the x86-64 psABI aligns a global
array and clang declares it, an `extern` one too, which clang's optimized code
stores into with aligned SSE moves (`check_data_declared_alignment`, #978);
compound literals and string literals keep their types' alignment.

Identical live string literals share one storage atom per translation unit
(#1474), in both code-generation tiers. `c_check_string_pool.c` groups selected
literal records by exact array type, requested alignment and decoded byte length
and contents, including embedded and trailing NULs. Concatenation and alternate
escape spellings therefore share storage when they produce the same object.
Each occurrence keeps its existing object identity and symbol, defined at offset
zero in the shared atom; code references and static pointer relocations retain
their original symbols and byte addends. Pooling happens after reachability
selection, so an unreferenced earlier occurrence cannot own a live literal's
storage. Named arrays, including const arrays initialized by a literal, and
predefined function/source-name arrays keep separate atoms. There is no suffix
merging or cross-unit pool in this producer.

The pool reuses the batched string hash and scalar/vector radix sorter. Equal
hashes require full-key equality; mixed-key collisions use an iterative merge
sort, with the earliest equal record as the canonical one. Thus collision runs
need at most O(n log n) key comparisons and linear scratch, rather than pairwise
search. A shared metadata validator rejects invalid type, size, alignment and
symbol descriptors before the pool reads payload bytes. Planning copies each
unique literal once, and publication capacity counts unique atoms and bytes;
failed calls retain the existing transaction rollback guarantees.

The emitter consumes checked aggregate initializer plans, including brace
elision, designated/range stores, bit-fields, compatible string copies and
selected union members. It handles integer and bit-precise storage,
binary32/binary64 and complex components, x87 components with deterministic
padding, and pointers. Objects use RODATA, DATA_RELRO, DATA, BSS, TDATA or TBSS
as appropriate. Only ordinary single-terminated byte literals use STRINGS
(`SHF_MERGE | SHF_STRINGS` in ELF). Wide literals, embedded-NUL literals and
predefined arrays use RODATA: an external linker must not split a complete
embedded-NUL object into independently mergeable substrings or merge an
identity-bearing array. Identical embedded-NUL and wide literals still share
the compiler's canonical atom. Symbolic pointers emit
ABS64 fixups with signed byte addends; absolute pointer values remain bytes.
External targets may remain unresolved. A referenced internal function needs
its real code atom: standalone data inspection reports that limitation, while
combined object/data output uses the program's actual symbol space.
All-zero writes and large clear ranges remain symbolic. For ordinary nonzero
objects, a bounded proof inspects at most eight final writes for a nonzero
integer representation untouched by later writes. When the retained byte suffix
already fits, validation and serialization then run together in source order,
without a scalar cache or replay. Capacity failures still take the validating
symbolic path, so an earlier invalid write retains diagnostic precedence.
Other aggregates validate and encode their scalar writes once before replay. When
overwrites can remove earlier nonzero bytes, a backwards last-writer walk over
sorted bit-range boundaries determines the final state; each interval is visited
once, with scratch proportional to checked writes rather than declared storage.
Only a surviving nonzero byte or relocation requires an object-sized payload,
built directly in the unpublished retained suffix. Encoded scalar slots use the widest actual scalar representation in
that object rather than an unconditional 32-byte slot; CLEAR/BYTES-only objects
allocate none; mixed plans may retain stride-sized positions for other part kinds.
Replay releases its cache before the next object is planned,
retaining only payload and fixups. Zero plans release their unused per-write
caches as well. Const zero objects keep RODATA and are filled directly during publication;
negative floating zero is classified by its encoded bytes. The byte-range scan
has bounded AVX-512 loads and an exact scalar twin, including partial-byte tails;
both stop after the first nonzero block/byte. Overlap boundaries reuse the existing
stable scalar/vector radix kernel, sorting both 32-bit halves and skipping
constant digits on the vector path, with guarded independent ordering tests.
Initializer plans retain the exact selected overload candidate and object.
The standalone source bridge and the program emitter share `c_check_mangle.c`
for selected overloadable symbols (#821). Calls and contextual addresses retain
the checker's selected object, and each emitted object receives its Itanium ABI
name once; explicit asm labels override it. Static inline overloads, including
Clang's `<tgmath.h>` wrappers, follow ordinary reachability and publication.
Unmarked overload candidates retain C names, and unused declarations need none.

The mangler uses an explicit worklist and canonical type substitutions, with
scalar/vector substitution lookup. It covers builtin and complex arithmetic,
qualified pointers, fixed/incomplete arrays, function pointers, vectors,
atomics, bit-precise integers and file-scope named tags. Signatures requiring
local/anonymous tag naming, variable array bounds, or an unsupported calling
convention still fail before publication unless an asm label supplies the ABI
name. These refusals do not select a different overload or publish an unmangled
fallback. The focused `test-checker --program-overloads` suite checks independently
stated encodings, deep signatures, allocation failures, deterministic emission,
cross-compiler linkage in both directions and the installed `<tgmath.h>` through
both object and direct-executable driver paths; the full checker suite runs it too.

A designator of the wrong kind for its current object is clang's
`CheckDesignatedInitializer` error, named by that object's type (#2202,
`CHECK_INIT_ARRAY_DESIGNATOR`, `CHECK_INIT_FIELD_DESIGNATOR`): "array designator
cannot initialize non-array type 'T'" at a `[` and "field designator cannot
initialize a non-struct, non-union type 'T'" at a `.` (at the name of GNU's
old-style `name:`), T the object the previous designator selected for a later
one (`.a[0]` of an `int a`), and an array of unknown bound's as declared
(`int[]`), through its inference. A designation in a scalar's or a vector's
braces is "initialization of non-aggregate type 'T' with a designated
initializer list" (`CHECK_INIT_NON_AGGREGATE_DESIGNATOR`): at the declarator
where the list is the whole initializer, its type as the declaration spells
it, else at the list's `{`. Each used to be "invalid initializer". The
initializer-field fixtures (`check_test_driver_initializer_fields`) compare
each with clang on both paths, `-fsyntax-only` and `-c`. Differences left: an
array of unknown bound's error still stops the unit's later initializers'
checks, as an unknown field's in one does.

The source initializer adapter matches the selected language's warning policy
for excess elements and compatible overlong string copies. Ignored tails are
still expression-checked, but produce no stores, runtime evaluations or live
data references. The direct initializer API retains its strict rejection
contract. Diagnostic-only scans borrow complete checked roots, restore both
arenas and never publish an initializer or a supposedly clean stream.

String initializer lookup uses `CheckInput.string_ids`, the reader's token-to-literal
index retained in the output arena, a sparse column (`c_check_sparse.c`, #628). Every adjacent literal part names the same
record. An array-valued expression also resolves its ordinary literal through
its object identity (`token + 1`), preserving parenthesized string copies;
predefined function-name arrays remain excluded from literal initialization.
The planner checks the indexed record's bounds and identity instead of scanning
all literals for every initializer, including nonstring elements of a record.
Deferred bodies remap this existing column together with their merged literal
pool, and corpus comparisons include the retained indices.

Static-assert messages contribute no expression value. The reader's existing
constant-context pass marks each message span once, after identifying its
assertion's top-level comma; the walk's action at the assertion's `)` reads the
message's escape sequences (clang's unevaluated string literal, "Unevaluated
string literals (#147)"), and one it refuses drops the assertion.
String operands in the condition remain checked.
This removes a parser-ancestor walk per string, whose repeated traversal of a
long initializer's separator chain was quadratic (#593). Both changes consume
existing reader facts; they add no search pass or per-initializer allocation.
The parser rejects nonstring message operands before this span is ignored;
regressions also check a C23 assertion with no message. The retained scaling
campaign measures the default production path. Scalar/vector semantic checks
establish equality, while the scalar reference's separate value-chain walks
remain outside this string-specific scaling claim.

### String literal blocks (#1388)

`check_literal_strings` decodes an ordinary or UTF-8 string literal's body 64
source bytes at a time (`check_literal_string_blocks`) on a host with the
vector ISA, and through `check_literal_string_part_scalar`, its twin and
oracle, with `--scalar` or on any other host. Each block is one masked load
that reads no byte past its part. Escapes start where simdjson finds them, at
the backslashes ending an odd run (the run's carry crossing blocks), and
`vpermi2b` tables give each byte its digit value and flags and a simple
escape's value, with `vpermb` moving each class one lane down so that a byte
sees what follows it. Masks of plain bytes, simple escapes, `\x` with one or
two digits and octal escapes of one to three digits leave each unit's value in
its first byte; `vpcompressb` packs them and a masked store writes their count.
The blocks leave to the scalar step, stopping before it, a universal character
name (`\N{}`'s too), `\x` with no digit or more than two, an octal escape past 255,
a quote, line break or NUL, and the unit holding the first byte UTF-8
validation (Keiser and Lemire's, run only on a block with a byte past 127)
rejects. A block takes at most 60 bytes unless it is the part's last, so every
unit it decodes (four bytes at most) ends in it. Wide, UTF-16 and UTF-32
literals and raw ones take the scalar step. `test-checker` decodes 30,000
random groups of one to three parts both ways, each part ending at a
`PROT_NONE` page, half of them 60 to 68 or 124 to 132 bytes long
(`check_test_string_literal_blocks`), and both must agree on every field and
byte. On #1388's megabyte of `\xhh` escapes in 32-character pieces the
checker's input stage went from 53.7 ms to 11.0 ms (Ice Lake server, 11 runs).

### Unevaluated string literals (#147)

clang's parser reads an attribute's string argument (the argument places
clang-tblgen's string switches give, `c_check_attribute_arguments.inc`, also
of a GNU attribute clang doesn't know, `__attribute__((nodiscard("…")))`, by
its name; parentheses around it peeled), a static assertion's message and a
`#line` directive's or line marker's file name as an *unevaluated* string
literal. Only the simple escapes (`\\ \' \" \? \a \b \f \n \r \t \v`) and the
universal character names are valid there; every other escape sequence (octal,
including `\0`; hexadecimal; `\e` and `\E`; GNU's `\( \{ \[ \%`; C2y's
delimited `\x{…}` and `\o{…}`; an unknown one, after its
-Wunknown-escape-sequence warning) is "invalid escape sequence 'S' in an
unevaluated string literal", S as written (`'\x64e'` is the whole hexadecimal
run, `'\0123'` the octal escape's three digits). Its reading's errors come
first: "hex escape sequence out of range" and "octal escape sequence out of
range" (the characters are a byte wide whatever the prefix), a delimited
escape's, "\x used with no following hex digits" (with no unevaluated error).
Each is at its escape's backslash, in source order across the literal's
concatenated tokens. clang's literal-wide error state is kept across them:
after any earlier error of the literal, hexadecimal and delimited octal
escapes have no range error (a plain octal one still does), and `\x` without
digits sets none. `check_literal_unevaluated_escapes`
(`c_check_literal_diagnostic.c`) is clang's `ProcessCharEscape` of such a
literal: it finds the backslashes with `memchr` and reads only escape
sequences byte by byte, a cold path of short, rare strings with no vector
twin; both checker paths call it the same way.

The universal character names valid there are those clang's
`ProcessUCNEscape` takes in any string literal, which it checks there too
(`check_literal_unevaluated_ucn`), each error at the backslash:
- **Digits:** "\u used with no following hex digits" (or `\U`), and
  "incomplete universal character name" short of four (eight).
- **Value:** "invalid universal character" for a surrogate or a value past
  U+10FFFF.
- **Before C23:** a value below U+00A0 other than `$`, `@` and `` ` `` is
  "universal character name refers to a control character" or "character 'c'
  cannot be specified by a universal character name".
- **Delimited `\u{…}`:** the delimited escapes' errors, and an overflow is
  "hex escape sequence out of range".
- **Named `\N{…}`** (#1447): its braces ("expected '{' after '\N' escape
  sequence", incomplete, empty; a line break ends its name too), then its name,
  exactly Unicode's (`unicode_name_strict`), else "'x' is not a valid Unicode
  character name" at the name with clang's notes (`unicode_name_diagnose`: the
  first byte no name has, "character x cannot appear in a Unicode character
  name"; then where the name is one loosely, that names are sensitive to case
  and whitespace; else, where every byte may be a name's, "did you mean NAME
  (C)?" for the nearest names by the edit distance of their letters and digits,
  while each is at most 3 farther than the one before), then its character as
  a numeric one's.

Such an error drops the construct as the others do, with no unevaluated error.

clang then drops what the literal belonged to, and its diagnostics stop there.
The reader (`check_input_unevaluated_strings`) reports
`CHECK_INPUT_UNEVALUATED_ESCAPE` once at the literal's first refused token
and the driver words each escape's diagnostics, the warning by its options
and pragmas (`-w`, `-Werror`, `-pedantic-errors`,
`-Wno-unknown-escape-sequence`). An attribute (`check_input_attribute_read_item`)
is dropped where its first argument that fails does, and its list and
declaration go on without it; in a declaration of no declarator
(`check_input_attribute_ignored_lists`) the error ends the reading, as
"expected string literal" does. A dropped C23 `deprecated` or `nodiscard`
gives clang's parser's error of the attribute its specifier sequence parsed
before it, at its `(`: "parentheses must be omitted if 'X' attribute's argument
list is empty", or "attribute 'X' cannot have an argument list" where that
one takes none (`check_input_attribute_standard_previous`). A static assertion
whose message is refused is checked no further: its condition, an expression
in a constant context (`BUSTER_CHECK_CONTEXT_VALUE`, without the runtime
behavior's warnings), is neither required to be an integer constant nor
asserted. Where the parse failed, a repaired view keeps the report
(`check_repair_input_report`), and the tokens before the first syntax error
that no view checked are read as `check_driver_unparsed_literals` reads their
numeric constants (`check_driver_unparsed_strings`: GNU attributes' and known C23
attributes' string arguments, a standard one's or a `gnu::` or `clang::` one's,
and static assertions' messages). A `#line` directive's is dropped
by the preprocessor (`pp_decode_file_name`), whose unknown-escape warning has
no flag, as its other warnings haven't. Asm labels and statements, pragmas'
strings and `_Pragma` read evaluated literals, as clang's do.

An encoding prefix (`L`, `u`, `U`, `u8`) has no effect there (#1447): each
prefixed token has -Winvalid-unevaluated-string's "encoding prefix 'P' on an
unevaluated string literal has no effect" at it, and the literal reads as an
ordinary one (`check_literal_unevaluated_parts`), so `deprecated(U"x")`'s
message is `x` and `_Static_assert(0, L"m" "sg")` fails with `msg`. Clang
gives a literal's prefix warnings before any of its escapes' diagnostics, and
a static assertion's before its failure: the reader's event of a literal it
takes (`check_input_unevaluated_refused`, completed at the assertion's
keyword), the driver's ahead of the escapes of one it refuses
(`check_driver_unevaluated_prefixes`). -pedantic's warnings come with the
escapes' (`check_literal_unevaluated_escapes`' `pedantic`): "use of
non-standard escape character '\e'" (-Wpedantic) before a GNU escape's error,
and -Wdelimited-escape-sequence-extension's "delimited escape sequences are a
C2y extension" and "named escape sequences are a Clang extension", which clang
places at the literal's token, after a delimited or named escape it read whole
while the literal had no error (a delimited universal character name's after
its own basic or control character's error). Both are off by default, given by
-pedantic, and errors under -pedantic-errors (`check_warning_pedantic`). A
literal's escape and prefix diagnostics show the macro argument it came from,
"expanded from macro 'M'" at the parameter, as clang's literal parsers' do
(`Preprocessor.diagnostic_arguments`); a numeric constant's errors don't, nor
clang's.

An evaluated literal's escapes have the same warnings, as clang's literal
parser gives them (`check_literal_evaluated_escapes`' `pedantic`): a GNU
escape's at its backslash, a delimited numeric one's while the literal has no
error, and a delimited or named universal character name's after one of no
error of its own, whatever came before (`"\x{100}\u{e9}"` warns of the
second). The driver words a refused literal's; the reader gives events of a
literal its extended decoding read (`CHECK_WARNING_EVENT_NONSTANDARD_ESCAPE`,
`CHECK_WARNING_EVENT_DELIMITED_ESCAPE`), and scans one the fast decoding read,
which takes `\e` without a word, only where the command line may give the
warning or the unit has a diagnostic pragma (`check_input_gnu_escapes`). A
static assertion's message, which the reader reads too, has the unevaluated
scan's alone (`check_input_assertion_message`).

Not clang's yet: what follows `\x` without digits, which clang alone keeps
(its attribute still applies); and the warnings of the literals after one the
checker refuses, which ends the reading. A static assertion's message of an escape
the fast decoding refuses (a named or a delimited one, #1447) is read as clang
reads it (`check_literal_strings_extended`). Like the checker's other errors, the
first one ends the unit's semantic diagnostics after its declaration.

### Evaluated literals' escapes (#1447)

The checker's literal decoding (`check_literal_strings_by`,
`check_literal_character`, and the string blocks' vector path) refuses what it
doesn't read. Clang takes some of that in an evaluated literal: delimited
escapes (`\x{…}`, `\o{…}`, `\u{…}`), GNU's `\E` (27, as `\e`), and an unknown
escape, whose character is its value, with -Wunknown-escape-sequence's warning
at its backslash (`\(`, `\{`, `\[` and `\%` without one, as clang words those
only under -pedantic). On a refused literal's cold path alone, the reader
decodes it again with them (`check_literal_strings_extended`,
`check_literal_character_by`'s `extended`, which the hot callers' constant
false compiles out) and, where that takes it, gives each unknown escape's
warning (`check_input_unknown_escapes`, `CHECK_WARNING_EVENT_UNKNOWN_ESCAPE`,
published at its offset in the token).

A literal the extended decoding refuses too is worded by the driver as clang's
(`check_driver_literal_escapes`): its escapes' diagnostics in source order
through its concatenated tokens (`check_literal_evaluated_escapes`, the
unevaluated literals' printer `check_driver_escape`), a numeric escape's range
by the literal's code unit (a byte, a `u` literal's 16 bits, none for `U` and
`L`), the universal character names' and the delimited escapes' errors as
above; a character constant's character past its type ("character too large
for enclosing character literal type"), or several characters in a wide or
Unicode one ("wide character literals may not contain multiple characters",
"Unicode character literals ..."), and an ordinary one's characters, counted
whatever their errors (`check_driver_character_count`), more than one's
-Wmultichar warning after its errors (four characters' -Wfour-char-constants).
A named universal character name (`\N{…}`, #1447) is its character, by
Unicode's names (the extended decoding's `unicode_name_strict`), with the errors
above. A string literal's own refusal stands for
its declaration's (`check_input_initializer_prepare_slow`) and its asm
statement's, so neither adds "invalid or unsupported declaration". An asm
statement's strings and an asm label's, which clang reads as evaluated
literals, decode the same way: the label through `check_input_attribute_string`'s
`evaluated`. A static assertion's message stays the unevaluated literals'
(`check_driver_escapes`). After a unit's parse error, the driver's scan of the
unparsed rest (`check_driver_unparsed_strings`) words the evaluated literals'
errors too. After a checker error that stops the reader, the literals it
left unread are scanned as a failed parse's rest is
(`check_driver_unread_literals`, from `CheckInput.walk_end` and past the last
report's token): their errors and warnings, which clang's literal parsers give
whatever came before. The unit's other semantic diagnostics still end there
(semantic error recovery).

An ordinary character constant of more than one character
(`CheckLiteral.characters`) has clang's warnings at it
(`check_input_multichar`): -Wmultichar's "multi-character character constant",
for exactly four characters -Wfour-char-constants' (ignored by default), and
past four "character constant too long for its type" too, its value the last
four.

### Large integer literal tables (#658)

`c_check_literal_table_input.c` recognizes file-scope integer arrays with at least
128 elements whose complete initializer consists of number tokens, optional
unary minus signs, commas and one outer brace pair. The scalar path checks the
same grammar; the vector path carries predecessor masks across bounded
64-token blocks. Sixteen spellings form a conversion batch: short decimal and
two-digit hexadecimal literals decode together, while other integer spellings
reuse the existing literal parser and unary action. Each value must fit its
8-, 16-, 32- or 64-bit element exactly. A case needing a conversion warning,
an excess-element warning, another numeric format or general expression
checking falls back before the producer changes any source state.

Successful lists retain one `CheckInputLiteralTable` record and one immutable
`CHECK_INIT_BYTES` part. Their elements produce no expression nodes,
`CheckValue` rows or individual initializer/data-plan parts. Inferred array
lengths are completed through the ordinary type constructor; omitted tails
remain zero-filled by the existing static-data consumer. Declaration order and
canonical object identity remain unchanged. The records are linked in source
order, and `check_initializers_run` advances through them with its declarations.
Compact and ordinary plans publish the same ascending `declaration_tokens`
index, so sparse lookup and retained string indices keep their usual contracts.
The payload and part belong to the input arena and remain alive with its
initializer plans. Deferred checking replays their array TypeIds with the
top-level declaration types. A failed payload or record reservation restores
the unpublished output suffix and scratch and sets the resource status.

An array of rows (#1383), `T t[N][M]` or `T t[][M]` with `T` such an element,
takes the same path when every element is a braced row of exactly `M` such
items: `{ list }` rows separated by commas, one trailing comma inside a row and
one after the last. `check_literal_table_rows` checks this grammar. Its vector
path classifies the same 64-token blocks into number, comma, minus and brace
masks and takes brace depth from a prefix XOR of the brace mask, carried across
blocks. A `{` must be at depth 0 and a `}` at depth 1, so the parity is the
depth. Each `}` compares the numbers before it, counted from the table's start
by `popcnt`, with its rows times `M`. The payload is the row-major values. The
type of an incomplete array is completed to the row count. A short row, an
elided or excess one, a deeper array and a designator at either level all keep
the ordinary path.

Arrays of integer structs (#1383), such as `struct R { int a; char b; long c; }`,
also use the rows grammar when each brace supplies exactly one literal per
member in declaration order. Every member must be a non-bit-field, nonvolatile
8-, 16-, 32- or 64-bit integer other than `_Bool`. The producer gathers literal
tokens once, converts each member's column with the existing scalar/vector
batches, and places the results at that member's byte offset in a zeroed record
payload. Type-table member offsets are in bits and are converted to bytes
before checking the store bounds. This preserves packed layouts, padding,
record alignment and omitted outer-array tails. Incomplete outer bounds finish
at the record count. The source-order reader also accepts block-scope static
tables; deferred function bodies retain ordinary checking. Floating, atomic,
aggregate and bit-field members, partial records, designators, nonliteral
expressions and conversions needing warnings retain the ordinary path for the
whole table. Tests require compact selection, compare its bytes and completed
types with the full graph, and cover guarded stores, scratch refusal and replay.

Automatic arrays, volatile and atomic elements, 128-bit elements, designators,
short or elided rows, deeper nesting and nonliteral expressions retain ordinary
checking and planning. `check_input_tables_reference` provides that exact
source path for tests. Permanent tests compare complete bytes and array bounds
between it and both compact paths, exercise protected syntax/output tails,
reservation rollback and deferred replay/fallback, and execute representative
generated objects against Clang. This bounded path leaves the reader's existing
token columns in place; their initialization and the parser remain substantial
costs on very large tables. It does not establish the full memory/work target
described in #658 or a general compilation speedup.

The [frozen native qualification record](https://github.com/buster14a/buster-performance/blob/32b96dd2a12c12c7aaf25be3639a31b7fc87cdd9/docs/measurements/unclaimed-perf-2026-10-28/issue-658/target/RESULTS.md)
measured 2.32 to 2.98 times lower whole-process wall time for its eligible byte
tables on the original base and candidate. Those exact-revision measurements
do not establish the gain on a later publication stack. Fallback and ordinary
SQLite controls, complete objects, runtime checks and remaining costs are
retained alongside them.

The source bridge owns symbols, names and Link tables in `output`; `bytes`
owns initialized contents and `scratch` is temporary. The three arenas must be
distinct. The lower-level emitter accepts an existing Link and object-to-symbol
map and plans validation/capacity before publishing atoms or fixups. Failures
leave its existing Link, bytes and scratch unchanged. The source bridge rolls
back all its output on failure and validates the completed Link on success.
Tests pass these atoms through the production ELF writer and independently
compare bytes, symbol targets and relocation addends with Clang's object files.

Static bit-field writes (`check_data_bit_field_value`, #601) merge only the
partial boundary bytes and replace each interior byte once. They consume the
validated integer's low bits, including signed and 128-bit values; an unaligned
128-bit field can occupy 17 destination bytes. Clears mask the boundaries and
zero the interior with `memset`, including ranges wider than a scalar. Neither
helper reads or writes beyond the checked extent, requires padding, or changes
the source order of overlapping designated writes and union replacements.
`c_check_data_bit_field_test.c` checks every scalar width and byte offset against
an independent bitwise reference at guarded allocation edges, larger clears,
ordered overlapping plans and adjacent relocations. Source fixtures also pass
packed 128-bit fields and replacements through the ELF writer and Clang.

The locked [9700X comparison for #601](https://github.com/buster14a/buster-performance/tree/960c8cd2fd82c707f89296ef386352e7b28edee5/docs/measurements/static-bitfields-601-2026-10-27)
compares `92c025e7` with `268c6a85`, Clang 23.1.1, `-O3 -march=native`,
without debug information, on CPU 2 with one lane. Each whole-process case has
24 samples per variant, warmed case-specific output files, two fresh tmpfs
executable copies and a second baseline as its A/A control. Eleven paired
stage-clock samples measure publication independently of parsing and checking.

For 65,536 records with two 31-bit fields, whole compilation takes
47.473 → 42.322 ms (10.85% less time), with 13.38% fewer user instructions;
publication takes 8.535 → 3.423 ms (59.90% less time). For 16,384 packed
128-bit records, whole time falls 13.37% and publication time 72.19%.
Peak RSS is essentially unchanged. SQLite and the configured Lua sources'
whole-time changes lie within their A/A copy ranges; this is no claim of an
ordinary-corpus, cold-cache or multicore speedup. The record retains all
identities, commands, raw reports, guarded tests and disassembly, and separates
an excluded pilot whose shared output file biased some whole-process timings.
Materialized payloads are constructed directly in an unpublished suffix of
`bytes`, with no second payload copy when atoms are published. A zero-fill
candidate releases its suffix; a failed batch restores the original length
before returning. When the byte arena cannot fit a candidate, temporary scratch
storage permits its zero-fill classification and exact capacity demand to
complete; a nonzero payload in that storage can never be published. Scratch
otherwise holds metadata proportional to objects and initializer references,
not a second image of every payload. Live input prefixes in either arena remain
valid throughout planning. Literal pools, non-TLS readonly objects and objects
with live relocations already determine their section without scanning bytes.
Other materialized objects use an early-exit 64-byte zero test with bounded
masked tails. Avoiding materialization of explicit-zero aggregates is separate
work in [#596](https://github.com/buster14a/buster/issues/596).

For [#600](https://github.com/buster14a/buster/issues/600), the locked 9700X
comparison of `92c025e7` and `4e21ef63` measured complete single-lane `-c`
processes, with fixed output files, two executable copies and both build
orders (36 processes per build/input). The
[qualification record](https://github.com/buster14a/buster-performance/tree/b9552b4f387d9673dd9e6e55dd4d04d16e51a491/docs/measurements/static-data-600-2026-10-27)
retains all sizes, counters, arena probes, assembly and correctness evidence.

| 64 MiB object | Before, ms | After, ms | Change |
| --- | ---: | ---: | ---: |
| writable array | 27.412 | 22.894 | -16.48% |
| readonly array | 27.426 | 22.858 | -16.66% |
| array with one relocation | 27.354 | 22.912 | -16.24% |

A 64 MiB array's emitter scratch peak fell from 64 MiB + 48 bytes to 48
bytes; readonly/literal/relocation-bearing objects scanned no payload bytes
for classification. Whole `-c` peak RSS stayed about 149 MiB for the arrays:
the ELF writer later constructs its complete output image in scratch while
retained bytes remain live. SQLite's 147.103 to 146.697 ms was within the
observed spread. The 1 MiB writable median rose by 0.054 ms (+2.46%) with
overlapping ranges; the explicit-zero control retained its materialization
and used about 2 MiB more peak RSS. Neither a general small-input speedup nor
an improvement to #596 is claimed. All nineteen source cases matched the
baseline's entire object and Clang's data oracle, and the checker, driver,
Link, object-writer and call-graph qualification groups passed.

## Backend handoff

`c_check_program.c` consumes the typed stream, source control records, asm
constraints and initializer references. `c_check_program_lower.c` translates
supported operations into `Tier0Function` records with explicit conversions,
loads/stores, scaled pointer operations, control flow, locals, calls and ABI
classes. `c_check_program_emit.c` compiles those functions, resolves shared
symbols and publishes code/data together; the functions' lowering, Tier 0
code and encoding run on lanes, into one lane's link
([the lanes guide](function-lanes.md), #151). Backend machine classes never
replace the full semantic `TypeId` in the checked stream. It also gives Tier 0
the unit's access column (`Tier0Function.symbol_access`, docs/tier0.md): a
symbol the unit doesn't define is reached through its GOT slot, since it may be
a shared library's and the direct path makes no copy relocations, and a
thread-local by local-exec where the unit defines it and by initial-exec where
it doesn't.

What the lowering covers past scalars (#145's programs):

- **Local objects.** Local arrays, structs and unions are Tier 0 frame objects
  of their own size and alignment (up to 1 GB each), reached through
  `LOCAL_ADDRESS`. An aggregate's value in the stream is its object's address.
  An array of 16 bytes or more whose declaration has no alignment attribute or
  specifier is aligned to at least 16, as the x86-64 psABI and clang align a
  local array (`type_large_array`, #978); a compound literal keeps its type's.
  Past 16 bytes' alignment, Tier 0 realigns the function's frame
  ([the Tier 0 guide](tier0.md#realigned-frames-136)).
- **Values aligned past 8 bytes.** A scalar local or parameter whose alignment
  (its type's, an aligned typedef's, or its declaration's `_Alignas`) passes 8
  bytes is a frame object of its size and alignment, as a long double's is,
  which `LOCAL`, `STORE_LOCAL` and `SET_LOCAL` of its class read and write
  (Tier 0's `tier0_value_object`), so that the rest of the lowering reads and
  writes it as it would a value's local. A parameter's arrives from its
  register as a struct of it would, or from its 8-byte stack slot, which
  Tier 0's prologue copies into the object (an aligned typedef passes as its
  base, so the slot is aligned to 8 alone).
- **Copies.** Struct and union assignment and initialization up to 256 bytes
  use one bounded `COPY`. Larger nonvolatile copies use one `memmove` runtime call (#597),
  through the same symbol publication, ABI assignment and live-value preservation
  as other runtime calls. The pointers and `size_t` have unsigned long's U64
  INTEGER ABI on the generated SysV x86-64 target; no new type is created after
  shared type classification. The call returns the destination when the
  assignment's value is used, and its return is discarded otherwise. `memmove`
  permits self-assignment. The number of typed entries and machine records is
  bounded independently of the object size, so syntax-sized lowering capacities
  cover this producer's actual demand. Small kept assignments retain the
  destination in a temporary (#440), because `COPY` itself has class `VOID`.
  Zero-size copies still evaluate their operands without touching memory.
  Volatile copies retain the existing inline pieces, including self-assignment's
  accesses; a library routine may skip copying when its pointers are equal.
  The producer follows the copied value through comma, conditional, selected
  and statement expressions, whose lvalue conversions can erase a source's
  volatile qualifier. Its bounded private worklist borrows unused task storage.
  A shared type-facts bit also retains volatile subobjects through by-value
  records, arrays and aligned/atomic wrappers, stopping at pointer pointees.
  Its memoized nonrecursive preparation visits each type and member edge once;
  a bounded scalar/vector member scan skips that preparation when no member
  is volatile. Initializer copies retain the declared destination's qualifiers
  even when a plan's part type is unqualified. Shape and native regressions
  cover destination-only volatility, nested volatile arrays, self-copy,
  initialization/return and nonvolatile pointers to volatile storage.
  The [9700X comparison](https://github.com/buster14a/buster-performance/blob/3d131b17c8b08bc2688da9c929e26ffea01055d2/docs/measurements/aggregate-copies-597-2026-10-27/README.md)
  at compiler `03d79f50` records an 8192-byte discarded assignment shrinking
  from 223 typed entries, 1152 machine records and 6197 text bytes to 6, 11
  and 43, with hot-buffer runtime 52.659 to 23.647 ns per call. Copies through
  262144 bytes compile where the baseline exhausts code capacity from 16384;
  all 18 tested objects through 256 bytes remain byte-identical. Five release
  test groups pass. There is no demonstrated compile-time gain; 257-byte calls
  cost more than the old expansion, and the single-placement SQLite control's
  median increases 0.79% with overlapping ranges. The report preserves these
  limits and the full evidence.
- **Initializers.** A local object's plan is applied in order: the object is
  cleared first unless its first part writes it whole (zeros, or an
  aggregate's copy of the object's size, as `T x = *p;` and `T x = f();`, which
  writes its padding too), then each part's zeros, bytes or value is stored, an
  aggregate's copied. Zeros of up to 128 bytes are stores of 8 down to 1 bytes;
  past 128 bytes (`BUSTER_CHECK_LOWER_FILL_UNROLLED`) they are one call of the
  target C library's `memset`, as clang `-O0`'s code clears them (#1376), the
  whole object's and each zero part's. Each such call is counted with the
  unit's runtime calls when its pools are planned (`check_lower_init_pools`).
  A volatile object, or one with a volatile part, keeps its stores, a loop of
  8-byte stores past 128 bytes. A compound literal of the object's size that
  initializes it whole (`T x = (T){ ... };`, as many literals deep as there
  are) writes its parts into the object itself, as clang's code initializes it
  in place, rather than into its own object, cleared, filled and then copied;
  neither may have a volatile part. The [9700X record](https://github.com/buster14a/buster-performance/tree/c0fddb73e121b6a8a5c30d1364954be2ccc3879e/docs/measurements/zero-initializers-1376-2026-11-06)
  compares `2dc50258` with `ba2e80ab`: `int a[256] = { 0 };` takes 26.3 → 4.3 ns
  a call in Tier 0 and 26.8 → 3.4 ns in Tier 1 (clang `-O0`: 4.3), and
  `struct Big b = (struct Big){ .flags = i };` 19.0 → 2.6 ns (clang `-O0`:
  2.4). Up to 128 bytes the code is unchanged; calling memset below 64 bytes
  would cost 2 to 4.5 times the stores. `sqlite3.c`'s compile time doesn't
  change (+0.0007% instructions); the compiler's own `main.c` makes 171 more
  memset calls and 1.9% less text.
  A whole-object initializer whose one value is a call (`T x = f(...);`)
  can use `x` as the call's hidden result object (#1549), removing the separate
  temporary and copy before either backend sees the stream. The result and
  destination must be the same unqualified record type, with matching sizes,
  no volatile part and enough alignment for the result's canonical ABI type.
  Register returns retain their previous path. The immutable source binding
  column is scanned only across that initializer, 16 tokens at a time on the
  vector path with a scalar tail and twin: any reference to `x`, even inside
  another argument's call, retains the temporary. Bodies with source labels
  or a reference to a known returns-twice function retain it too, because
  reentry can find a destination whose address escaped after an earlier
  initialization. The first candidate scans that body's bindings and
  intrinsic IDs once and caches the latter decision, before any argument
  tasks run, so an initializer's nested `__builtin_setjmp` is covered too.
  The existing declaration pass records `returns_twice` by object when it
  occurs, so a later redeclaration cannot hide an earlier attribute.
  The destination is an explicit argument to this call's lowering, so nested
  argument and callee calls retain their own result objects. Assignments
  continue to evaluate into a separate result and copy afterwards. This is a
  code-shape change; the #1376 measurements above do not measure this change.
  Above 128 bytes of explicit constant payload, dense nonvolatile byte-aligned numeric
  plans use a readonly byte image and one call to the target C library's
  `memcpy` (#598). The call returns the destination and discards it, which is the
  interpreter's pointer-result copy (#1550). The image ends at the last explicit value; omitted trailing
  storage is cleared separately. Images with internal holes larger than their
  payload keep the existing writes, avoiding large readonly zero gaps for sparse
  initializers. This preserves checked string truncation,
  embedded NULs, wide characters, designated overrides and local mutability.
  Pool preparation reads the same immutable by-value volatile-containment facts
  as aggregate-copy lowering. The unit prepares those facts once before pool
  planning; each initializer uses one bounded lookup, without a type-sized
  visited table or graph walk. The many-record regression retains complete
  prefixes using only 256 bytes of plan scratch despite hundreds of unused
  types in the unit.
  A mixed runtime plan retains its ordered parts, pooling an individual string
  part above the same threshold. Small payloads, volatile parts, bit-fields and
  values needing address relocations retain their existing lowering. Pools are
  immutable before function lanes start, have deterministic local symbols, and
  require no generated ISA beyond the ordinary C runtime call ABI.
  The [locked 9700X record](https://github.com/buster14a/buster-performance/tree/a1cfed5a9927bcbf4cca3130943efdf3fcd1af7f/docs/measurements/local-initializers-598-2026-10-27)
  compares `92c025e7` with `b5b679ad`: signed `int[40000]` compiles in
  18.676 → 15.428 ms without stage clocks (−17.4%), emits 680,026 → 96 bytes
  of text plus 160,000 bytes of readonly data, and takes 11,929 → 1,275 ns per
  warm repeated call. Strings above the threshold stay at 100 text bytes,
  14 typed entries and 24 machine records through 32,769 total bytes.
  Small objects are byte-identical and their timing differences are below 1%;
  tiny-input compiler peak RSS rises about 2 MiB, and the frozen SQLite
  whole-object control is 0.88% slower (same-binary A/A 0.20%). Large baseline
  string compiles that exhaust code capacity remain failure observations,
  with no baseline runtime speedup claimed for them. The record includes raw
  samples, identities, native/Clang scalar/vector checks and source-corpus
  receipts, including the successful pinned c-testsuite rerun.
- **C11 atomics (#671).** The C11 builtin family and ordinary atomic lvalues
  share the existing raw-bit native leaves for aligned representations through
  eight bytes, with generic libatomic calls for other layouts. Reads, assignment,
  compound updates and prefix/postfix increments preserve one evaluation of each
  source operand. Atomic storage padding, static size/alignment and addressable
  locals are distinct from their unwrapped C values. The
  [builtin guide](c-builtins-integration.md#atomic-and-terminal-helper-calls)
  records the runtime ABI and remaining value-representation limits.
  Floating min/max under `FENV_ACCESS ON` calls the target's `fminf`, `fmin`,
  `fmaxf` or `fmax` within the retry, as Clang's strict path does, retaining
  native DAZ behavior and requiring `-lm` when linking. Address and RHS
  evaluation remain outside that retry. This does not extend the existing
  floating retry-exception conformance claim.
  Atomic vector loads, stores, exchange and compare-exchange retain vector value
  bytes and their declared atomic ABI, including calls and returns. A vector of 4
  or 8 bytes is read from and written to its plan's buffers, which have 16 bytes
  at least, through their addresses at its own bytes
  (`check_lower_atomic_narrow_vector`), as Tier 0's `LOCAL` of a vector reads
  its object's whole bytes; and a 4-byte one returns as its value alone in xmm0,
  as LLVM returns a `<4 x i8>` or a `<2 x i16>`, which Tier 0 takes as an
  eightbyte's part (#137). Read-modify-write
  updates of atomic vectors are explicitly refused until their retry computation
  has a vector lowering.

- **Calls.** A call through a pointer is indirect (callee 0, the address pushed
  before the arguments), its callee evaluated first. A struct or union argument
  is its address, its size in `argument_sizes`, and Tier 0 passes its bytes
  where `abi_assign` puts them; a struct or union result lands in a frame object
  of the call site's own (`call_result_objects`), whose address is the call's
  value. That object is aligned to the result's canonical type at least, an
  aligned typedef's smaller alignment left out: a callee takes a hidden result
  pointer as so aligned (clang's stores through it may be `movaps`).
- **Structs and unions by value (#137).** A parameter of one is a frame object
  of its size and alignment, which Tier 0's prologue fills. One of more than 8
  bytes whose high eightbyte is NO_CLASS passes as its low eightbyte alone, an
  `i64` or a `double` in clang's code, in a register or, when clang's count of
  registers has one left and LLVM's none, in one 8-byte stack slot, the next
  argument's after it (`abi_assign`'s `BUSTER_ABI_FLAG_LOW`), which Tier 0
  copies as one part (#137). A function that
  returns one in registers returns its bytes' address (`result_size`); one that
  returns it through a hidden pointer takes the pointer as parameter 0, copies
  the bytes there and returns it, and one returned in x87 registers (a struct
  of a `long double`, in st0) returns its bytes' address too. Those larger than
  64 KB, or aligned past 1 GB, are refused. One aligned past 16 bytes passes on a
  stack aligned to it at the call, which realigns the caller's frame.
- **Transparent unions (#147).** An argument, a parameter and `va_arg`'s type
  that is a transparent union passes as the union's first member, as clang's
  code classifies it (`useFirstFieldIfTransparentUnion`; a result keeps the
  union's own classes): `check_lower_passed_as` gives `abi_assign` and
  `abi_va_arg` the member, and a parameter's frame object takes the union's
  bytes from where the member arrives. A first member of an integer, a pointer,
  an enum or `_Bool` passes as a value of its class, extended as its type is (a
  `short`'s sign-extended, as clang's `signext`); a struct, a union, an array,
  a 128-bit integer or an `_Atomic` one passes as the union's bytes, as an
  aggregate of the member's type does (`check_lower_transparent_class`). The
  value comes from the union's object, or from the member a conversion to the
  union selects (`BUSTER_CHECK_CONVERT_TRANSPARENT_UNION`, which the lowering
  refused before), whose value starts the union's bytes, every member having
  the first's size (`check_lower_transparent_value`): an integer's or a
  pointer's bits as the first member's class, a struct's, a union's or a
  complex value's from its address, and any other's (a floating value's, a
  wide one's) through a temporary. So a union of a struct of two floats and a
  struct of two ints passes in xmm0, where its own classes would take a general
  register, and glibc's
  socket calls (`bind`, `connect`, `accept`, `getsockname`, `sendto`,
  `recvfrom` and the rest) take `__SOCKADDR_ARG` and `__CONST_SOCKADDR_ARG`
  with `_GNU_SOURCE`. A union sized or aligned otherwise than its first
  member, by an `aligned` or `packed` attribute of its own, passes as clang's
  code passes it: its registers from the member, and where none is left a stack
  slot of the union's size and alignment (`getIndirectResult`;
  `abi_finish_transparent` records the union in the ABI table's `transparent`
  column, which `abi_assign_call` and its vector twin read), and `va_arg` reads
  the member from a slot of the union's size and alignment. Such a union past
  two eightbytes, or whose first member passes as an aggregate, a pair, a
  vector or in memory, is refused by name. And a union made transparent, by a
  later declaration's attribute, after a function definition or a static
  object's initializer that names a function, whose first member passes
  otherwise than the union does (its registers, its stack or its extension):
  clang's code classifies a function's type where it first meets it, which an
  object's initializer does where clang's code emits the object, and keeps
  that for the type's every use (`check_lower_transparent_late`; clang 23.1.4
  passes `void f(union U)` as the union in a call after the attribute when a
  definition before it met the type, or an initializer such as
  `void (*p)(union U) = f;`). The lowering takes the earliest such place in the
  unit (`check_lower_first_classification`, the first body or an earlier
  initializer that names any function), the first time a lane asks, which
  only such a union makes it do. So a union only made transparent inside a
  body, or after a static object's initializer, which clang's code emits only
  once a function uses the object, or after one that names a function of
  another type, is refused so too, though clang's code would pass it as its
  member. `test-checker`'s programs
  (`c_check_program_transparent_test.c`) run each kind of member value, union
  values, unnamed arguments and `va_arg`, in registers and on the stack,
  against clang -O0's build of the same source, calling the harness's clang
  functions and called by them through pointers, with the registers a
  `short`, an `unsigned short` and a `_Bool` arrive in read as 32 bits; and
  `test-checker-driver` links and runs glibc's socket calls with `_GNU_SOURCE`
  by both paths against clang's build (`c_check_transparent_driver_test.c`).
- **Casts to a union (#2301).** GCC's cast to a union from a member's type is
  clang's `CK_ToUnion`. `check_action_cast` takes the operand after its lvalue
  conversion, an array's or a function's decay included, and finds the union's
  first member of its unqualified type that is no unnamed bit-field
  (`check_action_union_cast_member`, clang's `getTargetFieldForToUnionCast`).
  The operand converts to that member (`BUSTER_CHECK_CONVERT_TO_UNION`), and
  the cast's value is an rvalue of the union, never a constant of the member.
  A vector operand's cast to a union is this cast, not a vector cast. The
  lowering (`check_lower_union_cast`) makes a frame object of the union, stores
  the member's value into its first bytes (copies an aggregate's), and gives
  the object's address as the cast's value. The batch and template fast paths
  leave such a cast to it. The union's other bytes stay as they are, as clang's
  code leaves them. A static object's initializer (`check_init_value`) clears
  the union, then initializes its member from the operand, as clang's constant
  evaluator folds the cast. `-Wgnu-union-cast`
  (`check_warning_graph_union_cast`) is at the cast's `(`, off by default and
  given by `-pedantic`, `-Wpedantic`, `-Wgnu` and itself. A union cast from no
  member's type gets clang's "cast to union type from type 'T' not present in
  union", and a cast to a struct from another type its "used type 'T' where
  arithmetic or pointer type is required", both at the `(`
  (`check_semantic_expression`). A cast to an arithmetic or pointer type (or an
  atomic one) from a record or an atomic cast's rvalue (#2375, #2230) is clang's
  "operand of type 'T' where arithmetic or pointer type is required", at the operand's first token (the getExprLoc of the
  lvalue conversion clang wraps it in, a member access's base too), T its type
  after that conversion: a typedef's sugar kept, a qualified one's dropped
  (`CT ct; (int)ct` is `'struct S'`), a cast's as written. A record's cast to
  its own type (#2375, `check_warning_graph_nonscalar_cast`) is `-Wpedantic`'s
  "C99 forbids casting nonscalar type 'T' to the same type", at the cast's
  `(`, T as written (`'const T' (aka 'const struct S')`), off by default and
  given by `-pedantic` and `-Wpedantic`. Differences left:
  - a function designator's decayed type in that error drops its parameters'
    typedef names, as buster's other diagnostics of a decayed function do;
  - where an error follows at the same expression, clang gives the cast's
    `-Wpedantic` or `-Wgnu-union-cast` warning first, and buster after it: a
    scalar cast of a same-type cast (`(int)(T)t`), and a file-scope
    initializer's "initializer element is not a compile-time constant".

  `test-checker`'s programs (`c_check_program_union_cast_test.c`) run each
  kind of member, static initializers and a transparent union's cast against
  clang -O0's build. `test-checker-driver`'s default-warning sweep compares the
  diagnostics with clang's under `-pedantic`.
- **Aggregate control-flow ends (#670).** A bounded walk of each ordinary
  aggregate-returning body's control graph removes unreachable transfers
  without renumbering labels. A reachable implicit return supplies an address
  for a register result or preserves the hidden result pointer; the value is
  unspecified. Tier 0's aggregate-end rule remains unchanged. Its validation
  also checks emitted unreachable statement tails, which retain the existing
  implicit-return safeguard. A second walk selects constant conditions for
  `-Wreturn-type`, with command-line and pragma severity, `-w` and system-header
  suppression. Return warnings cover aggregate and scalar bodies; hosted
  `main` keeps its implicit zero return. Known ordinary direct calls can return,
  and declared `noreturn` calls and terminating builtins end their source
  paths through unconditional cast, conversion and condition wrappers.
  A terminating path beside a fall-off path selects Clang's "in all control
  paths" wording. Statement-expression regions, assembly, indirect calls,
  conditionally evaluated terminal calls, semantic queries and nonconstant
  enum selectors retain conservative handling. Constant
  switch dispatch follows converted side-effect-free source selector
  values and signed case ranges only in the warning walk; the Tier 0 walk
  retains every dispatch edge. Source/native tests cover register and hidden
  results, branches, switch, loops, labels and defined paths through partial returns.
- **Variadic definitions (#137).** `va_start`, `va_arg`, `va_copy` and `va_end`
  expand to `c_builtin_varargs_lower.c`'s recipes over the function's incoming
  frame ([the Tier 0 guide](tier0.md#incoming-variadic-frames)): at the first
  `va_start`, two anchors give the register save area's and the overflow area's
  addresses, and `va_start`'s offsets follow the named parameters (8 bytes per
  general register, 48 plus 16 per vector register, from the function's own
  signature through `abi_assign`). `va_arg` of a struct or a union copies its
  eightbytes from the save area into a frame object of its own, or finds them
  in the overflow area, as `abi_va_arg`'s plan says; its value is their address.
  `va_arg` of a `long double` reads its 16 bytes in the overflow area, and
  `va_arg` of a vector takes the path of a struct of its bytes, as a 128-bit
  integer's does (a 16-byte one from its XMM register's slot of the save area,
  a wider one in the overflow area, where the call puts it), and loads the
  vector from their address. Named
  arguments aligned past 16 bytes align the incoming area to them, which
  Tier 0's incoming frame takes.
- **Long double (#136's item 8).** Its class is Tier 0's F80, whose values are
  in memory ([the Tier 0 guide](tier0.md#long-double-136s-item-8)): a local or a
  parameter of it is a frame object of 16 bytes aligned to 16, which `LOCAL`,
  `STORE_LOCAL` and `SET_LOCAL` of the class read and write, and a global of it
  is read with `LOAD` from its address and written with `STORE` or `SET`
  through it, the address evaluated first; so are other objects' (`*p`,
  `a[i]`, `s.m`). `x op= v`, `++` and `--` on a global or through a pointer go
  through its address as on memory. Its constants are each function's table
  (`x87_constants`), +0.0 at place 0, which truth tests and conversions to
  `_Bool` compare with. A function with an entry of the class or a long double
  result says so (`Tier0Function.x87`), which the lowering notes as it writes
  each entry. A temporary of the class (a statement expression's value or the
  operands live around it, `?:` without its middle operand, a postfix one's old
  value) is a frame object of its own (`check_lower_temporary`).
  The floating classification builtins on it (`isnan`, `isinf`, `isinf_sign`,
  `isfinite`, `signbit` and `fpclassify`) and `fabsl` are `c_builtin_float_lower.c`'s
  recipes over its argument's frame object, with clang 23's -O0 result for every
  x87 encoding ([the integration guide](c-builtins-integration.md#floating-checking)).
- **The 128-bit integers.** `__int128` and `unsigned __int128` are Tier 0's
  I128 and U128 ([the Tier 0 guide](tier0.md#128-bit-integers-136)), memory
  classes as long double's is (`check_lower_memory_class`): their locals,
  parameters and temporaries are frame objects, and their globals are read and
  written through their addresses. A constant is one `CONST` where its high
  half is its low half's sign or zero extension, else its high half shifted by
  64 and ORed with its low half. The convention passes one as two INTEGER
  eightbytes, which Tier 0 takes as a 16-byte object: an argument's value is set
  into a frame object of its own and passed by its address (`argument_sizes`
  16), a call's result comes back into its result object, which `LOCAL` reads,
  and a function returns one from a frame object of its own by its address
  (`result_size` 16). `va_arg` of one takes the path of a struct of two
  eightbytes, its value loaded from their address. Its division and remainder
  (`__divti3`, `__udivti3`, `__modti3`, `__umodti3`) and its conversions to and
  from `float`, `double` and `long double` (`__floattisf` to `__fixunsxfti`) are
  calls of libgcc's, as clang -O0 makes them: their operands set into frame
  objects of their own and passed by their addresses. Those functions' symbols,
  and `__errno_location`'s, are the runtime's (`CheckProgramLowered.runtime_symbols`):
  the unit's own declaration's where its program has one with linkage, else
  one of the lowering's own after the helpers' (`check_lower_runtime`). The
  direct path's executables take them from `libgcc.a`'s members, which their
  runtime joins as GCC's driver links them (`runtime.c`, `bind.c`). A shift
  by a 128-bit count shifts by its low 64 bits. A bit-field of the type is read
  and written as any other (below), its bits within 8 bytes. A `_BitInt(128)`
  is refused by name. The overflow builtins take 128-bit operands and
  destinations (`c_builtin_lower.c`'s `BUSTER_CBUILTIN_LOWER_WIDE`), each operand
  in a frame object of its own.
- **Half-precision values (#136).** `_Float16` and `__fp16` (IEEE binary16)
  and `__bf16` (bfloat16) are their 16 bits on the integer stack (U16), as
  clang's IR keeps a `half` or a `bfloat` (`c_check_program_half.c`): their
  loads, stores, locals, members, globals, static data and vector elements are a
  16-bit integer's, which keep every bit, a NaN's payload among them. The types
  have no class of their own, so a place that doesn't take them refuses them by
  name ("half-precision values here"). What computes with one converts it
  first. It extends to float exactly: a bfloat16's bits shifted up by 16, a
  binary16's by F16C's VCVTPH2PS or libgcc's `__extendhfsf2`. A float rounds to
  one by VCVTPS2PH (after a VMOVD into the register has zeroed the other lanes,
  so that they raise nothing), by AVX512-BF16's or AVX-NE-CONVERT's
  VCVTNEPS2BF16 (which flushes subnormals, as clang's code does), or by libgcc's
  `__truncsfhf2` and `__truncsfbf2`, as LLVM selects them for the function's
  target. A double and a
  long double round once, by libgcc's `__truncdfhf2` to `__truncxfbf2`. Integers
  convert through float, as LLVM converts them, but a 128-bit integer and a
  binary16 through libgcc's `__floattihf` to `__fixunshfti`, as clang calls
  them, whose infinities and NaNs saturate as `__fixsfti`'s don't.
  Arithmetic is clang's excess precision where the unit's target has no native
  arithmetic of the type (AVX512-FP16 for binary16, AVX10.2 for bfloat16):
  `+ - * /` and unary `+` and `-` of a chain of them compute in float, and the
  chain's value rounds once, where it leaves the chain (a store, a cast, a
  compare, a call's argument, a conditional's or a comma's operand). A compound
  assignment computes in float and rounds once; into a destination of another
  type (an integer, a `_Bool`, a bit-field, the other format) it converts the
  destination's value to float and the result back directly, with no rounding to
  the half-precision type, as clang's promotion types do
  (`check_lower_update_through`). `++` and `--` add one in float. With
  AVX512-FP16, each operator rounds (float's two roundings of one operation
  round as once). A chain of unary operators alone over its operand
  is LLVM's fold of it: a binary16's a sign flip per `-`; a bfloat16's an
  extension, a negation and a rounding where the count of `-` is odd, and the
  value where it is even; and in a function with `FENV_ACCESS` anywhere in it
  (clang then emits all of the function's floating operations as constrained
  intrinsics, outside the pragma's block too) a chain with a `-` is an
  extension and a rounding, which quiets a signaling NaN and raises
  `FE_INVALID`. Compares and
  truth extend their operands and compare floats (truth with +0.0, so that a
  NaN is true). A call passes and returns one in an XMM register's low 16 bits,
  and `va_arg` reads one there; structs of them go as the convention classifies
  them. Their bits move between the stacks by VMOVD where the function's target
  has AVX, or else through a frame temporary. Constants round in software,
  correctly (`check_action_half_round`), and the checker folds conversions to
  the types and negations, unary pluses and compares of their constants, but
  not their arithmetic, which rounds at run time. An integer constant converted
  to one rounds once, as clang folds it, where a conversion at run time rounds
  through float (a `__bf16`'s twice; a conditional's value converts so, as
  clang's does), and so does a float or double constant that the checker leaves
  to the lowering (a vector initializer's element: `_mm_setzero_ph`'s zeros,
  which libgcc's `__truncdfhf2` converted at run time before); but in a function
  with `FENV_ACCESS` none folds, as none of
  clang's constrained conversions does, and a float rounds to a `__bf16` by
  libgcc's `__truncsfbf2` whatever the target. An `_Atomic` one (#137) is its
  type's class too: its atomic loads and stores move its 16 bits through the
  plan's buffers, and its parameters, results and arguments go as the
  convention places its value, in an XMM register's low 16 bits. `__bf16`
  arithmetic where the unit's target has AVX10.2's, arithmetic and comparisons
  of `_Complex _Float16` and `_Complex __bf16`, an `_Atomic` one's update and an
  `_Atomic` integer's update by one ("this operation on half-precision values":
  clang's code adds the operand converted to an unsigned integer there, not as
  the usual arithmetic conversions compute), a `_BitInt`'s past 128 bits
  conversions to and from them, and a conversion to `__bf16` with AVX512-BF16
  but not AVX-512VL are refused by name. Of clang 23's code it doesn't follow five things: a
  conversion between `__fp16` and `__bf16` converts the value through float
  (clang copies the bits one way, and its backend stops the other way); `__bf16`
  under `FENV_ACCESS` extends by its bits as elsewhere (clang's code calls
  libgcc's `__extendbfsf2` with its argument in a general register, which
  libgcc reads from xmm0); a `__bf16` parameter keeps its bits (clang's code
  rounds each one on entry, which quiets a signaling NaN, and flushes a
  subnormal where the rounding is VCVTNEPS2BF16's); a `_Complex __bf16`, a
  `<2 x bfloat>` to clang, keeps its bits as an argument and a result and
  through `?:` (clang's code quiets a signaling NaN there, 0x7f81 coming back
  0x7fc1, and with AVX512-BF16 its `?:` of two flushes subnormals to zero); and a unary `+` alone in a
  function with `FENV_ACCESS` is its operand, as elsewhere, where clang's
  constrained round trip quiets a signaling NaN and raises `FE_INVALID` (the
  lowering takes a `+` as it takes a cast, with no test of its type in the
  tasks' loop). `test-checker`'s
  half-precision programs (`c_check_program_half_test.c`) run every other use
  above against clang -O0's code for x86-64, x86-64-v3, x86-64-v4, znver5 and
  sapphirerapids, by both paths, where the host has each one's features.
- **Vectors (#136).** GNU C's vector types and Clang's extended ones of 4, 8,
  16, 32 or 64 bytes (the intrinsics' `__m64` and `__m128` to `__m512i`, and the
  narrow vectors of their conversions; elements other than `bool`) are Tier 0's
  VECTOR ([the Tier 0 guide](tier0.md#vectors-136)), a
  memory class: their locals, parameters and temporaries are frame objects of
  their bytes (`check_lower_temporary_of`), their globals are read and written
  through their addresses, and each `LOAD`, `STORE` and `SET` of one has its
  bytes as its value, which the class doesn't carry. A function with one says so
  (`Tier0Function.vectors`), from the classes its entries have, which one
  bitmask gathers with long double's and the 128-bit integers'. Their entries
  are AVX-512 forms: the target needs AVX-512F and AVX-512VL, which the lowering
  takes from the preprocessor's target (`CheckProgramOptions.target`, from
  `-march` and the `-m` flags), the unit's and the function's (its target
  attribute's, below), and without them a vector's first entry is refused by
  name. The convention passes a vector in a register as wide as the unit's
  target's (64 bytes with AVX-512F, 32 with AVX), as clang's ABI does, and one
  of 4 or 8 bytes as clang's ABI classes it (4 bytes INTEGER, 8 bytes SSE, but a
  vector of one double MEMORY); a call passes and returns one as the 128-bit
  integers are passed, through a frame object of its bytes. A vector of 4 or 8
  bytes is in an XMM register's first bytes, the rest zero, so that its rows
  are XMM forms, as clang's code of `__m64` is; its floating division divides
  its own elements alone, under a mask of them, so that the zeros past them
  raise no `FE_INVALID` (clang's code divides them one at a time under
  `FENV_ACCESS`). An operator is an X86 entry of its row
  (`c_check_program_vectors.c`): an instruction per operator and element type
  where AVX-512 has one (`+ - * / & | ^ << >>`, the compares through an opmask
  and `VPMOVM2*`'s -1 or 0 elements, negation, complement), with AVX-512BW or
  AVX-512DQ where the row needs them; a scalar operand converts to the element's
  type and is splat into every element, a shift's count at the shifted vector's
  type, and so is a conditional's scalar arm (`check_lower_vector_operand`: the
  operator splats it itself, so that an integer's conversion to a GNU C vector
  of its bytes is a cast's, below, and the tasks' loop asks no cast whether it is
  one). `op=` loads, computes and stores back. Half-precision elements'
  (`_Float16`'s) arithmetic and compares are AVX512-FP16's (VADDPH, VCMPPH and
  the others), which the function's target must have (its extension:
  `X86Target.extensions`), and their negation flips the sign bits. Where the
  unit's target hasn't the extension, clang gives `_Float16` excess precision,
  even in a function whose target attribute adds it: it computes `+ - * /` and
  negation in float and rounds to half precision only where a value leaves the
  chain of those operators (a store, a cast, a compare, a call's argument, a
  conditional's or a comma's operand). One operator's row rounds as clang's does
  (float's 24 bits are at least twice half's 11 and 2 more, so rounding twice
  rounds as once), and a negation's quiets signaling NaNs as clang's float
  conversions do (VCVTPH2PS and VCVTPS2PH after the flip); a chain, an operand
  of those operators that is one of them or a unary plus, is refused by name
  (`check_lower_vector_half_chain`). A unit whose target has the extension
  computes chains in half precision, as clang does. bf16 elements' (`__bf16`'s)
  arithmetic is clang's too (`check_vector_lower_brain_rows`): where the unit's
  target has AVX10.2, AVX10.2's instructions (VADDBF16 to VDIVBF16, which round
  to nearest even, flush subnormals and report nothing) and a negation's sign
  flip; where it hasn't, clang's excess precision computes `+ - * /` and
  negation in float, as its -O0 code does: each operand widened (VPMOVZXWD, then
  VPSLLD by 16), the floats' operation and AVX512-BF16's VCVTNEPS2BF16 rounding
  back (to nearest even, subnormals flushed), which the function's target must
  have. A vector of 64 bytes, whose floats take two registers an operand, past
  a row's 8 records, sets its operands into frame temporaries, whose halves two
  rows read through their addresses, as clang's code reads its operands' halves
  from their slots, and a third joins the halves (VINSERTF64X4); one of 4 bytes
  divides its two elements under a mask of them. Vectors wider than 64 bytes
  use the same rows on register-sized pieces; under excess precision a piece
  has at most 32 bytes so its widened floats fit one register. Their chains are refused as
  half precision's, and so are their compares; a `__bf16` scalar splats by its
  bits. An element is read and written at
  its place in the vector's bytes, an rvalue's after it is set into a temporary
  (`SET_LOCAL`, which leaves nothing on the stack). A cast between vectors of the
  same bytes changes no bits, and so does one between a GNU C vector of 4 or 8
  bytes and an integer of its bytes, a move between their registers
  (`check_lower_vector_bits`: VMOVD or VMOVQ), where an extended vector's cast
  of a scalar splats it. Integer division and remainder, and 8-bit
  multiplication and shifts, which clang computes an element at a time, are
  refused by name.
  Transport vectors (`c_check_program_transport.c`) carry an object's address:
  a vector of `bool`, of 1 or 2 bytes, or of more than 64 bytes. Tiny arithmetic
  reads and writes exactly its elements; wide arithmetic uses register-sized
  pieces. Boolean bitwise operators work on the packed bits and clear padding.
  An extended vector's lanes through a pointer to it, `p->y`, are clang's
  `(*p).y` (`check_vector_arrow`): the source reader decodes a `->` member's
  name as a swizzle only in a unit whose type table has an extended vector
  (`TypeTable.ext_vectors`), growing its list past the `.` members it counted;
  a struct's `->` member is unaffected. Swizzles aren't lowered: `-c` refuses
  both spellings as a member access. The checker's swizzle diagnostics are its
  own words, not clang's ("illegal vector component name", "vector is not
  assignable (contains duplicate components)", "address of vector element
  requested", a `vector_size` vector's "is not a structure or union"): #2096.
  Extended vectors' `!`, `&&` and `||` compare each lane with zero and return
  all-one or all-zero signed lanes. Both operands are evaluated; these vector
  operators do not short-circuit. Half-precision truth uses scalar conversions
  and comparisons when there is no packed comparison at the target, preserving
  floating exceptions. A vector-condition `?:` selects by the condition lane's
  sign bit, or its packed boolean bit, and evaluates both arms. Its operands'
  values are captured before later operands run, including a wide condition
  whose original object an arm overwrites; the omitted middle operand is
  evaluated once. Scalar arms splat at their element type. Non-boolean logical
  operations and selections are bounded to 1,024 elements. Scalar-condition
  `?:` keeps the usual short-circuit behavior. Clang rejects `&&` and `||` of
  boolean vectors, and whole-vector `++` and `--` of either vector kind; the
  checker rejects them too. Compound assignments and increments of an element
  of a boolean vector remain a named lowering refusal because Clang 23
  miscompiles them.
  Element access through a volatile vector lvalue is also refused: Clang uses
  a whole-vector volatile load, and a read/insert/store for an element write,
  which a scalar access at the element's address cannot preserve. Whole-vector
  volatile values and extraction from an already captured rvalue remain supported.
  A vector local or parameter aligned past 16 bytes,
  as the intrinsics' types are, is a frame object of its alignment, for which
  Tier 0 realigns the function's frame
  ([the Tier 0 guide](tier0.md#realigned-frames-136)).
- **`_BitInt(N)` of up to 64 bits (#148).** It takes the narrowest class of 8,
  16, 32 or 64 bits that holds it (`check_lower_type_class`), its values extended
  from N bits in it, as clang stores them (`check_lower_bit_int_shift`). What
  may leave other bits is extended again after it by a pair of shifts
  (`check_lower_bit_int_extend`): a sum, a difference, a product, a left shift,
  a negation, a complement, an update, a conversion to it, and a value from
  memory or a call (an update's old value too), whose bits past N the convention
  leaves unspecified and clang's loads ignore. Tier 0 computes, compares and
  shifts in I32, U32, I64 and U64 alone, so an operation of class I8, U8, I16 or
  U16 (a `_BitInt`'s alone, as C promotes the other narrow types) converts its
  operands to I32 or U32, by their signedness, and its result back
  (`check_lower_computed`), and so does a switch on one, its cases too. A
  constant extends from N bits. An expression of a `_BitInt` goes first to
  `check_lower_bit_int_node`, out of line, by its value's kind, which the
  expansion reads for every expression anyway, and the batch's templates
  (`c_check_program_batch.c`) take none, by its type's fact
  (`BUSTER_CHECK_LOWER_TYPE_BIT_INT`); a program whose type table has no
  `_BitInt` (`CheckLowerWriter.bit_ints`) tests no compare's, update's,
  conversion's or constant's type for one, and its tasks' loop
  (`check_lower_drain`) is a copy without the expansion's and the
  conversions' tests, which `check_lower_drain_bit_ints` has (#137: they took
  3.9 million of `-c sqlite3.c`'s 1.26 billion instructions). The overflow builtins store into a
  `_BitInt` destination its value extended so.
- **`_BitInt(65)` to `_BitInt(128)` (#137).** They take the 128-bit classes,
  I128 and U128, as `__int128` and `unsigned __int128` do: values in memory,
  passed as two eightbytes where the convention says (`_BitInt(65)` to
  `_BitInt(127)` as two `i64`s, which a call may split between r9 and the
  stack), division, remainder and conversions to and from the floating types
  by libgcc's functions. One of fewer than 128 bits is kept extended from its
  width as a narrower one is, by a pair of 128-bit shifts. A constant comes
  folded to its width's bits, which the lowering extends from the width, as a
  narrower one's: a negative one's conversion to `__int128` or compare reads it
  as it is.
  The overflow builtins take one of 65 to 127 bits as an `__int128`'s limbs
  (#1447): its descriptor is its width, its high limb extended from it, and a
  destination's high limb is the result's extended from the width's bits past
  64, which must give it back for the result to fit.
- **x86 builtins (#136, #148).** The intrinsics' `__builtin_ia32_*` calls are
  X86 entries too (`c_check_program_x86.c`): a row per builtin, from a table by
  name, of its family's records with its rule's form, as clang -O0's code
  computes it, its arguments at their prototype's types (an 8- or 16-bit
  integer widened to the 32 bits a general register's field reads) and its
  immediates constants. The families: compares into an opmask and selects under
  one, KMOV, masked loads and stores, two-vector operations and permutes,
  shifts, an element's extraction (of a 256-bit vector's high lane, after
  extracting the lane) and insertion, the elements' top bits, BMI2's PDEP, PEXT
  and BZHI, undefined vectors (zeros), the opmask operations (KAND, KADD,
  KUNPCK, KOR, KXOR, KSHIFTL, and `ktestz`'s, whose row's result the caller
  tests), compression and expansion (into a vector and into memory), masks of
  the elements' top bits and vectors of a mask's, truncating conversions of a
  low element, lanes extracted under a mask and inserted, VALIGN, VSHUFI and
  VGF2P8AFFINEQB by an immediate, VPSHUFD, VPROLD and VPSLLDQ, VPCONFLICT,
  VPTERNLOG, VPERMT2 and VNNI's dot products (VPDPBUSD, VPDPBUSDS, VPDPWSSD and
  VPDPWSSDS), gathers and scatters, VPBLENDVB and VPSHUFBITQMB. And #129's
  intrinsics:
  - the floating selects, which are the integer selects of their elements'
    bytes, and the scalar ones (`selectss_128`, `selectsd_128`: VMOVSS and
    VMOVSD under the mask, over a copy of w);
  - VPMOV's, VPMOVS's and VPMOVUS's truncations under a mask at every width,
    into a register and into memory;
  - the masked moves of their own forms: VMOVUPS, VMOVUPD, VMOVAPS, VMOVAPD,
    VMOVDQA32 and VMOVDQA64 (the aligned ones fault on an unaligned address,
    as clang's code does), the scalar ones (`loadss128_mask` and the others,
    which clang lowers as the vector's masked load or store under the mask's
    low bits) and the expanding loads;
  - the floating compresses and expands, into a vector and into memory;
  - the SSE and AVX-512 operations of two vectors: the packs, averages,
    multiplies, signs and horizontal sums and differences, the permutes by a
    vector, the variable shifts and the shifts by an XMM register's count, the
    AES rounds, GF(2^8)'s products, BF16's conversions, max and min, and the
    SSE compares by the predicates their names spell (VEX's VCMP into
    vectors);
  - one vector's conversions and approximations, and the scalar ones of its
    first element (VRCPSS, VRSQRTSS); FMA's addsub, VPERMT2's and the BF16 and
    IFMA products; the blends by a vector's signs;
  - operations into w under a mask, of one vector (MASK_UNARY: every
    conversion's pair of widths, VGETEXP, VRCP14 and VRSQRT14) and of two
    (MASK_MERGE: VSCALEF, and the scalar ones, whose other elements are a's);
    a constant mask of every lane's bit is the form unmasked, with no opmask to
    load, as clang's code has the headers' intrinsics without a mask, which
    pass -1;
  - compares and tests into an int, from the flags (FLAGS and KTEST): the SSE
    compares (`comieq` and the others, of each kind, the parity giving
    unordered operands' equal and not equal), PTEST, VTEST, KORTEST and KTEST,
    then SETcc of the condition into a scratch register's byte and its zero
    extension into the result, as clang's code has it (`comieq`'s SETNP, SETE
    and AND, `comineq`'s SETP, SETNE and OR);
  - KNOT and the other widths' opmask operations;
  - the general registers' and memory's instructions (SYSTEM): CRC32, BEXTR,
    the FS and GS bases, RDPID, the shadow stack's, the cache lines', FXSAVE,
    the non-temporal and direct stores, PREFETCHI, INVPCID, WBINVD, WBNOINVD and
    EMMS; RDRAND's and RDSEED's steps (the value stored, and the carry an int);
    ADC's and SBB's sums and differences with carries, the carry in set by NEG;
  - VMASKMOV's loads and stores under a vector's signs, VLDDQU, SSE4A's EXTRQ,
    INSERTQ and non-temporal scalar stores, MASKMOVDQU (VPMOVB2M's mask and
    VMOVDQU8 under it) and SHA's messages and rounds of SHA-1;
  - the masked FMAs merged into a, zeroed and merged into c, each rounding
    control's; the scalar conversions to integers, each rounding control's;
    AVX's lane extraction; VRNDSCALE under a mask; bf16's scalar select (by
    KANDN of the mask and 1, as no scalar move of 16 bits is AVX-512F's);
  - the floating reductions of 64 bytes, two rows in clang's order: the
    halvings to 16 bytes, then to the element, the start the last operation's
    first operand where the reduction has one; and AVX512-FP16's of 16, 32 and
    64 bytes, the halvings to 8 bytes in the first row, whose start and result
    are their bits in a general register, moved as an unsigned short's to and
    from the half's U16 (max and min by VMAXSH and VMINSH, which give what
    clang's compare and VMOVSH give);
  - XOP's compares (`vpcomb` to `vpcomuq`), XOP's extension's, by the
    immediate's predicate as clang's code for AVX-512 has them: VPCMP's or
    VPCMPU's opmask by the same predicate in its own numbering, then VPMOVM2's
    vector of it;
  - XOP's own instructions, which clang's code has for every target: VFRCZ's
    fractions (UNARY), the horizontal sums and differences of adjacent
    elements into wider ones (`vphaddbw` to `vphaddudq`, `vphsubbw`,
    `vphsubwd`, `vphsubdq`), the shifts by a vector of counts (`vpshab` to
    `vpshlq`, BINARY's `swap`: the source in ModRM.rm and the counts in
    VEX.vvvv, W0's forms, as LLVM emits them), and the multiply-accumulates
    and VPPERM, whose third vector is the /is4 register (BLENDV's). No host of
    the tests runs XOP, so `test-x86-intrinsics` compares the compiler's code
    of their 42 intrinsics with clang -O0's, instruction by instruction
    (below);
  - AVX-NE-CONVERT's loads (VECTOR_MEMORY's LOAD, its extension's): a bf16
    or a half broadcast into every float element (`vbcstnebf162ps`,
    `vbcstnesh2ps`), and a vector of them's even or odd elements converted to
    floats (`vcvtneebf162ps` to `vcvtneoph2ps`), at 16 and 32 bytes;
  - AVX-VNNI-INT8's and AVX-VNNI-INT16's dot products of bytes and words into
    doublewords (`vpdpbssd` to `vpdpwuuds`, TERNARY's), at 16, 32 and 64 bytes
    in AVX10.2's EVEX forms, which clang's code of AVX10.2's intrinsics has:
    the masked ones of 16 and 32 bytes and those of 64. The headers' unmasked
    ones of 16 and 32 bytes are macros of the builtins, whose VEX forms a
    function with AVX-VNNI-INT8 or INT16 alone would need; there they are
    refused by name. No host of the tests runs either extension, and
    `test-x86-intrinsics` compares them with clang's code too;
  - SHA512's message steps (BINARY's: forms without vvvv, whose reg is a and
    the destination, `vsha512msg1`'s b of 16 bytes) and its two rounds
    (TERNARY's, c of 16 bytes), SM3's message steps (TERNARY's) and AVX10.2's
    SM4 key expansion and rounds of 64 bytes (BINARY's), each extension's
    (`X86Extension`'s SHA512, SM3 and SM4), compared with clang's code by
    `test-x86-intrinsics` as well;
  - AVX10.2's saturating conversions with truncation of floats and doubles to
    doublewords and quadwords, signed and not (`vcvttps2dqs` to
    `vcvttpd2uqqs`, MASK_UNARY's, those of 64 bytes with the exceptions'
    suppression where their last argument asks for it);
  - AVX10.2's bf16 operations: the scale (MASK_MERGE's), the exponent and the
    approximations (MASK_UNARY's), the maximum and the minimum (BINARY's), and
    the compares of the first elements into an int (`vcomisbf16eq` to
    `vcomisbf16neq`, FLAGS', as COMISS's);
  - AVX10.2's conversions to and from 8-bit floats: of halves to bf8 and hf8,
    saturating or not, from one vector (`vcvtph2bf8` to `vcvtph2hf8s`,
    MASK_UNARY's), with a vector of biases (`vcvtbiasph2bf8` to
    `vcvtbiasph2hf8s`, MASK_MERGE's, whose results of 32 and 64 bytes' halves
    are half their bytes, `CHECK_X86_MERGE_W_HALF`) and from two vectors
    (`vcvt2ph2bf8` to `vcvt2ph2hf8s`, BINARY's), and of hf8 to halves
    (`vcvthf8_2ph`); its conversion of two vectors of floats to halves
    (`vcvt2ps2phx`, MASK_MERGE's, with a rounding control at 64 bytes); and its
    dot products of pairs of halves into floats (`vdpphps`, TERNARY's);
  - MOVRS's vector loads (`vmovrsb` to `vmovrsq`, AVX10.2's, VECTOR_MEMORY's
    LOAD) and AVX512-BMM's products of 16-by-16 bit matrices into their
    accumulator, by OR and by XOR (`bmacor16x16x16`, `bmacxor16x16x16`,
    TERNARY's), each extension's (`X86Extension`'s MOVRS and AVX512BMM). No host
    of the tests runs any of these AVX10.2, MOVRS or AVX512-BMM instructions,
    and `test-x86-intrinsics` compares them all with clang's code;
  - the extensions' system instructions (SYSTEM, each of its extension:
    `X86Extension`'s RAOINT to USERMSR): RAO-INT's atomic operations of memory
    (`aadd32` to `axor64`), CLDEMOTE, SERIALIZE, UINTR's (`clui`, `stui`,
    `testui`'s CF and `senduipi`), PTWRITE, WAITPKG's UMONITOR, LWP's control
    block (`llwpcb`, `slwpcb`), MOVRS's integer loads (a byte's and a word's
    through a scratch register's low bytes, zero-extended) and its prefetch,
    RTM's XEND and XTEST's ZF, TSXLDTRK's, AMX-TILE's configuration
    (`ldtilecfg`, `sttilecfg`, `tilerelease`), ENQCMD's ZF and USER_MSR's
    register forms; a flag is SETcc's int, as FLAGS' is. No host of the tests
    has their extensions, or their effects are on the machine's state (a
    transaction's, the user interrupts', the tiles' configuration, MSRs), and
    `test-x86-intrinsics` compares them all with clang's code;
  - AMX internal tile-value builtins: zero, ordinary/streaming/read-shared loads,
    stores, INT8/BF16/FP16/complex/FP8 products and the six AMX-AVX512 row
    conversions. Their 1024-byte `int` vectors use memory objects for copies,
    arguments, hidden results and element access. Active rows are packed using
    their column-byte count as the stride; inactive result-object bytes are
    initialized to zero. Per-unit leaves configure palette 1 with every reserved
    byte zero, execute the real AMX instruction and release tile state. Each
    operation requires its target extension, and row operations also require
    AVX10.2. Ordinary arithmetic on tile-sized vectors remains unsupported.
    `test-checker --program-amx` compares decoded Clang instruction chains with
    the leaves at minimum, interior and full tile shapes, including wrong-width
    and swapped-source negative controls. It separately executes value/ABI
    tests on eligible ordinary x86-64 hosts; the instruction trace does not
    establish native AMX execution;
  - AVX10.2's masked load and store of bf16's first element
    (`loadsbf16128_mask`, `storesbf16128_mask`), as AVX512-FP16's of a half's;
  - VP2INTERSECT's pairs of masks (k2 and k3), each stored at its pointer;
  - AVX512-FP16's, whose rules name its extension (`CheckX86LowerRule.extensions`),
    which the function's target must have too: the half-precision elements'
    exponents, reciprocals and conversions to and from floats, doubles and 16-,
    32- and 64-bit integers under a mask (MASK_UNARY); two vectors' scales and
    the scalars' operations and conversions under a mask (MASK_MERGE); the
    complex products, plain and conjugate (VFMADDCPH, VFCMULCSH and the others,
    of 32-bit lanes), merged as clang's code merges them
    (`CheckX86LowerMerge`: into w, zeroed, into a, a's first lane by the mask's
    first bit, or c's other lanes after its first); the masked FMAs at 64 bytes
    and of the scalars, and the alternating ones without a mask; max and min;
    the scalar select (VMOVSH); the scalar's masked loads and stores (the
    vector's, under the mask, as clang's are); the scalar conversions to
    integers; and `vcomish`, VCMPSH's compare into k1 by the predicate, whose
    bit 0 is the int (SCALAR_COMPARE), as clang's code compares.

  A rounding control, a builtin's last immediate (CheckX86LowerRounding),
  picks the rule's form for 4 (`_MM_FROUND_CUR_DIRECTION`), its form that
  suppresses exceptions for 8 and 12, and its form of embedded rounding in the
  low bits' direction for 8 to 11, as clang's code does.

  A function with an X86
  entry has vectors (`Tier0Function.vectors`) whatever its values' classes. Its
  masks are C's integers (`__mmask16` is an `unsigned short`), and where the
  unit's target has AVX-512 BW and DQ, a row's masks are Tier 0's mask class
  (#179, `check_x86_lower_masks`): a mask operand its records would move into
  an opmask register from its integer is taken there by role, a mask result
  they would move out is left there, their other opmask registers are scratch
  roles, and the moves are Tier 0's conversions from and to the C type around
  the row, so that the function has masks (`Tier0Function.masks`). A row whose
  registers don't map onto roles (VP2INTERSECT's pair, a mask's integer read
  otherwise) keeps the moves, its opmask registers by number. A builtin the table doesn't name is refused by name
  ("this x86 builtin"), and so are one whose instructions the target hasn't and
  a gather's or a scatter's scale other than 1, 2, 4 or 8. Those whose
  instructions name fixed registers, or would write the vector registers
  Tier 0 keeps floating values in across rows (the state's restores), are calls
  of per-unit leaves, as the atomic builtins' are (`c_builtin_x86_helpers.c`,
  planned by `c_check_program_helpers.c`), their arguments and result unsigned
  longs: `__rdtsc`, `pause`, `xgetbv`, `monitorx`, `mwaitx` (whose leaf saves
  rbx, as its unwind table says), `tzcnt_u16`, `_u32` and `_u64` (bsf and cmov,
  the width for 0, as clang's code gives it), `lzcnt_u32` and `_u64` (bsr and
  cmov, the width for 0), the rotates of each width (rol and ror by cl), the
  XSAVE family's saves and restores and FXRSTOR, RDPKRU and WRPKRU, RDPMC,
  RDPRU and RDTSCP, the flags' read and write (pushfq and popfq in a frame of
  rbp, whose CFA the pushes don't move), CLZERO, MONITOR, MWAIT, VZEROALL and
  VZEROUPPER. `sha256rnds2`, whose round keys are xmm0's, is a leaf of
  vectors: its arguments and result are its builtin's 16-byte vectors, which
  its calls pass and return as a function's (frame objects, in xmm0 to xmm2
  and xmm0). Key Locker's leaves take both (`CBuiltinX86HelperInfo.vectors`):
  `aesenc128kl` to `aesdec256kl` their data block in xmm0 between the output's
  and the handle's pointers, `encodekey128` and `encodekey256` their keys, and
  `loadiwkey` its three keys and its control; the wide forms take pointers
  alone. The eight that encrypt or decrypt store the result where ZF is 0 (the
  handle was valid) and zeros where it is 1, as Intel's manual, clang's own
  `keylockerintrin.h` and GCC's code have it, through a `jne` over the zeroing,
  the leaf's branch (`CBuiltinX86HelperInfo.branch`), which branch sizing
  sizes; clang's code (23, and LLVM's main today) stores them the other way
  round. `umwait` and `tpause` put their deadline's halves in edx:eax and
  return CF; `xbegin` sets eax to -1, then begins the transaction with its
  fallback the leaf's `ret` (its displacement 0), so that it returns -1 where
  the transaction runs and its abort status where it aborts. Each leaf
  requires its instructions' features of the output target (XGETBV XSAVE's,
  MONITORX and MWAITX MWAITX's, the others theirs) and its extensions
  (`X86Target.extensions`: Key Locker's KL, and WIDEKL for the wide forms,
  WAITPKG and RTM); the baseline target still takes PAUSE, RDTSC, the BSF- and
  BSR-based counts and the rotates.
  `__builtin_unpredictable` is its operand's value, and `__builtin_assume`
  nothing: its operand isn't evaluated, as clang's isn't. clang's header
  builtins (`_mm_pause`, `_mm_lfence` and the others its headers declare as
  functions, which its calls take for the builtins) are still calls of those
  functions, which no library defines. `test-checker`'s x86 programs
  (`c_check_program_x86_test.c`) call each family's builtins at their
  prototypes' types, the masks' and the vectors' from the call's argument, with
  every result folded into the program's hash, by both paths against clang
  -O0's code for znver5; they run where the host has the rows' features
  (`x86_target_native`), and compile and link elsewhere. The AVX512-FP16
  programs (`c_check_program_fp16_test.c`) are a znver5 unit's functions with
  the headers' `target("avx512fp16")`, and one in a unit whose target has the
  extension (x86-64-v4's, against clang's for sapphirerapids), of modest values
  where the forms clang chooses could choose among NaNs and random bits
  elsewhere; they run where the host has AVX512-FP16 too (not on Zen 5). The
  bf16 vector programs (`c_check_program_bf16_test.c`) are a znver5 unit's
  arithmetic of random bits at every width, which runs where the host has
  AVX512-BF16 (Zen 5 among them); AVX10.2's square roots and multiply-adds, in
  a function with `target("avx10.2")`, and the arithmetic in a unit whose
  target has AVX10.2 (x86-64-v4's and the extension, against clang's for
  diamondrapids), which compile and link, no host of the tests having AVX10.2;
  and their refusals by name. `test-checker --program-vectors` runs them and
  the vector programs alone.
- **Generic vector builtins (#136, #148).** `__builtin_shufflevector`,
  `__builtin_convertvector`, the `__builtin_elementwise_*` functions,
  `__builtin_reduce_*` of integer elements and `__builtin_nontemporal_load` and
  `_store` of vectors are X86 entries of rows of their own
  (`c_check_program_vector_builtins.c`):
  - a shuffle by constant indices is VPERMT2 of its two vectors by an index
    vector, a constant (`Tier0Function.vector_constants`), at the wider of the
    operands' and the result's widths, or nothing where every defined element
    stays at its place. Its second vector is left out where none of its
    elements is read and its evaluation has no effects, and
    `__builtin_nondeterministic_value`'s isn't evaluated;
  - a conversion is VPMOVSX or VPMOVZX at the result's width, VPMOV's
    truncation at the operand's, or nothing between elements of one size. One
    of a shuffle's first elements (a vector's low half, as the intrinsics widen
    one) converts that shuffle's first vector in place;
  - a conversion between 8- or 16-bit integers and floats or doubles goes
    through 32-bit integers, as LLVM converts them: VPMOVSX or VPMOVZX to
    them, then VCVTDQ2PS or VCVTDQ2PD; or VCVTTPS2DQ or VCVTTPD2DQ, then
    VPMOVDB or VPMOVDW's truncation;
  - a conversion with floating elements is the pair's instruction, as clang
    -O0's code is: from 32- and 64-bit integers by their signedness (VCVTDQ2PS,
    VCVTUDQ2PD, VCVTQQ2PS and the rest, DQ's for 64-bit ones), to them truncated
    as C converts (VCVTTPS2DQ, VCVTTPD2UQQ and the rest), between floats and
    doubles, from half-precision elements to floats (VCVTPH2PS) and from floats
    to them (VCVTPS2PH at the current rounding), between them and 16-, 32- and
    64-bit integers or doubles with AVX512-FP16 (VCVTW2PH, VCVTTPH2DQ,
    VCVTPD2PH and the rest), from bf16 elements to floats
    (VPMOVZXWD, then VPSLLD by 16: a bf16 is a float's high half), and nothing
    within one format. A widening one is at the result's width, so that it reads
    a shuffle's first elements in place (`_mm_cvtps_pd`'s);
  - the intrinsics' narrowing conversions into a vector of 4 or 8 bytes
    (`_mm_cvtepi32_epi8` and the rest: a shuffle of a conversion into it with a
    compound literal of zeros) are the conversion, then the shuffle; into one of
    2 bytes (`_mm_cvtepi64_epi8`'s, a vector Tier 0 has no class for) they are VPMOV's
    truncation, which zeroes the rest of its register, where every element past
    the conversion's is a zero of the literal's; else that, then VPERMT2 with
    the literal's bytes as its second table. This requires a nonvolatile literal
    whose initializer values have no effects. A nonconstant or effectful second
    operand is refused by name. Such a literal has no local and isn't evaluated,
    and one evaluated anywhere else is refused by name;
  - max and min, the saturating sums and differences of 8- and 16-bit elements,
    abs, popcount (with BITALG or VPOPCNTDQ), clzg of 32- and 64-bit elements
    (VPLZCNT, with its fallback
    blended in where an element is zero) and the funnel shifts (VPSHLDV and
    VPSHRDV, with VBMI2) are an instruction each, and so are sqrt and fma of
    floats and doubles (VSQRTPS and VFMADD213PS and theirs at the current
    rounding, as llvm.sqrt and llvm.fma are), of vectors and of scalars, whose
    operands are floating values (`check_vector_scalar_row`, with AVX-512F),
    of half-precision elements with AVX512-FP16 (VSQRTPH, VFMADD213PH), and of
    bf16 elements with AVX10.2 (VSQRTBF16, and the fused multiply-add's 213
    form, its operands in clang's registers, with a negated operand's negation
    folded into it as clang's instruction selection folds it: VFMSUB213BF16,
    VFNMADD213BF16 or VFNMSUB213BF16, `check_lower_brain_negations`), where the
    negation alone would round through float;
  - a nontemporal store is VMOVNTPS, VMOVNTPD or VMOVNTDQ of the vector by its
    elements and a nontemporal load VMOVNTDQA, at an address the pointer's
    element type aligns to the vector's bytes, as clang's code needs them; of a
    vector of 4 or 8 bytes, its bytes moved to a general register (VMOVD,
    VMOVQ), then MOVNTI, and a plain load;
  - a reduction halves its vector (VEXTRACTI64X4, VEXTRACTI32X4, VPSHUFD, VPSRLD
    and VPSRLW) and applies its operation at each step, then moves the element
    into a general register, in two rows where that is more than a row's 8
    records. Byte product reductions first zero-extend to 16-bit elements and
    use VPMULLW, keeping the final low byte. A 64-byte input widens its two
    halves separately and multiplies them before the halvings. Row boundaries
    always keep each shuffle and its multiplication together. A vector its
    elements don't fill (an `ext_vector_type` of 3 ints or of 6 shorts, padded
    to a power of two) first gets its operation's identity in the lanes past
    them, as LLVM's widening gives them before clang's code reduces it: a row
    of VPTERNLOGD 0xEA, (v & keep) | fill, by two constants
    (`check_vector_reduce_fill_row`), for the floating sums too; the
    halvings once reduced whatever those lanes held. A floating maximum or
    minimum of such a vector is refused by name: LLVM reduces it in an order
    its width decides (an element at a time once halved to 16 bytes, three
    floats' over their elements alone), which picks the operand a NaN result
    takes its sign and payload from, where the halvings take another;
  - byte bit reversal uses AVX512-BMM's VBITREVB at 16, 32 or 64 bytes. Its
    masked intrinsics use the existing masked byte selection. The opcode and
    result/effect traces are compared with clang by `test-x86-intrinsics`;
    no runtime result is claimed on a host without AVX512-BMM. Without it, bit
    reversal of any element is clang's with GFNI: each element's bytes
    reversed (VPSHUFB by a constant of their places, BW's for elements wider
    than a byte), then each byte's bits (VGF2P8AFFINEQB by the matrix
    0x8040201008040201);
  - ctzg is clang's popcount(~x & (x - 1)) (all ones by VPTERNLOGD, VPADD,
    VPTERNLOGD's and-not, VPOPCNT: BITALG's for 8- and 16-bit elements,
    VPOPCNTDQ's for the others), each element's width for a zero one; its
    second operand's elements replace the zero ones' (VPTESTNM and VPBLENDM, or
    for 8- and 16-bit elements VPCMPEQ with zeros and a masked VMOVDQU8 or
    VMOVDQU16). clzg of 8- and 16-bit elements first reverses each element's
    bits, then uses this count. Its optional fallback tests whether the count
    equals the element width, which identifies exactly the zero inputs and
    keeps the word row within eight records. It needs BITALG and the bit
    reversal's features (GFNI, or AVX512-BMM for byte elements). pdep and pext are each element's BMI2 PDEP or PEXT (no vector
    instruction deposits or extracts bits), of 32-bit zero extensions of 8- and
    16-bit ones, through frame objects of the vectors
    (`check_lower_vector_lanes`). clmul (#148) is each element's carry-less
    product's low bits: with AVX-512VL and VPCLMULQDQ, where the elements'
    64-bit zero extensions take 64 bytes at most (`check_vector_clmul_packed`),
    VPMOVZX to quadwords, VPCLMULQDQ of each 16 bytes' first quadwords (0x00)
    and second ones (0x11), VSHUFPD by 0 for their low quadwords (clang's
    VPUNPCKLQDQ, whose EVEX forms `x86_semantics.c` doesn't cover) and VPMOV
    back to the elements, as clang computes 64-bit elements' (an element's
    product's low bits are its operands' bits' alone); else element by element
    through frame objects, as pdep's, VMOVD or VMOVQ into a vector register,
    VPCLMULQDQ and back (EVEX's with AVX-512VL and VPCLMULQDQ, else VEX's, of
    AVX and PCLMUL);
  - the masked memory builtins (#148, `check_vector_masked_row`) are clang's
    llvm.masked.* code with AVX-512: the mask, a vector of bools (its object's
    address, as a transport vector's value is), is its bits, loaded and cleared
    past its lanes where the instruction has more (a vector of 2 or 3 lanes, or
    of 4 or 8 bytes), moved into k1 by KMOVW (KMOVD or KMOVQ past 16 lanes); then
    one instruction under k1: a load's VMOVDQU8, 16, 32 or 64, zeroing the lanes
    k1 leaves or merging into the passthru's; an expanding load's VPEXPANDB, W,
    D or Q; a store's VMOVDQU and a compressing store's VPCOMPRESS; a gather's
    VPGATHERDD, DQ, QD or QQ by its elements' and its indices' sizes, into zeros
    or the passthru, its indices of 8 or 16 bits sign-extended first (VPMOVSX),
    as a GEP's are, scaled by the element's bytes; a scatter's VPSCATTER, scaled
    by the index's element's bytes, as clang 23's code has it (its GEP's
    element type is the index's, `CGBuiltin.cpp`);
  - the floating functions clang -O0's code computes by instructions are clang's
    sequences, operand for operand, so that NaN payloads and zeros' signs are
    clang's (`check_vector_floating_row`, of vectors of floats and doubles, and
    `check_vector_floating_scalar_row`, of float and double scalars, with
    AVX-512F): the roundings VRNDSCALE (floor 9, ceil 10, trunc 11, rint 4,
    nearbyint 12, roundeven 8; a scalar's of a register holding it alone,
    zeros past it, VRNDSCALESS being no form `x86_semantics.c` covers);
    round's x + copysign(0.5's predecessor, x), truncated, but under
    `FENV_ACCESS`, where clang computes the constrained round, the C library's
    round as the functions below are called (`check_lower_strict_round`, #934's
    review: the addition rounds by MXCSR's mode, which takes 0.5 to 0 under
    `FE_DOWNWARD`, and raises inexact); canonicalize's x *
    1.0; copysign's VPTERNLOGD by the magnitude mask; maxnum and minnum's VMAXP
    or VMINP of y and x and y where x is a NaN; maximum and minimum's of x and
    y with VPTERNLOGD's sign fix and x where x is a NaN; maximumnum and
    minimumnum's the same, but y where x is a NaN and x where y is; and ldexp's
    exponents converted by their bits' signed values (VPMOVSX first for 8- and
    16-bit ones, VCVTDQ2PS/PD, or VCVTQQ2PS/PD with DQ), then VSCALEFPS/PD,
    a scalar's exponent converted in the typed stream from its promoted type
    as the signed integer of its width. Their constants (the masks, 1.0, 0.5's
    predecessor) are operands past the arguments. max and min of floating
    values are llvm.maxnum and minnum, as clang's are, so they take maxnum's
    and minnum's rows and calls (`check_vector_floating_alias`, #1834); abs of
    them is llvm.fabs, the magnitude mask's AND (VPTERNLOGD 192, VANDPS, or
    ANDPS without AVX), at every target, of scalars and vectors
    (`check_vector_floating_builtin`). bf16 elements keep their own refusals,
    and a long double's abs, which clang computes with x87's FABS, its named
    one;
  - acos ... tanh, atan2, fmod and pow are the C library's functions, as
    clang's code calls them (sinf, sin; sinl of a long double), a vector's an
    element at a time, in element order, through frame objects of the vectors
    (`check_lower_vector_calls`): clang -O0's code makes a vector's calls in its
    scheduler's order, which the code around them changes, so errno after them
    is unspecified. The lowering's tables have room for a vector's calls, an
    element each, beside a node's own (`check_lower_prepare`'s lane calls:
    64 nested sines of a 64-byte float vector make 1,024). A long double's roundings (floorl ... roundl), maxnum and
    minnum (fmaxl, fminl) and ldexp by an int's exponent (ldexpl) are calls
    too;
  - the floating reductions are clang's trees: maximum and minimum halve the
    vector as the integer ones do, combining the halves by llvm.maximum's or
    minimum's sequence, a row a halving (`check_vector_reduce_floating_rows`);
    assoc_fadd adds the halves (VADDP), then the tree to its start value
    (-0.0 without one); in_order_fadd adds each element to the sum, first to
    last, in the typed stream (`check_lower_reduce_in_order`); max and min
    use maxnum and minnum, folding a legal vector's lanes in source order,
    including the first operand's signed zero on a tie. A wider vector first
    combines its low and high halves repeatedly in register-sized pieces,
    before the legal-width reduction, preserving the tree across register
    boundaries (`check_lower_transport_reduce_floating`). Associative sums
    replace unused padding lanes with -0.0; padded maxima and minima retain
    their named refusal. xor reduces as and and or do (VPXORQ).
  - elementwise integer and floating builtins of transport vectors use the
    same rows on disjoint register-sized pieces. An `ldexp` piece has as many
    lanes as fit its widest argument, with each argument advanced by its own
    element width. A 1- or 2-byte operand is padded into private storage for
    a minimum-width row, and only the result's actual bytes are stored.
    Libm functions, pdep, pext and the scalar clmul path read elements through
    the same address/value handoff. An integer scalar's pdep, pext and clmul,
    of up to 64 bits, is one row (`check_lower_scalar_lanes`, #2410): BMI2's
    PDEP or PEXT, or PCLMULQDQ between general and xmm registers, of its
    operands' bits zero-extended to 32 or 64 bits, the result truncated to
    the scalar. A `_BitInt` scalar of up to 64 bits, of any width, takes the
    integer sequences of `check_lower_integer_elements` as a 1-, 2-, 4- or
    8-byte scalar does, a funnel shift's count modulo a width no power of two.
    `__int128` and `_BitInt`s of 65 to 128 bits take the same sequences in
    Tier 0's 128-bit classes: their shift counts are U32 values, a constant
    past 64 bits is built of its halves (`check_lower_sequence_halves`), a
    `_BitInt`'s operands are masked by shifting up and back, and a funnel
    count of a width no power of two is reduced modulo it through its 64-bit
    halves, `(hi % w) * (2^64 % w) + lo % w`. These expansions, ordered floating sums
    and floating reduction trees are bounded to 1,024 elements and preserve
    the ordinary rows' element-type and target-feature refusals.
  - boolean elementwise builtins use packed bit operations at scalar and
    register-sized widths: max and saturating add are OR; min, pdep, pext
    and clmul are AND; saturating subtract is `a & ~b`; popcount and
    bitreverse keep the operand. One-bit funnel shifts have a count modulo
    one and keep their left or right input, respectively, while evaluating
    all three arguments once. Tiny stores preserve neighboring bytes, and
    result padding bits are cleared. Boolean clzg and ctzg retain a named
    refusal because Clang 23.1.4 emits an invalid scalar-to-vector `trunc`
    for them and crashes or produces incorrect code.
  - byte funnel shifts of 16-byte vectors use XOP's VPSHLB with signed byte
    counts, masking the original counts modulo eight. Their two halves are
    shifted separately and combined with VPOR; splitting the complementary
    shift into one and seven minus the count discards that half for a zero
    count. Repeated-byte constants are extra row operands. Distinct halves,
    signed inputs and `_mm_rot_epi8`'s identical halves are checked against
    clang; native execution requires XOP. Other widths and targets lacking
    XOP retain named refusals.

  `__builtin_nondeterministic_value` of a vector or a scalar is zeros, and
  `__builtin_bit_cast` (`check_lower_bit_cast`) is its operand's bits: a
  floating or vector constant the checker folded is its bytes; between integers
  or pointers, or between vectors, the value's bits are the result; between a
  vector of 4 or 8 bytes and an integer, a float or a double of its bytes, a
  move between their registers (VMOVD, VMOVQ); and between an integer and a
  float or a double they go through a frame temporary, as clang -O0's go through
  memory. A `_Complex _Float16` or `_Complex __bf16` bit cast reads or writes
  its four bytes through its frame object, keeping both parts'
  representations, including signed zeros and NaN payloads; a result gets an
  object of its own. The conversions between 8-bit integers and half-precision
  elements, between 8- and 16-bit integers and bf16 elements, to bf16 elements,
  and between half-precision elements and other types than floats without
  AVX512-FP16; the
  floating elementwise functions and reductions of half-precision and bf16
  elements but the square roots and multiply-adds (without AVX10.2 for bf16's,
  without AVX512-FP16 for half-precision ones'), and of half-precision, bf16 and
  `__float128` scalars; a long double's copysign, canonicalize, maximum,
  minimum, maximumnum and minimumnum (x87 sequences in clang's code) and its
  ldexp by a 64-bit exponent; a scalar's roundings, round, canonicalize,
  copysign, maximum and minimum functions and ldexp without AVX-512F; ldexp by
  an exponent wider than 64 bits; carry-less products without AVX and PCLMUL;
  the masked memory builtins without AVX-512F and AVX-512VL, of 8- and 16-bit
  elements without AVX-512BW (expanding loads and compressing stores without
  AVX-512 VBMI2), and gathers and scatters of 8- and 16-bit elements, which no
  instruction gathers or scatters (clang's code takes each element in turn); a shuffle by a vector
  of indices; bit reversal without GFNI (or, of bytes, AVX512-BMM); funnel
  shifts of 8-bit elements outside 16-byte vectors or without XOP; the integer elementwise functions of
  scalars wider than 128 bits (`_BitInt` past 128 bits, #2410), and of scalars, pdep, pext and clmul of more than 64
  bits, pdep and pext without BMI2 and clmul without PCLMUL (clang's code expands them bit by bit); nontemporal builtins of scalars and at underaligned addresses; bit
  casts of other types; and the rows whose extensions the target hasn't
  (BITALG, VPOPCNTDQ, VBMI2, DQ, GFNI, BMI2) are refused by name. Shuffles of
  8-bit elements without VBMI's VPERMT2B, and of 16-bit ones without BW's
  VPERMT2W, run element by element, as the baseline's do (#1834). `test-checker`'s vector builtin programs
  (`c_check_program_vector_builtins_test.c`) call each at every width, those
  of vectors of 1, 2, 4 and 8 bytes and wider than 64 bytes, the narrowing conversions and the floating
  intrinsics as the headers write them, with the masked truncating stores and
  the leaves (monitorx and mwaitx where the host has MWAITX), by both paths
  against clang -O0's code for znver5: the floating functions of inputs with
  both zeros, infinities, quiet NaNs of their own payloads and a signaling one
  among them, and of float, double and long double scalars, with errno after
  each scalar's call of the C library; and
  `test-checker-driver` links a program of MMX's intrinsics through clang's
  `<immintrin.h>` by both paths, whose output must be clang's build's.
- **A function's target attribute (#180).** A function's
  `__attribute__((target("...")))` gives its rows and leaves its own
  instruction set (`CheckProgram.body_targets`, made by
  `check_program_targets`): the static functions' declarations with a target
  or `target_clones` attribute, found by the static objects' pass (scalar per
  declaration, as the pass is), sorted by object with the radix kernel's twins,
  and each body's found by a binary search where its object is in their range,
  the attribute that applies being the first on the definition or a declaration
  before it, as clang merges it. Each string resolves once against the unit's
  options (`c_predefines.c`'s `predefines_function_target`, [the target
  guide](x86-target.md)), and an attribute of the string before it, as a
  header's functions have, is read as that one was. A unit without one, the
  usual one, makes no column. A function of several versions
  (GNU's function multiversioning: `target_clones`, or two strings, which the
  checker keeps apart, #1838) has each `target` version's body compiled with its
  own string's features, and a `target_clones` body with the unit's (below); a declaration-only version with no body is
  still outside that refusal, and so are vector values in a function whose
  attribute turns AVX-512F or AVX-512VL off; the convention stays the unit's, as
  clang's does.
- **Multiversioning's attribute conflicts (#839).** The checker reports clang's
  Sema errors for GNU's function multiversioning at `-fsyntax-only`: a function
  with `target("default")`, `target_clones`, `cpu_specific` or `cpu_dispatch`
  refuses every declaration attribute clang keeps except `used` (for `target`
  alone), `nonnull` and `nothrow`, at the function's name. `target` beside
  `target_clones`, `cpu_specific` or `cpu_dispatch` is the later attribute's
  "not compatible" error with clang's note at the earlier one. A target
  declaration that makes a function multiversioned with an earlier declaration
  checks the earlier declaration's attributes first, at its name, with clang's
  "function multiversioning caused by this declaration" note at the new one.
  That earlier declaration is the one before it in the name's chain, with the
  attributes its own earlier declarations gave it (clang's `OldFD`). A
  declaration that multiversioning refused is left out of the chain, so a
  later version repeats its error with the caused-here note. A redeclaration
  of an existing version (the same target string, or `target_clones` again)
  is merged and not checked, and a new version of a function already
  multiversioned is checked as one (#2135). A repeated `target_clones`,
  `cpu_specific` or `cpu_dispatch` reports clang's cannot-appear-more-than-once
  error with its conflicting-attribute note. `cpu_specific` and `cpu_dispatch`
  are supported in full (#1447, below). `test-checker` compares the errors'
  places with clang's, and `test-checker-driver` compares the errors and notes
  line for line.
- **`target_clones`' strings (#1838).** Each string is a comma-separated list
  of versions, each trimmed, as clang's `SemaX86::checkTargetClonesAttr` reads
  it (`c_check_target_versions.c`): an empty string or one ending in a comma,
  an `arch=` of no x86-64 CPU, or a feature a version can't name (one of no
  multiversioning priority, `c_check_target_versions.inc`, derived LLVM data
  generated by `tools/generate_x86_multiversion.c`) is -Wignored-attributes'
  "unsupported '...' in the 'target_clones' attribute string" at the version
  and drops the attribute; a version listed again is -Wfunction-multiversion's
  "version list contains duplicate entries" at it; a string of several
  versions among several strings is -Wtarget-clones-mixed-specifiers' warning
  at the first (warning ID 117); and a list without `default` is "'target_clones'
  multiversioning requires a default target" at the attribute's name. A version
  is placed at its string literal's quote plus its offset in the trimmed
  string, as clang places it. `test-checker-driver --target-versions-only`
  (`c_check_target_versions_driver_test.c`) compares each unit's whole output
  with clang's by both paths, and the warnings' options.
- **Multiversioning by target strings (#1838).** clang's rules for a function
  multiversioned by `target` and `target_clones` (`CheckMultiVersionFunction`
  and its helpers), in `c_check_target_versions.c`, after the #839 checks:
  - *Versions.* A function becomes multiversioned at its first declaration of
    `target("default")` or `target_clones`, or where a `target` declaration
    follows one of another string. Each version is an entity of its own that
    shares the name, which binding keeps apart
    (`check_input_target_versions_match`), and a declaration of a version's
    string, or of the same `target_clones` list, redeclares it
    (`check_input_cpu_dispatch_redeclare`). A `default` after a plain
    declaration redeclares that one, and so does a `target_clones` declaration
    or a plain one after `target_clones`.
  - *Errors,* each clang's, in its order: "'main' cannot be a multiversioned
    function"; "function declaration cannot become a multiversioned function
    after first usage" (with its note where `target` caused it); "multiversioned
    function must have a prototype" and "... has a different return type"
    (inline specification, linkage); "function multiversioning doesn't support
    feature 'X'" (or architecture: an `arch=` no `__builtin_cpu_is` takes, a
    negated feature, one of no multiversioning priority), for the earlier
    declaration's too, with the caused-here note; "multiversioned function
    redeclarations require identical target attributes" (strings of the same
    parse); "function declaration is missing 'target' attribute in a
    multiversioned function" (also at an earlier plain redeclaration, with the
    caused-here note); "multiversioning attributes cannot be combined";
    "'target_clones' attribute does not match previous declaration"; and "no
    matching function for call to 'f'" at a call no visible default version
    reaches (`check_input_target_versions_calls`). A refused declaration is
    invalid (`CheckInputReader.cpu_invalid`), and the later ones' rules skip it.
  - *Uses.* Whether a function was used before a declaration (clang's
    `isUsed`) is read from the binding events: an evaluated use that reached
    its declarations, outside its own body. A unit with a multiversioned
    function is read in source order for it (`CheckInput.target_multiversion`,
    `check_deferred_bodies`).
  - *Tests.* `test-checker-driver --target-versions-only` compares each unit's
    whole output with clang's by the vector path, the scalar one and with the
    bodies deferred.
  - *Differences from clang.* Where several versions are visible, clang
    resolves a reference as an overload set. A call of the wrong arity or
    argument types gets this checker's call errors, not "no matching function"
    with its candidate notes. A reference other than a call, which clang
    resolves only against a function pointer type and a default version,
    is accepted. A different calling convention isn't compared.
  - *Code* (`c_check_program_target_versions.c`, as clang's
    `emitMultiVersionFunctions` and `EmitX86MultiVersionResolver` make it). A
    name's versions form a group (`CheckProgram.target_groups`). A `target`
    version's body is its own symbol, its mangled name: `f` for `default`;
    otherwise `f.`, then `arch_CPU`, then its features sorted by multiversioning
    priority, highest first, joined by `_` (`f.popcnt_sse4.2`). Every reference
    to a version reaches the group's synthetic reference object R, whose
    symbol is `f.ifunc`. A `target_clones` function's own symbol `f` is that
    reference. Its body is compiled once per version, as clang compiles it,
    each copy with that version's instruction set and at that version's name:
    the version, `arch_CPU` for `arch=`, then `.` and its place among the
    versions that aren't `default`, which takes their count (`f.avx2.0`,
    `f.default.2`). A repeated version has no copy of its own.
    `f.ifunc` is another IFUNC at the resolver.
    - `check_program_target_copies` makes each version's target. The
      selection puts the copies after their body
      (`CheckProgram.body_options`), at symbols numbered after the program's
      (`check_program_target_number`).
    - A copy shares its body's locals in the lowering
      (`check_lower_local_body`) and its block-scope statics, as clang's
      clones do.
    - Each copy has its switches' jump tables of its own, as clang's clones
      do (#2247): `check_lower_switch_tables` makes them per body, the copies'
      too, and `check_lower_table_of` finds a table by its body and control.
    - Each copy's code takes its own symbols of the body's labels whose
      addresses it takes (#2247, `check_program_target_number`,
      `check_lower_label_symbol`). The last copy in the options' order keeps
      the labels' own symbols, which the program's data reaches: clang's
      block-scope static `static void* t[] = { &&a };` holds the last clone's
      label addresses, since that clone is emitted last.
    - Each copy plans its asm statements for its own instruction set, as
      clang's clones assemble each template again (#2247): a copy body's
      statements have plans of their own past the statements'
      (`CheckProgramAsms.copy_bases`, `check_program_asm_plan_of`), each
      naming its statement (`CheckProgramAsm.statement`), whose index gives
      `%=` its number and an asm goto's local labels and a helper's symbol
      their names, so that the copies' differ. A label a template defines by
      name is defined once per copy, a duplicate refused as in clang. A naked
      function's body is still compiled once (`check_program_target_copy_refused`):
      its templates are its whole code. A group with a reference, or a
    `target_clones` function the unit defines with external linkage, gets its
    resolver `f.resolver`, a weak function (local in a static group), and its
    IFUNCs of that binding. The resolver calls `__cpu_indicator_init` and
    returns the first version whose condition libgcc's CPU model meets. The
    versions are in clang's stable order by priority (`getFMVPriority`: the
    highest of their features' priorities, doubled, and an `arch=`'s key
    feature's, doubled plus one), highest first. A version's condition is its
    `arch=` (`__builtin_cpu_is`'s test of the vendor, type or subtype; a
    microarchitecture level's bit) and its features' bits. A version of
    neither is taken unconditionally, and a model none matches is a `ud2`.
    An `arch=X` version is chosen by the CPU model's vendor, family and
    model alone, as clang's is, with no test of X's features, though its copy
    is compiled with all of them (the owner's decision, #2355). On a machine
    whose model is X but which doesn't report some of X's features (a VM that
    hides AVX-512, as some hosted Zen 4 runners do), the resolver still
    picks it, and an instruction of a hidden feature traps, as clang's build
    does. A fixture that executes code on hosted runners chooses such a
    version by feature (`avx512f`, which the resolver tests by its bit), not
    by `arch=` (#2346). A
    static group's versions are emitted where its resolver is, as clang emits
    them. A block-scope static in a version is named by the function's name
    where clang emits it (`check_program_target_static_context`). In a
    `target` version, that is the version's mangled name where the function
    is multiversioned by then (`s.avx512f.n`: a static group's deferred
    versions, or a version after another), and its plain name otherwise
    (`f.n`). In a `target_clones` body, it is its first version's
    (`c.avx2.0.n`). The CPU model's symbols are the lowering's, only those a resolver
    reads (`check_lower_finish`).
  - *Tests.* `test-checker-driver --target-versions-only` also compiles a unit
    of versions (`target`'s and `target_clones`', external and static) and one
    of their callers (declarations, calls, addresses) by clang and both paths.
    Every symbol, with its binding, type and definedness, must be clang's, and
    the defined functions' distinct places of code as many as clang's (a copy
    per version); the
    scalar and vector objects must be the same bytes; and every mix of the
    objects linked by cc, the direct path's executables and `-g`'s objects must
    run as clang's build. It then runs the resolvers over 24 models of libgcc's
    CPU model, which the fixture defines: `target`'s versions by their values,
    and `target_clones`' by calling the resolver of a unit of their
    declaration, linked before clang's object of their definition. The
    resolver's result is named among clang's versions' distinct addresses.
  - *Differences from clang's code.* A naked `target_clones` body is
    compiled once, with the unit's features, where clang compiles a copy per
    version (each is valid there). A copy's debug subprogram has no `DW_AT_linkage_name`. The
    resolver is `.text`'s, not a COMDAT section's. Its
    unwind entry is clang's CFI for its frame: the CFA at `rsp + 16` after
    its push, at `rsp + 8` after each return's pop, and at `rsp + 16` again
    where the next test begins (a row program, `check_program_target_atoms`).
    The models' test unwinds through it from `__cpu_indicator_init`.
  - *`arch=` beyond `__builtin_cpu_is`'s names.* An `arch=` processor that
    `__builtin_cpu_is` doesn't name (`corei7-avx`, `skx`: aliases of
    `-march`'s) is clang's `EmitX86CpuIs` default, a test of the model's
    vendor against 0; the models' test covers it. A processor whose key
    feature has no priority (`knm`, the one Sema takes) has none in clang's
    `getFMVPriority` either, where `getFeaturePriority` is unreachable:
    undefined behavior, whose order differs between clang's builds (23.1.4
    sorts it below `sse4.2`, 23.1.6 above every version). It is refused by
    name.
- **`cpu_specific` and `cpu_dispatch` (#1447).** clang's CPU multiversioning,
  checked as clang 23's Sema checks it (`c_check_cpu_dispatch.c`, after the
  #839 checks) and emitted as its code generation emits it
  (`c_check_program_cpu_dispatch.c`). The CPU names are LLVM's processors
  (`c_check_cpu_dispatch.inc`, derived LLVM data generated by
  `tools/generate_x86_cpu_dispatch.c`; THIRD_PARTY_NOTICES.md has its inputs):
  each row's name, mangling character (none for a processor LLVM gives none,
  whose versions clang names `f.`), the mask of libgcc's CPU model a resolver
  tests and the features a version is compiled with.
  - *Attributes.* An unknown name is "invalid option 'X' for cpu_specific" (or
    `cpu_dispatch`) at it; an argument that is no identifier is "'cpu_specific'
    attribute requires an identifier" at the attribute; two names of one
    mangling character (`atom, bonnell`, or two of none) are
    -Wfunction-multiversion's "CPU list contains duplicate entries; attribute
    ignored" (warning ID 99). Each drops the attribute.
  - *Declarations.* A declaration with either attribute is a version
    (`cpu_specific`) or its function's dispatcher (`cpu_dispatch`). One of the
    same kind and CPU list (the names in order) redeclares that version, whose
    definitions are one entity's ("redefinition of 'f'" with its note); one of
    another list or kind is a version of its own, which binding keeps apart
    (`check_input_cpu_dispatch_match`): its own object, though it shares the
    name. A plain declaration before the first version is merged into it, as
    clang merges it. Against the visible declaration before it, as clang's
    `CheckMultiVersionFunction` asks, in its order: "'main' cannot be a
    multiversioned function", "multiversioned function must have a prototype"
    (with the caused-here note where the earlier one lacks it), "function
    declaration is missing 'cpu_specific' or 'cpu_dispatch' attribute in a
    multiversioned function", "multiversioning attributes cannot be combined"
    beside `target` or `target_clones` multiversioning (with a note), then the
    earlier versions most recent first: "'cpu_dispatch' function redeclared
    with different CPUs" and "multiple 'cpu_specific' functions cannot specify
    the same CPU: 'X'" (with notes), then the earlier declaration's attributes
    (#839's caused-here error) and "multiversioned function declaration has a
    different return type" (or inline specification, or linkage). A
    declaration with an error is invalid (`CheckInputReader.cpu_invalid`): the
    later ones' rules skip it, as clang's lookup does, and its body is checked
    all the same. A dispatcher's body is -Wfunction-multiversion's "body of
    cpu_dispatch function will be ignored" at its first statement, after the
    body's own diagnostics, and its falling off the end isn't -Wreturn-type's.
  - *Code.* The versions and dispatchers of a name form a group
    (`CheckProgram.cpu_groups`, from the static objects' target events). A
    version's body is its own symbol, its first CPU's mangled name (`f.M` for
    `core_2_duo_ssse3`), compiled with the unit's features and those every CPU
    of its list has (`check_program_targets`, and code generation's check of
    target features, #775); its other CPUs' names are symbols at the same
    code. Every reference, a call or an address, reaches the group's reference
    object (a synthetic one at its first declaration's attribute name), whose
    symbol is `f.ifunc`: the members' objects take its symbol as a weakref
    takes its target's (`check_program_cpu_number`). A dispatcher's body isn't
    a body of the program's. A unit with the dispatcher emits its resolver
    `f.resolver` (a weak function, local in a static group) and `f.ifunc` and
    `f`, IFUNCs of its binding at it; without one, `f.ifunc` is the undefined
    symbol the calls reach. The resolver calls `__cpu_indicator_init` (the
    lowering makes the CPU model's symbols for it, `check_lower_finish`) and
    returns the first of the dispatcher's CPUs' versions whose mask the model
    has, ordered as clang's `stable_sort` orders them, the most first, with one
    of several trailing versions of no mask kept as clang keeps it and taken
    unconditionally; a model none matches is a `ud2`. The symbols exist before
    an executable's runtime joins (`check_program_cpu_symbols`) and the
    resolver's atom after the functions' (`check_program_cpu_atoms`), so the
    direct path links them as `-c`'s object is linked.
  - *Tests.* `test-checker` compares the errors' places with clang's, and
    `test-checker-driver` (`c_check_cpu_dispatch_driver_test.c`, alone with
    `--cpu-dispatch-only`) each unit's whole output by both paths, the
    warnings' options, the objects' symbols' bindings, types and definedness,
    every mix of clang's and this compiler's objects linked and run, the direct
    path's executables, and the resolver's choice over 28 models of libgcc's
    CPU model, which the fixture defines itself. `test-checker`'s
    `cpu-dispatch-versions` program runs against clang's build.
  - *Differences from clang.* A version of several CPUs is compiled once, with
    the features they share, where clang compiles a copy per CPU (the other
    names are symbols at that copy, also in a static group whose dispatcher
    doesn't name them, where clang emits none). The resolver is `.text`'s, not a
    COMDAT section's, and has no unwind entry. clang refuses a reference other
    than a call where no function pointer type resolves it ("reference to
    multiversioned function could not be resolved") or where several versions
    are visible ("<overloaded function type>" with candidate notes); this
    checker accepts it, as a reference to `f.ifunc`. clang's "function
    declaration cannot become a multiversioned function after first usage" is
    not reported, and a dispatcher's body warning is given even where clang
    dropped the first statement for its error.
- **Complex values (#133, #137).** `_Complex float`, `double` and `long double`,
  GNU's complex integer types (`_Complex char` to `_Complex unsigned
  __int128`) and the complex `_BitInt`s of up to 128 bits that the usual
  arithmetic conversions make, are
  aggregates of their two elements, the real part then the imaginary one
  (`check_lower_complex`): their locals, parameters and temporaries are frame
  objects, the stream carries a value by its object's address, and copies,
  calls, returns, `va_arg` and static data take it as the convention classifies
  it (`float`'s and `double`'s in SSE eightbytes, `long double`'s in memory
  and returned in st0 and st1). An operation computes into a frame object of
  its own from its operands' parts (`check_lower_complex_compute`), each
  operand set into a temporary as soon as it is evaluated, since Tier 0 reads
  an entry's last operand as the class of the entry before it. A real operand
  keeps its type domain, as clang computes it: no imaginary part joins a sum, a
  difference or a product with it, so that signed zeros and infinities come
  out as clang's (`x - z`'s imaginary part is `-z`'s). A product or a quotient
  of two complex values, and a real dividend's over a complex divisor, call
  libgcc's `__mulsc3`, `__muldc3`, `__mulxc3`, `__divsc3`, `__divdc3` and
  `__divxc3`, runtime functions as the 128-bit integers' are, which give the
  values clang -O0's inline product and its NaN recovery give. A complex
  integer type's operation computes in its element's class, or in 32 bits for
  one of 8 or 16 (`check_lower_computed`), each part wrapped back to the
  element's width (a `_BitInt`'s extended from it, as its values are, and so
  are its parts loaded from memory, where another compiler's argument leaves
  the bits past the width as they come), and its product and quotient are
  clang -O0's inline ones,
  in the element's type whatever the operands' domains: (ac − bd) + (ad + bc)i,
  and ((ac + bd) / (cc + dd)) + ((bc − ad) / (cc + dd))i, each product and sum
  wrapped before the division (`check_lower_complex_quotient`; an `__int128`
  part's, or a `_BitInt`'s of 65 to 128 bits, computes in Tier 0's 128-bit
  classes, and its divisions are libgcc's `__divti3` and `__udivti3`). `==` and `!=`
  compare both parts; a value's truth is either part's; a conversion converts
  each part (to a real type the real part, to `_Bool` the truth); a compound
  assignment reads its destination in the calculation's type through its
  address, computes and stores the result back converted; `__real__` and
  `__imag__` read an lvalue's part or an rvalue's object's; and a constant's
  parts are a floating constant's (a `long double`'s in the function's table),
  or a complex integer constant's halves, each its part's bits.
  The direct path's executables take the libgcc functions from `libgcc.a`'s
  members (#483): `test-checker-driver --executable` links products and
  quotients of each type by both paths into the same bytes, which need
  `libc.so.6` alone and print every part's bits as clang's code computes them.
  `_Complex _Float16` uses the same aggregate representation, with two
  binary16 parts in four bytes and one SSE eightbyte for arguments and results.
  Copies, calls, returns, `va_arg`, constants, component access, truth and
  conversions take the parts' half-precision storage and conversion rules.
  Arithmetic and comparisons of this type are refused by name: their native
  FP16 and excess-precision rules and complex runtime calls are not lowered.
  `test-checker`'s half-precision fixtures check all pairs of special binary16
  representations and Clang/Buster calls in both directions, including stack
  arguments and variadic register-save and overflow areas. The `_Complex
  __bf16` that the usual arithmetic conversions make (C can't spell it: a
  `_Complex int` and a `__bf16` give one), a `<2 x bfloat>` to clang, takes the
  same representation and rules, and its fixtures the same checks, with no
  signaling NaN and, through `?:`, no subnormal, which clang's code changes
  (see "Half-precision values"); its arithmetic and comparisons are refused by
  name too (#137). A
  local of a type that no class holds is refused by what its type needs, as
  its values are (`check_lower_type_feature`), rather than as "values of this
  type".
- **`__float128` (#137).** A value is an aggregate's: a frame object of its
  16 bytes, aligned to 16, which the stream carries by its address
  (`check_lower_quad`), so that copies, calls, returns and `va_arg` take it as
  the convention classifies it (SSE, SSEUP: a vector register's 16 bytes,
  which Tier 0 moves with `movups`). Tier 0 has no entry of its arithmetic,
  and as clang -O0's code its sums, differences, products and quotients are
  libgcc's `__addtf3`, `__subtf3`, `__multf3` and `__divtf3`
  (`check_lower_quad_compute`), its comparisons `__lttf2` to `__netf2`'s int
  against 0 (unordered operands make each false but `!=`), a value's truth
  `__netf2`'s against +0.0, and its conversions `__extendsftf2` to
  `__trunctfxf2` for the floating types and `__floatsitf` to `__fixunstfti`
  for the integers, through `int`, `unsigned int`, `long`, `unsigned long`,
  `__int128` or `unsigned __int128`, the first that holds the integer's values,
  a signed one where one does, as LLVM promotes a narrower unsigned
  conversion (`check_lower_quad_conversion`, from the scalar conversions' rare
  branch, an aggregate against a scalar); a negation flips a copy's sign bit,
  as clang's `xorps` does. A compound assignment reads its destination in
  `__float128`, computes and copies the result back, or converts and stores it
  into a real destination; a global without an initializer is 16 zero bytes.
  A real `__float128`'s `__imag__` is a frame object of zeros: the checker
  folds it to a constant zero of its type where its operand has no effect
  (`check_lower_quad_constant` writes a folded constant's bits), and after the
  operand where it has (`check_lower_aggregate_zero`).
  A `__float128` constant keeps its binary128 bits (#1250, `c_check_quad.c`):
  `CheckValue.integer` holds the 16 bytes the object holds, and `floating` a
  long double mirror rounded to odd (the significand's top 64 bits, the last
  one set where a lower one is), whose sign, class, truth and order are the
  value's, and which rounds once more to `float`, `double` or a half format as
  the exact value would. A NaN's mirror holds its fraction's top 63 bits, as a
  narrowing keeps them. A `q`/`Q` literal is `strtof128`'s bits, rounded once.
  An integer converts to `__float128` rounded once, another format exactly (a
  NaN quieted, its payload at the fraction's top), and `__float128` converts to
  an integer by truncating its bits, to long double rounded once from them, and
  to the narrower formats from its mirror. Comparisons, the classification
  builtins, `fabsf128`, `copysignf128`, `fmaxf128`, `fminf128` and their
  `_num` kin, and unary `+` and `-`, fold from the bits, and so do `+`, `-`,
  `*` and `/`: the host's binary128 (libgcc's or compiler-rt's soft-fp),
  rounded once to nearest, with APFloat's NaNs (an invalid operation's positive
  canonical NaN, else the first NaN operand quieted, `check_quad_arithmetic`);
  under `FENV_ACCESS`, their exactness is the bits' (`check_query_quad`).
  Static data and code write the bits. `test-checker
  --program-float-constants`' `float-constants-quad-bits` and
  `float-constants-quad-arithmetic` compare them all with clang -O0's build.
  A `_Complex __float128` constant keeps both parts' bits
  (`check_quad_complex_set`): the real part's in `integer`, the imaginary
  part's in the two storage words its long double parts would hold, which every
  reader takes by `check_complex_component`. `__builtin_complex`, imaginary
  `q` literals, conversions, `==`, truth, `+`, `-`, `~`, and `*` and `/` fold as
  clang 23's evaluator folds them in static data (`HandleComplexComplexMul` and
  `HandleComplexComplexDiv`, Annex G's recoveries among them, each step
  APFloat's), and in code as clang -O0's code computes them: its `*` folds as
  LLVM folds the formula but where both parts are NaNs (its `__multc3`), and its
  `/` by a complex value is `__divtc3`'s at run time. `float-constants-complex-quad`
  compares them with clang -O0's build. An increment or a
  decrement (#147) is `__addtf3`'s or `__subtf3`'s of a constant 1.0, as clang
  -O0's is
  (`check_lower_quad_update_emit`, a postfix one's value a copy of the object
  before it). `test-checker`'s `float128` program, against clang -O0's build of
  it, has each of them, clean under clang's UBSan on every input.
- **Complex values of aggregate parts (#137).** `_Complex __float128`, and
  the complex `_BitInt`s past 128 bits that the usual arithmetic conversions
  make, are frame objects of two parts, the real one then the imaginary one,
  whose parts are values as their element's are: an aggregate's, each part's
  value its bytes' address (`check_lower_complex_parts`), which `__real__` and
  `__imag__` read without a load. Their operations take each part through the
  element's (`__float128`'s, or the `_BitInt`'s a limb at a time), a part's
  result copied into the object (`check_lower_complex_parts_compute`): sums,
  differences, negations and the conjugate, and products with and quotients
  by a real value, a real operand kept real as for the other complex types.
  As clang -O0's code has them, a `__float128` product and quotient of two
  complex values are libgcc's `__multc3` and `__divtc3`, and a `_BitInt`'s
  are the integer types' formulas, (ac − bd) + (ad + bc)i and
  ((ac + bd) + (bc − ad)i) / (cc + dd) whatever the domains, its parts'
  products and quotients libgcc's. Equality and truth go by parts, and
  conversions to and from the other complex and real types convert each part,
  an aggregate part by its address (`check_lower_complex_parts_conversion`).
  Compound assignments convert a complex or real destination to it, compute
  and convert back. The convention passes and returns them in memory; a
  global without an initializer is zeros. clang stores a `_BitInt` part as
  its width's bytes, leaving the bits past it as they were, which the
  operations extend again as they read them. clang's compiled worlds draw
  both (GCC can't spell `_Complex __float128`). `test-checker`'s
  `complex-float128`, `complex-bit-int-past-128-bits` and
  `complex-aggregate-parts` programs, against clang -O0's builds of them, have
  each of them, clean under clang's UBSan on every input.
- **`_BitInt` past 128 bits (#137).** A value is an aggregate's: a frame object
  of its 64-bit limbs, the least significant first, `ceil(N / 64)` of them
  aligned to 8 as clang lays one out, which the stream carries by its address
  (`check_lower_limb_type`), so that copies (through `memmove` past 256
  bytes), calls, returns and `va_arg` take it as the convention classifies it,
  in memory. A result keeps its bits past N extended from N, as clang's stores
  leave them; a compare, a right shift, a conversion and a value's truth
  extend an operand's top limb again as they read it, as clang's loads read N
  bits alone, since another compiler's value or a byte copy may leave those
  bits otherwise. Sums, differences and negations go a limb at a time with
  their carries, the bitwise operations a limb at a time, compares from the
  least significant limb up (the top limbs signed for a signed type), and
  shifts through a buffer of twice the limbs, each limb of the result two
  neighbouring limbs of the buffer shifted (`check_lower_limb_shift`, which
  keeps a count past the width inside the buffer). As GCC -O0's code does
  (clang's expands them inline), products, quotients and remainders are
  libgcc's `__mulbitint3` and `__divmodbitint4`, and conversions to and from
  `float`, `double`, `long double` and `__float128` libgcc's `__floatbitint*`
  and `__fix*bitint`, their precisions -N for a signed type and N for an
  unsigned one. Conversions to and from the other integers take or give the
  low limbs, then the sign's or zeros past them; a conversion between such
  types copies the limbs both have. Compound assignments and increments
  convert the destination to the calculation's type, compute and convert
  back, a postfix one keeping a copy of the old value. A switch on such a
  type (#2220) keeps its cases' limbs (`CheckProgramCase.wide_lower` and
  `wide_upper`, `check_program_case_value`), orders and finds its duplicate
  cases and overlapping GNU ranges by them (`check_program_case_compare`,
  clang's words with the value's decimal digits), and dispatches through a
  chain of compares, never a jump table: a case's limbs XORed with the value's
  and ORed together, a range's two orders against its constants
  (`check_lower_case_limbs`). A global without an
  initializer is zeros. A unit with such a type has room for 64 entries a
  limb for each node that touches one, and 32 a limb for each such switch's
  case (`check_lower_limb_room`), so the stream
  grows with the width as clang's code does: a `_BitInt(1048576)`'s sum and
  product compile, and a `_BitInt(8388608)`'s too, whose frames need more than
  the default 8 MB stack. Their constants fold (#1447, "Constants past 128
  bits" below): a folded value is a frame object of its limbs
  (`check_lower_limb_constant`), and a static one's initializer is its limbs'
  bytes (`check_data_wide_value`), the bits past the width extended as clang's
  are. A bit-field of one is refused by name, and so is a complex one
  ("complex types of _BitInt past 128 bits"). `test-checker`'s
  `bit-int-past-128-bits` and `bit-int-past-128-bits-constants` programs,
  against clang -O0's builds of them, have each of
  them, clean under clang's UBSan on every input. The overflow builtins take
  them (#1447) as clang does, but `mul_overflow`'s of a signed one, which is
  clang's error ("__builtin_mul_overflow does not support 'signed _BitInt'
  operands of more than 128 bits"): the exact result in the signed `_BitInt`
  that holds it, by these limbs' operations ([the builtins'
  guide](c-builtins-integration.md)'s overflow section).
- **Bit-fields.** A bit-field is read and written through the bytes that hold
  its bits and no other member's (C11 makes those other memory locations), as
  pieces of 8, 4, 2 and 1 bytes that form one little-endian unit: a read shifts
  its bits to the top and back, sign- or zero-extending them as its type says;
  a store clears its bits and puts the value's in, and its value is the field's
  as it now reads. `++`, `--` and compound assignment read, operate and store
  the same way, and so do initializer parts with a bit width. A field whose bits
  are spread over more than 8 bytes (a packed one) is refused.
- **Variable-length arrays.** Their objects live on the stack, as clang's do.
  The source reader's declaration handoff retains the half-open append range
  of VLA records introduced by its specifiers and declarator. Only that range
  is associated and given the declaration's scope; reusing a typedef creates
  no new bound or capture. A suspended `typeof` specifier saves the range's
  beginning across its source walk. A definition retains its header's range
  until parameter activation, and deferred-body cuts save it for each lane,
  whose private record table also contains other headers. The original
  declaration-token/body filters still exclude nested prototypes. These ranges
  use scalar indices, require no allocation or padded access, and are empty
  for declarations without new bounds. One-bound declarations visit one record
  each instead of all earlier records (#611); the small, ordered span updates
  retain the existing scalar record layout and stable bound identities.
  Empty ranges still validate variably modified declarations and retain their
  jump-scope metadata. A variably modified `typeof` operand is evaluated where its
   declaration is executed, after the type's bounds and before the declaration's
   initializers; a fixed-type `typeof` operand is not evaluated. In eleven paired fresh-process runs on the locked
  9700X (CPU 2, one lane), 32,000 one-bound typedef declarations compile in
  34.744 ms median instead of 249.305 ms, with 507,587,203 user instructions
  instead of 6,650,980,156. From 16k to 32k, head instructions grow 1.940 times.
  The SQLite control adds 0.0112% instructions and shows mixed, small timing
  changes within process/copy variability. The
  [frozen measurements and target checks](https://github.com/buster14a/buster-performance/blob/b5a850b6171da718b16feb4db341109476e3db19/docs/measurements/vla-record-spans-611-2026-10-27/README.md)
  record exact sources and limits, five byte-identical baseline/head objects
  and their stage receipts ([performance PR #27](https://github.com/buster14a/buster-performance/pull/27)).
  The control stream puts a `STACK_SAVE` before each block's first array, an
  `ALLOCATE` at each array's declaration (after its bounds) and a
  `STACK_RESTORE` at the block's end and before every `break`, `continue` or
  `goto` that leaves the arrays' scope, restoring the outermost block it
  leaves; a `return` needs none. The lowering keeps the saved position and each
  array's address in locals of their own (Tier 0's `STACK_SAVE`, `ALLOCA` and
  `STACK_RESTORE`). An array's size is its extents' locals, which its bounds
  set once where they were declared, times its constant dimensions and its
  element's size, and the same extents give `sizeof` and the strides of
  pointer arithmetic over a pointer to a variable-length array. One aligned past
  16 bytes (its element's alignment, or its declaration's `_Alignas`) takes its
  alignment less 16 bytes more, and its address rounds up to its alignment,
  `(address + alignment - 1) & -alignment`, where clang's code rounds rsp down:
  `ALLOCA`'s address is a multiple of 16. An array declared in a `for`
  statement's clause is released where the statement ends, after its own
  variables' cleanups, which its condition and its breaks reach; one declared
  directly in a statement expression's block, after the expression's value is
  kept and its block's cleanups ran, as clang's code releases them (#1447). One
  initialized (C23's `= {}`, the only initializer one takes) is cleared whole
  where it is allocated, by memset of its runtime size, as clang's code clears
  it. A bound whose statement expression holds more than expression statements
  (a declaration, an `if`, a jump or a label: `[({ int k = n; k + 1; })]`),
  which the constant reader can't read before the walk has, makes a block-scope
  array variable without that reading (#1447, `check_input_vla_walked`): it is
  no integer constant expression, and the walk checks and evaluates it whole,
  its block's declarations among its statements. The control stream puts such a
  jump out of a block's first array's bounds before the block's `STACK_SAVE`,
  where it needs no restore.
- **Labels as values** (GNU C's, which Lua's interpreter dispatches with).
  `&&label` is a `void *` address constant of the label's own object (its
  token plus one, which no declaration has); a label its function doesn't
  have, or `&&` outside a function's body, is an undeclared name at the
  label's, where clang puts its errors. In `goto *expression` the `*` is
  the goto's delimiter (the parser's child of the goto, with the whole
  expression after it as its own child, a comma's or an assignment's too:
  `goto *p ? &&a : &&b` goes to the conditional's value, as clang's does), and
  the expression converts to `const void *` as an assignment would. Clang's
  jump-scope rules for it: a function with a computed goto and no address of a
  label refuses its first one, and every computed goto must be inside each
  variable-length array's scope and each statement expression that holds a
  label whose address its function takes, since it may go to any of them.
  Those scopes nest, so the labels' innermost ones intersect in one interval,
  which each goto must be in: a pass over the function, not one per pair. The
  control stream marks each such label's place (`CHECK_CONTROL_LABEL`) and a
  computed goto is `CHECK_CONTROL_JUMP_INDIRECT`, with no `STACK_RESTORE`: as
  in clang's code, leaving an array's block that way doesn't release its
  storage, which the function's return does. An address of a label is a
  reference to its function in the selection. The lowering gives Tier 0 each
  function's labels (`Tier0Function.label_entries`), and the emission defines
  a local symbol at each one's first record after branch sizing
  (`.Lbuster_label_N`, of no type), which `GLOBAL_ADDRESS` reaches and a static
  table's pointers point at. The difference of two labels' addresses in a
  static initializer (GNU's relative jump tables, `static const int t[] =
  { &&a - &&b };`, #1447) is a constant of its own kind
  (`CHECK_CONSTANT_LABEL_DIFFERENCE`, `check_init_label_difference`), as
  clang's evaluator keeps an AddrLabelDiff: `&&a - &&b` of any pointer types to
  the labels (divided by no element size, as clang's isn't), through
  parentheses, a unary plus, `__extension__` and conversions to an integer type
  of 32 bits or more that doesn't widen it; any other operation on it, or a
  narrower or wider conversion, is no constant, as in clang. The static data
  writes the labels' distance in their function's code, which the emission
  placed before the data is planned, where an assembler resolves clang's
  `.long .La-.Lb`; the labels are objects the program must have symbols of.
  The two control walks filter the parser's immutable kind/role columns in
  bounded 64-byte blocks and visit exact events in source order: function
  bodies, labels, `__label__` lists, cases, gotos and label addresses in the
  first walk; gotos, label addresses and semicolons in the second. Masked tails need no padding;
  the scalar option retains the original full traversals. Deferred masking,
  branch priorities, body expiry and diagnostic order are unchanged.
  The control reader counts resolved `&&label` expressions while finding their
  targets (`CheckInput.label_address_count`) and reuses the loaded kind/role
  facts in their consumers. A unit without one has no
  addressed-label table or scan in `check_program`. A unit with one filters
  64 kind/role bytes at a time, then marks only the selected targets, with an
  exact scalar twin and a scalar tail requiring no input padding. The table
  belongs to the program pass's scratch arena; an absent table means no label
  needs a `CHECK_CONTROL_LABEL` entry.
  In five paired rounds on the exclusively locked 9700X, exact source `f7af14f`
  against `9de3d30` reduces frozen SQLite user instructions from 1,718,752,973
  to 1,688,577,437 (−1.7557%), wall time from 147.964 to 146.825 ms and peak
  RSS from 598,300 to 598,208 KiB. Required Lua and computed-goto dispatch
  inputs show no consistent wall/RSS regression; dispatch cycles vary in sign.
  The saving includes removing ordinary control-loop work, so it is not an
  exact attribution of #431's older 13.3-million-instruction delta. The
  [paired measurements, causal diagnostic, commands and validation](https://github.com/buster14a/buster-performance/blob/e46965d5ca1a609cec54345ac5ea1f38902560aa/docs/measurements/label-address-cost-449-2026-10-26/README.md)
  retain the limits and raw evidence for #449.
- **GNU's `cleanup` attribute** (#147, `c_check_cleanup_input.c`). As clang
  23's `handleCleanupAttr` checks it: on a block-scope variable of automatic
  storage (on any other declaration clang warns and ignores it, and the
  checker ignores it), its argument is an expression whose names are looked
  up where it is, so the variable isn't visible in its own attribute and an
  undeclared name is an error wherever the attribute is. It must name a
  function: an object, a function pointer or an enumerator is "'cleanup'
  argument 'o' is not a function", and any other expression isn't a function
  either, an error at its first token without a name. Of those the
  attribute's reader takes a name in parentheses, whose name it looks up as
  clang does, and a literal, and refuses the others as malformed (clang puts
  a binary operator's error at the operator, and an invalid expression's
  error is its own). A type's name is the parser's error ("unexpected type
  name"), which skips the rest of the declaration, so the error clang adds
  after it isn't reported. The function has a prototype of one parameter
  ("'cleanup' function 'f' must take 1 parameter"), to which the variable's
  address converts as clang's `IsAssignConvertCompatible` takes it: `void *`,
  `_Bool`, or a pointer to a compatible type whatever either's qualifiers
  ("'cleanup' function 'f' parameter has incompatible type", the spelling of
  clang's message without its two types). A pointer of another signedness or
  to a pointer of other qualifiers, which an assignment only warns about, is
  an error here. An `__auto_type` variable's parameter type is checked once
  its initializer gives the type (`check_input_cleanup_deduced`, from the
  initializer's finish), as clang checks it against the deduced type; the
  rest, and the variable's record, at its declaration. Another count of
  arguments is "'cleanup' attribute takes one argument" on a variable it
  applies to (clang checks the subject first). The last of a declaration's
  cleanup attributes applies (clang warns about the others; the checker
  doesn't). A cleanup's error, as the reader's errors but
  an undeclared name's, ends the unit's checking, where clang goes on to the
  later declarations' diagnostics (`check_input_diagnostic_recover`). Each
  applied one is a
  `CheckInputCleanup` (`CheckInput.cleanups`, in source order: none in a unit
  without one, which reads no declarator's attributes for it), with its
  variable's scope: its block's, or its `for` statement's, to the token after
  the scope's last. A goto or a case must not enter that scope, as a
  variable-length array's (`check_input_vla_finish`), and a computed goto
  must neither enter nor leave it: its labels' first and last must be inside
  the innermost cleanup's scope that holds the goto. The jumps' errors are
  clang's messages, for variable-length arrays too. A body read on a lane
  that declares such a variable leaves the unit to source order.
  The function runs at each exit from the scope, innermost first and in
  reverse declaration order, before the exit's `STACK_RESTORE`: the
  block's end, the `break`, `continue` or `goto` that leaves it (a loop's or
  switch's token stands for where a break or a continue goes, so that a `for`
  statement's own variables' cleanups run once, after its end's label), the
  function's end, and a statement expression's end, after its value is kept
  (`CheckProgramRegion.cleanups`). Each variable's call is written once
  (#1370), as clang's is: `CHECK_CONTROL_CLEANUP` (whose target is the record)
  at its scope's end. An exit of the function's statements is one
  `CHECK_CONTROL_EXIT` instead of a run of calls, which names its first and
  last record (`CheckProgramExit`); its tail (its restore, lifetime ends and
  jump, or its return) follows it, so every walk of the controls sees the
  paths it saw with the calls in place. `check_program_chains` plans each
  body's chains in linear passes (`CheckProgramChain`): the lowering jumps
  from the exit to its first record's call, and after each call a dispatch
  goes on to the destinations that stop there (exits with the same last
  record and the same tail share one, whose tail the first of them keeps in
  place; a lone jump's is its target), to the parent's call for exits that go
  on, and to the next control. Only a dispatch of two continuations or more
  tests an I32 destination slot, which every exit and fall that crosses one
  stores first (a fall 0, at its run's head, past which exits enter): the
  issue's function of early returns stores none, and its calls are its
  variables' count. An exit from a statement expression's statements takes
  the chain through the records within its block, whose calls its region
  lowers with its statements, from the empty evaluation stack the region
  starts with: the dispatch's jumps and the exit's agree on the stack, out of
  the region as within it. An exit that leaves the block's own records (their
  calls run after its END, once its value is kept) or records outside it, one
  through a record whose scope's end a constant switch's kept statements leave
  out, and one whose jump no path reaches keep their runs in place. Code and call rows grow
  linearly in exits plus cleanups, where they grew as their product (85
  cleanups with an early return each made 3,825 calls, and 86 exhausted the
  call rows). An exit keeps only the innermost record whose scope holds its
  target; the planner finds its last record in one preorder sweep, and a
  scope's end and `check_control_cleanup_at` skip closed scopes whole, so the
  planning is linear too. On the 9700X, 10,000 cleanups with an early return
  each compile in 36 ms by Tier 0 (`-c`), and sqlite3.c's compile is
  unchanged ([the measurements](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/cleanup-chains-1370-2026-11-09)).
  The planner stays scalar. It runs only for a body with an exit through a
  cleanup, which no sqlite3.c body has, and it is a few linear passes over
  that body's records and exits: 23–46 ms of check-program at 10,000–20,000
  cleanups, where the parse and the input take as much again. Tier 1 refuses
  such a body past about 800 cleanups (`cause=cfg-allocation`). Main refuses
  the same control flow written with gotos alike: #1384's dense matrices.
  A `return` computes its value first
  (`CHECK_CONTROL_RESULT`: into a temporary of its class, the result object,
  or for a struct or union its bytes copied into a frame object of their own
  or where the hidden pointer points), then the cleanups, then returns it
  (`RETURN` of target `BUSTER_CHECK_CONTROL_KEPT`). But a named return value,
  which clang's code builds in the return slot (Sema's NRVO), is returned
  after the cleanups, which may write it: a struct or union variable of the
  function's result type, automatic, neither volatile nor aligned past its
  type, named alone (in parentheses or not), with every return after its
  declaration in its scope naming it too (`check_control_named`: what
  `Scope::updateNRVOCandidate` and `applyNRVO` decide in that case). The
  lowering's call (`check_lower_cleanup`) takes the variable's address,
  which makes its local a frame slot, and drops the result: an aggregate's
  (in memory or in registers), a 128-bit integer's or a vector's lands in a
  frame object of its own, as a source call's does. The one parameter other
  than a pointer that clang's Sema takes, a `_Bool`, gets the address's low
  bit, as clang's code passes it (it bit-casts the address to the `_Bool`);
  an `_Atomic _Bool`'s, which clang's instruction selection can't take, is
  refused by name. The body's selection references
  the function. `test-checker`'s sections (`c_check_cleanup_test.c`) compare
  54 fixtures' acceptance and first errors with clang's, and run programs of
  each exit, each class of returned value, the named return value's cases,
  statement expressions, switches and the functions' kinds (their parameters
  and results among them) natively against
  clang -O0's build on both paths; `test-checker-driver` compares every error
  of 39 fixtures with clang's, its place and its message, on both paths
  (`c_check_cleanup_driver_test.c`: the jumps' notes aren't the driver's),
  and builds a program of every kind of path through a chain and the issue's
  four shapes of 100 cleanups by both tiers on both paths, run against clang
  -O0's build, and checks that 600 cleanups' object is linear in 300's;
  the deferred reader's fixtures and the selection's take a cleanup too.
- **A block's objects' lifetimes**, for Tier 1's frame (#177, the C driver's
  `--tier1`, `CheckProgramOptions.lifetimes`): `CHECK_CONTROL_LIFETIME` entries
  where a block's statements begin and end (after its cleanups) and before each
  `break`, `continue` or `goto` that leaves it (after the jump's cleanups),
  which the lowering turns into its locals' events
  (`Tier0Function.lifetime_entries`). A block a jump can enter past its opening
  brace has none: [the frame guide](tier1-frame.md)'s "Slot sharing between
  objects".
- **GNU's `weak`, `constructor` and `destructor` attributes** (#147, the
  emission's attribute pass). A weak declaration's symbol is weak, defined
  or undefined; on a declaration without external linkage it is clang's error
  "weak declaration cannot have internal linkage", at the attribute, and on a
  typedef it is ignored. One after its entity's definition is dropped, as
  clang's `checkNewAttributesAfterDef` drops it (with its warning), and so are
  constructors and destructors there (`check_input_attribute_late`, which
  copies a specifier's list before marking one declarator's). A weak
  declaration (or `#pragma weak`) after a use binds as clang's code
  generation does (#1849, the owner's decision on #1447; GCC makes the
  reference weak and says the behavior is unspecified): a definition's symbol
  is weak, a tentative definition's only where a weak declaration comes no
  later than its last tentative definition (`getActingDefinition`), and an
  undefined object's has the binding of the declaration that its first
  emitted reference names, which stays. `check_program_weak_late` keeps the
  weak declarations of the objects that have none on their first declaration
  (a pragma's applies at the pragma), and the selection's
  `check_program_weak_bindings` finds the first reference with the late
  rename's emission walk (eager bodies at their ends, static data at its
  declaration where clang must emit it, deferred bodies and data at the
  unit's end; a body's reference names the latest declaration before it,
  static data's the latest where it is emitted). Where only deferred code
  refers to the object, clang's order of deferred emission decides, which
  `check_program_weak_deferred` replays: a deferred function or deferred
  data is scheduled at the first eager reference to it, or at its definition
  if that comes later (`GetOrCreateLLVMFunction`, `EmitGlobal`); at the unit's
  end each one is emitted in that order and the definitions its own
  references reach first are emitted right after it, depth first
  (`EmitDeferred`), so `main` calling `g()` before `f()` makes the reference
  `g`'s. A body's references are taken by token, data's in initializer order.
  The refusal by name remains only where the replay's record of references
  ran out of room.
  A defined function's constructor and destructor
  each give an entry of the init or fini array: an atom of a pointer to the
  function (`ABS64`), in the order LLVM writes `llvm.global_ctors` and
  `llvm.global_dtors`, stably by priority from the definitions' order (an
  attribute's priority, 65535 without one and past it, as
  `preprocessXXStructorList` clamps it). The latest declaration's attribute
  applies, and in one declaration the first, as clang takes them. An entry
  below 65535 has its priority (`link_add_priority`), which the direct path's
  executable orders and `-c`'s object puts in `.init_array.N` or
  `.fini_array.N` (`check_program_sections`'s descriptors), as clang's object
  has them. The selection takes constructors and destructors as roots.
  `test-checker-driver` (`c_check_symbol_driver_test.c`) compiles a fixture
  of them by clang and both paths, and one of late weak declarations and
  pragmas (with and without `-fcommon`, and the refused shape): each symbol's
  binding must be clang's,
  both paths' objects the same bytes, and the objects, linked by `cc` alone
  and with another object's strong definitions, and the direct path's
  executables, must print what clang's build prints, its constructors' order
  before `main` and its destructors' after it.
- **GNU's `alias`, `ifunc` and `weakref` attributes** (#147,
  `c_check_program_alias.c`). `check_program`'s pass over the static
  declarations finds the file-scope ones with one (clang's code generation
  ignores a block-scope alias), and `check_program_aliases` makes an entry per
  object (`CheckProgram.aliases`), from its first such declaration: a weakref,
  whose target is its argument or an alias attribute's beside it, else an
  ifunc, else an alias. An entry's name is resolved to the unit's object whose
  symbol it names: an object's asm label (from any of its declarations), or
  else its identifier, of a file-scope declaration with linkage that isn't a
  weakref's (a weakref's own name is no symbol); a block-scope static's made
  name is none, as clang finds none. An overloadable function's name is the
  one its symbol gets: its mangled name (`check_mangle_name`, `_Z1fi`) where
  no asm label replaces it, never its identifier. The names are grouped by their bytes'
  hashes and found by their identifiers' ids in two scans of the static
  declarations, 64 tokens at a time, which a unit without these attributes
  never makes. The chains of aliases are followed by pointer jumping (at most
  33 rounds) to clang's aliasee, the first object on the way that isn't an
  alias (`GlobalAlias::getAliaseeObject`), which must be defined: a body, a
  variable's definition or tentative definition, or an ifunc; an ifunc's must
  be a function returning a pointer. An alias's symbol is at its aliasee's
  definition, or an aliased ifunc's resolver's, with that definition's size
  (as LLVM's ELF writer gives `alias = base` the base's section, value and
  size), its own name, binding (weak with `weak`) and visibility, and its
  definition's type, IFUNC where an ifunc is on the way; an ifunc's is an
  `STT_GNU_IFUNC` at its resolver, global whatever its declaration says, as
  clang gives every ifunc external linkage. Both are definitions of the unit
  (`check_program_object_access`: reached directly), which the emission names
  and flags with the other symbols and defines once the static data's atoms
  exist (`check_program_define_aliases`, at the end of
  `check_program_sections`, which every caller that publishes the static data
  runs); until then their symbols are marked as the program's without an
  atom, which lets the static data refer to a local one (`check_data_emit`'s
  local targets). Every alias and ifunc is emitted, as clang emits each, so the
  selection roots its definition (`BUSTER_C_LOWERING_ALIAS_TARGET`), unless
  that is an inline-only function, which isn't emitted: clang's error
  (`BUSTER_CHECK_PROGRAM_ALIAS_UNEMITTED`). A weakref's references are its
  target's: the selection's edges reach the target's definition, and its
  numbering (`check_program_alias_number`) gives a referenced weakref its
  target's symbol, whose undefined symbol is weak where nothing but weakrefs
  refers to it (`BUSTER_CHECK_PROGRAM_ALIAS_WEAK`, clang's
  `GetWeakRefReference`), whatever linkage its declarations give it, and then
  reached through the GOT; or, where no object has the name, a symbol of its own
  named after it, weak and reached through the GOT, which the name's later
  weakrefs share. Code generation's errors, at `-c` and when it links but not
  at `-fsyntax-only` (`check_driver_aliases`, after the selection, in the
  order clang emits the aliases): "alias must point to a defined variable or
  function" and "ifunc must point to a defined function" with clang's notes
  (that the name must be the mangled one, and the name clang gives each
  emitted, referenced or defined declaration of that identifier, an
  overloadable function's mangled one: `check_driver_overload_name`), "alias (or
  ifunc) definition is part of a cycle", "cannot alias a variable with a
  function" and "cannot alias a function with a variable" with the aliasee's
  definition noted, and "ifunc resolver function must return a pointer".
  But an alias or ifunc that names itself stops clang's code generation at
  its declaration, which reports its cycle at once, emits no later one and
  checks no alias: the first such one's error is then the unit's only one.
  `test-ir-source` and `test-ssa-source` define the aliases' symbols too
  (`ir_source_prepare`), and a run in the shared IR's interpreter reaches an
  alias's function through its symbol as through the function's own
  (`ssa_source_child`); an ifunc, whose resolver the interpreter doesn't call,
  leaves a program to run out.
  Sema's, at `-fsyntax-only` too (`check_input_attribute_definition`): a
  definition (a body, an initializer, an external tentative definition, or an
  earlier alias or ifunc of the declaration) that is also an alias or ifunc,
  "definition 'f' cannot also be an alias"; and a weakref with external
  linkage, in a block, without a target or on a typedef. A static variable's
  alias or weakref is no tentative definition, and an alias, ifunc or weakref
  target defines its entity for later redeclarations, functions' and
  variables'. A variable's alias right after a tentative definition (the
  previous declaration one, as clang's `getActingDefinition` requires) is
  clang's "alias definition of 'w' after tentative definition" at its name,
  with "previous definition is here" at the last one
  (`check_input_alias_tentative`), after which clang drops the alias
  attribute and reads on, as the reader does (the report is a recovered one,
  #1849). A redefinition of an alias or ifunc is clang's "redefinition of
  'x'" with its note, and an ifunc on a variable is ignored with clang's
  warning. An alias variable that the unit defines tentatively after the alias
  is the alias's declaration, as clang takes it: the selection leaves out its
  data, and its symbol is at its aliasee's definition (#1849). An alias whose
  thread-local storage isn't its definition's has its definition's symbol
  type, section and value, as clang's object has it; a reference to one, which
  clang's code makes as the alias's own declaration says, is refused by name
  (`BUSTER_CHECK_PROGRAM_ALIAS_REFERENCED`). Clang's code generation warnings
  at the aliases and ifuncs it accepts are `check_driver_alias_warnings`', at
  `-c` and in its order beside the errors (#1849): "alias and aliasee have
  different types" (`-Wattribute-alias`, a function alias whose type isn't
  compatible with its aliasee's definition's, which the note names), "alias
  (or ifunc) will not be in section '.s' but in the same section as the
  aliasee (resolver)" at a section attribute whose name isn't the aliasee's,
  and "alias will always resolve to f even if weak definition of wf is
  overridden" where the immediate target is a weak alias
  (`-Wignored-attributes`), each as the options and pragmas map it. `__has_attribute` and
  `__has_c_attribute` answer 1 for them. `test-checker` runs programs of
  aliases of static functions and objects (thread-local ones too) and chains of
  them, with static data that refers to local ones, ifuncs and weakrefs
  against clang -O0's build (`c_check_program_alias_test.c`), and the
  selection's fixtures of their roots (`c_check_program_select_test.c`);
  `test-checker-driver` (`c_check_alias_driver_test.c`) compiles a fixture of
  every shape by clang and both paths: each symbol's type, binding,
  visibility and definedness clang's, each alias and ifunc at its
  definition's section, value and size, both paths' objects the same bytes,
  run by `cc`'s links alone and with strong definitions of a weak alias's and
  of the weakrefs' names, and the direct path's executables (the dynamic
  PIE's own IFUNCs through its stubs), as clang's build runs; and compares
  every error and note of the errors above with clang's, at `-c` and at
  `-fsyntax-only`, an alias and an ifunc of themselves among them.
- **Asm labels that name the unit's own objects** (#1979,
  `check_program_label_merges` in `c_check_program_alias.c`). Clang's module
  has one global per name, so `double foo(double, double) __asm__("bar")`
  beside a definition of `bar` is that definition: every object whose asm
  label, or identifier (of a file-scope declaration with linkage, or a
  file-scope static), is one name takes one symbol, defined by the name's
  definition, else its first tentative definition, else undefined (one
  `UND` symbol for the name). The static objects' pass lists the declarations
  with linkage and an asm label; their objects are grouped by their labels'
  bytes, and the static declarations are scanned, 64 tokens at a time, only
  where a label is also one of the unit's identifiers or names two objects
  (glibc's redirections rarely do). The other objects take the group's
  object's symbol as the symbols are numbered (`CheckProgram.label_merges`,
  shared by the selection, whose references reach that definition), and a
  merged object's tentative definition defines no storage of its own. Two
  functions' definitions of one name are clang's code generation error
  ("definition with same mangled name 'bar' as another definition", with the
  first one's note), at `-c` and when linking, not at `-fsyntax-only`
  (`check_driver_aliases`). Two variables' definitions of one name are
  refused by name ("a second definition of a variable's symbol name through
  an asm label"): clang keeps the later one's storage alone. An overloadable
  function's identifier counts as its name here, where clang's mangled name
  replaces it. `test-checker-driver --asm-definitions-only` compares the
  objects' symbols with clang 23's and runs them.
  A label that names a symbol multiversioning makes and the unit defines
  (#2204: a `target` version's `f.avx2`, a `target_clones` body's or
  version's `f.avx2.0` or `f.default.1`, a `cpu_specific` version's) is that
  definition too: once the groups' symbols exist, the label's symbol, of an
  object the unit doesn't define, is forwarded to the made one
  (`check_program_label_forwards` in `c_check_program_emit.c`), and
  `check_program_sections` moves its fixups there once every atom (static
  data's too) exists, so neither `-c`'s object nor the direct path's
  executable has an undefined symbol of the name, as clang's hasn't.
  `test-checker-driver --target-versions-only` compares such a unit's object
  with clang's and runs its direct path's executable. A label naming a
  block-scope static's made name (`extern int fn __asm__("f.n")`) is the
  static in clang's module when clang emits the static's function before the
  label's first use, and the static is renamed `f.n.1` otherwise (a definition
  so labelled is one GLOBAL of the static's storage). The emission doesn't
  model clang's emission order, so it refuses any such label by name, the
  second order's too, whose link fails in clang (#2439):
  `test-checker-driver` checks the three shapes on both paths
  (`check_test_driver_static_label`).
- **GNU's `retain` attribute** (#147). A definition with one before it (the
  reader drops one after it) that the unit emits keeps its section: its named
  section's (a section attribute's), or a section of its own, its class's
  section's name and its symbol's (`.data.kept`, `.text.f`, `.bss.z`,
  `.tbss.t`, `.data.function.local`, as LLVM names a retained global's unique
  section), with `SHF_GNU_RETAIN` (`check_program_sections`, from the
  emission's `CheckProgramCode.retained_symbols`), which makes `-c`'s object
  `ELFOSABI_GNU`, as clang's is. Retain alone emits nothing: clang 23 emits
  no unreferenced static with it, so the selection doesn't take it as a root
  (`used` is one). `__has_attribute(retain)` and
  `__has_c_attribute(gnu::retain)` answer 1. `test-checker-driver` compares
  each retained definition's section (name, type, flags and alignment) and
  the object's OS/ABI with clang's object's, and runs the programs, linked
  from `-c`'s objects and by the direct path. The `common` attribute and
  `-fcommon` make common symbols (`BUSTER_LINK_SYMBOL_COMMON`, `-c`'s objects'
  `SHN_COMMON`): [the GNU extensions' table](c-gnu-extensions.md).
- **Switches (#136's item 7).** A switch's value goes in a temporary, and each
  case compares it, as clang -O0's code does for a sparse switch, unless clang
  would build a jump table: then the switch dispatches through one
  (`check_lower_switch_table`), decided before any body is lowered
  (`check_lower_switch_tables`) for each switch of four cases or more, which
  the checked program lists as it writes their controls
  (`CheckProgram.switches`), and found by its control when its body is
  lowered (`check_lower_table_of`). LLVM's rule at -O0
  (`SwitchLowering::findJumpTables`) takes a switch's whole range or nothing:
  four clusters or more (runs of consecutive values that go to one place;
  `case 1: case 2:` with nothing between them is one, as it is LLVM's one
  block) whose cases hold at least a tenth of the range's values (its
  jump-table density). The class must be one of 32 or 64 bits, and the range
  at most 65,536 values (`BUSTER_CHECK_LOWER_MAX_TABLE`; LLVM's limit is
  UINT_MAX). The dispatch, at the switch's first case: a value whose
  difference from the lowest case's, unsigned, is past the last entry goes to
  the default or the switch's end, and any other to the table's address plus
  the entry's 32-bit offset of its label from it (`LOAD` of `I32`, `CONVERT` to
  `I64`, `PTR_ADD`, `JUMP_INDIRECT`), as clang's code goes. Its labels and
  that jump are the function's next table in Tier 0's columns
  (`label_tables`, `table_jumps`, #785), so that the IR's `br_indirect` for it
  goes to its own labels alone. The table is a
  `.rodata` atom (`.Lbuster_switch_N`) of a PC32 fixup per value, to its
  label's symbol (`.Lbuster_case_N`, which the emission defines at the label's
  record as it does `&&label`'s) with the entry's offset as its addend, as
  clang's `.long .LBB - .LJTI` entries are. The tables and their labels are
  numbered in source order when they are decided, so that whatever lane lowers
  a body (#151), they come out as one lane's. Their symbols are the lowering's
  own, after the runtime functions', which `check_lower_finish` numbers once
  they are all known: while the bodies compile, a table's address is a
  provisional symbol after the runtime functions' (`provisional_tables`, which
  the access column has), whose references the emission's merge replaces after
  the runtime functions', in the functions that have tables: a table's final
  symbol falls among the runtime functions' provisional numbers, while theirs
  are below the tables' provisional ones, so that each reference is rewritten
  once. An empty GNU range
  (`case 5 ... 3`) counts for nothing, as clang's Sema drops it. In `-c`'s
  object the tables' and the labels' symbols are temporaries
  (`ObjectOptions.temporary_symbols`), as an
  assembler takes clang's `.LJTI` and `.LBB` labels: left out of its symbol
  table, where a label inside a function would split the function for a
  symbolizer, with the relocations against their sections' symbols. Deciding
  takes a merge sort of the switch's cases, scalar: a pass over a few cases
  per listed switch. On `sqlite3.c`, whose 53 tables hold 2,776 entries, the
  tables added about 431,000 instructions (0.019%) to `-c` before the function
  lanes, and their temporaries 298,000, with no cycles beyond the runs' spread
  (one executable with the tables on and off, on the 9700X), and its shell runs
  13.8% faster
  ([the record](https://github.com/buster14a/buster-performance/blob/6328c2664043f54076850f6c03c5a580ab5bfeb2/docs/measurements/switch-tables-136-2026-10-26/README.md)).
  With the lanes, on one lane, the tables and their temporaries add about
  900,000 instructions (0.04%; one executable with the tables on and off, on a
  Zen 4 laptop's build).
- **Truth tests** of 8- and 16-bit values widen them first, since Tier 0 has
  no compares of those classes.

- **Names.** A block-scope static is named as clang names it,
  `function.name` (its function's asm label, if it has one, for the
  function's name; an asm label of the static's own replaces both), and a
  file-scope compound literal is `.compoundliteral`. A made name that a symbol
  of the unit has already, made before it or given by the source (an
  identifier or an asm label), gets `.N`, as LLVM makes one unique in its module
  (`check_program_emit_made_name`).

Everything else the lowering can't express is refused with
`error: unsupported: FEATURE`, naming the feature: increments of `__float128`
values, bit-fields of `_BitInt`s past 128 bits, inline assembly and so on. The c-testsuite harness counts such a
test as unsupported, not failed.

Builtin module results retain exact argument conversions, runtime operation
identities and effect requirements. Native helpers are ordinary calls with real
symbols and fixups. Missing backend representations or runtime requirements
produce an explicit emission diagnostic. Independently compiled native fixtures
check the supported bridge; source acceptance is a separate gate.

`FENV_ACCESS` preserves source constant-expression proof separately from runtime
dependence on rounding and exceptions. Runtime operations carrying that dependence
cannot be folded away or discarded solely because their source operands are
constant. The fourteen F32/F64 math helpers exchange raw floating-point bits,
including NaN payload and sign. Square root calls the target's real
`int *__errno_location(void)` accessor when errno storage is needed; an existing
compatible declaration is reused and a conflicting declaration is diagnosed.

Diagnostics distinguish a source location from when a complete constraint is
available. Duplicate switch/default checks run after the original switch has
closed, in Clang's constraint order. Earlier complete initializer/asm/control
errors can be reported even when a later declaration or parser error prevents
full checking. Recovery only supplies diagnostic fragments; the original
rejection remains final and cannot emit an object.

Ordinary declaration conflicts retain their reason and previous source location
(`c_check_redefinition.c`). The driver names a redefined variable, function or
parameter; prints both types for a variable whose type changes; and distinguishes
conflicting function types from a static declaration after an external one.
The accompanying note identifies the previous declaration or definition.
The binding rules and rejection boundary are unchanged. Fourteen original
sources compare complete Clang diagnostics in scalar/vector paths and with one
or three body lanes (`c_check_redefinition_driver_test.c`).

The source driver caps its published errors as Clang 23 does (#669): nineteen
ordinary errors by default, followed, only if another error occurs, by the fatal
`too many errors emitted` diagnostic. Explicit `-ferror-limit=N` permits N
ordinary errors before that fatal diagnostic, and zero leaves them unlimited.
Warnings do not consume the error budget; each accepted diagnostic keeps its
macro notes, while the cutoff has neither a location nor notes. Suppressed
warnings (`-w`) do not enter the counts. The final line gives the published
warning and error totals with Clang's singular/plural spelling; clean units print
no summary, and explicit `-fno-caret-diagnostics` suppresses the summary as it
does in Clang. Both output paths share the publication gate, including deferred
preprocessor-history replay and the standalone `-E` command. This bounds reporting rather than the semantic
graph's construction. `-Wfatal-errors` publishes the first command-line or default error, including a warning promoted
to one by `-Werror`, as Clang's fatal error and stops publication there; warnings that remain warnings are unaffected.
A source pragma's `error` mapping stays an ordinary error, while its `fatal` mapping is fatal, as in Clang.
`-Wno-fatal-errors` restores the ordinary budget, and the last of the two options on the command line decides. The
standalone `-E` command and the source driver's preprocessing, parsing and semantic groups all read the option; the
standalone `parse` command reads it too, but keeps its generated-prefix behavior after a preprocessing fatal rather
than parsing the prefix the source driver checks. The preprocessing warnings (`#warning`,
macro redefinition, `#pragma message` and the others) follow their own options and pragmas with their flags
(`pp_warning_flag`, #1790, #1970). An attached note's place
(a redefinition's "previous definition is here") sets the include context the next diagnostic compares with, so
that one in the same header as the warning repeats its "In file included from" lines, as clang's do.

The source driver collects preprocessing, parser and checker groups before
publication (#698). Their order follows expansion-output boundaries and proven
semantic completion points, independently of the diagnostic's located token.
For example, an earlier missing name precedes a later `#error`; runtime shift
warnings complete after their function's closing brace and its lookahead, so
an EOF preprocessing diagnostic can precede that warning. Required constant
evaluation retains its own expression-completion boundary in a sparse warning
fact. Include stacks and macro/explicit notes are rendered with their primary
after the final order is known. The shared gate then counts only published
groups and applies the error budget once. Standalone preprocessing retains
immediate publication.

A nonfatal preprocessing error leaves the source stream available for
diagnostics. A real preprocessing fatal leaves only its produced prefix;
existing complete-declaration/statement recovery can diagnose safe original
fragments there. Either outcome remains a rejected unit and cannot replace an
object file, even if the final budget hides a later error. Diagnostic capture
exhaustion also rejects the unit and publishes one fatal rather than accepting
an incompletely diagnosed warning-only source. Failed history replays keep
queued groups' backing alive or replace a silent failure safely.

After the whole declaration/body resolver has completed, ordinary missing-name
binding reports can retain independent later declarations (#699) and, within a
declaration, every construct that doesn't hold a missing name (#1077), as
clang's `RecoveryExpr` does: the operators, calls, initializers and statements
that hold the name are cut, and their other operands, the statements after them
and the declarations without one keep their graphs and diagnostics. A token's
cut (`check_input_diagnostic_recover`) is the first report at or after its
subtree's first token, in its top-level declaration: a bounded forest pass up
the postorder for the subtrees' first tokens, and a backward scan over the
tokens for each one's next report, only on a rejected unit. A condition's
parenthesis and a case label's colon, whose subtrees hold their statements, are
cut by their own expressions alone, so a name in an `if`'s body or in a case's
statement leaves the condition and the case value, and a switch's duplicate
cases are found among its cases whose values hold no name. Missing operands
acquire no invented value or type. Initializer, assembly and control scans use
the same local completion cuts, and independent tentative-definition
constraints still run at translation-unit end. `test-checker`
(`c_check_undeclared_test.c`) compares fixtures of both orders, the parents the
name silences and the siblings it doesn't with clang, by both paths, with
`-fsyntax-only` and `-c`. A statement expression's statements before its
value's cut their own constructs alone, not the statement expression, whose
value clang keeps, nor what holds it (`({ undeclared; p; }) + 1.5`'s
"invalid operands", #1447): the cut scan steps over them. What remains
conservative: a statement expression whose value's statement holds the name
is still cut, where clang reads it as `void` and checks what holds it
(`({ x; undeclared; }) + 1.5`'s "invalid operands ... 'void'"), and loses the
unused-value warnings of its statements before it; an enclosing
operator's warning is published before the statement expression's own
diagnostics, where clang gives it after them, in valid units too
(`if (x = ({ printf("%s", 1); x; }))`); and a missing name that the
declaration walk reports in an array's bound, a `typeof` operand, a
`_BitInt` width or a file-scope designator is a declarator failure, which
keeps the whole unit's conservative prefix (an enumerator's value's, a
bit-field width's, and a declaration's last declarator's bound's or
`typeof`'s are recovered: below). A
rejected unit's later warnings, its runtime shifts' (`x << 40`) among them,
and those of a unit with other reports (a `break` outside a loop, a
declaration conflict), come from the reparsed view of "Semantic error
recovery" below, whose gaps its "later phases are left" lists.
Type-query/declarator failures, unsupported pragmas, resource failures and
unfinished resolver states retain the conservative prefix. Repaired/deferred
views with skipped bodies do not enable this new binding recovery. A successful
deferred merge has the same complete unit as source order and uses the same cuts.
All these views remain
diagnostic-only and preserve the original source rejection.

#### Declarator errors' words (#1447)

The declarator's errors that used to read "invalid or unsupported declaration"
take clang's words and places:

- **A function returning an array or a function**, at its `(`: "function
  cannot return array type 'int[3]'" or "function cannot return function
  type 'int (void)'"
  (`CHECK_INPUT_FUNCTION_RETURN`).
- **A negative constant bound**, at the bound: "'a' declared as an array with
  a negative size", or "array size is negative" in a type name
  (`CHECK_INPUT_ARRAY_NEGATIVE`, `check_input_vla_bound`).
- **A bound of another type than an integer's** (#2170), at the bound: "size
  of array has non-integer type 'double'" (`CHECK_INPUT_ARRAY_NON_INTEGER`).
  A constant's (`int a[1.5]`) and a variable array's (`int a[d]` of a
  `double d`) both get it, and the declaration recovers as a negative bound's
  does. A member's, as a member's negative bound, still stops the unit.
- **An array of functions**, at its `[`: "'a' declared as array of functions
  of type 'F' (aka 'int (void)')" (`CHECK_INPUT_ARRAY_FUNCTIONS`).
- **An incomplete element**, at its `[`: "array has incomplete element type
  'struct S'", with the tag's "forward declaration of 'struct S'" note
  (`CHECK_INPUT_ARRAY_INCOMPLETE`). It is the type table's own refusal
  (`TYPE_ERROR_ELEMENT`), found before it.
- **A variable of type `void`**, at its name: "variable has incomplete type
  'void'". An `extern` one without an initializer, which clang accepts, is
  taken (below).
- **The forward declaration note** of an incomplete tag's variable and
  tentative definition (`check_input_report_typed`).

- **An array past the object limit** (2^61 bytes, clang's and the type
  table's), at its bound: "array is too large (2'305'843'009'213'693'952
  elements)", the count with clang's digit separators
  (`CHECK_INPUT_ARRAY_TOO_LARGE`).
- **An element whose size isn't a multiple of its alignment** (an `aligned`
  typedef or enum raised it past its size, #1893), at its `[`: "size of array
  element of type 'enum E' (4 bytes) isn't a multiple of its alignment (8
  bytes)" (`CHECK_INPUT_ARRAY_ELEMENT_ALIGNMENT`), clang's
  `err_array_element_alignment`. It is the type table's own refusal
  (`TYPE_ERROR_ELEMENT_ALIGNMENT`), found before it. An aligned typedef's
  element prints as its canonical type (`'char'`, where clang has
  `'C' (aka 'char')`), as every diagnostic prints an aligned typedef's type.
- **A constant that fails** in a bound, a `typeof` operand, an enumerator's
  value, a bit-field's width, a `_BitInt` width or a designator, at the
  failing token, in the words an attribute argument's failure takes
  (`CHECK_INPUT_CONSTANT_EXPRESSION`, `check_input_constant_failure_report`,
  both from `check_constant_failure_error`'s reading of the evidence,
  `CheckConstantFailure`): "no member named 'b' in 'struct S'" at the
  member's name, "member reference type 'int' is not a pointer" there too,
  "member reference base type 'int' is not a structure or union" at the
  operator, "arithmetic on a pointer to an incomplete type 'struct T'" and
  "invalid application of 'sizeof' to an incomplete type 'struct T'" with
  the tag's note, a call's "too many arguments" (at the first extra
  argument) or "too few", with the function's note, and "invalid operands to
  binary expression". Other failures keep the generic error. A bound's
  failure before its first undeclared name is reported with it, in source
  order.
- **Statement checks in clang's words** (#2406): a `case` or a `default`
  outside any switch is "'case' statement not in switch statement" (or
  'default') at its keyword (`CHECK_INPUT_CASE_OUTSIDE_SWITCH`, the control
  pass); a subscript of neither an array, a pointer nor a vector is
  "subscripted value is not an array, pointer, or vector" at its `[`; and a
  subscript of a function's name or pointer is "subscript of pointer to
  function type 'int (void)'" at that operand, a typedef'd pointee as written
  (`check_semantic_constraint`).
- **A member access** whose base is no structure or union, anywhere else
  (#2288, `check_semantic_constraint`'s member branch): clang's "member
  reference base type 'int' is not a structure or union" at the operator,
  with the operand's type as written for `.` (`'myint' (aka 'int')`) and the
  pointee for `->`; `.` of a pointer to a record is "member reference type
  'struct S *' is a pointer; did you mean to use '->'?" there, and `->` of no
  pointer is "member reference type 'int' is not a pointer" at the member's
  name. Clang goes on reading `.` of a pointer as `->`, so a member that
  pointer's record lacks adds its "no member named" there; buster's
  expression stays invalid and gives only the first. `->`'s pointee type
  prints as its canonical type, not its typedef's.

Each of these now makes its declaration invalid, as clang's does, and the
reader goes on (below); a typedef's and a parameter's in clang's own ways. The semantic-recovery
fixtures compare each with clang.

A block's thread-local object without `static` or `extern` (`_Thread_local int
z;`, C23's `thread_local`, GNU's `__thread`, a `for`'s declaration too) is
clang's "'_Thread_local' variables must have global storage" (`'__thread'` for
GNU's) at the keyword (#2420, `CHECK_INPUT_THREAD_NON_GLOBAL`), and the report
recovers: the reader goes on with the declaration, as clang does. With
`register` beside it, clang's "cannot combine" comes first, and the reader
still stops there (two errors of one specifier list, #1820 (c)).

#### Invalid declarations (#1447)

Clang marks a declaration whose declarator or type is in error invalid, keeps it
bound, and leaves its uses quiet: a `RecoveryExpr` around each, whose enclosing
expressions give no diagnostic of their own. The reader does the same for the
declarator errors above and a variable of type `void` or of an incomplete type,
in a declaration's own declarator (not a deduced `auto`'s; a typedef's, a
parameter's and a K&R definition's are below):

- **The error recovers.** `check_input_declarator_recover` and
  `check_input_variable_recover` move the report to the recovered ones, so the
  reader goes on, and the type has a stand-in `int` where clang's has its
  error: a function returning an array returns `int`, as clang's
  `BuildFunctionType` makes it; an array of functions, of an incomplete
  element, of an element whose size isn't a multiple of its alignment, past
  the object limit or of a negative bound, and a `void` or
  incomplete variable, are `int`. A constant read at the walk (a bound,
  `typeof`, an enumerator, `_Static_assert`, a bit-field's width) sees the
  stand-in; its uses there are quiet all the same.
- **It stays bound.** `check_input_invalid_declare` marks its name in
  `CheckInput.invalid_declarations` (a byte per token, allocated by the unit's
  first, with `invalid_count`). `check_input_bind` binds it as it is, with no
  redeclaration checks and no linkage. After a visible invalid declaration, a
  function's redeclaration is invalid too, and an object's is checked as if
  nothing were visible, but for its linkage: `static void v; int v;` gives
  "non-static declaration of 'v' follows static declaration", and that one is
  invalid too. Another kind of entity's (`int a[-1]; int a(void);`, a typedef
  after either) is checked against it as against any other, clang's
  "redefinition of 'a' as different kind of symbol".
- **Its uses are quiet.** `check_input_binding_answers` gives each use of an
  invalid declaration no declaration and the silent report
  `CHECK_INPUT_INVALID_USE`, which recovers as an undeclared name's does
  (`check_input_diagnostic_recover`'s limits, the initializer and repair
  checks). The driver publishes nothing for it and counts it as no error of its
  scope, so the scope keeps its unused variables, and a unit whose only reports
  are such uses has its unused declarations' warnings from the first reading.
  The rejected unit's later readings leave each use's part out, and each
  invalid declaration with it, without leaving out the bodies that name them
  (`check_driver_rejected_common`'s `quiet`).
- **Source order.** A unit with an invalid declaration at file scope reads its
  bodies in source order (`check_deferred_bodies`), and a lane that meets one
  in its body refuses, so source order recovers it.

- **A typedef's.** Clang marks the typedef invalid but keeps its stand-in
  type, whose uses are checked as any other's: `typedef int U[-1]; U u = "x";`
  gives "initializing 'U' (aka 'int')". So the typedef isn't marked invalid,
  only its error recovered.
- **A parameter's.** Its error recovers in any parameter list, nested ones too
  (`int f(void (*cb)(int n[-1]))`), and the parameter is a stand-in `int`,
  marked invalid, so that its uses in a definition's body are quiet. The
  function whose own list it is is invalid too, as clang's
  `ActOnFunctionDeclarator` marks it, and so are its uses (`bad("x")`). A
  function pointer's parameter (`void (*fp)(int r[-1])`) leaves the pointer
  valid, its type `void (*)(int)`.
- **The rest of its group.** After a declarator whose own type is in error
  (not a void variable's, nor one whose only error is its own parameter's),
  clang's parser declares none of the group's later declarators: `int a[-1],
  b;` leaves `b` undeclared, and `typedef int U[-1], V; V v;` is "unknown type
  name 'V'" (which still stops the reader). The reader skips them to the
  `;`. A later declarator with an initializer, which is clang's parse error
  "expected ';' after top level declarator", still stops the reader without
  it.

A member's declarator error, and one in a type name (`sizeof(C[4])`, but for
a negative bound's), still stop the reader (#1893's too), where clang marks the member (and
its record) or the expression invalid and goes on.

An initialized invalid object recovers too: clang checks its initializer no
further, so the reader skips it to the declarator's end, the object invalid
and its uses quiet. An invalid function's initializer is clang's "illegal
initializer (only variables can be initialized)" at its name, after the
declarator's own error (`CHECK_INPUT_FUNCTION_INITIALIZER`), recovered the
same way. Clang still reports a skipped initializer's undeclared names, which
the reader doesn't.

An invalid K&R definition goes on too, its body checked as clang checks it, and
its declaration list's declarator errors recover: the listed parameter is a
stand-in, invalid and quiet in the body. `-c` refuses every such unit.

#### Extern objects of type void (#1447)

An `extern` declaration of type `void` (`extern void v;`, a block's too) is
valid, as clang takes it. Its address is a `void *` to an undefined symbol that
another unit defines. `sizeof v`, a discarded `v;` and `(void)v` are quiet: a
value of type `void` is no unused result, in a comma's, conditional's or
selection's operand too. A dereference's or subscript's of a `void *` still
warns, as clang's `isUnusedResultAWarning` names those unconditionally. Reading
its value is clang's error, and so is a definition (`void v;`, `extern void v = 1;`).

#### Semantic error recovery (#1447)

Translation-unit-wide recovery after semantic errors comes in phases. The plan
is [on #1447](https://github.com/buster14a/buster/issues/1447#issuecomment-6719193112).
Phase 1 covers the errors that clang's `handleDeclAttribute` gives and then
drops the attribute for, leaving its declaration valid:

- an argument's value, count, string or parameter index;
- the attribute's subject;
- a mutual exclusion;
- an alignment specifier's argument.

Such an error doesn't stop the reader. `check_input_recover_reports` moves its
report from `CheckInput.reports` to `recovered_reports`, so the ~50 places that
test `report_count == 0` act as for a valid declaration. The attribute is
dropped where the error is found:

- **An item's reading:** an argument that must be a string
  (`check_input_attributes`). The item is skipped to the list's next comma.
- **Its arguments' values:** `check_input_read_attributes`. The record leaves
  the list.
- **Its subject:** `check_input_attribute_subjects`. The record is ignored for
  this declarator. A shared record is checked again for the next one, which
  gives clang's repeated error.
- **Its function type and its place on a definition:**
  `check_input_attribute_function` and `check_input_attribute_definition`. The
  record is ignored for good, which also leaves the calls' checks without it
  (a dropped `format`'s). These passes don't read the mark, so they give the
  error again for each declarator.

A parser error from an item's reading keeps its place. Sema's errors complete
at their declarator's last token (`CheckInputReader.recovered_pending`): after
the parser's errors in the declarator, before a missing `;`'s at the next
token, which is clang's order.

Once the reader has run, `check_input_recovered_publish` appends the recovered
reports to `reports`, each marked `recovered`. The unit is rejected. If they are
its only reports, it is `CheckInput.recovered_valid`: it was checked as a valid
unit is. The driver then gives it a valid unit's diagnostic path, as it does for
a unit whose only errors are its preprocessor's:

- the initializers' and assembly's own runs;
- `check_program`;
- the common warnings, such as `-Wparentheses`.

Its `result` still fails, and `-c` emits nothing. The asm and control
diagnosis cutoffs skip recovered reports. As in clang, a unit with any error
publishes no unit-end warning: no unused file-scope object's and no unused
function's (`check_driver_rejected_warnings`).

Deferred bodies (#1447's lanes item) carry their recovered reports through the
lanes: a unit whose only errors are recovered keeps its deferred, multi-lane
schedule. A lane starts each body with an empty list of its own, and the body's
result keeps it. Each cut keeps the top level's count at the body's opening.
The merge (`check_deferred_recovered`) rebuilds the unit's list in the order
source order's reader finds them, by those counts:

- the top level's reports before each body's opening;
- then the body's;
- and the top level's after the last body.

Each report's TypeIds take the merged table's (`check_deferred_report`): the
type it names, and the two whose `value` holds one, a redeclaration's prior type
and an attribute expression's second operand's type. Every other `value` is a
token, a count or a pair of tokens, and tokens, declarations and names are the
unit's already. `check_input_recovered_publish` then appends them
as source order's does.

One recovered note isn't merged: an object's type conflict notes the entity's
latest earlier declaration, even a block-scope one in another body. The deferred
top level hasn't read the bodies it skipped, and a lane hasn't read the other
bodies. So the scan back from the declaration sends the unit to source order
when it reaches another body's closing brace before its match. A body with a report it doesn't recover still goes to
source order, as does a top level with one. Error-free units take none of this:
their recovered lists stay empty, and the driver copies their warning events
only on an error.

`test-checker-driver --semantic-recovery-only`
(`c_check_semantic_recovery_driver_test.c`) compares the whole output and
status with clang 23:

- by the vector path, the scalar one, and deferred bodies on one, two and
  three lanes;
- under gnu17 and gnu23;
- with no option, `-Wall`, `-ferror-limit=2` and `-w`.

It also checks that `-c` of each fixture is refused with no object written. The
deferred readings are `-c`'s, on one, two and three lanes: `-fsyntax-only`
starts no lanes, so a deferred `-fsyntax-only` reading is source order's. The
driver reads `--scalar`'s in source order; `test-checker-deferred` compares the
deferred reader with both paths, and its merge with its scalar twins, on its
recovered fixtures too.
The fixtures cover each pass above, shared records, two errors in one list,
missing names and a control error after a recovered error, block declarations,
exclusions, `_Alignas`, a dropped `format`, and enumerators.

Phase 2 covers redeclarations that conflict with a declaration of the same
name: the visible one in `check_input_bind`'s first test, or its entity's
linked one in the second (`int k = 1; int k = 2;`). Clang marks such a
redeclaration invalid and leaves it out of its name's lookup. So the checker
recovers its report and leaves the declaration unbound and unlinked: later
uses find the earlier declaration (after `int a; float a;`, `&a` is an
`int *`), and the unit goes on as in phase 1. A loader variable's
redefinition keeps its note and still stops the reader.

These errors take clang's wording and notes through
`check_input_redeclaration_report` (`c_check_redefinition.c`, from
`bde006626`). Phase 2 adds two variants to it, both with the note "previous
definition is here", where the error used to read "conflicting declaration":

- `CHECK_REDECLARATION_TYPEDEF`: "typedef redefinition with different types
  ('N' vs 'O')";
- `CHECK_REDECLARATION_KIND`: "redefinition of 'X' as different kind of
  symbol".

Thread-local redeclarations use the same type and linkage precedence (#1784):
an incompatible object type is reported even when thread-local storage also
differs, followed by static/non-static linkage conflicts and duplicate local
declarations. Otherwise a storage mismatch says "thread-local declaration of
'X' follows non-thread-local declaration", or its reverse. That diagnostic
notes the visible prior declaration, with its declaration/definition note;
object type conflicts still note the latest prior declaration of the entity,
including one from another block. The semantic-recovery fixtures cover both
storage directions, extern and static combinations, and subsequent
declarations and uses under the same Clang comparison matrix.

A mismatch of thread-local storage alone keeps its declaration, as clang's
`MergeVarDecl` links it after the error (#1786): the error is recovered and the
declaration is merged, bound and linked as a valid one, with its own storage.
The next declaration is compared with it (`_Thread_local int x; int x; int x;`
has one error, and a third `float x;` notes the second), and its linked
declaration's check doesn't compare the storage again, so the error is given
once. A definition after an earlier one gives the TLS error and then "redefinition
of 'x'", noted at the first definition, at the same name. A block extern's
mismatch with another block's declaration is checked against the linked one.

An object's second definition alone (`int x = 1; int x = 2;`) stays in its
chain too (#1868): clang's `AddInitializerToDecl` reports "redefinition of 'x'"
after `MergeVarDecl` has linked the declaration, which stays in lookup,
invalid. The checker binds it and remembers it
(`CheckInputReader.invalid_redefinitions`), and a later declaration's type
isn't checked against it, as clang's `MergeVarDeclTypes` skips an invalid
declaration: `float x;` after it is no error, and keeps its own type. Linkage
and thread-local errors still note it, and a third definition's error notes
the first. A block's duplicate local is still dropped, as clang's lookup
finds the first there.

A function's second definition of a compatible type stays in its chain as
well (#2362): clang's `ActOnStartOfFunctionDef` reports "redefinition of
'f'" after the declaration was merged, and marks it invalid. The checker
recovers the error, binds the definition as an invalid declaration
(`check_input_invalid_declare`) and leaves its entity's linked declaration
as it was. So, while it is visible, a later declaration of the function is
invalid and quiet, a third definition and a conflicting type alike, and so
are the name's uses, a block's `extern` one's too. Its body's errors and
another kind's redeclaration, noted at it, are still reported. A
multiversioned function's version (`target`, `target_clones`, `cpu_specific`)
is recovered the same way, as an ordinary redefinition is, so the reader goes
on past it. A declaration of another version is another entity's, checked
as any, and one of the invalid version's that follows it is a redefinition of
it again, noted there, as clang's `CheckMultiVersionAdditionalDecl` finds it.
A call of a `target` multiversioned name keeps its other versions, as clang's
overload resolution does. Its argument errors are still the plain call's
wording, not clang's "no matching function", as with every multiversioned
call.

`check_driver_rejected_warnings` follows clang's `ActOnPopScope`: a scope in
which an error occurred gives no unused-variable warning, nested scopes'
errors included. Its extent runs from the scope's `{` to its `}`, from a
for initializer's opening parenthesis through the whole loop statement, or,
for a function body, from the definition's start, since clang's
`ActOnStartOfFunctionDef` handles the definition's declarator inside the
body's scope. What clang gives as it parses, before that, doesn't count there
(`check_driver_error_scoped`, #1835): a specifier's error, a parameter's
attribute's, and an attribute's argument that must be a string literal. A
rejected unit's rounds also read past an error outside every body that keeps
its declaration valid (a dropped attribute's or specifier's), which their
reading recovers again, so the later bodies keep their warnings. Unused
parameters are still warned. A per-token prefix count of
the unit's error reports answers each scope's question in constant time, and
only a unit with errors builds it.

Clang's warnings that default to errors don't count in a scope either (#1969):
`DiagnosticIDs::isUnrecoverable` is false for a warning's class, whatever
its severity, so `Scope::hasUnrecoverableErrorOccurred` doesn't see them.
They are:

- a simple assignment's conversion between an integer and a pointer
  (`-Wint-conversion`) or between pointers (`-Wincompatible-pointer-types`,
  `-Wincompatible-function-pointer-types`), in an initializer, an assignment,
  a return or an argument (`check_driver_report_default_error`, and an
  initializer walk's `CheckDriverDiagnostic.default_source`);
- a return's missing or extra value (`-Wreturn-mismatch`).

So `int f(short q) { char *p = &q; return 0; }` warns of the unused `p`, as
clang does. A rejected unit's view keeps such a conversion in a body rather
than leaving its part out: it casts the source, through `(void *)(long)(…)`
to a pointer target and `(long)(…)` to an integer one
(`CheckDriverConversionSite`, `CHECK_DRIVER_CAST_*`). The reading then has
the same expression and uses, with no error and no `-Wall`/`-Wextra` warning
of the cast's own, so its statement's warnings are given too
(`x = p; return x / 0;`, `int x = p + (1 / 0);`).

A left-out expression statement keeps its complete operands (#2120,
`check_driver_rejected_operands`, `CHECK_DRIVER_CAST_OPERAND`). Clang builds
an expression's operands before it finds the expression's error, and gives
their warnings, on either side of the error: `x = s.b + printf("%s", x);`
gives its `-Wformat`, `k(1, (2, 3), 4)` (too many arguments) its comma's
`-Wunused-value`, `x = (x = x) + s.b;` its `-Wself-assign`. The view's `{ }`
in the statement's place holds each subtree off the path from a known error's
token to the statement's root, through evaluated operators, calls (their
argument lists' separators), subscripts, groups, casts, member accesses,
conditionals and returns, as its own `(void)(A);`, where it holds no known
error. A `(void)` cast gives no warning of its own, and the body has an error,
so its reading runs no flow analysis (`x = u + s.b;` gives no
`-Wuninitialized`), as clang's doesn't. Nothing under `sizeof` is kept. Where
the reading with the operands isn't valid, the round reads the view again
without them. Differences left: an argument past a call's parameters whose
first token is the error's place (`k(1, 2, printf("%d", "s"))`) has its own
operands kept, not itself, so its `-Wformat` isn't given; a statement
expression's earlier statements and an initializer's operands
(`int y = (x = x, s.b);`) aren't kept yet.

A condition's assignment (`while (q = p)`) is still left out: clang's
`-Wparentheses` of it follows the conversion's error, which completes after
the source, where the common warnings publish it at the `=`. So does a
conversion outside every body. A `return;`'s statement is left out too; its
scope doesn't count it either. `-Wno-int-conversion` and
`-Wno-error=int-conversion` don't reach these errors yet (#1963): they are
the graph's reports, not warning events.

For-initializer warnings retain the header's closing parenthesis as a scope
anchor and the parser subtree's final token as their completion (#1784).
An error after the loop therefore cannot suppress its warning; an error inside
the loop can. Nested loops and a loop's braced body can finish at the same
token, so from the first unused for initializer the reader stages its remaining
unused warnings privately and stably orders their scope anchors from inner to
outer with the existing binding radix sort. Declaration order within a scope
and preprocessing lookahead order remain intact. Units without an unused for
initializer allocate no staging buffer. Reader and Clang differential fixtures
cover braced/unbraced bodies, nested loops, if/else and do/while tails, statement
expressions, shared completion tokens and errors before, inside and after a loop.
Two more redefinitions recover as clang's `ActOnTag` and `ActOnEnumConstant`
do:

- **A tag defined again in its own scope** (`struct S { … }` twice, or an enum
  without a fixed type listed twice) gives "redefinition of 'S'" at the name,
  noting the earlier definition. Its body is read as an anonymous tag's, so the
  name's later uses find the first definition, and a repeated enumerator in it
  is reported as below. This is before C23 only. Under `-std=c23`/`gnu23`
  (N3037) a struct or union defined again in its scope is compared with the
  earlier definition when its body completes (`c_check_tag_compatible.c`),
  as clang's structural equivalence compares them:
  - The records' attributes are compared, arguments included, then each pair
    of members in order: its attributes, then its names, bit-field widths and
    types. An attribute's difference is clang's `-Wodr` error ("has an
    attribute", or "has a member with an attribute"), noted at each list's
    attribute where the lists first differ, and the comparison goes on.
  - Members' types are walked together through pointers, arrays, atomics and
    functions, the same at each level, qualifiers too. Any two records are
    compatible there unless one is unnamed and written in its definition: two
    named records, a struct and a union, or two typedefs' unnamed records are,
    as in clang. Such a pair is queued and compared member by member after the
    definition's own members, breadth first. Two unnamed records written in
    their definitions, one a struct and the other a union, are clang's error
    at the second's record, noted "'' is a struct here" at the earlier one,
    with no notes at the outer member (#1826, `CHECK_TAG_MISMATCH_KIND`).
  - The first difference is clang's "type 'struct S' has incompatible
    definitions", with its two notes at the differing members (name,
    bit-field, type, a member with no counterpart). An unnamed member's note
    is at its declaration's first token and names `''`. A queued pair's
    difference is reported at the second definition's unnamed record, named
    as clang names it there (`struct S::(unnamed struct)::(unnamed at
    f.c:2:21)`, an anonymous member's `(anonymous at ...)`).
  - The errors are published where the second body ends, after what its
    members report. A member whose record is itself an incompatible
    redefinition reads as an `int` in its place, as clang's invalid
    declaration does: the pointers, atomics and arrays around it stay
    (`_Atomic(int) *`, #1826). A record after a specifier that heads its
    subtree (`_Atomic struct T { ... }`) is found by its own keyword.

  A compatible redefinition declares the earlier type. Otherwise the unit is
  refused and recovered.

  The `-Wodr` errors follow clang's policy for a default error: `-w` leaves
  them, `-Wno-error=odr` makes them warnings (with their notes) and `-Wno-odr`
  drops them, as does a source pragma at the redefinition's name. In a system
  header they are ignored without `-Wsystem-headers`. A demoted or
  ignored one doesn't make the definitions incompatible: one that is otherwise
  compatible declares the earlier type, as in clang (`check_input_odr_severity`).

  A tag defined in an `_Atomic(...)` or a `typeof(...)` specifier
  (`_Atomic(struct S { int a; }) m;`, `typeof(_Atomic(struct S { ... }))`) is
  read before the specifiers, as a declaration's own definition is
  (`check_input_specifier_definition` goes on into each group, #1447), in a
  declaration, a member, a prototype's parameter (of prototype scope, with clang's `-Wvisibility` before C23), a typedef and a type name; it used to be
  the reader's generic refusal.

  An enum defined again at file scope (#1447, c_check_enum_input.c) is read the
  same way, an enum without a fixed type after one without, or one listed again
  with its fixed base:
  - Its body is an anonymous enum's. An enumerator of a name the earlier
    definition's declare too is no redefinition but a name both declare
    (`check_tag_enum_ambiguous`): its uses get clang's "reference to 'X' is
    ambiguous" (`CHECK_INPUT_AMBIGUOUS`), with the candidates' notes at the
    second definition's enumerator and the earlier's, and recover as an
    undeclared name's do.
  - Once complete it is compared as clang's structural equivalence compares
    enums (`check_tag_enum_compatible`): each list sorted by the names' bytes,
    then pairwise their names and values, and past the shorter list the
    longer one's next. The first difference is "type 'enum G' has
    incompatible definitions", with "enumerator 'Z' with value 1 here" notes
    (the second's, then the earlier's), or one past the other list and "no
    corresponding enumerator here" at the other definition's name.
  - A compatible one declares the earlier type, and its names are ambiguous
    within its body alone; an incompatible one keeps its own type, and the
    names both lists declare stay ambiguous.
  - A unit with one reads its bodies in source order. A rejected unit's later
    readings, which leave the redefinition out, count its enumerators of the
    earlier one's names as still declared there (`CheckInput.ambiguous_index`),
    and a body's own local of such a name as no use of it.

  - A block's redefinition is read the same way (#1826). Its ambiguity notes
    qualify the name by its function as clang's qualified names do,
    `g(int, char *)::B` and `k()::B` for a `(void)` prototype
    (`check_driver_function_scope`, from the function's printed type).
  - A later definition's ambiguous use lists every candidate, the latest first,
    as clang's lookup does: the definitions between the use's and the first
    are packed in its report's `value`, up to two (four definitions).

  Left: a fifth definition's use lists four candidates of its five; and a
  rejected unit whose incompatible redefinition's own names are used at file
  scope next to an ambiguous use loses its later readings' warnings, as its
  view leaves the redefinition out.
- **A repeated enumerator** gives "redefinition of enumerator 'A'" with its
  note at the earlier one, and is dropped (`CheckTagFrame.dropped`). As in
  clang, whose next `CheckEnumConstant` sees no previous constant, the next
  implicit value restarts at 0 of `int` or of the fixed type
  (`CheckTagFrame.restart`), and an enum left with no enumerator is
  complete as `unsigned int`.
- **An enum with a fixed underlying type.** Listed again with its base, it is
  the redefinition above (before C23). Redeclared with another base, without
  the base it had, or with one it hadn't, it gives clang's
  `CheckEnumRedeclaration` error: "enumeration redeclared with different
  underlying type 'long' (was 'int')" (each base as written, a typedef with
  its "aka"), or "enumeration previously declared with (non)fixed underlying
  type", with "previous declaration is here" at the earlier one
  (`CHECK_REDECLARATION_ENUM_BASE`, `_FIXED`, `_NONFIXED`). As clang's
  `ActOnTag` drops such a declaration, its body is skipped: its tag ends past
  the `}` and no frame reads it, so its enumerators are undeclared, and the
  name keeps the earlier enum.
- **An enumerator whose name is another kind of the scope's** (an object, a
  typedef, a parameter, a local) gives clang's "redefinition of 'K'", with
  "previous definition is here" at the earlier declaration, and is dropped the
  same way. (A name declared after an enumerator is the redeclaration's
  "redefinition of 'K' as different kind of symbol" instead.)
- **A tag a prototype's list declares first** (named, or any enum), which no
  declaration outside the function names, is clang's `-Wvisibility` warning
  "declaration of 'struct S' will not be visible outside of this function",
  on by default, at its name (an unnamed enum's keyword), as clang's `ActOnTag`
  gives it: in a definition's list, a nested prototype's, a member's
  function pointer's, a block's prototype and an array bound in the list, and
  for a definition that shadows a file-scope tag. A C23 definition there is
  no warning. Before C23, the list's redefinition of its own tag is "redefinition
  of 'S' will not be visible outside of this function", noted at the earlier
  definition; an enum's, read as an unnamed enum, gets the first warning too
  (`c_check_warning_driver_test.c`). A C23 list's redefinition of its own
  record is compared instead, as any scope's (#1869).

Phase 3 covers statements after an error:

- **Control errors.** A `break` or `continue` outside its loop is clang's
  invalid statement: the reader recovers its report, so a second one is
  reported too, and the unit takes the valid path above. `check_program`
  writes no control entry for it.
- **Flushed bodies.** Clang's `AnalysisBasedWarnings::IssueWarnings` runs at a
  body's end. Once the unit has an error by then, it builds no CFG: the body
  gets no analysis warning (`-Wreturn-type`, `-Wuninitialized`), and every
  runtime-behavior warning it queued is given, reachable or not.
  `check_driver_flushed_bodies` marks those bodies' tokens, by the first error's
  publication position (`check_driver_first_error`, `-Wreturn-type`'s too).
  In them `check_common_flush` evaluates every node but `sizeof`'s and
  `_Alignof`'s operands: unreachable statements, a constant or decided
  condition's other arm, short circuits and a static selection's other arms.
  It is one pass after both paths' evaluation, so the twins stay identical.
  A body before the error, and file scope, keep their reachability.
- **Graph errors.** An expression's error no longer cuts the asm and control
  diagnosis (`check_asms_diagnose`, `check_program_diagnose`) of statements
  that don't read it; `complete` declines those that do. Clang checks each asm
  operand even when another one of its statement is in error.

Error-free units take none of this: the flushed marks need an error, the
uninitialized pass reads them only when there are some, and the diagnose
paths run only on a rejected unit. `c_check_statement_recovery_driver_test.c`,
in the same section, compares its fixtures as above, the asm and control
errors' whole text too.

The asm and control errors give every one, in clang's words, after a graph's
error and in a unit without one:

- **Control.** `check_program_diagnose_all` lists each constraint error
  (`CheckProgramConstraint`), with the token of its "previous case defined
  here" note. A switch's conflicts come in clang's order
  (`ActOnFinishSwitchStmt`), all published at the switch's end:
  - the defaults, by a reverse walk of the order clang makes them in: a
    default is made once its own statement is parsed, so one labelling
    another comes first;
  - the singletons in converted value order, each noting the one before;
  - the nonempty ranges by their low values, each noting the last singleton
    within it, or else the range before it, whose high value it names.

  A condition of no integer type is "statement requires expression of integer
  type ('T' invalid)" at the switch, by its typedef's name. So is a pointer or
  record condition, which the graph's promotion refuses
  (`check_semantic_expression`). A case value's error is the reader's alone:
  clang drops the case.
- **Asm.** `check_asms_diagnose_all` gives each failing statement's first
  error (`CheckAsmFailure`, after `Sema::ActOnGCCAsmStmt`): an output's
  memory-constraint bit-field, `_BitInt`, incomplete type or lvalue, then an
  input's bit-field, memory-only lvalue, register `void`, `_BitInt` and
  incomplete type. A tie's comes after every operand's own. A GNU lvalue cast
  output gives `-Winvalid-gnu-asm-cast`'s error at the lvalue, through the
  casts that don't change its value, and doesn't end its statement. Clang
  names an output "input" in the bit-field and `_BitInt` errors, and an input
  "output"; the words follow it. An incomplete type's error has the tag's
  forward declaration note.

A unit without other errors whose `check_asms_run` or `check_program` fails
runs these diagnoses, so only a failing unit pays for them.
`CheckDriverDiagnostic.note` carries a note through the reports' walk.

A rejected unit gets the warnings clang gives it outside its errors
(`check_driver_rejected_common`), from a second reading of the unit without
them (its reader's, its graph's, its asm, control, initializer and musttail
errors). Where it can, that reading leaves out an error's part alone, in a
private view of the unit's tokens reparsed as a repair's is
(`check_driver_rejected_view`). `check_driver_rejected_edit` walks up from the
error to the nearest part it can leave out:

- **An initializer**, from its `=`, where the declaration is complete without
  it (not an unsized array's). A const object of an arithmetic or enumeration
  type keeps its declaration, its initializer replaced by `-*__func__`, as
  clang's const object stays valid with an unknown value.
- **An expression statement**, which `{` `}` replace (a `return`'s too).
- **A condition** (`if`, `while`, `do`, `switch`), which `-*__func__`
  replaces: an `int` that reads no object of the unit's and is no constant,
  so that the statements after it stay as reachable as they were. The
  statement keeps its body, as clang's does.
- **A for statement's clause**: the init's and the increment's are left out,
  the condition's replaced as a condition is, so that the loop doesn't become
  endless.
- **A statement expression's value**: an error in its last statement is its
  expression's, so the part around the expression is left out instead; an
  error in an earlier statement is that statement's.
- **A declarator**, where its declaration can't do without what holds the error
  (an unsized array's or a const pointer's initializer, its specifiers, an
  array bound): it is left out with the comma after it, or the last kept
  one's before it, or the whole declaration where no other declarator is
  kept. Every later identifier of its name in its scope is a use, whose own
  part is left out in turn (a work list), so that no use reads an outer
  declaration in its place. A typedef's isn't, as the parse reads its name as
  a type.

Each error's parts are left out all or none (an undo log). A part that holds a
`case` or `default` label isn't, as its switch's checks (`-Wswitch`'s
coverage, duplicate values) need it, nor one that holds a tag's definition
declaring a name used outside the part (its tag, an enumerator), which the
reading would take for an outer declaration's without an error
(`check_driver_tag_local`; a tag it can't find, as a C23 enumeration's with
`: type`, counts as used), nor one that holds a `goto` inside a loop, whose
jump `-Wfor-loop-analysis` looks for. A part may hold a label, another
`goto`, a label's address or a local label's declaration (#1447): a left-out
label that a kept `goto` names leaves the reading an error, so its view isn't
taken, and a left-out `goto` outside loops changes no warning a flushed body
gives (buster gives no `-Wunused-label` yet: once it does, a kept label whose
only `goto` is left out would warn). The
replacements are chosen to give no warning of their own (a constant condition
would: clang's "no case matching constant switch condition"). A part left out
gives no warning of its own reading, where clang gives those of what it builds
before the error; the first reading's folded shifts in it are still given, at
the unit's end (`check_common_shift_finish`), not among the body's. A
function that holds an error gets no `-Wunused-but-set-parameter` from that reading, as clang's
`ActOnFinishFunctionBody` gives none: it may lack the parameter's reads
(`CheckCommonFacts.flushed`'s 2). The body is then read as clang reads it, its warnings before and after the
error given, flushed (no analysis warnings).
Other errors leave out their bodies, by `check_input_selected` as
`-flazy-system-headers`' bodies are, as does every error where the view
doesn't parse or its reading isn't valid (`check_driver_rejected_read`
publishes nothing of an invalid reading, which is then read again with the
bodies left out). The second reading may find errors of its own, which the
first didn't reach: past a reader's error that isn't recovered (an incomplete
object's type), which ends the first reading's walk and skips its control
scan, a duplicate case, a label's redefinition or another such error. A view's
reading with errors falls back to the bodies' as above, as they may be the
view's own; an error outside every body falls back to its whole declaration
or definition left out (`check_driver_rejected_declaration`), with the bodies
that name it, which leaves no use undeclared, so that reading's errors are the
unit's too, unless a left-out declaration defines a tag outside every body,
whose later uses would read an incomplete type. The bodies' reading publishes those its known errors don't hold,
its reader's and graph's reports and its controls' constraints, in clang's
words (its reader's alone where its graph can't run after them), and the next
round leaves their parts out too. Each round adds an error and its parts, up
to clang's error limit's cut (the error after `-ferror-limit`'s, the first with
`-Wfatal-errors`), past which clang gives nothing: counted in the bodies that
hold a known error (each at least one; an error's tokens may be several), the
bodies after the one that brings them past the limit are left out whole, and
`BUSTER_CHECK_DRIVER_REJECTED_ROUNDS` (4) more readings find what the body
that holds it, or file scope after it, still hides; past them the unit's
warnings aren't given. Without a limit (`-ferror-limit=0`) the rounds go on
while each finds an error. Each round is a reading of the unit, so a unit of
`n` walk-ending errors costs up to `min(n, limit) + 1` readings. The rounds
run under `-w` too, for the errors they find, which clang gives whatever the
warning options; `-w`'s policy leaves out their warnings (#2162). The second reading's unused variables in a scope with an error
are left out (its own error counts, the dropped part's at the token after
it). A function holding an error keeps its unused parameters, as clang's
`ActOnFinishFunctionBody` gives them whatever the body's errors, except one
that what the reading left out of its body names, which clang's reading of that
part takes for a use (an erroneous expression's operands are still referenced);
a member's, a tag's or a label's name of the same spelling is no use, nor is
a declarator's name or a use inside the scope of a declaration of that name: a
block's, a `for` clause's, a statement expression's, or a prototype's parameter
list's (`check_driver_declared_scope`, `check_driver_prototype_scope`). Where that reading is valid, its program
gives the other bodies' `-Wreturn-type` and common warnings, the bodies after
the unit's first error flushed as above. Where a reader's error left the first
reading without some of its warnings, the second's are given, without the
unit-end ones: its warning events the first reading lacks (by kind and token),
and, where a reader's error ended the first reading's walk
(`CheckInput.walk_end`), every report and warning of the second from there on
(`check_driver_reports_from`). Nothing is given where an error lies outside every
body. Only a rejected unit reads twice.

The driver publishes its reports after `check_program`, so that a control
error it finds is among the unit's errors: as for the reader's and the graph's,
its scopes' unused variables and the unit-end warnings are left out
(`check_driver_rejected_warnings`). `c_check_statement_recovery_driver_test.c`
compares fixtures of graph, control, asm and reader errors with clang.
`c_check_structural_recovery_driver_test.c` compares the view parts' fixtures
(conditions, for clauses, statement expressions, declarators, parts that hold
a label, a goto, a label's address or a local tag's definition) with clang 23 under `-Wall` and `-Wall
-Wextra`, gnu17 and gnu23, by both paths and on one, two and three lanes, each
with errors mid-body among warnings before and after them.

A unit whose parse error the driver reads through a repaired view
(`check_repair_make`: its original tokens through the last complete statement,
then synthetic closers) gets the common warnings of the view's complete
original statements too (#2406, #2431): `check_program` runs on the view
where it has no other error (without its initializers' and asms' runs, which
the repair leaves out), and `check_driver_rejected_common`'s readings where it
has. `CheckCommonView`, which the driver sets around those passes, selects
their reports by `CheckRepair.warning_complete`, as the view's other warnings
are, and places them by the original's tokens: a body whose `}` the repair
writes again where the original has it publishes its deferred warnings before
the parse error, and one the error left open after it, without its flow
analysis (`check_driver_flushed_bodies`) or `-Wreturn-type`. A reading's
fresh error at a synthetic or incomplete statement isn't taken, and nor is a
program failure there. `c_check_repaired_view_driver_test.c` compares
fixtures with clang 23 as the structural ones are. Differences left: an
unused variable of a scope the parse error leaves open is still given
(clang's isn't), and one of a body the repair closes again publishes after
the parse error; both predate the repaired view's common warnings.

Every declaration's failed initializer is given, as clang gives each
(phase 2): `int a[2] = 5; int b[2] = 6;` has two errors where it had the
first alone. `check_initializers_diagnose_warnings` already visited every
declaration and kept the earliest failure; it now also lists each one
(`CheckInitDiagnostic.failures`, `CheckInitFailure`), copied into `output` after
its walk's arenas are rolled back, and the driver gives each with its own
conversion's words and a static object's second "not a compile-time constant"
error. Only a unit whose initializers failed takes this path.

An array of unknown size whose initializer is no list nor a string of its
element (`int y[] = 5;`) gets clang's `FK_ArrayNeedsInitList` error at its
name, worded by the literal its element takes: "or string literal" for a
character, "or wide string literal" for `int`, `unsigned short` and
`unsigned int` (wchar_t's, char16_t's and char32_t's), the list alone
otherwise (`CHECK_INPUT_DIRECT_INITIALIZER_ARRAY`'s detail,
`CheckInputArrayList`). `check_input_initializer_finish` recovers the report;
the array keeps its unknown size, as clang's invalid declaration does, and its
initializer leaves the sparse columns, so the initializers' run doesn't see it.
As clang checks the initializer first, the report completes at the
initializer's end, after the initializer's own diagnostics, and an extern
array loses its -Wextern-initializer event. Clang gives no such error where
the initializer is no valid value. The driver decides that once the
expressions are checked (`check_init_listless_drop`, which removes the
report): an undeclared name in it isn't shielded by a statement expression,
or the initializer's outermost node is diagnosed, of no valid type or
incomplete as the initializers' diagnosis counts it. An initializer with no
node keeps its report: a rejected unit's retained graph ends at its first
missing name, so a later valid initializer has none. A statement expression
shields a name in a statement before its last, its value
(`check_init_undeclared_poisons`). The report keeps its initializer's first
token (`value`) for this; only a unit with another error drops one. A
parenthesized initializer, a cast, a scalar's compound literal or an array's
compound literal that is only part of the initializer is no list either. An
array's compound literal that is all of it, even inside parentheses, is GNU's
array initializer, which clang accepts (#1447): `check_input_initializer_finish`
(which `prepare` schedules for an array whose initializer is parenthesized)
takes it where its element type is the array's, qualifiers too, and its size
the array's or the array has none. The literal's body is then the array's
initializer in the sparse columns, so the size inference, the initializers'
run and the emission read `{1, 2}` as the array's own; an array of unknown
size takes the literal's size (`((int[3]){1})` makes `int[3]`), the literal's
own completed type where it had none (`(int[]){1, 2}`). Any other array type
is clang's "cannot initialize array of type 'int[3]' with array of type
'int[2]'" at the name (`CHECK_INPUT_ARRAY_LITERAL_MISMATCH`, its value the
literal's TypeId, which lanes remap), recovered, the array keeping its type and
no initializer. A file-scope array's mismatch is left out where the literal's body
holds a value that is no constant (#1812): clang's file-scope literal is then
invalid, and its initialization of the array gives no error of its own. The
body's own "initializer element is not a compile-time constant" is given once
at its place, though both the literal's declaration and the array check it. At
block scope the mismatch still comes before the body's conversion error, where
clang gives it after.

An undeclared name that is a typo of a visible one is clang's (#1447,
`c_check_typo.c`). The rules are clang 23's, as probed:

- **Candidates.** The names visible at the use: the innermost declaration of
  each, an implicitly declared function's among them in its block. A library
  function's implicit declaration stays one after its block, as clang
  declares the builtin at file scope (`printf` called in one function is
  `printff`'s correction in the next). A name whose length differs
  from the typo's by more than a third of it is skipped
  (`TypoCorrectionConsumer::addName`), and so is one further than
  (length + 2) / 3 by Levenshtein's distance. The nearest is taken if no other
  name the context takes is as near and the typo's length is at least three
  times its distance.
- **A value** (`DiagnoseEmptyLookup`) takes an object, a function or an
  enumerator; within an expression, before `=`, `->` or `.`, not a function
  (`CastExpressionIdValidator`). A statement's first name takes an object
  alone before `=`, and any value otherwise (`StatementFilterCCC`). Its error reads
  "use of undeclared identifier 'countr'; did you mean 'counter'?", with
  "'counter' declared here". The use then binds to that declaration as if it
  were written (`CHECK_INPUT_UNDECLARED_CORRECTED`, recovered), so the later
  diagnostics follow from its type: a conversion's error, a case label's
  constant, a static initializer's "not a compile-time constant" (which the
  driver keeps at the corrected name's token after its own error); the unused
  declarations' analysis counts the use. They spell the name taken, as
  clang's do ("cannot assign to variable 'counter'", "'olden' is
  deprecated"; `check_driver_corrected_name`), and a warning at a declarator
  or an operator whose operand holds the use comes after the correction's
  error (`CheckInput.corrected_tokens`, `check_driver_after_corrections`):
  clang gives that warning once the operand is read. In a unit with another
  error, the readings that give its warnings keep the corrected use's part,
  which they correct again, where they leave out an error's
  (`check_driver_rejected_recovered`): `arx[5]` for `int arr[3]` still gets
  its `-Warray-bounds` at the function's end, as a division by zero its
  operand holds gets `-Wdivision-by-zero`. In a file-scope initializer an
  array index's warning comes at the declarator's end, after the
  correction's error and the operands' queued warnings, as clang checks the
  index once the full expression is complete (`check_common_report_bounds`).
- **Keywords and type names in an expression.** Clang offers keywords too
  (`AddKeywordsToConsumer`): an expression's (`sizeof`, `_Alignof` from C11,
  `true` and `false` in C23) and, within a function and not before a `)`
  (the parser's `WantRemainingKeywords`), a statement's (`do`, `else`,
  `for`, `goto`, `if`, `return`, `switch`, `while`), with `break` where a
  loop's or a switch's body holds the name,
  `continue` where a loop's does, and `case` and `default` where a switch's
  does, not their heads or a `do`'s condition (`check_typo_scopes`). The
  first name in parentheses, which clang reads as a cast's type or an
  expression's (`MaybeTypeCast`), and a cast's operand also take typedef
  names and the type specifier and storage class keywords; a cast's operand
  takes no value. Where a keyword or a typedef name is nearest, the error
  suggests it ("did you mean 'while'?", "did you mean 'myint'?" with
  "'myint' declared here") and the name stays undeclared, the expression
  rejected with no other diagnostic of it, as clang's is (the report's
  `detail` and `subject`, `check_typo_suggestion`). Where an operand follows
  the name (`sizeo a`, `retrn 5` in an initializer), the parse fails there;
  clang's correction takes the keyword and skips the expression's rest to the
  `,`, `;` or closer that ends it, without the parser's error. The recovery
  view leaves that tail out for any name there (`check_driver_typo_tail`,
  `CheckPrefixTails`), and keeps it, with the parser's error, where the name
  is declared or its search corrects it to a value. A name that stays
  undeclared is clang's "use of undeclared identifier" alone, its expression
  invalid and its rest skipped the same way (#2255): before C23, `u8'a'` is the
  name `u8` and a character constant. A view that reads past every parse
  error of the unit (`Parser.whole_view`) recovers from its missing names as a
  complete unit does (`check_input_diagnostic_recover`), so the unit's later
  diagnostics stay.
- **A statement's start** (`StatementFilterCCC`): a keyword is offered by the
  next token as within an expression, and a type keyword or a typedef name
  before a name, `(`, `[`, `*` or `&`; an object alone before `=`. Clang's
  parser reads the statement again with a keyword or a type it takes
  (`ClassifyName`): `retrn x;` returns x, `whil;` gives "expected '(' after
  'while'", and `myin x;` declares a `myint`, its error "unknown type name
  'myin'; did you mean 'myint'?". The reader can't parse again: the name's
  report says what it took (`CHECK_TYPO_STATEMENT`), and the driver reads the
  unit again from the original's tokens with each such name replaced, the view
  recovered and checked as a failed parse's is (`check_driver_typo_edits`,
  `CheckPrefixEdit`, up to 4 readings), its errors at the names first
  (`check_driver_typo_errors`) and a parse error naming one spelling the
  correction (`Parser.respelled`). An expression statement whose `;` is
  missing after its first name or that name's call (`retrn x`, `whil (a) a--`)
  is read on past as if the `;` were there (`ParseDiagnostic.probe`), as
  clang's `ParseExprStatement` does once the name is classified, so that the
  reader searches it; where the statement's expression keeps an undeclared
  name, clang's invalid expression, its rest is left out to its `;`, with the
  parser's error. Before C23 a name's call is an undeclared function's, which
  no search reads as a keyword (`whil (a)` stays a call).
- **A parameter's unknown type name** (`ParseImplicitInt`'s
  `DiagnoseUnknownTypeName`): clang corrects it to a typedef's name or a type
  specifier's or storage class's keyword and reads the parameter with it,
  "unknown type name 'myin'; did you mean 'myint'?" with "'myint' declared
  here" (a keyword's has no note: `unsignd` is `unsigned`, `constt` is
  `const`). The parse reads such a parameter in place, and the prefix view
  reads it as `int` (`Parser.recovered_parameter_types`); where a unit's first
  error is one, the driver takes that view, with every declaration before the
  first other error, before a repair view, which would end at its declaration
  (`check_prefix_parameter_view`), and the recovery rounds read past it. The
  reader searches each such `int` whose source still spells the name, among the
  declarations visible there, a block's own typedefs too
  (`check_input_parameter_typo`, `check_typo_parameter`), and lists what it
  suggests (`CheckInput.parameter_typo_tokens`, `CHECK_TYPO_PARAMETER`); the
  driver reads the unit again with the type in the name's place, as for a
  statement's start, and the error is its own. A name nothing is near keeps
  the parser's "unknown type name" alone and reads as `int`.
- **A parenthesized name before an operand** (`(myin)x`, clang's
  `MaybeTypeCast`): the parse fails at the operand; clang reads the name as an
  expression's, whose correction may be a typedef's ("use of undeclared
  identifier 'myin'; did you mean 'myint'?"), and the expression is invalid
  either way, with no recovery as a cast. The recovery view leaves the operand's
  tail out as for a name before an operand (`check_driver_typo_tail`).
- **A name after a block item's GNU attributes** that no binding of the parse
  names, which the parser reads as an implicit int's declarator
  (`parse_scalar_attribute_name`): clang's statement reader asks Sema what it
  is first, with its typo correction, and one it corrects to a value is
  "use of undeclared identifier 'valu'; did you mean 'value'?" and "expected
  identifier or '('" there, the item skipped to its `;`. The recovery view
  leaves the attributes out, so that the reader searches the name at the
  statement's start (`check_driver_typo_tail`); a value it takes makes the
  next view read the item as an empty statement, with both errors. A name no
  correction takes keeps the implicit int's error.
- **An undeclared function's call** in C99 and later (`ImplicitlyDefineFunction`,
  only where its diagnostic is an error) takes a function. Its error gets
  "did you mean 'compute'?" at the call and "'compute' declared here", but not
  where the nearest is itself implicitly declared. No search is made where
  the name was declared with linkage before, another call's implicit
  declaration or a block's extern (clang's `ExternCPrev`). The call still
  declares the function implicitly.
- **An unknown builtin's call** (#2121): a `__builtin_` name that no clang
  builtin row has in the unit's language mode, called directly before C23,
  is declared implicitly as any undeclared function, with clang's
  `warn_builtin_unknown`: "use of unknown builtin '__builtin_foo'
  [-Wimplicit-function-declaration]", an error by default in every mode
  (C89's too, and under `-w`), a warning under
  `-Wno-error=implicit-function-declaration` and nothing under
  `-Wno-implicit-function-declaration` (`PPOptions.implicit_builtin_diagnostic`,
  `CheckInput.implicit_function_builtins`). Its error searches for a
  correction in every mode. `__builtin_c23_va_start` is such a name before
  C23, as clang's table makes it a builtin in C23 alone
  (`check_constant_builtin_name`, `check_input_builtin_language`). In C23,
  and where the name isn't called, it is an undeclared identifier.
- **The limit.** At most 50 searches are made in a unit
  (`-fspell-checking-limit`), counted in source order: the values', the
  calls', and clang's search at each call of a prototyped function with too
  few or too many arguments (`TryTypoCorrectionForCall`, which suggests
  nothing in C). The calls' suggestions past the limit are dropped once the
  values' are known (`check_typo_finish`).

The search runs only after an undeclared name, so a valid unit pays nothing.
It replays the binding stream's scope stack once per unit (`CheckTypoScope`,
one for the walk's calls and one for the answers' values), then prefilters
the visible names by length (`3 * |difference| <= length`) in AVX-512. Each
distance comes from Myers's bit-parallel algorithm, with the candidate's
equality masks from byte compares; names over 64 bytes, and the scalar path,
use the dynamic-programming table, the twin that `test-checker` checks against
(`check_test_typo_distances`). On a 5,000-name unit, 66 searches add about
6 ms to a 12 ms read. A deferred reader corrects nothing on its lanes: a unit
with an undeclared name fails them and is read again in source order.

Left as they were:

- No search offers a builtin's name or a predefined type's (clang's
  `use of undeclared identifier '__builtin_absx'; did you mean
  '__builtin_abs'?`, or `'__builtin_ms_va_list'` for
  `__builtin_c23_va_start` named outside a call before C23).

- A parameter's correction to a storage class (`statc v` to `static`) reads
  the parameter with it, but buster lacks clang's "invalid storage class
  specifier in function declarator" and stops reading that declaration
  ("invalid or unsupported declaration"); so does a declaration's identifier
  list (`int b(voi);`, clang's "a parameter list without types is only
  allowed in a function definition"), which ends the reading before a later
  parameter's search (#2498).
- A statement start's correction whose statement then fails to parse in a way
  no recovery view reads past (`whil;`'s "expected '(' after 'while'", `myint
  & 1`) ends the checking of its body there, as other such parse errors do;
  so does a parse error within a call's parentheses (`foo (int i = 0; ...)`,
  C23's `for`, and `sizeo (int)`). A statement whose undeclared name ends
  before a `}` without a `;` keeps the parser's error.
- A name before an operand that is declared, or that a correction takes as a
  value (`a 5`, `nosuch 5` near a variable `nosuce`), keeps the parser's
  error, which ends the checking of its body or the unit's file scope after
  it, where clang reports the correction's error first and goes on. So does a
  tail after the first parse error no recovery view reads past (a body's
  missing `;` after `return 0`): its name's error is the parser's.
- After a block item's GNU attributes, a builtin's name without a call
  (`__builtin_abs;`, clang's "expected identifier or '('") is still an
  implicit int's declarator, and before C23 an undeclared function's call
  there lacks clang's "call to undeclared function" error before its "expected
  identifier or '('", whose parse error ends the body's checking.
- A declaration's type name outside a statement's start is clang's parser's
  to correct (`DiagnoseUnknownTypeName`): a parameter's `myin v` keeps
  buster's "unknown type name" with no suggestion, and its prefix view
  recovers it as `int`. Neither does an implicit declaration's diagnostic
  that `-Werror` makes an error before C99. A cast to one (`(myin)x`) is
  still buster's parse error.
- A function designator's comparison statement keeps its `-Wunused` and
  `-Wparentheses` notes after a correction.
- A name the walk itself reads is searched in the walk's order
  (`check_typo_walk_value`, `check_constant_name` in `c_check_constant.c`),
  among the declarations visible so far. In an enumerator's value or a
  bit-field's width, the correction is taken as if it were written, and a
  name with no correction is clang's error at the name, recovered: the
  enumerator takes the value it would have had with no `=` (clang gives
  `enum E { Z = 4, A = zzz, B }` A 5 and B 6, and a fixed enum's next value),
  and the member is no bit-field, an unnamed one an unnamed member of its
  whole type (`long : zzz` aligns the record to 8). A correction to a
  non-constant (`enum { B = countr + 1 }` with an `int counter`) keeps the
  generic declaration error where clang says "expression is not an integer
  constant expression" at the culprit, as any non-constant enumerator value
  or width does. In a `typeof` operand, an array's bound, a `_BitInt` width
  or a designator's index, a correction is taken too (`typeof(countr)` has
  counter's type, `int v[countr]` is a VLA), and each name with none is
  clang's error at it, in the expression's order. A declaration's last
  declarator whose bound or `typeof` holds such names, uninitialized and not
  a typedef, is an invalid declaration (below), as clang's, and the reader
  goes on (`check_input_declarator_recover_names`, and the specifiers'
  `typeof` with a stand-in `int`). Another still stops the reader there: a
  typedef's, a type name's, a designator's, an initialized one's, and a
  declarator before another, whose rest clang's parser skips (`int a[zzz],
  b;` declares no `b`, whose uses are then clang's undeclared errors). A `_BitInt` whose width had one is no
  type specifier, as clang drops it, and its parser's error follows: a
  declaration's "type specifier missing" (C23's "a type specifier is
  required") at its first declarator's name, a C23 parameter's "unknown type
  name" at its name, a member's or a type name's "type name requires a
  specifier or qualifier" at the list's end; an unnamed parameter's
  (`int f(_BitInt(zzz))`), clang's "expected parameter declarator", isn't
  given. Lanes and deferred readers search nothing: a unit with
  such a name is read again in source order, which corrects it. A walk
  search doesn't count a value's search that the binding answers make later
  in an earlier body, which matters only past clang's 50 corrections.

Declaration specifiers that don't combine don't stop the reader either
(`check_input_specifiers_recover`). Clang's `DeclSpec` drops the specifier the
error is about and keeps the declaration valid, so the unit goes on as in
phase 1, in every place specifiers are read (file scope, a body, a parameter,
a member, a cast's and `sizeof`'s type name). What is dropped:

- **A second type, width or sign**, at its error ("cannot combine with
  previous 'int' declaration specifier"): `int float` is an `int`,
  `long long long` a `long long`, `signed unsigned` an `int`.
- **A sign the type doesn't take** ("'float' cannot be signed or unsigned"):
  `unsigned float` is a `float`.
- **The type of a width it doesn't take** ("'long float' is invalid"): the
  width stays, so `long float` is a `long`, `long long double` a
  `long long`, `long _Bool` a `long`.
- **`_Complex` with a type it doesn't take.**

The reader finds the error where a specifier makes the list invalid. Only then
does `check_declspec_scan` work out which token clang drops: once over the list,
then again without that token, to check that nothing else is wrong. The
recovery applies where:

- the dropped specifier is a keyword;
- the list without it has no other error;
- the reader's state up to the error can be rebuilt from the scan's slots: a
  keyword's type, or a tag's, typedef name's or `typeof`'s type that the
  dropped token didn't replace.

A `_Complex` the type doesn't take is dropped at the list's end too, where the
reader checks it (`_Complex _Bool` is a `_Bool`), with every `_Complex` of the
list, as `DeclSpec::Finish` unsets the slot. That holds after a type of no
keyword too (`typeof(_Bool) _Complex`, `struct S _Complex`).

The operand readers recover the same way. A `typeof`'s or an `_Atomic`'s type
name (`check_constant_specifiers`) and an enum's fixed underlying type
(`check_input_enum_underlying`) are read again without the dropped specifier
(`check_input_specifiers_dropped`). So `typeof(_Complex _Bool)` is a `_Bool`
and `enum E : long float` a `long`.

An error of the list's end (a sign, a width or a `_Complex` the type doesn't
take) is published at the token after the list. It comes after the list's
repeated specifiers' warnings, in clang's discovery order:
`_Complex _Complex _Bool` warns, then errs.

Still stopping the reader:

- a list with a second error (`int float char x;`, `_Complex _Bool int y;`,
  `unsigned float unsigned x;`), where clang reports both;
- a storage class's conflict;
- a second type specifier that is no keyword (`int typeof(int)`, `int _BitInt(8)`,
  `int _Atomic(int)`, `int struct S`), whose state the slots can't rebuild.

Error-free lists never reach this. The semantic-recovery fixtures cover each
kind at file scope, in a body, a parameter, a cast, `sizeof`, a member, a
`typeof`, an `_Atomic` and an enum's base. They also cover a repeated
`_Complex`'s warning, "duplicate 'complex' declaration specifier" in clang's
words. The operand readers warn a repeated specifier as the declaration
reader does (#1836).

Type attributes' errors don't stop the reader either (`check_input_attribute_type`).
What clang 23 does with each is followed:

- **`vector_size` and `neon_vector_type`.** An error of the element type or
  the size leaves the type as it was (`HandleVectorSizeAttr`,
  `HandleNeonVectorTypeAttr`): `int v __attribute__((vector_size(3)))` is an
  `int`. The vector errors join `check_input_attribute_recoverable`'s list.
- **`mode`.** An unknown or unsupported mode, a vector mode of an enum and a
  complex mode of a vector leave the type as it was. A mode of the other kind
  (`int` with `SF`), or a base of no integer or floating type (a struct, a
  pointer), is clang's error too, but `AddModeAttr` still installs the mode's
  type: `float v __attribute__((mode(SI)))` is an `unsigned int`
  (`check_input_attribute_mode`'s `installed`).
- **An enum's `mode`.** Where clang installs nothing, the enum is as without
  the mode. Clang installs a floating type even in an enum (`double` as its
  integer type), which the type table doesn't hold. Such an enum gets the
  signed integer of that type's size and alignment
  (`check_input_mode_enum_integer`: `HF` a `short`, `SF` an `int`, `DF` a
  `long`, `XF` and `TF` an `__int128`), which gives it clang's layout. Its
  conversions are clang's too: a floating value converts into it silently,
  since `-Wliteral-conversion` takes no enumeration target, and an integer
  gets `-Wconstant-conversion` at the integer's width. A complex mode
  installs the complex type: the enum gets the signed integer of its size
  with the complex type's alignment (`SC` a `long` aligned to 4, `DC` an
  `__int128` aligned to 8), unless an alignment attribute gives one, which replaces it
  even downward (`mode(SC), aligned(2)` is aligned to 2); its enumerators stay
  `int`. `XC` and `TC` (32 bytes, which no integer has) and `HC` (complex
  `__fp16`, which the type table can't make) install nothing: the error
  recovers and the enum is one with no mode (4 bytes aligned to 4, where
  clang's are 32 aligned to 16, and 4 aligned to 2).

In each case the declaration stays valid and the attribute is ignored, as in
phase 1.

Clang's handlers run for each declarator, so an error shared by several is
given for each. The subject checks already repeat theirs. Two errors are given
before them, once per list, and are now repeated
(`CheckInputAttributeRepeat`):

- an `aligned` value's error, which the reader recovers and drops the record
  for;
- a specifiers' `mode` error, which the specifiers' type gives.

The list keeps a copy of each report. Its copies share it, since each
declarator merges the common list with its own. The first declarator that takes
the list keeps the original. Each later one gives the copy again, completing
with its own waiting errors (`check_input_attribute_repeats`). Error-free
lists have no repeats.

`_Alignas`'s errors are each declarator's, in the order of clang's
`handleAlignedAttr` and `CheckAlignasUnderalignment`
(`check_input_alignas_declarator`):

1. **Its place.** A declarator that can't take one gets the place's error for
   each `_Alignas`:
   - a function or a typedef: "only applies to variables and fields";
   - a parameter, a bit-field or a register variable: "cannot be applied to".
     A K&R definition's parameter declarations are parameters too. The parser
     takes their first `_Alignas` groups as the function declarator's suffix,
     so the declarator step ends before them.
2. **Its value.** The reader keeps the value's error on the record
   (`CheckInputAttribute.deferred`) instead of giving it when the list is
   read, and each declarator that may take the `_Alignas` gives it. The value
   has no power of 2, is no integer constant ("'aligned' attribute requires
   integer constant", at the argument, as clang names it), or is past 2^32
   ("requested alignment must be 4294967296 bytes or smaller", `aligned`'s
   too).
3. **The underalignment**, after the values' errors, at the last `_Alignas`
   that stands.

A declaration that declares nothing (`_Alignas(3) int;`, a member with no name
that isn't an anonymous record) has none of them, as clang declares nothing
for it. Each error recovers, so the declaration stays valid, with only the
alignments that stand.

`c_check_type_attribute_recovery_driver_test.c` compares its fixtures as above.
These differences remain:

- An enum's `XC`, `TC` or `HC` mode leaves it the layout of an enum with no
  mode.
- An enum's vector mode after its body gives its deprecation warning twice (on
  main too).

Three of clang's words and places are followed (#1447):

- **A static initializer that isn't constant.** "initializer element is not a
  compile-time constant" is at clang's culprit (`Expr::isConstantInitializer`):
  the value past same-type casts and conversions, at a binary, unary or
  postfix operator's token, a member's name, a cast's `(`, or the first token
  of a call, a subscript, a conditional or a name (`check_init_culprit_at`).
  A value of another type than its target's (`static long a = k + 1;`) has
  clang's implicit conversion as its culprit instead, at the initializer's
  first token, its parentheses included. The initializers' plan and the
  expressions' static check both place it so.
- **`sizeof` and `alignof` of an incomplete type.** "invalid application of
  'sizeof' to an incomplete type 'T'", with `alignof` or `__alignof` as
  clang spells the keyword, the type as written (a typedef with its "aka"),
  at a type name's keyword or an expression's first token, and a tag's
  "forward declaration" note (`check_semantic_forward_note`). An array of
  unknown size's alignment is its element's (C11 6.5.3.4p3), as clang takes
  it.
- **A call of no function.** "called object type 'T' is not a function or
  function pointer" at the call's `(`, with the callee's type as written once
  loaded: without its qualifiers and `_Atomic`, so a `const T`'s typedef name
  stays and a `const int` typedef's doesn't.

These are rendering only, apart from the alignment, and only the reports'
own nodes take them.

The later phases are left:

- **Invalid declarations.** None is left beyond phase 2 and the invalid
  declarations above. (A non-constant initializer's place, `sizeof` of an
  incomplete type's words and a nested tag's redefinition are clang's, above
  and in phase 2.)
- **Errors no view part leaves out.** A body's error in a part that holds a
  `case` label or a tag's definition whose names are used outside it, or in a
  typedef's declaration, still leaves its body out, and its warnings with it. A reader's
  error that ends its walk in a declarator (`int a[s.b];`, still "invalid or
  unsupported declaration") leaves its declarator out, but the first reading
  never saw the body's later errors, so a second reading that meets one isn't
  valid. The warnings clang gives of what a left-out part builds before its
  error aren't given, but for a folded shift's, which comes at the unit's end,
  and a left-out expression statement's complete operands' (#2120, above).
  A file-scope const object's initializer with an error is left out alone,
  so the declaration stays bound with an unknown value (`const int c;`), as
  clang's stays valid, and the bodies that use it keep their warnings
  (#1447). A static initializer whose only error is that it is no constant
  (`const int d = 1 / 0;`, `CHECK_INIT_NON_CONSTANT`) is kept instead: clang
  builds it and gives its warnings. The first reading's such errors go to the
  rounds (`check_driver_rejected_common`'s `nonconstant`), whose editor keeps
  the initializer and marks its declaration (`CheckDriverEditor.skipped`), and
  the reading's initializer run takes it as uninitialized, so its error isn't
  given again. A file-scope division by zero publishes at the declarator's end
  (`check_driver_file_initializer_end`, `PP_DIAGNOSTIC_RUNTIME`), as a comma's
  left operand and a shift count do, after the error, as clang's queued
  runtime warnings do. A unit of no body takes the rounds for such an
  initializer alone. Difference left: where the initializer's top is a comma
  (`int z = (1 / 0, 3);`, `int w = (1 << 40, 1 / 0);`), its runtime warnings
  publish at the comma's position, before the error, which completes at the
  initializer's end; clang gives the error first. A file-scope declaration's error that no smaller part holds (its
  specifiers', its declarator's, an invalid type's or an unsized array's
  initializer's) leaves the whole declaration out of the view, to its `;`
  (`check_driver_rejected_declaration`, #1447), and every body that names one
  of its names (not the words in its attributes' lists, `noinline` or an
  argument, which declare nothing, #2137: another declaration's attribute that
  spells the same word outside every body no longer refuses the view); a
  dropped part's error counts just before the token after it,
  so the definition that follows keeps its unused variables. An error in a
  function definition outside its body (its declarator's, its parameters')
  leaves the whole definition out the same way, to its body's `}`, so its own
  body's warnings aren't given, nor those of the bodies that name it, where
  clang gives both. The view puts a `;` at a left-out declaration's place, so
  the deferred warnings of the body before it, which clang gives at the token
  after the body, still come before the declaration's errors. Clang instead
  keeps such a declaration bound and invalid, its uses quiet: the reader does
  so for the declarator errors (invalid declarations, above), whose uses are
  known errors of their own; for the others the bodies that name it lose their
  warnings here, and so does the declaration's own initializer. An unsized array's
  (`int y[] = undeclared;`) isn't left out alone: the first reading leaves
  out the events of the bodies that use the array, which a later reading
  doesn't give again. A file-scope use of a left-out declaration's name
  elsewhere, or an error outside every body other than an initializer's, a
  file-scope declaration's or a definition's head, still
  leaves the unit without its common warnings.
- **Errors an edited reading meets.** A reading of the view that finds an error
  of its own isn't taken (its body is left out whole instead), so a body with
  two walk-ending errors (`struct T1 t; struct U1 v;`) loses the second, and
  its warnings with it.

### What the lowering costs (#531)

On the 9700X, the lowering (`check_lower_prepare`, then the bodies'
`check_lower_part`) took 25.7 ms of `-c`'s 190 ms on the frozen `sqlite3.c`,
five times Tier 0's compile of the same entries (#520's stage clock, with its
step's options). Laps of its own phases on a Zen 4 laptop's build of main put
69% of it in the tasks' loop, 12% in Tier 0's check of each lowered function
(`tier0_validate`), 8% in each body's setup (half of it the first touches of the
writer's columns), 7% in the program-wide preparation and 4% in each body's end
(its branch targets and its columns' copy). On the laptop's copy of the file it
ran about 740 instructions per typed entry, as many in a large body as in a
small one.

- **Every type's class, once.** The tasks' loop asked each node's types their
  class, and whether they were aggregates, complex, pointers or `_Bool`, each
  time by walking the type table (the aligned typedef's base, the enum's
  underlying type, the integer's width and signedness): about a quarter of the
  stage. `check_lower_classify_types` makes every type's class and facts
  (`BUSTER_CHECK_LOWER_TYPE_*`) before the bodies, 64 types at a time: each
  type's kind looks both up in 64-byte tables with `vpermb`, and the few whose
  kind doesn't settle them (an aligned typedef's, an enum's, a `_BitInt`'s, a
  complex type's, and the builtins) take the rules, `check_lower_type_class`
  and `check_lower_type_facts`, which its twin,
  `check_lower_classify_types_scalar`, applies to every type. The bodies read
  the table (`check_lower_class`, `check_lower_facts`), which has a row per type
  of the unit's table, as the lowering makes none: 4,740 for `sqlite3.c`.
- **The checked value in place.** Each expansion copied its node's 96-byte
  `CheckValue`; it reads it where it is.
- **No task for an empty conversion.** A conversion that writes no entry and
  can't fail (an array's decay, a pointer's to another, a change of qualifiers:
  `check_lower_conversion_empty`) isn't pushed.
- **An entry's append.** In a function that doesn't track its stack
  (`CheckLowerWriter.track`: one without a statement expression, in a program
  without a terminal helper), `check_lower_emit` appends without the operand
  class's bookkeeping, and the function's memory classes (`Tier0Function.x87`
  and `int128`) come from its entries' classes once it is lowered
  (`check_lower_entry_classes`, 64 at a time, and its twin).
- **Lane 0's writer's columns in base pages** ([the lanes
  guide](function-lanes.md#memory)): in the caller's scratch, each cleared a
  huge page.
- **Each statement's nodes prefetched a statement ahead.** The tasks' loop
  waited on its first touch of each node's checked value and conversions, two
  arrays too large for the caches. A statement's nodes come before its root in
  the graph, so before each statement the lowering prefetches the next one's,
  up to its root (`check_lower_prefetch`, its last 64 nodes at most). #1048
  removed it once the values were columns (#628), when its own work cost more
  than the misses it hid ([the lowering guide](c-lowering.md)).

The objects are the same bytes. On the 9700X, against main at `ad408cf2`
(medians: the whole compile's of 10 rounds of `perf stat -r 3`, the two in
turn, each after a warm-up of its executable; the stage's of 11 rounds of
`measure-compile` with #520's clock, each build the other's reference; the
emission's of `benchmark-check-lanes --lanes 1`'s 11 passes, twice each):

| `sqlite3.c` | main | #531 |
| --- | ---: | ---: |
| `lower` stage | 25.6 ms | 17.5 ms |
| its system time and peak RSS added | 2.9 ms, 56,044 KB | 0.0 ms, 9,824 KB |
| `-c`, task-clock | 190.5 ms | 181.8 ms |
| user cycles and instructions | 902M, 2,227M | 864M, 2,133M |
| kernel cycles | 155M | 145M |
| peak RSS | 674,216 KB | 627,200 KB |
| the emission on one lane, warm memory | 37.1 ms | 30.0 ms |

What is left is the tasks' loop's dispatch on tasks and operators, and the
first touches the prefetch doesn't hide; `tier0_validate`'s check of every
lowered function, a scalar walk of the stack with a mispredicted `switch` per
entry, 16% of the stage (#569); and the preparation's passes over every token
(#517).

## Correctness tests

```sh
./build/compiler test-checker --seed 133 --clang clang
./build/compiler test-checker --seed 1 --clang clang
./build/compiler test-checker-driver --clang clang --executable ./build/compiler
./build/compiler test-checker-asm --clang clang
./build.sh check-call-graph
```

`--clang` selects the independent compiler executable; it defaults to `clang`.
The test command fails if the oracle cannot run. It covers:

- Independent action results for integer widths, promotions, overflow, division
  and shifts; floating precision and 128-bit conversion midpoints; qualifiers,
  arrays, pointers, members, calls and modifiability; and the distinction between
  evaluation faults and constraints in unevaluated operands.
- Random action batches and legal partitions, poisoned result storage, and
  readable arrays ending immediately before protected pages, including empty
  inputs and lengths around vector boundaries.
- True dependency chains and wide graphs, random DAGs, a 65,537-node chain,
  every legal scheduler batch size, fresh type-table identity comparisons,
  source-ordered diagnostics, invalid topology and call slices, and arena
  exhaustion/rollback. Guarded graph columns test all logical lengths 0–33.
- Real preprocessing and both parser schedules, separately initialized type
  tables, source adaptation and both checker schedules. Small source fixtures
  compare acceptance and the first error line with Clang in GNU C17 mode.
  Generated unsigned arithmetic assertions are checked independently; mutations
  of their expected values must be rejected. Explicitly unsupported valid C
  fixtures require an unsupported report rather than claimed Clang parity.
  GNU folding has separate strict-action and source-extension fixtures,
  including warning provenance and forms that remain ineligible for an ICE.
- Independent declaration identities across nested/prototype scopes, source
  event batch/scalar equality, fallback and exhaustion, complex declarators,
  record/enum constraints, loop controls and update/store metadata.
- Static source definitions through `Link` and the ELF writer, independent
  Clang object bytes and relocations, guarded batches and transaction rollback,
  signed bit-precise storage and deterministic x87 padding. Character UCN tests
  distinguish GNU17 constraints from GNU23 acceptance.
- Bit-precise constants (#768): their widths, signedness and values from the
  literal reader (`check_literal_number`) in each radix, case and order of
  suffix, with digit separators, past 64 bits and at 128 and 129, and the
  spellings C23 doesn't have; their types by `_Generic` and their values by
  static assertions in a C23 source fixture, by both paths and against clang,
  and at run time in a program against clang -O0's
  (`bit-precise-constants`); and a 129-bit one refused by name.
- Named UCNs (`\N{...}`, #1447), which clang reads as its extension in C, in
  identifiers, `#if`'s character constants, literals and a static assertion's
  message, accepted by both paths and the default route, writing an object
  (`test-checker-driver`'s `check_test_driver_named_ucns`); their errors and
  notes, whole lines against clang's (`check_test_driver_unevaluated_strings`);
  and their values at run time against clang's build
  (`named-universal-character-names`).
- `#pragma clang section` and `#pragma clang optimize` (#619;
  `test-checker-driver`'s `check_test_driver_section_pragmas` and
  `check_test_driver_optimize_pragmas`, on all four routes): each line's
  settings and errors, and the section type conflicts in clang's order, against
  clang's first error, place and message; the refusals by name pinned; and
  `-c` of each unit clang takes, whose object must put every function, object
  and thread-local in a section of the name, type, flags and alignment clang
  -O0 -c's object puts it in, and have a file-scope `asm`'s sections as
  clang's, with their bytes (`check_test_driver_same_sections`); its
  executable by the direct path, which must exit with 0; and `-c`'s refusals by
  name, the conservative active-object-setting refusal and its
  no-active-object-setting controls among them.
- Executable programs (`c_check_program_test.c`): each source's checked
  object, from both paths, linked with clang's build of the same source (its
  oracle, each global renamed `oracle_NAME`) and a harness that runs both on
  the same inputs. The harness is compiled once per run and each oracle once
  per distinct text, with `-c` and the flags of the one-step build they
  replaced, and a path whose checked object and oracle are byte for byte the
  last linked program's takes that program's run: the same files would link
  into the same executable (#465). A control's oracle is clang's build of
  another source of the same names (`check_test_program_against`): a model of
  what the program computes, or a mutant of it. The program must agree with
  it, or with `mismatch` differ from it on some input (the harness's status
  1). A run is taken from the last program only if that run expected the same
  outcome. fpclassify's selector programs have both kinds (#148). Their
  models, for a double, run the normal and subnormal selectors in an order
  that C's sequencing fixes. The programs agree with the model that runs the
  subnormal selector first, as a GCC-built clang does. With the
  order-recording encoding they had, ours must agree with the model that runs
  the normal selector first and differ from the other, so a change to
  `check_lower_floating_prepare`'s order must change these two controls. Their
  mutants evaluate the normal selector twice, the subnormal one never or the
  argument twice. A program that fails is linked and run, and its directory
  kept with `driver.c` and `oracle.c`.
- The passes after checking that visit a unit's few records rather than every
  token (#592), on every program fixture by both paths
  (`check_test_program_sparse`): `check_data_prepare` against the dense pass it
  replaced, kept as the test's oracle, record for record, with each object's
  last declaration (`check_data_latest`) and the static declarations' keys;
  `check_program_token_body` against the token-sized column of every body's
  tokens; and the objects `check_program_object_access` finds the unit defines
  against a walk of every token. `check_init_declarations` runs at every count
  to 200 and larger ones, ending at `PROT_NONE` pages, beside #442's kernels.
- The diagnostics' report (`check_driver_reports`, #592) against the walk of
  every token it replaced, kept as the test's oracle
  (`check_test_driver_reports` in `c_checker_driver_test.c`): units with
  warnings of each kind, an error and a fatal error, reported as checked and
  with their sparse records rewritten at random (constants' and nodes'
  warnings, floating-point pragmas and packed members' addresses in token order
  and out of it, the original pragmas apart, reports, the driver's own
  diagnostics and excess elements, quiet and not). The text, the counts and the
  fatal flag must be the oracle's by both paths.

The fixtures that ask clang for acceptance and first errors go to it many at a
time (`parse_test_clang`), split in order among up to eight processes that run
at once, each process's output read for its own files: clang checks each file
on its own, so the results are those of one process (#465).

Where clang accepts a constraint violation with a warning, the checker accepts
it with clang's warning, as the semantic mutants require: an assignment that
discards qualifiers, nested pointer qualifiers (`int **` to `const int **`),
pointees that differ only in signedness, `void *` with a function pointer, a
comparison of distinct pointers or of a pointer with an integer, and a
conditional of either. The warnings clang makes errors by default
(`-Wint-conversion`, `-Wincompatible-pointer-types` and its function-pointer
group, `-Wreturn-mismatch`) are errors, but in a system header, where clang
suppresses them and converts as it would have. A token is in a system header
when its expansion location is (`check_input_system_tokens`): the preprocessor
judges every output token from runs its main loop keeps (`pp_system_levels`),
or with `--scalar` and for a unit whose runs can't say, by the expansion
history (`pp_system_levels_history`); `docs/c-preprocessor.md`'s "System
levels" has both.
Floating results are compared field by field, never by `long double` padding.

Packed-member addresses warn by default (`-Waddress-of-packed-member`, #507).
`c_check_packed_input.c` retains sparse source witnesses even in `sizeof` and
`typeof`: a direct chain of members must start at a declaration, contain a
packed record or field, and have either an insufficient enclosing alignment
or an offset incompatible with the final member's type. An explicitly aligned
object can strengthen a direct `.` access. `#pragma pack` alone does not trigger
this warning, nor do array decay, an address of an array element, or the complex
bases Clang currently excludes (a call, subscript or explicit dereference).
Anonymous members participate in both layout and the choice of packed culprit.
The reader counts actual layout witnesses before reserving their retained
storage. It bounds chain scratch by the largest candidate expression, and
builds conversion context only when a witness exists. An unused packed type
plus ordinary local addresses needs no diagnostic records or scratch.
The first anonymous diagnostic builds a temporary tag-to-source/context map;
later addresses reuse it, so formatting many anonymous locals stays linear in
the source size plus the emitted names.

`c_check_packed_diagnostic.c` cancels a pending warning when a source conversion
permits the culprit's reduced alignment, including integer and incomplete-pointer
casts. It follows conditional arms and a comma expression's right operand in
their implicit consuming context. Generic/choose selections transparently use
the chosen operand. A bounded source interval scan preserves pending warnings
inside unevaluated and unchosen operands, where Clang does not perform its
implicit-conversion analysis. An explicit cast cancels only a directly
addressed member; its target does not flow into conditional/comma operands.
A cast to `void` does not cancel it. Builtin lowering
conversions are distinguished from source conversions: `__builtin_assume_aligned`
retains its warning while the ordinary pointer parameters of `__builtin_memcpy`
can cancel it. The existing diagnostic pragma history controls suppression and
promotion, and `-w` silences the diagnostic. Source records remain ordered and
the driver merges them with its sparse warning and report keys.

The rules were checked against LLVM's `SemaChecking.cpp` and `SemaExpr.cpp` at
`llvmorg-23.1.3`, then probed with external Clang 23.1.4. No LLVM implementation
or fixtures were imported. `test-checker-driver` runs the original probes through
both checker schedules and, with `--executable`, both production command routes;
it compares warning decisions, complete messages and presumed line/column pairs
with Clang, and covers guarded classifier/interval tails and allocation failure.

### Intrinsics no host runs (`test-x86-intrinsics`)

```sh
./build/compiler test-x86-intrinsics [--clang PATH] [--march CPU] [--option FLAG]... [--executable PATH] [--cases FILE] [--print NAME] [--keep]
```

The executable programs check a builtin's lowering by running it, which XOP's,
AVX-NE-CONVERT's, AVX10.2's, SHA512's, SM3's, SM4's, MOVRS's and AVX512-BMM's
instructions can't be: no host of the tests has them (Zen 5 has none of them);
nor can the extensions' system instructions, whose extensions no host has or
whose effects are the machine's (a transaction's, the user interrupts', the
tiles' configuration, MSRs), or the leaves of Key Locker's, WAITPKG's and RTM's
instructions (`c_builtin_x86_helpers.c`), whose extensions the 9700X and the
hosted runners haven't.
`test-x86-intrinsics` (`x86_intrinsics_test.c`, #136) compares the compiler's
code of their intrinsics with clang -O0's instead, as the values each
instruction computes:

- A source includes `<x86intrin.h>` and `<immintrin.h>` and takes each case's
  address, so that each compiler compiles the case's `always_inline` definition
  out of line, as `--lower-all` of `main.c` lowers the headers' definitions:
  clang with `-O0 -march=znver5 -c` (`--march` names another CPU, and each
  `--option` adds a flag to every compile), this executable (or
  `--executable`'s) by both paths, whose objects must be the same bytes.
- Each case's function in each object is decoded (`x86_64_disassembler.c`)
  and followed from its entry. Every general register's, vector register's,
  opmask's and stack byte's value is an expression of the arguments' bytes:
  the argument registers (`g0` to `g5`, `v0` to `v7`) and stack slots (`s0`
  on) at the entry, constants (the object's data, which relocations name), and
  a node for each instruction that isn't a move, of its form (VEX's and EVEX's
  encodings of one instruction are one, a ModRM.rm operand of a register or
  memory compared as its register's: VEX's shifts by an immediate take a
  register alone, and EVEX's either), its immediate and its operands'
  values, a masked one's a select of its mask, its value and what it keeps,
  over lanes of the destination's elements (a conversion's no more than its
  source's: `vcvtph2bf8`'s eight bytes of an XMM register's halves).
  What an instruction reads and writes is its form's effects in
  `x86_semantics.c`; a form without them fails the case and is listed. Moves,
  pushes, pops and stack slots carry values, and the trace follows jumps in a
  function and calls of the object's other functions (the compiler's code calls
  the definitions clang inlines). An implicit general register is read whole,
  but UMWAIT's, TPAUSE's and LOADIWKEY's EDX:EAX and EAX, which are 32-bit
  halves; Key Locker's XMM registers that no role names (`x86_implicit_vectors`)
  are operands after the explicit ones, and each one written is the node's part
  for it, `node.x<i>`.
- At a conditional jump or `xbegin` the trace follows both paths (up to 8),
  each with its conditions: a jump's is a condition code's boolean of the flags
  it tests, `set<cc>(flags)` of the even code of its pair (`e` for `e` and
  `ne`), holding or not where the jump is taken; a jump on `e` or `ne` after a
  test of a condition's boolean with 1 or itself, or its compare with 0
  (clang's `sete`, then `test $1`), is the boolean's own condition; and
  `xbegin`'s is its node, holding on the path to its fallback, where EAX is the
  abort status, `status(xbegin#...)`. A trace of several paths is each path's
  conditions' line (`path ...`, its conditions in their texts' order, `=1`
  where one holds and `=0` where it doesn't) and its result and stores, the
  paths in those lines' order. A vector register's zero idiom (`pxor`) leaves
  the flags, which a general register's (`xor eax, eax`) defines.
- A case's result (its return register's bytes of the intrinsic's type) and
  its stores through pointers must be the same expressions in both objects, and
  so must its effects, in order among the stores: each instruction that writes
  no register or memory, and no flag or is a system instruction (a fence,
  SERIALIZE, LOADIWKEY, and UMWAIT, TESTUI and ENQCMD, whose flags its result
  may read), as its node, and each prefetch's and cache line's hint as its form
  and its address. A memory
  operand the form's table gives no size reads or writes its effects' width
  (`ldtilecfg`'s, `sttilecfg`'s and `enqcmd`'s 64 bytes).
- Controls: intrinsics that the executable programs run already, unmasked,
  merged and zeroed (`_mm_add_epi32`, `_mm256_dpbusd_avx_epi32` and their masked
  forms), and two XOP compares, whose code is AVX-512's, must agree too, and so
  must AVX10.2's masked store of bf16's first element (`_mm_mask_store_sbh`),
  a result stored through a pointer; and one intrinsic called with its two
  operands in each order must give different expressions in each object.
- The cases, 466 of them: the intrinsics of XOP's own instructions (VFRCZ's
  fractions, the horizontal sums and differences, the multiply-accumulates,
  VPPERM and the shifts by vectors of counts), AVX-NE-CONVERT's loads,
  AVX10.2's intrinsics of AVX-VNNI-INT8's and AVX-VNNI-INT16's dot products,
  masked, merged and zeroed, at each width, SHA512's, SM3's and SM4's; and
  AVX10.2's saturating conversions, bf16 operations and compares, conversions
  to and from 8-bit floats and of two vectors of floats to halves, and dot
  products of halves, MOVRS's vector loads and AVX512-BMM's products of bit
  matrices, each unmasked, merged and zeroed where the headers have them; and
  the system instructions of RAO-INT, CLDEMOTE, SERIALIZE, UINTR, PTWRITE,
  WAITPKG's UMONITOR, LWP, MOVRS (its integer loads and prefetch), RTM's XEND
  and XTEST, TSXLDTRK, AMX-TILE's configuration, ENQCMD and USER_MSR; and the
  leaves' intrinsics, Key Locker's 11, WAITPKG's UMWAIT and TPAUSE and RTM's
  XBEGIN; and AVX10.2's bf16 arithmetic, square roots and fused multiply-adds
  at each width, with four of their masked forms, which in znver5's unit clang
  computes in float (its excess precision: each operand widened, the floats'
  operation, VCVTNEPS2BF16's rounding back), a negated operand of a
  multiply-add folded into its form.
- A second run, `--march diamondrapids --option -mno-apxf --cases
  tests/x86-intrinsics/avx10.2-units.txt` (`tools/ci/correctness.tests`),
  compares 16 of the bf16 cases and a control in a unit whose target has
  AVX10.2, whose arithmetic is AVX10.2's own (VADDBF16 and the rest).
  `-mno-apxf` keeps clang's prologues free of APX's REX2 prefixes, which the
  disassembler doesn't decode.
- Inverted cases: clang's code of Key Locker's eight that encrypt and decrypt
  stores the result where ZF is 1 and zeros where it is 0, the other way from
  Intel's manual, clang's own `keylockerintrin.h` (`ZF := 1`, `MEM[__odata] :=
  0` for a handle that isn't valid) and GCC's code, which the compiler's leaves
  follow; their cases compare clang's paths with each condition negated
  (`X86IntrinsicCase.inverted`). Compared as they are (`--cases`, which has no
  such flag), they differ, and the cases without a branch agree.

The comparison is of the instructions' data flow, not of their meaning: code
that computes the same values with other instructions differs, and a case would
then need another check. Rules broken on purpose, a shift's source and counts
swapped, a saturating multiply-accumulate for a wrapping one, an unsigned
horizontal sum for a signed one and a packed fraction for a scalar one, each
make their own case differ and no other; and so do leaves broken on purpose: the
one-block Key Locker leaves' branch on `je`, the wide ones' first two blocks
stored the other way round, `encodekey128`'s type from esi, `encodekey256`'s
last part from xmm2, `loadiwkey`'s keys swapped, the waits' halves swapped and
`xbegin`'s -1 for 0, which made exactly their 14 cases differ; and bf16's
widening shifted by 15 rather than 16, and `fmsub`'s form `fmadd`'s, which made
exactly their 18 cases differ (the operators', the `fmsub`s' and the masked
forms that call them).

`--cases FILE` takes other cases, a line each: `X`, `Y` or `Z` for a vector
result of 16, 32 or 64 bytes, `B`, `H`, `I` or `L` for an integer of 1, 2, 4 or
8, `V` for none, then the intrinsic's name. `--print NAME` prints each object's trace of a
case, every record it followed with the value it made. The files are removed
unless `--keep` or a failure.

## Common warnings (#823)

`c_check_common_warnings.c`, included by the driver, reports the expression
warnings that need the completed graph or the parser's operator tree:
`-Wdivision-by-zero`, `-Wnull-dereference`, `-Wconstant-conversion`,
`-Wliteral-conversion`, `-Warray-bounds`, `-Wsign-compare`,
`-Wunused-comparison`, `-Wunused-value`,
`-Wlogical-not-parentheses`, `-Wparentheses` (assignment conditions and
bitwise/equality precedence), `-Wempty-body`, `-Wself-assign`,
`-Wreturn-stack-address`, `-Wformat`/`-Wformat-security`, `-Wswitch`,
`-Wdangling-else` and `-Wuninitialized`. A chained comparison (#2264,
`check_common_chained_comparisons`) is clang's `-Wparentheses` error by
default: a `<`, `>`, `<=` or `>=` whose left operand is one, unparenthesized,
"chained comparison 'X < Y < Z' does not behave the same as a mathematical
expression" at the outer operator; `-Wno-error=parentheses` demotes it and
`-w` leaves it an error, which the common checks under `-w` still give
(`check_common_default_error`), with no `-Werror,` in its option. `-Wsizeof-array-decay` (#2264, on by
default, `check_common_sizeof_array_decay`) is clang's
`warnOnSizeofOnArrayDecay`: `sizeof` of a binary operator, through
parentheses, whose operand is an array decayed to the operator's own type
(`sizeof(a + 1)`, `sizeof(0, a)`, `sizeof(p = a)`), "sizeof on pointer
operation will return size of 'int *' instead of 'int[10]'" at the operator,
once for each such operand, the array's type as written; published past the
`sizeof`'s `)`, after its operand's `-Wunevaluated-expression`. Not alignof's
operand. `-Wshift-op-parentheses` (#2264, on
by default, `-Wparentheses`' and so `-Wall`'s,
`check_common_shift_op_parentheses`) is clang's `DiagnoseAdditionInShift`:
each operand of a `<<` or `>>` that is an unparenthesized `+` or `-`, the left
one first, "operator '<<' has lower precedence than '+'; '+' will be
evaluated first" at the `+` with its note, given as the shift is built; a `+`
from a macro's argument shows that macro, its range being the `+`
expression's. `-Wstring-plus-int` (#2190, on by
default, `check_common_string_plus_int`) is clang's `diagnoseStringPlusInt`: a
`+` of a string literal, not parenthesized, and an operand of integer,
boolean or enumeration type in either order, "adding 'T' to a string does not
append to the string" at the `+` with its note, T the operand's type as
written (a typedef's, `__size_t`'s sugar), given as the `+` is built; a `+`
whose neighbor is no string literal is passed at once. Left: where a C23
`u8"ab" + 1` is returned as a `const char *`, its `-Wpointer-sign` comes
first, which clang gives after. A conditional's
`-Wpointer-type-mismatch` and `-Wconditional-type-mismatch` (#2292,
`check_driver_conditional_mismatch_message`) name its arms' types as clang's
`checkConditionalPointerCompatibility` and `checkPointerIntegerMismatch` do,
"pointer type mismatch ('int *' and 'long *')": each as written (a typedef's
with its `aka`), an array's or a function's decayed, a GNU `?:`'s left arm its
condition, at the `?`. A divisor `-Wdivision-by-zero` reads
as clang's EvaluateAsInt does (`check_common_zero_divisor`): an integer
constant, or, through integer casts, a const non-volatile object whose
completed initializer's receipt (`CheckInput.const_values`) gives zero
(`const int k = 0; 1 / k`, an element's or a member's too, #1447); not yet
arithmetic or a conditional of such reads (`1 / (k + 0)`), which clang folds
too. `c_check_binding.c` adds
`-Wunused-variable`, `-Wunused-const-variable`, `-Wunused-function` and
`-Wunused-parameter` as sparse events, with the declaration's body deciding
the completion, so they follow Clang's after-function order; a redeclaration
inherits an earlier `unused` or `used` attribute. A file-scope variable's
unused warning is given only in the main file, as Clang leaves a header's
file-scope variables alone because every unit that includes the header shares
them. `check_input_default_const` (#2168, clang's `ActOnUninitializedDecl`) adds a
const object without an initializer, at its name, with its type as declared.
- **Automatic storage:**
  - `-Wdefault-const-init-var-unsafe` where its type is const-qualified (an
    array's elements', an `_Atomic`'s, a variable array's too);
  - `-Wdefault-const-init-field-unsafe` where it is a struct whose first
    const member, found depth first through its structs and unions
    (`getConstField`), isn't an array of const elements nor a flexible array
    member. Its note, "member 'm' declared 'const' here", is at that member's
    declarator (`check_input_member_declarator`);
  - both, for a const struct;
  - none, for a union or an array of structs.
- **Static storage:** `-Wdefault-const-init-var` alone, off by default (in
  `-Wdefault-const-init` and `-Wc++-compat`). A file scope's object, a
  tentative definition, gets it once, at the unit's end, from the tentative
  definitions' walk (`check_input_tentative_arrays`): at its last tentative
  definition, with its type completed (`'const int[1]'`), after its tentative
  array's warning, and none where a later declaration defines it.
- **The C++ words.** The unsafe ones say "and is incompatible with C++" where
  `-Wc++-compat` is on there, as clang selects them by that group's fake
  diagnostic: `CHECK_WARNING_ID_CXX_COMPAT`, a policy no event gives.
A block's declaration with linkage (an `extern` object, any function
declaration) is never unused: clang's semantic context for it is the
translation unit, not the function, so `ShouldDiagnoseUnusedDecl` passes it
by. A use through it uses the outer declaration with linkage it redeclares,
so `static int s; void f(void) { extern int s; (void)s; }` leaves `s` used.
A block's own `static`, `register` and automatic objects can be unused. A
variable with an applied `cleanup` attribute is used, as clang treats it,
since its cleanup takes its address (#1881); one ignored at file scope can still
be unused. The
graph pass adds `-Wunused-but-set-parameter` for a parameter a statement
assignment writes and no read uses.

Each uses the shared warning policy, so a default or group option, a
`#pragma clang diagnostic` change and `-Werror` promotion behave as the other
sparse warnings do. A token from a system-header macro is suppressed by its
spelling location, as `assert`'s empty `if` is, unless `-Wsystem-headers` asks
for it. The `-Wswitch` check needs a named enum's source definition and literal
enumerator `case` labels; it compares the enumerators' checked values, so
aliases (`A = 1, B = 1`) that a case handles are not reported missing. A
computed case leaves the switch to a full constant evaluator.
`-Wuninitialized` follows graph evaluation order: a local scalar object read
before any write and not address-taken warns; a `sizeof` operand (except a
variably modified type's bound), a void cast, an address computation, an
unselected `&&`/`||`/`?:` arm, a static selection's unselected arms and
controlling expression, and an assignment target are not reads, while an
initializer's value, a compound assignment and an increment are, with the write
ordered after the read. A brace initializer writes its object even without an
expression node, and a variable read in its own initializer (braced or not) gets
Clang's "within its own initialization" message. A compound literal's unnamed
object is no variable and never warns, as Clang's analysis follows variables
alone: its own value, at its closing `}`, is not a read in its initializer,
while the variables that initializer reads still warn. The brace initializer's
read is found by a scan of its tokens (#1349). An AVX-512 pass, with its scalar twin
under `--scalar`, keeps only identifiers spelled as the object's name that don't
follow `.` or `->`, 64 tokens a block. Only those climb the parse tree for an
enclosing `sizeof`, and the climbs are memoized per unit, so a table of N
elements costs O(N). An unmodeled write through inline
assembly or a builtin suppresses the warning, and `-Wunused-but-set-parameter`
keeps a separate read that is not the target of the same compound assignment or
increment.
`-Wformat` checks the four `printf`-family names, a declaration's
`format(printf, ...)` attribute, the integer class for every length modifier
(`long` against `long long`, `%zd`, `%jd`, `%td`, ...; the signed and unsigned
types of one class agree), `%n`'s integer pointer by class (`signed char *`,
`short *`, `int *`, `long *`, `long long *`, and the unsigned counterparts),
each `*` field width's `int` and `.*` field precision's `int`, extra and
insufficient arguments, positional `%1$d`/`%2$d` conversions with positional
widths and precisions and their used-argument counting, C23 `%b`/`%B` (whose
expected name is the signed class), and wide `%ls`/`%lc` (and `%S`/`%C`)
arguments with a signed 32-bit `wchar_t` or `wint_t`, and literal formats'
common conversions and sizes. Its literal-internal diagnostics keep Clang's
column and the diagnostic collector's source order. The attribute's archetype
is compared after the attribute checker's reserved-name stripping, so
`__printf__` and `__gnu_printf__` check as `printf`. A `va_list` callee
(`format(printf, N, 0)`) keeps scanning past an invalid conversion, as Clang's
data arguments then start at the call's first: a positioned `%N$Q` stops the
scan only once N passes the call's argument count. A conditional format whose
condition folds selects its arm by the converted value, so
`(unsigned char)256 ? "%d" : "%s"` checks `"%s"`.
A common-warning pass whose scratch allocation fails reports
`fatal error: common warning capacity exhausted` and fails the unit rather
than publishing fewer warnings; the binding's unused-name columns report theirs
as the checker input's resource error. The test hook
`BUSTER_TEST_COMMON_WARNING_FAIL_AT=N`, read in every build as
`BUSTER_TEST_LANE_FAIL_AT` is, fails the unit's Nth (from 0) common-warning
scratch allocation.
`-Wunused-value` covers a bare identifier or literal, a parenthesized
comparison, a cast (including a typedef of `void`), a conditional, and a comma
expression, parenthesized or not; a comma operand with a call, assignment or
increment has an effect and does not get the left-operand warning. Whether a
discarded value warns, and where, is clang's `Expr::isUnusedResultAWarning`
(`check_common_unused_of`): a comma's value is its right operand's, but a
literal `0` there (`(x = y), 0`, parenthesized or not) is quiet; a conditional
warns where both arms do (a GNU `x ?: y` where its else arm does), at the else
arm's value, which clang evaluates last, so `n ? (f(), 0) : (g(), 0)` is
quiet; a `&&` or `||` warns where both operands do, at the operator (#1390); a
`_Generic`'s or `__builtin_choose_expr`'s value is its chosen operand's and a
statement expression's its final result's (one without is quiet); a call warns
where its result does (below), and an assignment, an increment, a volatile
object's read and a cast to void never do. The walk runs only for a discarded statement's or for clause's
value and a comma's operands, rather than as a fact of every operator, and
takes the left operands first while a 64-deep stack holds the right ones and
each logical operator's own mark (a deeper nest is quiet). Its reports complete
with the function's closing brace, as Clang's do, after the body's own warnings
and before an unused declaration's.
A discarded `__builtin_offsetof`, `__builtin_types_compatible_p`, place builtin
(`__builtin_LINE`, `__builtin_COLUMN`, `__builtin_FILE`, `__builtin_FILE_NAME`,
`__builtin_FUNCTION`) or type query (`__builtin_vectorelements`,
`__builtin_omp_required_simd_align`, `__is_destructible`,
`__is_nothrow_destructible`, `__is_bitwise_cloneable`) reports there too, at its
keyword, as a value of a conditional's arm or a comma's operand does, and so
does a `_Generic`'s or `__builtin_choose_expr`'s chosen operand without an
effect. `-Wunused-value` is in `-Wunused`, `-Wmost` and `-Wall`, with
`-Wunused-comparison` and `-Wunused-result` under it.
A discarded `&&` or `||` (#1390) is quiet where either operand accepts the
discard: an ordinary direct call, an assignment, an increment, a volatile read,
or a nested logical operator, comma, conditional, selection or statement
expression whose value does. A call beneath arithmetic, a comparison, an
explicit nonvoid cast or a conditional's condition doesn't silence it, and the
rule is syntactic even for a short-circuited operand. A call whose result warns
keeps the operator's warning: accepted `pure`, `const` and `warn_unused_result`
contracts, inherited per kind through the source-visible redeclarations without
changing earlier uses or leaking block declarations; a return type's contract;
and a library builtin's implicit const or pure, `abs`'s or `isalpha`'s, which
disabling the builtin removes while explicit contracts still apply (an
internal-linkage spelling is an ordinary function). Two cases keep a logical
operand's call warning conservatively (`check_common_unused_unknown`): in a unit
with an accepted `warn_unused_result` typedef, whose provenance a return type
spelled through `typeof` carries past the declarator, and for an overloaded
callee, whose candidate's contracts the name's declaration needn't hold. The
original fixtures in `c_check_logical_unused_driver_test.c` compare the complete
Clang output, warning policy, scalar/vector paths and requested lanes in
syntax-only and object modes.
A discarded call's result (#1447, `c_check_ignored_result.c`, clang's
`DiagnoseUnusedExprResult`): a call whose callee is declared
`warn_unused_result` or `nodiscard` is "ignoring return value of function
declared with 'warn_unused_result' attribute" (`-Wunused-result`), named as
written (a scoped one with its scope, `'gnu::warn_unused_result'`) and with a
standard unscoped `nodiscard`'s message after a colon; one whose return type's
struct, union, enum or typedef (through further typedefs, the tag first) is
declared so is "ignoring return value of type 'S' declared with 'nodiscard'
attribute" (`-Wunused-value`); and one declared pure or const is "ignoring
return value of function declared with pure attribute" (`-Wunused-value`),
unless the call is written in a macro's body or a system header's macro (a
macro's argument warns). Clang's builtin table gives a builtin's const and pure
(its c, U and g rows; an e row's const needs -fno-math-errno, which clang's
default isn't), a plain library function's only where the unit keeps the name a
builtin and declares it with external linkage. These report at once at the
callee's first token (its own parentheses', `(w)(n)`'s, a dereference's or an
address's, but not a group's around the call, `(w(n))`): a statement's and a
for clause's call, and the call a comma's left operand or a discarded comma,
conditional or selection leaves as the value that warns
(`check_common_unused_of`), so that `n ? (n, w(n)) : (n, w(n))` and
`(n ? w(n) : w(n)), 0` report the else arm's `w`, and `(a, n ? e(n) : w(n)), x`
nothing. They warn in unreachable code too, as clang's are no runtime
diagnostics: the walks leave out an unevaluated statement and comma (after a
`return`, in a dead `for` clause, a constant condition's other arm, a
short-circuited side), whose value's and left operand's calls a pass of their
own then reports (`check_common_unused_unevaluated`, the root loop's and a scan
of the commas, 16 operators at a time); an unevaluated operand's report drops
itself. A pure or const call stays quiet where the discarded expression itself
is written in a macro's body (clang's `ShouldSuppress`): a call's callee, a
comma's operator, a conditional's first token. A call through dereferences and
addresses (`(*f)()`, `(&f)()`) is its function's, and a call of a pointer
object (a parameter's too) reads the typedef its declarator spells past its
`*`, or past the `*` of the pointer typedef its specifiers name
(`check_common_ignored_pointee`). The marks are found once a unit, from the
tokens that have attributes: a function's by its first declaration, each
redeclaration's in source order, so that a call reads those up to the
declaration its lookup finds; a call then tests one bit of its callee's first
declaration and one byte of its name.
A statement expression in an unevaluated operand drops them, as clang does
(`check_common_report_discarded`). The functions stay out of line, as does the
discarded selection's marker: inlined into the emission's loops they cost
sqlite3.c's `-fsyntax-only` instructions even where no call warns. A call whose
callee has no function's name reads its return type's typedef from the
declaration that spells the pointer (`check_common_ignored_callee`): a member's
declarator, an element's or a pointer to a pointer's object (through its
further `*` and its bound), another call's callee's return typedef, a comma's
or a conditional's value (its true arm), and reports at the callee's first
token. A call a
macro's argument writes gets clang's "expanded from macro" note for each macro
whose argument it is (#1447): clang's macro backtrace leaves an argument's own
note out only where every range the diagnostic highlights is one token of that
argument, and a call's range spans its callee and its parentheses. The checker
marks such a diagnostic (`check_common_report_spanning`,
Preprocessor.diagnostic_spans), which the renderer reads while its primary line
renders. Left: the other warnings at a macro argument, whose ranges clang
23.1.5 treats unevenly (an unused comparison's and a lone unused value's, and a
comma's left operand's, get the note; an unused binary operator's and a sign
comparison's don't), and the notes of a warning's own notes, such as the unused
comparison's fix-it.
`-Warray-bounds` reports a direct array object, including a nested subscript
such as `b[1][5]` at `b`, treats a zero extent as unknown, and leaves
struct-member arrays to a full bounds analysis. `-Wunused-function` reports a
declaration chain once, at its definition when it has one and otherwise at its
first declaration; a shadowing local use does not keep a file-scope variable
used. `-Wreturn-stack-address` traces `&x[0]` and `&s.m` to their local object
but not `&p->m` through a pointer. As clang's `checkExprLifetime`, it warns
only for an address the return returns: through casts, pointer arithmetic, a
comma's right operand, either arm of `?:` (a constant condition's too) and the
arm a `_Generic` or `__builtin_choose_expr` selects. A dereference, a
comparison, a comma's left operand, a `?:` condition (a GNU `?:` included), a
selection's controlling expression and its other arms consume it or don't
return it. `-Wempty-body` leaves an `if` that has an
`else` alone. A loop and its following statement use their outer macro
invocations' columns, including an argument-first or nested expansion (#1339).
Only an enabled candidate whose position comes from a macro requests deferred
expansion history; file-spelled positions and units without empty-body candidates keep
that history deferred. A macro's position that the file alone gives, one
invocation between two file tokens (`pp_output_invocation_place`), keeps it
deferred too; `--scalar` asks the history, its twin (#628). The driver tests compare quiet, indented and same-line
body, object, argument, nested and pasted macro cases with Clang on both paths,
and check that file-spelled candidates and suppressed warnings do not replay history.
`-Wparentheses` covers a `for` condition as well as an `if`,
`while` or `?:` condition; a write in that condition is a use of the parameter,
not an unused-but-set statement write. The `-Wswitch` definition is the one its
selector's tag type names, so a fixed underlying type and a shadowing tag in
another scope resolve correctly. The return-flow walk runs for non-aggregate
functions only where `-Wreturn-type` is enabled, or for a `noreturn` function's
independent `-Winvalid-noreturn` fall-off warning, and for them takes only the
warning's walk: the first walk is an aggregate return's alone, whose lowering
reads its result (#1340). The walk's per-node facts, whether an expression
guaranteedly stops or may not return (`check_program_return_facts`), come from
its calls, miscellaneous and x86 builtins and queries: the vector path
(`check_program_return_sparse`) tests each node's operands' and arguments'
facts, a byte each, and computes only a source's (found 16 operators at a time)
or a node's with an operand that has facts; the scalar path, its twin and
oracle, computes every node's (`test-checker` compares the two on every program
fixture). The `analyzer_noreturn` scan takes the attributes column's tokens,
and each callee name's answer from clang's builtin table is looked up once. Explicit returns in a `noreturn` function warn at the return token even in dead source; reachable fall-off warns at the closing brace. Repeated qualifiers, storage classes and function specifiers carry `-Wduplicate-decl-specifier` at the repeated token, including pointer and parameter qualifiers. These warnings use the common command-line and source pragma policy.

Literal floating-to-integer conversions warn for the supported initializer, assignment, return and call-argument paths, including conditional arms and top-level comma values. Literal conversion warnings do not descend through a comma inside a conditional arm, or through a nested comma or conditional inside a comma's value. A floating literal a truth context converts to `_Bool` warns as clang's CheckBoolLikeConversion gives it ("changes value from 1.5 to true"): the operands of `!`, `&&` and `||`, a statement's or a conditional's condition and a static assertion's, never a conditional's arms, a comma's result or `sizeof`'s operand (`check_common_truth_operands`).

A literal conversion is reported at once, as clang's `Sema::Diag` reports
`DiagnoseFloatingImpCast` and `CheckBoolLikeConversion`, not with the runtime
warnings `DiagRuntimeBehavior` defers to the function's end (constant
conversions, shift counts, division by zero): it is published where its full
expression completes (`check_common_full_expression_end`: the end of its
root, or of the declarator's initialization on the way, and a brace
initializer's end for all its parts), so every literal conversion of a body
precedes that body's deferred warnings. One full expression's conversions
share that boundary and keep the walk's order (a call's last argument first).
The phase after the lexer's at that boundary puts them before the next
statement's own diagnostics. At file scope a declarator's constant conversion
is published at the same boundary in the later phase, after its literal ones,
as clang delays it to the declaration's end. Where these warnings may report
(unreachable code and short-circuited operands, but not `sizeof`'s operand) is
unchanged.

The value warnings follow the target's type as clang's `CheckImplicitConversion`
does (`check_common_conversion_target`). `-Wliteral-conversion` takes a builtin
target alone, so never an enumeration, while `-Wconstant-conversion` takes an
enumeration by its integer type's range (`enum Small : signed char e = 300;`).
An `_Atomic` destination takes neither. Its conversion is to the atomic's value
type, which the lowering needs, so the action marks it
`BUSTER_CHECK_CONVERT_ATOMIC` wherever it strips `_Atomic`: assignments,
initializations, brace elements, returns and prototype arguments. An
assignment's or an increment's value is no constant to clang's evaluator, so
its conversion warns of nothing (`char c = x = 1000;`, `{ x = 300 }`), though a
comma's right operand's does (`check_common_conversion_leaf`, #1447).

A cast to an atomic type (#2230) converts its operand to the atomic's value
type, and its rvalue keeps the atomic type, as clang's `CheckCStyleCast` types
it (`check_action_cast`): `(_Atomic int)x` is an `_Atomic(int)`. An `_Atomic`
object of the same value type takes it (an initializer's, an assignment's, a
static's as a constant clang's evaluator folds), `__auto_type` deduces it, a
cast to void and `sizeof` take it, and the usual arithmetic conversions take it
as their left operand alone, as clang's `UsualArithmeticConversions` unwraps
the left operand's atomic type and not the right's (`check_action_arithmetic`:
`(_Atomic int)x * 2` is an `int`, `2 * (_Atomic int)x` an error). It is no
integer constant expression. Any other conversion of it is clang's
incompatible one (`check_driver_conversion_source` keeps an rvalue's atomic
type, where an lvalue's is its value's): "initializing 'int' with an
expression of incompatible type '_Atomic(int)'", a pointer's and a `_Bool`'s
too. A variadic argument's, clang's `-Wnon-pod-varargs` error, is refused by
name. Other operators refuse it in the checker's words as they refuse a
record's (clang's "invalid argument type '_Atomic(int)' to unary expression",
"statement requires expression of scalar type", #2441). Buster's `-c` emits
the static initializers clang's code generation refuses ("cannot compile this
static initializer yet" for `_Atomic(int *) p = (_Atomic(int *))&x;`).

A `_BitInt` constant past 128 bits (#1447, "Constants past 128 bits") warns as
a narrow one does, by the same rules on its whole value
(`check_wide_conversion_changes`, the twin of
`check_common_conversion_changes`, and `check_wide_same_width` of
`check_common_same_width`), into any narrower type, a wide one too, with both
values printed in decimal (`check_wide_text`). A declarator past a specifier's
parentheses (`_BitInt(100) n = e;`, `typeof(int) y = e;`, whose `=` the
parser hangs off the specifier's `)`) is evaluated: the unevaluated-operand
scan (`check_warning_graph_void_step`) counts only the tokens between the
parentheses as typeof's operand, where it took every token after them, and
dropped those declarators' conversion warnings. Still separate: clang's
`-Wtautological-constant-out-of-range-compare`, whose values these would
print, isn't given for any type, and neither is the static assertion's
"expression evaluates to" note.

A brace initializer's integer constants warn of their constant conversions too
(#1447), in the reverse initializer order clang's `AnalyzeImplicitConversions`
work list gives them. A plan whose part may change its constant
(`CheckInitializer.changed_integers`, a superset made as its parts append) is
the only one whose parts the walk reads for them. Clang's `DiagRuntimeBehavior`
queues them where the full expression completes, after the runtime warnings its
values queued as they were built, so a plan's constant conversions
(`check_common_initializer_integers`) follow the roots of its values, inside a
function at the body's end and never in unreachable code. A constant that fills
a signed target's width exactly (`128` to `signed char`, clang's
`isSameWidthConstantConversion`) is clang's `Diag` instead, reported at once
with the literal conversions (`check_common_same_width`), scalar or listed, and
never into a character element of a brace list, which clang takes for an array
initialization. At file scope clang queues a declarator's runtime warnings to
its end: its shift counts (`check_driver_file_initializer_end`) and constant
conversions publish there in `PP_DIAGNOSTIC_RUNTIME`, after its literal
conversions. Still separate: `-Wbitfield-constant-conversion`'s truncation into
a bit-field, and a same-width conversion in unreachable code, which clang still
reports.

Clang's `DiagnoseIntInBoolContext` follows every conversion to `_Bool` (#1447,
`check_common_int_in_bool`): the operands of `!`, `&&` and `||` and the
conditions of statements and conditionals (`CheckBoolLikeConversion`, the
operand itself and never a conditional's arms), a static assertion's condition
(#2409, `check_common_assertion_reach`; left: a failed assertion's, whose
root its error leaves unwalked, gives none, where clang's precedes the error), and an initializer's,
assignment's, return's or argument's conversion, whose conditional arms and
comma result carry it. Its implicit conversions passed, a `<<` of a literal 0,
or of two literals, always evaluates to one truth, which clang's evaluation
gives (a count past the width shifts by the width less one, a negative one
shifts right); any other signed `<<` is `-Wint-in-bool-context`'s, in `-Wmost`
and so `-Wall`. A conditional whose two arms are integer literals of its own
type, not both 0 or 1, and both nonzero always evaluates to true, at its
condition's first token. A literal is clang's `IntegerLiteral`: a number, under
at most a unary minus, with no parentheses. The always-forms are
`-Wtautological-constant-compare`'s, on by default, under
`-Wtautological-compare` and `-Wmost`. Clang's `Diag` reports them at once, as
a literal conversion, in unreachable code and short-circuited operands too, and
a brace initializer's in its reverse order; the walks' candidates test the
operand's operator before any type.

The graph's passes share their facts, `check_common_facts`, which give each
node its parent, whether another node uses its value, whether it is evaluated,
and its reach: a bit each for whether its subtree holds a node whose own
warning may fire, a division by a constant zero or a shift by a constant count
(#1394), a detached static-selection arm's shift, a comparison, or an implicit
conversion of a literal (#1340, #1453). A node's own bit counts only where its
tests can warn (`check_common_candidate_bits`): a constant whose value an initialization's,
an assignment's, a conversion's or a call's argument's conversion changes (the
types alone rule out a widening conversion, before the value's row is read), a
self-assignment's names, a constant index outside its array, a source comma.
What its warning tests past them only takes warnings away. The scalar path
makes the facts in one pass in graph order and the evaluated nodes by a pass
from the roots down. The vector path works on the graph's tree
(`check_common_parents`: each node's reader, 16 nodes at a time, each operand
slot's present children compressed to the front and written a child at a time,
since a scatter costs as much whatever its mask and most slots are absent; a
tree where every operand and argument precedes its node and no node has two
readers, which counting the distinct children against the edges tells, and
otherwise the scalar path). It visits only the nodes whose operators may have a
bit of their own, 16 at a time, and carries each bit up to the first ancestor
that has it; and takes the evaluated nodes as everything with a parent, then
clears, with all below them, the roots the control and selection walks left
out and the operands a `sizeof` or a constant condition leaves out. A root that reaches
nothing runs its statement's unused-value marker alone, without the walk's
frames, which would find nothing else. The comma markers' subtree starts come
from a search back from the operand by doubling steps, then halving, each
probe's climb stopping at a token shallower than the operand
(`check_common_emission_subtree_start`), rather than from a column of every
token's start. `test-checker-driver` checks the parents by both paths and the
starts against their definitions on random graphs and trees whose columns end
at `PROT_NONE` pages (`c_check_common_tree_test.c`).
The expression walk starts only at a root that reaches something or whose
discarded expression may get the statement's unused-value warning, and enters
only the subtrees that hold its walk's bit; every warning keeps its order. The default-ignored warnings' work runs only
where the command line or a source pragma may enable them: the comparison bit
for `-Wsign-compare`, a plain `x = y` for `-Wself-assign`, and the
`-Wuninitialized` pass for it and `-Wunused-but-set-parameter`, as Clang skips
an analysis whose diagnostics are ignored. The token passes find their roles
and kinds 64 tokens at a time, the node passes their operators 16 nodes at a
time through an operator bitset, and `-Wuninitialized`'s declaration loops a
sparse column's next entry by its directory (`check_sparse_next`). Each scalar
path, the twin, visits every item and runs the same tests.
A static selection's unselected arms and controlling expression
(`check_common_unselected_roots`, which `check_common_evaluated` and the
returned-address check use) are the roots inside its range, from past its
keyword to its closing token, but outside the operand it chooses, between that
operand's top-level commas (`check_common_chosen_span`): a statement
expression's statements there are roots of their own, evaluated where the
selection is (#2023); the ranges' bits leave each one's chosen operand out,
the selections found by their closing tokens, sorted. One pass finds
them for every selection (#1341, #1360): the `SELECT` nodes 16 at a time by
operator, their closing tokens as bits; their keywords from one pass down the
tokens below the last of them, 64 kinds at a time, each closing token claimed
by its last `_Generic` or type-argument child before it, as the definition
finds it; the ranges in order, merged into bits; and the roots in them, 16
nodes at a time by a gather of their tokens' bits. An `__extension__`, which
the reader also makes a `SELECT` and which has no keyword, costs a bit. It
was a walk to the keyword and a pass over every node per selection, which
made a unit quadratic in its selections: every glibc `assert` in a GNU mode
is an `__extension__`, so `sqlite3.c -DSQLITE_DEBUG` spent 12.1 s there. The
walk (`check_common_unselected_walk`) remains for a selection whose selected
node is among the roots, which none of the callers' roots is, and as the
tests' oracle; `test-checker-driver` compares both paths with it on fixtures
and random trees and graphs whose columns end at `PROT_NONE` pages.

`test-checker-driver`'s `check_test_driver_common_warnings` compares the
filtered diagnostics of `defaults`, `-Wall`, `-Wextra`, `-Werror`,
`-Werror=unused-function`, `-Wno-*`, warning-clean and header fixtures, the
uninitialized evaluation contexts, static selections, self-initialization,
read-modify-writes and VLA bounds, wide and width-mismatched formats, enum
aliases, fixed-underlying and shadowing definitions, zero sign comparisons,
array-member/nested/zero-length bounds, declaration chains, scope-aware enum
definitions, brace initializers and self-initialization, bare/parenthesized/
cast/conditional/comma unused values with side-effect operands and their
function-end order, `for` conditions and writes, positional indexing and used-
argument counting, C23 `%b`, integer classes, unsigned `%n` pointers, exact and
precision formats, reserved-name archetypes, `va_list` invalid conversions,
constant and nested conditional formats, and returned stack addresses line for
line against Clang on both check paths; a filtered output that does not fit the
comparison buffer fails its case. `check_test_driver_common_exhaustion` fails
each common-warning scratch allocation in turn and requires the capacity error,
then the unconstrained output once the index passes the last. The semantics are also covered by
`test-checker`'s existing warning and constraint fixtures, and by
`test-diagnostics` for their ordering.

## The constraint catalogue

`test-checker` checks the constraint catalogue (`c_check_constraints_test.c`, #133's first "Done when" item). For
every Constraints paragraph of C17's 6.4 to 6.10 it has a violation of each of the paragraph's rules and a valid
twin. Fixtures of the conversions (6.3), the constant expressions (6.6) and the initializers (6.7.9) join them:
325 fixtures and 650 sources in all, 21 of 6.3, 17 of 6.4, 99 of 6.5, 13 of 6.6, 114 of 6.7, 26 of 6.8, 15 of
6.9 and 20 of 6.10. Three of 6.4's are C23's bit-precise suffixes (#768: `wB`, `uwbu` and `wbl`, which clang
refuses in every mode, beside valid twins).

Each source is checked as the corpus check checks a file (`check_corpus_file`): preprocessing, the parse, the
checker and the program's control flow, as the driver checks it, by both paths alike. A directive's source (6.10)
runs through the preprocessor's two kernels instead (`check_test_constraint_preprocess`), its first error's line read
from the diagnostics, since the corpus check gives a preprocessing error no place. Every source is also checked by
`clang -fsyntax-only -std=gnu17`. The acceptance and the first error's line must be clang's and the fixture's own
record of them. A fixture records clang's verdict on its violation:
- an error, for 266;
- acceptance with a warning clang gives by default, for 33;
- acceptance without one, for 26: an extension of GNU's or a later standard's, or a rule clang doesn't diagnose.

Every valid twin is accepted. A warning fixture also records the line clang's warning has (`warning`), and the
catalogue requires it from clang and from the checker: `CheckCorpusOracle.check_warning` makes
`check_corpus_file_outcome` compare the checker's first kept sparse warning's presumed line (`CheckCorpusPath.first_warning_line`,
from the reader's and the initializer walk's events and the translation unit's final undefined-internal analysis) with it.

The first survey of the catalogue (main at `321f597`) found 14 of the first 644 sources differing from clang, each
fixed with its fixture there:
- **Rejected, where clang takes them:**
  - declarations and members of no declarator;
  - a union's flexible array member;
  - an incomplete type in a `_Generic` association;
  - a static assertion of a floating constant;
  - an out-of-range floating constant converted to an integer in a static initializer;
  - K&R definitions, all five of their fixtures' sources ("Implementation boundary" above, and "Constant
    evaluation").
- **Accepted, where clang refuses them:** the `_Noreturn` keyword on an object, and an `inline main`.

The remaining const-object difference is covered by declaration-time initializer receipts (#704).
`const int k = 3; enum { E = k };` reads the completed initializer as GNU constant evidence, independently
of the standard ICE flag. Typed sparse writes retain scalar values, implicit zero coverage, aggregate
subobjects, symbolic pointers and active union selections. Each lookup checks the use's source boundary;
volatile, incomplete and effectful initializers supply no value proof. Runtime loads retain their effects,
including const atomic loads. An enumerator publishes ordinary ICE evidence after its definition.

Every warning the 33 get is the checker's at clang's line: the fourteen default warnings #705 lists
now have producers, their sparse events and the driver's rendering. They are `-Wnon-literal-null-conversion`
(a non-literal integer constant zero converted to a pointer, from a zero literal's source provenance that
parentheses and selections keep), `-Wliteral-range` (a floating literal past its type's magnitude, at the
literal), the ungrouped ordered pointer/integer comparison, `-Wmissing-declarations` (a declaration of no
declarator, at file scope and for a member), the ungrouped plain `_Complex` assumption,
`-Wgnu-variable-sized-type-not-at-end`, `-Wstatic-local-in-inline` with its `static` note, the
`-Wmany-braces-around-scalar-init` excess brace, `-Winitializer-overrides` with its prior-initialization note,
`-Wundefined-internal` at the declaration with its use's note at translation-unit end, and `-Wc23-extensions`
for an unnamed parameter before C23. Since then (#147), `-Wextern-initializer` warns of a file-scope extern
variable's initializer at its name, after its attributes' warnings, as clang's `AddInitializerToDecl` does (a block
scope's is an error). Command-line options, `#pragma clang diagnostic` groups, `-w`, system
headers and `-Werror` promotion apply through the shared sparse policy (`c_check_warning_policy.c`); the
driver's `check_driver_warning_events_at` orders them by completion and attaches their notes. `test-checker-driver`
compares the driver's complete diagnostics with clang's for every producer and policy, and
`test-checker`'s catalogue compares each warning's line.

The translation unit's end publishes after every other diagnostic, in clang's `ActOnEndOfTranslationUnit` order
(#2238): `-Wundefined-internal`, unless an error has occurred; `-Wtentative-definition-array`, "tentative array
definition assumed to have one element", for each object its tentative definitions leave an array of unknown bound
(`check_input_tentative_arrays`: once, at its last tentative definition, unless a declaration defines it, in the
order of its first tentative one, under the pragmas at that definition), which clang gives in a unit with errors too;
then, unless an error has occurred, the unused file-scope declarations' warnings. A warning `-Werror` promotes counts
as an error there, as clang's `hasErrorOccurred` has it, wherever it stands, the tentative arrays' among them
(`check_driver_unit_end_split`). Differences left: a unit with a parse error gives none of them, and a rejected
unit's object whose declaration the reader leaves out (`int b[] = { undeclared }; int b[];`) gets none.

## Corpus comparison and semantic mutations

```sh
./build/compiler test-checker-corpus --clang clang [--report NEW.tsv] [--files-from LIST --root DIR] [--semantic-mutations N [--minimize N]] [--seed N] [--keep] [preprocessor options] file-or-directory...
./build/compiler test-checker-corpus --clang clang --minimize-mutant FILE... [preprocessor options]
```

`test-checker-corpus` checks each file (or every `.c` file under a
directory) as the driver does and compares it with `clang -fsyntax-only` on
the same options, #125's configuration: acceptance, the first error's presumed
file and line, and the scalar and vector paths, column by column. It exits with
status 1 on any difference.

`--semantic-mutations N` then checks N semantic mutants, #133's last "Done
when" item (`c_check_mutation.c`). Each unit that the comparison found no
difference in and that clang accepts is printed as `-E` text with line markers,
its base, which is compared as a file first, so that any later difference
belongs to a mutation. The scalar parser's tree of the base gives the sites,
and each mutant is one edit of the base text:

| Family | Kinds |
| --- | --- |
| A type changed | a pointer added or removed, signedness or width changed, `const` added or removed, a struct for the type (or `int` for a struct) |
| A declaration removed | a local, a parameter, a file-scope declaration, a struct member |
| Operands swapped | a binary operator's operands, or two arguments of a call |

The families take turns, three mutants in four edit the unit's own file and
the fourth a file it includes, and `--seed N` (133) orders the units and picks
the sites. A mutant is checked as a file is, with clang reading the same text
as `-x cpp-output`. The first `--minimize N` (4) differences are minimized
(top-level declarations, statements, then tokens blanked while both results
hold, then the line markers dropped) and saved as `checker_mutation_N.c` in the
current directory; `--minimize-mutant FILE...` minimizes saved mutants again.
Each difference that was fixed is a fixture in `c_check_mutation_test.c`, which
`test-checker` checks against clang, which must still give the result recorded
with it, and through both paths. A difference the checker means to keep would
go in `check_mutation_intended`, with its reason; there are none. The command
prints, for each kind, the sites, the mutants, clang's rejections and the
differences.

### The campaigns

On a cloud VM, with clang 23.1.5, its glibc 2.39 headers and
`--no-default-config --target=x86_64-linux-gnu -march=znver5`, seeds 201 to
204. Their first runs found the last two of the differences below (main's
signature and initialized objects of incomplete type); these are their runs
after the fixes:

| Corpus | Options | Files compared | Mutants (clang rejects) | Differences |
| --- | --- | --- | --- | --- |
| SQLite 3.53.2's `sqlite3.c` | `-std=gnu17` | 322 | 320 (174) | none |
| musl 1.2.5 (Zig 0.14.1's tree): its 1,587 sources under `src` and `compat` | `-std=c99 -ffreestanding -nostdinc -D_XOPEN_SOURCE=700 -Os` with the oracle's include path | 4,725 | 1,600 (774) | none |
| Lua 5.4.8's 34 files in `src` | `-std=gnu99 -O2 -DLUA_COMPAT_5_3 -DLUA_USE_LINUX` | 867 | 800 (469) | `lvm.c`'s base |
| The libc corpus (Zig 0.14.1's `lib/libc`): the 3,832 of its 8,983 C and header files that clang accepts here | `-std=gnu17` | 6,552 | 1,600 (302) | `fmaxl.c`'s and `fminl.c`'s bases |

Files compared counts each corpus's files, the mutated units' bases and the
mutants. The scalar and vector paths gave identical columns on every one. The
differences were in bases, and mutants aren't made of a unit whose base
differs. Both are gone. Lua's `lvm.c` uses GNU's labels as values
(`&&L_OP_MOVE`, `goto *`), which the checker takes now (#133). The libc
corpus's `mingw/math/fmaxl.c` and `fminl.c` use `__builtin_isgreaterequal` and
`__builtin_islessequal` on `long double`, which the floating comparison
builtins check now (#148, [the builtin integration
guide](c-builtins-integration.md)).

The corpus comparison without mutants then finds no difference, on a laptop
with clang 23.1.4 and its system's headers:

| Corpus | Options | Files | Both accept | Both reject, at the same first error's line | Neither preprocesses |
| --- | --- | --- | --- | --- | --- |
| SQLite 3.53.4's `sqlite3.c` | `-std=gnu17` | 1 | 1 | | |
| Lua 5.4.8's 34 files in `src` | `-std=gnu99 -O2 -DLUA_COMPAT_5_3 -DLUA_USE_LINUX` | 34 | 34 | | |
| The libc corpus's C files | `-std=gnu17` | 2,315 | 733 | 612 | 970 |
| The libc corpus's headers | `-std=gnu17` | 6,668 | 3,110 | 675 | 2,883 |

A unit that neither side preprocesses lacks a header, stops at an `#error` or
has an `#if` line that doesn't evaluate. The comparison gives no line for a
preprocessing failure, so it doesn't compare their first errors. Clang parses
as it preprocesses, so it reports some of them after an earlier error. On
seven headers' `#if` lines, clang 23 says that a function-like macro isn't
defined (`IS_IN`, `__GLIBC_USE`), and the preprocessor gives the older "token
is not a valid binary operator in a preprocessor subexpression", on the same
line.

What the mutants and the bases found, each fixed with its fixture:

- pointer conversions as clang's Sema has them in every mode (qualifiers
  discarded, nested qualifiers, signedness, `void *` and function pointers),
  comparisons of distinct pointers and of a pointer with an integer,
  conditionals of either (to `void *`), and a call's argument errors at the
  argument and too many arguments at the first extra one;
- GNU's extensions in every C mode (above);
- clang's default-error warnings suppressed in system headers;
- `restrict` on a non-pointer at the qualifier, attribute items kept out of a
  failed declaration's graph, `const void` results, main's signature, and
  initialized objects of incomplete type;
- in the libc corpus's bases: `f16` literals and `align_value`, which clang's
  `<immintrin.h>` uses.

The 9700X job's test step checks 8 mutants of the frozen SQLite source and 96
of the oracle's four musl sources with seed 133, on the frozen headers.

## The source reader's passes over the tokens

`check_input` reads the parser's tree in several passes over every token: the
counts that size its tables, the walk in source order
(`check_input_resolve_range`), the expression graph's walk in postorder
(`check_input_expressions`), and the passes after them. #529 cut the work
they do per token where little of it was per declaration or expression:

- **Attribute arguments (#605).** `check_input_attribute_validate` counts the
  arguments in one forward delimiter walk, then fills a compact table of the
  separating commas as it checks their shapes in a second forward walk.
  Both skip complete parenthesis, bracket and brace groups through the
  parser's matching-parent links. A validated list of A arguments retains
  exactly A - 1 `uint32_t` token indices when A > 1, owned by the output
  arena and shared immutably by attribute copies and merges. Empty and
  single-argument attributes allocate no boundary table. The existing outer
  bounds plus those indices give `check_input_attribute_argument` each
  half-open argument range in constant time, without reading source tokens.
  Shape validation, the declaration reader, and the constant reader's
  preparation and evaluation reuse those ranges; numeric operands still
  evaluate once, in source order. Malformed delimiters fail before a table
  is published, and allocation failure reports the attribute's original
  range as a resource error. No token-sized sparse column is added.
- **Value chains.** The graph's walk gives each token the chain of its
  values, its children's chains joined, and counted the chain by walking each
  child's again. An initializer's separators are each a child of the next, so
  a list of n elements took about n²/2 steps: on the frozen `sqlite3.c`, 4.05%
  of the whole `-c`'s cycles. Each token now keeps its chain's length beside
  its first and last values (`CheckInputReader.value_counts`), and a parent
  adds its children's. The scalar path, `--scalar`, walks the chains: the
  tests that compare the two paths compare the counts with the walks.
- **One pass before reading.** `check_input_count_tokens` makes every count
  `check_input_run` sizes its tables by (`[`, `asm`, `sizeof`, member
  operators, operands and calls, and the names of the builtin constants, the
  varargs, `transparent_union`, `overloadable`, `_Generic` and
  `__builtin_choose_expr`) in one pass, 64 tokens' kinds and roles and 16
  tokens' names at a time. It compares 16 names with each sought name only
  where a filter of those names in two registers (`CheckInputFilter`) passes
  one of them, which few tokens' names do (#529). The same pass writes each token's syntax bits
  (`CheckInputReader.syntax`, `BUSTER_CHECK_SYNTAX_*`): one of the names whose
  builtins the walk prepares, an attribute's start, and a string or
  `__func__`. The walk in source order tests a bit where it compared the
  token's name and kind with each name and kind: on `sqlite3.c`, 50.8 million
  instructions fewer, about 70 a token. The scalar path computes each bit with
  the walk's own tests (`check_input_syntax_of`).
- **Comma contexts.** `check_input_expression_commas` classifies 64 contiguous
  roles at a time, then visits only separators and statement terminators.
  Non-separators' contexts depend directly on their roles; separators inherit
  a parent's context. Reverse source order completes later parents first. An
  earlier parent with a fixed context is computed directly; an earlier
  separator parent asks the unchanged reverse-postorder scalar pass to replay
  the whole view. This keeps dependency order for nonidentity traversals
  without another retained column, allocation or ancestor walk. Loads and
  stores are masked at the logical token count. The count is the parser
  view's complete token count: a prefix has its own complete postorder, and
  a repair includes its synthetic closers even beyond `original_end`, which
  limits later publication. The scalar host and `--scalar` use the original
  pass. Guard-page tests cover every tail through 257 tokens, shuffled
  dependency orders, deep chains and unknown role bytes; real prefix and
  repair views compare every output byte with the scalar oracle. This is a
  bounded residual of #529; general literal preparation and graph
  construction remain separate work. It does not repeat #646's sparse walks,
  #640's traversal derivation, #628's retained parser buffers, #658's compact
  integer tables or #641's post-check work.
  The [frozen qualification](https://github.com/buster14a/buster-performance/blob/e227084cbe0f9b064b9a506ca3e34c9826f49965/docs/measurements/check-input-comma-529-2026-10-28/README.md)
  compares `e7f89091` with `b8ee2900`: three six-placement SQLite cohorts improve
  whole-process wall time 1.16–1.31%, with pooled RSS +22–56KiB. Tiny startup
  cost remains unresolved and individual copy tails are retained. These are
  frozen measurements, separate from later composition correctness; they
  establish no current-main or general-corpus speedup.
- **Scans for few tokens.** The passes that act on few tokens test 64 of them
  at once (16 or 8, by the column's width) and visit only those
  (`check_input_scan_*`): the constant contexts (static objects, static
  assertions, case labels), the attribute items, the complete uses (tentative
  definitions), the asm labels (tokens with attributes, then with
  declarations) and the intrinsics' operands. Each scalar path visits every
  token through the same body. The selections are skipped in a unit without
  a `_Generic` or `__builtin_choose_expr` token, and the VLA finish's scans in
  one without a variably modified declaration's lifetime.
- **Builtins' names.** `cbuiltin_resolve`, which the reader asks of each
  distinct identifier, refuses a name that doesn't start with `__` before it
  scans its catalog, whose names all do (`test-preprocessor` checks it).
- **Const-object receipts (#754).** Every string literal and every
  const-eligible completed static initializer retains a sparse receipt
  (`check_const_append`, `c_check_const_values.c`): a duplicate search over
  the buffer's hash trie, a copy of a `BYTES` part's bytes, and 32 trie nodes
  per appended object. A string literal's token is seen once through the
  reader's string cache, and the deferred merge replays each object once into
  an empty buffer, so both pass `fresh` and skip the duplicate search (#872);
  completed initializers keep the search, since two declarations can share one
  object identity. What remains is the feature's own work: the tries, the
  byte copies, the initializers' expression re-evaluation
  (`check_input_evaluate_cached`), the liveness walk
  (`check_initializer_live_writes`) and the constant-fold warnings. On #872's
  M7-style units, the locked-9700X campaign
  ([the record](https://github.com/buster14a/buster-performance/tree/275811e54bff6b12698311e9480325a0b7399e5f/docs/measurements/checker-872-qualification-2026-11-01))
  reads `bc11fddb`..`d11a91d2` as #754's own work and nothing else: #754 is
  +1.6% by the median of the ten units' one-lane readings (+2.1% in source
  order), `sqlite3.c` +4.9% (source order +3.0%), `00204.c` +15.0%,
  `00200.c` +7.3% and `00213.c` +12.9%, while each of the other ten
  first-parent merges' median change is within ±1.2% one lane and ±1.6% in
  source order, and their cumulative `m754..d11a91d2` change is +0.4% by the
  token-weighted total. In the
  feature's own units that is 2.1 ns per input token on `sqlite3.c` in source
  order and 4.0 in the deferred reader's one lane, 9.7 on `00204.c`, 2.9 on
  `00200.c` and 23.5 on `00213.c`; `perf stat` counts +4.06% instructions and
  +3.74% cycles on `sqlite3.c` and +3.82% and +11.19% on `00204.c`. The
  `fresh` skip itself removes 0.5% by the ten units' median one-lane change,
  0.4% of `sqlite3.c`'s one-lane reading and 0.06% of its instructions
  (0.20% of `00204.c`'s); on the rule's multi-lane reading the fixed head's
  token-weighted total is `bc11fddb`'s (54.13 against 54.11 ms). The
  campaign's main, `97824e90`, is 148 first-parent merges past `d11a91d2`;
  its further +2.8% token-weighted one-lane reading over `d11a91d2`'s
  (+3.9% of `sqlite3.c`'s instructions) accumulates over that range's later
  checker features (#927, #966, #968 and #1002 among them), outside this
  change.
- **Store-to-load round trips in the receipts and the liveness walks (#873).**
  Samples of `ls_bad_status2.stli_other` on the 9700X and the 7940HS placed
  loads that can't take an older store's data in both. `check_const_append`
  wrote each trie node's new child into a stack copy of the node with a 4-byte
  store and reread the copy with an 8-byte load, at each of the 32 depths, and
  copied its object and its buffer out with wide loads right after narrower
  stores into them. `check_initializer_live_writes`'s radix sort zeroed its
  1 KiB of counts with vector stores and read them back with 4-byte loads, in
  each of its eight passes, and so did `check_initializer_live`'s sort of the
  address candidates. Now each new node is its old node's two children as one
  64-bit word, so that the trie loop's depths reread no node from the stack
  (its last depth reads two spilled values, stored long before). The object is
  copied before its places are written into the array. The capacities that the
  storage calls grow are locals, so that nothing takes the buffer's address and
  its copy into `*buffer` is a store per field. Both sorts run a pass only over
  a digit in which some offset differs from the first one's, and none when the
  offsets are in order already, as a single write's two ends and a table's
  pointers in source order are. All of it is exact: the frozen `sqlite3.c`'s
  `-c` objects (both paths, `-g`, `--lower-all`) and diagnostics, 1,337 musl
  sources' objects and c-testsuite's 220 programs' are main's, byte for byte.
  On that `sqlite3.c`'s `-c`, the 4,231 liveness walks (4,023 of them a single
  write) run 158 of the 33,848 passes that main ran, and the 104 sorts of
  address candidates none of their 832. Callgrind on a baseline
  (`BUSTER_HOST_ARCH=x86-64`) build counts 39.7M fewer instructions of 5,964M
  (−0.66%): the walk's 43.9M became 5.3M, the address sorts' 3.7M became 2.4M,
  and the appends' 4.01M became 4.29M, a few more instructions per depth for no
  stack round trip. On the 7940HS, `perf stat -r 5` of the native build before the address
  sorts' change counted 2.25% fewer instructions and 9.62% fewer `stli_other`
  ([buster14a/buster#1037's review](https://github.com/buster14a/buster/pull/1037#pullrequestreview-5727977655)).
  On the 9700X, against main at its fork point, with the same objects
  ([the record](https://github.com/buster14a/buster-performance/tree/d2f5d31761d6f67cdcfe862306e29b0451bb2ae6/docs/measurements/checker-store-forwarding-873-2026-11-01)):
  `-c` of that `sqlite3.c` took 2.15% fewer user cycles, 2.34% fewer
  instructions and 18.1% fewer `stli_other` (`-fsyntax-only` 2.35%, 2.90% and
  18.6%), the medians of 10 interleaved processes each; over six placements of
  the checker's unit and two copies, in two rounds, −1.82% and −2.11% of
  `-c`'s user cycles and −2.29% and −2.56% of `-fsyntax-only`'s, where main
  against itself read within ±0.27%. `--time-stages` puts it in
  `check-declarations`, 3.67 ms down to 1.90; `check_initializer_live_writes`
  and `check_const_append`, which took 15.1% of main's `stli_other` samples,
  are no longer among those over 0.41%.

Both ordinary and declaration-time `_Generic` readers use the same association
index (`c_check_generic.c`). Pointers, arrays, atomic wrappers and function
returns ending at a stable leaf use its unqualified TypeId as a necessary
compatibility key (#1499). Both vector kinds instead hash their stored element
TypeId and extent, the fields compatibility compares across those kinds.
Integers and enums keep one ambiguous key: their compatibility is not
transitive, and completing an enum after an earlier association must not stale
its stored key. Function parameters are never traversed for a key because
transparent unions can make different parameter tags compatible. Every
candidate still receives the exact source-position-aware compatibility check;
hash collisions only add candidates. Qualifiers and alignment never replace
those checks. The control's key also filters provably incompatible matches.

The producer appends associations in source order into scratch-owned key and
type columns. Each hash probe compares sixteen owned keys at once; the scalar
twin computes the same candidate and empty masks. Equal-key candidates stay in
insertion order through collisions and wraparound, so the first failing
association and expression/warning handling keep their existing order. Up to
eight associations use local storage; larger lists reserve a bounded,
at-most-half-full table from the token span. Allocation and sizing failures
propagate as resource failures, and each reader restores its scratch mark.
The index records exact duplicate and control compatibility-query counts.

For A distinct nominal tags, including pointers to forward-declared tags,
duplicate validation makes zero compatibility queries; a control with one of
those keys needs one exact query. Hash probing and input reading remain. This
is a bounded improvement for the nominal families in #602; a large family
sharing an ambiguous key can still require quadratic compatibility work.
The stable-leaf extension also separates distinct `_BitInt` widths, vector
shapes and functions returning distinct records. Its regression fixtures
assert zero duplicate queries for 257-member families and compare both source
readers with Clang. Pairwise controls cover the two vector kinds, enums,
qualifiers, arrays, function prototypes and source-time transparent unions;
an enum completed after insertion must still match its integer type.
No end-to-end timing or optimized-assembly result is yet recorded for #1499's
extension. Integer/enum families and functions differing only in parameters
remain in the conservative partition; this does not close the whole issue.
`test-checker` compares the index with independent pairwise semantic queries,
checks source-time transparent unions, guarded groups, collisions, exhaustion
and the query-count bound, and checks both source readers against Clang.

On the locked 9700X (CPU 2, one lane, six repeated fresh-process samples),
10,000 pointer associations went from 738.960 to 12.474 ms in an ordinary
expression and from 785.990 to 58.097 ms in a declaration-time constant.
Duplicate compatibility queries fell from 49,995,000 to zero in both readers,
and control queries from 10,000 to one. The complete constant-expression
compile still grows faster than linearly; this removes its quadratic
association-compatibility work, without claiming every other operation is
linear. SQLite's control instructions were effectively unchanged. The
[source pins, full scaling/RSS/instruction table, exact-query logs and validation](https://github.com/buster14a/buster-performance/blob/b6424ad152a7cdc49ee9d3ca89e66ac850c90dbb/docs/measurements/generic-association-index-602-2026-10-27/README.md)
retain both record and pointer families, scalar/vector/native equivalence and
the experiment's limits.

`test-checker` checks every scan, count and syntax bit, by both paths,
against its own reading of columns of every length to 200 that end at a
`PROT_NONE` page (`c_check_input_scan_test.c`).

On the 9700X (CPU 2, `-c` of the frozen `sqlite3.c` with #520's options, eight
rounds of `perf stat -r 3` with main's and this change's executables in
alternating order), the whole compile went from main's 903.3 million user
cycles and 2,227.2 million user instructions to 820.9 million and 2,014.0
million (−9.1% and −9.6%), and its task clock from 190.9 ms to 176.1 ms; every
round's cycles were apart. #520's stage clock (`measure-compile`, 11 runs of
each in rotating order, both executables with the clock merged in) puts the
reader's stage, `check-input`, at 64.8 ms (64.3 to 65.3) against main's 80.3 ms
(79.7 to 80.8), and the whole compile at 175.5 ms against 190.2 ms. The reader
alone (`benchmark-checker-bindings --only direct`, 11 passes on warm pages)
went from 67.3 ms to 54.4 ms (−19%). The object is main's, byte for byte, by
both paths. The scratch the reader writes grew by 3.5 MB (`value_counts` and
`syntax`), and the compile's peak RSS by 4.3 MB. [The measurement
record](https://github.com/buster14a/buster-performance/tree/6929480812d31868d77a515f88ddca3bad408429/docs/measurements/check-input-529-2026-10-27)
has the scripts, the raw outputs and the trees' manifests.

### The walks' visits

The two walks still made their tests at every token: the walk in source
order, some fifteen columns' at each, though it acts at about a quarter of
`sqlite3.c`'s tokens, and the expressions' walk its chain of tests, though at
about a quarter it only writes the token's first value. Each now visits the
tokens it may act at, and tests there only what may make it act. On SQLite
3.53.4's `sqlite3.c` (716,380 tokens), the walk in source order visits
263,473 of them and the expressions' walk 525,518
(`benchmark-checker-bindings` prints both):

- **The walk in source order** visits the tokens marked in a byte column,
  `CheckInputReader.visits`, the next marked one found 64 bytes at a time
  (`check_input_resolve_next`). Two kinds of writes mark them, each with a
  reason (`BUSTER_CHECK_RESOLVE_*`):
  - `check_input_count_tokens`' pass marks the tokens whose syntax the walk
    tests: their roles and kinds, and their successors', looked up 64 at a
    time in tables of the walk's tests (`check_input_role_tests`,
    `check_input_kind_tests`, with `vpermi2b`), and the syntax bits;
  - each write ahead of the walk to a column it tests marks its token
    (`check_input_visit`): an activation, a scope's end, an initializer's
    finish, a declarator's or an initializer's start, or a type query's, a
    designator's or a builtin's operand.

  A visit tests only the columns its marks give reasons for, each read when
  the walk tests it, after its earlier steps at the token may have marked it.
- **The expressions' walk** takes each token's classes from a pass before it
  (`check_input_expression_classes`): whether the walk acts at the token (it
  has children, its role or kind is one the walk's tests take, by the same
  tables, or one of seven sparse columns has an entry for it), and whether a
  sparse column does. The vector path reads that last from a bit a token,
  64 tokens' in a word (`CheckInputReader.sparse_marks`, #628), which every
  write of the seven columns keeps (`check_input_sparse_mark`), rather than
  the columns themselves; so the walk reads four of them (the cached
  constants, the `va_arg` calls, the designators and the variable arrays'
  bounds) only at tokens a mark names, and they live in the base pages of
  the reader's arena of rare columns. It skips the tokens it doesn't act
  at, whose first values, `NONE`, the pass writes, and tests the sparse
  columns only of the tokens that have an entry.
- **The rare columns' pages** (#628): the token columns of the reader's
  arena of rare columns (the specifier jobs, the declarations' bases,
  storage, flags and ABIs, the builtins and the rest of
  `BUSTER_CHECK_SPARSE_COLUMNS`) are written at few tokens and read at most:
  the classes above load the specifier finishes and the builtins at every
  token, and each declaration reads its first token's. A reading in source
  order keeps a bit per 64 bytes of them (`CheckInputReader.rare_marks`,
  `BUSTER_CHECK_RARE_GRANULE_SHIFT`), which every write's claim sets
  (`check_input_rare_claim`), and its vector path's hot reads give 0 for
  bytes with no bit without touching their page: a scalar read reads the
  column's first entry instead and clears it, and a vector load reads a static
  line of zeros, by masks rather than branches, which the marks, alternating,
  would mispredict (`check_input_rare_mask`, `BUSTER_CHECK_RARE`,
  `check_input_rare_line`). A vector load masked to no lanes would give the
  zeros too, but at a page nothing had touched it took the 9700X 174 TSC
  ticks, against 35 at a written page, every time (`docs/compile-time.md`'s
  "The rare columns read only where written").
  So the columns are base pages that only their writes
  fault in: of `sqlite3.c`'s 6,673 pages the walks write 688, which in huge
  pages, spread over the columns, had taken 20 MB at the reader's end. The
  scalar path, the twin, reads every entry, and counts the 64 bytes with a
  byte other than 0 and no bit in `CheckInput.visit_misses`
  (`check_input_rare_verify`), which a small fixture's columns, sharing a
  page, would hide in a page's bit.
- **Deferred bodies** (#113) need no marks of their own: each starts at an
  `OPEN_BODY`, which the walk tests, and the walk jumps over the body from
  there. Lanes share the column, each writing only its own body's tokens.
- **The twins.** The scalar path, `--scalar`, still visits every token and
  makes every test. At each token it also checks what the vector path would
  have done: each test that could act there, without the mark or the class
  that would make the vector path make it, and each sparse mark that the
  columns contradict, counts in `CheckInput.visit_misses`,
  which `test-checker`'s fixtures and `test-checker-corpus` require to be 0. A
  mutant without the activations' mark failed 3,243 of `test-checker`'s checks.
  `test-checker` also requires the vector path's walks to visit fewer tokens
  than the scalar path's (`resolve_visits`, `expression_visits`), which a mark
  on every token, passing every comparison, would not. The scans' test checks
  the marks and the classes by both paths against its own reading, with every
  column ending at a `PROT_NONE` page.

On the locked 9700X, CPU 2, main `92c025e7` against `60732582`, the completed
final comparison gives `check-input` 63.682 → 58.927 ms, the direct reader
53.912 → 49.375 ms, and whole `-c` perf task clock 146.340 → 141.725 ms.
User cycles fall 3.77% and instructions 5.21%; all observations, unchanged
scalar/one/eight-lane objects and the 16 target checker-suite results are
in the [record](https://github.com/buster14a/buster-performance/tree/703e034725e79fed5574e42b0be96b3af4cae9bf/docs/measurements/check-input-529-walks-2026-10-27).
This is the sparse-walk snapshot, before the later combined storage/traversal
integration, not a new measurement of that combination.

### The coded walk (#1038)

The expressions' walk still took each token it visited through its action's
chain of about 25 tests of the token's role, kind and columns before it acted,
every step loading at the token the postorder had reached. Its vector path is
now the coded walk (`c_check_expression_input.c`); `--scalar` keeps the walk,
the oracle.

- **Classes.** The classes' pass gives each token the walk acts at a class too
  (`CheckInputExpressionCode`, in the visits byte's bits 2 to 6): which branch
  of the walk's tests its role, its kind and the columns the walk doesn't write
  lead to. Each block of 64 tokens looks its roles up in a table of their
  classes (`check_input_role_codes`, with `vpermi2b`). An operand's class is its
  kind's (identifier, literal, string or the walk's own), and a `return`
  keyword's is RETURN (`goto`, `break` and `continue`, of its role, are the
  walk's). A role no test takes is a
  DROP, but for the kinds the walk refuses. An ignored token is a LIST of a list
  role and a DROP otherwise. A sparse entry or a type query finishing at the
  token (`specifier_finishes`, 8 pointers at a time) makes it the walk's.
  The columns the reader writes before the walk refine the class
  (`check_input_expression_fold`, #1144), read 64 tokens at a time:
  - a separator that `expression_commas` marks is a comma operator's BINARY,
    and the other separators are lists;
  - an initializer's assignment (`initializers`) is an INITIALIZE, a class of
    its own (#1038's stages 2 and 4, below; until then the walk's);
  - a builtin's token (`builtins`) of a class whose action left builtins
    to the walk (`check_input_builtin_codes`) is the walk's;
  - so is an intrinsic's call (`intrinsic_ids`).

  The actions test none of these as the walk reaches each token. An intrinsic's
  id was the walk's hottest load: a column no other pass reads, so a miss at
  nearly every call.
  `check_input_expression_code` is the scalar twin, by the walk's tests in their
  order. The scalar classes' pass computes the codes only for the coded walk:
  `--scalar`'s walk reads a token's `ACTS` and `SPARSE` bits alone, so it gets
  no code, and doesn't pay for the code's tests. Baseline builds run the coded
  walk through the twins, codes and all.
- **Compaction.** The postorder is read 1,024 positions at a time
  (`check_input_expression_compact`), 16 tokens' bytes gathered from `visits`
  and compressed with their tokens into two arrays on the stack. The column has
  `BUSTER_CHECK_VISITS_PADDING` (2) bytes past its `count + 1`, as far as a
  four-byte gather at the last token reads. A DROP's action is nothing but its children's chains joined,
  and no parent reads them: its first value is `NONE`, as the reader's setup
  wrote it, so its tokens are left out. The scalar twin writes every token and
  counts the ones it keeps.
- **Actions.** Each class has its own action, which writes what the walk's does
  (the node's columns, its leaf, the token's node and values):
  - an identifier's leaf, as twelve words (no x87 store of its zero floating
    parts), made before the chunk's actions (below);
  - a literal's leaf, and a string's;
  - prefix, binary, cast, member, subscript, postfix and conditional operators;
  - lists and groups;
  - terminators' discards, a statement expression's last statement's result
    among them, and conditions;
  - an initializer's assignment: the INITIALIZE of its one value, recorded as
    its declaration's initializer (`initializer_nodes`), or a braced
    initializer's values passed on as a list;
  - a return's conversion to the function's type, or its missing value's
    report;
  - plain calls, with their arguments' slice and the library builtins' marks.

  In a unit with overload tables, the actions carry the heads as the walk's
  action does. An identifier's leaf holds its use's head. `ADDRESS` and
  `SELECT` take their operand's head, and every other operator clears its
  token's. A call whose callee names a set is recorded in `overload_calls`, in
  node order.

  An identifier's leaf is a function of columns the reader writes before the
  walk: at its token (declared type, declaration, overload head), at its
  declaration (kind, storage, flags, object, enumerator's value), and its
  type's kind. So each chunk's identifiers get their leaves' varying words
  before its actions run (`check_input_expression_identifiers`):
  - the vector path compresses the chunk's identifiers;
  - it then gathers 16 identifiers' columns at once, a byte column as the
    aligned dword that holds the byte, which never crosses a page the column
    isn't on;
  - the identifier's action only adds the node and writes the words.

  `check_input_identifier_entry` is the scalar twin. Unlike the batch measured
  below, this needs no flush: the node ids still come from the walk, and only
  the leaves' contents are made ahead.

  An action doesn't write its node to the graph: it numbers it into a batch,
  whose columns are written at once (the graph by passes, below), with
  `check_graph_add`'s checks. An action tests what its class can't say: the
  values the children have, and the columns the walk writes as it goes. Where
  they don't hold, the token goes to the walk's
  own visit (`check_input_expression_visit`, the action factored out as
  `check_input_expression_act`). An action that leaves a token has written only
  its first value and its children's chains, which the visit writes again as
  they were. A unit with expressions past its limit or deferred is the walk's
  throughout. So are a K&R unit's identifiers and calls and a constexpr
  object's uses.
- **The twins.** The scan test checks the classes by both paths against its own
  reading of the walk's tests. It checks the compaction by both paths, with and
  without the DROPs, against a random postorder, every column ending at a
  `PROT_NONE` page. It checks the identifiers' leaves by both paths on random
  chunks over random columns, each ending at a `PROT_NONE` page, the byte
  columns at every alignment, and the batch's flush the same way (below). The
  coded walk counts, by class, the tokens it acted at by
  the class's action and those it left to the walk's visit
  (`CheckInput.expression_acted` and `expression_left`). `test-checker` requires
  it to act by class at tokens of every fixture whose features it takes, and
  the scalar walk at none. Over its fixtures, each class's action must act and,
  where it can decline, leave a token; a full run prints both counts per class.
  `benchmark-checker-bindings` prints a file's tokens acted at by class
  (`expression_classed`), declined by their classes' actions
  (`expression_declined`) and of the walk's own class (`expression_scalar`).
  Its plain run (not `--only`, `--after-scalar` or `--pipeline`) then times
  the walk by class in runs of the default handoff of their own, after its
  samples, with `PPOptions.expression_timing` (three TSC reads a token, which
  no compile sets): an `expression-classes` line with the walk's actions'
  nanoseconds and the timing's own ticks a token, which it takes off every
  class's, and an `expression-class` line per class with its tokens, those its
  action left (whose visits its time includes), its nanoseconds (the median
  over the passes) and its share. A class's time includes the batch's flushes
  its tokens make (the graph by passes, below): the scalar class's, those
  before its visits. Every comparison of the two paths compares the
  graphs, leaves, argument slices, overload calls and `token_nodes`:
  - `test-checker`, for every source fixture
    (`check_test_source_graphs_equal`), with the overload heads;
  - `test-checker-corpus`;
  - c-testsuite's objects.
- **What the walk is made of.** The counts per class that #1038 asked for, as
  shares of each corpus's tokens: every token is in one row. A class's tokens
  that its action left to the walk's visit are in parentheses. They were
  counted from the classes' bytes and `CheckInput.expression_acted` and
  `expression_left` (#1038's review). This first table is the coded walk as
  #1061 merged it, before the folds, the string and return classes and the
  overload tables' new trigger (the second table, below):

  | class | `sqlite3.c` | `main.c` | musl's 8 directories |
  | --- | --- | --- | --- |
  | tokens | 710,061 | 6,710,046 | 1,526,259 (441 units) |
  | not acted at | 27.89% | 21.30% | 28.23% |
  | identifier | 13.72% | 14.01% | 5.96% |
  | list | 10.68% | 8.16% | 25.92% |
  | terminator | 7.82% | 4.36% | 6.31% |
  | binary | 6.74% (0.59%) | 10.41% | 5.69% (0.15%) |
  | drop | 6.72% | 4.07% | 9.51% |
  | literal | 6.38% | 10.46% | 4.24% |
  | member | 4.59% | 2.81% | 0.16% |
  | separator | 3.62% | 10.61% | 0.57% |
  | group | 3.12% | 3.10% | 4.47% |
  | call | 1.93% (0.006%) | 1.96% | 1.47% (0.005%) |
  | condition | 1.63% | 0.48% | 0.13% |
  | cast | 1.28% | 0.96% | 0.32% |
  | the walk's own (scalar) | 1.26% | 3.79% | 3.14% |
  | prefix | 1.22% (0.13%) | 1.87% | 2.65% (1.80%) |
  | subscript | 1.00% | 1.19% | 0.06% |
  | postfix | 0.30% | 0.03% | 0.02% |
  | ternary | 0.09% | 0.43% | 1.14% |

  - **The walk's own action.** It takes the scalar class and the declined
    tokens: 1.98% of `sqlite3.c`'s tokens and 5.11% of musl's. The coded walk
    visits 512,058 of `sqlite3.c`'s tokens and acts by class at 450,240 of them.
    It skips its 47,745 DROPs, which need no action, and leaves 14,073 to the
    walk.
  - **The scalar class.** On `sqlite3.c` it holds `return`s (0.61%), string
    operands (0.27%), `case` labels' colons (0.21%), parenthesized `sizeof`s'
    and `_Alignof`s' closings (0.13%) and the sparse columns' tokens (0.03%).
    musl's is mostly `sizeof`'s closings (1.80%), its `return`s (0.46%) and the
    sparse columns (0.86%, almost all `asm`). Its declined prefixes (1.80%) are
    those `sizeof`s' keywords. `main.c`'s is string operands (2.48%), `sizeof`
    (0.78%) and `return`s (0.19%), its sparse columns 0.08%.
  - **Declines.** The actions that decline valid code do so by design: a binary
    operator in an initializer's designation or a builtin's, a call of a
    builtin or an intrinsic, a prefix `sizeof`, `alignof` or a label's address.
  - **`main.c`.** As #1061 merged it, the coded walk took none of its tokens.
    `main.c` spells `overloadable` (a member's name in `c_check_input.c`), and
    any token so spelled gave a unit overload tables (`CheckInput.
    overload_heads`), which the class actions didn't carry yet. So this
    column was the classes the coded walk would take.
  - **The trigger, narrowed (#1144).** A name of the attribute's now counts
    only inside an attribute list (`check_input_count_tokens`). That is: more
    of the lists' openings up to it than closings before it, which, a subtree
    being a run of tokens, puts it in a list's closing's subtree, where every
    GNU and standard attribute's name is. An attribute's argument counts too,
    a superset that costs at most the tables. Each path keeps the lists'
    depth as it goes: the vector path a 64-token block at a time, from the
    block's openings' and closings' counts. So a name's test costs the same
    however deep the tree. A first version walked each name's ancestors,
    which made the count quadratic in names times depth: 100,000 names
    100,000 levels deep took 20 s (#1144's review).
    The class actions carry the heads (above), so a unit that does declare
    overloadable functions is coded as well.
    - Tables nobody used were dear. In a 4-CPU VM, appending
      `int overloadable;` to `sqlite3.c` cost about 50 ms of a 296 ms `-c`,
      only ~10 ms of it in the walk.
    - In such a unit, overload resolution also dropped the library builtins'
      folds: `main.c` called `strlen` of a constant string 71 more times than
      clang's code does. Its object now folds them, and nothing else in it
      changes.
    - A unit with real overloadable functions still misses those folds. That
      is the overload path's bug, not changed here.
  - **The sparse tail.** It has to stay ordered, and is small in all three:
    0.03%, 0.08% and 0.86%.

  The second table: with the folds, the string and return classes, the
  statement expressions' terminators and the narrowed trigger (#1144), for
  `sqlite3.c` and `main.c` (both coded). musl's corpus wasn't at hand. Every
  token is in one row, and declined tokens are in parentheses:

  | class | `sqlite3.c` | `main.c` |
  | --- | --- | --- |
  | tokens | 710,061 | 6,848,922 |
  | not acted at | 27.89% | 21.30% |
  | identifier | 13.72% | 14.10% |
  | list | 10.68% | 8.16% |
  | terminator | 7.82% | 4.38% |
  | drop | 6.72% | 4.09% |
  | literal | 6.38% | 10.39% (0.001%) |
  | binary | 6.17% | 8.31% |
  | member | 4.59% | 2.82% |
  | separator | 3.60% | 10.54% |
  | group | 3.12% | 3.07% |
  | call | 1.93% | 1.84% |
  | condition | 1.63% | 0.49% |
  | cast | 1.28% | 0.96% |
  | prefix | 1.22% (0.13%) | 1.88% (0.77%) |
  | subscript | 1.00% | 1.19% |
  | the walk's own (scalar) | 0.97% | 3.36% |
  | return | 0.61% | 0.19% |
  | postfix | 0.30% | 0.03% |
  | string | 0.27% | 2.46% |
  | ternary | 0.09% | 0.43% |

  The walk's own visits, the scalar class and the declined tokens, fall from
  14,073 of `sqlite3.c`'s tokens (1.98%) to 7,800 (1.10%). For `main.c` they
  fall from all of its 5.39 million visited tokens to 283,522 (4.14%). Its
  remaining scalar class holds the builtins' and intrinsics' tokens the folds
  give the walk, its `sizeof`s and the walk's other roles; its declined
  prefixes are `sizeof`'s and `alignof`'s keywords.
- **Measured.** In a 4-CPU Sapphire Rapids VM without performance counters, `-c`
  of the frozen `sqlite3.c` (`--lanes 1`, CPU 2) was timed 31 times with
  rotating builds, by `--time-stages`' new `time-check expressions` row (the
  TSC across `check_input_expressions`). The walk's minimum went from 41.4 to
  33.1 ms, its median from 54.9 to 39.6 ms, and the `check-input` stage's
  median from 172.9 to 153.9 ms. Sampling at 50 µs (8 compiles each) gave the
  walk about 12,500 samples before and 7,100 after, about 43% less. The VM's page faults land
  inside the walk's interval (about 8 ms of system time in both builds), and
  its run-to-run spread is wide, so these figures are the VM's alone. The
  9700X's counters are the measurement of record.
- **The review's changes, measured.** These are 21 rotated runs each in the
  same VM, against the walk before the coded walk. Sharing the node writer with
  `check_input_add` (`check_input_add_node`, `check_graph_add`'s checks and its
  leaf's clear) moved the coded walk's minimum from 31.5 to 32.1 ms and its
  median from 34.3 to 35.1 ms, within the VM's spread. `--scalar`'s walk had
  paid for the codes' tests: its minimum was 60.7 ms against 56.2 before the
  coded walk. Since its classes no longer take codes, it is 57.5 against 55.5
  (another run of both). The
  action's call at every token is the same code with `noinline` or without it,
  since clang doesn't inline its 28 KB at two places. No benchmark times that
  path; it is the oracle.
- **Measured on the 9700X (#1144).** The 9700X job's `measure-compile` of
  `sqlite3.c` (11 runs, medians), against a run on `main`'s neighbour
  `06bcc898`:
  - the walk's interval (`check-input.expressions`) went from 17.27 ms to
    15.36 ms;
  - `check-input` went from 49.22 ms to 47.47 ms;
  - the whole compile went from 117.80 ms to 116.02 ms.

  That measures the narrowed trigger, the heads, the gathered identifier
  leaves and the folds; `sqlite3.c` has no overload tables or statement
  expressions. In the VM, the gathered leaves alone were within its noise:
  user-mode samples in the walk over five compiles were 3,010 and 2,987
  without them and 2,603 and 3,039 with them. The folds' build had the VM's
  best walk interval: minimum 33.7 against 35.9 ms, median 37.8 against
  40.6 ms, for the build with only the trigger and the heads (21 rotated runs
  each).
- **Measured on the 9700X (#1172).** The same step's `measure-compile` of
  `sqlite3.c` (11 runs, medians), at #1172's `4cade144`: the walk's interval is
  14.26 ms against #1144's 15.36 ms, with the leaves written once, the counters
  by class and the literals' pass (below). Its parts: classes 0.43 ms, prepare
  2.54, actions 8.96 and visits 2.41.
- **Tried, and slower.** Identifier leaves built 16 at a time from gathered
  declaration columns, written before anything could read a leaf. That is
  before every token left to the walk's visit, whose node records the nodes
  before it. Those flushes averaged about 3 leaves on `sqlite3.c`, and the
  walk's minimum rose from 32.1 to 34.2 ms, so the leaves are written by their
  action. #1144 makes the leaves' contents ahead instead, without placing them
  (above). The graph by passes, which would number the nodes before writing
  any, is below.
- **The literals' values (#1038).** `check_input_expression_literals` reads a
  chunk's literals before its actions, as the identifiers' leaves are: a plain
  decimal of one to nine digits without a leading zero (but `0`) is an `int`
  of that value, and so is a preprocessor's int (`__LINE__`'s) up to
  `INT_MAX`. Every other literal its action still parses
  (`check_input_literal`). The vector path reads in stages over the chunk's
  literals, 16 at a time, so that each stage's cache misses wait together:
  names and sources; the sources' kinds, locations and ends; the ranges (one
  of the two the last numbers were in, else `pp_find_range`'s search, which
  all lanes take in step); then the two aligned words that hold each
  spelling, four tokens a vector, whose digits one shuffle right-aligns and
  pairs, quads and lanes multiply out. `check_input_literal_plain` is its
  scalar twin. The scan test checks both paths on random chunks over three
  guarded ranges of spellings, and every plain value against
  `check_literal_number`; `test-checker`'s graph comparisons check the walk's
  literals against the scalar walk's, which parses every literal.
  - **What it costs, on the 9700X.** Most of a literal's cost is the first
    read since the parser of its preprocessor columns and its source: on the
    VM the pass takes 1.7 to 1.9 ms of `sqlite3.c` cold and 0.45 to 0.72 ms
    warm on the same chunks. #1172's A/B on the 9700X compiled the frozen
    corpus's `sqlite3.c` with the pass and with every literal parsed by its
    action instead (a temporary option, gone since), the same object:
    - rotated whole processes (`time-runs`, 11 each): 113.81 ms (113.60 to
      114.37) against 115.21 ms (115.03 to 116.14), 539.5 million user cycles
      against 547.4 million, and 1,281.8 million instructions against
      1,299.8 million;
    - the walk (`measure-compile`, 11 each, neither with a reference): 14.30 ms
      against 15.69 ms, `prepare` 2.53 against 1.62 ms and `actions` 9.05
      against 11.32 ms.

    So the pass takes 0.9 ms to save 2.3 ms of the actions, 1.4 ms (1.2%) of
    the compile. On the VM, whose gathers its microcode slows, the walk was
    within its noise. The lexer reads the source and the preprocessor writes
    those columns in order, as they make the tokens: that is where a literal's
    value would cost little more again.
- **Where the class actions' time goes.** `benchmark-checker-bindings` times
  the walk by class (above). On the 9700X, the frozen corpus's `sqlite3.c`, 5
  passes, at #1172's `4cade144` (with the literals' pass): the class actions
  take 3.84 ms, 8.39 ns a token. The timing's own cost, 27.3 TSC ticks (7.2 ns)
  a token, is taken off every class's and is the table's resolution: a class
  whose residual a token is below it is marked, and is no measurement.

  | class | tokens | ns a token | share | resolved |
  | --- | --- | --- | --- | --- |
  | the walk's own (scalar) | 6,639 | 263 | 45.4% | yes |
  | literal | 44,307 | 10.3 | 11.9% | yes |
  | prefix (913 left) | 8,381 | 45.2 | 9.9% | yes |
  | identifier | 95,222 | 3.57 | 8.8% | no |
  | terminator | 54,804 | 3.96 | 5.6% | no |
  | member | 31,957 | 5.46 | 4.5% | no |
  | call | 13,597 | 12.2 | 4.3% | yes |
  | binary | 42,450 | 2.48 | 2.7% | no |
  | separator | 25,264 | 2.78 | 1.8% | no |
  | condition | 11,273 | 4.39 | 1.3% | no |
  | subscript | 6,767 | 3.83 | 0.7% | no |
  | string | 1,851 | 11.8 | 0.6% | yes |
  | cast | 8,995 | 2.45 | 0.6% | no |
  | list | 78,073 | 0.25 | 0.5% | no |
  | group | 21,807 | 0.87 | 0.5% | no |
  | return | 4,238 | 3.75 | 0.4% | no |
  | postfix | 2,035 | 5.99 | 0.3% | no |
  | ternary | 630 | 8.42 | 0.1% | yes |

  The table is warm, as every pass of the benchmark is; the compile's walk is
  cold, and its `actions` took 9.05 ms of the same head's 14.30 ms walk in the
  compile-time step. What resolves is that the walk's own visits (45.4%), the
  literals (11.9%), the prefixes (9.9%, 913 of whose 8,381 the visit takes) and
  the calls (4.3%) are most of the warm actions. The operators and lists are
  27.6% in all, 1.06 ms: the prefixes, calls and ternaries (14.3%) resolve,
  and the others, from the members (4.5%) down to the lists, are each below
  the resolution, 13.3% together. The identifiers and the terminators (8.8% and
  5.6%), which are no operators, are below it too. The dispatch that #1038
  found the walk's cost in (one long chain of tests a token) is gone with the
  classes.
- **The graph by passes (stages 2 and 4): a scalar probe, and the bound a
  vector pass has to beat.** The numbering is probed by
  `benchmark-checker-bindings`' `expression-numbering` line
  (`PPOptions.expression_numbering`, `check_input_expression_numbering`): after
  the coded walk, it numbers every acting token again in postorder, as such a
  pass would before writing any node. Each value goes to its consumer's list
  (the nearest ancestor that isn't a LIST, a GROUP or a SEPARATOR the walk
  didn't visit), each operator's operands are read from its own list, a
  terminator makes a discard a value, and the walk's visits are barriers that
  take the walk's own nodes and value chains. Each node a class numbered is
  checked against the walk's graph: its token, and but for a VALUE its first
  operand; the barriers' nodes it only counts. Its time is the whole pass's,
  from its columns' allocation and clearing to its last token, the check
  outside it. On the 9700X, the frozen corpus's `sqlite3.c` (the same runs as
  the table above), it numbers all 316,441 nodes and checks 311,454 of them
  with no difference; written plainly it takes 3.21 ms warm, 10.2 ns a node,
  where the operators and lists it would replace take 1.06 ms of the same
  runs. (On the VM, with sqlite 3.53.2's amalgamation: 6.4 ms warm, 20.5 ns a
  node.) What that shows, and what it doesn't:
  - the consumer lists number the graph as the walk does, so a pass by them is
    exact;
  - this probe, a scalar loop of parent walks and list appends, costs three
    times the per-token actions it would replace, before a node is written;
  - it is not the stages' data-parallel design (node counts by class and a
    prefix sum for the ids, the consumers by pointer jumping, operands by
    segmented scans and gathers), which isn't built or measured. To pay on
    `sqlite3.c`, such a numbering and the operators' writes together have to
    take less than the operators' and lists' 1.06 ms warm, about 3.4 ns for
    each of the walk's nodes;
  - the walk's visits are its barriers: each makes as many nodes as it
    decides, and reads its operands' columns, so the ids after it are known
    only once it has run. On `sqlite3.c` they are 6,639 to 7,800, one in 58 to
    69 tokens acted at by class (the VM's option set, then the 9700X's frozen
    corpus), which cuts the runs a vector numbering would take to that many;
  - the nodes' recording is no barrier: nothing reads it before
    `check_graph_record_finish`.

  What was built instead is the batch below. The probe stays, and takes a
  braced initializer's INITIALIZE as a list that passes its values on.
- **The graph by passes, built (stages 2 and 4).** The class actions number
  their nodes before the graph has any of them, and the graph's columns are
  written 16 nodes at a time (`CheckInputBatch`,
  `check_input_expression_flush`):
  - **Numbering.** An action appends its node to the batch
    (`check_input_batch_add`, with `check_graph_add`'s checks): its operator,
    token, operands and form (an operator's, a leaf's, a discard's or an
    initializer's). Its id is the graph's count before the batch plus its place
    in it: the prefix sum of the actions' node counts, taken as they go, since a
    terminator's count is its values' and a declined token's none. The batch
    holds 1,024 nodes, a column of each on the scratch, in the first two cache
    levels. An action reads an earlier node of the batch through the batch (a
    call's callee and its designator, an address's operand's token for the
    overload heads, an argument's token), and writes what a node takes after it
    is made (a member's name, a call's argument slice, a static list's
    element's value context) into it. The value chains (`value_next` and the
    tokens' first values) are written as the actions go, since a parent reads
    them at once, and so are the leaves, whose slots the batch gives.
  - **Writing.** The flush writes the batch's nine columns as they are, and
    gathers at the nodes' tokens what every node takes from its token: the type
    its query asked for (an operator's and an initializer's), a static
    initializer's context (an operator's `STATIC`, an initializer's `VALUE`),
    where its expression ends (`check_input_expression_end`), its warnings'
    suppression and its floating-point environment, a byte column through
    `check_input_gather_bytes`. It then records the nodes
    (`check_graph_record_nodes`, after any a visit made before them) while their
    columns are hot. The scalar path, the twin, writes a node at a time.
  - **Barriers.** The walk's visit reads and writes the graph itself: the batch
    is flushed before each (the actions' time, not the visit's), and the batch's
    next node is the one after the visit's. A batch without room for a token's
    nodes is flushed first, and a terminator of more values than a batch holds
    is the walk's.
  - **Initializers.** The batch alone saved little: on `sqlite3.c` half of the
    walk's visits, each a flush, were initializers' assignments (4,163 of about
    7,800, each about 430 ns with the record of the nodes before it). They are
    now a class of their own, INITIALIZE (above), whose action makes the walk's
    INITIALIZE node or passes a braced initializer's values on. The walk's own
    visits fall from 6,639 of the frozen corpus's `sqlite3.c` tokens to 2,627,
    and 4,012 initializers are acted at by class.
  - **The twins.** The scan test writes random batches of up to 200 nodes,
    after up to 40 a visit made, over random token columns of up to 200 tokens,
    each ending at a `PROT_NONE` page and the byte columns at every alignment,
    by both paths: the two graphs must agree column for column, the gathered
    columns must be their tokens', and the records alike. A deliberate one-node
    difference in the scalar path made it report 1,842 of 38,098 nodes.
    `test-checker`'s graph comparisons, `test-checker-corpus` and c-testsuite's
    objects compare the batch's graphs with `--scalar`'s walk.

  Measured on the 9700X, the frozen corpus's `sqlite3.c` (`-c`, CPU 2, under
  the lock), against `main` at `0eca996a`; the objects are byte-identical by
  both builds and `--scalar`, and so are `main.c`'s:

  | | `main` | the batch | with INITIALIZE |
  | --- | --- | --- | --- |
  | the walk (`check-input.expressions`, ms) | 14.00 to 14.22 | 14.09 | 13.17 to 13.55 |
  | its actions, ms | 8.90 to 9.01 | 7.08 | 10.20 to 10.39 |
  | its visits, ms | 2.37 to 2.41 | 4.11 | 0.21 to 0.22 |
  | user cycles (millions, rotated) | 846.7 to 850.3 | 844.9 to 845.9 | 842.2 to 843.8 |
  | instructions (millions) | 2,151.6 | 2,150.8 | 2,147.1 |

  The walk's rows are `measure-compile`'s medians of 11 runs, twice per build;
  the batch's own visits still held its flushes then. The cycles are the
  medians of 5 rotated rounds of 11 whole processes each (`ab` with
  `time-runs`), whose rounds don't overlap: with INITIALIZE, 0.67% of the
  compile (about 5.7 million cycles, 1.0 ms of wall time). `perf stat` of 11
  runs agrees: 848.2 against 843.9 million cycles. Over six placements of the
  checker's unit and three fresh copies of each executable (#509's comparison,
  `build.sh shifts` and `time-runs --copies`, 3 rounds of 54 runs a build), the
  branch's user cycles are 0.81%, 0.71% and 0.89% below `main`'s, every
  placement's range below in every round, and its wall time 0.69%, 0.58% and
  0.76%; `main`'s second set of executables, the comparison's floor, is within
  0.14% of the first in both. Warm
  (`benchmark-checker-bindings`, 5 passes), the class actions take 3.31 ms
  against 3.92. The runs, their scripts, the profiles and the visits' breakdown
  by role are in the
  [record](https://github.com/buster14a/buster-performance/tree/aedaec4d23a58d586b88b3158051f047beb19c62/docs/measurements/check-input-1038-graph-by-passes-2026-11-05).

  Not built: the numbering by counts and a prefix sum in a pass before the
  actions, and the operands by pointer jumping and segmented scans. A class's
  node count needs its children's values (a terminator makes one discard a
  value, a declined token none), which the value chains give each parent at
  once; the probe above, which finds them by consumer lists in a pass of its
  own, takes 3.21 ms warm, three times the operators' and lists' 1.06 ms, so
  the chains stay. The batch's numbering is the actions' own count, and what
  the stages asked to be data-parallel, the nodes' columns, is.
- **The graph's first node.** The walk's first node, in `sqlite3.c` a string
  at token 14, touches every graph column for the first time: each starts a
  huge page the kernel clears. That one node took 3.2 ms of the walk on the VM
  warm and over 100 ms after a build (the kernel compacting memory for huge
  pages), most of the walk's system time (`expressions.usage`). It is the
  graph's columns' first touch (#528's), not the walk's work: clearing them on
  a parked lane while the reader's earlier passes run would take it off the
  compile's path.
- **Open question 2: one classification for the reader's walks.** It can't be
  shared. The walk in source order (`check_input_resolve_range`) visits
  259,625 of `sqlite3.c`'s tokens and 2,332,173 of `main.c`'s, of which the
  token pass marks 236,159 and 2,129,308 ahead; the walk marks the other 9%
  itself as it goes (`check_input_visit`: a declaration's activation point,
  past its declarator). The expressions' classes read what that walk writes
  (the declared types, the declarations, the specifiers' finishes and the
  sparse columns), so they can be made only after it, and in postorder, not
  source order. The two classifications are each one vector pass already:
  the expressions' classes take 1.2 ms of `sqlite3.c` on the VM. What
  `check_input_resolve_range` costs (5.18% of `-c` on the 9700X) is its own
  per-token actions, which a shared list wouldn't change.
- **Open question 3: leaves in the stream's columns.** They compose. A graph
  leaf is a `CheckValue`, as a stream's value is, and the stream (#628) keeps
  its values as 14 bytes of columns (type, flags, object, category and
  constant kind) and a 96-byte payload row only for the 12% of nodes that need
  more. Of `sqlite3.c`'s leaves, the identifiers (93,450) need the columns
  alone, but the enumerators, and the literals (43,898) a payload row: about
  6 MB against the 13 MB those leaves take as `CheckValue`s, about 4 fewer huge
  pages to clear.
  The graph run, which now copies each leaf into the stream's columns, could
  then take them as they are. It isn't done here: 46 places read leaves as
  whole `CheckValue`s through `check_graph_leaf_of`, most in the graph run and
  the reader's later passes, outside the walk. #628 since made the leaves
  32-byte records with an integer's low 64 bits, so that the literals and
  enumerators need no row either (24,071 of `sqlite3.c`'s leaves had more than
  the columns, 80 have more than a record); the graph run writes an
  identifier's record into the stream's columns without a value between them
  ("The graph's leaves" in `docs/compile-time.md`).

What is left of the reader's per-token work is the walks' work where they
act, the expression-comma propagation, graph construction and the remaining
output fills. Since #572 the graph stores `CheckValue` only for VALUE leaves;
since #186 the parser and reader share one retained traversal. The sparse
walk consumes that reader-owned postorder, and the continuation's dynamic
visit mark is retained even when zero-valued sparse stores are skipped.

## Benchmark and validation limits

```sh
./build/compiler benchmark-checker --nodes 65535 --shape wide
./build/compiler benchmark-checker --nodes 4096 --shape chain
./build/compiler benchmark-checker --only scalar
./build/compiler benchmark-checker --only vector
./build/compiler benchmark-checker --only vector --after-scalar 200
```

The benchmark calls the production `check_graph_run` API. The timed interval
includes graph validation, dependency analysis and scheduling, action
preparation, output allocation, semantic actions and diagnostic collection.
Graph construction and the scalar reference run are outside timing; each
measured output is checked against that reference afterward. The wide shape
contains independent balanced arithmetic trees; the chain exposes the cost of
frontiers with only one ready node. The input is synthetic and contains builtin
unsigned arithmetic, so it does not represent complete source adaptation or a
real compiler corpus.

One warmup precedes 21 recorded passes. The default alternates scalar/vector
order; `--only` isolates one measured path, and `--after-scalar` conditions the
vector path with repeated scalar graph work before each timed sample. Output
reports median nanoseconds per node, useful node throughput, cycles and
instructions when available, and schedule/memory statistics. A denied or
multiplexed performance counter is printed as unavailable, never as zero.
The reported output/scratch bytes exclude the source graph and type table;
representative end-to-end measurements must include those producers too.

Cloud checks use Clang 19 on an AVX-512-capable EPYC host, not the project's
9700X. The unmodified native full build on that cloud host encounters an
existing `lane_wait` compile error for `_mm_monitorx`/`_mm_mwaitx`, because the
VM does not advertise MWAITX. Focused checker compilation avoids that unrelated
command. A temporary external compiler wrapper adding `-mmwaitx` can check full
compilation while tests avoid executing `lane_wait`. The cloud integration
build also substitutes `-O1` for the production `-O3` in that external wrapper;
checker benchmarks compile directly with Clang 19 at `-O3`. This does not establish
that the unmodified production build passes on the cloud VM, nor authorize a
production ISA fallback. The protected target build and measurements remain
separate required validation. No Zen 5 throughput improvement is claimed here.

### Original submission validation, 2026-10-23

The following historical results describe `d9b6144f23d0c03a84324e704742e9f101522c9d`
on the AVX-512 cloud EPYC host. They do not qualify subsequent takeover changes.
Production source and build flags were not changed to accommodate that host.

| Check | Configuration | Result |
| --- | --- | --- |
| Original checker suite | Clang 19, `-O1 -march=native`, repository arithmetic/aliasing flags; independent source oracle `/usr/bin/clang-19` | 40,743 checks, zero failures for each of seeds 133 and 1. |
| Full compiler integration | Clang 19 through the temporary external `-O1 -mmwaitx` wrapper; executable linked in `/tmp`, then installed as `build/compiler` | Build succeeded; integrated `test-checker` passed 40,743 checks with zero failures. No lane-wait command was executed. |
| Call-graph gate | `./build.sh check-call-graph`, original submission | 31 translation units, 16,451 functions, 90,840 calls; zero disallowed cycles and three existing allowed cycles. |
| Source driver target-model checks | Original integrated executable, default/supported options and eight unsupported-model cases | Supported defaults passed; all eight unsupported configurations were rejected. |
| AddressSanitizer and UndefinedBehaviorSanitizer | Clang 18, `-O1 -fsanitize=address,undefined -fno-omit-frame-pointer`, seed 133, Clang 19 source oracle | 40,743 checks passed with zero failures and no sanitizer findings. This includes target-option rejection and type-capacity failure/rollback. |
| Production graph benchmark smoke | Direct Clang 19 `-O3 -march=native`; wide 65,535 nodes, chain 4,096 nodes and one-node isolated vector mode | Scalar and vector outputs matched. Singleton frontiers reported scalar execution. Hardware cycles and instructions were unavailable. |
| Unmodified native full build on this VM | Production `-O3 -march=native` | Blocked by the existing MWAITX intrinsic/host-feature mismatch described above. |
| Protected target and corpus acceptance | 9700X timing/scaling and SQLite semantic corpus | Not run; still pending. |

Clang 19's sanitizer runtime libraries were absent, so the instrumented build
used the installed Clang 18 runtime. That temporary build suppressed
`-Wbitwise-instead-of-logical` for two existing lexer expressions that Clang 18
warns about. LeakSanitizer could not read `/proc` in this environment; the
successful run used `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`. Leak detection is therefore not a validated
gate here. Benchmark runs overlapped compilation on a shared VM, so their
unstable wall-clock rates are deliberately omitted from this record.

### Takeover validation, 2026-10-23

At checkpoint `411e413`, after integrating main through `c62cd84`, the expanded checker passed **67,677
checks, zero failures** with local Clang 23.1.3, seed 133, ASan, UBSan and leak
detection enabled. The source adapter's SHA-256 for this run was
`96b93a0fcbc23dbc16b504a4ac4e4acff8c2ef3b21d12a058f39daaf81f8831c`;
the source/test files stayed unchanged across compilation and execution.
This Ryzen 9 diagnostic run does not substitute for protected 9700X validation.
Independent source review also compared 86 original cases with Clang on both
schedules, finding no remaining Clang-rejected inputs accepted in that matrix.

The subsequent review fix counts named record members separately when checking
flexible-array eligibility. Its five new fixtures reject unnamed-only
predecessors, a union member and a non-final member, and accept a named
predecessor followed by unnamed padding. The full checker passed **67,719 checks,
zero failures** normally and under ASan, UBSan and leak detection, with seed 133
and the Clang oracle. The source adapter's SHA-256 was
`81a4050407102bf4f5d7ff2482a0e8c7ad3ef63484eca3180a29e06bc9e43b94`;
the source fixture file's was
`22338c9a6a73d7adf434f187a206db0888ae0cdac7d8d869269ae19b90ac90f6`.
These are also Ryzen 9 diagnostic results. Clang accepts some rejected
flexible-array forms as GNU extensions; the fixtures record those intentional
acceptance differences explicitly.

The source producer/binding/checker measurement harness, frozen input hashes,
and reproduction commands are retained in
[the measurement directory](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-checker-2026-10-23/README.md).
Whole-corpus semantics, multicore scaling and the runtime backend consumer
remain acceptance work even when these bounded fixtures pass.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). Actual sources:
[reader](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_check_input.c#L4),
[graph/stream](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_checker.c#L4),
[static selection](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_check_data_input.c#L4),
[static emission](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_check_data.c#L4).

| Producer → consumer | Status and boundary |
| --- | --- |
| Full parser tree/postorder and PP spelling → `check_input` | Implemented supported-source reader; declarations/types are elaborated in source order, expressions in postorder. Parser/PP source IDs are retained. |
| Declaration elaboration → binding event resolver → expression graph | Implemented checker-side events and exact declaration/type columns. Since [#336](https://github.com/buster14a/buster/issues/336), each use's retained answer is published directly, without the parser's second grammar replay ([the binding handoff](c-check-bindings.md)). |
| Literal spelling → `c_check_literals.c` → leaf `CheckValue` | Counted physical/logical spelling is decoded once for its graph value; invalid/unsupported literals are errors. Macro occurrence remains the diagnostic identity. |
| `CheckGraph` → dependency scheduler → `check_actions` / scalar actions | Implemented earlier-node operands, call argument slices, source tokens and contexts; actual operand edges define readiness (a postorder batch ends at a node that reads a member; level order, `CheckOptions.level_order`, groups by operand heights). Type-interning actions form ordered barriers. |
| Actions/constants/conversions → `CheckStream` → typed dump/diagnostics/static selection | Implemented node-indexed values, explicit operand/store/argument conversions and stable source-ordered reports. Faults in unevaluated branches remain conditional as described above. |
| Source declaration/object columns + stream → `check_data_input` → `check_data_emit` → Link | Implemented scalar static-data subset. Canonical object IDs and latest composite types select one definition, without reparsing or name lookup in the emitter. |
| Graph/stream → executable typed functions / SSA | Missing at this pin. Graph nodes plus conversions are not an evaluation-order function stream or CFG; [#133](https://github.com/buster14a/buster/issues/133)/[#136](https://github.com/buster14a/buster/issues/136)/[#131](https://github.com/buster14a/buster/issues/131)/[#134](https://github.com/buster14a/buster/issues/134)/[#145](https://github.com/buster14a/buster/issues/145) own the bridge. |

The detailed storage contract above applies: natural alignment, explicit
node/argument count and capacity, no readable padding, disjoint live graph,
output and scratch regions. Optional columns remain optional only as documented.
Node identity survives grouping. Every partial ready-level batch flushes at its
region/level end; logical size 1..16 is independent of the eight arithmetic
lanes. Singleton and exceptional actions run exactly; changing legal batch
partitions must preserve fresh-table TypeIds, values, conversions and reports.
A successful run can contain semantic reports; consumers check both status
and report count. Output rollback does not roll back valid appended types.

Static data reads immutable counted `CheckDataObject` records (four u32 fields)
and an object-to-symbol map. It plans counts/bytes before publication, rolls
back failed emission, and owns emitted bytes in a distinct arena that outlives
object writing/linking. Scratch is restored. Forward-reference definitions
must be in the same batch or already published; arbitrary independent batches
are not valid. Aggregate/string/TLS emission remains outside this implementation.

Observed extra work: source reader's event construction/replay, operand-height
construction/grouping, and scans over declaration columns to select objects.
[#336](https://github.com/buster14a/buster/issues/336) removed the replay: the
reader's events are the one producer, and their retained answers the column. Heights are semantic dependencies, so parser depth cannot
replace them. Retaining heights/selected-definition lists is a candidate only
if producer maintenance and downstream savings beat the existing complete
path; future selective emission must include real body references ([#129](https://github.com/buster14a/buster/issues/129)).
Builtin folding ([#148](https://github.com/buster14a/buster/issues/148)), source-point environments ([#113](https://github.com/buster14a/buster/issues/113)) and executable
function lowering are explicit integration obligations, not hidden consumers.

Disposition: retain the checked graph and transactional scalar-data path.
`test-checker` covers independent actions, guarded graphs, partition sizes,
source/Clang diagnostics, source binding identity and data-to-object execution
fixtures. Existing synthetic checker rates exclude source adaptation and are
not end-to-end evidence. [#133](https://github.com/buster14a/buster/issues/133) owns supported-source/corpus completion and the
whole source/check/data costs, allocations, memory and occupancy; [#145](https://github.com/buster14a/buster/issues/145)/[#138](https://github.com/buster14a/buster/issues/138)
own the eventual complete compiler/program-quality evidence.

## Nested selections and warning ranges (#603)

`check_input_range_root` consumes the parser's postorder/subtree sizes and the
reader's existing inverse postorder and subtree ends. A complete expression
occupies one contiguous run in both orders. The first token's own subtree
begins that run; its postorder position minus its subtree size, plus the
expression length, locates the root in constant work. The root's size and last
token validate the requested slice. Both source selections and declaration-time
constant evaluation use this lookup, with no additional syntax-tree column.
Raw attribute expressions retain their separate explicit-stack representation.

`c_check_warning_input.c` applies clearing and suppression immediately in their
original order. Its interval index has a two-byte summary per node and one leaf
per 64 tokens, rounded to a power of two: less than one eighth of a byte per
token for large inputs. The low byte summarizes clearable warnings; the high
byte summarizes missing suppression bits. Completed intervals skip repeated
updates, while a later warning write reopens its leaf's ancestor path. Range
queries cost logarithmic index work plus the blocks that still need changes;
repeated nested ranges no longer rescan their full token spans. Short ranges
before index initialization use direct updates. The index borrows reader scratch
and its first long range initializes conservative summaries, without reading
the whole unit's warning columns.

Scalar and masked AVX-512 block updates preserve independently evaluated bound
contexts, both warning columns and the graph's suppression facts. Loads and
stores stay within the requested range and logical token count. Boundary blocks
retain conservative summaries for their untouched bytes. Deferred workers own
separate indexes, so neither interval ancestors nor adjacent bodies' bytes are
shared mutable summaries; joining workers invalidates the parent reader's old
summaries. Replacing constant contexts also invalidates the index. Diagnostics
retain their existing source-order consumer.

`test-checker` checks every slice of two independent parser trees against a
parent-scan root oracle, and ordered warning mutations against an immediate
byte-update oracle with protected tails at every length through 257. Source
fixtures cover nested selected/discarded arms, vector results, lvalues, bound
warnings and rejected discarded expressions. `test-checker-deferred` covers
warning-bearing selections in several bodies. The scaling campaign's original
C generator and work-count instrumentation live in buster-performance under
`docs/measurements/nested-selections-603-2026-10-27`.

The [locked 9700X record](https://github.com/buster14a/buster-performance/tree/6ab3a0e96500957ce30a05f974806eb153f482ac/docs/measurements/nested-selections-603-2026-10-27)
compares main `92c025e7` with the implementation at `1014c296`. At depth
2048, the eight default-driver cases improve from 8.42–30.57 ms to
2.04–3.91 ms (4.1–8.4 times faster). Runtime discarded generics' root and
warning scans fall from 23,065,600 and 46,123,008 token visits to 8,192 bounded
root queries, 154,432 warning-byte visits and 107,908 index-node visits.
All five depths and both schedules are retained. Scalar and AVX-512 paths
pass the independent byte oracle; the available whole driver and the reader's
initialization/traversal/selection path are measured together with their own
costs, rather than claiming an isolated block-update rate.

With the shared traversal from #640, complete-root queries consume
`CheckInputReader.subtree_sizes` by postorder position. The reader borrows the
parser's optional size column when available; tree-only, prefix and repair
inputs derive one reader-owned column alongside their existing traversal.
This actual consumer requires four retained bytes per token plus a sentinel.
The first-child start column is temporary and released immediately after
derivation. The default vector path publishes sizes in the same traversal
pass; scalar/prefix/repair views derive them from their already retained
postorder, positions, subtree ends and first children. The parent-scan root
oracle explicitly requests the parser's optional size column. Warning tests
also cover independent-to-ordinary context transitions and late writes after
worker-column replacement against the immediate-byte oracle.

Frozen SQLite has a small measured cost in this paired build: syntax checking
+0.16% and object compilation +0.36%, with about 0.1 MiB more peak RSS and
byte-identical objects. The record separates the deep Clang enum oracle's
limit, focused sanitizer evidence, full checker/driver/deferred suites and
whole-process measurements. It makes no whole-compiler sanitizer/corpus claim.

### Statement-expression jump-region checks (#657)

The control reader uses its existing postorder intervals when checking
whether a case, goto or asm-goto enters a GNU statement expression. A unit with
no statement-expression regions needs no region walk. Otherwise the target's
ancestors are checked only up to the first subtree containing the source: every
region above that ancestor also contains the source. A case therefore stops at
its switch instead of revisiting every enclosing switch. The subtree test uses
the reader's `postorder_positions` and shared `subtree_sizes`; it allocates no
new token column and does not require the parser's optional size metadata.
The scalar reader keeps the full ancestor walk as an independent diagnostic
oracle. Both use the same strict region-boundary rules, including jumps out of
nested regions and refusals when entering sibling regions.

A goto's or a case's refusal reads as clang's jump scope checker words it
(#1942): "cannot jump from this goto statement to its label" or "cannot jump
from switch statement to this case label", with the note "jump enters a
statement expression" at the outermost entered expression's `(`
(`check_input_report_jump`, `CHECK_INPUT_NOTE_JUMP`). A jump that bypasses a
variable length array's, a cleanup variable's or a VLA typedef's
initialization takes clang's note at that declaration's name too ("jump
bypasses initialization of variable length array", "… of variable with
__attribute__((cleanup))", "… of VLA typedef"). Clang checks jumps in
`ActOnFinishFunctionBody`, once the body's scopes have closed. So these errors
complete past the body's `}`, after its unused variables' warnings, and count
in no scope (`check_driver_error_scoped`). A computed goto's and an asm goto's
errors don't take their notes yet ("possible target of indirect goto
statement"). The errors clang gives in the body after such a jump are still
lost: the rounds' edited reading that would find them isn't taken. Nor is
clang's `void` value of a statement expression whose last statement is a
`case` (`x = ({ case 1: 2; })`'s "assigning to 'int' from incompatible type
'void'") given.

`test-checker` compares both walks for every pair of tokens in nested and sibling
regions, including boundaries and absent tokens, on scalar/vector normal and
tree-only parser schedules with no parser-owned size column. It also checks 65,533 transfers
in a depth-65,536 chain, with and without an enclosing region, within one CPU
second, and runs the source-level legality fixtures against Clang. This bound
checks the repeated-walk regression; it is not a throughput measurement.

The [frozen qualification record](https://github.com/buster14a/buster-performance/tree/4df43f7d7a685133b6beade655d9e9a306947081/docs/measurements/unclaimed-perf-2026-10-28/issue-657/target)
retains the exact source/binary identities, stage clocks, every object and all
outliers. Those timings compare `bc1b4861` with `086c24b1`; they do not measure
this later integration's whole-compile throughput.

## Local labels and `__auto_type` (#147)

**Local labels.** A `__label__` list at a block's start (the parser's
`PARSE_KIND_LABEL` statement) declares its names as labels of that block, as
clang's scopes have them. The reader marks the list's tokens
(`check_input_local_labels`); the controls' first walk pushes the list's names
where it meets the list and pops them past the block's closing brace
(`CheckLocalLabels`). A label's definition, a goto and an `&&` inside the block
take the innermost list's name token as their scope, by which the labels' table
keys a label with its name (the function's body for the others). An inner list
so shadows an outer one and the function's label of the same name, and a goto
from outside the block reaches none of them. Clang's errors, at its places: a
list's name that its block doesn't define is "use of undeclared label" at the
name in the list, given as the block ends, after the errors within it, and a
missing label of the function's at its first use, once a name, as the function
ends (`check_input_report_label_undeclared`'s order key, the closing brace); a
second definition in the scope
is "redefinition of label" at its name, with clang's "previous definition is
here" at the first (a local label's at its name in the list). First is clang's
order: it defines a label as its statement completes, so of a label and one
within its statement (`L: L: ;`, `L: { L: ; }`, `L: M: L: ;`) the inner is the
definition, which gotos reach, and the outer the redefinition. The
redefinition is recovered (#1447): clang drops the label and keeps its
statement, so the unit is read on as a valid one, and a later duplicate case in
the body is given too. A name that a block's lists declare
twice is ambiguous where it is defined or used, undeclared at both of its
declarations as the block ends, and undeclared at a use as the function ends:
clang's lookup makes a label of the function's for it, which nothing defines.
Each ambiguous reference makes its own (#1844, `check_input_label_lookup`), a
definition's defined, and those labels are of the function's own scope, which
is the body's: a body's list's names and they are one scope's candidates, so a
reference of the function's own, or one where only the body's list declares the
name, finds two and more after an ambiguity and is ambiguous too, while one that
finds a single made label takes it (a later goto reaches an ambiguous
definition's; a later definition defines an ambiguous goto's). The driver gives
them in clang's order, each ambiguity with clang's notes "candidate found by
name lookup is 'X'" at its candidates in its lookup's order: the list's entries,
the latest first, then the function's labels in the order they were made
(`check_driver_label_candidates`). A unit without a list builds none of it; the vector path's mask
takes `__label__`'s kind among the first walk's events, which the scalar path's
full walk meets anyway.

**A labeled last statement.** A statement expression's value is its last
statement's through the labels on it, as clang has it: `({ ...; out: r; })` is
`r`, the idiom local labels serve. The region's final statement is found
through the labels' children (`check_input_region_prepare`); the expression
walk finds the region of a terminator whose parent is a label through its
labels (`check_input_labeled_region`, tested only in a unit with statement
expressions); the cached evaluation's label passes its statement's value on;
and the control writer marks the labels but leaves their statement to the
region's value (`CheckControlWriter.region_final`).

**`__auto_type`.** A variable's declaration with `__auto_type` among its
specifiers takes its type from its initializer, as clang deduces it in C: the
initializer's type converted as an lvalue's (its qualifiers dropped, but not
`_Atomic`, a kind), an array's and a function's decayed, with the specifiers'
`const` and `volatile`. Until the walk has read the initializer, the
declaration's type is a placeholder, `restrict int` with the specifiers'
qualifiers (`BUSTER_CHECK_AUTO_TYPE`), which no specifiers give otherwise; the
specifiers' `_Atomic` and `restrict`, which clang applies to the deduced type
(#1844), make it `restrict unsigned int` or `restrict long` instead
(`check_input_auto_type_qualifiers`), so `_Atomic __auto_type a = 1;` is an
`_Atomic(int)`, `const _Atomic` of an array's value a `const _Atomic(int *)`,
and `__auto_type restrict q = p;` an `int *restrict`; a pointer declarator's own
`restrict` is its pointer's (`__auto_type *restrict p`). Its
initializer registers a finish event (`check_input_initializer_prepare`), so
that the initializer's uses capture their declarations as an incomplete array's
do (`inference_end`). The finish (`check_input_auto_type_finish`) types the
initializer with the cached evaluator and gives the declaration, its
initialization's query and kind, and its linked type the deduced type, before
any later use reads it. The cached evaluator takes a compound literal now, as the
expression walk's leaf at its `}`: `(struct S){...}` and `(int[]){...}` are
initializers it types, in `typeof`'s regions too. A use of the variable in a
constant evaluation of its own initializer (`(typeof(x)){0}`,
`sizeof(typeof(x))`, an array's bound in a compound literal there) binds as
any use does, recording its declaration, so that the finish's scan of the
initializer reports it at the use, with clang's message.

Clang's errors, at its places: `__auto_type` anywhere but a declaration's own
specifiers (a parameter's, a member's, a type name's) at the keyword
(`CheckInputReader.specifier_auto_type`); a typedef's and a function's at the
keyword; a variable without an initializer at its name; an initializer list and
an array at the initializer; a use of the variable in its initializer at the
use, an outer variable of its name notwithstanding; a bit-field's value at the
member's name; a void or incomplete type at the variable's name; an `_Alignas`
below the deduced type's alignment at the `_Alignas`, which the specifiers and
the declarator leave to the deduction rather than check against the
placeholder's `int` (`check_input_attribute_type`'s `deduced`); and its clashes
with the other specifiers where clang reports them: a second type specifier at
itself, and a signedness or a width at its keyword, wherever it is in the list
(`check_input_auto_type_clash`). What clang deduces as C++ deduces a
declarator's type, and GCC refuses, is deduced as clang deduces it (#147): a
pointer or function declarator (`__auto_type *p = &x`, with the specifiers'
qualifiers at the placeholder), and a declaration's later declarators, which
must deduce the same type, by C23 `auto`'s deduction below
(`check_input_c23_auto_deduce`; `BUSTER_CHECK_DECLARATION_GNU_AUTO` keeps
`__auto_type`'s words in its errors). Declarators that deduce differently are
clang's error at the declaration, in the checker's words ("'__auto_type'
deduced types differ within one declaration"). A variably modified deduced
type is the initializer's, whose sizes its own declaration evaluated, and the
declaration evaluates none, as clang's doesn't (`__auto_type p = &vla;`); a
cast to a variably modified type in the initializer evaluates its bounds once,
as the cast does anywhere (#2321). A redeclaration of a
variable with linkage takes the earlier declaration's type rather than a
deduced one, as clang's C `mergeTypes` takes the placeholder before deducing
(`extern long p; __auto_type p = 1;` is a long, its initializer converted;
`check_input_auto_type_merged`), through pointer declarators, and a `restrict`
placeholder's over a restrict pointer. Where clang's `mergeTypes` refuses the
merge (#1844, `check_input_auto_type_refused`), clang's error is given: the
qualifiers of a level differ (const, volatile and restrict alike), a pointer
declarator meets no pointer, or an `_Atomic` placeholder meets no atomic type.
It reads "redefinition of 'p' with a different type: 'const __auto_type' vs
'int'", or "redeclaration" with `extern`, at the name. The placeholder is
printed through its declarator (`'volatile __auto_type *const'`,
`'_Atomic(__auto_type)'`, `'restrict __auto_type'`;
`check_semantic_auto_type_spelling`). The note is at the entity's latest
earlier declaration, "previous definition is here" for a definition or a
tentative one (`CHECK_REDECLARATION_AUTO_TYPE`). The reader stops there, as at
other initialized invalid declarations, so clang's later errors aren't given.
Refused by name: `_Atomic` and `restrict` through a declarator
(`_Atomic __auto_type *p`, whose initializer clang finds incompatible) or
together (clang's restrict error names `_Atomic(__auto_type)`), `_Atomic` of an
atomic value (clang's error names the placeholder's type), `restrict` of a value
that is no pointer to an object (clang's `restrict int`, which the placeholder
can't be told from), and type attributes with `__auto_type` but a vector's and a
mode's. A redeclaration of an atomic entity takes its type, as clang's
`mergeTypes` merges it (#1844): a plain placeholder any atomic type
(`_Atomic int r; __auto_type r = 1;` is an `_Atomic int`, and so is
`__auto_type *p` after `_Atomic int *p`), and an `_Atomic` one an atomic type
alone. An initializer's cast to an atomic type is deduced as the atomic type
(#2230, `__auto_type a = (_Atomic int)1;` is an `_Atomic(int)`). A vector's attribute (`vector_size`, `ext_vector_type`) and a
mode's meet the undeduced placeholder, as clang's handlers do (#1844,
`check_input_attribute_auto_type`): "invalid vector element type 'T'" at the
first vector attribute, T the placeholder's type there (the specifiers' bare
`'__auto_type'`, or after the name the declarator's whole type,
`'const __auto_type *'`), before "mode attribute only supported for integer and
floating-point types" at a mode. Clang goes on with the declaration; the reader
stops there.

`test-checker`'s source fixtures compare these with clang, their acceptance and
first error's line, on both paths, and its programs (`local-labels`,
`auto-type`) run natively against clang -O0's code. `test-checker-driver`
compares each error's line and column, and its text where the driver's message
is clang's (`c_checker_driver_gnu_test.c`).

**C23 `auto` and `constexpr` (#793).** In C23 a declaration's `auto`, when no
other type specifier is beside it, is a type specifier rather than the storage
class (`check_input_specifiers`, `check_input_declaration`). Its declared type
is deduced from its initializer as clang deduces it: the initializer is
lvalue-converted and decayed at the top, then each pointer, array, function and
aligned level of the declarator matches its own operands, an array's extent and
a function's parameters must be the same, and the placeholder takes the type
there with the specifiers' `const` and `volatile`. A pointer declarator
(`auto *p = &x`) and a declaration's later declarators (`auto a = 1, b = 2`)
are the language's, and each later declarator must deduce the same type, as
clang deduces `__auto_type`'s; file scope and `static`, `extern` and `register` are
C23's. `auto` with another type specifier (`auto int x`) keeps its storage-class
meaning, so `auto` and no initializer there is C's existing storage-class rule,
not a deduction. Deduced declarations are refused by name where clang refuses
them: a typedef's, a function's, a parameter's and a member's `auto` (a
parameter's and a member's where the parser's own grammar reaches them), one
without an initializer or with an array at its initializer, and an initializer
list. A deduced type that is incomplete, variably modified, or a
bit-field's value is refused as `__auto_type`'s is.

A C23 `constexpr` declaration declares an initialized object whose type is
const-qualified at its top level (`constexpr int *p` is `int *const`) and whose
initializer must be a constant expression; a pointer's must be null (or
`nullptr`), and its value exactly representable in the declared type (clang's
refusal of `constexpr char c = 300`, `constexpr unsigned u = -1` and
`constexpr float f = 0.1`). The reader evaluates the initializer once
(`check_input_constexpr_finish`) and keeps an integer, floating, complex or
null-pointer value for the object's uses' constant readings, so that an array
bound, `case`, `static_assert` or another constant expression takes it. An
aggregate's initializer is left to the initializer walk. At file scope a
`constexpr` object has internal linkage, as clang gives it. Refused by name:
`constexpr` with `typedef`, `extern`, `_Thread_local`, `inline`, a function
type, a volatile type or an atomic type, without an initializer, a pointer
whose initializer isn't null, and one whose initializer isn't a constant
expression or isn't exactly representable.

`test-checker`'s gnu23 source fixtures compare both forms with clang and check
their deduced and const types; `test-checker-driver` checks the driver accepts
the issue's original files and refuses the same places as clang in C23.

## Tail calls (#147)

clang's `musttail` on a return statement (GNU's spelling, `__attribute__((musttail))`, and `[[clang::musttail]]`)
makes the call it returns run in place of the function, so that recursion through it takes no stack.

**The reader.** The parser reads C23 attributes before a statement's keyword as that statement's. The reader records
each musttail return (`CheckInput.musttails`, `check_input_statement_attributes`) and reports the attribute's own
errors at clang's places: arguments, which it takes none of, and any place but a return (a declaration's, another
statement's, a null statement's). `[[gnu::musttail]]`, which clang ignores as an unknown attribute, and another
attribute on a return are refused as unsupported, as the checker's unknown attributes are.

**The rules.** `c_check_musttail.c` checks each recorded return once the expressions have their types, in source
order, the first rule each breaks in the order of clang 23's `Sema::checkMustTailAttr`:

1. its value is a call, of its expression with parentheses, `__extension__` and a selection's chosen operand looked
   through, but no cast;
2. both the function and the callee have prototypes;
3. both have the same calling convention;
4. neither is variadic;
5. the callee isn't declared `[[noreturn]]`: by an accepted unscoped standard spelling on a declaration of its entity
   before the call, whose attribute clang's later declarations inherit (`check_musttail_noreturns`), not one clang
   drops after the definition (`ignored`, `check_input_attribute_late`), GNU's or the `_Noreturn` keyword;
6. both have the same signature, as similar types (`hasSimilarType`: the same canonical type once each level's
   qualifiers are gone, an array's elements' too, past arrays of the same constant bound or of none and pointers'
   pointees, `UnwrapSimilarTypes`), at any depth: the steps go into types of their own, no more than the table has.

A return that keeps them can't leave a cleanup variable's scope: an error for each such variable, the innermost first,
which clang reports as its function ends. Errors past what their count holds (a uint32_t's, which each active cleanup
at each return can pass) are a resource failure (`check_musttail_collect`'s limit), checked with a limit of 5. The driver spells clang's messages (`check_driver_musttail_message`), each
scope's error at its place even where one return leaves several: the reports' walk keeps every one of them
(`CheckDriverDiagnostic.repeated`) rather than a token's first alone. A call of a builtin's, which the checker
computes with no call, is refused as unsupported.

**The lowering.** `check_program` lowers a musttail return as its call alone, a `TAIL_CALL` entry
(`check_lower_tail_return`): no conversion, since the call's result is the function's own, and no result object, since
a result through a hidden pointer goes where the function's own hidden pointer points. The function's tail calls' rows
are marked (`Tier0Function.call_tails`). Tier 0 emits the call's moves as a call's, copies its stack arguments into the
function's own incoming area, tears the frame down as the epilogue does and jumps ([the Tier 0 guide](tier0.md)'s
"Tail calls"), and the unwind tables describe each teardown ([the unwind guide](unwind.md)'s "Tail calls"). The
region reader takes a musttail return for a return, inside a statement expression too.

**Tests.** `test-checker` compares 48 source fixtures' acceptance and first errors with clang's
(`c_check_musttail_test.c`). Its programs run natively against clang -O0's build of the same source, by both paths:
`musttail-shapes`, 23 shapes of recursion 20,000 calls deep and more, each checking at its deepest call that its
frame is where its first was; `musttail-external-callees`, tail calls of the harness's clang functions and of the C
library's `isatty`; and `musttail-vectors`, for x86-64-v4. `test-checker-driver` compares every error's place and
message and the exit status with clang's on 41 fixtures (`c_check_musttail_driver_test.c`).

## Attributes clang doesn't know (#147)

Clang ignores an attribute it doesn't know, with `-Wunknown-attributes`'
warning (on by default; `-Werror=unknown-attributes` makes it an error, and
`-Wno-unknown-attributes` and `-w` silence it). The checker does too: it keeps
nothing of such an attribute and warns where clang warns. Before this
change, it refused such a declaration by name.

- **What clang knows.** `c_check_attribute_clang.inc` lists clang's
  vocabulary for C on x86-64 Linux: the GNU names whose `__has_attribute` is
  nonzero (a keyword's surrounding underscores removed) and the C23 spellings
  whose `__has_c_attribute` is nonzero under `-std=gnu23`, plain or scoped.
  `check_input_attribute_clang_knows` looks a name up by binary search: a GNU
  one as written without its underscores, and a C23 one with its scope
  normalized (`gnu`, and `clang` for `clang` or `_Clang`). An attribute clang
  knows is applied by the checker's rule where it has one; where it has none
  it is still refused by name, as before. An attribute clang doesn't know
  gives the warning event `CHECK_WARNING_ID_UNKNOWN_ATTRIBUTE` and isn't
  appended, whatever the checker's rules say of its name. GCC's own
  `externally_visible`, `optimize` and `access`, C23's `unsequenced` (which
  clang 23 doesn't take) and WebAssembly's `export_name` (which clang knows
  only for that target) are among them.
- **The warning.** It is at the attribute's first token, a C23 one's scope,
  and names it as clang does: `unknown attribute 'X' ignored`, a GNU name as
  written, a C23 one normalized (`'gnu::invented'` for
  `[[__gnu__::__invented__]]`; a name's underscores stay under another scope,
  `'msvc::__y__'`) (`c_check_warning_render.c`). Its group is
  `-Wunknown-attributes`, under `-Wattributes` (`c_check_diagnostic.c`).
- **The suggestion.** Clang's `; did you mean 'Y'?`, as its
  `DiagnoseUnknownAttribute` makes it (`check_warning_attribute_suggestion`):
  a C23 scope that isn't one of its 9 is corrected to the closest of them,
  then a name that isn't one of its 539 spellings (normalized for that scope)
  to the closest of those, where clang knows that one in the attribute's
  syntax and scope. The closest is the first of the fewest edits (insertions,
  deletions, replacements) within a third of the name's length, rounded up,
  a candidate skipped whose length differs by more than a third of the name's
  (`SimpleTypoCorrection`); only that one is tried, so `alignd`'s closest,
  `align` (a declspec's), gives no suggestion. The lists are clang's
  `AttributeSpellingList.inc` (the end of `c_check_attribute_clang.inc`).
- **`__clang__`.** Where a C23 attribute's scope or name goes, right after
  `[[` or after a comma in that list, the number the predefined `__clang__`
  expands to is clang's `_Clang`, with its `-Wignored-attributes` warning "'__clang__' is a
  predefined macro name, not an attribute scope specifier; did you mean
  '_Clang' instead?" (clang's `TryParseCXX11AttributeIdentifier`). The parser
  makes such a number the identifier `_Clang` before it reads the unit
  (`parse_clang_scope_numbers`, which asks the expansion history only where a
  number stands there); the checker warns at a `_Clang` whose source is a
  number (`check_input_attribute_clang_macro`). These parser-phase warnings
  precede the declaration's semantic attribute diagnostics, including an
  error at the same token. A direct expansion's definition note is at line 2
  of `<built-in>`; wrapped and redefined macros keep their recorded expansion
  provenance.
  In a GNU list, `__attribute__((__clang__::x))`, the number stays one, where
  clang's list ends with "expected ')'" at it.
- **Statements.** C23 attributes alone before a statement are the
  statement's where the next token starts no declaration, as clang's
  `isDeclarationStatement` decides (`[[x]] y += 1;`): the parser reads them so
  (`parse_scalar_attributed_statement`), and the checker's statement
  attributes too. With GNU ones among them it is a declaration unless each
  attribute, C23's too, is one of clang's statement attributes
  (`parse_scalar_standard_statement_attribute`: `fallthrough`, and
  `clang::`'s `always_inline`, `assume`, `atomic`, `code_align`, `likely`,
  `musttail`, `no_outline`, `noinline`, `nomerge`, `suppress` and
  `unlikely`, `gnu::`'s `always_inline`, `fallthrough` and `noinline`, and
  `msvc::`'s `forceinline`, `forceinline_calls` and `noinline`). A C23
  attribute refused on a statement is named as clang normalizes it, at its
  first token (`'gnu::aligned' attribute cannot be applied to a statement`).
  Before a label's name a statement's attributes are the label's
  (`[[x]] l:`), read as GNU ones after its colon are
  (`check_input_label_attributes`); their closer is the name's parent in the
  tree, so a label's name is the token before its colon there
  (`check_input_label_name`). A name clang refuses on a label is its error
  before the attributes are read, but a `suppress` whose argument is no string
  literal (`check_input_label_suppress_literal`, #2332): clang's parser drops
  it with its string error first, which the reading gives. A refused attribute
  before it in the list still gives its subject error instead. The parser reports clang's errors for C23
  attributes after GNU ones and before a block's `}`. Specifiers of
  attributes alone have no type: an implicit int's error at an unbound
  declarator's name. In a block, an already-declared ordinary name after GNU
  attributes is instead "expected identifier or '('" there, since clang
  annotates the name as an expression before attempting the declaration.
  A member has "type name requires a specifier or qualifier" after
  C23 ones, which clang's specifiers don't hold (GNU ones count as
  specifiers there).
- **A name after a block item's GNU attributes.** Clang's statement reader
  asks Sema what an identifier is before it reads a declaration
  (`TryAnnotateName`), and a name of no type is an annotation token no
  declarator starts with. So where GNU attributes make a block item a
  declaration, an identifier there that names an object, a function, an
  enumerator or a parameter in scope, or before C23 one before `(` (which
  clang declares implicitly as a function), is its "expected identifier or
  '('", in every standard (`int x; void f(void) { __attribute__(()) x = 1;
  }`). A name of no ordinary binding that a tag in scope has is clang's
  "must use 'struct' tag to refer to type 'x'" (its
  `isTagTypeWithMissingTag`, the tag's keyword named), and any other an
  implicit int's declarator, which C89 takes. The parser decides it
  (`parse_scalar_attribute_name`): its bindings tell a name it bound as an
  ordinary identifier (`PARSE_MEANING_DECLARED`) from one it never bound, and
  it keeps the tags in scope (`ParseScalar.tags`), each declaration popped at
  its scope's end with its undo-log entry (the binding trace's, or one of its
  own). A tag a definition's parameter list declares is in scope in its body
  (C17 6.2.1p4), kept with the parameters and bound again there. A
  prototype's tag ends with its list, and a K&R definition's declarations'
  tags end with them, as clang's do.
  The vector path's fallback parses one declaration with the file-scope
  typedef names alone, so a name it never bound there may be a file-scope
  object's or tag's, and the whole unit goes to the scalar parser
  (`ParseScalarResult.undecided`), which has every earlier declaration. An
  empty list (`__attribute__(())`) is a declaration's, as clang's needs an
  attribute to read a statement; after a control statement's head (`if
  (1) __attribute__((unused)) x = 1;`, where C has no declaration) and before
  a label's name they are a statement's, in the checker as in the parser
  (`check_input_statement_attributes`).
- **Statement attributes' handlers.** As clang's SemaStmtAttr takes them
  (`check_input_statement_attribute_kinds`), each with -Wignored-attributes'
  warning at the statement's first token: GNU's `noinline` and
  `always_inline` (`gnu::`'s too), a function's alone, "attribute is ignored
  on this statement as it only applies to functions; use
  '[[clang::noinline]]' on statements"; clang's (and `msvc::noinline`), its
  `CheckStmtInlineAttr`'s (`check_input_statement_inline_calls`): a statement
  without a call, and at each call of a function any of whose declarations up
  to the visible one has the attribute it overrides (`always_inline` for
  `noinline`, `noinline` for `always_inline`) or `flatten`, "statement
  attribute 'X' has higher precedence than function attribute 'Y'" with
  "conflicting attribute is here" at that declaration's first token;
  `nomerge` on a statement that evaluates no call, "'X' attribute is ignored
  because there exists no call expression inside the statement"; a `nomerge`
  of arguments, clang's "'nomerge' attribute takes no arguments"; and
  `suppress`'s arguments, string literals through parentheses
  (`suppress(("a"))`, clang's `checkStringLiteralArgumentAttr`); a non-string
  inside them (`suppress((1))`, #2332) is the string error, then the parser's
  "expected ')'" at the same token, noted "to match this '('" at the innermost
  `(` (`check_input_attribute_suppress_paren`). Not yet clang's: arguments
  clang's parser itself rejects (`suppress("a",)`, `suppress("a" x)`), after
  whose first error clang's parser goes on and buster's parser stops; and an
  implicit int's declaration after a statement's attributes (`__attribute__((unused))
  (x)++;`), whose redeclaration errors and `-Wignored-attributes` warning
  clang's Sema gives after the parser's errors, which stop buster (#2332). The calls
  are clang's `CallExprFinder`'s (`check_input_statement_call`): a call's
  `(` (not a declarator's parameters') or an asm statement's, outside
  `sizeof`'s, `_Alignof`'s and `typeof`'s operands and GNU attributes'
  arguments, to the statement's end (`check_input_loop_statement_end`). The
  callee is its name through parentheses and the prefix `*`, `&` and `+`
  (`(f)()`, `(*f)()`, `(&f)()`), as clang's `getCalleeDecl` finds it
  (`check_input_callee_name`).
- **Selections in statement attributes.** clang's `CallExprFinder` and
  `HasSideEffects` visit a `_Generic`'s or a `__builtin_choose_expr`'s
  selected arm alone, so a call or a side effect in another arm, or in
  `_Generic`'s controlling expression, counts for none of `nomerge`,
  `noinline`, `always_inline` and `assume` (`check_input_unselected`). An
  assumption's argument is evaluated where it is read, and the evaluator
  records the arm it takes (`CheckInputReader.selection_arms`). A statement
  with a selection is checked once the reader's selections are read
  (`check_input_statement_defer` and `check_input_statement_deferred`), with
  its callees' declarations as the bindings resolved them: a speculative
  evaluation as the statement is read would append binding events out of
  their order.
  MSVC's `forceinline` and `forceinline_calls` are ignored off its ABI with
  "'X' attribute ignored", at the statement or, on a function, at the
  attribute after its subject's check. An asm statement's C23 attributes are
  each "'X' attribute ignored" at its first token, unknown ones too, and read
  no further (`check_input_asm_statement_attributes`); its GNU ones, but a
  label's, are the parser's "an attribute list cannot appear here" at the
  first (clang's `ProhibitAttributes`).
- **Differences left.** After a block item's GNU attributes, a name the
  parser can't classify as clang's Sema does: clang's typo correction of an
  undeclared name (its "did you mean" and then "expected identifier or
  '('"), a builtin's name without a call, and the "call to undeclared
  function" error clang gives before that one in C99 and later.
- **Tests.** `test-checker`'s attribute fixtures check the vocabulary's
  order and lookups and what each kind of name keeps
  (`c_check_attribute_test.c`). `test-checker-driver` compares the warning
  lines with clang's for a source of every kind of unknown attribute
  (declarations, members, labels and statements, beside known ones, with
  suggestions and `__clang__`), on both paths, with no option,
  `-Wno-unknown-attributes`, `-Werror=unknown-attributes` and `-w`; the
  whole output of attributed statements and labels; and clang's lines for
  misspellings of every spelling, made by a fixed generator
  (`check_test_driver_attribute_typos`). It also asks clang every listed
  spelling again (`check_test_driver_unknown_attributes`). `test-parser`'s
  snippets cover the statement and declaration forms against clang, and
  `check_test_driver_block_attributes` (`c_check_block_attribute_driver_test.c`)
  the whole output, by both paths, of names after a block item's GNU
  attributes, under C89, gnu89, gnu17 and gnu23.

The vocabulary is regenerated where clang's changes, from the installed
`clang` and its `libclang-cpp`, in a scratch directory:

```sh
export LC_ALL=C
LIB=$(ldd "$(readlink -f "$(command -v clang)")" | awk '/libclang-cpp/{print $3}')
strings -n 3 "$LIB" | tr -c 'A-Za-z0-9_\n' '\n' | grep -E '^[A-Za-z_][A-Za-z0-9_]{1,60}$' | sed 's/^__\(.*\)__$/\1/' | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' | sort -u > candidates.txt
clang -dM -E -x c /dev/null | awk '{print $2}' | grep '(' | sed 's/(.*//' > function-macros.txt
grep -vE '^(__has_|__building_module$|_Pragma$|__is_target_|__is_identifier$|__identifier$|defined$|__pragma$|__embed|__assume$)' candidates.txt | grep -vxF -f function-macros.txt > names.txt
awk '{print NR " __has_attribute(" $1 ")"}' names.txt > gnu-query.c
clang -E -P -ferror-limit=0 -std=gnu17 -x c gnu-query.c 2>/dev/null | awk 'NF==2 && $2!=0 {print $1}' > gnu-lines.txt
awk 'NR==FNR{k[$1]=1;next} (FNR in k){print}' gnu-lines.txt names.txt > gnu.txt
{ awk '{print $1; print "gnu::" $1; print "clang::" $1; print "msvc::" $1}' gnu.txt; awk '{print "gnu::" $1; print "clang::" $1; print "msvc::" $1; print $1}' names.txt; } | sort -u > c23-candidates.txt
awk '{print NR " __has_c_attribute(" $1 ")"}' c23-candidates.txt > c23-query.c
clang -E -P -ferror-limit=0 -std=gnu23 -x c c23-query.c 2>/dev/null | awk 'NF==2 && $2!=0 {print $1}' > c23-lines.txt
awk 'NR==FNR{k[$1]=1;next} (FNR in k){print}' c23-lines.txt c23-candidates.txt > c23.txt
```

Each query is numbered by its line, so an answer is matched with its name
even where a name is a macro or clang reports an error for it. The spelling
list is clang-tblgen's, from `Attr.td` and the files it includes at the
installed clang's tag (`AttrDocs.td`, `ASTNode.td`, `DeclNodes.td`,
`StmtNodes.td`, in a `clang/Basic` directory):

```sh
clang-tblgen --gen-attribute-spelling-list -I . clang/Basic/Attr.td -o spelling.inc
```

Its `ATTR_NAME` and `ATTR_SCOPE_NAME` lines, in order, are the file's
`BUSTER_CHECK_CLANG_SPELLING` and `BUSTER_CHECK_CLANG_SCOPE` lines. `gnu.txt` and
`c23.txt` are the two lists, in the file's macros
(`BUSTER_CHECK_CLANG_GNU_ATTRIBUTE("name")` and
`BUSTER_CHECK_CLANG_C23_ATTRIBUTE("spelling")`), under its header; update
THIRD_PARTY_NOTICES.md's record, the counts `test-checker` checks, and
compare the lists with `Attr.td` before taking them.

## Attributes on declarations they don't apply to (#147)

Clang checks each attribute's subject before its handler runs: on a
declaration of none of its subjects, it warns "'X' attribute only applies to
SUBJECTS" (`-Wignored-attributes`) and keeps nothing of the attribute, or, for
some attributes, gives an error of the same words. The checker used to refuse
such a declaration, or keep the attribute where clang drops it. It now does as
clang does (`check_input_attribute_subjects`, `c_check_attribute.c`).

- **The table.** `c_check_attribute_subjects.inc` is clang's
  `diagAppertainsToDecl` for C, one row per GNU and C23 spelling: the C
  declarations the attribute takes (`BUSTER_CHECK_SUBJECT_*`), whether
  another is clang's error, and the subjects its message names. It is derived
  LLVM data: `tools/generate_attribute_subjects.c` generates it from
  `clang-tblgen --gen-clang-attr-parsed-attr-impl` of `Attr.td`. The
  generator maps each of clang's subject predicates to the C declarations it
  takes, and a predicate it doesn't know fails it. For example,
  `GlobalVar` takes a variable of global storage and `FunctionPointer` a
  declaration of a function pointer's type. A predicate of no C declaration
  (Objective-C's, C++'s, HLSL's) takes none. The lookup is a binary search by
  spelling: a GNU name without its underscores, or a C23 spelling with its
  scope normalized.
- **Declarations' subjects.** Each place that reads attributes says what its
  declaration is:
  - a function, inline or not;
  - a variable of global storage (thread-local or shared, external at file
    scope) or local storage;
  - a parameter;
  - a member, a bit-field or not;
  - a typedef, a struct, union or enum, an enumerator, or a label.

  Each of these is also function-like, prototyped, or a function pointer, by
  its type: a parameter by its adjusted type.
- **The handlers' own subjects.** Two attributes are checked by their
  handlers, not by the generated code, and rows of
  `check_input_subject_handlers` give them:
  - `sentinel` takes functions and variables (parameters too) of a function
    pointer's type: "functions, methods and blocks".
  - `transparent_union` takes unions and typedefs of a union's type:
    "unions".
- **Each declarator.** The specifiers' attributes are shared by the
  declarators of their declaration, and each declarator checks them again.
  `int __attribute__((cold)) a, b, f(void);` warns at `a` and at `b`, and
  `f` keeps `cold`. The record a declarator ignored is marked so
  (`subject_ignored`), and the next declarator decides afresh.
- **Function types' attributes.** A calling convention, `regparm` or GNU's
  `noreturn` on a declarator whose type isn't a function's, or a pointer's,
  array's or aligned typedef's to one, gets clang's "'X' only applies to
  function types; type here is 'T'" (`check_input_attribute_type_warnings`).
  The warning is at the attribute's first token, a C23 one's scope; the name
  is written without its scope, and the type is as clang spells it, its
  typedef first ('T' (aka 'int')). This covers the specifiers' attributes too,
  for each declarator.
- **Their order and their type.** clang warns of a declarator's function
  types' attributes as `GetFullTypeForDeclarator` builds its type, before
  `ProcessDeclAttributes` warns of the declaration's attributes. So
  `__attribute__((noinline)) void (__stdcall *p)(void);` warns of `__stdcall`
  first, wherever the type's attribute stands: among the specifiers, at a
  group's start, after a `*`, or after the declarator. Among themselves:
  - Where the type is or wraps a function, x86-64's "calling convention is
    not supported" comes for the specifiers' and the trailing ones first,
    which clang moves to the function's chunk, then for the chunks' own.
  - Where it isn't, a chunk's attribute warns as clang meets it, of the type
    built so far: the pointers before it and the suffixes after its group
    (`int (__attribute__((regparm(2))) *r);` is 'int',
    `int * __attribute__((regparm(1))) x;` 'int *'). The specifiers' and the
    trailing ones warn after them, of the declarator's whole type.

  Parameter attributes complete before the enclosing declaration's type and
  subject checks. A C23 list on a function's chunk is checked before the
  surrounding group's convention. At each completion boundary, type warnings
  precede the declaration attributes' own checks. The declarator's run keeps
  its parts' types for this, and its specifiers' attributes
  (`CheckDeclaratorRun.common_attributes`).

  A prohibited C23 list at a group's start still rejects the source. A
  diagnostic-only repaired view removes the list so the remaining declaration
  can produce its attribute warnings after the placement error. The type
  printer also retains the declarator's parentheses in diagnostics:
  `__attribute__((regparm(1))) int (*p);` names the type 'int (*)', matching
  clang's `ParenType` spelling without changing canonical type identity.
- **`nonstring`.** On a variable, parameter or field whose type is no pointer
  to a character type or array of one (through arrays of arrays), clang's
  handler warns "'X' attribute only applies to fields or variables of
  character array type; type is 'T'". The warning is at the declaration's
  first token, past `__extension__` and C23 attributes, as
  `Decl::getBeginLoc` places it (`check_input_attribute_begin`).
- **`transparent_union`'s own warnings.** These follow
  `handleTransparentUnionAttr` (`check_input_transparent_union_warning`):
  - an incomplete union, or one of no fields: a warning at the attribute;
  - a first field of a floating or vector type: a warning at that field;
  - a field of another size than the first's, or of a greater alignment: a
    warning at that field, with a note of the first field's size or
    alignment at it.

  Each union's member tokens are kept from its completion
  (`CheckInputUnionTokens`), for a typedef that applies the attribute later.
  A field of incomplete type (a flexible array member) leaves the attribute
  ignored without a word, as clang's does. A field less aligned than the
  first is fine (`type_mark_transparent_union`). It used to make the union no
  transparent one, which refused calls through it that clang takes.
- **Vector types in messages.** A vector type printed without an a.k.a. is
  described as clang's `ConvertTypeToDiagnosticString` describes it:
  "'T' (vector of N 'E' values)" (`check_driver_warning_type`).
- **Differences left.**
  - An attribute clang knows that the checker neither has a rule for nor takes
    generically (the next section's "Attributes without a rule") is refused by
    name before its subject is checked. The remaining handler and ABI
    boundaries are listed there; `selectany` has its own rule.
  - An error row's error (`int e __attribute__((error("x")));`) drops the
    attribute and the checker goes on, as clang does ("Semantic error
    recovery", #1447). An error it doesn't recover from still stops the unit's
    later diagnostics.
- **Tests.** `test-checker` checks the table's order and lookups
  (`c_check_attribute_test.c`): the counts, a row of each kind, `_Clang`'s
  scope, and an attribute whose handler checks itself. It also checks
  `type_mark_transparent_union`'s statuses
  (`c_check_transparent_type_test.c`). `test-checker-driver` compares the
  whole output with clang's on both paths (`check_test_driver_unknown_attributes`'s
  statements) for:
  - variables, functions, parameters, members, bit-fields, typedefs, tags,
    enumerators and labels, by GNU's and C23's spellings;
  - error rows, each in a unit of its own;
  - function types' attributes on other types, and shared specifiers;
  - `sentinel`, `transparent_union` and `nonstring`;
  - `transparent_union`'s own warnings, and a call through a union with a
    less aligned field.

The table is regenerated from the installed clang's tag, in the directory of
`Attr.td` and the files it includes (the spelling list's, above):

```sh
clang-tblgen --gen-clang-attr-parsed-attr-impl -I . clang/Basic/Attr.td -o parsed.inc
clang-tblgen --gen-clang-attr-parsed-attr-kinds -I . clang/Basic/Attr.td -o kinds.inc
cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_attribute_subjects.c -o build/generate_attribute_subjects
build/generate_attribute_subjects --self-test
build/generate_attribute_subjects parsed.inc kinds.inc "clang/include/clang/Basic/Attr.td at llvmorg-X (sha256 ...), by clang-tblgen VERSION" > src/buster/compiler/c_check_attribute_subjects.inc
```

Then update THIRD_PARTY_NOTICES.md's record and the counts `test-checker`
checks.

`c_check_attribute_arguments.inc`, how clang's parser reads each attribute's
arguments (`check_input_arguments_row`), is made the same way from clang's
parser switches, by `tools/generate_attribute_arguments.c`:

```sh
clang-tblgen --gen-clang-attr-parser-string-switches -I . clang/Basic/Attr.td -o switches.inc
cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_attribute_arguments.c -o build/generate_attribute_arguments
build/generate_attribute_arguments --self-test
build/generate_attribute_arguments switches.inc "clang/include/clang/Basic/Attr.td at llvmorg-X (sha256 ...), by clang-tblgen VERSION; see THIRD_PARTY_NOTICES.md" > src/buster/compiler/c_check_attribute_arguments.inc
```

A list's value that is a condition is evaluated for GNU's and C23's syntax on
x86-64; the generator fails on a condition it doesn't know.

## Attributes' places, GCC's positions and their handlers' checks (#147)

Clang reads an attribute's place before its subject: where a C23 list stands
decides whether it is the declaration's or a type's, and its parser and Sema
give their own diagnostics for some places. Several handlers also check their
arguments against the function. The checker now gives these as clang does.

- **The facts table.** `c_check_attribute_subjects.inc` also has a
  `BUSTER_CHECK_FACT_*` row for each spelling clang has on x86-64 Linux, from
  the same `ParsedAttrInfo` and from
  `clang-tblgen --gen-clang-attr-parsed-attr-kinds`:
  - whether GCC knows the attribute (`IsKnownToGCC`), and whether it isn't
    C's (its `acceptsLangOpts`, of which C holds only `!CPlusPlus`);
  - whether clang's generated handler applies it with nothing more
    (`SimpleHandler`), whether it is a type's or a statement's, and whether
    its arguments are parsed its own way;
  - whether clang ignores it wherever it is (`IgnoredAttribute`: `bounded`,
    `nv_weak` and two others);
  - its least and most arguments, and whether its last is variadic.

  The generator evaluates `existsInTarget` for x86-64 Linux, so a spelling of
  another target's is none. There are 366 GNU spellings and 283 C23 ones, as
  many as clang's vocabulary has (`check_input_fact_row`). A C-only
  attribute's condition (`!CPlusPlus`) is C's; the generator used to read it as
  another language's.
- **The exclusions table.** `BUSTER_CHECK_EXCLUSION` rows are clang's
  generated `diagMutualExclusion` pairs, by GNU spelling, where both
  attributes exist on x86-64: 44 of them (`check_input_exclusions`).
- **GCC's positions.** On a function's definition, each GNU attribute after
  the declarator that GCC knows gets clang's parser warning "GCC does not
  allow 'X' attribute in this position on a function definition"
  (`-Wgcc-compat`, `check_input_attribute_gcc_position`), at its name. The
  warning precedes a type attribute's argument error (`regparm`, `pcs`),
  even when argument processing prevents the declarator from completing.
- **nonnull's indices.** As `handleNonNullAttr` checks them
  (`check_input_attribute_function`):
  - an index of no parameter is the error "'nonnull' attribute parameter N is
    out of bounds", N being the argument's place, at the attribute;
  - an index of a parameter of no pointer type (a pointer, or a transparent
    union with a pointer member) is the warning "'nonnull' attribute only applies to pointer
    arguments", and the index is dropped;
  - with no pointer index left, a function of no pointer parameter warns
    "'nonnull' attribute applied to function with no pointer arguments",
    unless it is variadic or a macro wrote the attribute;
  - on a parameter, arguments are the warning "'nonnull' attribute when used
    on parameters takes no arguments", unless the parameter is a function
    pointer; without them, a parameter of no pointer type warns as above.

  The warnings come before the error, in argument order: each is published
  at the attribute's token and the error after it
  (`check_input_report_argument`).
- **A C23 list's place** (`check_input_standard_place`). A list after a
  function declarator's `)`, an array's `]` or a pointer's `*` is the
  declarator chunk's type's; one right after the specifiers (a type, a tag's
  body, `_Atomic(T)`) is their type's; a list right after another stands where
  that one does. There, as clang's `processTypeAttrs` and parser take them:
  - a `gnu::` attribute of a declaration after a chunk, even one clang doesn't
    know, is ignored with "attribute 'gnu::X' ignored, because it cannot be
    applied to a type" (`-Wignored-attributes`);
  - a `gnu::` attribute of a type there is applied, with "GCC does not allow
    the 'gnu::X' attribute to be written on a type" (`-Wgcc-compat`);
  - any other attribute of a declaration there, or any after the specifiers,
    is the error "'X' attribute cannot be applied to types"; so is
    `nocf_check` after the specifiers, as clang's parser has it;
  - `clang::noderef` is ignored anywhere in this syntax, and `vector_size`
    after the specifiers, with "'X' attribute ignored";
  - a list after a pointer's qualifiers (`int * const [[gnu::unused]] p`) is
    the parser's error "an attribute list cannot appear here".
- **Before C23** (#2409), each list gives clang's parser's extension
  warning "[[]] attributes are a C23 extension" at its first `[`, under
  `-Wc23-extensions` (`-Wc2x-extensions`), off by default and given by
  `-pedantic` (`CHECK_WARNING_ID_C23_ATTRIBUTES`): once, from the reader that
  reads the list (`check_input_attributes`, `CHECK_WARNING_EVENT_C23_ATTRIBUTES`).
  Clang's `-Wpre-c23-compat` warning of a list in C23 isn't given.
- **Lists before a declarator's suffixes.** C23's direct-declarator takes a
  list after the name and after each suffix, before the next one:
  `int x [[gnu::unused]] [2];` and `void f [[gnu::cold]] (void) {}`. The
  parser used to reject these. After a GNU attribute, the declarator has
  ended, so a suffix or a C23 list there is still an error
  (`parse_scalar_after_declarator`).
- **Calling conventions x86-64 hasn't.** `stdcall`, `fastcall`, `thiscall`
  and `pascal`, in any spelling, get clang's warning "'X' calling convention
  is not supported for this target" where they meet a function's type: a
  declaration's, a pointer's, a typedef's or a parameter's
  (`check_input_attribute_conventions`). A specifier's is warned of once for
  its declaration (`CheckInputReader.convention_warned`). On another type
  there is no such warning, as in clang.
- **Conflicting conventions.** Two conventions meeting one function are
  checked in source order, where they meet it, as clang's
  `handleFunctionTypeAttr` does: the second that differs is the error "A and
  B attributes are not compatible", by clang's names. Here `sysv_abi` and the
  conventions x86-64 hasn't are `cdecl`. A typedef's convention counts as an
  earlier one. On another type, conflicting conventions are no error; each
  gets the function-types warning. They used to be the generic error, also
  on other types, and a convention x86-64 hasn't conflicted with none.
- **Calling conventions starting a declarator.** One may start a declarator
  in parentheses (`void (__cdecl *f)(void)`), but not a later declarator of a
  declaration (`int a, __cdecl b;`), which is clang's parser error.
- **A tag declaration's attributes** (`check_input_tag_attributes`), as
  clang's `ParseClassSpecifier` and `ActOnTag` take them:
  - The attributes before a tag's name are the tag's. On a declaration or a
    reference that isn't its definition, they are checked as the tag's
    subject. Then they are kept for the definition, which takes them before
    its own: `struct __attribute__((packed)) B; struct B { char c; int i; };`
    is 5 bytes, and `aligned` takes the largest. The checker used to drop
    them, which changed such a struct's layout.
  - On a declaration after the definition, each attribute the definition
    lacks is warned of as "attribute declaration must precede definition",
    with a note at the definition, and ignored. C23's `deprecated`,
    `maybe_unused` and `nodiscard` may come after it.
  - An enum reference's attributes (`enum __attribute__((x)) E e;`) are
    ignored without a word.
  - The attributes after a reference's name are its declarators', as if they
    were written before `struct`: `struct S __attribute__((aligned(16))) v;`
    aligns `v`. The checker used to drop them too.
  - After a bare declaration's name (`struct S __attribute__((x));`), each is
    ignored with "attribute 'x' is ignored, place it after "struct" to apply
    attribute to type declaration".
  - A `transparent_union` of no definition is ignored with clang's warning,
    which clang gives twice.
  - A C23 list before a reference's name is the parser's error "an attribute
    list cannot appear here", at its first attribute. So is one after a bare
    declaration's name, at its `[[`. One after a reference's name is in the
    specifiers' place.
- **Type names.** A GNU attribute of a declaration in a type name (a cast's,
  `sizeof`'s or `_Alignof`'s, a compound literal's, `typeof`'s, `_Atomic`'s, a
  builtin's type argument, `_Generic`'s association) is ignored with "'X'
  attribute ignored when parsing type" (`check_input_attribute_in_type_name`),
  once. `sizeof(int __attribute__((mode(DI))))` used to be 8, where clang's
  is 4. A function type's attribute there on another type gets the
  function-types warning. A GNU attribute after a type name's declarator
  (`sizeof(int [2] __attribute__((x)))`) is the parser's "expected ')'". A
  parameter's is still its own.
- **`vector_size`** (`check_input_vector_size`), checked in the order of
  clang's `BuildVectorType`, each its error at the attribute:
  - an element of no builtin integer type but `bool`, no real floating type
    and no `_BitInt`: "invalid vector element type 'T'", the type as clang
    prints it with its typedef. Struct, union and enum elements used to be
    accepted;
  - a `_BitInt` whose width is no power of 2;
  - a size of no integer constant, negative (at the argument), of 2^61 bytes
    or more, zero, or no multiple of the element's size, or more than 2^32 - 1
    elements.

  A size the checker's types can't hold is still refused.
- **`-Wgnu-alignof-expression`.** C11's `_Alignof` and C23's `alignof` of an
  expression get clang's "'_Alignof' applied to an expression is a GNU
  extension" at the keyword (`check_input_alignof_expression`), once whichever
  evaluation reaches it. GNU's `__alignof__` doesn't. An operand that holds
  an undeclared name, which the walk builds no node of, gets it too, after the
  name's error, as clang reads it as an expression; a type name's doesn't
  (`check_input_alignof_undeclared`, #1946).
- **`-Wgnu-designator`** (#2202, `check_input_gnu_designator`). GNU's designators
  get the warnings clang's parser gives as it reads them, where the reader
  prepares the initializer: an array designation of one designator without
  its `=` (`[0] 2`), "use of GNU 'missing =' extension in designator" at the
  value, and an old-style field designator (`b: 1`), "use of GNU old-style
  field designator extension" at its name, both on by default
  (`CHECK_WARNING_ID_GNU_DESIGNATOR`, an extension `-pedantic-errors`
  promotes); and an array range (`[1 ... 2]`), "use of GNU array range
  extension" at its `...`, `-pedantic`'s (`CHECK_WARNING_ID_GNU_ARRAY_RANGE`,
  ignored by default). All three are `-Wgnu-designator`'s, under `-Wgnu`.
  Differences left: in a unit rejected by a parse error that holds a record
  definition, these warnings are dropped, as the reader's other warnings are
  there.
- **Attributes after a definition.** A function's or variable's declaration
  after the entity's definition may carry a declaration attribute that the
  definition lacks: the definition's own and those it took from earlier
  declarations. As clang's `checkNewAttributesAfterDef` does, the checker warns
  "attribute declaration must precede definition", with a note at the
  definition, and ignores the attribute
  (`check_input_attribute_after_kind`, beside `check_input_attribute_late`'s
  seven kinds). An ignored alignment no longer aligns the object:
  `int v = 1; extern int v __attribute__((aligned(16)));` keeps `v` at 4,
  where the checker used to give it 16.

  A redefinition of a compatible type is checked too, before its
  "redefinition of 'f'" error (#1920): clang merges its attributes when it
  declares it and reports the redefinition at its body or initializer. So is
  a redeclaration of a multiversioned function's version
  (`check_input_cpu_dispatch_redeclare`). A declaration that multiversioning
  refuses isn't merged, so it gets no warning.

  Some attributes are left alone, as clang leaves them: type attributes, the
  machine mode, the alias family, `overloadable`, the keywords, statements'
  and locals' attributes, and C23's `deprecated`, `maybe_unused` and
  `nodiscard`. Multiversioning's (`target`, `target_clones`, `cpu_specific`,
  `cpu_dispatch`) are checked where the declaration merges with the
  definition's, as `int f(void) {...} __attribute__((target("default"))) int
  f(void);` does. A target of another string than the definition's makes a
  new version instead, an entity of its own. A tentative definition is no definition
  here. The check runs on the serial path: a body that a lane checks gets
  none.
- **Deprecated and unavailable uses** (`check_input_deprecated_uses`). A use
  of a function, variable, typedef, tag or enumerator after a declaration
  that a `deprecated` attribute marks gets clang's "'X' is deprecated"
  (`-Wdeprecated-declarations`), or "'X' is deprecated: MESSAGE" with its
  string, and a note "'X' has been explicitly marked deprecated here" at the
  attribute. An `unavailable` one's use is clang's error "'X' is
  unavailable", with its note at the name of the declaration the use's lookup
  finds (a tag's latest declaration). An enumerator of a deprecated or
  unavailable enum is marked by its enum, which the note names. Where both
  attributes mark it, by one declaration or by several, the use is
  unavailable, whatever their order, as clang's `Decl::getAvailability` takes
  the worst; the message is the latest such declaration's. A use reads the
  marks of the declarations up to the one its lookup finds, which inherits
  those before it, as clang's redeclarations inherit their attributes; so a
  block's later `extern` redeclaration that a use doesn't see doesn't mark it,
  and a block's earlier one does mark a later file-scope redeclaration. As clang's
  context rules have it, a deprecated context drops a deprecated use's
  warning and an unavailable one an unavailable use's error too: the body of
  a function itself marked (by any declaration before its body), with a K&R
  definition's parameter declarations, a marked
  declaration's declarator and initializer, and a marked declarator's
  parameter lists and array bounds at any scope (a prototype's, a
  definition's, a function pointer's, named parameters or not;
  `check_input_availability_contexts`). A struct's, a union's or an enum's
  body in a declarator's bound, parameter list or initializer keeps its own
  context, as clang reads a member's or an enumerator's use through its tag to
  the tag's parent; in a function's body it takes the function's. A marked
  tag's own definition is its body's context, nested tags included: its mark
  is its declarations' up to the body, and an enum's attributes after it, but
  not a struct's (`check_input_availability_tag`). A fixed enum's base isn't
  in it: a use in the base (`enum E : T { ... } x`) is diagnosed once, in the
  declaration's first declarator's context, at any scope; in a declaration of
  the enum alone (`enum E : T { ... };`, `enum E : T;`), in the enum's
  (`check_input_enum_base_context`).
  A tag definition's name is a use of the tag only where declarators follow
  its body (`struct S { ... } x;`, its attributes after the body counting),
  as clang diagnoses the specifiers' type for them; a standalone definition's
  isn't. A tag's use in a declaration's specifiers is diagnosed once for each
  declarator, at any scope, each in its declarator's context
  (`check_input_tag_use_copies`): `struct S a, b;` warns twice, and
  `struct S a __attribute__((deprecated)), b;` once, where a typedef name's
  use is diagnosed once, in the first declarator's context. A declarator's
  name is past the tag's body, a fixed enum's base (whose typedef name is
  none) and attribute groups (whose names are none).
  A declaration's specifiers take its declarator's worst mark as
  their context too: a deprecated declaration's drop a deprecated typedef's or
  tag's use but keep an unavailable one's error, and an unavailable
  declaration's drop both. A declarator is an object's, a function's, a
  typedef's (`check_input_typedef_declared`: `C_BINDING_TYPEDEF` is every
  other token's zero too) or a marked member's, whose own mark is its
  specifiers', parameter lists' and bounds' context
  (`CheckInputReader.member_marks`). A declaration in a record's, a
  function's or a block's body is read from that body's own bracket level.
  In a function's body, only a typedef's or a member's mark is its
  specifiers' context: a block's object's isn't, as clang diagnoses a block
  declaration's typedef name as it reads it. That is the specifiers' alone (`typeof`'s operand
  and an attribute list included, with their commas): a `,` or `=` at the
  declaration's own bracket level ends an initializer or an earlier
  declarator, whose use keeps its own context, not a later declarator's mark.
  The declarator is the declaration's own, outside every parameter list: a
  parameter's mark is no context of its specifiers. A `{` ends the specifiers
  only at the declaration's own level; a compound literal's in a group
  (`typeof((T){0})`) is theirs. A tag's body in them isn't: an enumerator's
  value keeps its own context. There
  is none in a bare declaration of a tag.
  A typedef that its own marks don't mark takes the marks of the struct,
  union or enum it names (through qualifiers and other typedefs), as clang's
  `ShouldDiagnoseAvailabilityOfDecl` looks through an available typedef to
  its tag type: those of the tag's definition where one is before the use,
  else its first declaration's own (`TagType::getDecl`). The use names the
  typedef, the note the tag. An anonymous struct's, union's or enum's
  definition with declarators, in a type name or as an anonymous member is a
  use of its type at its keyword (`check_input_anonymous_mark`), once for each
  declarator, named as clang names the type: its first typedef's name, else
  `struct (unnamed at FILE:LINE:COL)`, or `(anonymous at ...)` for an
  anonymous member's (`check_driver_spelling_build`); an unavailable one's
  error names it too (`check_driver_reports`). The pass runs after the bindings, only in a unit where some
  `deprecated` or `unavailable` attribute was read: the uses are the tokens
  bound to another declaration, and each marked declaration is found once by
  the declaration its uses count by (its object's first, its tag's first),
  chained in source order to the next of the same one. The scalar path walks
  every token twice, for the contexts and for the uses. The vector path visits
  only the tokens where a step can do anything (#1489):
  - the identifiers whose key is marked, found 16 at a time by gathers
    (`check_input_deprecated_marked`);
  - the tag keywords and names, the functions' names and the marked members'
    names;
  - the bodies' and scopes' braces;
  - every identifier, where a tag is marked.

  It counts each visited token's bracket levels and parameter lists from
  64-token masks by popcount. Both paths run the same step at each token they
  visit (`check_input_availability_token`, `check_input_deprecated_use`).
- **Deprecated and unavailable members** (`check_input_member_uses`). A
  struct's or union's member that such an attribute marks is recorded when
  its record is completed, by its index in the type table's members
  (`CheckInput.marked_members`, ascending). After checking, in a unit with
  one, each `.` or `->` whose member the operand's checked record finds by
  its name (through anonymous members, as the checker found it) gets
  clang's warning at the member's name, or the unavailable error (where it is
  both, whatever the order) with its
  note at the member's declaration: of any operand (a dereference, an
  address, a call, a compound literal) and in `sizeof`'s. The error is a
  reader report made after checking, which the driver and the corpus
  comparison count. clang's context rules come from a byte per token
  (`CheckInput.availability_contexts`, `check_input_availability_contexts`):
  a deprecated or unavailable function's body, and a deprecated or
  unavailable declaration's declarator and initializer; a deprecated context
  drops a deprecated member's use, and an unavailable one either's.
  `offsetof`'s member designator is no use, as in clang. An initializer's
  uses are the initializers' walk's (`check_init_member_use`, through
  `CheckGraph.marked_members`): a designator's member, at its name, and a
  member a value takes by its position, at the value's first token, at each
  level of elided braces, as SemaInit's `DiagnoseUseOfDecl` has them. An
  unavailable member's error there is staged with the walk's warnings
  (`CHECK_WARNING_EVENT_UNAVAILABLE_USE`), which the driver gives as the
  error whatever the warning options (the walk stages its events under `-w`
  too, of which the driver gives this one alone), and the corpus comparison
  counts as the initializer's failure.
  An access only the reader's evaluator reads (`typeof`'s operand, a
  file-scope array bound, an enumerator's value, a static assertion's) is
  recorded where its member is marked (`CheckInputReader.evaluated_members`)
  and diagnosed by the uses' pass in its context, its declaration's
  specifiers' included; one the graph reads too (a variable length array's
  bound) is diagnosed once.
- **Deprecated overloads** (`check_input_overload_uses`). An overloadable
  function's declarations that a `deprecated` attribute marks (by them or an
  earlier redeclaration) are recorded by the uses' pass
  (`CheckInput.marked_overloads`). After checking, a call whose selection
  (`CheckStream.overloads`) is one gets clang's warning at the callee's name,
  in its context; an address taken through the overload set gets none, as in
  clang. An unavailable overload is the selection's error already.
- **Attributes without a rule** (`check_input_attribute_generic`). An
  attribute clang knows that the checker has no rule for is taken by its facts:
  - one clang ignores everywhere (`bounded`) is ignored without a word;
  - another language's (CUDA's `device`, `trivial_abi`, `called_once`) gets
    clang's "'X' attribute ignored";
  - one clang's generated handler applies with nothing more (`nouwtable`,
    `disable_tail_calls`, `xray_never_instrument`, the analyses' and
    Objective-C's) is kept with no effect, for its subject's check and its
    argument count's;
  - so are some whose own handlers check what is checked here: `minsize`,
    `optnone`, `noescape`, `annotate`, `uninitialized`,
    `no_sanitize_memory`, `analyzer_noreturn` and `carries_dependency`. With
    them come their handlers' warnings: `noescape` on a parameter of no
    pointer type, and `optnone` taking over `always_inline` and `minsize`,
    "'X' attribute ignored" with a note at `optnone`. `annotate`'s first
    argument must be a string literal. Its later arguments must be constant
    expressions, including floating values and addresses. A rejected extra
    argument names its position starting at 1 after the string, with the
    evaluator's note at the failing subexpression or division operator
    (`check_input_attribute_annotate`);
  - one of no C subject (Objective-C's, C++'s, the consumed analysis's:
    `objc_runtime_name`, `callable_when`, `vecreturn` and the others), whose
    subject's diagnostic is all clang gives in C;
  - `available_only_in_default_eval_method`, whose typedef-use restriction
    under an explicit translation-unit method is checked against the lexical
    evaluation history, including inherited and implicit `float_t`/`double_t`
    marks (see the floating-point pragmas above);
  - and these, whose handlers change nothing in C past their subjects,
    counts and arguments' kinds: `btf_decl_tag`, `randomize_layout` and `no_randomize_layout` (without
    `-frandomize-layout-seed`), `objc_boxable`,
    Fuchsia's `use_handle` and `release_handle`, `suppress`, `swift_bridge`,
    `unsafe_buffer_usage` and `amdgpu_max_num_work_groups`;
  - Objective-C's bridges, `objc_bridge`, `objc_bridge_mutable` and
    `objc_bridge_related` (#1447), which change nothing in C past their
    handlers' checks (`check_input_attribute_objc_bridge`): a first argument
    that is no identifier (an expression, or no argument of
    `objc_bridge_related` without parentheses, whose custom parsing skips the
    count) is "parameter of 'X' attribute must be a single name of an
    Objective-C class" at the declaration's first token (a tag's keyword, a
    typedef's first token); on a typedef `objc_bridge`'s identifier must be
    `id` as written ("parameter of 'X' attribute must be 'id' when used on a
    typedef") and its type a pointer to `void` ("'objc_bridge(id)' is only
    allowed on structs and typedefs of void pointers"), both at the
    attribute. A later argument is an expression whose names must name
    something, as for any generic attribute, a tag's too ("use of undeclared
    identifier", before the count's error). `objc_bridge_related`'s arguments
    are read as clang's ParseObjCBridgeRelatedAttribute reads them
    (`check_input_objc_bridge_related_parse`; the parser leaves them a
    balanced run of tokens, `parse_scalar_custom_attribute`): a class's
    identifier ("expected a related Objective-C class name, e.g.,
    'NSColor'"), `,` (expected past the class), an optional selector of an
    identifier and a `:` (not half of a `::`: "expected a class method selector
    with single argument, e.g., 'colorWithCGColor:'"), `,` ("expected ','" at
    the token, or the selector's error at a `:`), an optional identifier
    ("expected ')'" at another token, without a note) and the `)` ("expected
    ')'" with its note). Its C23 spelling with arguments, like the other
    attributes clang's ParseClangAttributeArgs reads its own way
    (`availability`, `external_source_symbol`, `type_tag_for_datatype`), is
    located at its name rather than its scope in every report
    (`check_diagnostic_custom_attribute_name`). `test-checker-driver`
    (`c_check_objc_bridge_driver_test.c`) compares each unit's whole output
    with clang's, by both paths, in GNU C17 and C23;
  - Swift's `swift_newtype` and `swift_wrapper` (GNU spellings alone) and
    `swift_async_error` (#1447), which change nothing in C past their
    handlers' checks. `swift_newtype`'s and `swift_wrapper`'s argument is
    read as clang's ParseSwiftNewTypeAttribute reads it
    (`check_input_swift_newtype_parse`, a balanced run of tokens to the
    parser): empty parentheses are "argument required after attribute";
    a token other than `struct` or `enum` is the warning "'X' attribute
    argument not supported: 'Y'" (Y the identifier or keyword as written,
    `(null)` for another token, as clang streams the token's
    IdentifierInfo), which drops the attribute before its subjects are
    checked, and consumes the token unless it is a parenthesis, bracket,
    brace or string; then "expected ')'" with its note. Without parentheses
    the handler's count, exactly one, follows the subjects' check.
    `swift_async_error`'s convention (`check_input_attribute_swift_async_error`)
    is `none`, `nonnull_error`, `zero_argument` or `nonzero_argument`, as
    written; another identifier is "'X' attribute argument not supported: 'Y'"
    at the attribute, which ignores it. The two indexed conventions take
    exactly two arguments, the second an integer constant in 32 bits
    (`requires an integer constant`, or the too-large error at it), and the
    others one. A convention other than `none` on a function with
    `swift_async(none)`, in either order, is "'swift_async' attribute must
    be applied to a function annotated with non-'none' attribute
    'swift_async'" at the `swift_async`. (A non-`none` `swift_async` needs a
    block parameter, which C hasn't, so its error-parameter checks never
    apply.) A first argument that is no identifier, on which clang 23 crashes
    (its handler reads it as an identifier), is refused by name.
    `c_check_swift_attribute_driver_test.c` compares their units as above;
  - Swift's names, `swift_name` and `swift_async_name` (#1447,
    `check_input_attribute_swift_name`), whose string must fit the
    declaration as clang's SemaSwift::DiagnoseName checks it, or its
    -Wswift-name-attribute warning (`CHECK_WARNING_ID_SWIFT_NAME_ATTRIBUTE`)
    at the string, which ignores the attribute. A function's is a Swift
    function name (`check_input_swift_function_name`): an optional
    `getter:` or `setter:`, a base name with an optional context of
    `.`-joined identifiers, and parenthesized labels each ending in `:`, with
    clang's rules for `self:`, `newValue:` and `subscript`; its count of
    labels must be the function's parameters' (an async name's one fewer,
    and a function of none is its own warning), more only for a
    single-parameter `init` of a function of none, fewer only where pointers
    to unqualified pointees, possible out parameters, make up the rest ("too
    few/many parameters in the signature ... (expected N; got M)"). A
    function without a prototype is -Wignored-attributes' "only applies to
    non-K&R-style functions". A variable's, a tag's, a member's, a typedef's
    or an enumerator's is an identifier with an optional context; a label's
    "cannot be applied to this declaration";
  - `swift_error` (#1447, `check_input_attribute_swift_error`): its
    convention, as written, `none`, `nonnull_error`, `null_result`,
    `zero_result` or `nonzero_result` (another identifier's warning ignores
    it); every one but `none` needs an error parameter, a pointer to a
    pointer to CFError, which clang takes to be the first struct bridged to
    `NSError` (`objc_bridge` or `objc_bridge_mutable`) that a check met, and
    then that struct alone (`CheckInputReader.cf_error`, so a body's lane
    refuses `swift_error`); `null_result` a function returning a pointer, and
    the two zero conventions one returning an integer, an enumeration or
    `bool`, each clang's error at the attribute. A first argument of no
    identifier, on which clang 23 crashes, is refused by name.
    `c_check_swift_name_driver_test.c` compares their units as above;
  - `cfi_salt("S")` (#1447, `check_input_attribute_salt`), a prototype's
    type's: the salt's text, interned (`intern_find_or_add`), is part of the
    function type's identity, in its info word's bits from
    `BUSTER_TYPE_FUNCTION_SALT_SHIFT` (`type_function_salt`), so two
    prototypes that differ by their salt alone are different and
    incompatible types (`type_compatible`: redeclarations' "conflicting
    types", assignments' and initializers'
    -Wincompatible-function-pointer-types, `_Generic`), and print alike, as
    clang prints no salt. A type made again from another keeps its salt
    (`type_function_like`, `type_function_extended_abi`). The list's last
    salt applies, to the first function inside the declarator's pointers,
    arrays and wrappers, as a calling convention does; an empty one is none.
    A function without a prototype is clang's "only applies to non-K&R-style
    functions". Where no function takes it, it is the function types'
    warning ("only applies to function types; type here is T"), and its
    subjects' error too, unless it follows the declarator's name (clang drops
    it there) or is C23's, a type's alone; a C23 one where a declaration's
    attributes are is "cannot be applied to a declaration", as the function
    effects' are, after a declarator's name too. A body's lane refuses it,
    since its salt is interned. `c_check_cfi_salt_driver_test.c` compares
    its units as above.

  Every attribute's arguments are read first as clang's parser reads them,
  by their kinds (`check_input_arguments_row`, from
  `c_check_attribute_arguments.inc`): an identifier first argument of one
  that takes an identifier is one; an argument it reads as an unevaluated
  string literal that is none is "expected string literal as argument of 'X'
  attribute" at it, wherever the attribute is; a type argument that starts
  no type is "unknown type name" (an identifier) or "expected a type" and
  "expected ')'" (see "Type arguments" below); and a generic attribute's expressions' names must name
  something where the attribute is (`check_input_thread_names`): an earlier
  declaration, a struct's earlier member, and a function declarator's
  parameters only for the attributes clang parses in the function's scope or
  after the declaration (`enable_if`, `diagnose_if`, the thread-safety
  analysis's). An attribute whose arguments clang parses its own way
  (`availability`, `external_source_symbol`, `counted_by` and the like) is
  its own.

  Five have rules of their own, with their handlers' checks and effects:
  - `lifetimebound` changes nothing in C. On a parameter of a function that
    returns void it is clang's error, at the attribute; a C23 one after a
    function's declarator is its type's, clang's "non-member function has no
    implicit object parameter" error; elsewhere its subjects' error.
  - `loader_uninitialized`'s variable is a definition without an initializer,
    in `.bss` by default, never common: where common linkage would be clang's (`-fcommon`,
    or `common`, without `nocommon` or a section) a weak one, as clang's code
    makes it for an initializer that isn't zero. An initializer, `extern`, an
    incomplete type, and adding it after the variable's definition are clang's
    errors. An incomplete variable names its type and its tag's forward
    declaration; a redefinition names the variable and its earlier definition.
    An ordinary explicit `section` places its undefined initializer in
    read-only storage, while named BSS and TLS families retain their section
    kind. Both the executable's atom class and the object's section flags
    retain that choice.
  - `weak_import`'s declaration is a weak reference where the unit doesn't
    define its entity. On a variable's definition (a tentative one, a local's
    or a parameter's) it is clang's warning "cannot be specified on a
    definition", and on other declarations its subjects' ("variables and
    functions"), both of which ignore it.
  - `internal_linkage` gives its declaration internal linkage: a local
    symbol, with `-Wundefined-internal` for a function used and never
    defined. A redeclaration that its entity's first declaration hasn't is
    clang's error, with its note there; on a parameter and on a variable of
    local storage, clang's warnings, which ignore it.
  - `nocf_check` is clang's "ignored; use -fcf-protection" warning wherever it
    is, whatever its arguments: the driver refuses `-fcf-protection`.

  clang's thread-safety analysis's attributes (`guarded_by`,
  `requires_capability`, `acquire_capability`, `capability` and the others,
  `check_input_thread_rows`) change nothing here past their handlers'
  checks (`c_check_thread_safety_input.c`). Their arguments are
  complete expressions, evaluated with the declaration evaluator, including
  members, calls, casts and binary operators. Each of
  their names must be the scope's there: an earlier declaration (not the
  one being declared), a struct's earlier member, or a function
  declarator's own parameter; one that names nothing is clang's "use of
  undeclared identifier", before any of the declaration's attributes is
  applied, after which clang's parser drops that attribute and the
  declaration goes on with its others (#2251, `check_input_names_drop`): so
  does a name in a generic attribute's expression or in a known attribute's
  argument expressions (`aligned(zzz)`, `alloc_size(nosuch)`,
  `check_input_read_attributes`). A name that clang's search corrects
  (`aligned(alignedy)` near a variable `alignedx`) still gets no suggestion
  and no "'aligned' attribute requires integer constant" after it; and a
  later warning of the unit's file scope can be dropped where a dropped
  attribute's declarator group is read again by a constant (`int v
  __attribute__((aligned(zzz))), w;` then `_Static_assert(_Alignof(w) == 8,
  "")`). Then, after their subjects and counts (with the handlers' "at
  least 1 argument"): `capability`'s name a string literal; a trylock's
  first argument an int or a bool; a lock function's integer literal
  argument an index of its function's parameters, with clang's "out of
  bounds" error and its extra information.

  `-Wthread-safety-attributes`, off by default, retains capability-type,
  pointer-subject, non-static-method, invalid-string and acquire-order-context
  warnings as sparse events. Capability checks preserve the typedef
  declaration that carries an annotation; annotating one typedef of `int`
  does not annotate every `int`. The completed declarator's parameters and
  earlier record members supply expression bindings without reopening the
  ordinary scope.

  The retain-ownership attributes (`cf_consumed`, `ns_consumed`,
  `os_consumed`, `cf_returns_retained`, `cf_returns_not_retained`,
  `ns_returns_retained`, `ns_returns_not_retained`,
  `ns_returns_autoreleased`, `os_returns_retained`, `os_returns_not_retained`,
  `os_returns_retained_on_zero`, `os_returns_retained_on_non_zero`) change
  nothing in C. Their handlers' checks are clang's
  (`check_input_attribute_ownership`): no C type is an Objective-C object or
  points to a C++ record, so CF's take a pointer (a parameter's pointee a
  pointer, for a returns' one) and NS's and OS's nothing; each warns where its
  subject is no type it takes, at the declaration's first token (a tag's
  keyword), and is ignored: "cf_consumed attribute only applies to pointer
  parameters", "'X' attribute only applies to functions that return a
  pointer", "... pointer-to-CF-pointer parameters", "... functions, methods,
  and parameters" and the others. `ns_returns_retained` is a function type's
  too: "'ns_returns_retained' attribute only applies to functions that return
  an Objective-C object" at the attribute, and "only applies to function
  types" off one.

  More have their handlers' checks and change nothing here
  (`check_input_attribute_handled`): `btf_type_tag`, `swift_attr` and
  `no_field_protection` (debug information's and other languages' type
  attributes); `vec_type_hint`; the function effects (`nonblocking`,
  `nonallocating`, `blocking`, `allocating`, whose analysis warns of nothing by
  default: an argument that is no integer constant is clang's "nonblocking
  attribute requires an integer constant") and `cfi_unchecked_callee`, function
  types' ("only applies to function types" off one); `init_priority`, a C++
  one, "'init_priority' attribute ignored"; `always_destroy` and `no_destroy`,
  clang's error at a variable's name of no static storage; `nomerge` on a
  variable of no function pointer type, "'nomerge' attribute is ignored
  because 'x' is not a function pointer"; `abi_tag`'s least argument;
  `matrix_type`, "matrix types extension is disabled. Pass -fenable-matrix to
  enable it" (and its subject's or count's error after it);
  `overflow_behavior`, its warning that it is ignored without its
  experimental option (`-Woverflow-behavior-attribute-ignored`), but on a tag
  its subject's; `xray_log_args`'s index of the function's parameters;
  `personality`'s function; `function_return`'s, `zero_call_used_regs`'s and
  `model`'s strings, an unknown one warned of ("argument not supported") or
  clang's "code model 'M' is not supported on this target", and those that
  change the code (`thunk-extern`, any zeroing, the large model) refused by
  name; and C23's `annotate_type`, a type's annotation, whose string and
  constants are checked as `annotate`'s are (at least one argument, its first
  a string literal, the others constant expressions) and which changes no
  type. A C23 type attribute before a declaration is clang's: `btf_type_tag`
  slides to the specifiers, with `-Wdeprecated-attributes`' warning under
  `clang::`, and a function effect or `annotate_type` there is "cannot be
  applied to a declaration".

  Static analyses' and Objective-C's annotations change nothing in C either,
  with their handlers' checks (`check_input_attribute_annotations`):
  `acquire_handle`'s integer parameter ("attribute only applies to output
  parameters"); `mig_server_routine`'s function whose result isn't
  `kern_return_t` (the last typedef of its spelling) of `int`, warned of at
  the declaration's first token; `malloc_span`'s function of no span-like
  result, warned of with clang's note why (incomplete, no struct, not two
  fields, an expected integer field, no pointer/integer or pointer/pointer
  pair: `check_input_span_reason`); `iboutlet`'s and `iboutletcollection`'s
  warning anywhere in C; `type_visibility`, of a tag its visibility ("argument
  not supported"), of a typedef "attribute ignored", of anything else "only
  applies to types and namespaces"; `param_typestate`'s and
  `return_typestate`'s state (unknown, consumed, unconsumed); the
  `lifetime_capture_by` family (no argument, `this`, the parameter's own name,
  an argument of no identifier, `global`'s, `unknown`'s and `this`'s
  -Wdeprecated-attributes warning, the spellings' arguments, and the names of
  no parameter checked once the function's parameters are known:
  `check_input_capture_names`); the ownerships' identifier, counts, pointer
  result and parameter indices (evaluated where they are read: out of bounds,
  of another type, or another kind's); `NSObject`'s and
  `objc_independent_class`'s warnings at the declaration's name
  (`-WNSObject-attribute`, `-WIndependentClass-attribute`), NSObject's typedef
  of no pointer to void or a record an error; `objc_precise_lifetime`'s error,
  no C type being retainable; and `enforce_read_only_placement`'s subjects.
  Other targets' vector types (`arm_sve_vector_bits`, `riscv_rvv_vector_bits`)
  are clang's target error wherever a declarator's type takes them ("is not
  supported on targets missing 'sve'"), a tag's subject's error elsewhere;
  `clang_builtin_alias`, and C23's `clang::builtin_alias` (#2291), is always
  its error on x86-64 ("can only be applied to a ARM, HLSL, SPIR-V or RISC-V
  builtin"); `blocks(byref)` is a local variable's
  (another argument warned of, a static local's or a nonlocal one's an error).
  The statements' hints, which change nothing at -O0, are clang's
  (`check_input_statement_attribute_kinds`): `code_align` and
  `opencl_unroll_hint` before a `for`, `while` or `do` ("only applies to 'for',
  'while', and 'do' statements" elsewhere), code_align a power of two from 1 to
  4096, opencl_unroll_hint positive, an argument of no integer constant
  "expression is not an integer constant expression", and a second code_align
  "conflicting loop attribute" noted at the first; `atomic` before a compound
  statement, its options `remote_memory`, `fine_grained_memory` and
  `ignore_denormal_mode` (each with `no_` or not). C23's `clang::likely` and
  `clang::unlikely` (#2291) on any statement, a label's or a `case` or
  `default` label too, without a word; an argument is their count's error, the
  other one in the same statement's attributes "'clang::unlikely' and
  'clang::likely' attributes are not compatible" noted at the first, and an
  `if` whose branches are both likely or both unlikely is -Wignored-attributes'
  "conflicting attributes 'clang::likely' are ignored" at the then branch's,
  noted at the else branch's, once the `if` is read (`check_input_statement_likelihood`).
  After the not-compatible error in a then branch, the rest of that `if` isn't
  read, so its conflicting-attributes warning, which clang still gives, is
  lost. On a declaration each is
  "cannot be applied to a declaration". GNU's `assume` is an empty
  statement's ("only applies to empty statements" elsewhere): one argument, a
  conditional expression (an assignment or a comma at its top is "use of this
  expression in an 'assume' attribute requires parentheses", at it), read as
  clang's Sema reads it (a name that names nothing first, a structure's or
  union's value "is not contextually convertible to 'bool'"), and dropped with
  -Wassume's "assumption is ignored because it contains (potential)
  side-effects" where it has them (an increment, an assignment, a volatile
  read, a call of other than a const or pure function; an operand of sizeof or
  typeof aside: `check_input_assume_effects`). Left: clang's -Wunused-value
  warnings inside it, which come from its expression's walk.

  clang's checks-only attributes (#1447) change nothing on Linux past their
  handlers' checks, which are clang's (`check_input_attribute_checks`,
  `c_check_attribute_checks_input.c`), and are taken in each spelling (GNU's,
  its keyword's underscores and `clang::`) but where said:
  - `availability`'s arguments are read as clang's parser reads them
    (`check_input_availability_parse`): a platform, then `introduced`,
    `deprecated` and `obsoleted` with versions, `unavailable`, `strict`,
    `message`, `replacement` and `environment` (clang's error in C), each
    malformed one clang's error at the token it names ("expected a version of
    the form 'major[.minor[.subminor]]'", "redundant 'introduced'
    availability change", "'x' is not an availability stage" and the
    others). Its handler's checks follow (`check_input_availability_handle`),
    with platforms named as clang names them: an unknown platform, stages out
    of order ("feature cannot be deprecated in macOS version 10.2 before it
    was introduced in version 10.4; attribute ignored"), `unavailable`
    overriding the others, mixed version separators, an Apple platform's
    version that its release numbering skipped
    (`-Winvalid-version-availability`, noted with the version clang takes
    instead), and anyAppleOS's, swift's and fuchsia's versions. A
    redeclaration's attribute for a platform its earlier declarations have
    with other versions is "availability does not match previous
    declaration", noted at the earlier one
    (`check_input_availability_redeclaration`); a tag's redeclaration's
    against the tag's attributes so far, its definition's where it has one
    (`check_input_availability_tag_merge`), a definition's own not against
    those it joins. No platform is Linux's, so no use is deprecated or
    unavailable by it.
  - `external_source_symbol`'s clauses (`language`, `defined_in`,
    `generated_declaration`, `USR`), each once, are read as clang's parser
    reads them (`check_input_external_source_parse`), with its errors at the
    token or the offset it names.
  - `enforce_tcb` and `enforce_tcb_leaf` name a TCB by a string; both kinds
    with one name on a function, on a declaration or across its
    redeclarations, are "attributes ... are mutually exclusive", noted at the
    other. In the body of a function its `enforce_tcb` attributes put in TCBs
    (its own and its earlier declarations', in the order clang merges them, a
    duplicate twice), each call of a function its callee names directly
    (through parentheses and `&`, an implicit declaration too) that isn't a
    library builtin is `-Wtcb-enforcement`'s "calling 'g' is a violation of
    trusted computing base 'x'" for each of those TCBs the callee's own
    attributes don't name, at the call's first token once the call is
    complete (`check_input_tcb_calls`). Operands of `sizeof`, `typeof` and
    `_Alignof` and a `_Generic`'s controlling expression aren't evaluated,
    and a call through a pointer names no function.
  - `callback`'s arguments are its callee and its payload among the
    function's parameters, by name or index (`__` an unknown one, `__this`
    unavailable in C): one callee, a parameter of function pointer type, not
    variadic, whose parameter count is the payload's, with clang's errors in
    its order (`check_input_attribute_callback`). It is LLVM's metadata, which
    -O0's code doesn't need.
  - `swift_async`'s kind (`none`, `swift_private`, `not_swift_private`) and,
    but for `none`, its completion handler's parameter index, a parameter
    that must be a block pointer returning void: no C parameter is, so that
    is its error.
  - `ns_error_domain`'s identifier, on an enum, must name a variable.
  - `modular_format`'s identifier and its aspects' strings (each once); its
    function needs `format` ("'modular_format' attribute requires 'format'
    attribute", once its declaration's attributes are merged), and a
    redeclaration's other arguments are "attribute 'modular_format' is
    already applied with different arguments". It is an optimization's hint.
  - The type tags (`c_check_type_safety_input.c`): `argument_with_type_tag`
    and `pointer_with_type_tag`'s kind, argument index and tag index, on a
    function or a function pointer, and `type_tag_for_datatype`'s kind, type
    and flags (`layout_compatible`, `must_be_null`) on a variable. The tag's
    arguments are read as clang's parser reads them
    (`check_input_type_tag_parse`), with its errors at the token ("expected
    identifier", "expected ','", "invalid comparison flag 'F'; use
    'layout_compatible' or 'must_be_null'", "unknown type name 'T'", "expected
    a type", "expected ')'"). The handlers' checks follow
    (`check_input_type_tag_handled`): each index an integer constant naming a
    parameter, a variadic function's past them too, `pointer_with_type_tag`'s
    argument a pointer parameter, and the tag's declaration a variable. Each
    function, function pointer or variable keeps its own rules and tags, or
    its previous declaration's where it has none of that kind, as clang's
    redeclarations merge them; a variable of an integer type with an
    initializer registers its tags for the initializer's value (an
    initializer that is no integer constant is "'type_tag_for_datatype'
    attribute requires the initializer to be an integer constant expression").
    At each call of a function or function pointer named directly,
    `-Wtype-safety` (on by default) checks each rule as clang's
    CheckArgumentWithTypeTag does (`check_common_type_safety`): the tag
    argument, through casts, `&`, `*`, a comma's right operand and a
    constant condition's arm, names a variable's tag or is an integer literal
    whose value was registered before the call; a tag of another kind is "this
    type tag was not designed to be used with this function"; else the
    argument, as converted for its parameter (but a pointer's own type where
    the parameter is `void *`, which a `void *` argument skips), must have the
    tag's type (a pointer to it), plain `char` matching the `signed` or
    `unsigned char` it is: "argument type 'A' doesn't match specified 'K' type
    tag that requires 'T'"; a `layout_compatible` tag compares as clang's
    isLayoutCompatible (`check_common_layout_compatible`), and a
    `must_be_null` tag wants a null pointer constant ("specified K type tag
    requires a null pointer"). Refused by name: a rule's index past a variadic
    function's parameters (whose calls clang checks for that argument), a
    rule on a member, a tag on a parameter, a tag whose type the constant
    reader doesn't read, and these attributes in a body a lane reads. A
    file-scope initializer that calls a function gives its tag's error before
    "initializer element is not a compile-time constant", which clang gives
    first.
  - `objc_ownership` and `objc_gc` are a declarator's type's, applied as
    clang's GetFullTypeForDeclarator distributes them
    (`check_input_attribute_objc`): the specifiers' to the declarator's
    outermost pointer, a chunk's from its pointer or its group outward, one
    after the declarator to the innermost pointer outside its outermost
    function, in type names too. Where no pointer takes one it is "'X' only
    applies to (Objective-C object or block) pointer types; type here is 'T'",
    naming the type where it stopped, before the declaration's attributes'
    checks; through macros, as clang places it: `strong` and `weak`
    named `__strong` and `__weak` and placed at the outermost macro's
    expansion where that macro is spelled so (findMacroSpelling), and
    `objc_ownership(weak)`'s pointer warning at the immediate expansion of the
    macro that wrote it (`pp_output_macro_place`). Where a pointer takes it,
    its argument must be an identifier;
    `objc_ownership(none)` and `(weak)` (named `__weak`) warn that C has no
    Objective-C object, `strong` and `autoreleasing` change nothing outside
    ARC, and another argument is "argument not supported". On a declaration
    of no declarator `objc_ownership` is "only applies to variables", and on a
    statement both are "cannot be applied to a statement", a wrong count's
    error first, as neither has subjects. `objc_gc(weak)` and
    `objc_gc(strong)` on a pointer make a garbage-collection qualifier that
    changes the type (`int *__weak`, which redeclarations, `_Generic` and the
    types' spellings see): the type table's extended qualifier (#1447, "The
    type qualifiers' attributes" below). A parameter's qualifier isn't its
    function type's, as clang's canonical parameter types are unqualified.
    `objc_ownership(weak)` is named `__weak` on a pointer alone; on another
    type clang's first test names the attribute.
    Both attributes' C23 spellings (#1447) apply where they are written: after
    the specifiers, a pointer or a function's or array's suffix, to that type
    alone, never moving outward (its warning naming that type); before a
    declaration or after a declarator's name (`CheckInputAttribute.objc_declaration`)
    as GNU's after a declarator, objc_gc's there sliding with
    -Wdeprecated-attributes' "applying attribute 'clang::objc_gc' to a
    declaration is deprecated" after the type's warnings, and after a
    typedef's name "cannot be applied to a declaration". On a declaration of no
    declarator, objc_gc's is "cannot be applied to a declaration" and
    objc_ownership's "only applies to variables" at the `;`. clang 23 runs out
    of memory on `[[clang::objc_gc(weak)]]` (or `strong`) before a
    declaration, after its deprecation warning; the checker gives that
    warning and applies it as after the declarator's name.
    `c_check_objc_c23_driver_test.c` compares their units with clang's in
    GNU C17 and C23 by both paths.
  - `diagnose_as_builtin` (`check_input_attribute_diagnose_builtin`,
    `c_check_attribute_checks_input.c`), clang's handleDiagnoseAsBuiltinAttr:
    its first argument names a builtin function, as getBuiltinID finds one: a
    name no declaration hides of one of the catalogue's builtins the language
    has (`__builtin_memcpy`, `__builtin_ia32_pause`), but a library
    function's, or the unit's declaration of a library function that is its
    builtin (`check_input_checks_library`); any other argument, a name in
    parentheses too, is "'diagnose_as_builtin' attribute requires parameter 1
    to be a builtin function" at its first token. Its builtin's
    parameters, its prototype decoded past any custom checking
    (`__builtin_va_start` takes one), must be as many as its other arguments
    ("references function '__builtin_memcpy', which takes exactly 3
    arguments"), each an index read as checkUInt32Argument reads it (not a
    constant: "requires parameter N to be an integer constant"; wider than 32
    bits: the value's error at it; -1 is 4294967295) of one of the function's
    parameters ("references parameter 4, but the function 'f' has only 3
    parameters"; a function without a prototype has none) whose canonical
    unqualified type is that builtin parameter's, else "parameter types do
    not match", at the index, naming the parameter's type as its declarator
    spells it and the builtin's as clang's declaration of it does
    (`cbuiltin_library_spell_parameter`: `'__size_t' (aka 'unsigned long')`,
    `'const char *restrict _Nonnull'` for a nonnull one, a vector's
    `'__attribute__((__vector_size__(8 * sizeof(short)))) short' (vector of 8
    'short' values)`, `FILE`'s by the unit's typedef, `'FILE *restrict
    _Nonnull' (aka 'struct F *restrict')`). A builtin's name in another
    expression, not called (`&__builtin_abs`, `__builtin_abs + 0`), is clang's
    "builtin functions must be directly called" at the name (at the outermost
    parentheses around it), then the argument's error at the expression clang
    recovers (its first token; a lone `&name`'s after the name), or, in a
    sizeof's, alignof's or typeof's operand, which clang doesn't recover,
    nothing more: the attribute is dropped. A redeclared builtin
    (`int __builtin_abs(int);`) is still that builtin, as an argument and in
    another expression, but a static one.
    More than 32 arguments are the count's error, as no builtin takes as many.
    An undeclared library function's name (`diagnose_as_builtin(memcpy,
    ...)` with no declaration of `memcpy`, or `(memmove)`) is its implicit
    declaration, as clang's lookup of a name in any attribute argument's
    expression makes it: "call to undeclared library function" with its
    header's note (C89's "implicitly declaring library function" warning),
    and the attribute takes it, so a later call declares nothing again
    (`check_input_generic_undeclared`). Refused by name is an index of 0, on
    which clang 23 crashes. A definition's identifier list compares its parameters by
    their declarations' types, the promoted prototype's standing aside, as
    clang's ParmVarDecl types do. It changes nothing else here: the
    `-Wfortify-source` warnings clang gives the function's calls, as it gives
    them a builtin's, the checker makes for no call (it has no
    `-Wfortify-source`).

  clang's NEON vectors (#1447), `neon_vector_type(N)` and
  `neon_polyvector_type(N)`, are vectors on x86-64 too: GNU vectors of a kind
  of their own (`BUSTER_TYPE_FLAG_NEON` and `BUSTER_TYPE_FLAG_NEON_POLY`, a
  vector's key flags in the type table, `type_vector_kind`), laid out, passed
  and computed as a GNU vector of their element and count, with which they are
  compatible (areCompatVectorTypes: assignments, redeclarations,
  `__builtin_types_compatible_p`, and an arithmetic operation's type the
  right operand's, as CheckVectorOperands takes it), but spelled as clang
  spells them (`'__attribute__((neon_vector_type(4))) int' (vector of 4 'int'
  values)`) and mangled as its x86-64 mangler mangles them
  (`_Z1f17__simd128_int32_t`; it has no name for `long`'s, refused). They take
  ext_vector_type's places (its record's kind, `CheckInputAttribute.neon`),
  with HandleNeonVectorTypeAttr's checks in its order
  (`check_input_neon_vector`): one argument, an integer constant ("'X'
  attribute requires an integer constant"), an element of one of
  isPermittedNeonBaseType's builtin types ("invalid vector element type 'T'":
  signed char, short, int, long, long long, each unsigned too, `__fp16`,
  `__bf16`, float and double; a polynomial vector's signed char, short and
  long long alone, AArch32's, as x86-64's triple takes them), and a vector of
  64 or 128 bits ("Neon vector size must be 64 or 128 bits"), its count
  truncated to 32 bits and its width computed in 32 bits as clang computes
  them (`neon_vector_type(4294967300)` of int is 4 ints). A mode attribute
  keeps an old NEON vector's kind (handleModeAttr). A C23 `clang::` spelling
  slides to the specifiers' type from the declaration's place
  (slidesFromDeclToDeclSpecLegacyBehavior) with -Wdeprecated-attributes'
  "applying attribute 'clang::neon_vector_type' to a declaration is
  deprecated; apply it to the type instead": one before the declaration (or a
  parameter's) as clang warns before applying it, one after a declarator's
  name once its type took it (CheckInputAttribute.neon_deprecated). After a
  typedef's name, it and `btf_type_tag` are ProcessDeclAttribute's "cannot be
  applied to a declaration" instead (`check_input_attribute_typedef`), the
  vector's after its element's, size's and count's errors
  (CheckInputAttribute.neon_typedef). A statement's is its error alone, and one in a tag's own list (`struct
  [[clang::neon_vector_type(4)]] S`) is ProcessDeclAttribute's "cannot be
  applied to a declaration" after a count's error, as `btf_type_tag`'s now are
  too.

  The type qualifiers' attributes (#1447): GNU's `address_space(N)`, the
  OpenCL address spaces (`opencl_global`, `opencl_local`, `opencl_constant`,
  `opencl_private`, `opencl_generic`, `opencl_global_device`,
  `opencl_global_host`), which clang 23 takes in C as address spaces too, and
  `objc_gc(weak)` and `objc_gc(strong)`, are clang's extended qualifiers, part
  of a type's identity: the type table's `QUALIFIED` record (docs/c-types.md's
  "Extended qualifiers"), made by `type_extend`. An address space applies where
  clang's processTypeAttrs applies it: the specifiers' type
  (check_input_attribute_type), a pointer's chunk's pointer or a group's type
  outside it (CheckDeclaratorPart.spaces, which CheckInputAttribute.space_applied
  keeps from the whole type), and the whole type after the declarator; an
  array's space is its element's. clang's checks are made in its order
  (check_input_address_space): a function type's "function type may not be
  qualified with an address space", address_space's count ("takes one
  argument", before its subject, as a type attribute's), its argument ("requires
  an integer constant", "address space is negative", "address space is larger
  than the maximum supported (8388580)"), "multiple address spaces specified
  for type", and -Wduplicate-decl-specifier's "multiple identical address spaces
  specified for type"; the deprecated OpenCL spaces' -Wdeprecated-attributes
  warning (with clang's doubled quotes) comes from the parser, at the attribute.
  The declarations a space can't qualify are clang's errors at their names: an
  automatic variable, a parameter (as declared, through a typedef), a member,
  and a function's compound literal (at its `(`). objc_gc's weak and strong
  qualify the type where check_input_attribute_objc's distribution handles
  them, the declarator's outer levels made again from there
  (check_input_objc_requalify), with clang's "multiple garbage collection
  attributes specified for type" and -Wignored-attributes' `__weak` warnings
  on an automatic variable and a member.

  Types print as clang's (`'__attribute__((address_space(1))) int *'`,
  `'const __global int *'`, `'int *__weak'`, `'__attribute__((address_space(1)))
  T *'`). Compatibility needs equal extended qualifiers, so redeclarations and
  `_Generic` see them; `__builtin_types_compatible_p` and `typeof_unqual` drop
  them, as clang's getUnqualifiedType does, and a value has none. A member's
  lvalue takes its record's space (not its garbage collection qualifier).
  Pointer conversions follow clang's checkPointerTypesForAssignment: a pointee's
  space the target's doesn't include (clang's isAddressSpaceSupersetOf in C:
  equal spaces, OpenCL's generic over every space but constant, global over its
  device and host parts; the default space and a target's, 0 among them, over
  no other) is the error "changes address space of pointer", worded by the
  conversion (assigning, initializing, returning, and passing with clang's note at
  the parameter: c_check_semantic_diagnostic.c's check_semantic_address_space);
  a garbage collection qualifier changed is the discarded qualifiers' warning.
  A subtraction's and a conditional's pointers to spaces neither of which
  includes the other are clang's "pointers to non-overlapping address spaces"
  error (a conditional's at its `?`); a conditional of overlapping spaces is
  clang's C result, the composite without the spaces (one type keeps its own,
  and a void pointee takes the other's), and comparisons and casts take any
  spaces.

  Lowering: clang's x86 target maps address spaces 256, 257 and 258
  (`__seg_gs`, `__seg_fs`) to the `%gs`, `%fs` and `%ss` segments, whose loads
  and stores take a segment override. A load or store in `%fs`, `%gs` or `%ss` of an
  integer, a floating value (long double too), a 128-bit integer or a vector,
  by indirection, member (a bit-field's unit's pieces too), subscript or name, and an assignment, compound
  assignment, increment or decrement of one, takes it (check_lower_access: the
  typed stream's `BUSTER_TYPED_ACCESS_SEGMENT` on its LOAD, STORE or SET, which
  Tier 0 puts on the access's memory operand; docs/tier0.md); a global in one
  goes through its address, as a memory class's does (`lea sym(%rip)` and then
  `%gs:(%rax)`, the byte clang's `%gs:sym(%rip)` reaches). The atomic builtins
  that a direct plan takes take it too (their LOAD or SET, `lock xadd`, `xchg`,
  `lock cmpxchg`, a fetch's loop), from the object type's space or the pointer
  argument's pointee's. Tier 1 falls back to
  Tier 0 for such a function. The rest is refused by name ("a load or store in a
  segment's address space", check_lower_segment_access, where a unit has such a
  type at all: CheckLowerWriter.segments): an aggregate's copy past 8 of the
  target's widest vector registers (as clang stops on), atomic
  builtins of another plan (generic ones by pointer, `__atomic_clear`,
  `__sync_lock_release`); and so are the accesses whose lvalue doesn't carry the
  space: a vector's element, read or written (its type loses the vector's
  space), and an asm statement's register output (stored through its address;
  "an asm output in a segment's address space ... but a memory one"). Those and
  the atomic builtins compiled without the override before #1447's segment work. A memory asm operand's address is
  the template's, without the segment, as clang's is. Taking an address there is
  compiled. Any other space's code is the default space's, as clang's.

  C23's spellings (`[[clang::address_space(1)]]` and the others) apply where
  a C23 type attribute does: after the specifiers or a pointer, to that type;
  before a declaration or after its name, to the declaration's type, which
  clang's legacy rule lets them slide to (slidesFromDeclToDeclSpecLegacyBehavior),
  with `-Wdeprecated-attributes`' "applying attribute 'clang::address_space' to
  a declaration is deprecated; apply it to the type instead", before the type
  takes it or once it took it, as the NEON vectors' (#1447). On a tag's own
  list or a statement they are clang's errors.

  Clang names a type by the macro that wrote its space (MacroQualifiedType:
  `'AS1 int *'`), declaration by declaration. Here an address space error's
  operands and a failed conversion's source and target do the same: the
  macros their own declaration writes (`check_driver_space_scope`, #1447
  item 2), so `ASA int *b` prints `'ASA int *'` where `AS1` writes the same
  space elsewhere, and a space it writes directly is spelled out.
  What differs from clang: other types, and a space their declaration doesn't
  write (a typedef's), print by the macro only where one macro writes that
  space throughout the unit (`check_driver_space_macros`). Its attribute is
  spelled out in three cases: a space the unit writes both ways or with two
  macros (`address_space(1u)` is space 1 written directly), every space where
  an operand isn't an integer constant (`address_space(1+0)`), and, in
  `__builtin_dump_struct`'s output, a member that writes several spaces or one
  directly (`docs/c-builtins-integration.md`). Clang spells a group's leading
  space where it was
  written (`'int (__attribute__((address_space(1))) *)[3]'`); a garbage
  collection qualifier inside a function's level is refused by name, and the
  specifiers' one that moves to a pointer whose chunk has another is
  "multiple garbage collection attributes" at the chunk's, where clang's is at
  the specifiers'. The discarded qualifiers' warning keeps the checker's generic
  wording for these pointers as for any. A vector's element loses its space, as
  it loses its `ALIGNED`.

  Other targets' calling conventions (`aarch64_sve_pcs`,
  `aarch64_vector_pcs`, `m68k_rtd`, `pcs`) are conventions x86-64 hasn't, as
  `stdcall` is: clang's "calling convention is not supported for this target"
  warning where they meet a function, which ignores them, and "only applies to
  function types" off one; `pcs`'s argument is checked first where it meets a
  function or a tag ("'pcs' attribute takes one argument", "invalid PCS
  type"). An identifier there (`pcs(x)`) is "'pcs' attribute requires a
  string" at it, then clang's handler goes on with its text: `aapcs`'s
  unsupported convention warning (and on a tag the one of no function), any
  other's "invalid PCS type" at the attribute (`check_input_attribute_pcs`,
  CheckInputAttribute.deferred); on another type the attribute's warning alone,
  as clang's type processing never reads the argument. The GPU kernels' (`amdgpu_kernel`, `device_kernel`,
  `nvptx_kernel`) are a function declaration's: the same warning on one, and
  "'X' attribute only applies to functions" (an error) on any other
  declaration. A convention on a tag's declaration, or GNU's `noreturn`, is
  "'X' attribute only applies to functions and methods" (after an unsupported
  convention's warning), as clang's handlers give it to a declaration without a
  declarator; a bit-field's after its width meets its type
  ("only applies to function types"). A convention's wrong count of arguments
  is clang's error where it meets a function or a tag ("'X' attribute takes no
  arguments", after a definition's `-Wgcc-compat` warning), and on another
  subject its subject's warning alone (`CheckInputAttribute.convention_count`);
  its arguments are expressions to clang's parser first, whose errors (an
  undeclared name's) drop it wherever it is (#1447), but `pcs`', a string's. A variadic function's `vectorcall` or
  `regcall` is clang's "variadic function cannot use vectorcall calling
  convention", and so is a `swiftcall` or `swiftasynccall` one's; a declaration
  (not a definition or a typedef) of such a function without a prototype is
  "function with no prototype cannot use the vectorcall calling convention"
  (clang's `err_cconv_knr`), unless an earlier declaration of the name is
  visible, which clang merges first (#1447).

  `intel_ocl_bicc`, `swiftcall` and `swiftasynccall` are function types'
  conventions (#1447), spelled in types as clang's `TypePrinter` spells them:
  `inteloclbicc`, `swiftcall` and `swiftasynccall` where a declaration's
  attribute prints, and `intel_ocl_bicc` (and `swiftasynccall` without its
  space) in an a.k.a.'s canonical type (`BUSTER_TYPE_PRINT_CANONICAL_CC`). A
  function designator's `preserve_*` prints before it
  (`'__attribute__((preserve_most)) void (void)'`). A convention that one
  macro writes throughout the unit names the type by that macro, before the
  canonical spelling, as clang's `MacroQualifiedType` does
  (`check_driver_convention_macros`:
  `'MS void (void) __attribute__((ms_abi))'`). The Swift parameter
  attributes (`swift_indirect_result`, `swift_error_result`, `swift_context`,
  `swift_async_context`) are a parameter's ABI in its function's type
  (`TypeParameterABI`), printed before its type
  (`void (__attribute__((swift_context)) void *)`), so that two functions'
  types differ by them. A parameter's are checked as
  `SemaSwift::AddParameterABIAttr` checks them, at the attribute: a pointer
  ("'swift_context' parameter must have pointer type; type here is 'int'"), a
  pointer to an unqualified pointer for `swift_error_result`, and two ABIs
  on one parameter ("swift_async_context and 'swift_context' attributes are
  not compatible", with its note); then the declared function's own, once its
  convention is final (`check_input_declarator_conventions`), as
  `checkExtParameterInfos` checks them, at the parameter: the convention
  (swiftcall or swiftasynccall, swiftcall's alone for an error result, and any
  for `swift_async_context`, whose function clang's backend gives it r14, which
  the producer refuses by name; the convention's error once per function, at
  its first incompatible parameter, as clang's `hasCheckedSwiftCall` has it), an
  indirect result first or after another, an error result just after a
  context. A function chunk farther from the name than the nearest one
  (`void (*f(void))(…)`'s returned function's, `void (*(*p)(int))(…)`'s) has no
  convention when clang builds it, as distribution gives the declaration's
  conventions to the nearest, so its parameters are checked as System V's
  there (#1447). The nearest chunk of a declaration's pointer or array
  declarator (`void (*p)(…) __attribute__((swiftcall))`) is checked once the
  declaration's convention is final, with the conventions clang distributes to
  it, the specifiers' and the trailing ones, but not one written in the
  pointer's own chunk (`void (__attribute__((swiftcall)) *p)(…)`, between the
  parentheses around the name and the name), which applies after clang builds
  it (`check_input_declarator_conventions`, `check_input_declarator_group`).
  The nearest chunk of a parameter's, a member's or a type name's declarator is
  refused by name. Off a parameter, each is "only applies to parameters" (a
  warning).

  A definition's `-Wgcc-compat` warning comes before Sema's
  diagnostics of the same attribute.

  The GPU kernels' attributes change nothing here but have their handlers'
  checks, their arguments evaluated where the attributes are read (a tag's
  subjects are reached from the constant evaluator): `reqd_work_group_size`'s
  and `work_group_size_hint`'s three sizes ("requires parameter N to be an
  integer constant"; a negative one's "requires a non-negative integral
  compile time constant expression", which goes on; "must be greater than 0"
  where some but not all are 0), `intel_reqd_sub_group_size`'s,
  `amdgpu_flat_work_group_size`'s and `amdgpu_waves_per_eu`'s ("argument is
  invalid: max must be 0 since min is 0", "min must not be greater than
  max"), and `amdgpu_num_sgpr`'s and `amdgpu_num_vgpr`'s, with their
  `-Wdeprecated-declarations` warning. An argument wider than 32 bits at its
  type's width is clang's "integer constant expression evaluates to value V
  that cannot be represented in a 32-bit unsigned integer type"
  (`check_input_constant_u32`). On a function no kernel's convention makes one,
  the first of the OpenCL group is "attribute 'X' can only be applied to an
  OpenCL kernel function" and then the first of AMDGPU's "'X' attribute only
  applies to kernel functions", at the function's name and after the
  attributes' own diagnostics (`check_input_attribute_kernels`). An error clang
  places at an argument's expression is at its `Expr::getExprLoc`
  (`check_input_expression_location`, from the parse tree's root of it): a
  binary operator's at the operator, a member access at the member's name, a
  postfix operator's at it, a subscript's at its base's, any other at its
  first token. An attribute's error drops the attribute, and the checker goes on to
  the declaration's other attributes and the next declarations, as clang does
  ("Semantic error recovery", #1447).

  `selectany` is accepted on external data. Its definitions have weak binding
  and publish the existing `LinkGroups` representation: one COMDAT group per
  selected variable, including its relocation sections. Explicit section names
  remain separate when their groups differ; zero-fill, const and thread-local
  definitions retain their own section kinds. With several source units the
  first group's atoms are kept in command-line order before symbol resolution,
  so references belonging only to discarded groups disappear. Ordinary strong
  definitions still take precedence over weak definitions. `-c` objects and
  direct links share this producer; no additional object-writer format is used.
  An accepted `alias("name"), selectany` declaration has weak binding at its
  base's section, value and size. It creates no group of its own, including when
  its base belongs to a selectany group.

  `no_caller_saved_registers` is a calling convention's attribute here
  (`CHECK_CALL_NO_CALLER_SAVED`, [the calling-convention guide](calling-convention.md)):
  clang's function type bit, a System V function's type whose callee keeps
  every register. Its count is checked before its subject, as clang's type
  processing does. On a tag it is nothing; elsewhere it gets the function
  types' warning. With `sysv_abi` or `cdecl` it is the same type. Beside
  another convention it is clang's bit beside that one (#1447,
  `CHECK_CALL_KEEPS_REGISTERS`, `TYPE_FUNCTION_ABI_KEEPS_REGISTERS`): printed
  after the convention (`'void (void) __attribute__((ms_abi))
  __attribute__((no_caller_saved_registers))'`), part of the type's identity,
  inherited by a redeclaration that names the convention alone, and clang's
  "function declared with 'no_caller_saved_registers' attribute was previously
  declared without" where a redeclaration adds it. Where it stands between two
  conventions of their own (`ms_abi, no_caller_saved_registers, sysv_abi`),
  clang 23 gives no conflict error, which the checker gives as without it.

  x86's `interrupt` has its own rule (`CHECK_ATTRIBUTE_INTERRUPT`), clang's
  `handleAnyX86InterruptAttr` on x86-64. Its subjects are functions and
  pointers to functions with prototypes (a variable's, a member's, a
  parameter's, a typedef's: "only applies to non-K&R-style functions"
  elsewhere, which a K&R declaration is and a K&R definition isn't). Its
  arguments are expressions to clang's parser (an undeclared name, a missing
  operand: their errors drop it) and nothing to its handler, whatever their
  count. Then, the first rule broken being the declaration's error
  (`check_input_interrupt_handler`), which drops the attribute and leaves the
  declaration valid, as clang's handler returns (#1447: its later attributes'
  errors, `_Alignas`' among them, follow it in their order, and each
  declarator and later declaration is checked): a result that isn't void, at its type's
  start (`check_input_interrupt_result`: past storage classes, qualifiers and
  attributes; `_Complex`'s type's, `_Atomic(T)`'s T's); a count of parameters
  but one or two (a variadic one's named ones), at the declaration's start; a
  first parameter that isn't a pointer (adjusted, an array's or a function's
  is one), and a second that isn't an unsigned integer of 64 bits (`unsigned
  long`, `unsigned long long`, `unsigned _BitInt(64)`, an enumeration of one,
  qualified but not atomic), at its declaration's start (a K&R definition's
  declaration of it). A function declared by a typedef's name has its
  parameters at its name; the other errors clang places nowhere have no
  place here either (an invalid SourceLocation: a pointer's, a typedef's, a
  member's, a parameter's, and the result of a function declared by a
  typedef's name or returning a pointer to an array or a function,
  `pp_diagnose_unlocated_notes`). It keeps the type as it is and adds
  clang's implicit `used`: a static handler is emitted and never
  -Wunused-function's, and a `used` after the definition is none new. A
  redeclaration inherits it; one after the definition drops it with "attribute
  declaration must precede definition" and its implicit `used`'s again at no
  place, unless the definition has a `used`. After checking, in a unit that
  read `interrupt` or `no_caller_saved_registers`
  (`CheckInput.interrupt_calls`), the calls are looked through
  (`check_input_interrupt_calls`): a direct call of a function an
  interrupt attribute marks, at or before the declaration its name finds
  (along the declarations each redeclaration's lookup found, which clang
  merges it with: one in a closed or hidden block isn't a later file-scope
  declaration's that has an earlier visible one, #1447,
  `CheckInput.redeclaration_links`)
  (through parentheses, `_Generic`, `__builtin_choose_expr` and one
  address-of, as `ActOnCallExpr` finds it; in `sizeof` and `_Generic`'s
  operands too), is clang's "interrupt service routine cannot be called
  directly" at the callee (Expr::getExprLoc's place: a parenthesized one's
  `(`, a member's name, a subscript's base's). That error is the call's alone:
  the unit's later checks run as a valid unit's do (#1447,
  `CheckInput.recovered_valid`), and no other diagnostic of its full
  expression is published, as clang's invalid call leaves none
  (`CheckInput.interrupt_ranges`). In the body of a function
  either marks, any other call whose callee no `no_caller_saved_registers`
  marks (the declaration's attribute, which a typedef's function type
  doesn't give) is -Wexcessive-regsave's warning, interrupt's message where
  both mark it, with "'g' declared here" at the declaration the callee's name
  finds (a builtin's at its name's first use in the unit, where clang
  declares it) and no note through a pointer; a call around a direct call's
  error, which clang doesn't build, has none. Builtins are calls but those
  clang parses as keywords' forms (`va_arg`, `convertvector`, `offsetof`,
  `types_compatible_p`, `choose_expr`). The calls are the graph's in its
  postorder, clang's order: a call's callee's and arguments' calls first;
  then those the reader's evaluator alone reads, a `typeof`'s and a constant
  array bound's, whose `)` no graph node has (`check_interrupt_evaluated_calls`,
  #1447): a declared name's callee, an undeclared function's (its implicit
  declaration's), a builtin's (a called name no declaration binds, noted at
  its first use), and one through a member or a postfix expression's base
  (`typeof(s.f())`, `typeof(t[0]())`), with no note. An
  interrupt service routine's definition is Tier 0's handler frame
  (`Tier0Function.interrupt`, `docs/tier0.md`): `CheckProgram.object_interrupt`
  holds each function's first declaration with the attribute,
  `check_program_interrupt(program, b)` says whether body `b` is a routine's
  (that declaration at or before its definition), and the lowering gives it
  `no_caller_saved_registers`' set of every register (`check_lower_preserve`,
  LLVM's `CSR_64_AllRegs` of `x86_intrcc`) and its 1 or 2 parameters' kind.

  x86's `force_align_arg_pointer` (`CHECK_ATTRIBUTE_FORCE_ALIGN_ARG_POINTER`,
  #1447) is clang's `handleForceAlignArgPointerAttr`'s: a function's, with
  no arguments ("takes no arguments" otherwise). It ignores the attribute
  without a word on a function pointer (a variable's, a parameter's, a
  member's) and on a typedef of a function's type or of a pointer to one,
  and with "only applies to functions" on anything else
  (`check_input_subject_handlers`' row). A redeclaration inherits it; one
  after the definition drops it with "attribute declaration must precede
  definition". `CheckProgram.object_realign` holds each function's first
  declaration with it, and `check_program_realign(program, b)` says whether
  body `b` realigns its stack (`Tier0Function.realign_stack`,
  `docs/tier0.md`'s realigned frames: 16 bytes' at least).
  A variadic routine's list starts at the interrupted code's argument
  registers, as LLVM's `x86_intrcc` has it (#1447, `docs/tier0.md`).
  `__builtin_eh_return`, `__builtin_setjmp` and musttail calls in one are
  refused by name, as in any preserving frame.

  GCC's and clang's `patchable_function_entry(N[, M[, "section"]])`
  (`CHECK_ATTRIBUTE_PATCHABLE_FUNCTION_ENTRY`, #1447) is clang's
  `handlePatchableFunctionEntryAttr`'s: a function's ("only applies to
  functions and Objective-C methods" elsewhere, the generated row's
  warning, before anything else), of one to three arguments (the generic
  count errors), its third a string literal (the parser's "expected string
  literal as argument of 'X' attribute"). Its count and offset are read
  where its attributes are, each in 32 bits as `checkUInt32Argument` reads
  them (`check_input_constant_u32`: `CheckInputAttribute.constants`, `wide`
  and `argument_values`), and its section's string (`text`); its handler's
  errors come once its subject takes it (`check_input_patchable_entry`), in
  clang's order, each dropping the declaration: no integer constant, "'X'
  attribute requires parameter N to be an integer constant" (N from 0, as
  clang numbers them here); more than 32 bits, "integer constant expression
  evaluates to value V that cannot be represented in a 32-bit unsigned
  integer type" at the argument; negative, "'X' attribute requires a
  non-negative integral compile time constant expression"; an offset past
  the count, "'X' attribute requires integer constant between 0 and N
  inclusive" (`CHECK_ARGUMENT_BETWEEN`); and an empty section, "section
  argument to 'patchable_function_entry' attribute is not valid for this
  target: section must not be empty" at the string. A redeclaration without
  one inherits its last declaration's, and one after the definition is
  dropped with "attribute declaration must precede definition".
  `CheckProgram.patchable_keys` holds the function declarations with it,
  sorted by object, and `check_program_patchable(program, b)` gives body
  `b`'s NOPs as clang's code generation takes them: the first attribute of
  its function's last declaration with one up to its definition (a count of
  0 makes none), or `-fpatchable-function-entry=N[,M[,SECTION]]`'s
  (`predefines_patchable`, `CheckProgramOptions.patchable_count`), and its
  entry's section, the attribute's or else the option's. The emission's
  (`check_program_emit_patchable`, both tiers' code) is the [object writer
  guide](object-writer-and-linker.md)'s "Patchable function entries".

  The rest stay refused by name, where they apply: `format_matches` in either
  syntax. Recognizing their
  names does not supply their handlers.
- **Type arguments.** The attributes clang's `attributeIsTypeArgAttr` names
  (`preferred_type`, `vec_type_hint`, `iboutletcollection`, `preferred_name`,
  and C++'s `Owner`, `Pointer`, `contained_type` and
  `sycl_kernel_entry_point`, known or not in that syntax) take a type name,
  which the parser reads as one in a GNU list where a type starts the argument
  (`parse_scalar_type_argument_attribute`; the vector path leaves such a
  declaration to it), and a missing `)` is expected at the token; a C23 list's
  argument stays a run of tokens. `preferred_type`'s and `vec_type_hint`'s type
  is read where their attributes are (`CheckInputAttribute.argument_type`, a
  C23 list's lone typedef name by its binding) and checked past the subject as
  their handlers check it (`check_input_attribute_annotations`):
  `preferred_type` without one is "'preferred_type' attribute takes one
  argument", and its incomplete type (a function's is none) "incomplete type
  'T' where a complete type is required" at the type past its leading
  qualifiers, spelled as written, with a tag's "forward declaration of 'struct
  S'" note at its declaration; `vec_type_hint`'s type must be an extended
  vector's, a floating one (complex too) or an integer one (a complete
  enumeration's, `_BitInt`'s, not `bool`'s), else "a non-vector or
  non-vectorizable scalar type is an invalid argument to attribute 'X'" at the
  attribute, and a second one of another type is ignored with
  -Wignored-attributes' "attribute 'vec_type_hint' is already applied with
  different arguments". In a C23 list, a type argument that starts no type is
  "expected a type" alone, and a second argument "expected ')'" without its
  note, as clang's balanced reading of it gives them.
- **Argument counts.** An attribute's count of arguments other than clang's
  is clang's error, worded as clang's messages: "'X' attribute takes no
  arguments", "takes one argument", "requires exactly N arguments", "takes at
  least N arguments", "takes no more than N arguments". Declaration attributes
  are counted after their subject and their arguments' parser errors: a
  required string slot, an undeclared name, a malformed type, an incomplete
  `sizeof` operand, a call or a builtin (`check_input_attribute_parsed`,
  `check_input_attribute_expressions`). The shared expression evaluator retains
  the actual operation, types and callee so those errors keep their locations
  and notes. A trailing empty argument is an expression error, while an empty
  required string slot is the string's error first (`section(,)`). The type
  attributes `noreturn`, `packed`, `may_alias`, `transparent_union` and
  nullability, and `sentinel`, keep their count-first checks.

  The scalar parser uses those same generated string-slot facts when a
  malformed expression would otherwise hide an earlier string error, as in
  `section(1,)`. A stray name immediately before the closing parenthesis of a
  `sizeof` or `alignof` type inside a GNU attribute is retained as a parser
  error with its opener note. The diagnostic-only recovery view drops that
  name, keeping the original complete type and closing parenthesis, so the
  outer attribute can still diagnose its count. This view cannot accept or
  emit the source. A delayed count diagnostic takes its name from its source
  report and publishes after the arguments, independently of its sort key.

  Custom grammars keep their handlers' rules: `cpu_specific` and
  `cpu_dispatch` require at least one identifier, `no_sanitize` at least one
  string, and `target_clones` requires a default target. Their zero-argument
  forms reach those checks after the subject instead of a generic refusal.
- **Arguments' errors** (`CHECK_INPUT_ATTRIBUTE_ARGUMENT`, with
  c_check_attribute.c's `CheckAttributeArgumentError` in its detail). An
  argument a handler refuses is clang's error, in clang's words and at its
  place, where it used to be the generic "invalid or unsupported
  declaration":
  - An argument that is no integer constant: "'aligned' attribute requires
    integer constant" (aligned's and align_value's, at the argument), "'X'
    attribute requires an integer constant" (regparm, constructor,
    destructor, assume_aligned's alignment), "'X' attribute requires
    parameter N to be an integer constant" (the parameter indices', format's,
    assume_aligned's offset) and "ext_vector_type attribute requires an
    integer constant", at the attribute; a name in it that names nothing is
    clang's parser's "use of undeclared identifier"
    (`check_input_attribute_constant_error`, from both evaluators: the
    reader's and the constant path's).
  - Values: "requested alignment is not a power of 2" (aligned's, 0
    included, align_value's and assume_aligned's), "zero vector size",
    "'regparm' parameter must be between 0 and 6 inclusive", "'X' attribute
    parameter N is out of bounds" (alloc_align's and format_arg's index,
    format's indices as clang's `checkUInt32Argument` reads a negative int),
    "'sentinel' parameter 1 less than zero" and "'sentinel' parameter 2 not 0
    or 1". assume_aligned's offset is any integer, as clang keeps it.
  - Strings and identifiers: clang's parser reads a string argument as an
    unevaluated string literal, so one that is none is "expected string
    literal as argument of 'X' attribute" at it, wherever the attribute is
    (before a C23 one's place is decided), and an escape sequence it refuses
    in one is its error, which drops the attribute ("Unevaluated string
    literals (#147)"); visibility's identifier is its
    handler's "'visibility' attribute requires a string"; "'mode' attribute
    requires an identifier" and format's "requires parameter 1 to be an
    identifier" at the attribute; tls_model's other string is its
    "tls_model must be ..." at the argument.
  - Parameters: alloc_size's and alloc_align's index of a parameter of no
    integer type is "'X' attribute argument may only refer to a function
    parameter of integer type" at the argument; format's and format_arg's of
    no pointer to a character type (char, signed or unsigned) "format argument
    not a string type", format_arg's function's result "function does not
    return string type", and a strftime format's third argument other than 0
    "strftime format attribute requires 3rd parameter to be 0".
  - `mode` is clang's `AddModeAttr` (`check_input_attribute_mode`): the name
    without a keyword's underscores; a vector mode (V, a power of two, a
    mode) with -Wdeprecated-attributes' warning, for each declarator; the
    width's integer type of the base's signedness, or its floating type
    (HF `__fp16`, XF `long double`, TF and KF `__float128`, IF none), complex
    for a C mode, a vector of it by the mode's count or the base vector's
    bytes; the declaration's qualifiers dropped, as clang's `setType` drops
    them; `word`, `byte`, `pointer` and `unwind_word`. Its errors, in clang's
    order: "unknown machine mode 'M'", "mode 'M' is not supported for
    enumeration types", "mode attribute only supported for integer and
    floating-point types", "type of machine mode does not match type of base
    type", "unsupported machine mode 'M'" and "type of machine mode does not
    support base vector types". A wrong base does not suppress the separate
    unsupported-mode error when both apply.
  - `mode` on an enum gives it the mode's integer type, signed (an enum of no
    integer type yet takes `int`'s, as clang's `AddModeAttr` defaults it).
    Before its body, as clang's `EnumDecl::setIntegerType` makes it, the enum
    is complete from that type and each explicit value must be representable
    in it, its enumerators otherwise a plain enum's
    (`BUSTER_CHECK_ENUM_MODED`); after its body, clang's `ActOnEnumBody`
    takes it for the enum's type. A value no fixed or moded type represents
    is clang's "enumerator value is not representable in the underlying type
    'T'" (`CHECK_INPUT_ENUM_NOT_REPRESENTABLE`), the base spelled as written
    ("'T' (aka 'signed char')"), and a fixed type's value after its greatest
    names the value. The error is recovered, as clang's `CheckEnumConstant`
    goes on: the value is converted (or wrapped) to the type, and a name clash
    at the same enumerator is still reported after it, with its note (#1807).

  And the handlers' warnings, each of which ignores its attribute
  (-Wignored-attributes): "'X' attribute argument not supported: Y" (a
  visibility clang doesn't know, at the string; a format kind it doesn't
  know, at the attribute: its ignored `gcc_diag` kinds are silent), "'X'
  attribute only applies to return values that are pointers" (alloc_size)
  and "... pointers or references" (alloc_align, and assume_aligned's before
  its arguments' errors), "'visibility' attribute
  ignored" on a typedef, target's strings clang's `checkTargetAttr` doesn't
  take ("unsupported 'F' in the 'target' attribute string; 'target'
  attribute ignored", its unknown CPU and tune CPU, a duplicate `arch=` or
  `tune=`, `fpmath=` and `branch-protection=`: `check_input_target_warning`;
  `apxf` and `no-apxf` are taken, as clang 23 takes them, for `-mapxf`'s
  features, #1919),
  and no_sanitize's "'no_sanitize' attribute argument 'S' not supported on a
  global variable" (but address, hwaddress and memtag, at the variable's
  name); with no_sanitize's "unknown sanitizer 'S' ignored"
  (-Wunknown-sanitizers) and format's "GCC requires a function with the
  'format' attribute to be variadic" (-Wgcc-compat) at the declaration's
  name. A cleanup's name that names nothing is clang's parser's error alone:
  the attribute is dropped, its subject unchecked. Target warnings name the
  decoded string fragment, including permitted escapes, rather than the
  source literal's escape spelling.
- **Subject before arguments** (`CheckInputAttribute.deferred`). The
  handlers clang runs once an attribute's subject takes it give their
  arguments' errors only then: tls_model's model, constructor's, destructor's,
  assume_aligned's, alloc_size's and alloc_align's integers, and format's kind
  and indices. The reader evaluates them where it reads them
  (a tag's subjects are reached from the constant evaluator) and moves the
  error into the attribute (`check_input_attribute_defer`), which
  `check_input_attribute_subjects` gives after the subject, the exclusions and
  the count; a use of an undeclared name, clang's parser's error, stays. An
  unsupported format kind or a non-pointer allocation result warns and ignores
  the attribute before any constant-value error. A malformed format kind is
  likewise checked only after its subject: `format(1, 2, 3)` on a variable
  has only the subject's warning. The
  type attributes clang leaves to a declarator's type (regparm,
  ext_vector_type) are ignored on a tag's declaration without a word, their
  arguments unread (`CheckInputReader.tag_attributes`).
- **Declarations of no declarator** (`check_input_free_standing`). clang's
  `ParsedFreeStandingDeclSpec` applies none of the specifiers' attributes. Their
  GNU lists and alignment specifiers are read as its parser reads them alone
  (`check_input_attribute_ignored_lists`: an argument that must be a string
  literal, and a name in an expression that names nothing, are its errors), and
  on a tag's declaration each is warned of after the storage class's warnings:
  "attribute 'X' is ignored, place it after "struct" to apply attribute to type
  declaration" (named as written), "'_Alignas' attribute ignored". A bare tag
  declaration's attributes after its name are read the same way. C23 lists that
  begin such a declaration are clang's parser's error, "misplaced attributes;
  expected attributes here" just after a file-scope tag declaration's keyword,
  else "an attribute list cannot appear here", with "declaration does not
  declare anything" at the specifiers after them. Specifiers without a type
  specifier (`const;`, `static;`, `_Alignas(8);`, a calling convention's
  keyword, `_Nonnull`) are int's, in any language, without a word, and
  declare nothing; a list whose specifiers are all such keywords and GNU
  attributes is a declaration, not a null statement's attributes. Its errors
  come first, in clang's order whatever their places', then its warnings:
  "restrict requires a pointer or reference" at its first restrict, whatever
  the type, "'inline' can only appear on functions", then "'_Noreturn' can
  only appear on functions".
- **Declarations after a missing `;`** (`check_prefix_resume`,
  `c_check_prefix.c`). Where a declaration's `;` is missing before a
  declaration specifier, clang's `ParseDeclGroup` goes on as if it were
  there, so `int x _Alignas(3);` declares `x`, and `_Alignas(3);` is a
  declaration that declares nothing (-Wmissing-declarations). The parser
  marks such an error (`ParseDiagnostic.resumed`, `docs/c-parser.md`), and a
  failed parse's checking takes a view of the unit with a synthetic `;`
  before each such specifier, parsed again: in rounds, each for the missing
  `;`s before the parse's first other error, as a body's after its first
  show up only once the body is parsed past it. The rounds go on until one
  finds none (a body with k of them takes k), within a budget of 256 times
  the unit's tokens parsed in all, past which the last view stands with its
  errors; the two views' columns take turns, so the memory is the unit's
  size whatever the rounds, and the errors of all rounds are merged in order
  by a stable merge sort (50,000 bodies of two each: 0.66 s, clang's
  output). A view without errors is
  checked whole, as a prefix is (no translation unit's end); one with errors
  takes the repair and prefix views of its own first error. The view's
  tokens are its own (the synthetic `;`s take the next token's place), and
  the parse's errors printed are each round's missing `;`s' and the last
  view's, at the original's tokens: so a body's second missing `;`, which the
  parser's recovery skips with the rest of the body, is clang's error too. A
  pragma that clang's parser reads as a token between the declarator and the
  specifier (`#pragma pack`, `weak`, `GCC visibility`: any but those its
  preprocessor takes alone, such as `message` and the diagnostic ones) stops
  the recovery, as it does clang's.
- **An array's size or a designator's index after a comma** (#1867). `int
  u[n, m];` and `{ [1, 2] = 3 }` are clang's "expected ']'" at the comma, then
  its parser skips to the `]` and goes on with `u[n]` and `[1]`: the view drops
  the tokens from the comma to the `]` (`ParseDiagnostic.resume_delete`), so the
  declaration's later uses and the initializer's other elements are checked as
  clang checks them, by both paths. Differences left: without a `]` before the
  next `;` (`int z[1, 2;`) nothing is resumed, where clang reads on.
- **A GNU attribute list's item that isn't a name** (#1447). After
  `__attribute__((unused, __clang__::x))`'s "expected ')'" (`__clang__` is
  the macro 1), clang's ParseGNUAttributes skips to the list's `)` and goes
  on with the declaration: the view drops the tokens from the error to that
  `)` (`ParseDiagnostic.resume_delete`), so the list's earlier attributes and
  the unit's later declarations are checked, by both paths. Differences left:
  the warnings a rejected unit drops still drop (`-Wdeprecated-declarations`
  among them), and the caret display's "expanded from macro '__clang__'" note.

  Malformed K&R parameter declaration tails also have bounded repairs. The
  repaired view discards the recorded tail while preserving the original
  parse error and any delimiter note. All these views serve diagnostics only;
  the original unit remains rejected.
- **Alignment specifiers** (`check_input_alignas_error`, C11 6.7.5). On a
  parameter, a `register` variable or a bit-field, "'_Alignas' attribute cannot
  be applied to a function parameter" (and the others); on a typedef or a
  function, "'_Alignas' attribute only applies to variables and fields"; an
  alignment below the declared type's, "requested alignment is less than
  minimum alignment of N for type 'T'", the greatest of all the declaration's
  alignments (GNU aligned's too) against its declarator's type, not its
  specifiers' (`check_input_alignas_underaligned`). Two declarators of one
  `_Alignas` are clang's error twice, here once: its argument's error is
  found once, where the specifiers are read ("Semantic error recovery",
  #1447).
- **Mutual exclusions** (`check_input_attribute_excluding`). After an
  attribute's subject check, one that an attribute already on the declaration
  excludes is clang's error "'cold' and 'hot' attributes are not compatible",
  with "conflicting attribute is here" at the other. The new attribute is
  named as written, the earlier one as clang names an applied attribute: a GNU
  one without its keyword's underscores, a C23 one with its scope. A
  redeclaration's attribute is checked against its entity's earlier
  declarations' (`check_input_attribute_merge_exclusions`) after the
  attributes it drops after the definition, as clang's `mergeDeclAttributes`
  does.
- **Block-scope redeclarations** (`check_input_declaration_visible`). Of an
  entity's earlier declarations, a redeclaration's attributes are checked
  against those clang's lookup finds: the file scope's and an enclosing
  block's, not another body's nor a closed block's (and every earlier one
  where none is visible, as clang's `findLocallyScopedExternCDecl` finds a
  closed block's extern). A block's attributes reach the definition's, which
  the attributes after it are checked against, only from a block's first
  declaration of its entity, which a later file-scope declaration that sees
  none links to and inherits from; the late masks take file scope's and
  those. Each block-scope declaration with linkage that has a
  visible previous one records its scope's end
  (`CheckInputReader.declaration_scope_ends`, allocated at the first); the
  others count as file scope's. Recording the scope does not depend on
  whether attribute evaluation has published the declaration's attributes
  yet, so delayed publication cannot make a closed block visible again.
- **Bodies on lanes** (`check_input_attribute_late`). A body read on a lane
  can't read its entity's other declarations' attributes, which other lanes'
  columns hold, nor publish its own to later ones. A redeclaration there that
  writes an attribute clang drops after a definition
  (`check_input_attribute_after_kind`) or one of a mutual exclusion sends the
  unit to source order ("a body's redeclaration has attributes its entity's
  other declarations decide"), as the late kinds' did already; one it inherits
  (naked) doesn't. Measured in a cloud container with `-c --deferred-bodies
  --lanes 4 --time-stages`: none of the 220 c-testsuite units it reports on
  (45 merged on lanes, 175 too small for them) gives this reason, nor does a
  unit of 19 glibc headers with their extern inline functions (merged). So
  source order costs nothing measured there, where publishing the attribute
  state across lanes would take a merge of its own.
- **GNU `noderef`** (`c_check_noderef_types.c`, `c_check_noderef_input.c`,
  `c_check_noderef_graph.c`). Source-only immutable shadows retain annotations
  through pointers, arrays, function results and parameters, typedefs, record
  members, `typeof` and inferred declarations. They never enter canonical type
  identity, layout or generated values. A sparse token column exists only in
  units spelling `noderef` or `__noderef__`; ordinary units take no expression
  pass or annotation allocation. Declarator and constant/type-name readers
  compose the same shadow shape. An attribute just inside declarator parentheses
  marks that chunk's type without adding a canonical layer. An invalid
  placement retains its type sugar and gives `-Wignored-attributes`, as Clang
  does; a trailing declaration
  spelling has no dereference effect. C23 `clang::noderef` remains ignored.
  The checked graph transfers the annotations through calls, casts, pointer
  arithmetic, assignments, comma and conditional expressions, then gives
  `-Wnoderef` for dereferences, subscripts and arrow accesses. A pointer or array
  identifier's warning names it and notes its declaration; an atomic operand
  uses the generic expression warning. Unary dereferences that select array
  values compute addresses without a warning; subscripts retain their annotation
  check. `sizeof`, `alignof`, `typeof`
  and generic controlling expressions suppress the warning. Taking an address
  cancels its lvalue's dereference, including array-member address computations,
  while loading an intermediate pointer member still warns. Implicit pointer
  conversions that remove the annotation warn; explicit casts do not. The
  check uses the original source and destination pointer types before array
  decay or atomic value conversion, and only fixed prototype parameters
  supply assignment conversions at calls. Conversion locations share the
  attribute argument reader's expression spelling, including parentheses.
  This lookup follows the first token's parent chain once, retaining a
  subscript base's location without repeatedly scanning nested prefixes.
  Checked subscripts also consult the existing operand type and value category:
  an implicit lvalue or array conversion reports the base's beginning, as does
  the value conversion of a materialized record's member (`get().pointer`).
  A value such as `p++` keeps its own operator location. The pointer operand
  supplies that location in reversed subscripts such as `0[p]` too.
  Initializer conversion diagnostics follow the actual selected member or
  array element, including nested designators, union alternatives, atomic
  pointer elements and GNU ranges. The graph pass retains one bit per value
  for these consumers; its temporary expression shadows remain disposable.
  The initializer's warning sink keeps one conversion warning per selected
  range element, while dropping scalar initializers already diagnosed by the
  graph pass. Conversion warnings also apply in unevaluated operands;
  dereference warnings keep their query suppression.
  Function redeclarations preserve the previous composite function type's
  sugar, while an object's most recent declaration and a definition's own
  parameters keep their own sugar. Parameter adjustment preserves atomic
  wrappers; K&R declarations retain the adjusted annotations and the typed
  parameter's location for declaration notes. Call arguments and their sparse
  parameter annotations advance in source order together. A body's annotated
  record members request source-order replay before publishing member indexes;
  other body shadows remain immutable in the deferred reader's retained arenas.
- **C23 type placement.** A list after the declaration specifiers applies to
  their base type before the declarator is built. Thus
  `void [[gnu::ms_abi]] f(void);` warns "'ms_abi' only applies to function
  types; type here is 'void'" and ignores the attribute. A pointer's `_Atomic`
  qualifier is consumed before checking a following list, so
  `int * _Atomic [[gnu::unused]] p` gives "an attribute list cannot appear
  here".
- **Differences left.**
  - Arbitrary semantic-error continuation remains unsupported: after a
    declaration fails, later semantic diagnostics can be missing. The bounded
    missing-semicolon and malformed K&R-tail repairs above preserve their
    original parse errors and notes; they do not make every parser or semantic
    failure resumable (`docs/c-parser.md`).
  - A calling convention's redeclaration and assignment messages, the same for
    `no_caller_saved_registers`:
    - a function declared with an explicit convention other than its earlier
      declaration's is clang's "function declared 'A' here was previously
      declared 'B'" (or "without calling convention", where its first
      declaration has none explicit), with its "previous declaration is here"
      note at the first declaration (#1447, `check_input_report_redeclaration`).
      A redeclaration that adds `no_caller_saved_registers` in its own
      attribute is clang's "function declared with 'no_caller_saved_registers'
      attribute was previously declared without the 'no_caller_saved_registers'
      attribute" (`'gnu::no_caller_saved_registers'` for a C23 one), before a
      type conflict, noted at the declaration its lookup found. One whose
      typedef carries the attribute, which crashes clang 23, keeps "conflicting
      types for 'f'";
    - a declaration's convention sugar (below, "Type sugar") prints in a failed
      conversion's source and target alone. Every other diagnostic prints the
      type canonically, as before: a function designator's `preserve_*`
      before it, and a convention's macro by the unit. A declaration keeps no
      convention sugar in three cases, and its type prints the old way:
      - it writes a typedef too (`I (*p)(void) PM` is clang's
        `'PM I ((*))(void) __attribute__((preserve_most))'`);
      - it redeclares without the attribute (clang's merged type keeps the
        first declaration's sugar);
      - it is a parameter's declarator;
    - outside that sugar, a convention's macro name, as an address space's, is
      per unit: a convention the unit writes both ways, with two macros, or
      anywhere through a keyword (`__regcall`), is spelled out.
  - x86's `interrupt` (#147):
    - of the calls in operands the reader's evaluator alone reads (a
      `typeof`'s, a constant array bound's), a callee through anything but a
      member or a postfix expression's base (`typeof((*fp)(0))`, which a
      declarator there looks like) is no call to it, so neither
      -Wexcessive-regsave's warning;
    - a direct call's error drops every other diagnostic of its full
      expression (`CheckInput.interrupt_ranges`: its declarator's initializer
      or its statement, but the call's other arguments and list elements),
      where clang keeps those of the other operands that don't hold the call,
      built before or after it (`g(1) + (f(0), 1)`'s -Wexcessive-regsave of
      `g(1)`, `x = (f(0), 1), x = 1 / 0`'s division by zero, `(f(0), (1, 2))`'s
      inner comma's -Wunused-value).
  - After a missing `;`'s recovery, an error the checker recovers from
    (`int x _Alignas(3) int y _Alignas(4);`'s "requested alignment is not a
    power of 2") goes on to the later declarations, as clang does ("Semantic
    error recovery", #1447). Another checker error still ends what is
    published; a unit's later parse errors are printed.
  - The checks-only attributes (#1447): a C23 attribute in a cast in a
    file-scope `_Generic`'s controlling expression (`objc_gc`'s,
    `address_space`'s) is "invalid or unsupported declaration". A `_Generic`
    association's type in a body is read as a cast's
    (`check_input_selection_type_name`), with its attributes' warnings; one
    the constant reader reads (a file-scope initializer's) gives none. A
    `typeof`'s or `sizeof`'s type name that starts with a GNU attribute is
    read as one (`check_constant_type_start`), with its attributes' type
    warnings ("'aligned' attribute ignored when parsing type"). As everywhere, a
    declaration's first error ends the unit: a shared specifiers'
    `objc_ownership` with a count's error warns and errs for the first
    declarator alone, and a TCB function's call clang declares implicitly
    with an error (C99 and later) has no `-Wtcb-enforcement` warning after
    it.
    `diagnose_as_builtin`'s calls have no `-Wfortify-source` warnings, as no
    call has.
  - A bare tag declaration's C23 lists after its name (#1447,
    `check_input_tag_attributes`): a struct's or a union's, right after the
    name, are clang's parser's "an attribute list cannot appear here" at the
    first, then its Sema's of each attribute as the tag's own list's
    (`CheckInputReader.tag_name_list`): a type attribute's count error or
    "cannot be applied to a declaration", an unknown one's warning. The
    declaration goes on, its errors recovered. An enum's, or lists after a
    GNU list, are ignored with `-Wignored-attributes`' "attribute
    'clang::neon_vector_type' is ignored, place it after "enum" ...", its
    scope named. Not yet clang's: the tag doesn't take such a list's other
    attributes (a later use of `struct V [[deprecated]];`'s tag isn't
    deprecated); an enum's list's, or a list's after a GNU list, declaration
    attributes (`deprecated`) are clang's "cannot be applied to types" error,
    not the warning; and a later warning of the unit's file scope
    (`-Wdivision-by-zero`) is dropped after a constant that reads such a tag
    (`_Alignof(struct V)`).
  - A struct's or a union's definition's C23 lists between its name and its
    body (#2268, `check_input_tag`): the parser reads on past them
    (`parse_scalar_tag`, whose tree the vector path's rules build too), and
    the checker gives clang's parser's "an attribute list cannot appear here"
    at the first, recovered, then reads them on as the tag's own, with the
    ones before its name (`CheckMisplacedCXX11Attribute`): their Sema's
    errors, as a bare declaration's, and the layout takes them
    (`[[gnu::packed]]`, `[[gnu::aligned(16)]]`). An enum's list there, which
    clang applies to the type with "cannot be applied to types" before the
    parser's "expected identifier or '('" at its `{`, is still the parser's
    error alone.
- **Tests.** `test-checker` checks the facts table's order and bounds, its
  counts and five rows, and conflicting conventions on a function and on
  another type (`c_check_attribute_test.c`). `test-checker-driver` compares
  the whole output with clang's on both paths
  (`check_test_driver_unknown_attributes`' statements) for:
  - GCC's positions on definitions;
  - nonnull's warnings, and its out-of-bounds error;
  - C23 lists in each place, and lists before suffixes;
  - the conventions x86-64 hasn't, and each kind of conflict;
  - tag declarations' attributes, with the layouts they give, checked by
    `_Static_assert`;
  - attributes in type names, `vector_size`'s errors, and
    `-Wgnu-alignof-expression`;
  - attributes after a definition, with the alignment they no longer give;
  - attributes without a rule of each kind, argument counts, and mutual
    exclusions on one declaration, through a specifier's list and across
    redeclarations;
  - deprecated and unavailable uses of each kind, and their context rules:
    members' in initializers and where the evaluator alone reads them,
    overloads' calls, members' own marks, anonymous tags', and typedefs that
    name marked tags;
  - an attribute's count after its subject and its arguments' parser
    errors, with or without arguments, and a declarator's type's convention
    warnings before the declaration's attributes'.
  - declarations after a missing `;` and declarations of no declarator
    without a type specifier, with a function specifier or with restrict,
    under gnu17 and gnu23 and the warnings' options
    (`check_test_driver_resumed_declarations`,
    `c_check_resume_driver_test.c`).
  - bodies' redeclarations with attributes after a definition and of mutual
    exclusions, against the declarations each sees, by both paths and with the
    bodies on 1, 2 and 4 lanes (`check_test_driver_lane_attributes`,
    `c_check_lane_attribute_driver_test.c`).
  - x86's `interrupt` (`check_test_driver_interrupts`,
    `c_check_interrupt_driver_test.c`), with the lines of no place too, by
    both paths and deferred bodies on two lanes: each of its handler's errors
    at its place or none, its subjects, arguments and C23 places,
    redeclarations and its implicit `used`, K&R declarations and definitions,
    direct calls through each callee's form and in unevaluated operands, and
    -Wexcessive-regsave in both callers' kinds, with ncsr and other callees,
    builtins' notes, nested calls' order, its options and pragmas; then `-c`
    of definitions, refused by name.

  `test-checker` checks the exclusions table's order and pairs, and that the
  facts tell C's attributes from another language's. The binding tests retain
  a block declaration's effective scope before its attributes are published.
  `c_check_attribute_arguments_driver_test.c` compares complete diagnostic
  streams for malformed expressions, custom empty lists, annotate constants,
  deferred format/allocation checks, secondary mode errors and escaped target
  strings, and the escape sequences of unevaluated string literals in every
  context, mode and warning option ("Unevaluated string literals (#147)",
  `check_test_driver_unevaluated_strings`).
  `c_check_attribute_effects_driver_test.c` covers complete
  thread-safety expressions with their warning option, loader diagnostics and
  loader section bytes and flags, through the scalar and vector paths.
  `c_check_attribute_checks_driver_test.c` compares the whole output of the
  checks-only attributes (#1447) in their valid uses, wrong subjects and wrong
  arguments, TCB calls, availability's merges and the Objective-C pointer
  attributes' types, with the warnings' options and GNU C89's implicit calls;
  `diagnose_as_builtin`'s builtins, counts, indices and types; and the NEON
  vectors' elements, sizes, places, compatibility with GNU vectors, mode and
  spellings. `test-checker`'s program fixtures run NEON vectors' code
  (`neon-vectors`).

## diagnose_if and enable_if (#1447)

clang's `diagnose_if(condition, message, "warning" | "error")` and
`enable_if(condition, message)` hold on any function declaration
(`c_check_condition.c`), not only on #1361's fortify wrappers.

- **Arguments.** Their counts are clang's errors first ("takes at least 3
  arguments", "takes no more than 4 arguments", "requires exactly 2
  arguments"), as their names would otherwise be read without the
  parameters. A message that is no string is "expected string literal"; a
  third argument other than `"error"` or `"warning"` is "invalid diagnostic
  type for 'diagnose_if'; use "error" or "warning" instead"; a fourth,
  clang's warning group, is "unknown warning group 'G'" where clang knows no
  such group (`pp_warning_group_index`). A group clang knows makes the
  diagnostic clang's custom one of that group (below, "Warning groups").
- **The condition.** It is read once, where its function is declared, in the
  function's parameter scope: its names are looked up there
  (`check_input_thread_name`; an undeclared one is clang's error), and the
  declaration evaluator checks it. One that names no parameter is a constant
  then, false or true for every use, or clang's "'X' attribute expression
  never produces a constant expression" at the attribute, its note
  "subexpression not valid in a constant expression" at the variable it
  reads. In one that names a parameter that error is clang's too where the
  variable's read can't be skipped; a read in a `&&`'s or `||`'s right
  operand or a conditional's second or third (`x > 0 && g > 0`) can be, as
  clang asks whether some arguments give a constant, and it is an unknown
  value that a call's evaluation needs only where it reads it. A `volatile`
  parameter's read is never a constant either (#2383): it is that error, its
  note at the parameter's name, where it can't be skipped, and an unknown
  value where it can (`x && a`). A condition's error drops its attribute, as
  clang drops it, and the reader goes on (phase 1 of semantic error recovery,
  below), so a later declaration's and a later call's errors are given too.
  An `enable_if` whose message is empty notes "candidate disabled: <no
  message provided>". A cast clang's constant evaluation refuses is that
  error too where no arguments skip it, whether the condition names a
  parameter or not (#2253, `check_condition_cast`): a cast to void, its note
  "non-literal type 'void' cannot be used in a constant expression", and one
  from an integer to a pointer (but a null pointer constant's) or from a
  pointer to an integer (but bool), "this conversion is not allowed in a
  constant expression", at the cast's `(`; a call's evaluation discards a
  comma's left operand cast to void. A condition of type void is "value of
  type 'void' is not contextually convertible to 'bool'". One that names a parameter becomes a program
  (`CheckConditionStep`), from the parse tree in postorder: each largest
  subexpression without a parameter, and each sizeof's or alignof's, is a
  constant the evaluator gives; a parameter is its place and type; the
  others are casts, unary, binary, logical, comma and conditional operators,
  which each call runs as the checker's own actions (`check_action_scalar`).
  A call of a declared function, which no constant evaluation gets past, is
  an unknown value where its arguments name a parameter (its arguments are
  still read), and an assignment or an increment one where a `&&`, a `||` or
  a conditional can skip it; otherwise each is "never produces a constant
  expression", its note at the call's name or the operator, as a call
  without a parameter is (`g(1) > 0`, `h()`). Any other shape (a subscript,
  a builtin's call) is "not yet supported" in user code; in a system header
  the condition is kept and decides nothing, as #1361's fortify wrappers had
  it. A function declaration with a `pass_object_size` parameter (#1361's
  object-size wrappers, `CheckInputAttributes.object_sizes`) in a system
  header doesn't read its conditions at all, whatever their shape: none is
  built, checked or kept (`check_input_conditions`), as glibc's fortify
  wrappers need. A user's reads them as any other function's (#2147): an
  undeclared name is clang's error, and the conditions decide at uses and
  calls, overloads and addresses; a shape the steps don't take (a
  `__builtin_object_size` of the parameter) is kept and decides nothing, as
  a system header's is, where clang's warns at a call that overflows. Still
  apart: clang disables an inherited `enable_if` that names a parameter at
  every call of a redeclaration (its condition's parameters aren't the
  redeclaration's), which this checker evaluates.
- **Lists.** Each function declaration where some condition holds has its
  list (`CheckInput.condition_declarations`, by the declaration's name
  token): its previous declaration's, then its own, as clang's
  redeclarations inherit attributes. A use reads the list of the declaration
  its lookup found. The conditions and lists are data
  (`c_check_condition_eval.c`, `CheckConditionTable`), which the graph keeps
  (`CheckGraph.conditions`) for overload resolution.
- **Uses.** A diagnose_if condition that names no parameter and holds is
  diagnosed at each use of the name, sizeof's, typeof's and `_Generic`'s
  operands included (clang's DiagnoseUseOfDecl): its message as a
  `-Wuser-defined-warnings` warning or an error, with the note "from
  'diagnose_if' attribute on 'f':" at the attribute. A use of a function an
  enable_if restricts (one whose condition isn't always true), other than a
  call's callee through parentheses and outside sizeof's, alignof's and
  typeof's operands, is "cannot take address of function 'f' because it has
  one or more non-tautological enable_if conditions", at the outermost `(`
  around the name. clang gives it where an operation converts the name to its
  address (`check_condition_conversion`: the use's parent past its
  parentheses, a call's `)` for an argument, an initializer's outermost `}`
  for an element), not for a sizeof's, alignof's or typeof's operand (but
  inside an operation there, `sizeof(q == r)`); and only where clang builds
  that operation, which an operand holding a failed conversion stops
  (`(q == s) + r`'s `r` has none). An operation that converts its operands in
  order stops at the first that fails (`c ? q : r`, `g(q, r)` and `q == r`
  give `q`'s alone); an initializer list converts each element on its own,
  once none holds a failed conversion (`check_condition_addresses`). The
  error's statement or declaration then has no later expression checks, from
  its start. `check_input_condition_uses` finds the uses once the
  unit's names are bound: the vector path gathers 16 tokens' declarations'
  marks at a time, the scalar path its twin.
- **Calls.** After checking, each call whose callee names a function with a
  list (`c_check_condition_graph.c`, in the warning graph's pass over calls)
  binds each argument whose checked value is a constant (an integer, a
  floating value, a null or absolute address), converted to its parameter's
  type, and runs the programs. An object's or a function's address (`&x`, an
  array or a function that decays, a string literal) is an address that is
  never null, as clang's evaluation knows: compared with a null pointer, or
  tested for truth, it decides; any other action on it is unknown. An
  argument that isn't a constant is an unknown value, which a `&&`, a `||` or
  a conditional that doesn't read it skips. An unknown result, and an action
  that faults (an overflow, a division by zero, a shift past the width),
  leave a condition unknown, as clang's EvaluateWithSubstitution does: a diagnose_if is then not given, an enable_if doesn't admit the
  call. The first enable_if that doesn't admit it, in the order clang checks
  them (the latest declaration's first, each declaration's in source order),
  is "no matching function for call to 'f'" at the callee, its note
  "candidate disabled: M" at the declaration the call found; otherwise the
  diagnose_if conditions that hold are given at the call's `)`.
- **Overloadable functions** (`c_check_overload.c`). An overloadable
  function's conditions take part in its overload resolution, as clang's do:
  - *Viability.* After a candidate's argument count and conversions, its
    enable_ifs are checked as at a call (`check_overload_enabled`), each
    parameter bound to its argument's checked value converted to the
    parameter's type. One that doesn't admit the call, or is unknown (an
    argument that isn't a constant), makes the candidate not viable.
  - *Ranking.* Between candidates whose conversions are equally good,
    clang's `compareEnableIfAttrs` decides
    (`check_condition_compare_enable`), before `pass_object_size`'s rule: the
    one whose enable_ifs are the other's, in declaration order, followed by
    more is better; ones that differ make neither better. Conditions are the
    same as clang's profile of the expression says
    (`check_condition_same`, kept as `CheckCondition.profile`): token for
    token, a literal by its value and type (`0x1` is `1`, a macro is what it
    expands to, `1L` isn't `1`), a parameter by its place and type, and
    parentheses count (`(1)` isn't `1`).
  - *Identity.* As clang's `IsOverload`, two overloadable declarations of
    one signature whose enable_ifs differ, or come in another order, are two
    functions (`check_overload_enables_same`, in the declaration chains and
    the index's compaction, `check_overload_index_previous`). A
    redeclaration's enable_ifs are therefore its own again: it inherits only
    its previous declaration's (the latest of the same signature and
    enable_ifs, `check_condition_overload_prior`) other conditions, its
    diagnose_ifs, which accumulate.
  - *diagnose_if.* At a call, the selected candidate's list is the one read
    (`check_input_condition_call` finds it in `CheckStream.overloads`): its
    diagnose_ifs that name no parameter at the callee's name, then those that
    hold with the arguments at the `)`. A use other than a call gives none,
    as clang's doesn't, and no "cannot take address" error.
  - *Addresses.* An overload set's address, resolved against a function
    pointer type, doesn't take a candidate with an enable_if that isn't
    always true (clang's "made ineligible by enable_if").
  - *Names.* An overloadable function's enable_ifs are in its Itanium name,
    as clang mangles them (`Ua9enable_ifI`, each condition's argument, `E`,
    after the name: `_Z1hUa9enable_ifIXgtfL0p_Li0EEEi`), which is what names
    two overloads of one signature apart. Each argument is made where the
    reader builds the condition (`c_check_condition_mangle.c`,
    `CheckCondition.mangled`), from the parse tree: a lone literal or
    enumerator is its `L<type><value>E`, anything else `X<expression>E` in
    prefix order, a parameter `fL0p<cv><n>_`, parentheses dropped, an
    operator's code before its operands, a cast to an arithmetic type
    `cv<type>`, an enumerator and a sizeof or an alignof folded to their
    values' literals, a float's, a double's and a long double's bits in
    hexadecimal. A function with an enable_if of another shape (a call, a
    subscript, a member, a cast to a pointer) isn't emitted: the mangler
    refuses it as it refuses other signatures it can't name.
  - *Errors.* A call no candidate is viable for is "no matching function for
    call to 'f'" at the callee, and one several are viable for "call to 'f'
    is ambiguous" (`check_semantic_overload_call`, which repeats the
    selection, `check_overload_explain`). Their notes are clang's, in its
    display order: "candidate disabled: M" (`<no message provided>` for an
    empty one), "candidate function not viable: requires 2 arguments, but 1
    was provided" or "requires single argument 'a', but no arguments were
    provided" (`at least` for a variadic one); and an ambiguous call's
    "candidate function" for each of clang's best (its closure from the
    first best of the candidates not worse). This also gives clang's
    errors for overload calls of the wrong argument count without
    conditions. A candidate that fails by an argument's conversion keeps the
    checker's own error.
- **Several at once.** Of the conditions that hold at one use or call, an
  error of no warning group is given alone, the first in source order;
  otherwise every warning, in source order, each with its note, and every
  diagnostic of a warning group, without one.
- **Warning groups.** A diagnose_if with a fourth argument is clang's custom
  diagnostic of that group (`CHECK_WARNING_EVENT_DIAGNOSE_IF`'s payload, its
  group's index in `pp_features_warning`): a warning, or for `"error"` an
  error by default, named `[-WG]`, without the note. Its options are the
  group's and those of every group that holds it in clang's tree
  (`pp_warning_group_holds`, over `pp_warning_group_parents`: clang 23.1.5's
  `diagtool tree`), each in turn (`check_warning_group_command_line`): `-W`
  a warning (so `-Wall` makes `"error"`'s of `format` a warning), `-Wno-`
  ignored, `-Werror=` an error, `-Wfatal-errors=` fatal, `-Wno-error=` an
  error a warning; then a source pragma of such a group
  (`check_diagnostic_group_severity_from`). clang keeps none of these as the
  user's: `-Weverything` enables an ignored one, `-Wno-everything` leaves it,
  and `-Werror` makes a warning an error through any `-Wno-error=`; `-w`
  ignores a warning, not an error. An error by default has no `-Werror,` in
  its option, however it became one.
- **Comma operators** (`check_input_attribute_unused`): a comma's left
  operand in a condition, or in any attribute's expression argument, or in a
  constant the reader evaluates alone (#2202: a declarator's constant array
  bound, a type name's bound, a bit-field's width, an enumerator's value), is
  clang's `CheckCommaOperands` warning, once however often the argument is
  read, where its value is unused as clang's `isUnusedResultAWarning` says:
  a nested comma's right operand's (quiet after a literal 0), a
  conditional's where both arms' are (at the else arm's), a logical
  operator's where both operands' are (at the operator), quiet for a call,
  an assignment, an increment and a cast to void, at a cast's `(`, sizeof's
  keyword, a member's name or the operator. It is -Wunused-value's "left
  operand of comma operator has no effect", its "should this cast be to
  'void'?" for a cast to `void *`, and -Wunused-comparison's for a
  comparison, with its `=` or `|=` note. The parser reads an attribute
  argument's commas as separators; one in a chain that a group's
  parentheses hold is the operator (`check_input_attribute_comma`). In those
  constants, a comma in an operand of `sizeof`, an alignof or a keyword is
  unevaluated and quiet (`check_input_bound_evaluated`), a comparison takes no
  note (its left operand is no lvalue in a constant), and in a function body
  the warning is published with the body's queued diagnostics at its end
  (`check_input_unused_completion`), as clang's `DiagIfReachable` defers it;
  at file scope it precedes the declarator's fold warning
  (`c_check_vla_fold_driver_test.c`).
- **Cost.** A unit without these attributes tests a reader flag where
  declarations are read and a count where uses and calls are; the steps are
  built only for these attributes.

An overload call's candidate that fails by an argument's conversion has
clang's note "candidate function not viable: no known conversion from 'A' to
'P' for Nth argument", at its first such argument, with clang's fix-it words
("; take the address of the argument with &", "; dereference the argument
with *"), these candidates first in display order (#2384,
`CheckOverloadExplained.argument`). An overload set's address that no
candidate matches, initialized or assigned, is clang's error with
'<overloaded function type>' as its source, "incompatible function pointer
types ..." where the enable_ifs leave one candidate, and a note at each
candidate, the latest first: "made ineligible by enable_if", "has different
number of parameters", "has type mismatch at Nth parameter", "has different
return type", or none (`check_driver_overload_address`).

What isn't clang's yet: an overload set's address returned or passed has
the conversion error of its latest candidate's type, where clang's is
"returning/passing '<overloaded function type>' ..."; an incompatible
pointer argument makes a candidate not viable here, where clang's C
overloading takes it with its warning (`h(ip)` of `h(long *)` and
`h(struct T)` picks `h(long *)` there);
a `volatile` parameter's read as a comma's left operand in a condition gets
`-Wunused-value`, which clang doesn't give a volatile read; the condition shapes
above; `-Wint-to-pointer-cast` and its relatives in a condition, which this
checker doesn't give anywhere yet
([#1943](https://github.com/buster14a/buster/issues/1943)); the comma warnings of a static assertion's
condition, of a type name's bound that a constant expression reads (`sizeof(int[(1, 2)])`
within a bound, an `_Alignof`, or a file-scope initializer's `sizeof`: their check reads
constants again, which would make the constant walk recursive), and a const builtin call's "ignoring return value"
as a left operand, which clang gives too; in a function body, a constant
bound's comma warnings come before the body's graph-walked ones rather than
among them in source order; and, as everywhere in this driver, the uses' errors stop the unit's
later expression checks, so a call after one such error isn't diagnosed. A
grouped
diagnose_if under `-Weverything` meets every other warning that option
enables, which this checker doesn't all give.

## Operand errors (#147)

- **Increments and decrements.** As clang's `CheckIncrementDecrementOperand`,
  the operand's type is tested before its modifiability
  (`check_action_steppable`). A real type (a complete enumeration's), a pointer
  or a complex type is stepped: a complex one's real part by one, as C2y's and
  clang's are (`check_lower_complex_update_emit`, a postfix one's value a copy
  of the object before it; a complex of aggregate parts, a `_Complex __float128`
  or a complex `_BitInt` past 128 bits, has its real part stepped by one of its
  element type, `check_lower_complex_parts_update_emit`, #1447; a `__float128`
  is stepped with `__addtf3` or `__subtf3`, `check_lower_quad_update_emit`). Any other is "cannot increment value of type
  T" (or "decrement"), an array's and a const record's too; a vector's, a
  record's and a function's are among them. T is the operand's type with its
  qualifiers, an `_Atomic` one's value type, spelled with the typedef its
  object was declared with (`check_semantic_operand_spelling`, through
  dereferences, subscripts and members).
- **Binary operators' operands** (#1872). An arithmetic, shift or bitwise
  operator, or its compound assignment, whose operands' types it doesn't take
  is clang's `err_typecheck_invalid_operands`: "invalid operands to binary
  expression ('int *' and 'double')", at the operator, both types as written
  (an array's and a function's undecayed, a qualified lvalue's qualifiers and
  a typedef's sugar kept, `check_semantic_operand_spelling`). Two pointers to
  incompatible types subtracted are "'int *' and 'float *' are not pointers to
  compatible types". A vector's operands keep the checker's message. What
  isn't clang's: a compound assignment to a const lvalue with such operands
  reports the const error, where clang reports the operands first.
- **Vector typedefs' a.k.a.** Clang's desugaring stops at a vector type's own
  typedef: a typedef of `v4si` prints `'w4si' (aka 'v4si')`, and `v4si` itself
  `'v4si' (vector of 4 'int' values)` (`check_driver_warning_type_at`).
- **A typeof's sugar** (#1837). A declaration whose base a `typeof(...)` spells
  prints it as clang's `TypePrinter` does: `'typeof(long) *' (aka 'long *')`,
  with a typedef name as written (`typeof(L)`), a nested typeof's own sugar,
  and `typeof (x)` for an expression that is one name
  (`check_input_typeof_spelling`, `check_driver_typeof_text`). Another
  expression operand prints as clang's StmtPrinter prints it, with expression
  spacing (`'typeof (x + 1) *'`, `'typeof ((L)x)'`, the static assertion's
  `check_semantic_requirement`), and a type name with its typedef names as
  written (`'typeof(L *)' (aka 'long *')`). A member's declarator spells its
  type too (below).
- **A failed static assertion** (#1447). Its error is clang's CheckStaticAssert's
  (`check_semantic_assert`): the requirement is the first term of the
  condition's `&&` conjunction that is no integer literal (nor C23's `true` or
  `false`) and is false, else the whole condition (findFailedBooleanCondition),
  at its first token; an integer literal's is the plain "static assertion
  failed" at the condition's first token, `(` included. The requirement prints
  as clang's StmtPrinter prints it: an integer literal's value in decimal with
  its type's suffix (`0x80000000` is `2147483648U`), a floating one's APFloat
  digits at its type's default precision with `.` where they have none and
  `F`/`L` (`0.1f` is `0.100000001F`, `2.0` is `2.`), a character one by its
  value (`'\0'` is `'\x00'`), `sizeof (x)` with a space before an expression
  operand, `__alignof` and `_Alignof`, C23's `(bool)`, a call's or an
  initializer list's `, ` between its items and a comma operator's `(3 , 4)`
  (#2382). A binary requirement but
  `||` adds "expression evaluates to 'L op R'" at its operator where either
  operand is worth printing (DiagnoseStaticAssertDetails' UsefulToPrintExpr: no
  literal, nor a unary operator's, nor a comparison, logical or comma
  operator), each value by its operand's own type (ConvertAPValueToString): a
  bool's `true`, a plain char's `'A' (0x41, 65)` with a control's `<U+001B>` and
  a byte past ASCII's `<0xC8>`, a floating one's APFloat digits. A read of
  a const object is its completed initializer's value (#2214,
  `check_semantic_operand_value`): `const int k = 0;`'s `k == 45` is
  `'0 == 45'`, a member's and an element's too. Still
  separate: a type name's literals print as written (`sizeof(int[0x10])`, clang
  `int[16]`, and a compound literal's `(int[]){1, 2}`, clang `(int[2])`);
  `_Generic`'s associations (`int:1`, clang `int: 1`); a record type defined
  in `__builtin_offsetof` (clang `struct (unnamed at f.c:1:2)`); a complex
  value's note.
- **A failed initializer conversion.** A scalar initializer's INITIALIZE is at
  its declarator, outside the initializer's tokens. When its conversion fails,
  that error is the only one: the initializers' walk doesn't check that
  initializer again (`check_initializers_diagnose_warnings`). A static
  object's still gets clang's second, non-constant error at the initializer
  after a conversion clang warns of and makes (`-Wint-conversion`'s and
  `-Wincompatible-pointer-types`'), where the converted value is no constant:
  an object's value read, or an address converted to an integer narrower
  than a pointer (`check_init_converted_nonconstant`); an absolute address
  (`(int *)5`) stays a constant.
- **A failed assignment conversion** (`check_driver_conversion_error`,
  `check_driver_conversion_words`), as clang's `DiagnoseAssignmentResult` words it:
  - its kind (`check_driver_conversion_kind`): an integer to a pointer and a
    pointer, array or function to an integer ("incompatible integer to pointer
    conversion", "incompatible pointer to integer conversion",
    `[-Wint-conversion]`), two pointers ("incompatible pointer types",
    `[-Wincompatible-pointer-types]`; two pointers to functions, "incompatible
    function pointer types"), or any other ("… incompatible type …", of no
    flag);
  - its action: "initializing T with an expression of type S" at the
    declarator, "assigning to T from S" at the `=`, "returning S from a function
    with result type T" at the returned expression (T spelled with the
    function's result typedef), "passing S to parameter of type T" at the
    argument with "passing argument to parameter 'x' here" (or "… parameter
    here") at the parameter's name, or where it has none where the
    declarator reader puts its missing name, for a pointer parameter
    (`check_driver_parameter_source`), and the end of its declarator for
    another (`check_driver_parameter_place`). Both read the declaration's
    tokens when a note is given; no prototype keeps them. A call's later
    arguments that don't convert either get their own errors, in order, as
    clang's ConvertArgumentsForCall goes on past an error-by-default
    conversion (incompatible pointers, an integer and a pointer) and stops at
    a hard one (incompatible types, another address space) (#2413,
    `check_graph_report_arguments`, which checks the call again with each failed
    argument taken as its parameter's type: only for a call already in error).
    Still apart: a later argument's `-Wpointer-sign` or qualifier warning comes
    before an earlier argument's error here, where clang gives them in argument
    order;
  - the types as written: an array's and a function's unconverted, a qualified
    lvalue's qualified, with their typedefs;
  - clang's fix-it words (`check_driver_conversion_hint`,
    `tryToFixConversion` with C's `compareTypesSimple`): "; dereference with *"
    where the pointee converts (not of a null pointer constant), "; remove &" of
    an `&x`, "; take the address with &" where an lvalue's address converts (not
    a pointer's to a void pointer), "; remove *" of a `*p`.

  An initializer's subobject is worded so from the initializers' walk
  (`CheckInitializer.failed_source`), at its value; a record's or an array's
  whole initializer of no braces whose value is no record of the type, at the
  declarator: the record's initialization from the value, or "array
  initializer must be an initializer list" (or "… or string literal", "… or
  wide string literal", by the element type). A static object's braced
  subobject whose converted value is then no constant gets clang's second
  error, "initializer element is not a compile-time constant", at it too
  (`check_init_value_nonconstant`).
- **Indirection through a pointer to void** (`-Wvoid-ptr-dereference`, clang's
  `CheckIndirectionOperand`): "ISO C does not allow indirection on operand of
  type 'void *'" at the `*`, of the operand's type as written (its typedef
  kept, through inner dereferences to the object they name; its top-level
  qualifiers dropped), in dead code too, and before an error the expression
  then has (`(*p)++`). Not right after a unary `&` (`&*p`; `&(*p)` warns, as
  clang's IsAfterAmp is the token's), nor where clang's parser is in an
  unevaluated context (`check_warning_graph_void_context`, a climb up the
  parse tree from the `*`, each token's context found once, only in a unit
  that has such a dereference): a sizeof's or alignof's operand, with its
  unselected `_Generic` associations and `__builtin_choose_expr` operands and
  its statement expressions, and a `_Generic`'s controlling expression. An
  array declarator's bound is evaluated again, in a sizeof's type name too
  (`sizeof(char (*)[(*p, n)])` warns). A sizeof whose operand is an
  expression of variable array type evaluates it after all (clang's
  `TransformToPotentiallyEvaluated`): it warns there, and twice in a bound
  inside it. Still quiet, as the graph doesn't have them: dereferences the
  constant evaluator reads alone (`__builtin_offsetof`'s designators,
  `__builtin_types_compatible_p`'s type names) and a `typeof`'s operand,
  which clang evaluates again when its type is variably modified.
- **Indirection through a null pointer** (`-Wnull-dereference`, #1447, clang's
  `CheckForNullPointerDereference`): "indirection of non-volatile null pointer
  will be deleted, not trap" with its note "consider using __builtin_trap() or
  qualifying pointer with 'volatile'", both at the `*`, of a `*` whose operand
  is a null pointer constant through casts (an integer constant 0 once the
  casts are passed: `*(int *)0`, `*(char *)(void *)0L`, `*(int *)(1 - 1)`; or,
  in C23, any expression of type `nullptr_t` once the casts and conversions are
  passed: `*(int *)nullptr` and a `nullptr_t` variable's), of a type neither
  volatile nor an array's or a function's
  (`check_common_null_dereference`), where an lvalue conversion or an
  assignment (a compound one's too) reads it: not under `&`, `.` or an
  increment, nor through a pointer variable, `->` or a subscript. As clang's
  `DiagRuntimeBehavior`, reachable and evaluated code alone, at the body's end
  with the division warnings (the arithmetic scan's, `check_common_arithmetic_own`).
- **Unreachable code, as clang's CFG prunes it** (#1447). The runtime
  warnings (division by zero, array bounds, null dereference) skip an
  initializer after a `return` or in a dead branch, as they already skipped
  its expression statements: `check_common_unreachable` clears its scalar and
  braced roots and makes them barriers that no discarding parent re-evaluates.
  A constant `if`'s dropped ranges are left out too. A branch condition's truth
  (`check_program_branch_truth`, clang's `tryEvaluateBool`) also counts a
  logical operator that one operand decides though the other isn't known:
  `x && 0` and `x || 1`, through `!`, casts and conversions. That covers
  `if (n && 0)`'s body, `if (n || 1)`'s else and `while (n && 0)`'s body, and,
  as clang's `VisitConditionalOperator` and `VisitLogicalOperator` prune them,
  `(n && 0) ? A : B`'s `A`, `(n || 1) ? A : B`'s `B` and `(n && 0) && A`'s `A`
  (`check_common_operand_mask` takes the same truth where the condition's value
  isn't constant).
  `-Wreturn-type`'s walk takes the same truth, so `if (n || 1) return 1;` ends
  a function without the warning.
- **Const assignment** (#1447). clang's `DiagnoseConstAssignment`
  (`check_semantic_const_assignment`) walks an assignment's lvalue through
  members, subscripts and vector elements to a variable or a direct call. It
  names the first const level it meets and adds a note for each const level,
  outermost first:
  - "cannot assign to non-static data member 'a' with const-qualified type
    'const int'", noted at the member's declarator, which is found in its
    record's body;
  - "cannot assign to variable 'x' with const-qualified type 'const int'",
    noted at the variable's name;
  - "cannot assign to return value because function 'f' returns a const
    value", noted at the return type's first type specifier.

  A level reached through `->` is const by its pointer's target. A record with
  a const member somewhere in it (`DiagnoseRecursiveConstFields`,
  `check_semantic_const_fields`) names that member's first instance,
  breadth-first through its members' records, and adds a note for each one,
  with "nested" past the record's own members. The lvalue is named as a
  member, a variable, or, parenthesized or otherwise, "lvalue". Any other
  lvalue gets "read-only variable is not assignable". A report carries up to
  `BUSTER_CHECK_SEMANTIC_NOTES` (32) notes, and the search visits at most 64
  records.
- **Arithmetic on a pointer to an incomplete type**: clang's "arithmetic on a
  pointer to an incomplete type 'struct s'" (a subscript's "subscript of
  pointer to incomplete type", at its `[`), of the pointee as written, with its
  "forward declaration of 'struct s'" note at the tag's declaring name.
- **Type sugar** (#1447). An operand's type keeps the typedef clang's sugar
  keeps there. A dereference or subscript follows the declaration's chain of
  typedef specifiers to the first typedef whose type is the operand's level
  (`check_driver_spelling_chain`): `*p` of a `pv p` (`typedef v4si *pv`) is
  `'v4si'`, and `**b` of a `VPP b` (`typedef VP *VPP`) is
  `'VP' (aka 'void *')`. A chain's types only shrink, so its first match is
  clang's. A `.` or `->` operand keeps its member declarator's typedef: the
  member's declarator is found in its record's body, which
  `CheckGraph.tag_completions` locates (`check_semantic_member_token`), so
  `x.f++` of a member `uu f` is `'uu' (aka 'struct u')`, and a warning's
  member operand (`check_common_operand_spelling`) keeps it too:
  `x.n < i` is `'ul' (aka 'unsigned long')`. A failed conversion's source and
  target and the null pointer warning's target do too, with a member's
  `typeof` (`&s.m` of a `typeof(long) m` is `'typeof(long) *'`, #1837). An a.k.a. keeps a
  vector's own typedef at any level (`'pv' (aka 'v4si *')`), as
  `desugarForDiagnostic` stops there. It doesn't look inside an `_Atomic`
  either: `_Atomic(I) *` has no a.k.a. (`check_driver_atomic_value`). A typedef'd array's element keeps its
  qualified typedef (`'CI[2]' (aka 'const int[2]')`, `type_print`). A qualifier
  the specifiers write again beside a qualified typedef prints over its name
  (`const CI twice[1]`'s `'const CI[1]' (aka 'const int[1]')`): the operand's
  spelling is then the typedef's use, not its declaration
  (`check_input_typedef_sugar`, resolved by `check_input_sugar_declaration`).
  A typedef with fewer qualifiers than its level names it under the others
  (`volatile CI`'s `'volatile CI[2]' (aka 'const volatile int[2]')`).
  A `-Wformat` argument prints its type as the lvalue conversion leaves it
  (`check_common_format_value`): without its top-level qualifiers (`const I x`
  is `'I' (aka 'int')`, a file-scope `const int` is `'int'`), and desugared
  where its typedef holds them itself, as clang's `getUnqualifiedType` does
  (`CI`'s `'int'`). An array decays to a pointer to its qualified element
  (`'const int *'`). An `_Atomic` argument is checked and printed as its value
  type, which the lvalue conversion leaves (#2234,
  `check_common_format_argument_type`): `%d` of an `_Atomic int` is quiet, and
  `%s` of one is "the argument has type 'int'".
  A declaration's calling convention keeps clang's `AttributedType` where it
  is written, in a type sugar node of its own (`c_types_sugar.c`, made by
  `c_check_type_sugar_input.c`; `docs/c-types.md`'s "Type sugar"). A failed
  conversion's source and target print it, from the operand's declaration
  through `&`, `*` and subscripts (`check_driver_operand_sugar`):
  - `void g(void) __attribute__((sysv_abi))` is
    `'void (void) __attribute__((sysv_abi))'`, though the canonical type has
    no `sysv_abi`;
  - `void (*p)(void) __attribute__((preserve_most))` is
    `'__attribute__((preserve_most))  void ((*))(void)'`: clang's
    `ParenType` of the group, around the attribute's function;
  - an attribute in a group before `*` goes on that group's type, its modified
    type the function without it. A macro writing it prints that function
    (`void (MS *r)(void)` is `'MS void (*)(void)'`);
  - one after a `*` goes on the pointer;
  - a C23 list before the specifiers goes on the whole type.

  Each list a macro writes prints by that macro, per declaration
  (`MacroQualifiedType`, `check_driver_sugar_type`). There is no a.k.a.,
  since clang's `desugarForDiagnostic` doesn't look through these nodes.
  `no_caller_saved_registers` beside a convention keeps none, since clang
  rebuilds the type there.
  A target address space that one macro writes throughout the unit prints by
  that macro, as clang's `MacroQualifiedType` does (`check_driver_space_macros`):
  `'AS1 int const *'`, `'ASN(3) int *'`, and the outermost of nested macros.
  An a.k.a. spells the space out.
  A compound literal's type name spells its value's type as a cast's does
  (#1812, `check_input_token_type_spelling`): `&(L){ 1 }` is `'L *'`, and
  GNU's array initialized by a literal of another array type names it
  (`'A2' (aka 'int[2]')`).
  A null pointer constant argument's warning (#1100) prints its parameter's
  type with the typedef the callee's declaration writes
  (`check_input_parameter_spelling`, `'N *' (aka 'struct N *')`): a
  function's own parameter list, as the conversion notes find it, a function
  pointer object's (through `*`), its typedef'd function type's
  (`F *gp`), and a member's (`s.m(…)`).
  A conversion's target keeps the typedef its declaration writes where it is
  a call's parameter (`check_input_call_parameter_spelling`) or an
  initializer's member or element (`check_semantic_subobject_spelling`, by the
  subobject's offset from the initialized object, through arrays and member
  declarators; a union's member is the first of the target's type), in
  `-Wimplicit-enum-enum-cast` and in a failed initializer conversion's error:
  `struct S s = { .lp = c }`'s "initializing 'LP' (aka 'long *')".
  size_t's `__size_t` sugar (#1447 item 10) in a failed conversion's source:
  - a sizeof's or alignof's value;
  - a call's value, of a library builtin that returns size_t;
  - an arithmetic result, as clang's `UsualArithmeticConversions` keeps the
    sugar of the operand the result takes its type from
    (`check_driver_size_sugar`). That is the converted-to side's, a shift's
    left operand's, a unary operator's, a comma's right operand's and a
    conditional's arms'. Two operands of one type keep only a sugar both
    have, so `n + sizeof(int)` of an `unsigned long n` is `'unsigned long'`,
    and `strlen(s) + 1` is `'__size_t' (aka 'unsigned long')`;
  - an implicitly declared library builtin's designator, or its address, as
    its prototype (`check_driver_library_designator`,
    `cbuiltin_library_spell_as`): `'void *(__size_t)' (aka 'void
    *(unsigned long)')`, `'__size_t (*)(const char *)'`.

  The other warnings' operands keep their spelling as before. A builtin named
  outside a call before any call declares it gets its implicit declaration's
  error after the conversion's, where clang gives it first.
  A variable array prints its size expression as written, with expression
  spacing (`check_driver_vla_bounds`): `'int (*)[n + 1]'`, `'int[n][m * 2]'`.
  A failed conversion's source and target print the typedefs a function
  type's parameters write (#1447 item 5, `check_driver_placed_type`), with
  clang's a.k.a.: `void (*q)(I)` is `'void (*)(I)' (aka 'void (*)(int)')`.
  They come by place, from the operand's declaration (through `&`, `*` and
  subscripts, `check_driver_operand_declaration`), an initialized declarator,
  or a call's parameter. The places are the declaration's own specifiers,
  then each nested parameter's, in order (`check_driver_places`, as
  `__builtin_dump_struct`'s `check_lower_dump_places` reads them;
  `TypeNames.alias_order`). Where the specifiers the printer prints don't
  match them, the type prints as before. Only rendering reads them: a clean
  compile does no work for this.
  `c_check_type_sugar_driver_test.c` compares these whole-output with clang.
- A failed static assertion's requirement prints a character constant by its
  value, as clang's `CharacterLiteral::print` does (#1999,
  `check_semantic_character`): its prefix; `\'`, `\\` and the control
  characters' C escapes; else, with an ordinary constant's sign extension of a
  byte taken off, a printable byte as itself, `\xNN` below 256, `\uNNNN` to
  0xFFFF and `\UNNNNNNNN` past it (`'ab'` is `'\u6162'`).
- **Differences left.**
  - A cast inside a variable array's bound prints its builtin type keywords
    as clang's TypePrinter does (`int[(unsigned)n]` is `'int[(unsigned int)n]'`,
    `check_semantic_builtin_spelling`), but other type names as written: a
    complex's, a `_BitInt`'s, a pointer's own qualifier order. `sizeof` of a parenthesized expression
    has no space (`sizeof(x)`, clang's `sizeof (x)`; the static assertion's
    requirement shares the spacing). Only a type's first eight bounds print;
    later ones print `[]`.
  - A typedef written in a function type's parameters prints only in a
    failed conversion's source and target (below); other warnings print
    those parameters canonically. A parameter's top-level qualifier, which
    the function type drops (`void (*pp)(volatile I)`), and a variable
    array's bound inside a parameter (`int (*)[n]`) print in neither the
    type nor its a.k.a., where clang's sugar keeps both.
  - An initializer subobject's null pointer warning (`{ .lp = 1-1 }`) prints
    its target canonically: the initializer walk that gives it keeps neither
    the declaration nor its subobject's member.
  - A call into a redeclared function prints its parameters' sugar from the
    declaration the call sees, where clang's composite type keeps the first
    declaration's (`void c(N *q); void c(NP q);`'s call prints `'NP'`, clang
    `'N *'`), in the null pointer warning and the conversion errors alike.
    Neither the warning walk nor the driver keeps a redeclaration's first
    declaration.
  - The reader's spelling helpers (`check_input_operand_type_spelling`,
    `check_input_object_spelling`), which run before the stream has types,
    don't follow members. Their callers that can do follow them with the
    stream (#1837): the warnings the driver spells after checking
    (`check_common_operand_spelling`: `-Wsign-compare`, `-Wformat`,
    `-Wenum-compare` and the like), a failed conversion's source through `&`
    and `*` (`check_driver_pointer_spelling`) and an assignment's member target,
    and the null pointer warning's member target. An initializer's member or
    element target (`struct S x = { .lp = c }`) still prints canonically.

## Ignorable and unsupported pragmas

The checker consumes retained pragmas after their existing semantic handlers. An
unknown directive is not an unsupported C construct: it is ignored, with
`-Wunknown-pragmas` policy (normally silent, enabled by `-Wall` or `-Wpragmas`).
Unknown `STDC` directives warn by default. Malformed alignment hints such as
`#pragma align 4` and ignored tooling comments use `-Wignored-pragmas`.
`#pragma comment(lib, "m")` is accepted silently for the current ELF path; this
is not an implementation of COFF linker directives or automatic library loading.
GCC optimization hints such as `ivdep`, `push_options`, `pop_options`,
`reset_options`, `target`, and `optimize` use the unknown-pragma policy, as in
the pinned Clang 23.1.4 oracle. In particular, accepting `GCC target("avx2")`
does not enable AVX2; use the implemented per-function `target` attribute.
The same policy covers plain and GCC `scalar_storage_order`,
unqualified `section`, `code_seg`, `data_seg`, `bss_seg`, `const_seg`, `init_seg`
and `fenv_access` in the accepted Linux mode. Their arguments do not change
layout, symbol placement or floating evaluation, even when malformed; enabling
`-Wunknown-pragmas` reports them and promoting that warning prevents emission.

OpenMP directives are ignored when OpenMP was not requested, with the separately
enabled `-Wsource-uses-openmp` / `-Wopenmp` policy. Effective driver option order,
not a definition of `_OPENMP`, determines whether OpenMP was requested. An active
`-fopenmp` request, including alternate runtime selections, must not silently
produce serial code: a code-generating action refuses the option itself before
reading source (the instrumentation boundary in the introduction), while a
non-emitting action refuses each `#pragma omp` directive through the checker. A
subsequent `-fno-openmp` restores the ignore behavior for both.

`#pragma weak NAME` applies weak ELF binding to an external variable or function,
including tentative definitions and undefined references. A directive after an
explicit definition keeps that definition's existing binding. Lookup uses the
translation unit's declarations, so a local shadow does not redirect it. Forward
directives that never acquire an external declaration warn at EOF; forward
typedefs and internal `redefine_extname` declarations retain Clang's distinct
warning groups and locations. A weak directive after an existing internal declaration
or typedef is a silent no-op, as in the oracle. A directive after a use applies
to the declaration that was the latest at it, from the directive on, as clang
adds the attribute there (#1849): an earlier eagerly emitted reference keeps
the symbol global (the weak attribute's "after a use" rule).

`#pragma redefine_extname NAME SYMBOL` uses the existing asm-label emission path
for declarations and subsequent references. The first pending rename and explicit
asm labels take precedence; a directive after an explicit definition keeps its
already emitted name. Source-time macro expansion applies to both directives'
operands. Malformed forms use `-Wignored-pragmas`, including missing names and
trailing tokens. These lexical warnings survive parser recovery.

For a rename after a parsed use, emission selection decides whether an earlier
emitted reference has already fixed the original name. Compact references select
candidate eager bodies; the existing target-feature emission walk finds their
actual references, with each body's definition-completion point. It preserves
source labels and constant-false loop bodies while skipping plain statements
after a return or an ordinary noreturn call. Static initializers retain their
eager/deferred rules. Unevaluated expressions, unselected operands and deferred
functions do not block a later rename. A
directive inside an eager function still precedes that function's emission. This
cost and its cold per-pragma decisions exist only in units with a late rename;
the declaration's retained asm-label fact and source coordinates stay immutable.
The object oracle covers 43 late-rename shapes, including eager/deferred static
data, unreachable labels, loops, gotos, overloaded noreturn calls and trapping builtins, and
compares scalar/vector objects and poisoned multi-lane objects. The shared walk
borrows live preprocessing options only during selection and retains no new
pointer to a worker's preprocessor.

`#pragma weak ALIAS = TARGET` (#1849) declares ALIAS a weak alias of TARGET, as
clang's Sema clones TARGET's declaration under ALIAS's name with weak and alias
attributes: TARGET is looked up at the pragma, or at its first later external
declaration, and one that already has an alias attribute makes it a no-op; a
TARGET never declared is clang's "weak identifier 'TARGET' never declared" at
ALIAS. The shapes whose ALIAS the unit declares with external linkage, of
TARGET's kind, type and thread-local storage, without a section, alias or
definition after the pragma and, where it declares ALIAS before the pragma,
named by nothing after it, take the alias as attributes of ALIAS's declaration
(`check_input_symbol_alias`), which the emission makes clang's weak alias of
TARGET's definition; one whose ALIAS the unit defined before the pragma, or
declared an alias of its own before it, is clang's no-op. Clang's clone itself
is ALIAS's symbol where the unit never declares ALIAS, or declares it only
before the pragma, never names or defines it, and gives it another kind, type,
thread-local storage or a section (`CheckInputSymbolPragma.alias_target`): the
selection keeps TARGET's definition as an alias's base, and
`check_program_define_pragma_aliases` adds a weak symbol of ALIAS's name at
it, of its type and size, after the aliases' (in the unit of several lanes
that has TARGET's definition). The rest stay refused by name where clang's
Sema errors: an ALIAS declared before the pragma and named after it ("reference
to 'ALIAS' is ambiguous", clang's lookup then finds two declarations), defined
after it ("redefinition of 'ALIAS'"), declared after it with another kind or
type, and a TARGET the unit doesn't define ("alias must point to a defined
variable or function"); and an ALIAS of internal linkage. This is separate
from ordinary weak binding. `test-checker-driver`
(`c_check_symbol_pragma_driver_test.c`) compares the supported shapes'
symbols and programs with clang's, by both paths.

With `-flazy-system-headers`, an earlier occurrence of the pragma's name in a
deferred body is conservatively refused because its binding was not checked;
unrelated deferred bodies do not suppress weak or renamed symbol metadata.
The unified handler retains the literal handler's allocation rollback, native
ELF, separately linked strong-override, and selected-header regressions.

`#pragma ms_struct on`, `off` and `reset` select the layout sampled by subsequent
record definitions. The state follows source order across function boundaries;
forward declarations do not capture it. Macro operands expand when the semantic
preprocessor retains the directive. Malformed state names, missing operands and
extra tokens warn under `-Wignored-pragmas` and leave the previous state intact.
A directive between struct members is a hard placement error.

Explicit `ms_struct` and `gcc_struct` attributes attach to structs and unions,
including attributes inherited from a forward declaration. `gcc_struct` selects
ordinary layout even while the pragma is on. Both attributes on one declaration
conflict, with a note at the earlier one; Clang permits them across separate
declarations, where `gcc_struct` wins. The type table implements complete
bit-field storage units, same-size sharing, zero-width and union rules, and
packed/aligned interactions in both layout paths (see [types](c-types.md)).
Units whose byte size is not a power of two, such as a `_BitInt(129)` bit-field,
are explicitly refused because their Microsoft alignment cannot fit the current
type representation. A raw preprocessor client without semantic pragma expansion
also refuses a macro-dependent setting instead of guessing its expanded state.

The reader fixtures check all four parser schedules and retained state across
function boundaries; the driver compares warning controls and exact diagnostic
locations with Clang. Separately compiled Clang and Buster callers/callees exchange
ordinary, packed, zero-width and union records in both directions at `-O0` and
`-O2`, with fixed layout assertions and scalar/vector object equality.

`#pragma options align=X` and `#pragma align=X` (#1849, clang's
`ActOnPragmaOptionsAlign`) act on the same stack as `#pragma pack`
(`check_input_pack_align_record`): `packed` pushes the current value and packs
to 1; `natural`, `native` and `power` push it and set the default, whose layout
they are on x86-64; `reset` pops the stack's top, or with an empty stack resets
a value other than the default (a pack value, or packed or natural's mode),
else warns "#pragma options align=reset failed: stack empty". A malformed one
is ignored with clang's `-Wignored-pragmas` warning at its place ("expected
'align' following '#pragma options'", "expected '='", "expected identifier",
"invalid alignment option", "extra tokens"), as the options and pragmas map
it. `mac68k`, clang's error on this target, is refused by name, as are other
unsupported semantic effects.

`#pragma pack`'s own warnings (#1849, `check_input_pack_record` and
`check_input_pack_align_warnings`) are clang's, whole output on both paths:
- its parser's (`HandlePragmaPack`, `-Wignored-pragmas`), for a malformed
  directive, which is ignored: "missing '('", "unknown action", "expected
  integer or identifier", "missing ')'", "extra tokens", at the token it names
  or past the line's last one. A raw client's operand that a macro spells is
  left unhandled, as an align one's, for the checker's refusal;
- Sema's (`ActOnPragmaPack`), at the directive's `pack`, where the parser
  handles its annotation: after the construct before it and before the next
  directive's line. Right after a function's body they follow that body's own
  end-of-body warnings, which the checker publishes at the next parser token,
  so there a later pack directive's parser warnings come before them, where
  clang's come after. They are "expected #pragma pack parameter to be '1', '2',
  '4', '8', or '16'", "specifying both a name and alignment to 'pop' is
  undefined" (no option), "#pragma pack(pop, ...) failed: stack empty" and
  `pack(show)`'s value (no option: 8 for the default, as clang prints it);
- at the unit's end (`-Wpragma-pack`, `CHECK_WARNING_ID_PRAGMA_PACK`), one
  "unterminated '#pragma pack (push, ...)'" per push left open, an align
  push's too, the innermost first. The innermost has clang's note "did you
  intend to use '#pragma pack (pop)'" where the value is the default's, at the
  directive that last set it (clang's `CurrentPragmaLocation`, which a pop
  restores from its push), or without a place where none did;
- for an inclusion that leaves the value other than it found it ("the current
  #pragma pack alignment value is modified in the included file", clang's
  `ChangedStateAtExit`), at the include's file name, where its output ends,
  with the note at that directive. The inclusions come from the preprocessor's
  runs of output by range (`check_input_pack_includes`), a unit without pack
  directives or with inexact runs walking none. clang's
  `-Wpragma-pack-suspicious-include`, off by default, isn't reported.

`test-checker-driver` compares them in the default-warning sweep (both paths,
`-Werror`, `-w`, `-c`) and with real headers (`check_test_driver_pack_includes`).
Warning suppression cannot approve these directives. Already
supported directives keep their original handlers and syntax validation.

Directive warnings are sparse lexical facts with independent preprocessing
source coordinates. Command-line policy, source-ordered diagnostic push/pop,
macro invocation locations, system headers, parser recovery, and EOF directives
use the same warning machinery. A warning promoted to an error prevents object
emission. Neither classification nor warning policy changes `-E` output.

