# Immutable native program execution (#2648)

`native-execute-v1` executes one privately uploaded static Linux x86-64 ELF
program through the existing contained single-job worker. The uploaded file may
be a user-authored assembly microkernel or a prebuilt program. It is immutable
input to a fixed recipe; it cannot supply a host command, environment, path,
systemd property or timeout. This first slice has no arguments, archive inputs,
dynamic dependencies, compilation, timing samples or benchmark verdict. Those
capabilities remain unavailable. Exit zero is execution success, not a
correctness oracle for the user's program.

This source change does not install or qualify a host. An installation must
upgrade the service, broker and static credential gate together, retain the
existing approved identities/sandbox/lease policy, and install the exact
`BQ_NATIVE_PROFILE` bytes from `native_profile.h` as
`/opt/buster-bench/installed/recipes/native-execute-v1.recipe` with the same
root-owned read-only policy as the existing recipes. The source tests do not
prove that benchpress has this installation or that its quiet window is valid.

## Private upload and submission

As the service's authenticated control identity, use the fixed local Unix
endpoint directly. No Git branch, pull request or workflow dispatch is needed:

```text
/usr/local/libexec/buster-bench-service gateway upload-program ./my-static-program
/usr/local/libexec/buster-bench-service gateway submit-program my-job-key MANIFEST_SHA256
/usr/local/libexec/buster-bench-service gateway status JOB_ID
/usr/local/libexec/buster-bench-service gateway result JOB_ID
/usr/local/libexec/buster-bench-service gateway export JOB_ID ATTEMPT_TOKEN FULL_RESULT_SHA256
```

For a configured alternative local endpoint, the corresponding commands are
`client SOCKET upload-program FILE` and `client SOCKET submit-program KEY SHA`.
The local filename never crosses the control socket. Upload prints the
canonical program-manifest SHA256; submission binds that identity in both
existing source-identity fields. Reusing a submission key with the same request
is idempotent, while different request bytes conflict. An off-host coordinator
must transfer and validate these immutable bytes before native materialization;
the off-host control implementation owns that transfer. The MCP adapter also
offers bounded `bench_program_begin`, `bench_program_write` and
`bench_program_finish` tools; [MCP client instructions](deploy/MCP_CLIENT.md)
describe their fields and direct submission. Neither interface establishes
that the installed backend has been upgraded or qualified.

The authenticated schema-2 protocol adds operations 14 (begin), 15 (write) and
16 (finish). Begin/finish bodies contain 64 lowercase ASCII program-SHA256 bytes
and an eight-byte little-endian file size. A write adds an eight-byte offset and
1–432 data bytes, keeping every request at the existing 512-byte body bound.
A successful 76-byte reply contains error u32, durable byte cursor u64 and
64 manifest-SHA256 bytes. Replies bind schema, operation, correlation, identity
and cursor before the client uses them. Only the existing UID/GID-authenticated
principal can upload. A service processes uploads only while its synchronous
job worker permits normal control traffic; control during a running job remains
unavailable in this local transport.

## Store, materialization and execution

The service owns `queue/native-blobs` at mode 0700. Program size is limited to
4 MiB, with at most 128 lifetime store entries (completed bundles and partial
uploads count). The worst-case admitted program bytes are bounded by 512 MiB;
an in-flight finish can temporarily duplicate one file. There is no automatic
retention deletion. At capacity the service refuses new uploads; operator
maintenance must preserve all inputs still referenced by retained jobs.

A partial file is named by the program digest, is mode 0600, and starts with
its eight-byte expected size. Writes append only at the durable cursor or retry
an identical complete prefix. Gaps, changed prefixes and changed sizes conflict.
Finish rehashes the complete file and validates the bounded ELF headers:
`ET_EXEC`, `EM_X86_64`, little-endian 64-bit ELF, at least one executable load
segment containing the entry point, no `PT_INTERP`, no `PT_DYNAMIC`, no segment
with both write and execute flags, and no out-of-file/overflowing segment range.
This is admission validation, not a proof of program safety or correctness.

The canonical manifest is exactly:

```text
BQ-NATIVE-V1
operation=execute-once
platform=linux-x86-64-static-elf
arguments=none
program-sha256=PROGRAM_SHA256
program-size=DECIMAL_BYTES
```

Its SHA256 names a mode-0500 directory containing `manifest` and `program`, both
0400. Finish syncs the complete files and directory, then publishes that one
directory with no-replace rename and syncs the store. An interrupted finish can
retry only exact service-owned partial prefixes of the two declared files.
Extra, foreign, linked, changed or invalid-mode evidence is preserved and
refused. A retry after publication validates both committed files; it removes
only a matching complete orphan partial file. Existing immutable bundles are
never replaced.

The normal worker acquires the host lease before reservation/materialization.
The materializer checks the installed recipe, verifies the manifest SHA256 and
program SHA256/ELF again, and copies independent base/candidate input trees.
Copied manifests are 0440 and programs 0550; source directories are 0550. The
journal still uses the five historical request fields and the same request
hash domain. New records use journal schema 4. Schemas 1–3 still replay unchanged;
worker transition semantics remain at schema 3. A pre-schema-4 reader refuses
new journal records, so downgrading a writer requires an explicit migration.

The root broker's existing version-2 selector 3 admits only this recipe and
stage 29, `native-execute`. It constructs the fixed service outer and fixed
`native-payload` helper itself. The outer runs as `buster-bench` and captures
bounded stage logs into the private result directory. The static entry gate
verifies the candidate numeric credentials, exact group set, no-new-privileges
and empty capability sets before the helper starts. The helper additionally
refuses any identity other than `buster-bench-candidate`, verifies the copied
manifest/program descriptors and hashes, and uses `fexecve` with fixed argv and
environment. Uploaded code never executes as the service or outer account.

The candidate stage retains the existing resource limits, private network,
no-new-privileges, strict filesystem policy and syscall restrictions. Its only
job write surface is the fresh candidate-group scratch directory. The attempt
inputs and installed policy are read-only; queue, lease, result directory and
both control socket directories are inaccessible. The parent/child unit
relationship and cleanup logic include the new fixed stage. No archive
extraction, dynamic dependency loader or caller-selected shell is introduced.

The trusted outer captures at most 1 MiB of combined stage output and explicitly
records truncation while continuing to drain output. It seals the recipe
manifest and existing `BQ-BUNDLE-V1` index. The manifest binds job/attempt,
program manifest identity, fixed operation and stage exit status. That status
is the fixed stage command result; it is not an exact payload signal claim.
Worker outcome, cleanup, result binding, replay and authenticated export use the
existing durable paths. Compilation and benchmark metrics stay `unavailable`.

## Validation boundaries

Registered normal and sanitizer service tests cover real files, authenticated
packet dispatch, prefix retries/restart, all five publication interruption
points, malformed/dynamic/RWX/range ELF rejection, journal compatibility,
materialization, a disposable known-transcript static executable through the
same descriptor verifier used by the candidate helper, sealed result binding,
export and replay, and the normal worker state machine. Broker/gate tests cover
fixed native selector/stage construction, candidate account and write surface,
forbidden selectors/identities/runtime properties, and exact cleanup names.

The disposable execution fixture uses the local test identity and
no-new-privileges. It does not substitute for an installed systemd/candidate
account witness. On-host admission still requires the existing approved service
installation and containment/cleanup evidence, followed by a bounded native
fixture proving its transcript, final outcome and export under that installation.
