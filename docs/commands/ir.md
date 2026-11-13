# ir

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/ir.c` supplies Tier 1's per-function arena-backed instruction
  columns, scalar and 16-wide verifiers, compact CFG/use arrays and stable printer
  (#131), with labels (`br_indirect`), by-value aggregates' ABI records (the
  producer's locations, which the IR's types can't give), a variadic
  definition's frame and anchors, and frame slots' lifetime markers (`lifetime`,
  #177, from `Tier0Function.lifetime_entries`). `ir_lower.c` lowers the `Tier0Function` stream
  into the all-in-memory form for SSA promotion: frame objects as address-taken
  slots of their size and alignment, `COPY` as integer load/store pieces, the
  dynamic stack's operations, indirect calls, labels and `JUMP_INDIRECT` (every
  jump through the function's one dispatch block, as clang's indirect gotos go,
  which keeps an interpreter's CFG linear), aggregates by value, variadic frames,
  long double and `__int128`; it refuses vectors, `X86` rows, the frame
  address (`FRAME_ADDRESS`) and tail calls (`TAIL_CALL`). These capability
  refusals use `IR_ERROR_UNSUPPORTED`: ordinary `--tier1` may fall back to
  Tier 0, while `--tier1-strict` rejects them without an internal error.
  `test-ir` checks
  independent fixtures, guarded columns, malformed inputs and allocation
  rollback; `test-ir-lowering` also executes generated IR (values, objects,
  indirect calls, records by value, the dynamic stack) against the typed-stream
  interpreter and Tier 0's native code, and Tier 0's long double and `__int128`
  programs through the shared IR's interpreter against their stack machines.
  `test-ir-source` lowers, compares and promotes every function `-c` compiles of
  real C files (`ir_source_test.c`), and `benchmark-ir-source` reports their IR's
  bytes per source token and the largest functions' shares, and times lowering,
  verification and promotion on them (`ir_source_benchmark.c`). `benchmark-ir`
  measures verification and derived-array rebuilding on synthetic functions.
  `benchmark-ir-lowering` measures the typed-stream producer through output
  verification on generated programs. [The IR guide](../../docs/ir.md) records the
  producer/consumer contract and the corpora's results.
## IR tests and benchmarks

`./build/compiler test-ir-metamorphic [--seed N] [--first N] [--count N]`
checks bounded scalar IR programs against an independent arithmetic/memory
model after ID renaming, pure-value reordering/insertion, CFG edge splitting,
unreachable-block insertion and commutative operand exchange. Both complete
verifiers, both text parsers and every admitted scalar/vector optimizer must
preserve checked execution. The default six-case seed is in shared correctness
CI; longer runs accept explicit time/step budgets and failure-artifact/replay
options. `docs/ir-tools.md` records the precise subset, categories and source
revision requirement. This command performs correctness work, not a timing claim.

`./build/compiler test-ir [--seed N] [--count N]` checks the arena-backed
representation, both verifier paths, compact CFG/use arrays and stable printer.
Independent fixtures cover operand roles, conversions, phi edges and dominance;
guard pages cover instruction columns and vector tails. Malformed metadata,
u32 limits, allocation rollback and adversarial switch cases are rejected or
handled without relying on twin agreement alone. Generated multi-block
functions with phis must give both paths the first use error, or none, that an
independent dominance oracle gives (four per count, #786). The default random
count is 64; `--count 1024` expands the seeded mutation cases.

`./build/compiler test-ir-lowering [--seed N] [--count N]` lowers generated
typed programs through both paths, compares every column and verifies each
function. A test-only executor with explicit call frames compares results and
global-memory effects with the existing typed-stream interpreter and with Tier
0's native code of the same streams, run in a child process. It runs `--count`
programs of values and as many each with frame objects and copies, with calls
through pointers to functions and with records passed and returned by value,
whose IR the executor runs as its ABI records say. Targeted fixtures exercise
conditional joins, entry-zero backedges, narrow shifts, wide pointer scales,
objects' slots, `COPY` of every size from 1 to 256, labels and computed gotos
(one, and two through the dispatch block), a variadic definition's frame, a struct by value's records, guarded source tails
and transactional failures. A loop whose `?:` joins carry a U64 and an I32 at
one depth (#1189), and the same loop in a statement expression under a pending
U64, whose slot then crosses the backedge, must lower to a depth slot per depth
and type, each reached by one type alone, compute what C does, and promote to no
slot, load or store. A symbol's address taken more than once is one value
(#2574) defined where `ir_typed_plan_places` puts it (#2593): past an early
return, at a branch every path from which takes it, before a loop that takes
it, and not at all where a path takes it nowhere or only a loop's exits do,
each checked by its blocks and the function's results. Every lowered function is also promoted by both SSA
paths, verified, and must keep its objects' slots. Tier 0's random long double
and `__int128` programs (four times the count) run through the shared IR's
interpreter (#176) against their stack machines. The default count is 32
programs.

The optional `Tier0Function.table_addresses` column (#1217) identifies direct
jump-table symbol addresses to materialize once in the entry preheader. Its
ordered indices, entry kinds, classes and symbol access are validated before
lowering; inputs without the column keep ordinary entry placement. Targeted
tests cover two tables, mixed computed gotos, unreachable tables, a table
backedge to source entry zero, and malformed columns with output/scratch
rollback. [The lowering guide](lowering.md#jump-table-address-reuse-1217)
records the producer contract and the native dispatch check.

`./build/compiler test-ir-source [--print NAME] [preprocessor options] file...`
(`ir_source_test.c`) is #131's acceptance on real C: each file checked, selected
and emitted as `-c` compiles it (`ir_source_prepare`, as benchmark-backend-source
prepares its files), then every function's typed stream lowered by both paths,
which must agree column for column and verify, and promoted by both of
`ssa_ir.c`'s paths, whose results verify with dominance. A function's semantic
result type is its RETURNs' class, or its declared result's
(`ir_source_result`). A file the checker rejects, or whose program or emission
refuses a feature by name, is left out with its reason; any other failure, and
any function refused, lowered apart or unpromoted, fails the command, naming the
function and the first error. `--print NAME` prints that function's IR as lowered
and as promoted. The correctness list runs it on c-testsuite's subset, the frozen
`sqlite3.c` and the frozen musl's units.

`./build/compiler benchmark-ir-source [--only lower|lower-scalar|verify|
verify-scalar|promote|promote-scalar] [--after-scalar MS] [preprocessor options]
file...` (`ir_source_benchmark.c`) prepares the files' functions as test-ir-source
does, lowers and promotes each once outside the time, and reports the IR's bytes
(each function's arena, alignment gaps and all, as an arena of its own) per
source token (the parser's), per typed entry and per instruction, lowered and
promoted, and the ten largest functions with their shares; then 21-pass medians
per typed entry and per IR instruction of each variant over every function, each
pair's paths taking turns first. Any file or function that fails fails it. Run it
under the 9700X's lock with `taskset -c 2`; the 9700X job runs it on the frozen
`sqlite3.c` where the IR or its lowering changes.

`./build/compiler test-ir-source --benchmark-self-test [preprocessor options]
file...` prepares the actual source benchmark's corpus, including its twin
lowerings, promotion, GVN and optimizer checks, then runs both measured lowering
paths once. It opens no performance counters and reports no timings. Unlike
benchmark commands, this correctness mode also runs on baseline host builds.
Put `--benchmark-self-test` first. Shared correctness CI checks the classic bit
builtins with `-march=x86-64` and `-march=znver5`, including functions that add
and remove BMI, LZCNT and POPCNT through their target attributes (#1374).

`./build/compiler benchmark-ir [--instructions N] [--passes N] [--only
scalar|vector|uses|cfg] [--after-scalar MS]` measures the verifier and derived
builders on small and large synthetic functions. `benchmark-ir-lowering
[--seed N] [--passes N] [--only scalar|vector] [--after-scalar MS]` measures
the complete available typed-stream-to-verified-IR path on generated mixed
programs: small and larger value-only ones, and the larger shape with frame
objects and copies, reported apart. Both require optimized builds, calibrate samples to at least 2 ms,
rotate variant order and report 21-pass medians by default. TSC ticks are
labeled separately from core cycles. The 9700X workflow runs the tests and
pins each benchmark to one allowed CPU; it adds the runs after 200 ms of scalar
work before each vector sample where the pull request changes the module
([Sharing the 9700X](ci.md#sharing-the-9700x)). [The guide](../../docs/ir.md) records
interfaces, measurement scope and the corpora's results.
