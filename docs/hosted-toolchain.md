# Hosted toolchain delivery

`tools/ci/hosted-tools.lock` is the authority for the official release inputs
(LLVM and its license, CMake, Ninja, mold and wild) and for their GHCR mirror.
This change does not upgrade them. `prepare-hosted-tools` restores the exact
manifest-keyed Actions cache, checks every artifact's SHA-256, and acquires only
absent or damaged inputs, in this order:

**exact verified Actions cache → digest-pinned GHCR mirror → original upstream URL.**

The test/fuzz matrix (`9700x.yml`, whose rows' jobs, a portable row's compiler
build among them, are `hosted-correctness.yml`'s) and both self-host bootstrap
rows (`selfhost.yml`) depend on successful preparation; they never independently
fall back to a remote source. `build.yml`'s single job is its workflow's only
consumer, so the `hosted-tools` action, given an empty `artifact-name`, runs the
same preparation inside that job. `build-pgo.yml` and the 9700X need only CMake
and Ninja: `setup-build-tools.sh` without `BUSTER_HOSTED_RELEASES` acquires each
from the mirror, then upstream, with the same checks.

Preparation verifies the restored cache as the consumers will. Only when it
doesn't verify (a miss or a damaged entry) does preparation publish its
acquired inputs as a run-scoped artifact; on a verified hit every consumer
restores the same immutable key, and the artifact would go unread. Every run
used to publish it: 1.27 GB, uncompressed, kept 7 days, about 400 GB in the
five hours of 2026-11-11's busiest wave (#2372). It then optionally saves the
reusable cache. Only a push or manual dispatch on main seeds that cache. An
artifact publication failure fails preparation. Consumers try the cache,
verify it, and fall back to that run's artifact. On a cache-hit run there is
none, so a consumer whose cache read fails fails its setup step, which the
setup re-run (`rerun.yml`, #2276) retries once; an outage of the cache service
long enough to outlast that also reaches the artifact service. The artifact
name is a job output from the successful preparation attempt, not a name
reconstructed from the consumer's attempt number: rerunning a failed shard can
still read the original preparation. The run's artifacts are kept 1 day.
No credentials or installed executables are cached.

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

`setup-hosted.sh` installs system packages from the run's offline inputs, with
no download ([Offline inputs](#offline-inputs-2275)), and applies the existing
sysctl policy. `install-hosted-tools.sh` only extracts verified releases into a private
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
extraction time, which decompressing the whole 12 GB stream dominates. The
server's build exits 77 for a toolchain without what it needs, which elsewhere
means no server; with this tree, `hosted-product.sh build` fails on it instead,
so an install that loses the archives fails the row rather than leaving it
green and slower ([CI guide](ci.md#shared-row-products)). `tests/ci/hosted-tools.sh`
runs that check against the tree it installs.

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
still fails setup. The jobs' other inputs (apt, the corpus, the mirrors) come
from the same preparation ([Offline inputs](#offline-inputs-2275)), so a
consumer's remaining network dependencies are GitHub's checkout, cache and
artifact services.

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
semantics. Cache hits remove upstream **release** downloads; the offline inputs
below remove the apt, corpus and Git fetches, leaving checkout and GitHub's
cache and artifact services. What the change bought is upstream
traffic and reliability, not per-shard latency: a warm shard spends about as
long restoring 1.2 GB from the cache as it once spent downloading it, and the
preparation job adds a short serial step. The measured cold/warm costs, transfer
counts and cache size are in `buster-performance`
(`docs/measurements/hosted-toolchain-2026-11-08/`); raw evidence belongs there.

## Offline inputs (#2275)

The hosted jobs' other external inputs are acquired once per run too, by the
same preparation job, and every job reads them with no network. Until this
change each job fetched them itself, so on 2026-11-11 a 10-minute failure to
resolve Ubuntu's mirrors failed every hosted job of four main runs in setup.
`tools/ci/hosted-offline.sh` owns the bundle:

| Input | Acquired by | Pinned by |
| --- | --- | --- |
| apt closure of `setup-hosted.sh`'s packages | `apt-get install --download-only` on the preparation's runner | the package list (in `hosted-offline.sh`) and the runner image |
| baseline runtime's 4 `.deb`s | `setup-baseline-runtime.sh`, through `cached-download.sh`, from the archive's snapshot of 2026-11-11 (`snapshot.ubuntu.com`), since the archive drops superseded updates | their SHA-256 digests |
| frozen corpus | `fetch-corpus.sh`, through `cached-download.sh` | its SHA-256 digest |
| c-testsuite and SQLite | `fetch-c-testsuite.sh`, `fetch-sqlite.sh` | their commits |
| Lua 5.4.8 | `hosted-offline.sh lua`, through `cached-download.sh` | the release archive's SHA-256 digest |

**Layout.** The bundle's `apt/` holds the downloaded `.deb`s and `MANIFEST`,
which records the runner (`ImageOS`, `ImageVersion`, release, architecture),
the package list, each `.deb`'s package, version and SHA-256 (`bundle`
records), and the version of each listed package the image already has
(`image` records). `ci/` is `BUSTER_CI_CACHE`'s layout, as on the 9700X:
`downloads/DIGEST` and bare mirrors holding `refs/pinned/COMMIT`.

**Preparation.** `prepare-hosted-tools.yml` restores two caches and runs
`hosted-offline.sh prepare`:

- `hosted-apt-v1-IMAGEOS-IMAGEVERSION-LIST`: the apt closure, which is the
  image's, since it holds what that image lacks.
- `hosted-ci-v1-SCRIPTS`: the rest, keyed by the contents of the scripts that
  pin them, including `hosted-offline.sh` for Lua. Their pins stay in those
  scripts: `nightly.sh` records `fetch-corpus.sh`'s blob as the corpus identity.

Each input is first read as a consumer reads it, offline. Only an input that
fails so is acquired, and the log names every input's source
(`input=NAME source=cache|network reason=...`).

- **Cache hit.** Every input is the cache's, and the job contacts no external
  host.
- **Cache miss.** `apt-get update` and `--download-only` are tried up to 3
  times, each bounded to 8 minutes, with pauses of 20 and 40 s. Each other
  input's script is run up to 3 times; `cached-download.sh` also retries each
  download for 240 s. Before each network attempt, an apt host the runner
  can't resolve is pinned in `/etc/hosts` to the address Cloudflare's
  DNS-over-HTTPS resolver gives it, else Google's. This is the pin
  `setup-hosted.sh` had (eb105d93), now in the only job that still reaches the
  mirrors. It brings in nothing unverified: apt checks the archive's signed
  Release files, and every download keeps its SHA-256.

Every acquired input is checked as its consumers will use it. The corpus and
the runtime are extracted and verified. The checkouts must be the pinned
commits. The closure must install from the bundle alone on the preparation's
image: an `apt-get --simulate` with no package lists. The job then publishes
the bundle as the attempt's companion artifact, `ARTIFACT-NAME-offline`, on
every run (92 MB, kept 1 day). As for the release inputs, only a push or
dispatch on `main` saves the caches.

**Consumers.** The `hosted-tools` action downloads the companion artifact. It
reads no cache: the artifact is this run's resolution, so every job of the run
installs the same versions. It checks the manifest
(`hosted-offline.sh verify`): the format, the package list, the release and
architecture, every `.deb`'s SHA-256, and no file the manifest doesn't list.
It then sets `BUSTER_HOSTED_OFFLINE`, `BUSTER_CI_CACHE` and
`BUSTER_CI_OFFLINE=1` for every later step.

- `setup-hosted.sh` runs `hosted-offline.sh apt-install`. That is
  `apt-get install --no-download` of the manifest's `.debs`, resolved against
  the installed packages alone: an empty `Dir::State::Lists`, so neither the
  image's lists nor its sources play any part, and there is no
  `apt-get update`. It then compares every recorded version with dpkg's, and
  lists them in the step summary.
- With `BUSTER_CI_OFFLINE=1`, `cached-download.sh`, `fetch-c-testsuite.sh`,
  `fetch-sqlite.sh` and `hosted-offline.sh lua DIR` read the bundle alone. A
  missing file, a hash that doesn't match or a broken mirror fails the step.
  Nothing is deleted or fetched again.
- `hosted-offline.sh lua DIR` verifies the same Lua 5.4.8 archive as the
  runtime measurement before extracting its tree, including `src/luac.c`.
  The hosted code-size qualification consumes that tree and the frozen
  SQLite amalgamation with `measure-tier1-programs --sizes-only
  --check-code-size=lua`. It validates both reports and enforces the Lua
  bound, reusing the row's existing compiler with no timing or 9700X work.
- A single-job workflow (`build.yml`, an empty `artifact-name`) is its own
  preparation. It restores the same caches and runs `prepare` itself.

**Image rollouts.** GitHub rolls a new runner image out over several days, so a
shard can draw a different image from its preparation's. It still installs the
bundle's `.deb`s, with a warning, and every `bundle` record must hold. An image
package whose version differs from its `image` record is reported, but only on
another image; on the bundle's own image it fails. Where the closure no longer
installs on the other image, the step fails, naming both images. Rerunning the
failed jobs may draw the bundle's image; a new run resolves the closure for
whatever image its preparation draws.

**Recovery.** A corrupt restored cache entry is reacquired by the preparation.
Actions caches are immutable, so delete that exact entry to stop reacquiring it
on every run. A consumer never repairs anything: rerun the workflow, or its
preparation job, not just a shard.

**Validation.** `bash tests/ci/hosted-offline.sh` takes about 10 s and needs no
network. It stands in for `apt-get`, `dpkg-query`, `sudo` and `curl`, uses real
`.deb`s and Git upstreams, and covers:

- a cold preparation through retried failures, and a warm one with every source
  gone;
- a consumer reading an artifact-shaped copy;
- a missing file, a bad hash and an unlisted `.deb`;
- a changed image, for both the preparation and a consumer;
- a changed package list, release or format;
- the repair of a damaged bundle.

The preparation job runs it before preparing. `hosted-inputs-drill.yml` adds
two real-runner jobs:

- **A shard's setup.** It takes Ubuntu's mirrors (every host apt's sources name,
  and plain HTTP), packages.microsoft.com, Lua and GitHub's release, raw and
  package hosts down before the handoff's restore, and github.com and
  codeload.github.com right after it. Setup, the baseline runtime, the corpus
  and both mirrors must still succeed, as must extracting Lua.
- **A warm preparation.** It prepares cold, keeps the bundle in a cache of its
  own, takes every source down, and requires a fresh restore to prepare with
  every input from the cache.
