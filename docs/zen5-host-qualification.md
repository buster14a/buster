# Ryzen 7 9700X host qualification and A/A calibration

[Benchmarking guide](agents/benchmarking.md) · [Issue #426](https://github.com/buster14a/buster/issues/426)

This document defines the repository-owned evidence formats used to qualify the dedicated Ryzen 7 9700X for performance acceptance. It does **not** make a machine qualified merely because a script exits successfully, and it does not create another execution path to the host.

The physical run must occur inside the server-owned benchmark-service reservation and quiet phase. The service owns materialization, preparation, execution, cleanup, sealing, and export. `tools/zen5_host_qualification.py` and `tools/zen5_aa_noise.py` only capture/replay fixed facts inside that admitted job or validate the exported bytes offline.

The ownership boundaries remain:

- #422: physical-host qualification and managed-isolation evidence;
- #426: A/A noise calibration, versioned dedicated policy inputs, and model-specific PMU qualification;
- #880: live deployment and recovery proof for the service path;
- #881: the complete fixed `native-retirement-performance-v1` recipe;
- #882 and #883: pre-cutover and final-tree candidate decisions.

A direct SSH run, the retired `zen5-audit.yml` workflow, an ordinary self-hosted Actions job, or a successful cooperative lock does not satisfy these boundaries.

## Version-1 PMU manifest

`tools/zen5_pmu_events_v1.json` is an immutable, model-specific manifest for `AuthenticAMD` family 26 model 68, the Ryzen 7 9700X used by `benchpress`. Its raw event definitions are bound to Linux's `amdzen5` PMU tables at commit `518e5b794c06c0f0eb40df3e202274a66202c137`.

| Group | Event | Event select | Unit mask | `perf_event_attr.config` |
|---|---|---:|---:|---:|
| core execution | instructions / cycles / branches / branch misses | architectural | architectural | `0x1`, `0x0`, `0x4`, `0x5` |
| L2 hierarchy | `l2_cache_req_stat.dc_access_in_l2` | `0x64` | `0xf8` | `0xf864` |
| L2 hierarchy | `l2_cache_req_stat.ls_rd_blk_c` | `0x64` | `0x08` | `0x0864` |
| fill sources | `ls_any_fills_from_sys.all` | `0x44` | `0xff` | `0xff44` |
| fill sources | `ls_any_fills_from_sys.local_l2` | `0x44` | `0x01` | `0x0144` |
| fill sources | `ls_any_fills_from_sys.dram_io_all` | `0x44` | `0x48` | `0x4844` |
| data TLB | `ls_l1_d_tlb_miss.all` | `0x45` | `0xff` | `0xff45` |
| data TLB | `ls_l1_d_tlb_miss.all_l2_miss` | `0x45` | `0xf0` | `0xf045` |

All selectors are user-space scoped. Each group includes its own instructions denominator and is collected separately from ordinary timing trials. Version 1 requires three diagnostic repeats and at least a 90% running fraction for every required event. A missing, unsupported, malformed, multiplexed-below-threshold, or uncounted event is `null` plus an invalidity reason; it is never zero.

`cache-misses` is intentionally absent from the model-specific groups. A generic cache-miss count may be retained as a generic diagnostic, but must never be relabeled as an L1, L2, or DRAM event. The DRAM/IO event retains the hardware definition's MMIO component in its name and semantics.

IBS fetch/operation PMUs are inventoried separately. Availability, sysfs type, format, capabilities, and CPU mask are retained. Version 1 does not collect IBS samples and records count/running fraction as `null`; a later attribution experiment needs its own reviewed sampling policy.

## PMU capture and replay

Inside an already admitted service job, invoke the fixed capture against the exact immutable workload:

```sh
python3 -B tools/zen5_host_qualification.py capture \
  --output result/host/zen5-pmu-v1.json \
  --cpu 2 \
  --repository-root "$IMMUTABLE_CHECKOUT" \
  --working-directory "$IMMUTABLE_CHECKOUT" \
  --input "$IMMUTABLE_INPUT" \
  --output-artifact "$EXPECTED_OUTPUT" \
  --environment-input "$INSTALLED_PROFILE" \
  -- "$TRUSTED_COMPILER" ...
```

The service recipe, not a request, selects the command, CPU, inputs, output contract, and environment identities. The result retains:

- exact repository revision/tree and clean-checkout state;
- executable, input, output, profile, and external-environment hashes;
- CPU model, microcode, kernel, boot, topology, SMT, firmware, memory, transparent-hugepage, perf, and power-policy facts;
- the complete fixed `perf stat` invocation and raw JSON for every group/repeat;
- per-event count, event runtime, percentage/running fraction, raw perf row, and explicit status;
- workload exit status plus raw stdout/stderr byte counts and SHA-256 digests;
- optional IBS inventory;
- a separate `qualification_status` and complete invalidity list.

Replay the exported bytes without access to the measured host:

```sh
python3 -B tools/zen5_host_qualification.py validate result/host/zen5-pmu-v1.json
```

Replay distinguishes evidence integrity from physical success. A capture that truthfully says `invalid` remains structurally replayable. It can never be interpreted as `pmu-qualified`; conversely, a `pmu-qualified` result must have no replayed or declared qualification failure.

Compare two captures before reusing a qualification:

```sh
python3 -B tools/zen5_host_qualification.py compare \
  old/zen5-pmu-v1.json new/zen5-pmu-v1.json \
  --output requalification-diff.json
```

Any environment-contract change requires requalification. This includes CPU/microcode, kernel/perf, firmware, memory description, topology/SMT, power policy, transparent huge pages, and the operator-supplied external profile/toolchain identities. A changed boot identity is reported separately even when the static contract is unchanged.

## Fixed-count immutable-binary A/A record

`tools/zen5_aa_noise.py` validates and summarizes raw same-binary A/A observations exported by the service. It does not execute the compiler itself.

Version 1 freezes the following plan before the first observation:

- two rounds;
- sixty pairs per round, matching the minimum admitted by the versioned retirement statistics;
- four contiguous blocks of fifteen pairs in each round;
- every round contains all four `(label assignment, path assignment)` combinations: normal/normal, swapped/normal, normal/swapped, swapped/swapped;
- AB/BA alternates deterministically inside each block;
- an explicit, predeclared minimum gap separates blocks;
- two distinct pathnames must hash to the exact same binary bytes and mode;
- every execution retains wall time, peak RSS, exit status, output digest, logical label, pathname, sequence, and monotonic bounds;
- exactly 120 planned slots remain present, including rejected or invalid slots;
- stopping is fixed-count, and no outlier is deleted.

Print the canonical schedule and digest with:

```sh
python3 -B tools/zen5_aa_noise.py schedule --output aa-schedule-v1.json
```

Validate a service-produced raw capture:

```sh
python3 -B tools/zen5_aa_noise.py validate result/host/aa-capture-v1.json
```

Produce the descriptive model:

```sh
python3 -B tools/zen5_aa_noise.py analyze \
  result/host/aa-capture-v1.json \
  --output result/host/aa-noise-model-v1.json
```

For wall time and peak RSS, the model reports logical-label, physical-path, and execution-order effects; absolute nearest-rank quantiles; pair-center drift; lag-one serial correlation; and per-round/per-block medians. The reported 95th-percentile absolute label effect is named `empirical_resolution_fraction`. It is an observation for that exact host/profile/binary/workload, not a universal acceptance threshold.

Any invalid or missing slot makes the analysis `invalid` and suppresses metric summaries. The raw slot remains in the capture. There is no retry-until-green path, optional stopping, outlier deletion, candidate comparison, equivalence declaration, or performance verdict in this tool.

## Statistical decision boundary

The dedicated candidate decision remains the versioned native implementation in `tools/throughput/retirement_stats.h` and the approved `native-retirement-performance-v1` contract. That path covers unchanged pairs, real regressions, broad intervals, one-round-only signals, family correction, and invalid data. It reports pass/regression/inconclusive/invalid under the predeclared simultaneous family.

The A/A model supplies empirical host/profile context and validity evidence. It does not modify the ordinary shared-CI guard, replace the approved practical margins, promote instruction counts to an acceptance metric, or reinterpret “no confirmed regression” as equivalence. A future instruction-count acceptance metric would require its own repeated paired counter experiment; three diagnostic medians remain profiling evidence only.

## Recovered 2026-09-19 diagnostic evidence

`docs/performance-audits/2026-09-19T222922Z.md` and its evidence directory preserve the useful portion of Actions run `35412010412` before its artifact expires. The exact 9700X accepted the Linux perf aliases for the L2, fill-source, and dTLB events and returned nonzero counts. That is useful model/event discovery evidence.

It is deliberately classified `diagnostic-only-not-qualified`: it had five sequential timing runs rather than paired A/A, no label/path swaps or separated fixed blocks, no raw event-attribute encodings or enabled/running fractions, no current service reservation/lease/cleanup receipt, and no representative frozen candidate verdict. The retired workflow must not be restored as an alternate executor.

## Offline regression checks

The two tools have dependency-free self-tests suitable for ordinary hosted CI:

```sh
python3 -B tools/zen5_host_qualification.py --self-test
python3 -B tools/zen5_aa_noise.py --self-test
```

These checks validate schemas, exact event encodings, unavailable/null behavior, running-fraction rejection, successful and invalid replay, fixed schedule coverage, label/path/order reconstruction, drift/serial summaries, missing-slot rejection, and the prohibition on turning invalid evidence into a qualified result. They do not claim a physical host run.
