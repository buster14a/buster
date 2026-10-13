# OpenSSL qualification

[Harness index](../compatibility.md). This opt-in Linux x86-64 intake belongs
to [#3184](https://github.com/buster14a/buster/issues/3184). Build/test intake
does not complete the issue's TLS, mixed ABI, provider or performance gates.

## Pin and dependencies

OpenSSL 3.5.9 is pinned to
[45e844fa2a14ec92d146bd8f5778ac130b6625fb](https://github.com/openssl/openssl/commit/45e844fa2a14ec92d146bd8f5778ac130b6625fb).
The 3.5 series is [LTS](https://openssl-library.org/policies/releasestrat/).
The pin is an input identity, not a deployment recommendation.

The pinned [LICENSE.txt](https://github.com/openssl/openssl/blob/45e844fa2a14ec92d146bd8f5778ac130b6625fb/LICENSE.txt)
grants Apache-2.0 for OpenSSL. The bundled build generator
[Text::Template 1.56 license](https://github.com/openssl/openssl/blob/45e844fa2a14ec92d146bd8f5778ac130b6625fb/external/perl/Text-Template-1.56/LICENSE)
offers GPL-1.0-or-later OR Artistic-1.0-Perl; this is separate from
OpenSSL's library license. Test-asset and component-specific notice inventory
remains incomplete. No source, keys or third-party code is copied into Buster.

External prerequisites are Git, Perl with its core test modules, make,
Clang, ar, ranlib, tar for evidence transport, system libc and development headers. Their actual binaries
and installed package/license inventory remain environment-specific.
Upstream's unchanged [INSTALL.md](https://github.com/openssl/openssl/blob/45e844fa2a14ec92d146bd8f5778ac130b6625fb/INSTALL.md)
and [NOTES-PERL.md](https://github.com/openssl/openssl/blob/45e844fa2a14ec92d146bd8f5778ac130b6625fb/NOTES-PERL.md)
define build and generation requirements. These tools are external experiment
inputs; this adds no runtime dependency or system installation.

## Native commands

With a trusted Release compiler and an existing output parent:

~~~sh
./build.sh test_openssl --self-test
./build.sh test_openssl /absolute/openssl /absolute/fresh-results /absolute/buster/build/Release/ide /absolute/clang
~~~

The driver verifies the exact Git pin and rejects tracked, untracked, ignored,
sparse and hidden-index inputs through the existing compatibility controls.
The output is an exclusively claimed fresh directory outside the canonical
upstream source directory. Builds occur out of tree and source admission
is repeated after the cells. The pin's 11 optional test-integration gitlinks
must match their declared paths/commits and remain absent or empty. Populated
submodules are refused by this intake. Their upstream tests remain explicitly
unsupported/skipped where upstream reports them; this is not an all-integration
qualification. The controls' isolated Git fixture covers
wrong pin, dirty tracked source, ignored/untracked inputs and hidden index flags.
Every tracked regular file is independently hashed without Git filters and
matched to its admitted blob, including canonical-path checks. Git's cached
status alone cannot establish source identity. OpenSSL's independent verifier
disables Git replacement objects; its fixture also
rejects a replacement commit that would otherwise disguise changed source.
Additional native controls reject wrong gitlink commits, unknown paths and
populated optional integrations, while accepting absent and empty directories.

The bounded cells are `linux-x86_64 --debug shared`, first with `no-asm` and
then with ordinary architecture assembly enabled. Both Clang cells run before
Buster attempts. Perl Configure and configdata generation remain upstream.
There is no source patch, compiler-flag removal, disabled failing feature,
replacement C object, or fabricated successful compiler probe.

The native compiler proxy preserves upstream arguments and explicitly pins
both C compilers to Linux x86-64 with the baseline instruction set. The driver hashes both compilers once per run, passes the digests
to the proxy for per-action provenance and re-hashes them after the cells;
`receipt.txt` records `compilers_unchanged`. Reference C actions use Clang;
candidate C actions and compiler identity/preprocessor queries use `ide cc`.
Assembly-only actions and object-only links use external Clang, explicitly
attributed in per-action provenance. Mixed C/assembly input is refused.
The normal configuration's assembly is upstream-generated external assembly;
it is not evidence for Buster intrinsic or SIMD code generation.
Archiving remains upstream make's explicitly configured ar/ranlib stage.

Each successful build runs upstream `list-tests` and
`make test HARNESS_JOBS=4 HARNESS_TIMER=1`. Verbose `V=1` is not passed:
upstream's `test/run_tests.pl` then ignores `HARNESS_JOBS` and runs every
recipe serially. Without it, upstream still prints the full output of
failing tests, each recipe's result, skip reason and timing, and the final
`Files=`/`Tests=` summary. Failed builds record tests as not run. The raw
inventory and upstream test log retain the denominator, failures and skips;
the intake does not synthesize pass counts. Shared and static artifacts
produced by upstream are distinct and require subsequent qualified clients.

## Evidence and remaining acceptance

`summary.tsv` separates configure/build/test outcomes. `receipt.txt` keeps
TLS interoperability, mixed ABI, provider acceptance, security and performance
explicitly incomplete even if every build/test cell passes. Child `.command`,
`.stdout`, `.stderr`, `.status`, `.provenance` and artifact digests retain actual
invocations and outcomes. Source Git inventory and configdata dump retain
generated-input configuration. The evidence is packed natively as `evidence.tar.gz`, with archiver/archive
identities retained separately. This preserves upstream's deliberate colon-bearing
test filenames, which GitHub's artifact uploader cannot accept directly. A native
control checks that `file:server.pem` survives inside the archive.
Children use existing bounded captures,
deadlines and fail-closed cleanup; a timeout or truncated log cannot pass.
All children retain the original ten-minute bound. The unmeasured 600-to-1800
second test-limit increase was reverted; the portable reference still timed out
at the larger limit. The measured cause is serial execution (see below), not
the bound. Final bounded TAP progress/summary is also printed to the job log. The
[initial census](https://github.com/buster14a/buster/actions/runs/38131412845/job/114446340801)
built both Clang references and passed optimized reference tests. Both Buster
cells stopped on `-m64`, owned by [#3458](https://github.com/buster14a/buster/issues/3458)
and [PR #3442](https://github.com/buster14a/buster/pull/3442). That run's artifact
upload failed on the colon filename, so only its job log is retained.

The [final historical intake](https://github.com/buster14a/buster/actions/runs/38133873942/job/114453613810)
at `e65b49e1efe9f43e671afdec171f8d0238460586` failed: portable reference tests
timed out at 1800 seconds without capture truncation, optimized reference tests
exited successfully, and both candidate builds rejected `-m64`; candidate tests
were not run. Its separate self-host check reached a 40,941,128-byte fixed point.
[Artifact 11702275158](https://github.com/buster14a/buster/actions/runs/38133873942/artifacts/11702275158)
was uploaded successfully. The native log records inner archive SHA-256
`abfec46e016efda32db0b311b2ade445466ac772dc3e9b4a843b6186f3de5e82`;
this is retained producer evidence, not independent archive certification or a
complete inspected TAP denominator.

### Local diagnostic runs (2026-10-13)

These ran in a shared 4-vCPU Linux x86-64 cloud container with load 5-15 from
other sessions. The compiler was Clang 18 and a Clang-built Release `ide`
(SHA-256 `b2f156e26add2fa12a86d2f0c89bf351cea63f30ac717f514c438c9800105f50`).
Wall times are diagnostic only; CPU seconds are given beside them. These runs
do not show that hosted cells fit the 600-second bound.

At `dcb1469b613acaa60b206988c8a204691982d28f` (before the fixes), every cell
failed:

- Portable reference tests timed out after 131 of 347 recipes. With `V=1`,
  upstream printed `Warning: HARNESS_JOBS > 1 ignored with HARNESS_VERBOSE`.
- An unbounded rerun of that suite needed 1621 CPU seconds, so two jobs
  cannot finish it in under about 810 seconds.
- The Buster portable build hit its bound after 232 C actions, all of
  which passed, with only 55 seconds of total compile time. Each proxy
  action spent about 2 seconds (4 to 6 under load) hashing the 69 MB `ide`;
  the compile itself took about 0.02 seconds.

At the fixed revision, each proxy action takes 0.02 to 0.03 seconds, and its
provenance keeps the digest. All four cells were rerun:

| Cell | Build | Tests |
| --- | --- | --- |
| Reference portable | 257 s | all 347 recipes, 4518 tests, in 582 s wall / 1618 CPU s |
| Reference optimized | 259 s | all 347 recipes in 211 s wall / 554 CPU s |
| Buster portable | 974 actions in 58 s, then `ssl3_cbc.c` fails (#3324) | not run |
| Buster optimized | `-Wa,--noexecstack`, then `-Qunused-arguments` (#1418) | not run |

Both reference suites fail only `04-test_bio_dgram.t`, in its two `AF_INET6`
iterations. The container has no IPv6 (`/proc/net/if_inet6` is absent).
Treating that as the cause is unverified, and upstream tests are not changed.
The `#3324` failure is a cast to a function-pointer type name with a named
parameter, `(void (*)(void *ctx, ...))`.

Whole-project execution follows the [main-only shared OS owner](../../external-project-ci.md).
The obsolete branch-push/manual project workflow is removed. OpenSSL has no
qualified shared input recipe yet; this PR does not schedule upstream builds or
tests in PR/merge-group CI. The existing hosted TCC proof job runs only the
isolated native `test_openssl_controls` source/routing/archive fixtures, with no upstream
checkout or project compilation. This separate zero-argument command cannot
route to an upstream cell; extra arguments fail before any controls run.
Keep this intake draft until hosted cells reproduce the local results above
and the applicable acceptance evidence is complete. Correctness development stays hosted; diagnostic elapsed
times cannot satisfy the approved exact-candidate 9700X performance requirement.

Before issue completion, retain complete input/dependency/generated-file
licenses and object/link provenance; qualify upstream tests and known-answer
counts in both cells; add verification-enabled local TLS in both directions
with trusted independently built peers and rejection/error assertions;
exercise mixed-compiler stateful clients against both static/shared libraries
and selected providers; then request the issue's actual applicable Zen 5
cost/runtime evidence under #2334/#2761. No functional result establishes
constant time, production security or FIPS certification. Install only into
a disposable experiment prefix; never replace system libraries.
