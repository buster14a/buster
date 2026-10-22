# Preprocessor fuzzing

[#109](https://github.com/buster14a/buster/issues/109) checks
`c_preprocessor.c` against `clang -E` on generated programs. An independent
harness generates the cases, runs the clang oracle, compares outputs and
minimizes differences (`preprocessor_fuzz*.c`, `test-preprocessor-fuzz`).
`c_preprocessor_fuzz.c` attaches the preprocessor to it
(`test-preprocessor --fuzz`).

## Commands

```sh
./build/compiler test-preprocessor
./build/compiler test-preprocessor --clang
./build/compiler test-preprocessor --clang --fuzz 1000000 --seed 11 --jobs 16
./build/compiler test-preprocessor --clang --fuzz 64 --first 15911 --seed 3 --std c17
./build/compiler test-preprocessor --print-case 15911 --seed 3 --std c17
./build/compiler test-preprocessor-fuzz --std c17
./build/compiler test-preprocessor-fuzz --std c23
./build/compiler benchmark-preprocessor-fuzz --std c17
```

- `test-preprocessor` runs its fixtures, then 1,500 generated cases in each
  of C17 and C23 (seed 1) without clang. Both kernels must agree on each
  case, as below. With `--clang`, each case is also compared with clang.
- `--fuzz N` sets the number of cases per mode (0 skips them). `--first N` is
  the first case's id, `--seed N` picks another sequence and `--std c17` or
  `--std c23` runs one mode. `--jobs N` forks N workers, each with a
  contiguous share of the ids. The cases don't depend on N. Without files,
  `--clang` also runs the fixtures and the predefines, keywords and features
  checks.
- `--print-case N` prints case N as clang reads it alone: its `#line`, its
  source and its marker. Pipe it into `clang -E -P -undef -nostdinc -std=c17
  -x c -`, or into `./build/compiler -E -P -undef -nostdinc -std=c17` after
  saving it.
- `test-preprocessor-fuzz` checks the harness alone: the generator, the
  oracle's batching and isolation, the comparison and the minimizer on a
  planted difference. `benchmark-preprocessor-fuzz` measures the harness's
  cost. Neither runs the preprocessor.

The command names were left open until #117's preprocessor existed. They are
now settled. The preprocessor's fuzzing is an option of its test command,
`test-preprocessor --fuzz`, beside its fixtures and corpora. The independent
harness keeps `test-preprocessor-fuzz` for its self-tests.

On the 9700X, take the lock for each run:
`EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh COMMAND`. A million
cases in each mode take about 10 minutes on 15 threads there, longer than a
CI job may wait. So split a long run with `--first` and `--fuzz` into runs of
about 5 minutes, each under its own lock.

## Files

- `preprocessor_fuzz.c`: bounded text building, the comparison
  (`preprocessor_fuzz_compare_mode`), the minimizer and the saving of
  `fuzz_failure_N.c`.
- `preprocessor_fuzz_generate.c`: the generator. It has 36 template
  families and 12 random-program themes (`preprocessor_fuzz_construct_names`).
- `preprocessor_fuzz_clang.c`: the clang oracle. It covers batching, the
  attribution of a rejected batch's errors to its cases, and splitting the
  output at the markers.
- `preprocessor_fuzz_test.c` and `preprocessor_fuzz_benchmark.c`: the
  harness's self-tests and benchmark.
- `c_preprocessor_fuzz.c`: our side of each case, the kernels' check, the
  comparison with clang, the minimizer's oracles, the reports and the
  workers. `main.c` includes it after `c_preprocessor_test.c`, whose helpers
  it uses.

## A case

Inputs depend only on the seed, the case's id and the mode. Each case's names
start with `ppf_<id>_`, so no two cases of a batch share one. A case ends by
undefining its names, and the generator closes its conditionals and its last
logical line. The batch then puts the case's marker, `BUSTER_PPF_END_<n>`, on
a line of its own after two newlines.

Our side preprocesses the case as clang reads it alone. That is its `#line 1
"ppf_<id>.c"`, its source and its marker
(`preprocessor_fuzz_batch_source`). It uses preprocessors of its own with the
oracle's configuration: `-undef -nostdinc` and `-std=c17` or `-std=c23`, with
trigraphs in C17, as in clang's ISO modes. Clang runs as
`clang --no-default-config -E -P -undef -nostdinc -std=c17|c23
-fno-color-diagnostics -fno-caret-diagnostics -ferror-limit=0 -x c -`.

### The kernels

The vectorized kernels and their scalar twins must agree on every case, as
on the fixtures (`pp_fuzz_kernels`):

- the acceptance and the counts of errors and warnings;
- the streams, token for token (kind, flags, value and location);
- the directive tables and the expansion history;
- both printers, byte for byte, with line markers and without.

The `-P` text must also lex back into the stream's spellings. The exception
is a stream that can't lex back, because clang's text for it can't either
(`pp_fuzz_unprintable`, counted as "whose text can't lex back"). An
unterminated literal runs to the end of its line and takes the tokens after
it. A stray `\` at the end of a line splices it to the next. A stringized
unterminated literal keeps its quote, since clang escapes only literals'
quotes.

### Against clang

A case must have clang's acceptance. An accepted case's stream is printed as
clang's `-P` printer would print it (`pp_fuzz_clang_text`, below). Both
texts are split at their markers and lexed as the mode lexes. They must then
give the same token kinds and spellings (`preprocessor_fuzz_compare_mode`).
Whitespace and line breaks don't count.

Some cases are compared by their spellings with all whitespace removed
(`pp_fuzz_by_spellings`, counted "by spellings"), because clang's printer
decides where their tokens are joined. There are two kinds:

- A stream with an unterminated literal. It runs to the end of its line in
  either text, so the line breaks clang chooses decide which tokens it takes.
- A case with two or more tokens on presumed line 1. Clang's printer takes a
  token that started a line inside a macro's arguments for the file's first
  (`MoveToLine`'s `IsFirstInFile`), and indents it by its column in place of
  a space, which can join it to the token before.

#### Clang's `-P` printer, emulated

Our printer's text lexes back into its stream; clang's sometimes doesn't. It
can join two tokens that then lex as one: a number and an identifier that
starts with a UTF-8 letter, `U` and `""`, `:` and `:` in C17. So the
comparison prints our stream with clang's spacing on our side too. A token
that starts a line, or follows a pragma's line, starts a new line, indented
to its column (`HandleWhitespaceBeforeTok`). Any other token gets a space
when clang's token would have a leading space, or when clang's
`TokenConcatenation::AvoidConcat` asks for one (`pp_fuzz_avoid_concat`).
The C rules:

- A number before `$` gets a space, since `isPreprocessingNumberBody` counts
  `$`.
- Only `L` is a string prefix in C: `IsStringPrefix` has `u`, `U` and `u8`
  for C++11 alone. So `U` and `""` print joined as `U""`.
- Two tokens that were next to each other in the source never get a space.
  Clang's tokens start at the line splices before them.
- A token that started a line among a macro's arguments keeps its own flags
  in clang (`BUSTER_PP_LINE_SPACE` marks ours), but an argument's first token
  takes its parameter's. Clang gives it no space, unless whitespace or a
  comment came before it on its line, and starts a line with it, which its
  printer moves to when the expansion's line is another one: after a line
  splice before the macro's name. Our printer does the same in `-E` with line
  markers, and so every expansion read from a file records where its tokens
  start (`PPExpansionStart`).
- After a `_Pragma` that prints nothing, clang's lexer starts a line at the
  next token (`Lexer::PropagateLineStartLeadingSpaceInfo`, as the pragma's
  lexer ends). Its printer moves to that token's line only when it is another
  one (`MoveToLine`): after a line splice, as with a `\` that the token's
  line break then keeps apart. `Preprocessor.silent_pragmas` records these
  pragmas and the expansion the next token comes from. Our printer does the
  same in `-E` with line markers.
- A line's first token is placed where clang's lexer starts it: at a line
  splice right before it. It is indented from column 2 when it is in column 1
  with a leading space, by clang's rule for its token's. Our
  `BUSTER_PP_SPACE` also counts the line's start there, which clang's
  doesn't: so a file token's leading space is read from its line, and an
  expansion's first token's from its name's (`BUSTER_PP_NAME_SPACE`).
- A pragma line's `#` isn't indented: clang writes `#pragma` right after
  moving to its line.
- A pragma line spells its identifiers as written (UCNs kept). Elsewhere a
  UCN identifier prints as UTF-8.

### Batches

Clang reads a block of 64 cases in two batches: the ones we accept, then the
ones we reject. A batch of accepted cases usually exits cleanly. A rejected
batch's errors are attributed to their cases by the `#line` file name in each
error's location. If an error names no case, or one is fatal, the batch runs
case by case.

The output is split at the markers. Each marker ends its printed line and is
found by its bytes, as a whole identifier outside literals. Lexing the batch's
whole text to find them would fail: a case's output can hold an unterminated
character constant that ends at a later quote on its printed line. A `/*`
after it would then take the marker into a comment. Any other marker, or one
out of its place, fails the split.

Four kinds of case run in a clang process of their own (`isolated`):

- the cases that use `__COUNTER__`, whose state is the process's;
- a fatal error (a missing `#include`);
- one that leaves an invocation, an operand, a conditional or a comment open
  (`Preprocessor.unterminated`);
- one that leaves `#pragma clang diagnostic push` without its pop.

A case that differs from clang is read again alone, and so is every case of
one block in 128. The batch must give what the case alone gives. A case that
differs alone is printed, minimized with the oracle that found it
(`preprocessor_fuzz_difference`) and printed again with both sides' results.
It is saved as `fuzz_failure_N.c` in the current directory, the first 4 per
worker and mode. If clang gives a batch no result, each of its cases is read
alone. A case that still has none is reported and fails the run.

## The generator

The 36 template families cover object-like and function-like macros, both
variadic syntaxes, and empty and parameterless macros. They cover
stringification, chained and empty-argument pastes and the invalid `#` and
`##` positions. They cover variadic forwarding, `__VA_OPT__`, GNU's comma
elision, self and mutual reference and invocations crossing a replacement's
end. They cover protected and unprotected commas, empty, missing and
multiline arguments, and redefinitions. They cover conditionals, `defined`
from macros, `#if`'s arithmetic, logical and conditional operators and
short-circuited division. They also cover `_Pragma`, `__LINE__` in multiline
invocations, `__COUNTER__`, and splices and comments at token boundaries.
Every case also starts with a chain of aliases, redefinitions and nested
invocations of its own.

The 12 random themes write programs of up to 12 statements over eight names.
The statements are definitions of every shape, text with invocations and
groups, conditionals two deep and directives. Builtin lines, lexical corner
cases, pragmas and directives among a macro's arguments come too. Each theme
favors its own kind of statement. For #109, the generator gained:

- every pragma clang handles in `-E`:
  - `message`, `GCC warning`, `GCC error`;
  - `diagnostic` with each of its verbs and groups;
  - `push_macro` and `pop_macro`;
  - `GCC poison` and its uses: in text, `#ifdef`, an argument and a paste;
  - `GCC dependency` and `GCC system_header`;
  - `once`, `mark` and `region`;
  - `omp` and `STDC`;
  - the `clang` regions and macro notes;
  - unknown pragmas;
  - all of them in `_Pragma` too, also from macros and in expanded arguments;
- `#line` with its numbers' edges and file names, line markers and their
  flags;
- the feature queries over the case's names:
  - attribute queries whose operand has two names, a scope (written, or from
    a macro), a `_Pragma`, a query inside it, parentheses or a comma;
  - attribute queries closed by a macro's `)`, or with a directive inside
    the operand;
- `_Pragma` in `#if` and `#line` lines;
- extra tokens after directives;
- UCN spellings;
- in the variadic and stringification themes, macros that mix `__VA_OPT__`,
  `##` and `#` with gaps of every kind, invoked inside a stringizing macro
  with empty and other arguments, so that the expansion's spaces show in the
  string.

The generator keeps two clang bugs out of its cases, since they would hang
or swamp the oracle. The first is a `## ##` whose second operator goes with
an empty argument or an empty `__VA_OPT__()` at a body's end: clang's
`TokenLexer::pasteTokens` reads past the expansion, and a release build
hangs. The generator writes no rare token next to a `##`
(`PreprocessorFuzzGenerator.last_paste`). The second is `#line 4294967294`
or `4294967295`: every later diagnostic's caret line is then gigabytes of
spaces, so the generator doesn't write those numbers.

## What the fuzzing found

Each difference was fixed in `c_preprocessor.c` against clang 23, with a
fixture in `pp_test_cases` (the block after "#109's differential fuzzing").
Probing around each one found more, which were fixed and pinned the same way:

- **Arguments.** Arguments are expanded at their parameter's first use in the
  body, not in parameter order, which `__COUNTER__` shows. The variadic
  argument is expanded at a `__VA_OPT__` with content, an inactive
  `__VA_OPT__` skips its content's arguments, and an empty `__VA_OPT__()`
  expands nothing. A `_Pragma` in an argument being expanded doesn't run
  (`InMacroArgPreExpansion`): its tokens stay, are stringized and pasted, and
  run when the substitution is rescanned. `defined` from a `#if` line's macro
  argument stays an identifier while the argument expands.
- **Pastes.** In `## ##`, the second `##` is the first's right operand (an
  error), unless an empty argument or `__VA_OPT__` after it removes it.
  GNU's `, ## __VA_ARGS__` takes a comma from anywhere in the result, and from
  before an empty argument's placemarker. It keeps the variadic argument's own
  first space, and it does nothing when `##` follows `__VA_ARGS__`.
  `X ## , ## __VA_ARGS__` with the argument omitted is `X`.
