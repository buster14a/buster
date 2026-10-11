# curl/libcurl qualification

[Harness index](../compatibility.md) · Owning milestone:
[#3181](https://github.com/buster14a/buster/issues/3181).

## First diagnostic cell

The native build driver exposes an opt-in Linux x86-64 build-system census:

```sh
./build.sh test_curl --self-test
./build.sh test_curl --census /absolute/pristine/curl /absolute/fresh-output /absolute/built/ide
```

Build the trusted Release `ide` first through the normal native driver.
The external checkout must be exactly the curl 8.16.0 Git tag,
[`11b991232fbcaa88e2b1faecac224416b0001e35`](https://github.com/curl/curl/commit/11b991232fbcaa88e2b1faecac224416b0001e35).
Its unchanged `include/curl/curlver.h` declares `8.16.0-DEV`; the release
archive's rewritten version header is a different source input. The version
oracle requires the Git header's exact `8.16.0-DEV` identity.
The output parent must already exist; the output itself must be absent and
outside upstream. Driver/output paths containing a CMake list separator (`;`)
are rejected. Compilation and correctness use authorized cloud hosts.
There is no automatic PR, merge-group or main workload.

This is a **blocker census**, not a completed curl compatibility promise.
A passing configure probe, linked CLI or `--version` result does not qualify
transfers, TLS verification, mixed-compiler ABI or performance. Current observed
results and exact candidate revisions belong on the issue and implementation PR.

## Declared configuration

The harness uses curl's unchanged CMake/Ninja graph and normal feature detection.
It independently configures Clang and Buster, each with static and shared
libcurl and the CLI, Debug (`-O0 -g`) and baseline `-march=x86-64`.
Buster is selected as `ide cc` with `CMAKE_C_COMPILER_ARG1`; no C source is
replaced or compiled by Clang in the candidate graph.

The initial protocol scope is HTTP/HTTPS (`HTTP_ONLY=ON`), with OpenSSL as
an external host-built TLS dependency, synchronous system resolution and IPv6.
c-ares, threaded resolution, compression (zlib/Brotli/Zstandard), HTTP/2,
HTTP/3/QUIC, SSH, IDN2, PSL, GSSAPI, GSASL and RTMP are explicitly disabled.
Other TLS backends are disabled. This deliberately declared first configuration
is identical in the reference and candidate and cannot be described as every
curl feature. curl debug instrumentation defaults remain unchanged.

Examples and manual/man-page generation are disabled; `BUILD_TESTING=ON`.
The upstream `test-nonflaky` target is attempted after successful construction.
Its normal `~flaky ~timing-dependent` filtering and documentation exclusions
remain visible in the retained commands/output. Preserve individual upstream
attempt/pass/fail/skip reasons; target exit status is only a stage outcome.
Missing test servers, Perl modules, certificates or other external prerequisites
remain failures/gaps, never fabricated successes.

## Evidence and controls

The existing native pristine Git guard is reused before and after the run.
Its self-test rejects wrong revisions, tracked edits, untracked and ignored
inputs, assume-unchanged and skip-worktree concealment. The harness rejects
reuse of an output directory and does not generate files inside upstream.

Each cell retains CMake's cache, generated headers and configuration logs,
`compile_commands.json`, Ninja's command graph and dependency inventory.
The native compiler/link launcher retains each child's cwd, length-prefixed
argv, stdout, stderr and status separately under `children/`. Configure
identification/probes that bypass launchers remain in
`CMakeFiles/CMakeConfigureLog.yaml`; an unrecorded probe cannot be counted
as a recorded compilation action.

Stage commands have a 600-second deadline and 16 MiB capture per stream,
using the existing bounded process helper. Timeouts, failed capture,
truncation and failed process cleanup fail the stage. The trusted reference
runs first; candidate failures do not suppress its construction or tests.
`summary.tsv` distinguishes configure/build/version/upstream-test outcomes.
Later stages of an unbuilt candidate are `not-run`.

The run hashes the selected compiler, native driver, Clang, CMake, Ninja and
curl COPYING, retains the complete immutable source tree ledger, and records
OpenSSL's detected version. A hosted run must additionally retain the actual
OpenSSL package versions, copyright, header/library hashes and machine records.
The configured paths in CMakeCache are authoritative; system dependency
versions are not inferred from the runner label. Complete system-header closure,
all linked artifact identities and object/action denominators still require
evidence accounting before the milestone's provenance gate is accepted.

## License intake

curl's pinned [COPYING](https://github.com/curl/curl/blob/11b991232fbcaa88e2b1faecac224416b0001e35/COPYING)
and CMake source SPDX notices identify its first-party terms as SPDX `curl`.
The pinned `tests/runtests.pl`, `tests/server/sws.c` and
`tests/secureserver.pl` also declare SPDX `curl`. Other test scripts/assets
can carry component-specific terms; these checks are not a complete test-asset audit. Retain the selected inputs' notices
before redistribution; this harness vendors no curl code or test assets.

OpenSSL is an external prerequisite. Record the selected distribution package's
copyright and component notices. For example, inspected upstream OpenSSL 3.5.0
[LICENSE.txt](https://github.com/openssl/openssl/blob/openssl-3.5.0/LICENSE.txt)
is Apache-2.0, but that source snapshot does not establish which package the
host used or cover every bundled component. Other host tools and libc retain
their own licenses. Buster's first-party grant remains unselected per
[LICENSES/README.md](../../../LICENSES/README.md); this work changes no license.

## Remaining milestone gates

After normal Buster configure/build succeeds, preserve the declared feature set
and qualify isolated loopback HTTP/HTTPS GET/upload, redirects, range/resume,
timeout/cancellation and concurrent transfers against independently specified
payload/status expectations. Test certificate and hostname rejection with
verification enabled. Keep expected TLS rejection separate from unexpected
errors.

Exercise easy/multi callbacks and lifecycle using a trusted client against
Buster libcurl and a Buster client against trusted libcurl, including the shared
library cell. Retain all applicable upstream test results and a useful transfer
workflow before considering Git transport integration (#3180).

Clean, real source-edit and header-edit rebuild costs, memory, library/CLI code
bytes and checked runtime remain separate evidence. Hosted elapsed times are
diagnostic only. Applicable exact-candidate Ryzen 7 9700X evidence is mandatory
for performance qualification (#2761/#2334); this harness dispatches no benchmark.
