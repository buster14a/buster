# The checker's binding handoff

[#336](https://github.com/buster14a/buster/issues/336): how `check_input` gives
every identifier use its declaration, now that #128's parser adapter
(`ParseBindings`, [#320](https://github.com/buster14a/buster/pull/320)) and the
checker's own reader both produce binding events. `c_check_binding.c` holds the
handoff, `c_check_binding_test.c` its tests and `c_check_binding_benchmark.c`
`benchmark-checker-bindings`.

## One producer

Typed elaboration needs immediate visibility in source order: a typedef name
changes how a declaration parses, a redeclaration needs the visible earlier
one, an enumerator's value can name an earlier one, and linkage joins a block's
`extern` with the file's definition. So `check_input`'s reader is the
producer, and it can't be replaced by an untyped one. It records, in
`reader->bindings` (`CBindings`, `c_bindings.c`):

| Event | When the reader records it | Token |
| --- | --- | --- |
| `DECLARE`, ordinary | after a declarator is complete, before its initializer (`check_input_bind`); an enumerator after its definition (`c_check_enum_input.c`) | the declaration's name |
| `DECLARE`, parameter | in the prototype's scope, as its declarator completes, so a later parameter's bound sees it; again with the same token when the definition's body opens | the parameter's name |
| `DECLARE`, tag | after the tag's name, when no visible tag of the scope is redeclared (`check_input_tag_identity`) | the tag's name |
| `USE`, ordinary | for each identifier operand the reader reads that isn't a member, a tag, a builtin call or an intrinsic (`check_input_resolve`, and the constant readers in `c_check_constant.c`) | the use |
| `RESTORE` | when a block, body, `for` or condition scope closes, with the mark of its opening | none (its mark) |

Labels have no events: the control readers resolve them per function
(`label_targets`). Tag uses take their canonical declaration during
elaboration, from a lookup at that point. Raw attribute arguments and asm
operands keep the identity captured during elaboration.

Points are token positions and never decrease, so the events are in source
order. Capacity is `4 * tokens + 1` events and three head arrays of the
interned names' count, in the reader's scratch. Exhaustion is sticky: the
stream becomes invalid, and `check_input` reports a resource failure.

## Identity: token, event, declaration

`c_bindings_append` stores in every event's `previous` the head of its name in
its namespace when the event is recorded. For a `USE` event that head is the
declaration the use sees: the activation event + 1, or 0 for none, which is
what both resolvers return for it. So:

- a use token `t` has a `USE` event `e` with `declaration[e] == t`;
- its answer is `h = previous[e]`, with `h <= e`;
- its declaration token is `d = declaration[h - 1]`, of kind `kind[h - 1]`,
  and `CheckInput.declarations[t] = d`.

A parameter reactivated in its body keeps its token, so its body uses and its
prototype uses get the same `d`. A closed scope's declarations stop being
heads at its `RESTORE`, so a later use sees the earlier one again.

## The modes

Every mode feeds one consumer, `check_input_binding_answers`, which first
checks every answer (a use names a token, sees nothing at or after itself, and
sees a `DECLARE` of a token), then publishes each use's declaration, its type
at that point and its reports:

- the type is the declaration's, unless an incomplete array's earlier type
  applies at the use, or elaboration already captured a use-specific type for
  that declaration (a later completion doesn't change an earlier expression);
- a use whose declaration has no type, a typedef name used as an operand, or a
  captured identity that the answer contradicts is reported as a declaration
  error; a use without a declaration is reported undeclared.

| Mode | Answers from | Extra work | Extra memory |
| --- | --- | --- | --- |
| `direct` (default) | each use's retained `previous` | none | none |
| `stacks` (`--scalar`) | `c_bindings_scalar`'s replay of the events, which reads neither `previous` nor the heads | one pass over the events | replay heads (3 × names × 4 bytes) and 3 × events × 4 bytes |
| `batch` | `c_bindings_batch`'s radix sort and scan | sort and scan | sort keys and scan columns |
| `parser-stacks`, `parser-batch` | #128's `ParseBindings`: the scalar grammar replayed with a trace, its events resolved, and its declaration column mapped onto the reader's uses (with the tag column checked against the reader's tags) | a second parse of the unit, a resolution and the mapping | the parser's events and columns, in the output arena until the handoff ends, and the trace in scratch |

`check_input` and its wrappers take `CHECK_INPUT_BINDINGS_DEFAULT`: `direct`,
or `stacks` when the parser was asked for its scalar twin. The other modes are
reached only through `check_input_bindings_mode`, which the tests and the
benchmark use. Before #336, the default was `parser-batch` and `--scalar` was
`parser-stacks`.

The direct answers aren't a vector path; they are no path at all. The stack
replay stays as the scalar twin because it is independent: it rebuilds every
answer from the events alone, and the source tests require it to agree with
the direct answers, the batch sort and the parser's replay on every token.

## Ownership, lifetime and errors

- The events, their heads and `previous` live in the reader's scratch for the
  call. `check_environment_publish` copies the events (without `previous`, the
  heads or the undo log) into the output arena as `CheckInput.environment`,
  #113's semantic history, which the binding tests replay with both resolvers.
- `stacks` and `batch` take their results and work arrays from scratch and pop
  them before returning. The parser modes borrow the parser through a local
  view whose scratch is the checker's, and pop everything they pushed.
- Invalid events, a resolver's allocation failure or an invalid answer set
  `CheckInput.resource` and publish no use's declaration, type or report.
  Undeclared uses and declaration errors are ordinary reports, in token order.
- `binding_event_count` and `binding_statistics` describe the reader's events
  in every mode (identifiers and declarations count events, so a reactivated
  parameter counts twice); only `batch` counts candidate walks and fallbacks.
  `parser_binding_event_count` and `parser_binding_statistics` are filled only
  by the parser modes, and are zero by default.

The parser's adapter itself is unchanged and still opt-in. The lazy
system-header planner (#155, `c_check_lazy.c`) still builds `ParseBindings`,
because it decides which bodies to check before the reader runs.

## Tests

`test-checker` runs `check_test_bindings`:

- source fixtures (shadowing and restoration, closed siblings past the batch
  sort's walk limit, `for` scopes, redeclarations, an initializer naming its own
  declaration, declarator bounds, `typeof`, an incomplete array completed later,
  builtin typedefs shadowed, typedef names in prototypes and blocks, tags and
  their shadowing, enumerators, parameters in prototypes and bodies, a block's
  `extern` linked to a later definition, labels beside an ordinary name, and
  undeclared uses): each through both parser paths, then every explicit mode on
  a fresh type table, which must give the default run's acceptance, reports,
  declarations, types, graph size and event statistics. The batch sort must walk
  and fall back on the closed-sibling fixtures; no other mode may count walks;
- the parser's own columns as the syntactic oracle: its replay's immediate
  answers, its stack and batch resolutions and the checker's published
  identities must agree on every ordinary and tag use;
- the published history replayed by both resolvers into the same declarations;
- random event streams of 0 to 64 events with every column the handoff reads
  or writes ending at a `PROT_NONE` page, through the three event modes, which
  must agree;
- each event mode's failures: an undeclared use, a stale capture, scratch
  exhaustion (which `direct` doesn't need), invalid events and an empty stream;
  invalid answers (a use seeing itself or a later event, a handle naming a use or
  a restore, a declaration outside the tokens), each refused before anything is
  published; and the parser mode's allocation failures, which leave the parser
  and both arenas as they were.

`test-checker-corpus` compares the two default paths, now `stacks` and
`direct`, with each other and their acceptance and first diagnostics with
Clang's.

## Benchmark

```sh
./build/compiler benchmark-checker-bindings [--passes N] [--only MODE [--after-scalar MS]] [-E's options] {file.c | --input NAME}
./build/compiler benchmark-checker-bindings --input NAME --print
```

It preprocesses and parses once, outside timing, and requires the checker to
accept the unit. Every mode's run on a fresh type table must publish the
default run's acceptance, reports, declaration, type, kind, object, linkage,
definition and node columns and graph. Then it times whole `check_input` calls,
21 interleaved passes after a warm-up by default, the order rotating by pass.
A sample includes reading, elaboration, the handoff, expression graph
construction, initializer validation and history publication; the type table's
initialization and the arenas' reset (their pages stay committed) are outside
it. The modes differ only in the handoff, so their differences are its cost.
`--only MODE` times one mode back to back, and `--after-scalar MS` runs the
scalar twin's whole reader (`stacks`) for MS ms before each sample.

It reports:

- denominators: tokens, the reader's events (declarations, uses, restores),
  the distinct names they bind, and the parser replay's events;
- per mode: median, minimum and maximum nanoseconds, cycles where the counter
  is available, per token, event, use and distinct name, the difference from
  `direct`, walks and fallbacks, the output arena's retained bytes, and the
  highest byte each arena's run writes (a lower bound on its peak: a transient
  push that a later retained one writes over doesn't show);
- isolated pieces on the run's retained events: the reader's stack replay and
  batch sort, the parser's grammar replay, and its two resolutions, each with
  its own footprint past its arenas' lengths (exact for what it writes).

The 9700X job runs it on every pull request on the small and header-heavy
inputs and with 5 passes on frozen SQLite; where the checker's sources change,
it adds three processes of the large input and SQLite, and SQLite's `direct`
and `parser-batch` alone and after 100 ms of scalar work.

### Inputs

The synthetic inputs are built in (`--input NAME`; `--print` writes one out),
so that no measurement input lives under `docs/measurements`:

| Input | What it is | SHA-256 of `--print` |
| --- | --- | --- |
| `small` | 4 functions of the checker's source-measurement generator (`generate_checker_source.c 4`, now in buster-performance), byte for byte | `be568cb5db8dd471edc19be730653ce43623143c033ccc8cc883b640e263dcee` |
| `large` | the same generator's 4,096 functions | `0e33678843ad025277ad6bf0c918c556e526d9421ca78c3f42ab3dd5f5dc47a7` |
| `headers` | 24 C library and POSIX headers and four functions that use and shadow their names | `b62447a1cbdccfcb310f3c43f644c081e8af7c720c7565d9184c22c5a25518e0` |
| SQLite | the frozen amalgamation on the 9700X, `$corpus/sqlite/sqlite3.c` | recorded in [the preprocessor oracle guide](preprocessor-oracle.md) |

The checker doesn't accept `main.c` yet (Clang's `_Float16` intrinsic
headers), so `headers` stands in for a header-heavy unit. Its headers, and
therefore its counts, are those the search options find.

## Measurements

### Development host (not Zen 5)

An Intel Xeon VM at 2.1 GHz, Clang 23.1.5, no hardware counters, SQLite
3.50.4's amalgamation and Ubuntu 24.04's headers. These show direction only;
the decision rests on the 9700X's numbers below.

| Input | Tokens | Reader events (uses) | Distinct names | Parser events | `direct` | `stacks` | `batch` | `parser-stacks` | `parser-batch` (old default) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| small | 243 | 94 (48) | 9 | 118 | 31.5 µs | 31.7 µs | 32.2 µs | 41.3 µs | 42.2 µs |
| headers | 52,160 | 9,263 (81) | 3,363 | 12,185 | 7.32 ms | 7.02 ms | 7.25 ms | 9.29 ms | 9.54 ms |
| large | 237,579 | 94,210 (49,152) | 4,101 | 110,602 | 34.3 ms | 34.4 ms | 34.3 ms | 44.0 ms | 45.2 ms |
| SQLite 3.50.4 | 684,797 | 159,421 (93,280) | 9,453 | 153,880 | 150.5 ms | 162.4 ms | 151.4 ms | 191.8 ms | 182.1 ms |

Medians of whole `check_input` calls, 11 passes (SQLite 5); `small` and
`large` with `-std=gnu17` alone, `headers` and SQLite with `-march=native` and
the system's search directories as well. The three event
modes are within each other's spread (SQLite's samples ranged from 143 to
195 ms); the parser modes cost 21% to 30% more on every input, past the spread
(on SQLite, `parser-batch`'s fastest sample, 176.9 ms, was slower than every
`direct` sample, at most 173.4 ms). Isolated, on SQLite: the parser's grammar replay took
29.3 ms, its stack resolution 2.6 ms and its batch 5.2 ms; the reader's stack
replay 0.92 ms (5.8 ns/event) and its batch 2.95 ms (18.5 ns/event). On the
large input the parser's replay wrote 30.9 MB of output and 27.3 MB of
scratch; the reader's stack replay 0.81 MB of scratch and its batch 1.88 MB.
The sort doesn't earn its cost anywhere: the reader's events are in source
order already, and the stack replay is 3 to 5 times faster.

A small unit's call is dominated by per-call setup that grows with the interned
names, not with the unit: `small` took 31.5 µs without target options and
136 µs with `-march=native`'s predefines, whose macros' names the reader's
per-name arrays (the binding heads, the parameter marks) clear on every call.
That cost is the reader's and the same in every mode; it is outside #336.

### Ryzen 7 9700X

Pending: the 9700X job's run on this change records them.
