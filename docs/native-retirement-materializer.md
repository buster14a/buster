# Offline dependency materializer

This is a prerequisite for #36 and #508, extracted from the existing #645
implementation. It does not select a production compiler backend, execute a
census, approve a support manifest, or establish retirement acceptance.

## Provenance and entry points

`tools/native_retirement_materializer.py` is byte-identical to Git blob
`33745cb7c3aabd565efbd2ec9e8cb9178629d360`, preserved by commit
`537e334d1860cf156ad9b8fc01f6590b3ef79918` on
`work/36-recovered-integration-20260915`. That recovery ref and #645's landing
branch remain owned by their existing workstreams.

- `parse_manifest` validates the descriptor and canonical dependency namespace.
- `materialize_file` binds the descriptor bytes and descriptor-relative root.
- `materialize` verifies local source bytes and publishes a dependency tree.
- `_archived_replay` validates an optional historical row projection;
  `_verify_archived_fixture_inputs` enforces its historical support-ledger pin.
- `main` exposes the `materialize` and `self-test` commands.

No external Python dependencies are required. Filesystem execution uses POSIX
directory descriptors and is tested on hosted Linux and macOS; normalizing a
Windows path spelling is not a claim of native Windows execution support.

## Usage

From the repository root:

```sh
python3 tools/native_retirement_materializer.py self-test
python3 tools/native_retirement_materializer_test.py -v
python3 tools/native_retirement_materializer.py materialize \
    --manifest /absolute/controlled-repo/docs/dependencies.json \
    --source-root /absolute/controlled-repo \
    --output /absolute/private-workspace/new-dependencies
```

The descriptor uses schema `buster-native-retirement-dependencies-v1`, version
1, and explicit local source/destination, size and SHA-256 records. The module
header gives its shape. This extraction does not replace the live descriptor
in `docs/native-retirement-dependencies-v1.json` or promise that every historical
or current descriptor is compatible with this standalone boundary.

Operate in an exclusively owned source/output workspace with no concurrent
publisher. The output must be absent. Authenticated copies are staged in a
sibling temporary directory and renamed only after validation. Source identity,
link, namespace, receipt, and cleanup checks are retained. This is not a
hostile-concurrency sandbox or a crash-durability protocol: no fsync guarantee
is made, and SIGKILL can leave an unpublished temporary sibling.

External checkout declarations are recorded and their source bytes are
hash-checked; they are not network fetch instructions or proof of Git remote
provenance. The separate external-checkout verifier and census integration
remain #645/#508 work.

## Regression and acceptance boundaries

The reusable donor tests are retained. Their dependency on the donor's live
28-fixture descriptor is replaced by a self-contained one-fixture, 144-row
protocol fixture. It independently constructs row identities, exercises all
three dispositions, validates publication/receipt stability, and rejects
mutated counts, digests, axes, mapped headers and input bytes. These are protocol
rows, not 144 compiler executions.

Only the synthetic positive fixture temporarily substitutes its own support
hash inside the test process. The shipped historical pin remains
`24ae23cff2ab5bd6ff46304a878ed4dc3e5d639422751de93ea74dabde7e437e`;
a separate unpatched control proves that another ledger is refused. No current
support contract is rewritten to pass this test. Full historical corpus replay
and current-candidate closure remain separate acceptance requirements.

The `Offline dependency materializer` workflow runs the command and regression
suite on standard GitHub-hosted Linux/macOS runners, retaining exact checkout
and source-blob identities. For an initial PR base lacking the command, it
records the actual missing-command exit; later bases containing the command run
its self-test instead. This is a newly available tool, not a compiler defect
reproduction. Normal repository CI and independent review still apply. No MIR,
compiler self-host, dedicated-host benchmark, or final retirement result follows
from these Python tests.
