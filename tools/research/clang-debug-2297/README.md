# Buster debug versus Clang: bounded hosted capture

Owning research record: https://github.com/buster14a/buster/issues/2297 .
The earlier archived debug-off FAST versus Clang O2 comparison is an optimization
tradeoff and does not decide debug/debug winners. This capture measures generated
program configurations, keeping the compiler executable a trusted Clang-built
Release Buster in every lane.

Production source baseline is d4ca72fb4e97ae0a410b0d03258b00495fa45e13.
The research commit changes no compiler, build-driver or CMake implementation;
the workflow asserts that relationship before building. The exact branch head,
compiler binaries, producer compile database, installed tool versions, host,
source files, output bytes and all raw samples accompany each result.

Three configurations: Buster cc -O0 -g -fregister-allocator=fast with ordinary
bounded canonical passes enabled; Clang -O0 -g; Clang -O2 -g. Buster native -O0
does not disable its bounded passes. Baseline CPU spellings are -march=baseline
for Buster and -march=x86-64 for Clang. Other common contract flags and target
triples are recorded verbatim; equal spelling does not imply equal pipelines.
Ordinary -g format defaults remain visible (Buster DWARF4; Clang's own default).

The initial branch push runs the research workflow once on GitHub-hosted Ubuntu
26.04. All compiler building, execution, debugging and measurement happen there.
Seven rotated compilation observations and seven runtime observations plus
warmups are predeclared. Each compiler sees identical input files per workload
and the same final object path per source; compilation excludes external linking.
All objects use the same Clang-driven linker for execution. Synthetic checksums
also have an independent Python scalar model. cJSON uses cross-lane parse/print
checksums; this is not the complete upstream cJSON test suite.

The C sampler captures CLOCK_MONOTONIC wall time around fork/exec/wait4 and Linux
ru_maxrss, avoiding Python's inherited RSS floor. It includes child startup and
artifact publication and excludes launching the sampler. RSS is a greatest
process peak, not summed simultaneous whole-tree memory. Debug verification and
GDB queries occur separately from timing. Wrong debugger values, unavailable
values and missing/out-of-scope variables remain distinct results.

Results are exploratory medians/ranges on a shared cloud VM, not dedicated-host
performance acceptance or universal compiler rankings. A failure retains commands
and logs and cannot win a numeric cell. No retries seek a preferred result.
Compare debug/debug first; show debug/O2 as the separately named optimization
gap. For lower-is-better X, reduction is 100*(1-X_winner/X_other); runtime/compile
speed factor is slower/faster. Source count, .text, .debug_* and total object
bytes have separate meanings and denominators. Nothing sums overlapping pass gains.

## Inputs and licenses

All research Python/C fixtures are first-party work or adapted from Buster's own
#1948 archive at d044412afa395deef9092221d2596e7f40f02704. The generated scalar and
macro fixtures reproduce the archive bytes; runtime/driver main functions use a
single final return. Buster first-party license is unspecified, as verified in
LICENSES/README.md at the production baseline; #621 owns the missing grant.
No external library implementation is vendored. cJSON 1.7.19 is a separate
checkout at c859b25da02955fef659d658b8f324b5cde87be3, verified MIT LICENSE at that
revision; the measured C/H files and their hashes are retained with evidence.

Clang21.1.8 upstream license is Apache-2.0 WITH LLVM-exception plus retained
legacy/component notices, verified in clang/LICENSE.TXT at
2078da43e25a4623cab2d0d60decddf709aaea28 (blob
24806ab4c9eb291db4d28e159901f7a0901b9fd1). The Ubuntu binary/package is separately
identified; its package patches are not asserted to equal that upstream commit.

Actions checkout11bd71901bbe5b1630ceea73d27597364c9af683 and
upload-artifactea165f8d65b6e75b540449e92b4886f43607fa02 LICENSE files are verified
MIT, each blob a67dca8b4f65d6bd351f6b1e333ce2cd84d843a5. Dependency inventories
are not asserted exhaustive. Executed GDB/binutils versions and package notices
are captured; primary tool banners distinguish their GPL terms from project terms.

## Reproduction

Use the pinned workflow for compiler construction through existing build.c.
Generate the deterministic fixtures with generate_sources.py, place the pinned
external cJSON C/H alongside them, compile measure_child.c with trusted Clang,
then run capture.py with --buster, --clang, --sources, --output and --measure-child.
Run debug_probe.py with --buster, --clang and --output separately. The artifact
retains exact commands, so reproduction does not depend on abbreviated prose.
