# Independent preprocessor fuzz tooling

This is the preprocessor-independent slice of
[#109](https://github.com/buster14a/buster-fast/issues/109): an original
deterministic C generator, clang oracle, output comparator, minimizer and
self-tests. It does not call the preprocessor in #117/#193 or establish that
the compiler preprocesses any generated case correctly.

`test-preprocessor-fuzz` and `benchmark-preprocessor-fuzz` are provisional
command names. Their final names and the harness's placement need agreement
with the #117 session before either side merges. The intended later entry
point is `test-preprocessor --fuzz`, once the expansion and output interfaces
from #122/#124 are available. The owner decides whether this independent
tooling merges before the preprocessor's first part.

## Files and interface

`main.c` includes these after the lexer and lane arenas:

- `preprocessor_fuzz.c`: bounded text construction, token comparison, the
  planted comparison adapter, line/token minimization and saving failures.
- `preprocessor_fuzz_generate.c`: generated programs and construct-family
  names.
- `preprocessor_fuzz_clang.c`: process invocation, batching and result
  extraction.
- `preprocessor_fuzz_test.c`: self-tests and the provisional test command.

`preprocessor_fuzz_benchmark.c` is a separate translation unit linked into
the same compiler executable. This is external test tooling with scalar
control flow, outside the compile path; there is no vector twin for the
harness. Tokenization uses the existing `c_tokenize` API.

`PreprocessorFuzzCase` carries source bytes, a case ID, a construct mask and
whether the case needs a separate process. `PreprocessorFuzzOutput` carries
bounded output text and separate `valid` and `accepted` flags. Normal source
rejection is a valid result. Launch, signal, I/O, capacity and allocation
failures are invalid results and cannot count as a compiler disagreement.
Result text belongs to the caller's arena.

The future preprocessor adapter should produce the same output structure
and call `preprocessor_fuzz_compare`. It compares acceptance first, and
then, for two accepted outputs, token counts, kinds and exact spellings.
Whitespace and the lexer's start-of-line bit are ignored. All preprocessing
token kinds are preserved, including `C_TOKEN_UNKNOWN`; this comparison
does not parse or validate C expressions. Two normal rejections compare
equal. An invalid result is always reported separately.

## Generator and oracle

Inputs are determined by the seed, case ID and C17/C23 mode. The generator
uses a small alphabet of names under a unique `ppf_<id>_` prefix. Its
families exercise the constructs listed in #109, including deliberately
rejected cases. Macro definitions are cleaned up, and conditional groups
and final logical lines are terminated so that one case cannot consume the
next case's markers.

The 36 families cover object/function macros, both variadic syntaxes, empty
and parameterless macros, stringification, chained and empty-argument pastes,
invalid `#`/`##` positions, variadic forwarding, C23 `__VA_OPT__`, GNU comma
elision, self/mutual references, invocations crossing replacement boundaries,
nested parentheses, unprotected bracket/brace commas, empty/missing/multiline
arguments, undefinition/redefinition, conditional directives, macro-produced
`defined`, arithmetic/bitwise/logical/conditional operators, short-circuit
dead division, `_Pragma`, line/counter builtins, splices/comments and
redefinition identity. Each case also has seed-dependent alias chains,
redefinitions and nested calls using the same eight-name alphabet.

Each of the five deliberately invalid `#`/`##` forms has its own compared
case: leading paste, trailing paste, trailing stringification, stringification
of a non-parameter, and a paste that cannot form a preprocessing token.
No other intentional error shares that case, so one rejected construct cannot
hide another construct's incorrect acceptance. The minimum coverage run
includes all five. Regression checks repair each case's single fault and
require Clang to accept the repaired input.

Arithmetic and bitwise operators have separate predicates with distinguishable
outputs for both true and false comparisons. Their bounded operands avoid
overflow, invalid shifts and division by zero. Regression checks replace each
operator expression with zero and require the accepted output token stream
to change, making a broken evaluation observable.

The oracle requires Clang 23, found in `PATH` or selected by `--clang PATH`.
It launches the executable directly with:

```text
clang --no-default-config -E -P -undef -nostdinc -std=c17 -fno-color-diagnostics -fno-caret-diagnostics -ferror-limit=1 -x c -
clang --no-default-config -E -P -undef -nostdinc -std=c23 -fno-color-diagnostics -fno-caret-diagnostics -ferror-limit=1 -x c -
```

The generated input is supplied on standard input. Default configuration
files are disabled to keep the flags reproducible; the extra diagnostic
options bound and simplify captured diagnostics without treating warnings
as rejection.

Cases share a process using distinct names and marker tokens. Both batched
and individual inputs reset the presumed line and filename with `#line`.
An errored batch is retried case by case to recover each accept/reject
result. Markers must have the expected complete token spelling and order.

`__COUNTER__` cannot be reset by a case prefix or `#line`: it is state in
the clang process. Cases using it are marked for individual execution.
This preserves their meaning and makes their results independent of prior
cases, while ordinary cases still use batching. Generated pragmas have
balanced state effects.

## Minimization

The current direct-call adapter plants a difference only when both exact
identifier tokens `BUSTER_PPF_LEFT` and `BUSTER_PPF_RIGHT` occur in the
candidate. It synthesizes two output results and uses the same comparator
as the future differential runner. Text inside comments or string literals
does not activate it.

The minimizer deletes complete lines first, then token spellings, retaining
each deletion only while the adapter reports a real difference. It preserves
remaining line boundaries and separates neighboring tokens after a deletion.
Allocation or lexing errors abort minimization; they do not preserve a
candidate. Tests expect the remaining token stream to contain only the two
trigger identifiers.

`preprocessor_fuzz_save` writes `fuzz_failure_N.c` without overwriting an
existing file. The self-tests exercise saving in a temporary directory and
remove their artifacts. Connecting real differential failures to this
minimizer is part of the later preprocessor integration.

## Commands

```sh
./build/compiler test-preprocessor-fuzz --std c17
./build/compiler test-preprocessor-fuzz --std c23
./build/compiler test-preprocessor-fuzz --fuzz 256 --seed 7 --clang /usr/bin/clang --std c23
./build/compiler benchmark-preprocessor-fuzz --std c17
```

The test command checks its components and batch/single oracle agreement;
`--fuzz` does not claim to run the unfinished compiler against clang.
The benchmark measures complete harness runs on 16 accepted family-0 cases
(object macros plus the shared generated scaffold), with generation, process
creation, temporary I/O and cleanup
included. It reports 21 interleaved-pass elapsed medians for batching and
individual processes. These are harness costs, not preprocessor throughput.

On the shared 9700X, every command above must acquire the shared lock
separately with `EXCLUSIVE_OWNER=astra ~/buster-fast-bench/exclusive.sh`.
Pin benchmark runs to CPU 2. CI runs both language-mode tests and the
benchmark, using the runner's existing lock hooks.

The full #109 acceptance work remains open: integration with the actual
preprocessor, millions of differential cases in each mode, permanent
regressions for found differences, and the default scalar/vector fuzz run
that does not need clang. None is implied by these self-tests.

## Validation on the 9700X

On 2026-10-20, using `benchpress` (Ryzen 7 9700X, Clang 23.1.1,
glibc 2.44) with one shared-lock acquisition per command:

| Command options | Checks | Failures |
| --- | ---: | ---: |
| `--std c17 --fuzz 0` | 1,139 | 0 |
| `--std c23 --fuzz 0` | 1,139 | 0 |
| `--std c17` | 1,195 | 0 |
| `--std c23` | 1,195 | 0 |
| `--std c17 --seed 99 --fuzz 128` | 1,327 | 0 |
| `--std c23 --seed 7 --fuzz 256` | 1,591 | 0 |

The unchanged `./build.sh`, every existing smoke test (including the object
writer's external cross-check), and `./build.sh check-call-graph` passed.
The call graph had 7 translation units, 1,586 functions and zero cycles.
The review fixes include main's licensing updates through `08abc25`.
Raw review logs and binary/source hashes are retained on benchpress in
`~/buster-fast-bench/astra-01a158dd-109-review-evidence/`.

`taskset -c 2 ./build/compiler benchmark-preprocessor-fuzz` measured C17,
seed 1 and 16 cases. Its 21-pass median was **5.964 ms per batch**
(2,682.6 cases/second) for batching and **86.681 ms**
(184.6 cases/second) for individual processes. These figures include the
external clang processes; they do not measure the compiler's preprocessor.