- **Spaces in a substitution.** These follow clang's `NextTokGetsSpace`
  exactly (`pp_substitute`). A body token's leading space counts unless a `##`
  is before it. Every token `__VA_OPT__` has, its `)` too, sets the space for
  the next token, even when the `__VA_OPT__` gives nothing. A `##` between
  two tokens takes the pending space, and one next to a placemarker doesn't.
  `# __VA_OPT__`'s string takes the space pending at its `#`, which stays
  pending.
- **Attribute queries.** `__has_attribute`, `__has_c_attribute`,
  `__has_cpp_attribute` and `__has_declspec_attribute` read their operand
  expanded, a token at a time, to the `)` that closes it, wherever that comes
  from (`BUSTER_PP_CAPTURE_STREAM`, `pp_stream_check`). A macro's expansion
  can close it, and what follows the `)` in that expansion comes after the
  answer. The operand runs on past directives and past an included file's
  end. The token after `__has_c_attribute`'s name is read as it is (clang
  looks for a `::`). Only the C and C++ queries take a scope. The other
  queries read their operand unexpanded.
- **`_Pragma` in a directive's line.** In `#if`, `#elif`, `#line` and an
  expanded pragma's operands, a `_Pragma` runs, and its pragma line comes
  before the directive's effect (`pp_lift_pragma_lines`).
