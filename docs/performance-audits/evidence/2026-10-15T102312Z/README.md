# Placement evidence

Implementation `c05d9b1945eca31adc01a2681b1dcfd301c7cbdf`, baseline `84f051c13044938bcb8cbe1ac48ea3ee79defcff`.

- [Archive](placement-evidence.zip), 6,422,732 bytes.
- [SHA-256](placement-evidence.sha256): `fdcefe68814c220e86bce26f147d4010d8d13ddc9b31889e698c56c393ee30a9`.
- The archive's `evidence/provenance.json` binds compilers, source trees, inputs and build configuration.
- `evidence/final-*/summary.json` and adjacent raw perf files are the accepted native work observations; preliminary and rejected data remain separately named.
- `evidence/census-final/` is diagnostic instrumentation at the implementation head, with matching emitted artifacts. It is not a timing subject.
- `evidence/validation/` contains the completed local gates; the superseded sanitizer log is non-qualifying.
- Frozen binaries and generated artifacts remain private in the session workspace and are identified by SHA-256.
- The evidence driver SHA-256 is `ff634ca8c13d2c6a83d028360e112aa745c012e38179433cdea49832bef65af3`.

First-party Buster licensing is unselected/NOASSERTION. Existing upstream notices and license copies are included; no new external implementation is imported.
