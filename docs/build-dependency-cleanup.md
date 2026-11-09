# Unity fixture dependency cleanup

This inventory covers the source/fixture series based on
`b57c704e3a5e9bbe3d8627927d277af396c44bb2`. PRs #2024, #2026, #2027,
#2028, #2043 and #2044 were merged before that base was selected; #2045's
build-driver work is also present. Build settings, no-op implementation,
linking, baking and publication belong to the separate driver effort.

## Completed packets

The drafts form an ordered stack; none is merge authorization.

| Packet | Scope | Verified repository dependency edges |
| --- | --- | --- |
| [#2046](https://github.com/buster14a/buster/pull/2046) | Independent diagnostic, dataflow, SSA, allocation, frame, unwind, builtin, measurement and checker/source fixtures; narrow benchmark clocks/counters and intrinsic helpers | 606 removed, none added |
| [#2047](https://github.com/buster14a/buster/pull/2047) | Type/ABI, builtin/environment, lane, lexer, Tier 0, encoder and semantics helpers; three-bound ABI header | 147 removed, 3 added |
| [#2048](https://github.com/buster14a/buster/pull/2048) | Private ELF/link/import/bind/debug/executable/dynamic/static/snapshot fixtures and individual helper consumers | 135 removed, none added |
| [#2049](https://github.com/buster14a/buster/pull/2049) | Direct production optimizer includes preserve first-definition order while BACKEND owns four test roots | 40 removed, none added |
| [#2050](https://github.com/buster14a/buster/pull/2050) | Differential runner, tool-probe and process-worker self-tests; narrow nonstandard support roots | 20 removed, 2 owner-only fixture edges added |
| [#2051](https://github.com/buster14a/buster/pull/2051) | BACKEND/CHECKER source preparation separated from the production dataflow include boundary | 9 removed, none added |

Across all 43 translation units, both scalar and vector dependency comparisons
remove **957 repository edges** and add **5**, with another **48 system-header
edges removed**. All 32 standalone dependency sets are unchanged in each mode.
These are structural results, not a wall-time percentage. The two new fixture
files improve edit locality as well as removing repeated private parsing.

With the same Debug preprocessing flags, total scalar unity output falls from
384,730,472 to 329,839,216 bytes (54,891,256 fewer); vector output falls from
386,606,036 to 331,713,157 bytes (54,892,879 fewer). CHECKER, FRONTEND and
BACKEND remain the largest preprocessed units at roughly 35.5, 34.6 and
33.3 MB in scalar mode. This measures actual expanded source volume, including
guarded spans inside shared files; it does not predict a compile-time percentage.

Candidates were ordered by repeated private source work and edit fanout before
changing small shared interfaces. ELF helper narrowing followed the independent
large fixtures; the two production include-boundary packets followed explicit
inspection of first-definition order. The emitted command homes remain fixed.

## Necessary sharing

The exact authoritative path/owner/reason rows live in
`tools/ci/build_regression.c`; they are not directory or filename-prefix grants.
DRIVER means `main.c`; other names identify `main_unit_NAME.c`.

| Roots or helper family | Actual unity consumers |
| --- | --- |
| Checker comparison, parser/preprocessor builders, preprocessor oracle | All eleven, through the existing shared parser/checker interfaces; private suites stay in their command's unit |
| Object-view declarations/readers | All eleven through shared symbol/source readers; individual helper bodies are narrower |
| Source preparation, BURS/selection/corpus helpers, benchmark counter utilities | BACKEND, CHECKER |
| Type table builders | TYPES, FRONTEND, BACKEND, CHECKER; generated type worlds use TYPES, CHECKER |
| ABI builders; ABI bounds header | TYPES, CHECKER; bounds also BACKEND |
| Integer builtin oracle; environment/history helpers | TYPES, CHECK_BUILTINS; FRONTEND, CHECKER respectively |
| Encoder helpers; tool probing | DRIVER, X86, FRONTEND, LINK, BACKEND, plus ASSEMBLER for encoding or TYPES for probing |
| Lane topology; branch process runner | DRIVER, CHECKER, EXECUTABLE; DRIVER, X86 respectively |
| Link/import/bind/permutation builders | LINK, DYNAMIC, EXECUTABLE |
| Debug object/native helpers and clock | LINK, BACKEND; debug object comparison also CHECKER |
| Executable assembly/execute/value helpers | LINK, EXECUTABLE, DYNAMIC, ASSEMBLER, BACKEND, CHECKER |
| Dynamic inputs; static writer; snapshot patch helpers | DYNAMIC, EXECUTABLE, CHECKER; DYNAMIC, EXECUTABLE; EXECUTABLE, CHECKER respectively |
| Foreign builders | EXECUTABLE, CHECKER |
| Intrinsic object readers; checker row header; testsuite input helper | X86, CHECKER; DRIVER, CHECKER; DRIVER, BACKEND respectively |
| Semantics root | Form names serve DRIVER/BACKEND; six `used` globals retain all eleven copies at their original positions; fixtures belong to DRIVER |
| Process CPU limits/capture files, libm/host-symbol lookup, temporary paths and work assignment | Existing shared production/parser/parallel interfaces; private process/tool self-checks are now separate owner-only files |

Production passes and generated builtin declarations reached through fixtures
have separate justified exact rows. Scalar host headers have two exact
configuration-scoped rows. Standalone consumers remain real dependencies:
vector scheduling fixtures also serve `tier1_schedule_benchmark.c`, counters
serve `tier0_benchmark.c`, and the intrinsic catalog serves
`c_checker_benchmark.c`. Their full edit plans include those objects.

## Automated prevention

The original ownership check remains in ordinary correctness CI. Its expanded
graph covers **548 scalar / 546 vector fixture and support paths**, with **487
single-owner paths**, **61 / 59 exact shared rows**, and **41 structurally pinned
shared fixture/support roots**. The conventional C subset is now 504 files with
31 shared roots, compared with 502 and 96 on the base. Counts describe this
revision and are not acceptance targets.

Literal include closure protects active descendants of any extension or
directory. Eleven important nonstandard paths additionally assert their command
owner. Exact reasons/owner sets reject widened or obsolete sharing. Shared-root
fingerprints detect new shared declarations, aggregate members, includes, macro
names and widened guarded spans, while excluding correctly private islands.
Bodies, initializers and macro replacement bodies still need source review.
Dependency records must be complete, current, unique and configuration-correct.
Internally guarded roots also retain standalone preprocessing in source-only CI.
The Build workflow includes the new checker headers in its path triggers.

Negative cases cover direct/nested/header/fragment leaks, arbitrary generated
and outside descendants, shared-root growth, widened guards, changed owner sets,
bad/obsolete exceptions, unknown selectors and stale/truncated/count-mismatched
dependency data. The [contributor rule](commands/build.md#preventing-dependency-growth)
describes how to extend the checks without exempting private work.

## Validation and remaining work

Each source packet passed normal scalar Debug/Release, all eleven vector unity
syntax checks, focused runtime commands, standalone preprocessing and the
existing incremental/configuration harness. Final vector syntax/dependency
captures include all **43** translation units. All **43 scalar Release objects
and the final compiler are byte-identical** to the base; compiler SHA256 is
`ebdf87d69030587e2aa19cd2d3260cecd17b9327bb8196967b773838d9b09e4d`.
ABI and import assertion coordinates, floating-point scopes, attributes, static
storage and forced-emission globals were preserved. All 22 Debug/Release unity
symbol tables contain no emitted interface-reference functions. The checker
fixture tests pass ASan/UBSan with leak detection.

Local validation used Clang 23.1.4, CMake 4.4.5 and Ninja 1.13.2 on a Ryzen 9
7940HS, with `BUSTER_HOST_ARCH=x86-64 BUSTER_DEBUG_INFO=0` and queued core grants.
Vector code was compiled, not executed. This host supplies no authoritative
timing claim. No benchmark was queued on Benchpress merely to obtain a number.
Hosted CI, sanitizer/fuzz campaign acceptance and locked 9700X qualification
remain separate from these local results. Draft code PRs deliberately withhold
hardware admission.

The remaining source opportunities require broader production boundaries:

1. Shared parser/checker/source interfaces still retain several helper-rich test
   roots in every unit. A future packet can separate their data/prototypes from
   shared production reader implementations, preserving include order and
   normal diagnostics. Do not remove their exceptions before actual consumers
   and standalone graphs are proven.
2. CHECKER and BACKEND retain substantial emitted command work, while FRONTEND
   also has a large parsing footprint. This fixture-only series removes
   parsing/invalidation work while preserving all emitted code; it does not
   rebalance command code generation. Moving command homes or production
   interfaces needs a distinct measured packet and current locked-host policy.
3. The six forced semantics globals are intentional emitted storage. Reducing
   those copies would change objects and lies outside byte-identical fixture
   isolation. Initializer/macro/helper-body growth also remains a review duty.

Raw dependency captures, hashes, validation logs and reproduction tools belong
in `buster-performance/docs/measurements`, not in this compiler repository.
