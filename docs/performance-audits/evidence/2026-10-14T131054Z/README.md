# SDK guard follow-up evidence

Implementation: `944623bf6ca68cced2e7e458adf89dcec042c6be`.
Original storage evidence remains in [the preceding bundle](../2026-10-14T125135Z/README.md).
This bundle records the two-line test-helper guard correction, tests-off SDK
build/install/client, four-lane ASan/UBSan suites, fresh native size/hash,
self-host, source-size and the final frozen compile/output check.

The comparison summary pins the new executable and every observation. Its
raw directory and all validation/failed-hosted-job logs are preserved in
`raw-evidence.tar.gz`. `identities.json` records the final production build
command, binary hashes, stripped size, section table and source identities.
`SHA256SUMS` covers every file in this bundle except itself.

Reproduce the production and frozen comparisons with the preceding bundle's
commands, substituting the final implementation above. The SDK sequence is
[documented with the external example](../../../../tools/embed_example/README.md):
use a fresh owned `build/sdk` tree with `--no-include-tests` and
`-DBUSTER_COMPILER_SDK=ON`, build `buster_compiler`, install to an owned prefix,
then configure/build/run the external client against that installation.
`./build.sh test_self_host --build-directory build/native --config Release`
repeats the final self-host check. The four-suite sanitizer command and all
actual logs are retained; no earlier full test suite is labelled as a new run.

These are local Linux/7940HS results, not Zen 5 or native Apple/Windows evidence.
Buster's first-party license is unselected; existing Arm-derived data retain
the permission-review boundary. See [the license inventory](../../../../LICENSES/README.md)
and [third-party notices](../../../../THIRD_PARTY_NOTICES.md). No new upstream
material is added.
