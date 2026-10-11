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
Clang, ar, ranlib, system libc and development headers. Their actual binaries
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
OpenSSL's independent verifier disables Git replacement objects; its fixture also
rejects a replacement commit that would otherwise disguise changed source.
Additional native controls reject wrong gitlink commits, unknown paths and
populated optional integrations, while accepting absent and empty directories.

The bounded cells are `linux-x86_64 --debug shared`, first with `no-asm` and
then with ordinary architecture assembly enabled. Both Clang cells run before
Buster attempts. Perl Configure and configdata generation remain upstream.
There is no source patch, compiler-flag removal, disabled failing feature,
replacement C object, or fabricated successful compiler probe.

The native compiler proxy preserves upstream arguments and explicitly pins
both C compilers to Linux x86-64 with the baseline instruction set. Reference C actions use Clang;
candidate C actions and compiler identity/preprocessor queries use `ide cc`.
Assembly-only actions and object-only links use external Clang, explicitly
attributed in per-action provenance. Mixed C/assembly input is refused.
The normal configuration's assembly is upstream-generated external assembly;
it is not evidence for Buster intrinsic or SIMD code generation.
Archiving remains upstream make's explicitly configured ar/ranlib stage.

Each successful build runs upstream `list-tests` and `make test` with verbose
results and two test jobs. Failed builds record tests as not run. The raw
inventory and upstream test log retain the denominator, failures and skips;
the intake does not synthesize pass counts. Shared and static artifacts
produced by upstream are distinct and require subsequent qualified clients.

## Evidence and remaining acceptance

`summary.tsv` separates configure/build/test outcomes. `receipt.txt` keeps
TLS interoperability, mixed ABI, provider acceptance, security and performance
explicitly incomplete even if every build/test cell passes. Child `.command`,
`.stdout`, `.stderr`, `.status`, `.provenance` and artifact digests retain actual
invocations and outcomes. Source Git inventory and configdata dump retain
generated-input configuration. Children use existing bounded captures,
deadlines and fail-closed cleanup; a timeout or truncated log cannot pass.
The full upstream test suite has a 30-minute deadline per cell; other children
retain the existing ten-minute bound. Final TAP progress/summary is also printed
to the job log. The first portable reference attempt exceeded the shorter
original deadline, so it is not a passing test result.

The [hosted intake](../../../.github/workflows/openssl-qualification.yml)
runs only by explicit dispatch or pushes to the issue's owned experiment branch.
It adds no ordinary PR, merge-group or main workload. The hosted driver
bootstrap is the existing CI exception. Correctness development stays hosted;
diagnostic elapsed times cannot satisfy the approved exact-candidate 9700X
performance requirement.

Before issue completion, retain complete input/dependency/generated-file
licenses and object/link provenance; qualify upstream tests and known-answer
counts in both cells; add verification-enabled local TLS in both directions
with trusted independently built peers and rejection/error assertions;
exercise mixed-compiler stateful clients against both static/shared libraries
and selected providers; then request the issue's actual applicable Zen 5
cost/runtime evidence under #2334/#2761. No functional result establishes
constant time, production security or FIPS certification. Install only into
a disposable experiment prefix; never replace system libraries.
