# Emission metadata protocol

`src/buster/compiler/emission_metadata.c` is #1056's tier-neutral source/frame
event protocol: bounded, producer-owned events with two-sided record-boundary
anchors, explicit generated origins, a frame-rule vocabulary, and transactional
count/place/fill resolution against final record starts and ends. Layout
validation has an exact scalar path and an AVX-512 twin; source/frame checks and
sparse resolution are scalar, with no locked-9700X measurement yet.

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

The driver's `-g` source-row path (`c_check_debug.c`) is the current production
consumer: it validates Tier 0's declaration, kept-entry and epilogue rows through
the protocol and fails closed on disagreement. The common emitted-function
result, producer-owned frame publication, Tier 1 `-g` and the unwind writer
remain; #1056 remains open.