- **GCC poison.** It is an error where a lexer reads the name: file text,
  directive lines but `#error` and `#warning` text and header names,
  arguments, `_Pragma` text and pastes. It is never an error in a plain macro
  expansion. A poisoned macro still expands. `#ifdef` of a poisoned name
  that isn't a macro is false, and so is `#ifdef defined`.
- **Directives.** These cases give warnings, not errors: `#include "f" junk`,
  `#else x`, `#endif x`, `#undef X y`, `#pragma once x`, and extra tokens
  after `#line`'s file name and after `#ident`'s string. `#elifdef` without
  a name is an error.
  `#line`'s number fits 32 bits and skips digit separators. A line marker's
  flags are `[1|2] [3 [4]]`, and a 2 needs an open 1.
- **Expressions.** `>>` shifts by the amount's low 32 bits, or by 63 when
  that is 64 or more. A comma in `#if` is an error.
- **Pragmas.** Clang prints each pragma in its own way:
  - `message` and `GCC warning` print as clang reprints them, and `GCC error`
    too;
  - `diagnostic` prints only for a known group (`ignored` of an unknown one,
    and a `pop` without a push, give a warning and nothing);
  - `omp`'s operands expand;
  - `mark`, `region`, `once` and `GCC system_header` print nothing;
  - `push_macro` makes a redefinition silent;
  - `assume_nonnull end` is placed one line early in `-E`'s markers.
