# Hosted toolchain delivery

`tools/ci/hosted-tools.lock` is the authority for the official release inputs
(LLVM and its license, CMake, Ninja, mold and wild). This change does not upgrade
them. `prepare-hosted-tools` restores the exact manifest-keyed Actions cache,
checks every artifact's SHA-256, and downloads only absent or damaged inputs.
The test/fuzz matrix depends on successful preparation; it never independently
falls back to upstream.

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

A cold workflow has one acquisition sequence per missing input (with bounded
retries); concurrent cold workflows can each acquire their own inputs. No
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

For standalone hosted-environment preparation, first prepare a private input
directory with `hosted-release.sh prepare DIRECTORY` and set
`BUSTER_HOSTED_RELEASES` to it. A set-but-empty or missing input directory is an
error. The existing standalone/self-hosted `setup-build-tools.sh` path can still
acquire its inputs when that variable is unset. The 9700X's private cache and
admission/lock policy are unchanged.

## Validation and limits

`bash tests/ci/hosted-tools.sh` runs network-free cold/warm, corrupt/missing,
wrong-schema, changed-response, symlink, handoff, relocation and incomplete
extraction fixtures. Tiny fake tools qualify installer mechanics only. Real
hosted runs must still pass the actual compiler, Clang API, sanitizer and fuzzer
probes; setup success is not compiler correctness qualification.

The workflow preserves all existing hosted rows, fuzzing and aggregate failure
semantics. Cache hits remove upstream **release** downloads, not apt, corpus,
checkout or GitHub-service network access. Record cold/warm setup and transfer
costs (including preparation and the artifact handoff) before claiming a latency
improvement. Raw measurement evidence belongs in `buster-performance`.
