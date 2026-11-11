# Hosted toolchain delivery

`tools/ci/hosted-tools.lock` is the authority for the official release inputs
(LLVM and its license, CMake, Ninja, mold and wild) and for their GHCR mirror.
This change does not upgrade them. `prepare-hosted-tools` restores the exact
manifest-keyed Actions cache, checks every artifact's SHA-256, and acquires only
absent or damaged inputs, in this order:

**exact verified Actions cache → digest-pinned GHCR mirror → original upstream URL.**

The test/fuzz matrix (`9700x.yml`) and both self-host bootstrap rows
(`selfhost.yml`) depend on successful preparation; they never independently fall
back to a remote source. `build.yml`'s single job is its workflow's only
consumer, so the `hosted-tools` action, given an empty `artifact-name`, runs the
same preparation inside that job. `build-pgo.yml` and the 9700X need only CMake
and Ninja: `setup-build-tools.sh` without `BUSTER_HOSTED_RELEASES` acquires each
from the mirror, then upstream, with the same checks.

Preparation always publishes a run-scoped artifact, then optionally saves the
reusable cache. Only a push or manual dispatch on main seeds that cache. Cache
read/write service errors are nonfatal only because the verified artifact is
available; an artifact publication failure fails preparation. Consumers try the
cache, verify it, and fall back to that run's artifact. The artifact name is a
job output from the successful preparation attempt, not a name reconstructed
from the consumer's attempt number: rerunning a failed shard can still read the
original preparation. No credentials or installed executables are cached.

The cache key includes the exact lock-file identity and the platform/schema,
not the Buster revision, shard, sanitizer or optimization mode. Original input
archives are cached; recipe changes do not make those bytes stale. Restored
bytes are verified even on an exact key hit. There are no broad restore keys,
and a version string or `.complete` file never authenticates an input. PRs can
read eligible main caches but cannot publish to the trusted default-branch
cache through this workflow. GitHub's branch and fork access restrictions still
apply; this is not a repository-wide cross-PR filesystem.

## Failure and recovery

A cold workflow has one acquisition sequence per missing input (mirror, then
upstream, each with bounded retries); concurrent cold workflows can each acquire their own inputs. No
repository-wide concurrency group cancels unrelated required runs. A curl
attempt has connection, transfer and stalled-transfer limits, and an outer
300-second deadline with a five-second kill grace bounds the entire sequence.
The preparation job also has a 20-minute overall bound. HTTP/transport failures
and integrity failures identify their component; bytes/time reported by curl
are for its final attempt, not a fabricated total across retries.

A corrupted cache entry is rejected and reacquired by preparation. Actions
caches are immutable, so an already occupied corrupt key cannot be repaired by
saving over it. Delete that exact cache entry through GitHub's cache management
and rerun preparation; the run-scoped handoff remains usable meanwhile. A cache
write denial or eviction does not invalidate a published artifact. If the
artifact has expired and no valid cache remains, rerun the preparation job (or
the workflow), not just a consumer. Never remove hash checks to recover a run.

`setup-hosted.sh` installs system packages and applies the existing sysctl
policy. `install-hosted-tools.sh` only extracts verified releases into a private
staging tree, checks versions/runtimes and publishes the completed tree. Relative
linker links survive relocation. A failed extraction publishes no installation.
The selected compiler, resource directory, runtime/library path and tools remain
explicit; no runner-provided Clang is silently substituted.