- **Lexing.**
  - Literal prefixes follow the mode.
  - Before C23, a number has no digit separator: `1'000` is 1 and an
    unterminated `'000` (`pp_lex_mode`).
  - A `_Pragma`'s string with an unterminated comment is an error.
  - So is a paste that would start a comment.
- **Builtins and predefines.**
  - `__has_warning` takes strings only.
  - `__has_embed` is answered (`pp_has_embed`).
  - The target queries answer as clang does for x86-64 Linux.
  - `-undef` keeps the macros clang keeps.
- **UCNs.** Identifiers with UCNs keep their spelling when stringized and
  pasted, and in pragma lines.

The first frozen campaign (`c4a7706`) found the rest. On the 9700X, seeds 21
to 30 with 1,000,000 cases per mode each, 130 cases differed from clang (70
in C17, 60 in C23), one's `-P` text didn't lex back into its stream and two
had no result; six of its 300 workers crashed (the attribute query below).
Locally, seed 11 was stopped after its first six differences. The runs that
checked the fixes (seeds 3 to 7, 200,000 to 500,000 cases per mode) found
eight more. They come down to these fixes, each with a case in
test-preprocessor, and to the harness changes after them:

- **`__VA_OPT__` in a macro without parameters** is a token. Clang
  substitutes nothing in such a macro (`TokenLexer::Init`), so its
  `__VA_OPT__`, its parentheses and a `#` before them stay as written.
