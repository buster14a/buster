# Using the benchmark service

This is the task-oriented guide: how to run your own program on the benchmark
host, sample its runtime, and get the evidence back, from a shell or from an
MCP client such as Claude Code, Codex or ChatGPT. It describes the same-host
deployment that #437 accepts. The contracts behind each step are in the
documents linked at the end; installation and recovery are in
[deploy/SAME_HOST_MCP.md](deploy/SAME_HOST_MCP.md).

Nothing here needs a pull request, a branch, a compiler revision or a
workflow dispatch. You submit directly to the service and get a job number
back.

Uploading through MCP costs one tool call per 432 bytes (#2701), and
`native-runtime-v1` returns no program output (#2702). To time a small C
workload and read what it printed, the owner can instead add the source under
[`benchmarks/9700x/`](../../benchmarks/9700x/README.md) in a pull request; that
path runs outside the service, without its containment or sealed results.

## The model in one page

- **One job at a time.** The service owns the machine for a whole job, from
  preparation to cleanup. Other submissions wait in a queue of at most eight.
- **You submit data, never commands.** A job names an installed *recipe* and
  immutable inputs. It cannot choose a command line, a path, an environment
  variable or a resource limit on the host.
- **Your program is an upload.** You upload one static Linux x86-64
  executable. The service stores it under a digest and gives you a *manifest
  digest*; that digest is what you submit.
- **Your program runs contained.** It runs as a separate unprivileged account
  inside a systemd sandbox with no network, a read-only view of its inputs and
  one small scratch directory.
- **Results are sealed.** Every job leaves a manifest, a bundle index and its
  logs under three digests. You fetch the archive and can verify it offline.
- **The service is silent while a job runs.** Requests made during a job are
  refused as `busy` or time out after 30 seconds. That is how a job is kept
  undisturbed; see [While a job is running](#while-a-job-is-running).

### Recipes

| Recipe | What it does | Inputs |
| --- | --- | --- |
| `native-execute-v1` | Runs your uploaded program once and keeps its exit status and output. | The manifest digest, given twice. |
| `native-runtime-v1` | Runs your uploaded program 2 times to warm up and 9 times to sample, each as a fresh process pinned to the job CPU, and keeps wall time, CPU time, peak memory and fault/switch counts per run. | The manifest digest, given twice. |
| `validate-buster-v1` | Builds two installed Buster revisions and runs the existing paired throughput check. | Two installed 40-hex revisions. |
| `zen5-calibration-v1` | The fixed host calibration recipe. | One installed revision, given twice. |

`bench_capabilities` (MCP) or `gateway capabilities` (shell) lists the recipes
an installation actually serves. Trust that over this table.

## What a program must be

The upload is checked when it finishes and again before it runs.

- A 64-bit little-endian x86-64 ELF of type `ET_EXEC`.
- Fully static: no interpreter and no dynamic section.
- No segment that is both writable and executable, and a non-executable
  stack.
- Between 64 bytes and 4 MiB.
- It receives no arguments, a fixed minimal environment (`PATH`, `LC_ALL=C`)
  and `/dev/null` on standard input when sampled.
- Exit status 0 is success. Anything else, a signal or a timeout fails the
  job; for runtime sampling one bad run fails the whole job.
- Standard output and error are captured, up to 1 MiB.
- Under `native-runtime-v1` each run is limited to 10 seconds, may not leave
  child processes behind, and cannot move itself to another process group.

Two ways to produce one. An assembly microkernel with no libc:

```sh
clang -nostdlib -static -Wl,-z,noexecstack -Wl,--build-id=none -o kernel kernel.S
```

A C program against a static libc:

```sh
clang -O2 -static -o program program.c
```

Compile wherever you like; the service never compiles your source. Check the
result with `readelf -hl`: the type must be `EXEC`, and there must be no
`INTERP` or `DYNAMIC` line.

Put the work you want to measure inside the program and make it long enough
to matter. `native-runtime-v1` measures a whole process from launch to exit,
so a program that runs for microseconds mostly measures process creation.

## From a shell on the host

The public socket only accepts the service account, so commands run through
`sudo -u buster-bench`. `gateway` uses the installed socket path.

```sh
G="sudo -n -u buster-bench -- /usr/local/libexec/buster-bench-service gateway"

$G capabilities
```

Upload. The file must be readable by the service account. The reply carries
the manifest digest:

```sh
$G upload-program /path/to/program
# program-manifest-sha256=<64 hex> program-bytes=<n>
```

Submit. The first argument is an *idempotency key* that you choose: letters,
digits, `.`, `_` and `-`, at most 64 characters.

```sh
$G submit-program  my-experiment-1 <manifest digest>     # native-execute-v1
$G submit-runtime  my-experiment-1r <manifest digest>    # native-runtime-v1
# job=<n> token=0 … phase=queued …
```

Read the outcome once the job has finished:

```sh
$G result <job>
# job=<n> token=<t> … phase=finished outcome=succeeded …
# manifest-sha256=…  bundle-sha256=…  full-result-sha256=…
$G logs <job>          # lifecycle events, not program output
```

Fetch the sealed archive and unpack it. The export prints the receipt digest
on standard error; the unpacker refuses an archive that does not match it:

```sh
$G export <job> <token> <full-result-sha256> > export.bin 2> export.err
/usr/local/libexec/buster-bench-service unpack-export export.bin "$PWD/out" \
    "$(sed -n 's/^export-receipt-sha256=//p' export.err)"
ls out
# native-execute-v1.manifest  native-execute-v1.bundle  native-stage.log
```

`native-stage.log` is your program's output. A runtime job also contains
`runtime-samples.txt`.

## From an MCP client

Start the adapter as a local stdio MCP server. From another machine that is
an SSH command; see
[deploy/SAME_HOST_MCP.md](deploy/SAME_HOST_MCP.md#admitting-a-personal-client)
for the account rule and for ChatGPT's tunnel:

```sh
ssh <operator>@<host> sudo -n -u buster-bench -- \
    /usr/local/libexec/buster-bench-service mcp /run/buster-bench/control.sock
```

The tools, in the order a job uses them:

| Step | Tool | Arguments |
| --- | --- | --- |
| See what is installed | `bench_capabilities` | none |
| Start or resume an upload | `bench_program_begin` | `program_sha256`, `program_size` |
| Send bytes | `bench_program_write` | the same two, plus `offset` and `bytes_hex` (1 to 432 bytes as lowercase hex) |
| Commit | `bench_program_finish` | `program_sha256`, `program_size`; returns `program_manifest_sha256` |
| Queue a job | `bench_submit` | `idempotency_key`, `recipe`, and the manifest digest as both `baseline_sha` and `candidate_sha` |
| Check | `bench_status`, `bench_logs` | `job_id` |
| Read the outcome | `bench_result` | `job_id`; returns `attempt_token` and the three digests |
| Fetch the receipt | `bench_artifact_receipt` | `job_id`, `attempt_token`, `full_result_sha256` |
| Fetch the archive | `bench_artifact_read` | the same three, plus `receipt_sha256` and `offset`; repeat from `next_offset` until `eof` |
| Cancel | `bench_cancel` | `job_id` |

Numbers that can exceed 2^53 (`job_id`, `attempt_token`, sizes, offsets) are
decimal **strings**.

`program_sha256` is the SHA-256 of the program file and `program_size` its
length in bytes. `bench_program_begin` returns a `cursor`: the number of bytes
the service already holds. Write from there. If a reply is lost, send the
same write again; an identical retry is accepted, and different bytes at an
offset already written are refused.

A read returns at most 3,072 archive bytes. A slice is not evidence by
itself. Concatenate every slice, compute SHA-256, and compare it with
`archive_sha256` from the receipt. The receipt bytes followed by the archive
bytes are exactly the file `unpack-export` takes.

Closing the MCP session, or cancelling an MCP request, never cancels a job.
Only `bench_cancel` does, and `cancel_requested` means the request was
recorded, not that execution has stopped.

## Reading a result

A job has three separate answers. Do not merge them.

- **Phase** says where it is: `queued`, `reserved`, `preparing`, `settling`,
  `measuring`, `finalizing`, `cleaning`, `finished`.
- **Execution outcome** says what happened: `succeeded`, `failed`,
  `cancelled` or `interrupted`.
- **Measurement validity** and **statistical decision** are `not-evaluated`
  for the native recipes. A successful run is not a performance verdict.

The recipe manifest in the archive states the details: `stage-exit-status`,
whether the log was truncated, and for a runtime job the summary lines.

### Runtime samples

`runtime-samples.txt` begins with a header naming the program, the CPU and
the fixed protocol, then one row per run:

```text
row=<warmup|sample>,<index>,<wall ns>,<user ns>,<system ns>,<peak RSS bytes>,
    <minor faults>,<major faults>,<voluntary switches>,<involuntary switches>,
    <diagnostics mask>,<exit status + 1>,<signal>,<timed out>,<launch error>,<launch stage>
```

Every row is kept, including the warmups. The manifest repeats the median,
minimum and maximum wall time, the median CPU time and the largest peak RSS
of the nine samples, and binds the file by `sample-records-sha256`.

What these numbers are, and are not:

- They time a whole process, from launch until it has been reaped.
- The only correctness check is exit status 0. The output is recorded but not
  compared with anything.
- There is one subject. Nothing compares two programs or decides that one is
  faster.
- The manifest says `qualification=diagnostic-only`. Treat them as a careful
  observation on one host, not as a qualified benchmark.

## While a job is running

The deployed service is synchronous: while it runs a job it serves nothing
else.

- A request sent during a job fails as `busy` at once, or as `io-uncertain`
  after 30 seconds if it reached the service's small connection backlog.
- Neither tells you what the job is doing. Wait and ask again.
- **Retry a submission with the same idempotency key.** If the first attempt
  was in fact received, you get the same job back; a retry never creates a
  second job. The same key with different inputs is refused as a conflict.
  Do not invent a new key because a reply was lost.
- An interrupted upload resumes from its cursor.

Do not poll in a tight loop. A validation job takes minutes; a native job
takes about as long as the program runs, plus a second or two.

## Limits worth knowing

| Limit | Value |
| --- | --- |
| Program size | 64 bytes to 4 MiB |
| Program store | 128 entries for the lifetime of the store, counting unfinished uploads; an operator removes entries by hand |
| Queued jobs | 8 |
| Jobs per journal | 512 over its lifetime |
| Captured output | 1 MiB |
| Runtime protocol | 2 warmups, 9 samples, 10 seconds each, fixed |
| Archive read through MCP | 3,072 bytes per call |
| Client wait | 30 seconds |

## What is not there

- The service does not compile anything you upload. C or assembly must be
  built before upload.
- No program arguments, input files or dynamic libraries.
- No check of program output against an expected value.
- No A/B comparison of two uploaded programs, and no statistical verdict on
  runtime samples.
- No custom compiler benchmark. Compiler work is limited to the fixed
  `validate-buster-v1` and calibration recipes.
- No cached status during a job. That needs the optional
  [off-host split](deploy/OFFHOST_CONTROL.md).

These are tracked on #2648 and #437.

## Where the details live

| Topic | Document |
| --- | --- |
| Upload protocol, store layout, containment of a native job | [NATIVE_EXECUTION.md](NATIVE_EXECUTION.md) |
| Runtime sampling protocol and its limits | [NATIVE_RUNTIME.md](NATIVE_RUNTIME.md) |
| Sealed export format and offline verification | [EXPORT.md](EXPORT.md) |
| MCP adapter behaviour and client configuration | [README.md](README.md#native-stdio-mcp-client), [deploy/MCP_CLIENT.md](deploy/MCP_CLIENT.md) |
| Installing, upgrading and recovering a host | [deploy/SAME_HOST_MCP.md](deploy/SAME_HOST_MCP.md) |
| Queue, journal, worker and broker contracts | [README.md](README.md), [deploy/SYSTEMD_BROKER.md](deploy/SYSTEMD_BROKER.md) |
| The intended, larger custom-benchmark contract | [CUSTOM_BENCHMARKS.md](CUSTOM_BENCHMARKS.md) |
