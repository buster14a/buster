# Native MCP client deployment boundary

The native stdio adapter translates MCP tools into the authenticated local
control protocol. The queue daemon remains the single journal writer and
owns submission, cancellation and recipe admission. Build the adapter through
the existing driver, then configure clients to start the resulting executable
directly so stdout contains only MCP messages:

```sh
./build.sh bench_service capabilities
build/bench-service-tools/service mcp /run/buster-bench/control.sock
```

The socket path is an operator startup argument. Tool calls cannot select a
socket, host, principal, executable, shell command or filesystem path. The
adapter process needs the daemon's already approved peer UID/GID; starting it
does not grant that identity or change socket permissions. The currently
deployed public protocol uses one fixed `github-actions` principal. A personal
client must be explicitly admitted into that trust boundary; this adapter does
not introduce per-user authentication or change the runner's pinned sudo policy.

The adapter submits directly to the daemon; it does not dispatch a GitHub
workflow. Direct owner-authenticated MCP/CLI submission is the intended normal
path. The existing protected CI gateway can remain another client of the same
queue. The broader native experiment capability likewise requires no PR or
branch for each private program bundle.

The native upload tools use `program_sha256` and a decimal `program_size`.
`bench_program_begin` starts or resumes the upload; its `cursor` is the durable
byte count. `bench_program_write` takes an exact decimal `offset` and 1--432
bytes encoded as lowercase `bytes_hex`. An identical retry is safe; changed
prefixes and gaps are refused. `bench_program_finish` verifies all bytes and
the static ELF contract before returning `committed=true`. A manifest digest
returned before that finish acknowledgement is only the declared identity,
not proof of a committed bundle.

Submit the finished `program_manifest_sha256` using `bench_submit`, recipe
`native-execute-v1`, and the same digest in both `baseline_sha` and
`candidate_sha`. Those fields identify one program bundle for this recipe;
no compiler revisions are needed. `bench_capabilities` reports native upload
and execution only when the backend lists that admitted recipe. A successful
one-shot execution has no compiler or runtime performance series. See the
[execution contract](../NATIVE_EXECUTION.md) for supported binaries and limits.

Submitting the same digest with recipe `native-runtime-v1` runs two warmups
and nine fresh-process samples of that program instead. Its sealed rows are a
diagnostic process-latency measurement, not a qualified comparison; see the
[runtime contract](../NATIVE_RUNTIME.md). `custom_runtime_benchmarks` is true
only when the backend serves that recipe.

The installed daemon is synchronous: during an admitted job it stops serving
ordinary control requests until cleanup. An MCP call can therefore return a
transport error without cancelling or resubmitting the job. Retrying submission
uses the original idempotency key and exact immutable request. An MCP connection
closing or a request-cancellation notification does not release queue admission.
Only the explicit `bench_cancel` tool requests job cancellation; its receipt
distinguishes that request from a confirmed terminal outcome.

## Off-host control plane

The accepted #437 deployment is same-host; follow
[SAME_HOST_MCP.md](SAME_HOST_MCP.md) for it. The remainder of this section
describes the optional off-host split.

The off-host topology keeps the queue, adapters, cached logs/results
and any tunnel client on a separate control machine. The worker initiates the
fixed SSH transport; client polling must create no worker activity during
measurement. The current local control socket cannot supply that topology by
itself. Running the adapter or an SSH-per-call proxy on the 9700X is a functional
client smoke test, not qualified off-host service acceptance.

Before enabling external clients, install the durable off-host control/worker
protocol as described in [OFFHOST_CONTROL.md](OFFHOST_CONTROL.md) and document its principal mapping, reconnect/reconciliation policy,
cache freshness and exceptional cancellation path. Run the 100-submit /
1,000-read quiet-phase fixture and retain the worker traffic trace. Do not move
the queue files between hosts or run a second writer against shared storage.
Keep the existing host singleton, inherited lease, containment, installed
recipe identities and result sealing contract intact.

## Private ChatGPT connection

OpenAI's [Secure MCP Tunnel guide](https://developers.openai.com/api/docs/guides/secure-mcp-tunnels)
supports an operator-installed tunnel client forwarding to a stdio command.
It is an external deployment prerequisite, not a Buster library dependency.
Configure it on the control machine with the reviewed installed service binary
and fixed socket path. Follow the current guide and the installed client's
help for its exact command-line syntax.

A tunnel ID, runtime API credential and appropriate Platform tunnel roles are
required. The target ChatGPT workspace must also permit developer mode and be
associated with the tunnel. Those account facts must be verified in the actual
workspace; a documentation example or successful local tool enumeration does
not establish them. See the official [connection and testing guide](https://developers.openai.com/plugins/deploy/connect-chatgpt).

A shared stdio tunnel can issue more than one valid `initialize` request to
the same adapter process, including a setup probe followed by the actual
client handshake. The adapter acknowledges each request and preserves an
already ready transport. Invalid parameters still fail; initialization does
not submit, cancel, reset or authenticate a job.

For tunnel-client 0.0.14, enable its initialized-notification compatibility
guard when connecting ChatGPT's legacy client path:

```sh
tunnel-client run --profile buster-bench \
    --mcp.stdio-send-initialized-notification
```

This makes the tunnel send `notifications/initialized` after a successful
initialization, and suppresses a later duplicate from the caller. The adapter
still requires that notification before its first tool call. Use a service
revision containing the shared-stdio fix; the notification guard alone cannot
fix an older adapter that rejects a second initialization. Check the installed
client's help for newer releases. See the official client's
[protocol documentation](https://github.com/openai/tunnel-client/blob/v0.0.14/docs/protocol.md)
for the opt-in guard.

Keep credentials in operator-managed deployment storage. Do not put them in
requests, source control, compiler workspaces, job artifacts or diagnostic logs.

## Acceptance receipts

Record the installed service revision and binary hashes, control/worker host
identities, transport configuration, tool schemas and account/workspace access.
From ChatGPT, invoke `bench_submit`, retain its durable job ID, disconnect and
retrieve that same job's status/result later. Repeat from Codex. Validate the
bound evidence through the existing authenticated exporter and independent
unpacker. Enumeration, queue fixtures and a local adapter smoke test remain
separate from those actual client-to-host receipts.

Complete the real compare recipe and host qualification gates separately.
`measurement_validity=not_evaluated` and a successful execution do not establish
a compiler speedup or an equivalence decision. The historical retirement recipe
being superseded does not make the independent #437 service goals complete.