- **`__VA_OPT__` after `##`** pastes left to right with its content, as
  clang's `TokenLexer` pastes. `.5 ## __VA_OPT__(e ## .5)` is `(.5 e) .5`,
  `.5e.5`; pasting the content first would form the invalid `e.5`. An empty
  argument first in the content waits, as clang's placeholder does. And
  `, ## __VA_OPT__(__VA_ARGS__ ## ...)` is GNU's comma (`VaArgsPseudoPaste`).
- **`_Pragma`'s operand.** It reads its `(`, its string literal and its `)`
  expanded, a token at a time, as clang's `Handle_Pragma` reads them with
  `Lex` (`pp_start_pragma_stream`). A macro can give any of them. The main
  loop steps a token at a time while a stream is open (`pp_step_limit`), so
  that the pragma runs before anything after the `)` is read: a `pop_macro`
  changes what follows. A malformed operand is reported once, and recovers as
  clang's does.
- **Line splices before a token.** Clang's lexer starts a token at a line
  splice right before it (`pp_lexed_start`), a `??/` one with trigraphs too.
  So `__LINE__` at an expansion's end, a `)` or a name after a lone `\` line,
  gives the splice's line, and so does a `#line`'s number.
- **An unterminated literal that ends in a line splice** is spelled with the
  newline after it: clang's cleaning reads the character after the splice,
  past the token (`Lexer::getSpellingSlow`, `pp_trailing_splice`). Stringized,
  it leaves that newline in the string.
