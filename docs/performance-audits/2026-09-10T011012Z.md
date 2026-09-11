`2026-09-10T011012Z` (GitHub Ubuntu 24.04, virtual EPYC 7763, Clang
18.1.3; **Z05 literal-call census and single-fragment scratch candidate**).

## Status and reconciliation

Baseline: `ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae`. This entry records an
untimed production-call census and a small, not-yet-performance-accepted
candidate for [#57](https://github.com/buster14a/buster/issues/57), under
[#128](https://github.com/buster14a/buster/issues/128). It does not claim a
Zen 5 speedup. Candidate correctness, paired performance and final-SHA checks
belong to the linked implementation PR and must complete before acceptance.

The migration map identifies historical Forgejo #612 as GitHub #57, and
Forgejo #601 as GitHub #46. Current GitHub #148 is not remapped. A02 owns that
issue's integer grammar/overflow unification. This change does not modify
`c_conditional_number`, the ext80 reader, suffix validation, integer type
selection, floating conversion, or the adjacent short-circuit evaluator in
PR #357. PR #362 changes machine-code/ABI work, not this range decoder.

The existing narrow decoder already classifies masked 64-byte windows through
`simd.h`, copies plain prefixes and delegates escapes to the authoritative
scalar reader. The narrow count-only path already avoids materializing bytes.
Those parts of the historical issue are completed work, not missing features.
The scalar differential reference and portable dispatch remain unchanged.
PR #160's three immediate-reuse sites were cold on its frozen stage-1 input;
its dense-escape and tiny-literal losses are not evidence for another universal
SIMD fast path. That experiment is not retried here.

The preflight export followed every API pagination link: all-state issue pages
100/100/100/63 (137 open issues), PR pages 100/36 (two open PRs), issue-comment
pages 100/10, and no inline review comments. Open PR file diffs/review submissions
were exported. Relevant current source, test seams, migration map, frontend,
SIMD, parallelism, benchmarking, build, testing and style guides were read;
the newest baseline audit was `2026-09-09T163649Z`. Symbol/history searches and
relevant earlier audits informed scope. This is a timestamped snapshot, not
an atomic ownership lock or a claim to have read every historical discussion.

## Evidence and provenance

Read-only source/history snapshot:
https://github.com/buster14a/buster/actions/runs/34422885275

Baseline self-host plus census run:
https://github.com/buster14a/buster/actions/runs/34423891006

The latter's `z05-evidence-34423891006` artifact has SHA-256
`74e9d6aebd83dfced69f5789849c3824a52918dd61a453dec549a8d25d9ecaef`.
It retains configure/build/self-host logs, exact commands, CMake caches,
compiler hashes, the unapplied observer patch, environment, source/generated
input hashes, complete raw traces, allocation reports and disassembly.
Retention is seven days; the auxiliary branch
`astra/z05-literals-20260910-transport` retains reproducible CI instructions.
No transport workflow or diagnostic hooks are part of the implementation PR.

The runner reports four vCPUs, an AMD EPYC 7763, Microsoft virtualization,
SMT active, Ubuntu Clang 18.1.3 and Linux 6.17.0-1022-azure. Affinity was 0-3.
This is not physical Zen 5, not a Zen 4 nonregression run and not an isolated
physical-core benchmark. PMU, IBS, effective-frequency, downclock and physical
SMT-sibling evidence were not collected and are unavailable, not zero.
No local compiler builds or tests were used.

CI first built an untouched Clang Release compiler with the repository build
driver, split compilation and at most two build jobs, then ran ordinary
`test_self_host`. Only after that work finished did it build a separate
`BUSTER_BENCH_ALLOCATIONS=ON` observer. Generation used a separate directory.
The diagnostic patch adds bounded, checked streaming records around production
readers and scoped preprocessing/syntax/semantic/lowering entry points; it does
not replace their parsing rules. This observer is serial-compiler-only. Its
I/O, allocation hooks and wrapper costs disqualify it from timing evidence.

Both compilers consumed the same archived baseline source and copied generated
headers, with the same working directory, flags and output pathname. All seven
object comparisons and allocation-report completeness checks passed. The
recorded source/generated hashes were rechecked after the runs. The system
header closure was shared on the runner but not separately frozen/hashed;
this is a limit of this diagnostic run, not a complete acceptance manifest.

Inputs were `basic_c_operations`, `basic_c_string_concat`,
`basic_c_nested_string_initializers`, `basic_c_plain_char_literal_sign`,
`basic_c_float_literal`, `basic_c_static_float_literal`, and the complete
`src/buster/apps/ide/ide.c` unity source. The first six are existing fixtures,
not a new synthetic timing suite. Commands used:

```sh
z05-build generate --cc clang --ci --linker DEFAULT -- \
  -DBUSTER_UNITY_BUILD=OFF -DBUSTER_INCLUDE_TESTS=OFF
z05-build build --config Release -t ide -- -j2
z05-build test_self_host --config Release
z05-build generate --build-directory build-probe --cc clang --ci \
  --linker DEFAULT -- -DBUSTER_UNITY_BUILD=OFF \
  -DBUSTER_INCLUDE_TESTS=OFF -DBUSTER_BENCH_ALLOCATIONS=ON
z05-build build --build-directory build-probe --config Release -t ide -- -j2
ide cc -g0 -O0 -fregister-allocator=none -c tests/basic_c_string_concat.c -o current.o
ide cc -g0 -O0 -fregister-allocator=none -c -Isrc -Ibuild/generated \
  -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 src/buster/apps/ide/ide.c -o current.o
```

The final two commands were repeated with the separate observer and
`BUSTER_Z05_TRACE=<trace> BUSTER_ALLOCATION_CENSUS=1`, followed by `cmp`,
SHA-256 capture and the existing `tools/allocation_census.py` reader. Exact
absolute paths and all fixture commands are in `commands.txt`.

## Whole-compiler population, not timing

The frozen compiler-input trace contains 728,089 records and hashes to
`df66c9021ef396518875be0cc8bbd73d7a633ef91f8af676e619b4125dc4aec9`.
The identical normal/observer output object hashes to
`417de8e4a25a12751e6b2511235e406acf549256e077545789fc6ce3e67d9962`.

| Production reader | Calls | Spelling bytes | Median / p90 / p99 spelling length |
| --- | ---: | ---: | --- |
| Bounded u64 reader | 654,935 | 1,793,817 | 1 / 5 / 20 |
| Narrow full decoder | 35,034 | 13,346,272 | 18 / 290 / 4,241 |
| Narrow count-only reader | 11,748 | 12,996,050 | 114 / 4,094 / 4,559 |
| Wide decoder | 2 | 10 | 5 / 5 / 5 |
| Floating value reader | 23 | 82 | 4 / 4 / 4 |
| ext80 integer reader | 0 | 0 | Not observed |

Lengths include prefixes/quotes/suffixes where present. Full range decoding
made 23,819 calls: **23,775 single fragments (99.815%) and 44 concatenations**.
All these range calls were narrow on this input. Each bookkeeping-array site
made 23,819 requests: 533,008 and 266,504 requested bytes, with 103,436 bytes
of alignment padding at the first site. Concatenation separately allocated
2,419,357 bytes in 44 output buffers. These are cumulative traffic counters,
not retained memory, cache traffic or RSS.

Of 35,034 narrow full decodes, 31,251 (89.20%) had no backslash and 17,062
had spelling length at most 16. Their bodies contained 13,276,204 bytes and
223,869 raw backslash bytes. Count-only bodies contained 12,972,554 bytes and
222,573 raw backslash bytes. These are **backslash-byte densities**, not counts
of parsed escape events; paired backslashes and variable-length escapes are
not interpreted by the offline observer.

Observed identical backing-span transitions were 11,747 count-to-decode and
only 20 decode-to-decode. Pointer, length, exact spelling hash and relevant
width/delimiter distinguish observations within each successful process.
These are not complete semantic cache keys: synthetic token views, range
identity, target/dialect and arena lifetimes still matter. The count-to-decode
population does not justify eagerly materializing bytes on count-only queries.

The numeric reader ran 1,925 times in preprocessing, 33,340 in semantics and
619,670 in lowering. 613,801 calls (93.72%) had at most eight spelling bytes.
The production reader reported decimal 555,222, hexadecimal 97,161 and octal
2,552 times; binary did not occur in this input. Its observed suffix tails
were empty 564,281; `u` 60,120; `U` 21,956; `UL` 7,963; `L` 300; `ull` 250;
and `ULL` 65. Tails come from the actual reader's consumed index, not a second
numeric parser. They describe the baseline and do not validate its permissive
partial-consumption or overflow policy; #148 owns that correction.

## Candidate and acceptance boundary

`c_ir_decode_string_literal_range_for_target` already returns a single
fragment's output buffer directly, but unconditionally allocates two arena
bookkeeping arrays first. The candidate instead points those descriptors at
its existing local `CIrDecodedString` fields, allocating arrays only when
there is more than one fragment. Decoder selection, escaping, count/width/kind,
concatenation, overflow checks and success-only publication stay unchanged.
There is no cache, extra IR, widened row, new SIMD primitive or numeric rule.

The census identifies 47,550 removable requests / 570,600 requested bookkeeping
bytes. These are a prediction from measured call counts and the code change;
candidate observer results must confirm the realized change and alignment
redistribution. No instruction, cycle, wall-time or RSS improvement follows
from that arithmetic alone.

The existing quoted-literal differential gate is extended with a single-output
allocation budget, arena-owned pointer checks and sentinel outputs on failure.
The existing registered test adds dirty-arena cases for empty/escaped ordinary,
u8, UTF-16, UTF-32 and wide strings; supplementary characters; parenthesized
and concatenated ranges; malformed escapes; and incompatible prefixes, across
Linux/Windows x86-64 and Linux AArch64 target descriptions and GNU17/GNU23.
The existing scalar/window/count comparisons and exact-tail cases stay intact.

Before acceptance, require final-SHA full/mode/sanitizer/platform/self-host CI,
independent fixture controls, identical frozen outputs, ordinary compiler
code-size/disassembly review, and paired native-throughput evidence including
tiny cases. Performance acceptance additionally requires verified physical
Zen 5 and separate Zen 4 nonregression under the documented protocol. Those
measurements are absent at this entry's creation. Keep the PR draft rather
than treating a kernel result, lower allocation count or hosted guard as a
proven end-to-end speedup. Wider kernels, batched numeric conversion and a
permanent decoded-literal memo remain untested hypotheses, not implemented wins.
