# Custom native execution and benchmark contract

The requested service capability includes user-authored native programs,
assembly microkernels, prebuilt binaries and optional C workloads. Ordinary
execution and runtime benchmarks need no compiler experiment or build.
Compiler throughput and generated-program runtime are independent optional
measurements. A custom workload must be data selected by immutable identity,
rather than another compiled-in generator name for each benchmark.

MCP and CLI submissions go directly to the authenticated durable service and
return a job ID. Ordinary execution and benchmarks require no workflow
dispatch, PR, repository branch or per-experiment GitHub approval. CI is an
optional client of the same queue. The service authenticates its owner;
repository contribution permissions do not authorize execution.

The first [native execution slice](NATIVE_EXECUTION.md) accepts privately
uploaded static Linux x86-64 executables, including assembled microkernels.
CLI and bounded MCP upload tools return an immutable manifest identity for
direct submission to `native-execute-v1`. There is one program, fixed argv
and environment, and no dynamic runtime closure or optional compilation yet.
Runtime samples and custom compiler measurements remain unavailable in this
slice. The MCP adapter rejects caller commands and runtime argument fields;
successful execution is not a custom benchmark result. Source implementation
does not establish that a particular worker has the coordinated installation.

## Existing machinery

The native harness's `buster-throughput-workload-v2` descriptor already
retains a complete staged C/header inventory with per-file size and SHA-256,
whole-tree identity, resource/header/runtime closures, target and mode,
logical input bytes and correctness evidence. `check-workload` validates
that material; `admit-workload` performs functional object, compile-link and
runtime admission. Neither command collects a custom paired timing experiment.
See the [native workload contract](../throughput/README.md).

Reuse this inventory model and the existing native process collector and
paired statistics. A native program bundle additionally binds architecture,
ABI, runtime closure and a relative entrypoint within its immutable files.
Prebuilt programs skip compilation; assembly/C preparation selects an installed
toolchain profile and records its exact identity and output binary. The public
service selects the immutable bundle and operation/profile. Bounded program
arguments belong to the bundle and cannot select scheduler commands or host
paths. The installed recipe owns preparation, resource limits and result
locations.

## Separate measurements

| Operation | Timed work | Required untimed gate |
| --- | --- | --- |
| Ordinary execution | No performance series; run the immutable program and retain outcome/logs/artifacts | Program identity, runtime closure and declared result contract |
| Native runtime benchmark | Run a prebuilt or prepared program for declared work/iterations | Binary identity and declared checksum/transcript or result oracle |
| Source to object | Compiling the declared frozen translation units | Required input/resource closure and output contract |
| Source to executable | Compiling and linking the declared frozen program | Correct artifact and runtime dependencies |
| Generated-program runtime | Executing the built A/B programs for declared work/iterations | Build both programs, verify their binaries and independent expected checksum/transcript |

Runtime validation cannot substitute for compiler correctness, and a changed
binary is not by itself a runtime correctness failure. Use the declared
semantic oracle for intentional output changes. Keep raw compiler and runtime
samples, denominators, validity and decisions separate. Record microbenchmark
launch overhead and a fixed work count; do not choose repetitions after
looking at the result.

The reservation spans input staging, builds, all correctness gates, both
measurement series, final sealing and verified process-tree cleanup. Custom
code runs under the existing contained candidate identity. Client disconnects
and polling do not release admission or start another experiment.

The owning implementation issue is [#2648](https://github.com/buster14a/buster/issues/2648),
a child capability of #437. Its acceptance includes a user-authored assembly
microkernel, a prebuilt program needing no compiler, an ordinary task unrelated
to compiler measurement, and a custom C case without new presets. Versioned
request identity, immutable materialization, finite budgets, oracle failures,
timing separation and tamper-checked export/replay are shared requirements.
Functional tests and real service-profile qualification remain separate
evidence gates.
