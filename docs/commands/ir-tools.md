# ir-tools

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `ir_parse.c` reads the shared printer format with one bounded path, a
  64-byte newline scan (#2924). `test-ir-parse [--fixtures DIR]` checks canonical `.ir` fixtures
  and diagnostics; its fixtures are `tests/ir/catalog.tsv`'s cases (runtime
  data, #2053: a line of ID, FILE and NAME each, under
  `buster-fixtures v1 ir-parse`), and every `.ir` file in `tests/ir` must be
  one, so that a new fixture is its file and its line, and rebuilds nothing;
  `test-ir-optimize` runs the pass fixtures in that form.
  `test-ir-reduce` checks deterministic structural reduction, exact failure classes,
  allocation rollback and canonical replay; `reduce-ir` writes a new reproducer
  bundle without requiring the C frontend. See [IR reduction](../../docs/ir-reduce.md).
  `interpreter.c` and `ir_interpreter.c` execute typed streams and shared IR
  with explicit arena frames, the IR's calls with by-value aggregates' records
  and variadic definitions' anchors among them (#788); `interpreter_native.c`
  supplies the SysV host boundary and `interpreter_link.c` materializes data
  atoms. Their commands are
  `test-interpreter`, `test-interpreter-native`, `test-interpreter-link`,
  `test-ir-interpreter`, `interpret-ir`, and `interpret-c`. The latter runs retained
  typed streams from separately prepared C units, resolving their functions and
  data by name. `test-interpreter-link` checks which definition supplies a
  symbol's address and size, its relocations, and which reference or conflicting
  definition is diagnosed first across input units.
  `test-ssa-source --typed` adds that execution to the three IR
  forms' output/status comparison. [The tools guide](../../docs/ir-tools.md)
  records the API, text grammar and frontend-dependent acceptance. Run builds
  and tests through the approved 9700X's shared per-run lock.

## Runtime parser contracts

`test-ir-parse --fixtures DIR` selects the root containing `ir/` for all
canonical inputs, parser contracts and frozen expectations. The default is
`tests/` under the working directory. An absolute root works from outside
the checkout.

The `ir/invalid`, `ir/overflow`, `ir/reachable` and `ir/diagnostics` directories
hold the 53 rejection, five numeric-overflow, four reachability and six named
diagnostic records extracted from the C runner. Each directory's `catalog.tsv`
starts with `buster-fixtures v1 ir-parser-cases`, followed by six tab-separated
fields per row: ID, FILE.ir, ERROR, REJECTED, LINE and COLUMN. IDs and filenames
are unique within their suite; inputs are raw bounded bytes, including empty
files. A missing or malformed catalog, duplicate, unlisted file, unreadable
input or empty inventory fails the suite before any of its cases runs.

ERROR names the expected parser result (`end`, `syntax`, `limit` or `verify`
for invalid inputs; `limit-or-syntax` for overflow; `ok` or `verify` for
reachability). Named diagnostic records require `syntax`, the rejected token
spelling and its positive line and column; the runner also checks its first
byte offset in the raw input. Other records use `-`, `0`, `0` for those fields.
The original assertion loops, ordering, arena checks and verifier checks remain
compiled. Guard-page placement, generated input and independently constructed
IR models remain in C too.

The ordered contract results retain their frozen expectation in
`ir/parser-digest.expected`: sixteen lowercase hexadecimal digits and a final
newline. Adding or changing a contract requires reviewing the expected parser
result and this golden together; both are runtime data and neither triggers a
host compile, link or table bake. The digest covers the same results and IR
columns as before the extraction. Canonical round-trip cases retain their
separate raw-byte and opcode/type-coverage checks.