- **A line marker's name and flags** are read expanded after its digits
  (`HandleDigitDirective`'s `Lex`): `# 30 E` with `E` empty is a plain
  `# 30`.
- **A `_Pragma`'s text** gets a newline after it (`prepare_PragmaString`),
  which splices away a `\` that ends it: `_Pragma("\\")` is an empty
  pragma.
- **`#line`'s note** starts at its number, or at the macro or builtin that
  made it, as clang's `AddLineNote(DigitTok)`, not at the directive's first
  operand. `#line _Pragma("x") 10` and `#line E 10`, with `E` empty, number
  from the digits, and the `_Pragma`'s line keeps its own number.
- **Character constants in `#if`** follow clang's `CharLiteralParser`
  (`pp_parse_character`):
  - an ordinary or `u8` constant can't hold a character past 0x7F
    (`'\u00e9'` and `'é'` are errors), nor `u` one past U+FFFF;
  - several characters pack into an int, each byte unsigned, and only an
    ordinary constant may have them;
  - errors: `\x` past the code unit, `\u` that is incomplete or names a
    surrogate, and before C23 a UCN of a basic character;
  - errors that leave a value: octal past the code unit, `\x` without
    digits;
  - warnings: an unknown escape, bad UTF-8 in an ordinary constant, and
    several characters.
- **The printer after a silent `_Pragma`.** The emulation moved to the
  `_Pragma`'s line; clang moves to the next token's (above).
- **A prefix the mode lacks before an empty character constant** is an
  identifier: `u8''` before C23 is `u8` and `''`, as are `u''` and `U''`
  before C11, and the prefixes of unterminated literals. So pasting `u8` and
  `''` forms two tokens there, an error (`pp_splits_unknown`).
- **The emulation's line starts.** It indented a line's first token in column
  1 whenever ours had a leading space, which counts the line's start; after a
  `\` that ends the line before, the indentation decides whether the two
  lines' tokens join. A pragma line's `#` is no longer indented. And a
  `_Pragma`'s line in a `#if` among a macro's arguments took the place of the
  macro's expansion start, so its tokens seemed to be on the `_Pragma`'s line;
  the start is recorded again after the pragma's line
  (`Preprocessor.displaced_location`).
- **A scoped attribute query at the main file's end.** `__has_c_attribute(x`
  as the file's last tokens waited for the token after `x` (to see a `::`)
  after the file's last context was gone, and read past the context stack.
  It now waits for the main file's end, which ends the stream.
- **Directives among a macro's arguments.** Clang's macro expansion is off
  while it collects arguments, and only `#if` and `#elif` turn it back on
  (`EvaluateDirectiveExpression`). So a `#line`'s and an `#ident`'s operands
  there are read as written (`pp_collecting`): `#line N` with `N` a macro, or
  `#line _Pragma("x") 1`, is an error there.
- **A disabled macro's name among the arguments its expansion gives** is
  painted as it is collected (`ReadMacroCallArgumentList`): with
  `#define e a ( e` and `#define a(...) __VA_ARGS__`, `e )` is `e`. The `)`
  comes after `e`'s expansion has ended, which enabled `e` again before the
  argument expands.
- **A line among a macro's arguments** that the emulation kept on the line
  before starts its own in clang when the expansion's line is another one
  (above): `L\`, then `h(`, `U` and `"#")` on lines of their own, printed
  `U` and `"#"` together, one literal.

Each harness change came from a case the comparison couldn't place. They
cover the printer's emulation, the by-spellings cases, the isolation rules,
the attribution of a rejected batch's errors, the line-based marker split
and reading an unanswered batch case by case. After the first frozen
campaign, two more: an error clang prints at no location (`error: missing
'(' after '__has_include'`, for `__has_include` after an empty argument at a
macro's end) is a lone case's rejection, and makes a batch run case by case,
where it had left the case without a result; and an unterminated literal
whose text ends in `\` at a line's end can't lex back, as a stray `\` there
can't.

## Known differences

No difference is filtered from the comparison. The generator writes none of
the following, which are listed in
[the preprocessor's design](c-preprocessor.md) ("Not yet"):

- `-E`'s line markers when an attribute query's operand runs past an
  included file's end. Clang prints the file's exit before the answer, and
  places it on the query's line.
- A `_Pragma` in a computed `#include`'s line: clang reads the string as a
  header name when the `_Pragma` comes from the line itself.
- Clang's `#pragma clang __debug` commands.
- A UCN that names a basic character (`A`), which clang rejects in an
  identifier.
- Warnings that don't change acceptance: `-Wvariadic-macros`,
  `-Wexpansion-to-defined` and others. The comparison counts errors, not
  warnings.
- `\N{name}` escapes in `#if`'s character constants are refused: there is no
  table of Unicode's names. Clang decodes them.
- `-funsigned-char`: an ordinary character constant in `#if` stays signed,
  where clang makes it unsigned.
- `-E -P`, without line markers, keeps no line numbers, so it doesn't move the
  token after a `_Pragma` that prints nothing to its own line. Only a `\`
  that ends the text before it shows that.

## Qualification

After the last fix (`c4a7706`), the code was frozen and run on both hosts.
The local host is a Ryzen 9 7940HS (Zen 4, clang 23.1.3, 16 threads). The
9700X is benchpress (Zen 5, clang 23.1.1, 15 threads, under its lock in
slices of 500,000 cases per mode). Every run compared every case with clang:

| Host | Seeds | Cases per mode | Differences | Cases without a result |
| --- | --- | ---: | ---: | ---: |
| 7940HS | 11 to 20 | 10,000,000 | 0 | 0 |
| 9700X | 21 to 30 | 10,000,000 | 0 | 0 |

Before the freeze, runs of 100,000 to 200,000 cases per mode on seeds 1 to 3
found the differences listed above. They were fixed and run again until each
seed gave none. A focused check of substitution spacing ran beside them: 4,000
stringized `__VA_OPT__`, `##` and `#` bodies per run, over 12 seeds and three
modes. It found the spacing rules above and now finds no difference.

The corpora stay token-identical after the fixes:

- `sqlite3.c` (707,153 tokens);
- Lua 5.4.8 (34 files, 578,971 tokens);
- this repository's 19 translation units (11,998,175 tokens);
- the corpus oracle's CI checks (`docs/preprocessor-oracle.md`).
