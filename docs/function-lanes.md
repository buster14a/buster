# A unit's functions on lanes

[#151](https://github.com/buster14a/buster/issues/151): the C driver compiles a
translation unit's functions on a group of lanes. Each function's lowering to
Tier 0's typed stream, its Tier 0 code, branch sizing and encoding are a lane
item. The link the functions' code goes into, and the object or executable
made from it, are byte for byte what one lane makes, whatever the lane count
and whichever lane finishes first ([#153](https://github.com/buster14a/buster/issues/153)).

The driver runs it for `-c` and for the direct path's executables:
`./build/compiler [--lanes N] ...`. By default it may run one lane per CPU the
process may run on, and runs as many as the bodies' tokens pay for (below);
`--lanes N` runs N, as long as the unit has bodies for them.

## The phase

`check_program_emit_lanes` (`c_check_program_emit.c`) is the emission. Its
steps are source order's (`check_program_emit` before #151), cut differently:

1. **Before the lanes**, on the calling thread: the lowering's preparation
   (`check_lower_prepare`, `c_check_program_lower.c`): the helpers, the errno
   accessor, every type's ABI classification, and the tables a body's lowering
   reads or writes by token and object (`CheckLowerShared`); and the access
   column Tier 0 reads (`Tier0Function.symbol_access`).
2. **The lanes**: the bodies are cut into parts of consecutive bodies by their
   tokens (`lane_parts_by_cost`) and taken costliest first (`lane_take`). A
   lane takes a part's steps in source order's order for the whole program:
   its bodies' lowering (`check_lower_part`), its calls' signatures assigned
   together (`abi_assign`), each body's parameters and `tier0_validate`'s
   check; then each body's Tier 0 code (`tier0_compile`) and, while the part
   has no failure, its code after Tier 0 (`check_program_emit_code`): branch
   sizing, encoding into the lane's bytes, its unwind frame, its labels'
   offsets and with `-g` its raw rows, and what its fixups need. A lane makes
   its writer (`check_lower_writer_begin`) when it takes its first part, so a
   lane that takes none costs nothing more than its start.
3. **The merge**, on the calling thread: the lowering's result
   (`check_lower_finish`), the attributes' checks, the link's capacity,
   names and symbols, the C runtime's binding (`bind_begin`, for an
   executable), then each body's atom, symbol definitions and fixups in source
   order (`link_add_atom`, `link_define_symbol`, `link_add_code_fixups`), the
   helpers', and the unwind tables.

A body writes only its own entries of what the lanes share: its
`Tier0Function` and code, its frame, its result and its tokens' and objects'
entries of the lowering's tables (`local_map`, `switch_locals`,
`save_locals`, whose `local_map` entries it puts back before it ends).

### What a part numbers

Two things source order numbered as the bodies went, which a lane can't know:

- **The runtime's functions.** The lowering calls libgcc's (`__divti3`,
  `__mulsc3`, ...) where Tier 0 has no entry, and numbers each one's symbol
  at its first call, after the program's and the helpers'. A writer gives a
  call of one a provisional symbol of its own (`CheckLowerShared.provisional_runtimes`,
  past the helpers'), whose access is its final symbol's, and records the
  functions its part calls first, in the order it does. `check_lower_finish`
  numbers them part by part in source order, as source order's lowering would,
  and the merge puts the final symbols in the code's references before the
  link takes them. A unit's own declaration of one of them stays its symbol,
  found once before the lanes.
- **The label symbols** (GNU C's `&&label`). A part's go to its writer's room
  for them, after its earlier parts'; `check_lower_finish` puts the parts'
  together in source order.

### Failures

The failure reported is source order's, at every lane count. Source order's
emission takes its steps for the whole program in turn: every body's
lowering, the label symbols' check, the locations' room, every call's
convention, every body's frame, the attributes', every body's Tier 0 code,
the rows' room, the link and its binding, then each body's code. A part takes
the same steps, stops at its first failure (but past a failure after Tier 0 it
goes on with Tier 0 alone, whose failures come first), and records its step;
the merge reports the earliest step's failure, its first body's. A failure the
convention's check finds is at the token source order's writer had after the
last body, the last part's.

A resource's failure on lanes, or lanes that can't start, run the emission
again on the calling thread alone, from the arenas as they were: one lane's
arenas, the caller's, decide whether a unit fits. `CheckProgramLanes.again`
says why it ran again (`CheckProgramAgain`): lanes that didn't start
(`lane_run`, which also runs lanes it can't pin, then says so), or a part that
ran out of room in its lowering (or whose lane had none for its writer), in a
body's Tier 0 code or in a body's code after Tier 0.

### Memory

Lane 0 is the calling thread, with the caller's arenas, so one lane's emission
pushes what source order's did, where it did. Each other lane has three arenas
of its own (`CheckProgramLanes.arenas`): its functions' lowered columns, its
writer's columns with its functions' Tier 0 code and what the merge reads, and
its functions' bytes. The caller keeps them until it is done with the
emission's link (`check_program_lanes_release`); the next emission with them
puts them back.

A lane's writer has some thirty columns, each with room for any of the
program's bodies, far apart in its scratch arena, of which a lane touches a
little at each column's start. In huge pages, each column's first touch cleared
a 2 MB page, and each lane's arenas are new: on the 9700X, 8 lanes' emissions
of `sqlite3.c` took 6.7 billion kernel cycles against one lane's 0.17, and
were slower than 4 lanes'. The lanes' scratch arenas are in base pages
(`arena_reserve_small`), so a lane clears only what it touches: 0.58 billion.
The functions' columns and bytes are packed, and keep huge pages past their
first 64 KB.

## The lane rule

One lane, and one more per `BUSTER_CHECK_EMIT_LANE_TOKENS` of the bodies'
tokens beyond twice the largest body's, no more than the bodies (a part has at
least one) or the lanes the caller allows. A lane costs its thread's start,
its writer's columns and its arenas' first pages, which its share of the
bodies must pay for; and the lane that takes the largest body takes at least
as long as it. #113's deferred reader sizes its lanes the same way
([docs/c-deferred-bodies.md](c-deferred-bodies.md)).

## Tests and measurements

`./build/compiler test-check-lanes [--executable PATH] [--seed N]
[--programs N] [--keep]` (`c_check_lanes_test.c`):

- generated programs of up to 600 functions of many kinds, among them
  libgcc's functions in random first-use orders, computed gotos through static
  tables of labels' addresses, switches, structs by value, long doubles and
  variadic calls, emitted on one lane and on 2, 3 and 8 lanes, on 8 with one
  lane slow, and on 8 that can't be pinned (CPUs past every one `lane_pin`
  takes), with `-g`'s rows and unwind tables and without: each the one lane's
  object, byte for byte, by each path, run again on one lane only when its lanes
  didn't start;
- the lane rule on programs of 1 to 1,200 functions;
- failing programs whose bodies fail at different steps in both orders: one
  lane's failure at every lane count;
- a lane short of room (`CheckProgramLanes.squeeze_body`, the tests' alone):
  the lane that takes a body's part has only so many bytes to spare in one of
  its arenas while it makes its writer and works on the part. In a generated
  program of its own seed and in the failing programs, by each path, with `-g`
  and without, the bodies with the fewest and the most tokens are squeezed in
  each arena from no room up, doubling, until the part fits: each emission one
  lane's object or failure (the generated program's an object, the failing
  programs' a failure), the sweeps reaching each reason a lane short of room
  has, on lane 0 and on the others;
- lanes that start no thread, in a child limited to the processes it has
  (`RLIMIT_NPROC`, which doesn't limit root), under an alarm that ends it if
  its lanes hang: one lane's object or failure;
- the driver's objects and executables by `--lanes 1`, `2`, `3`, `8` and by
  default, by each path: the same bytes, and executables that exit alike; with
  lanes that start no thread, by `--lanes 8`, `--lanes 1`'s objects and
  executables; and the failing programs' exit statuses and diagnostics by
  `--lanes 8`, with lanes and without, `--lanes 1`'s.

The lanes' serial retry, which the last three exercise, was otherwise untested
([#538's review](https://github.com/buster14a/buster/pull/538#pullrequestreview-5664568010)).
Mutants of the emission that ignore each of its reasons to run again, or that
never run again, each fail the test.

`./build/compiler benchmark-check-lanes [--passes N] [--lanes LIST] [--cpus
LIST] [preprocessor options] file...` (`c_check_lanes_benchmark.c`) times the
emission of a file's checked program on each lane count and by the rule, with
the steps apart (`CheckProgramLanes.timing`).

### The 9700X

On the 9700X under its lock, lane 0 on CPU 2 and the others on one CPU of
each other core (`--cpus 2,0,1,3,4,5,6,7`), medians of 11 runs. Clang 23.1.1
built the compiler with `build.sh`. The frozen `sqlite3.c` uses the CI step's
options (2,602 functions, 554,386 tokens in their bodies, the largest 28,522).
Times are in milliseconds.

| Lanes | Emission | Speedup | Preparation | Lanes' part | Merge |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 36.6 | 1.00 | 3.0 | 31.1 | 2.4 |
| 2 | 22.6 | 1.62 | 3.0 | 17.1 | 2.5 |
| 3 | 17.5 | 2.09 | 3.0 | 12.0 | 2.5 |
| 4 | 15.0 | 2.43 | 3.0 | 9.6 | 2.5 |
| 6 | 12.9 | 2.84 | 3.0 | 7.3 | 2.5 |
| 8 | 11.9 | 3.06 | 3.0 | 6.4 | 2.5 |
| The rule's (8) | 12.2 | 3.01 | 3.0 | 6.6 | 2.5 |

The lanes' part takes 4.9 times less time on 8 lanes than on one. The
preparation and the merge stay serial, about half of 8 lanes' time; most of
both is passes over every token of the unit.

The whole driver, `-c` of #136's frozen copy of `sqlite3.c`, 11 rounds in a
rotating order, against main (`e04462c1`). The objects are byte-identical:

| | Median | CPU time |
| --- | ---: | ---: |
| main, on CPU 2 | 192.8 ms | 191.5 ms |
| `--lanes 1`, on CPU 2 | 193.7 ms | |
| by default, on CPUs 0 to 7 (8 lanes) | 168.6 ms | 219.7 ms |
| `--lanes 4`, on CPUs 0 to 7 | 171.7 ms | |
| by default, on every online CPU (15 lanes) | 170.0 ms | |

Eight lanes take 12.6% off the compile. One lane runs 0.02% more user
instructions than main (`perf stat`, 2,290.7 million against 2,290.2), and its
times are within the rounds' spread of main's (±0.5%). Lanes on both SMT
siblings of a core cost about 1 ms against one lane per core, which isn't worth
reading each CPU's topology for in every compile.

The rule also holds on smaller units. On generated programs of 403 functions
(26,892 tokens in their bodies, the largest 6,034), it takes 4 lanes: 0.66 ms
against 1.51 on one lane, 2.29 times faster, where 6 lanes are the best at
2.48. On programs of 603 functions (40,125 tokens), it takes 6 lanes: 0.90 ms
against 2.29, 2.53 times faster, where the best is 2.69, also on 6.
