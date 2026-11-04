# Emission metadata protocol

`src/buster/compiler/emission_metadata.c` is #1056's tier-neutral source/frame
event protocol: bounded, producer-owned events with two-sided record-boundary
anchors, explicit generated origins, a frame-rule vocabulary, and transactional
count/place/fill resolution against final record starts and ends. Layout
validation has an exact scalar path and an AVX-512 twin; source/frame checks and
sparse resolution are scalar.

`src/buster/compiler/emission_metadata_test.c` supplies
`test-emission-metadata [--seed N]`, built by `build.sh` and run by the
correctness inventory:

```sh
./build/compiler test-emission-metadata [--seed N]
```

The standalone wrapper `tests/emission-metadata/test.c` gives the isolated
baseline, AVX-512F and sanitizer commands. The design, ownership contract,
record-boundary rules and remaining acceptance are in
[the emission metadata guide](../emission-metadata.md).

Its production consumers are the driver's `-g` source-row path (`c_check_debug.c`),
which validates the emitted code's source rows through the protocol and fails
closed on disagreement, and the emission's unwind tables, which resolve each
function's frame events through it after branch sizing (`emitted_unwind`, the
[emission guide](emission.md)'s `emitted_code.c`). The default compile's cost of
that path on the locked 9700X is in the emission metadata guide's "Cost and what
remains".
