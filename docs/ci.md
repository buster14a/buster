# Hosted correctness, native vector correctness and Zen 5 benchmarks

Every change reaches `main` through the merge queue (#1430). The queue's
candidate for it runs the complete hosted contract on its exact commit before
it merges: both host architectures with all four O3 build modes (plain and
three sanitizer configurations) across each row's shards, the vector pool, the
three fuzz campaigns, and each auxiliary workflow its change touches (see "The
merge queue"). Documentation-only changes run it too. Main's ruleset requires
the queue, with no bypass, and main is fast-forwarded to the commit the queue
tested. A push to `main` runs no checks (the owner's decision on 2026-11-10);
the nightly measures main's first-parent states on the 9700X. SQLite's whole
Tcl suite (`hosted-correctness.yml`'s `sqlite-full`) runs on the nightly and
dispatched runs alone (#2334).

Before the queue, every merge and direct push to `main` ran hosted correctness
against its own `github.sha` (#1271). In the nightly mode that was the plain
rows alone (#2057), with the sanitizer rows and fuzz campaigns on the nightly
snapshot's daily run; before it, every row and campaign. The nightly still
reads those runs' records, and the selector keeps its push modes for them.

Candidates have separate run-id concurrency groups: none cancels or replaces
another, and each candidate's `stale` job cancels the runs of candidates GitHub
dropped. PRs keep their per-PR superseded-run cancellation. Hardware jobs still
take the shared host lock. The selector's main-policy mutants and the nightly
glue fixtures pin this contract, a push trigger included. A candidate never
runs the 9700X job (`hardware=skip`): its native correctness and targeted
measurements run in the 02:00-08:00 window as its first-parent state's (see
"Nightly main measurements"). The qualification self-test rejects a candidate
with an all-skipped suite: only pre-merge PRs can use the no-code exemption.

The workflow in `.github/workflows/9700x.yml` runs correctness tests on
GitHub-hosted `ubuntu-26.04` runners. Its matrix builds the triggering revision
with `BUSTER_HOST_ARCH=x86-64` and `BUSTER_HOST_ARCH=native`; on a pull request,
the second row is the vector row, `BUSTER_HOST_ARCH=vector` (see "The vector
row", #2033), which the runs that run the sanitizer rows have too, beside
`native`, with its sanitizer configurations. All use the current
official stable LLVM 23.1.4 release, including its matching C++ pragma oracle,
LLVM object/DWARF tools and lld. Each row also builds the clang server that
the semantic driver's checks send the oracle clang's runs to (#2071), from the
release's static clang and LLVM archives: an `x86-64` row's build job, once for
its shards (see "Shared row products"), and each shard of a `native` row. The
native plain row's `--differential` compares every served run with a child's.
The installer runs only on disposable hosted runners. GCC remains the `cc`
oracle for fixtures that require GNU C extensions.

The build pins `-fno-stack-protector` (#916). A distribution's Clang can
default to `-fstack-protector-strong` while the official release does not, so
the host's configured default would otherwise decide the compiler's code and
its measurements. The owner chose no stack protection for now, which matches
the official release's default and gives host and CI builds the same canary
count. The compiler's predefined macros still follow the host driver's
configured default (`BUSTER_HOST_SSP_DEFAULT`). The build's post-link step
checks each linked executable for a `__stack_chk_fail` reference and fails if
the policy did not take effect.

The baseline build runs on x86-64 without AVX-512. Native builds select the
existing scalar twins when the compiler's host target lacks the full SIMD
feature set. Intrinsic-only shared helpers have scalar definitions for that
build. Generated-code ISA and ABI remain controlled by the existing target
options. Both hosted variants log their CPU, target features and selected path.
The baseline leg must report `scalar`; the native leg uses vector kernels when
the full predicate in `host_isa.h` holds, and scalar kernels otherwise. A hosted
CPU without AVX-512 therefore still runs the complete correctness suite. The
vector row must report `vector`, on CPUs its probe qualified.

Each matrix row is a call of `.github/workflows/hosted-correctness.yml`, which
runs the row's inventory as the row's own number of shards (`matrix.shard`, its
`shards` input), a runner each, dealt by `parallel --shard K/N --costs
tools/ci/costs/ROW.costs`: the runner deals the commands to the shards by the
row's own costs on a hosted runner, the costliest first, each to the shard that
would end the earliest with it, the same deal on every shard's runner, so that
a row's shards run every command once (docs/commands/parallel.md). The plain
rows run as 4 shards, the `x86-64` TSan and MSan rows as 6, the `native` MSan
row as 5 and the others as 4 (#2031; [Shard costs](commands/ci.md#shard-costs)
has the tables and the plans). Unsharded, a plain row's test step took 14.6
minutes of its 18-minute job on one 4-CPU runner, and the MSan rows took up to
98 minutes. A portable `x86-64` row builds its compiler once, in a job of its
own, and its shards run that verified product (see "Shared row products"
below). A `native` row's shards each build the compiler themselves, since a
native build must run on the CPU model it was built for, which hosted runners
don't share. A row's once-only steps (the baseline fallback checks, the QEMU
smoke test, the constrained arenas, the parser smoke test, the performance
readers and the TSan/MSan `main.c` compiles) run in its shard 1, which the deal
gives that much less of the inventory (its table's `#: first-shard`), and each
shard's sanitizer reports are an artifact of its own.

Standard `ubuntu-26.04` runners do not promise a particular CPU model or this
feature set. The vector row draws runners until one whose CPU qualifies runs
each of its items (see "The vector row"), with no emulation.
Required native vector coverage of the measured `znver5` build comes from the real Ryzen 7 9700X, after
both hosted variants pass on the same triggering revision. The hardware job
asserts `BUSTER_HOST_VECTOR=1`, builds with `BUSTER_HOST_ARCH=znver5` and runs
the complete suite before any benchmarks. A pull request never runs it (#2055,
the owner's choice on 2026-11-10, after #1272 had dropped its native suite on
2026-11-04): its hosted rows qualify it, and native correctness and
measurements run on its merged first-parent state in the nightly window, and
on dispatched runs. Hosted and hardware jobs read one
suite inventory, `tools/ci/correctness.tests`, with the same counts, seeds and
external cross-checks. There is no vector emulation.
Hosted runners are virtual machines, so the compiler runs there with
transparent huge pages off (#1109, `arena_page_policy` in `lane.c`), and the
9700X with them as its system has them.
Baseline memory boundary tests also run with undefined-behavior checks.
The hosted matrix additionally builds the entire compiler with each of
`BUSTER_SANITIZER=address|thread|memory`, for both host architectures.
Every variant runs `tools/ci/correctness.tests`, including the same corpus,
seeds, lane counts, generated-code execution and external cross-checks. These
are full suites, a row's shards together; the TSan and MSan rows are not
reduced module lists.
The baseline job additionally runs Buster under QEMU's `qemu64` CPU to preprocess
and compile a C fixture and assemble a multi-instruction fixture, then links and
executes their results on that same emulated CPU.
That leg links generic Ubuntu startup objects, libc and compiler support routines
from checksum-pinned packages in a private directory (`BUSTER_HOST_RUNTIME`).
It uses their loader explicitly for emulation: a hosted VM's installed startup
objects and libraries can require a newer ISA independently of `-march=x86-64`.
`BUSTER_CLANG_RESOURCE_DIR` selects the matching Clang builtin headers, preserving
their paths in line markers even when Clang is staged outside `/usr/lib/clang`.

The self-host qualification is a separate workflow,
`.github/workflows/selfhost.yml`, and also runs only on GitHub-hosted runners.
It builds the compiler with the compiler, runs a bounded smoke subset, and
compares deterministic probe objects and executables, once for each host path:
`native` (the vector path, skipped on a runner without it) and `x86-64` (the
scalar path, #1193). It is not part of the required correctness or benchmark
path; see [the self-host guide](self-hosting-ci.md).

The frozen corpus comes from the
[input release](https://github.com/buster14a/buster-performance/releases/tag/ci-corpora-preprocessor-391e729af658).
A run's preparation job fetches it once, with the apt packages, the baseline
runtime and the c-testsuite and SQLite mirrors, and the hosted tests read them
all with no network ([Offline inputs](hosted-toolchain.md#offline-inputs-2275)).
`tools/ci/fetch-corpus.sh` checks the archive digest, original manifest digests,
every input byte, file counts and file kinds. The archive retains source/header
notices, package provenance and component license texts. Historical paths and
the old Clang binary hash remain provenance records; the active oracle is the
installed Clang 23 toolchain. A missing or changed input fails setup.

The `benchmarks` job runs native correctness followed by benchmarks; on a pull
request, the benchmarks alone (the native correctness step's `if` skips
`pull_request`, and the selector's oracle policy pins that). It has
`needs: [changes, evidence, test, fuzz]` and explicitly requires successful
`test` and `fuzz` results, and the `evidence` job's success and admission.
GitHub applies these results to their entire matrices. A failure, cancellation
or skipped suite prevents the physical job from being queued, including
scheduled and manual runs whose change selector is skipped. The one skipped
suite that admits it is a nightly dispatch's: its `evidence` job found the
commit's own earlier run had passed the plain rows. A nightly backlog run and
an admitted snapshot run are in the *plain* scope, as a pull request is: plain
rows only, and no fuzzing (see "Nightly main measurements"). The hardware job checks out the same `github.sha`, verifies the Ryzen
7 9700X identity and vector eligibility, and retains the existing host lock,
CPU placements and benchmark selection. Native tests use eight CPUs under the
lock, and benchmarks reuse that tested executable. A native test failure stops
the job before measurements. The benchmarks still check their paths against
their scalar twins, so a vector-path defect they reach fails them there.

The job stages the current official stable Clang 23.1.4 release and matching
Clang API, linker and object/DWARF tools in the runner's private cache, verifying
the archive SHA-256 and preserving its licenses. A new completeness marker
upgrades older compiler-only caches. The pragma oracle links to the official
self-contained `libclang-cpp`, avoiding the release's Ubuntu-specific static
system-library paths on the Arch host. Frozen inputs use the same verified
archive as hosted tests. No system packages are modified.

The staged tree also keeps LLVM's static archives (`lib/libLLVM*.a`, 222
files, 338 MB), as the hosted install does, so that the Release build
generates the checker's and the backend's native code in parallel parts
([build guide](commands/build.md#parallel-native-generation)): the splitter
links them through `llvm-config --link-static`, the release having no shared
`libLLVM`. Its system libraries name the release's build machine's
`/usr/lib/x86_64-linux-gnu/libzstd.a`, which the Arch host doesn't have;
the build links that one by name, `-lzstd`, from the host's `libzstd.so`.
Without the archives the job's configure step said `parallel native
generation is off` and compiled the two units whole (#2563): a clean build
of 97.1 s against 68.9 s with the parts, measured with the same clang. A
tree staged before them is staged afresh once. Turning the parts on moved
the measured executable's code (#2058 measured the two executables within
±0.33% of each other), so measurements across that change compare
different layouts.

The job keeps its verified setup inputs in the runner's private cache
(`BUSTER_CI_CACHE`). Each one is kept under a key that identifies it, checked
on every use, published atomically, and replaced when stale or corrupt:

- **Frozen corpus:** the archive, under its SHA-256
  (`tools/ci/cached-download.sh`). Every extracted file is still verified
  against the manifests.
- **c-testsuite:** a bare mirror holding the pinned commit
  (`tools/ci/fetch-c-testsuite.sh`). The checkout fetches it through Git's pack
  protocol, which checks every object's hash.
- **SQLite's tree (#146):** a bare mirror holding SQLite 3.53.2's commit, in
  the same way (`tools/ci/fetch-sqlite.sh`), for `test-sqlite`'s CLI tests.
- **Tier 1's and the helpers' programs:** `speedtest1.c` and Lua 5.4.8's
  release, under their SHA-256 (`cached-download.sh`), for
  `measure-tier1-programs` and `measure-helpers` alike (#2629).
- **buster-performance's pinned records (#2629):** a bare mirror holding each
  pinned commit the job reads, shallow (`tools/ci/fetch-performance.sh`): the
  branch-sizing qualification's harness (`a00d16e4`) and Tier 1's Lua scripts
  (`818cfbbc`). A step checks out its directory alone from the mirror, through
  Git's pack protocol (a partial clone, sparse), where `actions/checkout`
  fetched it from GitHub on every run.

Everything else the job uses is on the host: the official Clang release above,
CMake and Ninja (`setup-build-tools.sh`, in the same cache), the frozen corpora
under `/var/lib/buster-9700x/corpora`, and the host's packages (`tcc`, `gcc`,
its clang, `perf`). Beyond the commits it measures, the run's and its pull
request's base (`build-benchmarks.sh` and the branch-sizing comparison fetch
the base, shallow), the job downloads nothing a run before it already did.

The pragma oracle is not cached. Its 2.3 s build finishes while the compiler
build's longest unit (35 s) still runs, so a cached oracle would save no time.
And a key exact enough to reuse it safely would have to cover everything the
build reads outside the staged tree: the system C++ headers, the linker and
its inputs, and environment variables such as `CPLUS_INCLUDE_PATH`.

The staged Clang release itself has a content manifest
(`tools/ci/tree-manifest.sh`): the SHA-256 of every file and the target of
every link, written when `setup-benchpress.sh` publishes the tree. Every use
checks the tree against it (about 0.15 s for 600 MB, in 8 processes, when it
was measured; the tree is 1.6 GB with LLVM's archives) and stages it afresh
from the verified archive when anything differs. A tree staged before the
manifest is staged afresh once.

On 2026-11-03 PR jobs, the setup before native tests took about 45 s, nearly
all of it the 40 s build. The corpus and c-testsuite fetches took 0 to 1 s, and
the oracle's 2.3 s ran beside the build (#1183). So these caches remove the
host's network dependence more than they save time.

The build's critical path is one unit, `main_unit_checker.c` (35 s). Only a
content-addressed object cache could take time off it, and only when that
unit's preprocessed input repeats. That needs a compiler launcher, which
`build.sh` deliberately clears, so it is left as an open item. Native
correctness (about 110 s) and the runner hook (1 to 92 s, including lock waits)
are the other costs. `tests/ci/caches.sh` tests each cache's hit, miss,
corruption and changed input.

The hosted `qualification` job is the ruleset's required context,
`Integration qualification` (#1430). It was named
`Build and execute on Ryzen 7 9700X` until the merge queue, which no longer
described a job that qualifies most runs on hosted runners alone; an alias job
reported the old name until the ruleset required the new one, which it did in
the same edit that turned on the merge queue. It requires the `evidence` job's
success, successful hosted tests and fuzzing, and the hardware job: on the
daily and dispatched runs (and, before the queue, a push to main before the
nightly mode), native correctness and the measurements. A merge-queue
candidate never runs the hardware job: the context passes on the full hosted
contract, which its coverage step checks job by job (see "The merge queue").
A pull request, draft or ready, never runs
the hardware job (#2055, the owner's choice on 2026-11-10): the context passes
on its hosted plain rows with fuzzing and the hardware job skipped, and its
message says that native correctness and measurements run on the merged
first-parent state in the nightly window. A skipped hardware job after a hosted
failure therefore cannot satisfy the merge gate. Documentation-only
changes keep their explicit no-code path. A nightly dispatch reports through
its coverage record and has no such context, and neither does a scheduled run
the nightly mode retired.

A push to main runs nothing (#1430): its candidate's run qualified it before
it landed, and its state's nightly run qualifies it on the 9700X, recorded in
the ledger. Before the queue, a push reused no pull request's native
qualification (that run has none, and the `host-identity` job is gone); before
the nightly mode it ran native correctness on the landed commit, and in it
none in the daytime.

Draft and ready PRs alike run hosted correctness and never queue the 9700X
(#2055). Both `ready_for_review` and `converted_to_draft` still trigger the
workflow, using the same per-PR concurrency group: marking ready qualifies the
current revision, and returning to draft cancels the superseded ready run. A
code-changing draft qualifies on its hosted rows as a ready one does; GitHub
doesn't merge drafts. The selector-confirmed no-code path is still allowed; it
reports an exemption, not vector coverage. Performance-document/harness exceptions remain selector-controlled.

`tools/ci/qualification.c` is the aggregate's fail-closed predicate. It checks
selector/event consistency as well as every required result; missing, invalid,
failed or cancelled results cannot pass. Its `--self-test` enumerates
37,500,000 combinations against the explicit allowed tuples. They span the
event, draft state, selection, dispatch purpose, the `evidence` job's hardware
decision, and seven job results.
The hosted selector job runs that test before any hardware admission, and the
selector's own policy mutants pin the draft guard and ready/draft triggers.
The evidence result it reads is the `evidence` job's where the CI glue's
fixtures (the `glue` job, `CI glue fixtures`) did what the event requires:
passed on a pull request and a merge-queue candidate, which alone run them,
and skipped elsewhere (`qualification_evidence`, which the self-test checks
for every event and both results).

A pull request's run stops at its first failure. Its two test rows' shards fail
fast, and its tests wait for the policy job (`Measurement storage policy`: the
measurement storage, agent-guide size and DCO hook checks in one job, #2334), which
finishes about when the `changes` job does, and don't start when it fails. A
merge-queue candidate's run does the same, and its fuzz campaigns fail fast too
(see "The merge queue"). Pushes
to main, the daily run and dispatched runs keep `fail-fast: false` and test
whatever those checks report: their sanitizer rows and fuzz campaigns are main's
coverage, and one detector's failure must not hide what the others find. The
Tier 1 machine-problem workflow's two architectures follow the same rule.
The CI glue's fixtures are the exception (#2658): the scripts the evidence,
record and 9700X jobs run, the vector row's draws and the setup re-run's
decisions, against fixtures, in a job of their own beside the rows, which the
required aggregate waits for. In the evidence job they held every row back by
about 4 minutes (merge-queue run 43481767714: 236 s of its 246 s, the rows
starting at 298 s), when neither a pull request nor a candidate uses the
9700X; a candidate's pull request has passed the same fixtures.
Within a shard, a pull request's or a candidate's runner stops at its first
failing command too (`parallel --fail-fast`, #2703): it starts nothing more and
kills the commands running beside the failure, so the failure ends its job at
once rather than when the shard's last command does.
GitHub has no fail-fast across jobs. A job that cancels the run through the API
was considered (#1195) and dropped for pull requests and the other runs: it
needs a token with `actions: write`, which fork pull requests don't get; on a
pull request, once the policy checks gate the tests, nothing would remain for it
to stop; and on the other runs it would cut the coverage above. A merge-queue
candidate has none of those reasons, and its run is cancelled at its first
failure (#2703; see "The merge queue").

To check CI edits locally, run the selector's self-test and validate the shell
and workflow syntax. To exercise fallback code on a capable machine, set
`BUSTER_HOST_ARCH=x86-64` for `build.sh`; keep Zen 5 performance measurements on
Benchpress.

## Shared row products

A portable row (`x86-64`: plain, ASan, TSan and MSan) builds its compiler once
for its shards (#2032). The test job calls
`.github/workflows/hosted-correctness.yml` for each row. In a portable row, that
workflow's `compiler` job sets its runner up as a shard does: the verified
hosted inputs, `setup-hosted.sh`, the sanitizer host report, the fatal policy
and detector proofs, and the baseline runtime for the plain row. The build's
settings are the workflow's environment, the same for the build and the
shards. The job then builds and packs the product in four steps, three of them
`tools/ci/hosted-product.sh`'s:

- `build`: the pragma oracle, the clang server and
  `./build.sh --config Release --jobs "$(nproc)"`, with the build step's checks
  of `build/compiler`. A native row's shards run this same command. The
  server's build exits 77 where a toolchain can't give it one copy of LLVM (a
  distribution's shared LLVM without clang's archives). With another clang that
  is a notice and no server, and the driver's checks run the oracle clang as
  children. With the hosted installation, the clang that `toolchain_root()`
  finds, it is an error: that tree holds the archives, so 77 means a regression
  in its tar list or the release's layout, which would otherwise leave the row
  green, its driver commands 251 s slower in the plain row (#2071). The check
  sits here, the one step that knows its toolchain, rather than in the
  inventory, which the 9700X and local runs share.
- `ownership`: the inventory's unity fixture ownership heading. It is the one
  heading that reads the build's tree (Ninja's dependency records) rather than
  the executable, so it runs here, on that tree, through the built executable's
  runner.
- the sanitizer report audit (sanitizer rows): the post-link step and the
  ownership heading run the instrumented executable.
- `pack`: the compiler, the pragma oracle, the clang server, the
  build's compile commands, the product's identity and provenance, and the
  SHA-256 of every file, as a reproducible `product.tar.zst`. Zstandard's
  third level packs the plain row's 48 MB compiler into 15 MB; the static clang
  server (#2073), about 203 MB, adds some 60 MB (the MSan row's product, with
  its 183 MB compiler, packs 387 MB into 128 MB). It keeps the identity and
  provenance beside the archive and exports them as `BUSTER_PRODUCT`, as an
  install does, for a suite on the same runner (a vector configuration's
  item 1).

It publishes the archive as this run's artifact
`compiler-ARCH-SANITIZER-OOPT-ATTEMPT`, kept 7 days, and its outputs give the
artifact's name and the archive's SHA-256.

The product's identity is what the build and every shard compute with the same
function and compare byte for byte:

- the repository, the run, the commit and its tree, with the tracked files
  unmodified;
- the row;
- the digest of the whole hosted toolchain installation, 2.0 GB (clang, its
  resource directory with the sanitizer runtimes, lld, mold, wild,
  libclang-cpp, LLVM's and clang's archives, CMake and Ninja), which the
  install step reports the time of;
- the baseline runtime's tree, in the plain row;
- build.sh's own configuration record, the toolchain identity, the tools on
  PATH and every setting, which `./build.sh configure` writes;
- `print-compile-flags`, `print-sanitizer-flags` and `print-sources`;
- the target's flags (`print-host-flags`) and its predefined macros with
  `host_isa.h` (`BUSTER_HOST_VECTOR` 0, or 1 for the vector target);
- for the vector target, the row's probe's answers on the runner, every
  feature `yes`;
- `ldd`'s resolution of each executable.

Its provenance is reported but not compared: the runner, its CPU and kernel;
its image, glibc and the versions of the system packages the build reads; and
the ownership check's result. Runner images can differ between a row's jobs, as
they could between its shards when each built its own compiler, so a shard
reports where the image, glibc or packages differ from its own.

A portable row's shards need its `compiler` job and start once it ends. Their
first step fails unless the build succeeded: no shard builds the compiler or
fetches it elsewhere. After the usual setup, sanitizer probes and host report,
they download the artifact by the build's output name, and
`hosted-product.sh install` checks it:

1. the archive's SHA-256 against the build's output;
2. that it holds only directories and regular files under `product/`;
3. exactly the files it lists, each with its hash;
4. the identity, which a difference fails, printed;
5. the build's image, glibc and packages, whose differences are a notice;
6. each executable, the clang server among them, run once.

Only then does it install them where `build` puts them:
`build/compiler`, in the checkout's `build/` where the compiler finds its
headers, `build/compile_commands.json`, and the pragma oracle and clang server
in `RUNNER_TEMP`. It exports the same variables as `build`, and
`BUSTER_PRODUCT`. On such a shard the inventory's ownership heading checks that
the product records its pass and exits 77 (skipped); every other command runs
as before. `check-call-graph`'s `./build.sh` call configures the tree and
builds the IR it checks, which no product replaces.

Products stay in their run. No cache holds one. A shard downloads only
from its own run (no `run-id` or token), by the name and SHA-256 that its
run's build job gave, and the identity binds the product to the repository,
run, commit and tree. A pull request's run builds and tests its own products, a
merge-queue candidate or a dispatch builds its own, and no run can use
another's. The
selector's row policy (`changes_row_policy`) pins this with counterexamples.

A failed build fails its row's shards at their first step, and a cancelled one
cancels them. Rerunning the failed jobs reruns the build first, under a new
attempt's name and SHA-256. A shard always reads the product that its run's
build job last published, by that job's outputs, so a shard rerun without the
build reads the attempt that succeeded, as the hosted inputs' handoff does. A
missing (expired), truncated or altered archive, a partial extraction or a
mismatched configuration fails the shard and says why. Rerun the build job, or
the workflow: nothing rebuilds or refetches by itself, so one bad product can't
start four builds. `bash tests/ci/hosted-product.sh`, which the build job runs
before building, checks each of these refusals, a product without its server,
the clang server's status 77 (failing the build on the hosted toolchain and a
notice with another clang) and the inventory's skip on a fake checkout,
toolchain and build. `bash tests/ci/hosted-tools.sh` checks the same 77 failure
against the tree that `install-hosted-tools.sh` installs.

Native rows keep their per-shard builds: each hosted CPU selects its own path.
The vector row's explicit target makes each configuration's builds one, so
each configuration shares one product too, built by its item 1's holder on a
CPU its probe qualified (see "The vector row"). Reuse across revisions (a
change to fixture data alone) is a separate extension (#2032's discussion).

## The vector row

Hosted `native` rows ran the vector path at random: 62 of 105 sampled jobs did
(59%; 41% in the busiest hours). The others drew a Zen 3 EPYC 7763, or an EPYC
9V74 VM with AVX-512 masked, and ran scalar, so a defect only the vector path
has failed some runs and not others (#2033's review). The owner's decision D3
(2026-11-10) replaces that row with a deterministic vector row, with **no
emulation**: the workflow starts hosted jobs until enough of them draw a CPU
that runs the vector path.

**Scope.** The row's configurations are its sanitizers, each with 4 items,
its shards, which one pool of draws holds:

- A pull request: the plain configuration alone (`none`, 4 items), its second
  row, in place of `native`.
- The runs that run the sanitizer rows (merge-queue candidates, full
  dispatches, the nightly snapshot's deferred run; before the queue, pushes
  before the nightly mode): the plain and the three sanitizer configurations
  (`none address thread memory`, 16 items), beside the `native` rows.
- Elsewhere (nightly backlog runs and admitted snapshot runs, the `plain`
  scope; before the queue, pushes in the nightly mode): no vector row. Their `native` rows stay,
  and `tools/ci/nightly.c` counts the plain rows' shards by their names, which
  the vector row's jobs don't have, so its jobs never count.

The test job's matrix adds the row with `include`: one call of the row
workflow, whose `sanitizer` lists the configurations
(`BUSTER_VECTOR_CONFIGURATIONS`).

**The target.** `BUSTER_HOST_ARCH=vector` builds `-march=x86-64-v4 -maes
-mgfni -mvaes -mavx512vbmi -mavx512vbmi2 -mavx512vpopcntdq -mavx512bitalg
-mavx512vnni`, exactly `host_isa.h`'s predicate, never `native` or `znver5`;
configuring fails unless it selects `BUSTER_HOST_VECTOR=1`
(`cmake/BusterSettings.cmake`). `./build.sh print-host-flags` prints the host
target, which the inventory's commands that compile for the build's host
(`tools/ci/correctness.tests`) and the row's predefines and parser smoke test
pass to clang and to the compiler, for every row. The row tests the vector
path compiled for x86-64-v4 with generic tuning, not `znver5`'s code
generation: the 9700X still runs native correctness of the measured `znver5`
build in the nightly window.

**Items.** An item is a configuration's initial and its shard: `m2` is
memory's shard 2 of its 4. `BUSTER_VECTOR_ITEMS` lists all 16 the costliest
first, which is the order the collectors assign them in, across the
configurations: memory's, address's, thread's, then the plain one's, each
configuration's item 1 first. `costs plan` over each configuration's table
gives its longest shard of 4 (564, 439, 638 and 447 s by the tables of #2033's
first full run, 43027989712), and its build adds to each item (506, 618, 204
and 153 s there): item 1 also builds the configuration's product and runs its
once-only steps. The pool is one call of the row workflow,
so the four configurations share its draws: at a 41% rate, a first draw of 48
candidates for 16 items falls short in 10.9% of runs, where four rows' draws of
12 for 4 items each would leave at least one short in 60%.

**Draws** (`.github/workflows/hosted-correctness.yml`, `draw-1` to `draw-4`, and
`tools/ci/vector-draw.sh`). A hosted VM belongs to the job it runs, so the
candidate that probes is the one that works. Each draw starts generic
candidates, which run the shards' own steps (`*row-steps`), each step gated on
the item the candidate holds:

1. **Probe**, before the checkout: a C probe, built by the image's `cc` with
   the target's flags, whose `#if` is the predicate itself, asks
   `__builtin_cpu_supports` for `x86-64-v4` and each predicate feature. It
   reads what the OS enables too, so a VM that masks AVX-512 fails it, whatever
   its model. A probe that stops (a CPU without AVX2 may meet an instruction
   the flags let it use) doesn't qualify either. Its record: the level, the
   candidate, the runner, the CPU model, the verdict and the missing features.
2. **Publish**: every candidate uploads its record as
   `vector-OOPT-ATTEMPT-probe-LEVEL-CANDIDATE-yes|no`.
3. **Take or redraw**: a qualifying candidate waits for its draw's assignment
   and takes the item it names: its configuration (`BUSTER_SANITIZER`), its
   shard (`BUSTER_SHARD`) of its configuration's items (`BUSTER_VECTOR_SHARDS`)
   and whether it builds (`BUSTER_VECTOR_BUILDER`, item 1), which a step checks
   before setting the configuration's linker and differential checks as the
   workflow's environment gives a row of that sanitizer. The take prints
   `Vector item ITEM: SANITIZER, shard K of N`, which `tools/ci/costs.c` reads
   for the row of the candidate's log. The candidate then gets its compiler
   (below) and runs its shard as any shard does, after the confirmation below.
   A candidate without an item ends within seconds, successfully, as a redraw,
   so that rerunning the failed jobs reruns only real failures. Every candidate
   uploads its record with its decision (`-draw-LEVEL-CANDIDATE`).

**The claim is a rendezvous by rank**, the design's fallback. Its first
candidate was artifact-name uniqueness: GitHub refuses an upload under a name
the run already has, and the candidates would have claimed an item by
uploading its name. The race fixture (below) disproved it on 2026-11-11 (run
42993863311): when 16 jobs uploaded one name within the same second, all 16
uploads succeeded, in each of 4 rounds, and the run held 16 artifacts by that
name. So no candidate claims anything. Each draw's **collector**
(`collect-1` to `collect-4`, one per draw: the pilot had none for the last
draw, whose candidates would have waited 30 minutes for nothing) starts with
its draw and polls the run's artifacts every 15 s through the same-run Results
service used by `upload-artifact` and `download-artifact`. These lookups use the
job's artifact runtime token and consume no repository REST requests (#2309).
A failed listing doubles the pause, up to 60 s. A small local Node action,
`.github/actions/vector-runtime`, runs the existing shell with the runtime
credentials that GitHub supplies to JavaScript actions. It installs no npm
packages, writes no token to files or workflow outputs, and has no REST
fallback. The script and action come from the triggering commit through a
sparse git checkout. The collector waits until as many candidates qualified as items are
missing, or every candidate has published its verdict or ended, at most 25
minutes. Only when the artifacts cannot decide does it read one optional jobs
snapshot, within the page allowance below. Then it assigns: the qualifying candidates, by their numbers, get the
missing items, the costliest first (`BUSTER_VECTOR_ITEMS`). One
job decides, and it uploads the decision once, as an artifact whose name holds
it: `-assign-LEVEL-ITEMxCANDIDATE...`. Nothing can take an item twice. It never
waits for an item's tests. The next draw starts candidates for the items still
missing alone: 3 per item in the first draw (12 on a pull request, 48 for the
four configurations; the collector refuses fewer), then 4, 5 and 5 per missing
item. With draws independent, as #2033's 24 runs measured, a binomial model
gives, for a pull request's 4 items:

| Rate of qualifying CPUs | Runs needing a 2nd draw | a 3rd | a 4th | Still short after 4 |
| --- | ---: | ---: | ---: | ---: |
| 41% (busiest hours) | 20.5% | 2.3% | 0.16% | 0.011% |
| 59% (sampled mean) | 1.8% | 0.045% | 0.0005% | ~0 |

and, by the same model, for the pool's 16 items:

| Rate of qualifying CPUs | Runs needing a 2nd draw | a 3rd | a 4th | Still short after 4 |
| --- | ---: | ---: | ---: | ---: |
| 41% (busiest hours) | 10.9% | 1.05% | 0.072% | 0.005% |
| 59% (sampled mean) | 0.0094% | 0.0002% | ~0 | ~0 |

**One product per configuration** (#2032). The build runs the executable (the
post-link step's baked tables), so it needs a qualifying CPU too. The holder of
a configuration's item 1 builds it (`hosted-product.sh build`), checks unity
fixture ownership on the build's tree, rejects any sanitizer report so far,
packs it with its identity (`hosted-product.sh pack`) and publishes it as
`vector-product-OOPT-ATTEMPT-CONFIGURATION-SHA256`, kept 7 days, before its own
suite. The configuration's other items build nothing. After their own setup
(the checkout, the hosted toolchain, the detectors' proofs, the corpus) they
wait for it (`vector-draw.sh product`, polling every 30 s and backing off as
the draws do), in the attempt its holder last ran in (in a rerun of the failed
jobs that didn't rerun the holder, the latest earlier attempt's: the API lists
such a job with the rerun's number, #2276), download it by that name and
install it by the SHA-256 the
name holds (`hosted-product.sh install`), as a portable row's shard does. Every
item's suite, item 1's too, skips the inventory's ownership heading on the
product's record of its pass (`BUSTER_PRODUCT`, which the install exports, and
the pack, for item 1, whose tree the identity's `./build.sh configure` leaves
out of date: a check there failed in #2033's first full run). An
explicit target is shared only where it runs: `hosted-product.sh` packs or
installs a vector product only where the row's probe (`BUSTER_VECTOR_PROBE`),
run again, passes, and the identity holds the target's flags
(`./build.sh print-host-flags`), `BUSTER_HOST_VECTOR 1` and the probe's
answers, every feature `yes`. An item fails, never builds, when no candidate
can hold item 1 any more (the last draw assigned it to none), when item 1's
holder ended without publishing, when its attempt has two products, or after
90 minutes. Products stay in their run, as the portable rows' do: no cache,
and a download by name from the run alone.

After its publication step, the builder always uploads
`vector-OOPT-ATTEMPT-builder-CONFIGURATION-ended`, including after setup,
build or upload failure. A waiting item checks the product first, then this
marker; no product plus a marker fails promptly. One optional jobs snapshot
also detects a builder that already ended without its marker. If the runner
disappears after that snapshot and cannot publish the marker, the existing
90-minute product deadline applies. Jobs are never polled repeatedly.

### Vector REST budget (#2309)

The first attempt of a PR's four-item vector row makes at most **16 charged
REST requests**, regardless of how many artifact polls or candidates run:

| Caller | Maximum charged REST requests |
| --- | ---: |
| Four collectors, one optional jobs snapshot each, at most two pages | 8 |
| Three product consumers, one optional jobs snapshot each, at most two pages | 6 |
| Before/after quota observations, collector 1 and the verdict | 2 |
| Candidate assignment waits and artifact/product listings | 0 |
| **Total** | **16** |

Thirty such PR attempts per hour use at most **480** requests. Against the
conservative 1,000-request hourly budget in #2309, that leaves 520 for other
workflows; a token with a larger live limit has more room. This is the vector
row's bound, not a promise about unrelated workflows' consumption.
The daily/full pool's twelve product consumers raise its first-attempt bound
to 34. Job lists larger than their allowance remain unknown; they do not
extend the budget or permit a partial list to establish that a job is absent.

Failed-job and full reruns need additional job identity reads: the jobs API
labels carried-over jobs with the new attempt number, so their start time
must be compared with `run_started_at`. Each rerun command has at most six
requests, including failed requests and pages; collector 1 gets one extra
for the initial observation. The product resolves its builder's attempt once
per holder, then waits through the artifact service. An unavailable,
ambiguous or incomplete identity fails closed. A rerun is not covered by
the fresh-PR 16-request bound; its own count is reported separately.

Every REST call reads `X-RateLimit-Remaining` and `X-RateLimit-Reset` and
refuses another request while the observed remaining quota is at or below
100 and the reset has not arrived. Missing or malformed headers stop further
reads in that command. Optional first-attempt status reads can stop while
artifact progress continues; required rerun identity reads cannot be replaced
by a guess. The Results-service client has one 20-second request timeout and
no internal retries; the shell owns its bounded exponential back-off.

Each command keeps a TSV request ledger outside shell subshells, so requests
inside command substitutions and pipelines still count. The candidate and
collector always upload those ledgers as
`vector-OOPT-ATTEMPT-rest-draw-LEVEL-CANDIDATE` and
`vector-OOPT-ATTEMPT-rest-collect-LEVEL`. The verdict totals this attempt's
ledgers, checks that completed draw/assignment records have ledgers, enforces
the first-attempt ceiling and reports the before/after header observations.
The local request count is exact; the header delta includes other workflows
sharing the token. If the quota window changed, it reports both observations
without claiming a comparable delta.

Artifact inventories preserve every attempt and duplicate name. An invalid
or incomplete listing is never an empty inventory: collectors cannot assign
without a successful complete listing and every earlier draw's assignment.
The runtime client conservatively refuses 1,000 or more artifacts instead of
assuming completeness across many reruns; this is our safeguard, not a
documented Results-service truncation limit. A fresh workflow run clears that
condition. The exact upstream protocol references are pinned in the action's
source header; no third-party implementation is vendored.

Passing assignments through job outputs would require a different rendezvous:
a candidate must keep its qualifying VM while the collector decides, so making
it depend on that collector would create a dependency cycle. Splitting probe
and execution into different jobs would lose the CPU qualification. The
runtime artifact rendezvous retains the current assignment and product
contract while removing REST polling.

The trade-off is latency against one tested binary. A waiting item's runner
is idle while item 1 builds, as long as it would have taken to build its own;
it then spends the pack, the upload, a poll and the install (about 20 s in the
portable rows: 8 s of ownership check, 3 s of pack, 2 s of upload, 2 s of
download and 4 s of install) before its suite. So sharing saves no
runner-minutes and costs each waiting item about half a minute, against four
builds that each test their own binary.

**Evidence and verdict.** Before its suite, an item's candidate confirms that
the build's target selects `BUSTER_HOST_VECTOR=1` and runs `benchmark-lexer`
on `x86_64_encoder.c`, which a scalar build refuses: the vector lexer lexes
all 592 KB on this CPU, no part needing the scalar lexer, and the tokens must
match the scalar twin's. Once every step passed, it uploads its evidence
(`-evidence-ITEM`): the probe's verdict and CPU, its decision, the target,
`BUSTER_HOST_VECTOR=1`, the kernels' run, its item and configuration, the
product it built (item 1) or installed, with its SHA-256, and the suite's pass.
The `verdict` job (`vector-draw.sh verdict`) requires exactly one passing
qualified execution of each item by the candidate its assignment names, item 1
with the product it built and the others with one they installed. Its summary
lists every candidate's CPU and decision and every item's configuration,
runner and product. An item no candidate held fails it as **incomplete: no
qualifying CPU drawn**, which is not a test failure; an item whose run failed
fails as such (its candidate's job failed too).

**Attempts and reruns.** Every record and product carries the run attempt.
Rerunning all jobs draws and assigns every item afresh, and every item 1
builds again. Rerunning the failed jobs reruns no collector, so the earlier
attempt's assignments stand: an item's candidate reruns it on a new runner, and
if that runner doesn't qualify, the job fails, saying that no test failed and
to rerun the failed jobs again. A rerun item installs the product its
configuration's item 1 last published: the earlier attempt's when item 1
passed, or the new one when item 1 reruns too. After an incomplete verdict the
draws all passed: re-run all jobs to draw again.

**Permissions and the race fixture.** Nothing needs write access: every record
is the run's own artifact, which a fork's pull request can upload too.
`.github/workflows/vector-claim-race.yml`, on pull requests that change the
draws (the row workflow, `tools/ci/vector-draw.sh`, the runtime action or its
fixtures) or the race fixture and on
dispatch, runs 16 racers, each on its own runner, as one draw's candidates at
one instant, through the same script: about half qualify (by a hash of the
racer and the run), the collector assigns the 4 items of its one configuration
(`race`: `r1` to `r4`), and the judge requires
each assigned item taken by exactly the racer its assignment names, only by a
qualifying racer, no racer taking two, and min(4, qualifying racers) items
assigned. Its control round uploads one name from every racer at once and
reports how many succeeded. `bash tests/ci/vector-draw.sh` tests the script's
collector, candidates (reruns among them), a pool's order, the product waits
(an earlier attempt's product, a rerun builder's, a builder that ended without
one, an item 1 no draw can assign), request budgets, quota guards, back-off and
the verdict against fake runtime and REST services. The PR's `evidence` job
runs it and `node tests/ci/vector-runtime.cjs`, which verifies the runtime
wire contract, duplicate/all-attempt semantics, malformed responses,
completeness bound, secret-safe errors and shell action hand-off.
`bash tests/ci/hosted-product.sh` tests a vector
product's install on a qualifying runner and its refusals.

## CI latency baseline

`.github/workflows/ci-timings.yml` runs entirely on hosted runners, at 08:37
Europe/Madrid or on manual dispatch. It reads at most the latest 100 runs of
`9700x.yml`, fully paginates each run's latest-attempt jobs, and reports the
median and nearest-rank 95th percentile of successful hardware jobs' queue,
execution, runner-hook, lock-wait, lock-hold, setup/build, native-test,
benchmark-step and host-occupation durations. Host occupation runs from the
job's first step, after the runner hook took the lock, to the job's end. Four
classes stay
separate:

- PR;
- main push (before the merge queue; merge-queue candidates have their own
  section, `timings --queue`);
- nightly targeted: backlog dispatches, recognized by their run name;
- full: the daily run, full dispatches and the nightly snapshot's run.

The classes say how a run was started, not what it covered: the coverage
records do that. The raw API responses,
normalized observations and summary are retained as 14-day workflow artifacts.
This adds no benchmark work and changes no hardware scheduling or merge gate.

Missing or inconsistent timestamps contribute no sample; a job or step that
failed/skipped is not a successful timing observation. No observations means
unknown, not zero. `runner-hook` is the job-started hook's step, from the
API's whole-second step times: hook overhead and any waiting.

The shared lock's times come from the 9700X's runner hooks themselves, which
log every acquisition and release in the job's log. The job-started hook
(`/usr/local/libexec/buster-9700x/job-started.sh`) prints
`buster-9700x: waiting for the machine, held by: …` while another holder has
it, then `buster-9700x: this job has the machine`. The job-completed hook prints
`buster-9700x: gave the machine back`. The collector reads those lines and
their sub-second runner timestamps from each successful hardware job's log,
read as data and not kept, and reports two metrics:

- `lock-wait`, from the job-started hook's start to `this job has the
  machine`;
- `lock-hold`, from there to `gave the machine back`, the host's time under
  this job's lock.

Both are rounded to whole seconds. The lines bracket the lock operations, so
each metric also includes the hook's own work around them. In the job below
that was 14 ms from the hook's start to its `waiting` line, and 4 ms from the
job-completed hook's start to its release line: tens of milliseconds, below
the rounding, but not zero. A log that can't be read, or that lacks one of the
lines, gives that metric no sample. On 2026-11-03, PR run 41664434811's
job waited 8.0 s behind an agent session's differential test and held the
lock for 151.6 s.

The collector (`tools/ci/timings.sh`) uses read-only Actions access on the
hosted runner. `tools/ci/timings.c` validates bounded tab-separated observations,
rejects duplicates/invalid numbers and computes statistics. The PR path tests
both without API credentials or network calls:

```sh
bash tests/ci/timings.sh
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/qualification.c -o /tmp/qualification && /tmp/qualification --self-test
```

## The merge queue

GitHub's merge queue (#1430) tests each queued pull request on a temporary
`gh-readonly-queue/main/...` branch: main and the entries ahead of it,
merged with the pull request. It merges only what passed. `9700x.yml` runs
on that branch's `merge_group` event (type `checks_requested`, main, no path
filter), so a candidate reports both required contexts, `Integration
qualification` and `Measurement storage policy`. Main's ruleset (25225502)
requires them and the queue, with no bypass actors:

- merge commits, the method main keeps;
- only candidates that pass merge (`ALLGREEN`), one at a time;
- at most 4 candidates build at once;
- a 180-minute limit for the required checks to report.

The build limit comes from the drill's measured demand. A passing candidate
runs 100 jobs and 912 runner-minutes over a 37-minute critical path, peaks at
72 concurrent jobs during the vector draw, and holds about 45 until minute 21.
So four builds hold about 180 of the account's roughly 360 concurrent jobs,
which leaves the rest for pull requests, and merge about 6.5 candidates an hour
(156 a day). That covers the peak day of 135 merges, though not a burst like
11 November's, when 102 merged in four and a half hours. Then the queue grows,
and each entry waits its turn. Merge limits don't combine builds, so raising
the batch size saves nothing.

A candidate's run is the only per-merge correctness run (the owner's decision
on 2026-11-10). It works like this:

- **Scope.** The full hosted contract runs on the candidate's exact commit:
  - both host architectures, each with its plain, ASan, TSan and MSan O3
    builds and every row's shards;
  - the vector pool's 16 items;
  - the three fuzz campaigns.

  A documentation-only candidate runs all of it too. SQLite's full Tcl suite
  stays on the nightly and dispatched runs, as for pushes (#2334).
- **Selector.** `changes --event merge_group` sets `code`, every plain
  benchmark and the performance readers' gate, whatever the candidate changes.
  The 9700X job's other gates stay off. The `changes` job runs its self-tests
  and the `evidence` job its fixtures, as on a pull request, so a candidate
  whose merge breaks the CI tools fails.
- **Evidence.** `scope=full`, `hosted=run`, `hardware=skip`, reason
  `merge-group`. A candidate never uses the 9700X. Its merged first-parent
  state gets native correctness and measurements in the nightly window.
- **Concurrency.** Each candidate has its own run-id group, because the queue
  builds several at once: none cancels or replaces another.
- **Failing fast.** A candidate stops at its first failure, as a pull request
  does (the owner's choice on 2026-11-12). A failing candidate then leaves the
  queue sooner and holds fewer runners.
  - Its tests and fuzz campaigns don't start when the policy job fails.
  - Its first failing row cancels the other rows, and within a row its first
    failing shard or vector draw cancels the rest.
  - Its first failing fuzz campaign cancels the other campaigns.
  - Within a shard, its first failing command stops the shard (`parallel
    --fail-fast`, #2703): nothing more starts, and the commands beside it are
    killed.
  - Once its test rows or its fuzz campaigns have failed, `stop-tests` or
    `stop-fuzz` cancels its run (#2703). GitHub's fail-fast works within one
    matrix only, so the aggregate used to wait for the other matrix, the glue
    fixtures and the auxiliary workflows: the fuzz campaigns alone take about
    21 minutes (`frontend`). Its `Integration qualification` still runs
    (`always()`) and fails, as in the 68 runs `stale` had cancelled by 14
    November, and GitHub rebuilds the entries behind it at once.

  Before #2703, a failing candidate's first failed job ended 22.5 minutes in,
  on average, over 15 failures (13–14 November), though its failing command
  ran for 0.7 to 82 s. Each held about 790 runner-minutes, near a passing
  candidate's 912. Failures were 18.3% of the queue's runner time, and the
  entries cancelled behind them another 11.2%.
- **Auxiliary workflows.** A candidate also calls `build.yml`, `build-pgo.yml`,
  `selfhost.yml` and `tier1-problem.yml`, each whole (`workflow_call`, job
  `auxiliary-NAME`), where its own change touches a path that workflow's
  `pull_request` trigger lists. Its own change is its merge against its base,
  `HEAD^1..HEAD`. The selector reads each list from the workflow itself
  (`changes_auxiliary_decide`), so each rule is written once. Its `auxiliary`
  output names them, as `,NAME,NAME,` or `,` for none. They start after the
  policy job. A pull request still runs them from its own triggers.
- **Invalidated candidates.** GitHub never cancels a candidate's run, as the
  drill showed. When it drops a candidate, or rebuilds the entries behind a
  failed one, it deletes the candidate's branch, or points the branch at the
  rebuild when the base is the same. So each candidate's `stale` job
  (`nightly.sh stale`, which `nightly stale` decides) cancels the workflow's
  unfinished `merge_group` runs whose branch no longer names their commit and
  whose commit isn't on main. A rebuild starts a candidate, so the runs it
  replaces go as it starts. The job never gates. A read that fails cancels
  nothing, and neither does a merged run, which may still be uploading its
  record. A candidate dropped from the end of the queue has no rebuild behind
  it, so its run goes on to the end.
- **Aggregate.** `Integration qualification` runs for a candidate.
  It passes only when the hosted rows, the fuzz campaigns, the policy job and
  the coverage step all succeeded and the 9700X was skipped. There is no
  documentation exemption.
  - The coverage step (`nightly.sh coverage`, `nightly coverage`) reads the
    run's own jobs, because a test job's result alone stays green when a shard
    or a row is skipped.
  - It requires every shard 1..N of every row and sanitizer (one N per row),
    the vector pool's verdict and each fuzz campaign to have completed
    successfully.
  - Each auxiliary workflow the selector named must have run at least one job,
    and every job it ran must have succeeded. One the selector didn't name must
    have run none. A missing list (no selector) fails.
  - A missing, skipped, cancelled, failed or timed-out job fails it, and so do
    shard counts that disagree.
  - It reads the jobs up to three times, 15 s apart, since the API may not
    show the last needs' completion yet.

  `qualification.c` reads the step's outcome. Its self-test enumerates the
  candidate's one green tuple.
- **Record.** A candidate's coverage record has purpose `queue` and coverage
  `none`. It holds the candidate's commit and tree, its base (the first
  parent: main, or the entry ahead) and its pull request's head (the second
  parent, `pr_head`). Its outcome is:
  - `deferred` when the aggregate passed it: its hardware is the window's;
  - `blocked-hosted` when a hosted job failed;
  - `interrupted` otherwise.
- **Accounting.** `ci-timings.yml`'s daily report (`timings.sh`,
  `timings --queue`) covers the latest 100 completed candidate runs of
  main's queue (another branch's, such as the drill's, never lands on main). It
  splits each one by what became of its candidate: merged (its commit is on
  main), passed but invalidated, failed, or cancelled. For each it gives the
  candidates and the runner-minutes (the sum of the jobs' own times), the
  share spent on candidates that didn't merge (failures and rebuild waste),
  and the merged candidates' median and p95 wall time. Queue delay is apart
  from the run (#2031): each hosted job's wait for a runner, from its creation
  to its start, of the jobs a runner took (not skipped ones, nor the 9700X's,
  which waits for its own machine, not the hosted pool). The report gives the median and
  p95 of each candidate's longest wait and the mean of every job's. It doesn't
  see the time an entry waits in the queue before its candidate's run starts.
- **Nightly credit.** The drill (12–13 November, #1430) showed that main is
  fast-forwarded to the exact commit the queue tested, with the same SHA and
  tree. So that run is the state's own:
  - the harvest credits its row to the state;
  - its plain rows qualify the state for the window, with no hosted
    preparation;
  - a backlog dispatch reuses them.

  Should main's commit ever differ, nothing would credit the run, and the
  nightly would run the state's hosted rows itself. Once any candidate's run has been credited,
  the report lists every later state without a passing candidate run of its
  own commit as a coverage gap, and the coordinator raises an alert for it.

Main's own runs alone save the hosted input caches every branch reads: a
push, a dispatch or a scheduled run on main. A candidate's caches would belong
to its branch alone, and a nightly dispatch runs on a `nightly/` branch. Once
pushes run nothing, `selfhost.yml`'s daily schedule keeps the caches fresh,
since it calls `prepare-hosted-tools.yml`. The apt closure's key changes with
each runner image.

**Hotfix entry.** The `hotfix` label lets a pull request enter the queue without waiting for its own rows, to land
a fix as fast as possible. The queue admits a pull request only once the ruleset's contexts pass on the pull
request itself, and `Integration qualification` normally waits for its hosted rows.
- **When the label is added,** `hotfix.yml` reports that context as passed on the pull request's head. With the
  policy job's `Measurement storage policy`, about 30 s, the pull request can be queued at once.
- **On later pushes while the label stays,** `9700x.yml` skips the pull request's aggregate, which counts as passed.
- **Unchanged:** its rows still run as feedback, and its candidate runs the full contract like any other. A
  `merge_group` event has no pull request labels, and nothing in `hotfix.yml` runs for a candidate or a push
  (`changes_hotfix_policy` pins this).
- **The cost:** a hotfix that fails its candidate fails in the queue, holds a candidate until then, and makes
  GitHub rebuild the entries behind it. Adding a label takes triage access.

The checking workflows have no `push` trigger: `9700x.yml`, and the auxiliary
workflows a candidate calls. `publish-hosted-inputs.yml` keeps its trigger,
since it publishes rather than checks.

## Nightly main measurements

Main's first-parent states (#1207) are measured on the 9700X between 02:00 and
08:00 Europe/Madrid, the window the owner widened from 04:00-07:00 on
2026-11-05. A branch's internal commits are not states; a merge of
them is. Each state needs **targeted** coverage: the selector's choice against
its own first parent, with native correctness first in the same job. A state
whose first-parent change is documentation alone needs no targeted measurements,
and the ledger says so; its merge-queue candidate ran the full hosted contract
on it.
The cycle's snapshot also needs **full** coverage, which adds every benchmark,
the isolated samples and the oracle-cache audit. Plain coverage, every plain
run as pushes measured before the nightly mode, satisfies neither.

A state is measured behind its own plain hosted rows (both architectures,
every shard), the gate a pull request needs: its merge-queue candidate's run,
whose sanitizer and fuzz results are the state's too. Before the queue they
came from the snapshot's daily run and, before the nightly mode, its pushes. They are
reported, but they don't block measurement, which was the owner's choice on
2026-11-03.

### Evidence and reuse (`tools/ci/nightly.c`)

Every `9700x.yml` run has an `evidence` job, which must succeed:

- **Pull request:** never admitted, draft or ready (`hardware=skip`, reason
  `pull-request`, #2055): its hosted plain rows qualify it.
- **Push:** in the nightly mode, deferred (`hardware=skip`, reason
  `deferred`, outcome `deferred`): the push runs its plain hosted rows
  (`scope=plain`, #2057), and
  the window runs its native correctness and targeted measurements as its
  state's backlog run. Its plain hosted rows are the state's, so the state
  is ready without a preparation. Before the nightly mode, admitted: its 9700X
  job runs native correctness and the measurements on the landed commit.
  Since #1272 a pull request's run has no native correctness, and since
  #2055 no 9700X job, so a push reuses nothing from it; records from
  before then that say a push reused its pull request's run
  (`native-reused`, outcome `pending`) are still read by the harvest.
- **Merge-queue candidate** (#1430): never admitted (`hardware=skip`, reason
  `merge-group`). It runs the full hosted contract, its own. When main is
  fast-forwarded to its commit, its plain rows are that state's, as a push's
  are (see "The merge queue").

  Every 9700X job records its host identity in the coverage record: the
  kernel, the microcode, GCC's version and glibc, and a digest of the software
  the native inventory runs (the package database, the external tools and the
  libraries they load, the static runtime, and the files of the C toolchain's
  packages). A component that can't be read leaves no identity, never a
  placeholder.
- **Nightly dispatch:** the commit must be a first-parent state of main at or
  before the pinned snapshot, or the job fails and nothing runs. The newest
  conclusive push, dispatch or merge-queue run of that commit decides its
  hosted rows: a pass is reused and a later failure isn't hidden. The window
  must also admit the run.

Every run's `record` job writes `coverage-record-ATTEMPT`, kept 90 days. It
holds the commit, tree, first parent and first-parent tree; the blobs of the
workflow, selector, inventory, toolchain and corpus scripts; the selection; what
the run ran, reused, admitted and deferred; the 9700X job's window, native
correctness and host identity; and the outcome. A merge-queue candidate's
record also holds its pull request's head (`pr_head`). Outcomes are `complete`,
`not-required`, `deferred` (no hardware admitted; for a merge-queue candidate,
the aggregate's pass), `pending` (before #1272: a push
qualified by reuse whose measurements weren't reusable), `blocked-hosted`,
`blocked-native`, `failed` (a measurement after native correctness),
`interrupted` (cancelled or timed out) and `rejected`. Only the 9700X job's
success completes (or, in records from before #1272, a push's evidenced reuse).

### The coordinator (`.github/workflows/nightly-backfill.yml`)

**Scope.** The backlog's states start at the epoch: the first-parent commit of
main that added the nightly worker (`tools/ci/nightly.c`). The worker is each
state's own `9700x.yml`, dispatched with a `backlog` or `snapshot` purpose and
checking its window after the lock. Earlier commits' workflows have neither, so
the coordinator can't measure them within the window, and they are out of
scope. Their coverage is what their pushes and the old daily run measured. The
report counts the fortnight's first-parent states before the epoch.

The coordinator is hosted, and only main's scheduled and dispatched runs write.
They create and delete `nightly/SHA` branches, and dispatch and cancel
`9700x.yml` runs on them, so that each run, its checks and its definition are
that state's own. Its run name, `Nightly PURPOSE (admit|defer) of SHA
[REQUEST]`, correlates it with the ledger.

1. **Harvest.** Each step reads a fortnight's runs (later steps a day's):
   pushes, the daily run, dispatches and merge-queue candidates. It harvests
   the records of completed runs of a state that the ledger lacks. A record
   that disagrees with the API's job conclusions counts as nothing, and so
   does a candidate's pass that its aggregate job didn't make.
2. **Pin.** The cycle (the night ahead, from noon) pins main's head as its
   snapshot once, from 22:00 Europe/Madrid the evening before the window, in
   local time whatever the clocks do that night. A step
   before then pins and plans nothing, unless an override admits work now.
   Every dispatch of the cycle names that pin, not main's head
   at dispatch, which may have moved on. The worker checks the state's
   first-parent ancestry against the pin and records it.
3. **Prepare.** Before the window, the coordinator dispatches the snapshot's
   deferred run: every hosted row and the 100,000-input fuzz campaigns, the
   daily diagnostics the old schedule ran. It also dispatches the plain rows of
   the oldest states that have none, three at a time and forty a cycle. A
   prepare job that waits for the window plans again every five minutes until
   it opens, so each completed preparation frees its slot for the next. It
   never admits hardware, even when a step lands at 02:00 or carries an
   override: admission is the window job's alone. The window job starts from
   the prepare job's ledger and its pending dispatches, so a dispatch the API
   doesn't show yet isn't repeated. The pending dispatches come as a job
   output, with their count, not with the ledger's artifact. A valid empty list
   is still a list. A window job without a valid one plans nothing and fails,
   and the next coordinator run plans once the runs show the dispatches. The
   ledger's own fallbacks are safe: harvest reads its rows from the records
   again, and the pin comes as an output too.
4. **Measure.** Inside the window it admits one 9700X run at a time: the
   snapshot's comprehensive run first, then the oldest ready state whose own
   worker window is open and has room for the run. While the
   snapshot's hosted rows are in flight, it keeps room for the snapshot. If
   the snapshot is ineligible, it measures the newest eligible earlier state
   and says so. The comprehensive run is a daily requirement: only one
   completed in this cycle meets it. An unchanged main still gets its daily
   full run and oracle audit, though an earlier cycle measured the same
   commit.

**Historical worker windows.** A `nightly/SHA` dispatch runs that state's own
workflow and tool. States before the window expansion still admit only
04:00-07:00 Europe/Madrid. The coordinator reads each state's literal
`BUSTER_NIGHTLY_WINDOW_START_HOUR` and `BUSTER_NIGHTLY_WINDOW_END_HOUR`
declarations from its immutable `tools/ci/nightly.c` blob, once per distinct
blob in a job. `nightly worker-window` reads those bytes as data; it does not
build or execute historical code. The supported contracts are 04:00-07:00 and
02:00-08:00. Missing, malformed or unsupported declarations are reported as
unknown, and the state's hardware remains pending until reviewed.

The planner intersects the worker's civil-time window with the current
02:00-08:00 limit and reserves the whole dispatch-to-completion bound. It skips
an old worker before 04:00 or too late to finish by 07:00, considering later
eligible states immediately. If only workers whose opening is still ahead
remain, the window job waits for them. The same rule applies to the snapshot
and its substitute, and a waiting snapshot reserves room before its own
deadline. A state that cannot fit stays in the backlog; it is never marked
complete or pruned for this reason. Each job rebuilds the metadata from Git,
including after a prepare/window/continuation handoff. The worker still checks
its window independently after taking the shared lock.

States move between `not-required`, `complete`, `in-flight`, `ready`,
`preparing` and `needs-preparation`; `blocked-hosted`, `blocked-native` and
`failed` (three failed or interrupted measurements) are reported and skipped,
and never block later states. A rerun keeps its run's ID and takes the next
attempt. So a state's hosted and native verdicts are its latest run's latest
attempt's, whatever order the ledger holds them in. Each failed attempt counts
toward the three, since each one occupied the 9700X. The cycle's report counts
each run once, by its latest attempt. Nothing depends on dates but the cycle, so
unfinished states carry over, and a completion never moves a cursor past an
earlier gap. Pending dispatches that aren't yet visible as runs prevent
duplicates. A step whose run listing fails, even partly, plans nothing until a
full listing succeeds; otherwise a run in flight it hid would be dispatched
again. An admitted run older than two hours is cancelled.

**Refused branches.** GitHub refuses (HTTP 403, "Resource not accessible by
integration") to let the workflow token create some states' `nightly/SHA`
branches. In the 2026-11-05 cycle it refused all four merges it was asked for
whose pull request's head lacked a workflow change main already had, and none
of the three other states (two merges whose heads had main's workflow files, one
commit with a single parent). By that pattern, 27 of the cycle's 49 states would
be refused. The token can't take the `workflows` permission that GitHub asks
for to create such refs. A refused branch warns, the state is skipped for the
rest of that coordinator job, and the job plans again at once: nothing was
launched, so it doesn't wait as for a run in flight. The state neither keeps a
preparation slot nor stays the oldest ready state while later states wait. Each
job tries it once again. Any other failure to create a branch (a timeout, a 429
or 5xx, a failed lookup) is not a refusal: it warns, and a later step tries
again. A branch that another step made meanwhile is used. A `nightly/SHA`
branch that already points at another commit is never dispatched; it is an
error, and the state is skipped for the job as a refused one is, since it stays
wrong until a person fixes it. The report counts
and names the skipped states, with an alert. A refused snapshot is replaced by
the newest eligible earlier state, as an ineligible one is. With the `nightly-ledger` environment's `BUSTER_NIGHTLY_BRANCH_TOKEN`, the
coordinator creates and deletes the `nightly/SHA` branches with it instead. That
token is a fine-grained token with only `Contents: Read and write` and
`Workflows: Read and write` on this repository. Dispatches, cancellations
and reads keep the workflow's token. Without the secret, the refused states are
skipped as above. A token GitHub rejects as a credential (expired, revoked, or
without access to this repository) is warned about once and dropped for the
rest of the job, which then makes the branches with the workflow token, as
without one. A 404 alone doesn't reject it: a branch someone else deleted
before the prune is a 404 to a valid token too. The token is rejected only if
it can't read the repository itself either. Branches created with it trigger workflows,
unlike the workflow token's, but every workflow's `push` trigger is limited to
`main`.

**Time.** The tz database's Europe/Madrid is checked at three points:

- at dispatch, by the coordinator's planner;
- at admission, by the `evidence` job;
- after the 9700X job's hook took the lock, by its first step after the host's
  own check. That check reads only the operating system, the architecture and
  the CPU model. The window step compares the clock with the cycle's 02:00 and
  08:00, which the `evidence` job computed as instants. It runs before the host
  identity's package queries and hashing, and before the checkout, the cache
  and the toolchain's staging. So a job that took the lock too late does
  nothing but refuse. The self-test pins that order, and the glue test runs
  the job's first steps in order with the window closed to check that no
  package is queried and nothing is hashed.

If the database is unavailable, nothing is admitted. The self-test checks the
database against the EU's rule every hour from 2026 to 2030. Spain changes its
clocks at 02:00 local time, so the window opens at the first instant the local
time reaches 02:00. When the clocks go forward, 02:00 doesn't exist, and the
window opens at 03:00 CEST, five hours before 08:00. When they go back, it
opens at the first of the two 02:00s, seven hours before 08:00. The pin at
22:00 is four hours ahead either way. A run is admitted
only if its whole bound still ends by 08:00: the job's timeout (20 minutes for
backlog runs, 25 for the snapshot) plus 5 minutes, and the coordinator adds
the hosted jobs' time. A refusal after the lock fails the job before any work,
which the record reads as a deferral. The job's timeout ends anything that
runs over, and the hook then releases the lock. The lock's protocol and its
fairness to agent sessions are unchanged; nightly work is kept out of the day
rather than given priority.

The window job starts when the window opens and must end within GitHub's six
hours. So it stops once no backlog run could still end by 08:00, about 07:35,
even with a hardware run in flight. That run ends by 08:00 under its own bound,
and the next coordinator run harvests its record. The night the clocks go back,
the window is 7 hours, longer than one job. A window job that has run 345
minutes while it still waits inside the window hands over to `window-continued`,
the same way the prepare job hands over to it: the pin and its pending
dispatches go as job outputs, and the ledger comes from its artifact. The
continuation numbers its requests from 20001, so none repeats the window job's.
Two jobs hold any window.

**Late schedules.** GitHub starts this repository's scheduled runs hours late.
In the week of 2026-10-28, the former daily run's `23 7 * * *` started 6h22m to
9h53m late, and `selfhost.yml`'s `47 5 * * *` started 7h13m late. So the
coordinator has nine schedules:

- every two hours from 16:07 to 04:07;
- then 05:37 and 08:17, after the window.

A run that prepares before the window waits for it when it opens within 5 hours
55 minutes of the job's start. For any common delay up to about 15 hours, some
run starts between 20:05 and 07:35, close enough to wait or still inside the
window. Unless delayed, the earlier schedules start before 22:00: they harvest
and report, and pin nothing (step 2). Any other run waits in the concurrency
group, or reports.

**Override.** Both workflows' manual dispatches take an `override` reason for
deliberate out-of-window work. The window checks report the reason and never
pass it silently. Through the coordinator, `mode: run` with a reason
behaves as follows:

- the planner admits as if inside the window, and its report says why;
- the prepare job starts the window job at once;
- the loop ends when nothing more is admissible;
- each dispatch passes the reason on, and the run's 9700X job honours it after
  the lock.

**The ledger** carries over through the coordinator's artifacts: this run's
prepare job's, then the newest from main's previous runs. It is published to
`buster14a/buster-performance`'s `nightly-ledger` branch (`nightly/main-coverage.tsv`
and `nightly/reports/CYCLE.md`) when the `nightly-ledger` environment holds
`BUSTER_PERFORMANCE_LEDGER_TOKEN`. That token needs contents write access to
that repository alone, and the environment should allow main only. Each run's
own record is the source the ledger is rebuilt from, so a lost ledger costs
only the re-harvest.

Publication is best effort. A refused token, a network error or a branch that
moved on meanwhile gives a warning with git's reason. It changes nothing else:
the prepare job has already handed over its outputs, the window job's night
goes ahead, and the next run publishes again. A fresh history is pushed only
to a branch that doesn't exist yet, never forced over one that does.

**The report** gives each status's count; this cycle's selected, completed,
failed and deferred runs and hosted preparations; reuse; the oldest carried-over
state; the remaining states; and evidence links. It warns when the week's
arrivals exceed the completions observed per cycle, or when the oldest pending
state is over three days old.

**Capacity.** In late October main gained 31 to 82 first-parent commits a day.
Nightly runs are bounded at 25 minutes, but PR jobs took 4 to 5 minutes. A
three-hour window holds about 25 to 30 such runs, so the backlog drains only
where pushes measure most states during the day. The report's alert
shows when they don't.

**Rollout.** The switch is `vars.BUSTER_NIGHTLY`. Until it is `on`, scheduled
coordinator runs are dry runs: they harvest, plan and report, and pushes measure
every plain run. A dispatch with `mode: run` can try the coordinator first. With
the switch on, pushes ran hosted rows alone and kept off the 9700X (since
#1430 they run nothing), and the
coordinator dispatches their native correctness and targeted measurements into
the window. `9700x.yml`'s daily `23 7 * * *` run is removed (2026-11-05): the
snapshot's comprehensive run is the daily run, so with the switch off there is
none, and a person dispatches `purpose: full` instead.

**Open items** this repository can't close:

- the ledger's publication token;
- whether the coordinator's capacity drains the observed arrivals, which only
  enabled cycles will tell.

## Sanitizer builds and detection policy

Diagnostic builds default to production `-O3`, with line information, frame pointers, PIE and
`-fno-sanitize-recover=all`. Ordinary and benchmark builds retain `-O3` and their
existing fixed-address link. `build.sh print-compile-flags` and
`print-sanitizer-flags` expose the actual arguments as NUL-delimited arrays.
Instrumentation is applied to every build unit and the final link; it cannot
be combined with the private baseline runtime used for QEMU smoke tests.
`BUSTER_SANITIZER_OPTIMIZATION=1|2|3` remains available for explicitly selected
diagnostic builds. The hosted matrix runs at `-O3` throughout, without separate
`-O1` sanitizer rows. Its sanitizer rows and the fuzz campaigns run on
merge-queue candidates and on the daily and dispatched runs, not on pull
requests, whose
hosted jobs are the plain builds of both architectures.
The sanitizer rows' job limit is 360 minutes and each command's is 7200 seconds:
the driver's in-process checks run the whole compiler per fixture and take tens
of minutes each with a detector, while the plain rows have a 120-minute job
limit and 600-second command limit.

| `BUSTER_SANITIZER` | Checks |
| --- | --- |
| `none` (default) | Ordinary optimized build |
| `address` | ASan, UBSan, bounds, leaks, stack use after scope and return |
| `undefined` (manual only) | Standalone UBSan and bounds for targeted investigations |
| `thread` | TSan across the correctness inventory, but the compiler's own `main.c` checking comparison |
| `memory` | MSan with level-two origin tracking across the correctness inventory, but the compiler's own `main.c` checking comparison |

The `address` mode combines ASan, UBSan, bounds and leak checking in one build
and run; its detector probes verify each kind of failure before the full suite.
TSan and MSan retain separate builds because their runtimes cannot be combined
with ASan. Removing the standalone UBSan CI rows saves eight builds and suite
runs (two architectures, each row's shards) but removes an independent instrumented
execution: standalone UBSan has a different allocation layout and can continue
past failures that would stop ASan. `BUSTER_SANITIZER=undefined` remains
available for targeted investigation.
The checks cover the host compiler implementation. Generated machine code and
external reference tools retain their own execution and validation contracts;
bootstrap instrumentation does not instrument Buster's generated instructions.
Generated entry points do not carry Clang's function-type metadata prefix.
The native-call fixtures disable only the `function` check at their call
boundaries; ASan, the other UBSan checks and MSan remain enabled there. A separate
incorrect-function-type canary proves that normal host calls retain this check.
The source call-graph capture omits bootstrap sanitizer/coverage instrumentation,
whose runtime calls and generated globals are outside the source graph; its
analyzer still runs inside the instrumented compiler.
The other resource-exhaustion fixtures retain their cases and output/fallback comparisons.
ASan, UBSan, MSan and TSan builds inject compiler-owned arena-reservation and
lane-start failures in place of process-wide address-space/process limits. ASan,
MSan and TSan need room for shadow/fake-stack memory and runtime threads; even
standalone UBSan needs address space to report allocation failures. In particular,
`RLIMIT_AS=0` caused the UBSan runtime to overflow its stack and fail to mmap its
diagnostic buffer instead of reporting the compiler's intended reservation failure.
Ordinary builds retain the actual OS-limit tests. No detector is disabled for these
cases; injection controls are absent from normal builds. The injected limits are
process-wide, since lanes reserve arenas and start on their own threads.
A few fixtures run generated programs with FS pointing at the program's own TLS
block. The detectors reach their per-thread state through FS, so the code that runs
in that window (the link tests' libc stand-ins and C-runtime startup, and Tier 0's
call of a program with thread-locals, which switches FS by system calls of its own)
takes no sanitizer instrumentation. Outside that window the fixtures stay fully
instrumented.
Under ASan, MSan and TSan, the units driver's walk that fails each reservation in
turn is not run: each detector maps shadow of its own, whose high-water mark the
parent's `VmPeak` keeps above its `VmSize`, so a probe inherits a peak that hides
the growth a limit would fail at, and a limit below the shadow stops the detector
at its start. UBSan also skips that OS-limit walk, since its diagnostic runtime
cannot safely run under an artificially exhausted address space. The parser
driver's reservation check still runs under all sanitizers using injected arena
limits; ordinary builds retain the OS-limit walk.
TSan also kills a child that starts a thread after a fork from a process of
several threads, so under it `test-lanes`' child forked from a session's lane 0
(#905) runs a group of one.

The TSan and MSan rows leave out the checking comparison of the compiler's own
`main.c` (#129, `tools/ci/correctness.tests`), with `-DBUSTER_COMPLETE_DRIVER=1`
for the whole compiler: 4,864,411 preprocessed tokens on 2026-11-10. At
6,323,682 tokens the comparison's two paths held 7.5 GiB resident
unsanitized (measured with an `-O3` UBSan build on the 7940HS), and TSan's
shadow took the same check past 20 GiB before it was stopped; that cannot share
a 16 GiB hosted runner with any other command. The `-c` commands peak at
10 GiB under TSan and 6.6 GiB under MSan (`-O3`, `--lanes 1`), so those rows
run them in the workflow's separate "Compile main.c alone under TSan or MSan"
step after the suite, where the runner is otherwise idle; the plain and ASan
rows of both host architectures, plus the 9700X, run them in the suite. The
plain and ASan rows and the 9700X also run the checking comparison, so
the self-check remains required wherever the runner has the memory. Every other
command runs in every row.

`tools/ci/sanitizers.sh` replaces inherited runtime options with a fixed fatal
policy and matching symbolizer. Deliberate faults prove each enabled detector,
its failure exit code and source symbolization before the full suite starts.
These proofs use a separate directory. An unconditional report audit rejects
diagnostics from the actual suite even when a negative test expected its child
to fail. The libFuzzer crash-replay proof's planted abort writes its stack traces
to a directory of its own, and each one must name the probe's source. No
suppression file is used, and the only command a row leaves out is the `main.c`
checking comparison above, for its measured memory; `parallel` reports it as
skipped and counts it apart. Reports, detector proofs and compile commands are
retained as workflow artifacts.

Every sanitizer row and fuzz campaign first runs `tools/ci/sanitizers.sh host`,
after `tools/ci/setup-hosted.sh` and before the build, so that a report caused
by the runner can be traced to its host (#2060). It logs `uname -r`, one CPU's
model name and `flags` line, the `Seccomp:`, `Seccomp_filters:` and
`NoNewPrivs:` lines of `/proc/self/status`, `kernel.yama.ptrace_scope`, and the
container and VM that `systemd-detect-virt` reports, with the container marker
files and PID 1's `container=` variable. A missing file or tool prints
`unavailable`; the command always exits 0 and the step continues on error. It
changes no option and no audit: every non-empty report still fails the job.

For example, on a supported Linux host:

```sh
BUSTER_HOST_ARCH=x86-64 BUSTER_SANITIZER=address ./build.sh
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./build/compiler test-lanes
```

Use the owner's `cores` budget where installed and the shared hardware lock on
Benchpress, as described in `AGENTS.md`.

## In-process coverage-guided fuzzing

```sh
BUSTER_FUZZ_AVAILABLE=1 ./build.sh
./build/compiler --fuzz -seed=133 -runs=10000 -max_len=4096 \
    -dict=tests/fuzz/c.dict -artifact_prefix=/absolute/crashes/ /absolute/corpus/
```

Fuzz builds default to the combined `address` mode at `-O3`. Every compiler unit receives
`-fsanitize=fuzzer-no-link`; the link uses Clang's matching no-main libFuzzer
runtime. `--fuzz` hands control to `LLVMFuzzerRunDriver` before ordinary command
dispatch, preserving engine options, coverage, mutation, crash artifacts,
minimization and replay. Other compiler commands continue to work in that
executable. A missing runtime or incompatible TSan configuration is an error.
Where LeakSanitizer is linked, `--fuzz` first runs one recoverable leak scan,
before the engine arms its `-timeout` alarm. LeakSanitizer's one-time ptrace
probe forks and waits without retrying `EINTR`, and libFuzzer's `SIGALRM` has
no `SA_RESTART`, so an alarm during that wait printed a false "ptrace appears
to be blocked" report that failed the audit (#2060). A leak the early scan
finds is still reported and still fails the job.

The default original callback in `src/buster/compiler/fuzz.c` accepts inputs up
to 64 KiB. It compares scalar/native lexer output and exercises preprocessing,
parsing, checker-input construction, typed expression checking and initializer
planning. Rejection of malformed C is normal. All per-input owning state is
released, and source-directed include/embed lookups and existence queries
cannot read external files. Permanent enabled/disabled controls verify that
boundary against existing absolute paths. This callback does not execute
input-derived machine code or claim to fuzz every backend phase.

To replace the callback while retaining the same instrumented compiler and
driver, provide a C file that defines
`int buster_fuzz_test_input(const uint8_t* data, size_t size)`:

```sh
BUSTER_FUZZ_AVAILABLE=1 BUSTER_FUZZ_ENTRY=tests/fuzz/entry_override.c ./build.sh
./build/compiler --fuzz -runs=10000 /absolute/elf-corpus/
```

The file is included in the driver unit after the module interfaces, so it can
call their APIs directly. The shipped override exercises the ELF object/shared
header reader. It is an actual replacement for the default frontend callback.
Do not retain borrowed input pointers after the callback returns.

Hosted CI runs bounded frontend and ELF campaigns with seed 133, original C
seeds and newly compiled original ELF fixtures. Merge-queue candidates (main
pushes before the queue) run 10,000 inputs per campaign; scheduled/manual runs use 100,000. A third, deliberately
crashing entry proves the override, saved crash bytes and exact-input replay.
All three jobs retain their corpora, crash inputs, logs, compile commands and
matching executable, and on those runs must pass before hardware qualification can start.

## Hosted toolchain delivery

See [the hosted toolchain guide](hosted-toolchain.md) for verified preparation, the digest-pinned GHCR mirror and its promotion, cache/handoff recovery and offline shard installation.