Of the LLVM release the tree holds the compilers, lld, the object and DWARF
tools, the headers, clang's resource directory, the bundled `libclang-cpp` (the
pragma oracle's) and the static archives of LLVM, Polly and clang. Clang's 93
archives, 238 MB of the tree's 2.0 GB, are the clang server's (#2071): with
LLVM's they link it with one copy of LLVM, which `libclang-cpp`, bundling LLVM
without the targets' initializers, can't give it. They add no measurable
extraction time, which decompressing the whole 12 GB stream dominates.

For standalone hosted-environment preparation, first prepare a private input
directory with `hosted-release.sh prepare DIRECTORY` and set
`BUSTER_HOSTED_RELEASES` to it. A set-but-empty or missing input directory is an
error. The standalone/self-hosted `setup-build-tools.sh` path acquires its
inputs (mirror, then upstream) when that variable is unset. The 9700X's private cache and
admission/lock policy are unchanged.

## GHCR mirror (#1768)

`ghcr.io/buster14a/buster-hosted-inputs` holds the **original, unmodified
release files** as OCI blobs, one manifest layer per lock component (titled by
its name), with an empty `{}` config. A blob's OCI digest is the SHA-256 the lock
already pins, so the mirror adds no second identity for the payload, and the
upstream fallback reconstructs exactly the same bytes and the same installed
tree. The lock's `mirror` record pins the reviewed **manifest digest**:

```text
mirror ghcr.io/buster14a/buster-hosted-inputs sha256:<manifest digest>
```

`unpublished` in its place disables the mirror (preparation logs
`source=mirror unavailable reason=unpublished`). Consumers read anonymously, so
no secret reaches any preparation job, including fork PRs. They fetch the
manifest by digest, never by tag, check its SHA-256 against the pin and that it
lists the component, then fetch the blob by the lock digest and verify it before
use. A manifest or blob that does not match, or a pinned manifest that omits a
component, is an **integrity failure that fails preparation**; it is not hidden
as a fallback. An unpublished pin, a denied read (`reason=denied`), a missing
object (`reason=missing`) or a transport failure (`reason=transport`) falls back
to upstream with that reason in the log. Signed storage redirects and tokens are
never logged. `BUSTER_HOSTED_MIRROR=off` disables the mirror for an outage drill.

The blobs are the original archives, not a reduced bundle. That keeps one trust
anchor (the lock's SHA-256), byte-identical fallback and the upstream license
files as shipped. Only the preparation job (or a single-job workflow) reads the
mirror; shards read the run artifact, so a reduced bundle would mainly shorten
that one transfer. Reduce only after measuring that transfer's cost.

### Publication and promotion

`publish-hosted-inputs.yml` is the only writer. It runs on pushes to `main` that
change the lock, `hosted-release.sh` or itself, and on dispatch, which publishes
nothing unless the ref is `main`. It has no `pull_request` trigger, so PR code
never holds `packages: write`. It runs the fixtures, acquires the inputs from
the cache or upstream with the mirror disabled, uploads only absent blobs, puts
the manifest under the tag `inputs-<hash of the component records>`, reads it
back by digest, and writes the `mirror` line to its job summary. It then tries
an anonymous read and reports whether the package is public. The manifest
depends only on the inputs, so republishing after a pin change is a no-op with
the same digest.

1. A reviewed PR changes the component pins (or `hosted-release.sh`) and merges.
2. The publisher runs on `main`. On the package's first publication, the owner
   sets its visibility to **public** under the organization's packages (a new
   GHCR package may start private; consumers then report `reason=denied`).
3. A reviewed PR replaces the lock's `mirror` line with the summary's pin; its
   hosted CI is the qualification of that pin.

A moved tag changes nothing: consumers use only the pinned digest. To roll back,
restore an earlier reviewed `mirror` line (its manifest stays in the package).

### Retention and limits

Do not delete a package version whose manifest digest is pinned by `main`, a
supported branch, the nightly backlog or a rollback pin; the tag names the input
set it holds. Public GHCR packages have no storage charge; check the
organization's current packages billing before making it private. Pinning the
mirror changes the lock and so the Actions cache key once; that run fills the
cache from the mirror. GHCR, Actions caches/artifacts and GitHub release
downloads are all GitHub services: the mirror is a separate, repository-retained
service path, not an independent failure domain. A platform-wide GitHub outage
still fails setup, and apt, corpus and checkout traffic remain network
dependencies.

## Validation and limits

`bash tests/ci/hosted-tools.sh` runs network-free cold/warm, corrupt/missing,
wrong-schema, changed-response, symlink, handoff, relocation and incomplete
extraction fixtures. A fake registry checks publication (and that republishing
uploads nothing and keeps the digest), pinned consumption with upstream absent,
tampered blobs and manifests, a manifest without a component, missing, denied,
unreachable and disabled mirrors, no source at all, the standalone path, and that
no credential reaches a command line. Tiny fake tools qualify installer mechanics only. Real
hosted runs must still pass the actual compiler, Clang API, sanitizer and fuzzer
probes; setup success is not compiler correctness qualification.

`hosted-inputs-drill.yml` rehearses the outages on real hosted runners with no
Actions cache, on PRs that change the acquisition path, weekly and on dispatch.
It takes a source down by pointing its hosts at the loopback address in
`/etc/hosts`. With upstream down, every input must come from the pinned mirror
and nothing from upstream. With the mirror down, the log must report
`reason=transport` and every input must come from upstream. Both successful
rows install, compile and link with lld and mold, and inventory the tree (type,
mode, path, link target and SHA-256 of every entry); a final job requires the two
inventories to be identical. With both down, preparation must fail, publish
nothing, and do so within 420 s: one mirror attempt plus one bounded upstream
sequence for the first input. The weekly run also catches a pinned publication
that has gone missing.

The workflow preserves all existing hosted rows, fuzzing and aggregate failure
semantics. Cache hits remove upstream **release** downloads, not apt, corpus,
checkout or GitHub-service network access. What the change bought is upstream
traffic and reliability, not per-shard latency: a warm shard spends about as
long restoring 1.2 GB from the cache as it once spent downloading it, and the
preparation job adds a short serial step. The measured cold/warm costs, transfer
counts and cache size are in `buster-performance`
(`docs/measurements/hosted-toolchain-2026-11-08/`); raw evidence belongs there.
