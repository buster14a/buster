# Cross-run analyzer reuse qualification: bounded-model no-go

**Disposition:** no-go for the tested bounded whole-input-tree observer. This
does not reject other cache designs, especially per-invocation designs with a
separately authenticated producer. No cached verdict was consumed. Each shadow
attempt invoked the existing full analyzer and left its result format,
workflow policy and acceptance authority unchanged.

This record preserves the temporary native prototype, the exact two-arm
full-inventory campaign, the miniature changed-source controls and the later
hardening checks. The full arms used underlying repository source commit
`fcf5790c4aa3452396113b99eaeecb9f7c496b91`, with only the original native
prototype overlay from commit
`7c9f3082d885993291893c52e0de7334ac2178cb` (the helper source and its
`build.c` registration; combined tree
`c14d82134cbd1dcd44eab9d3802b151969cf6281`). The hardening source is a separate
later revision and was not used for either full arm. The current branch removes
the command and source after retaining them here.

## Finding

The observer found a repeat partial-input fingerprint on the second full run,
but the whole snapshot was incomplete and the content match remained false.
The snapshot covered 1,388 entries and 133,380,341 bytes (127.20 MiB of the
128 MiB / 134,217,728-byte budget). It did not bound implicit Clang resource
headers, implicit system include search, the compiler runtime/loader closure,
or the complete process environment. It also detected time-sensitive or
token-spliced input. The prototype therefore marked producer authentication
unavailable, eligibility false and qualified reuse zero in both arms.

Both arms still ran the full Release analyzer inventory and both independent
aggregations passed all 182 selected translation units. The first full run
took 962.101 seconds; the repeat took 956.996 seconds. Whole-run discovery
(planning plus pre/post snapshots) took 2.581 and 2.758 seconds respectively,
about 0.27% and 0.29% of analyzer wall time in this cloud container. That is a
diagnostic cost for the tested implementation, not a dedicated-host benchmark.
Since neither run was eligible, the coarse whole-run reuse rate was 0/2 and
qualified analyzer work saved was zero. Per-translation-unit time weighting
was unavailable because this observer did not map child timings to eligible
rows. No Zen 5 performance acceptance was run.

The analyzer reported largest individual child high-water RSS values of
596,664,320 and 596,193,280 bytes; these are not combined simultaneous RSS.
Its `sampled_peak_tree_rss_bytes=2125824` is a coordinator-only lower bound
under the incomplete sampler tracked by
[#3159](https://github.com/buster14a/buster/issues/3159), not total child
memory. CPU time was unavailable. These runs used shared cloud scratch with
no exclusive CPU lease; host-wide runner occupancy was not measured. The
reported `peak_live_processes=1` describes the analyzer's observed process
population and says nothing about unrelated host processes.

The cost was not the only blocker: the observer approached its byte cap while
still missing required system/toolchain inputs, and a SHA-256 match cannot
authenticate who produced a state file or whether the producer was trusted.
The local state record is only a content-integrity check. A producer identity
would need to come from an independently trusted channel; no such receipt or
trust boundary was available to this prototype. Its `authoritative_eligible`
field was always zero by design.

## Evidence and reproduction

[`full-campaign-evidence.tar.gz`](full-campaign-evidence.tar.gz) contains both
complete analyzer result directories and logs, both independent aggregate
outputs, the observer records and state, both full stdout captures, the
compile database and CMake cache, generated input, the exact driver binary,
miniature campaign attempts and fixture, plus source/patch and compiler
provenance. `full-campaign-evidence.tar.gz.sha256` pins the archive; the
archive's `SHA256SUMS` pins its members. The separate source files beside this
README make the original and hardened code reviewable without extraction.

The complete source and run details are in the
[audit](../../2026-10-09T012151Z.md). The immutable base source
used for both full arms is available at
[`fcf5790`](https://github.com/buster14a/buster/tree/fcf5790c4aa3452396113b99eaeecb9f7c496b91).
The local prototype overlay is preserved by its source and exact patch beside
this README and inside the archive.
Run artifacts retain their original absolute workspace paths; reproduction
with the archived compile database therefore requires that checkout path or
an explicit path rewrite followed by a new campaign.

To reproduce from a fresh checkout, extract the archive, check out the pinned
base above, and apply the archived prototype patch from the repository root:

```sh
mkdir -p /tmp/issue3134-evidence /tmp/issue3134-out
tar -xzf docs/performance-audits/evidence/2026-10-09T012151Z-issue3134/full-campaign-evidence.tar.gz \
  -C /tmp/issue3134-evidence
git worktree add --detach /tmp/buster-3134 fcf5790c4aa3452396113b99eaeecb9f7c496b91
cd /tmp/buster-3134
git apply /tmp/issue3134-evidence/prototype/prototype-full-pair.patch
```

Then compile the driver with the command below. If this direct Clang build
needs the generated shader header, restore the archived generated directory:

```sh
mkdir -p build/generated
cp -a /tmp/issue3134-evidence/full/generated/. build/generated/
```

The header contains absolute paths from the original workspace. For a new
checkout, rewrite those path strings to the new generated build directory
before using it. Generate a fresh database with the command below. For
byte-for-byte reanalysis of the original inputs,
use the archived compile database and driver binary at the original checkout
path instead; otherwise a new database and run constitute a new attempt.

The direct driver build used this exact command from the repository root:

```sh
/usr/lib/llvm-21/bin/clang -std=c11 -O2 -g0 -Isrc -Ibuild/generated \
  -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -fwrapv \
  -fno-strict-aliasing -funsigned-char -Wall -Wextra -Werror \
  -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function \
  -Wno-missing-field-initializers -Wno-missing-braces build.c -lm \
  -o build/clang-analyze-shadow-driver
```

The source-specific build driver generated the independent Release analysis
tree with:

```sh
PATH=/usr/lib/llvm-21/bin:$PATH build/clang-analyze-shadow-driver generate \
  --build-directory build/analyzer-shadow-full --ci --no-sanitize --no-fuzz \
  --no-lto --linker DEFAULT -- -DBUSTER_UNITY_BUILD=OFF
```

Each full arm used the same driver, compile database, source, configuration,
arguments, cwd, compiler, observed environment digest and state path. The
results directory and state contents naturally differed between arms:

```sh
build/clang-analyze-shadow-driver clang_analyze_shadow \
  build/analyzer-shadow-full --config Release \
  --clang /usr/lib/llvm-21/bin/clang --shards 8 --jobs 2 --timeout 600 \
  --results /tmp/issue3134-out/full-arm-N \
  --shadow-state /tmp/issue3134-out/state
```

For each arm, the ordinary independent aggregation was then run against that
arm's result directory and returned `status=pass`. The explicit aggregation
command, run with the same fresh result directory, is:

```sh
build/clang-analyze-shadow-driver clang_analyze build/analyzer-shadow-full \
  --config Release --clang /usr/lib/llvm-21/bin/clang --shards 8 --jobs 2 \
  --timeout 600 --results /tmp/issue3134-out/full-arm-N \
  --aggregate
```

No built Buster compiler was used for analysis. The driver was compiled
directly by the installed Clang because TCC 0.9.27 is below the repository's
minimum supported 0.9.28rc. The full input configuration and exact compiler
identity are in the archive manifest. To rerun the hardened native controls,
first apply the full-pair patch above, then replace the helper and rebuild:

```sh
cp /tmp/issue3134-evidence/prototype/prototype-hardened.c tools/clang_analyze_shadow.c
/usr/lib/llvm-21/bin/clang -std=c11 -O2 -g0 -Isrc -Ibuild/generated \
  -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -fwrapv \
  -fno-strict-aliasing -funsigned-char -Wall -Wextra -Werror \
  -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function \
  -Wno-missing-field-initializers -Wno-missing-braces build.c -lm \
  -o build/clang-analyze-shadow-driver
build/clang-analyze-shadow-driver clang_analyze_shadow --self-test
```

The hardened source passed 12 checks, zero failures. The original frozen
prototype passed 10 checks, zero failures; the hardening source did not run
the full campaign.

## Input, invalidation and trust model

| Area | Model and observed result |
| --- | --- |
| Invocation identity | Hashes analyzer config, database path, Clang path, shard/job/timeout/quiet policy, working directories, source/output strings, ordered compile-database argv and projected analyzer argv. The compiler executable and analyzer policy driver are hashed. The original full-pair hash used NUL separators, not length framing, so a hypothetical embedded NUL in a database string was ambiguous; later hardening adds length framing. These digests describe inputs; they do not authenticate a producer. |
| Explicit input roots | Recursively hashes regular-file contents and sorted directory membership for each TU cwd/source parent and recognized explicit `-I`, `-isystem`, `-iquote`, `-idirafter`, `-F`, `-iframework`, forced include/macro, resource-dir, sysroot, module-map, VFS overlay and plugin roots. Relative options resolve against the TU cwd. Missing explicit paths contribute to the digest so creation can invalidate a prior observation. This bounds negative lookup only inside the scanned roots. |
| Tree behavior | Iterative snapshot capped at 20,000 entries and 128 MiB. Symlinks and special files make the snapshot incomplete; permission, I/O, size and concurrent read failures miss. Directory entries are sorted before hashing. Snapshot before/after the full run detects persistent input changes, but does not provide a filesystem snapshot or prevent transient mutation between reads. |
| Dynamic and virtual inputs | Scans bytes for `__DATE__`, `__TIME__`, `__TIMESTAMP__`, token-paste and line-splice markers; these conservatively invalidate. Module, plugin, VFS and `-Xclang` options invalidate. Unknown/unparsed input forms must miss. |
| Toolchain and system inputs | This model does not inventory Clang's implicit resource directory, implicit system include search or dynamic loader/runtime closure. These were explicit invalidation reasons on both full arms. `-nostdinc` does not prove the compiler resource directory or runtime closure. A wrapper executable hash does not bind its own runtime or transitive toolchain; wrapper closure was not qualified. |
| Environment | Hashes a sorted, explicit compiler-related allowlist (`PATH`, include/compiler/config/runtime-related keys and `CLANG_`/`LLVM_`/`GCC_`/`LD_`/`DYLD_` prefixes). Because the remaining process environment and external host state are not bounded, the model marks environment identity incomplete even if the allowlist digest matches. |
| Unparsed inputs | Response-file contents and every possible Clang input form are not comprehensively parsed by the prototype. Such forms would need an explicit content snapshot or invalidation before any authoritative use. The full arms remained globally incomplete and ineligible, so these omissions could not yield a qualified hit in this campaign. |
| Schema and invalidation | Uses `BUSTER_CLANG_ANALYZE_SHADOW_V1` and a bounded `BUSTER_CLANG_ANALYZE_SHADOW_STATE_V1` sidecar; the existing analyzer result format is unchanged and no consumer reads this sidecar. Both full runs were invalidated by `dynamic-or-token-spliced-input`, `implicit-compiler-resource-directory-unbounded`, `implicit-system-include-search-unbounded`, `compiler-runtime-closure-unavailable`, and `environment-identity-is-an-explicit-allowlist`. |
| Producer and state | State is a small, bounded local record with symlink/corruption/oversize protections. A checksum detects accidental corruption; a user who can replace the record can recompute it. The full-pair reader did not use nonblocking open, so a FIFO path could block before the file-type check; hardening added `O_NONBLOCK` and a FIFO control. No authenticated producer receipt, trusted cache namespace or independent attestation exists. Thus a matching record is an observation only. |
| Verdict authority | Every attempt calls normal `clang_analyze_main`; parser, planning and launch failures remain possible, and a call alone is not counted as completed analysis. The existing analyzer parser/result and independent aggregator decide success. No code path accepts shadow state as a verdict or skips fresh analysis. A failed fresh run does not publish successful state. |
| Post-campaign hardening | The original full-pair tree walk did not distinguish `readdir` failure from EOF. The archived hardening checks and invalidates directory read errors, length-frames strings and opens state nonblocking; its 12/12 self-test is a separate control revision, not the full-run implementation. |

The original full-pair C source and exact integration patch are
[`prototype-full-pair.c`](prototype-full-pair.c) and
[`prototype-full-pair.patch`](prototype-full-pair.patch). After preserving the
campaign, hardening added length-framed string hashing (including embedded
NUL), directory read-error detection and nonblocking rejection of FIFO state
paths; see [`prototype-hardened.c`](prototype-hardened.c) and
[`prototype-hardening.patch`](prototype-hardening.patch). Those changes have
their own captured self-test and were not back-applied to the frozen full-arm
artifacts.

## Controls and limits

The miniature campaign exercised same-source repeats, source content changes,
an earlier include directory gaining a `switch.h` after a previous lookup
miss, a fresh analyzer failure from that header, and a successful run after
the header was removed. Runs 12/13, 16/17, 18/19 and 20/21/22/23 retain the
source fingerprint and outcomes; runs 21 and 22 failed with the new higher
priority header, and run 23 passed only after its explicit removal. The
unchanged-source observations reported partial fingerprint matches but still
reported `content_fingerprint_match=0`, `authoritative_eligible=0` and
`qualified_reuse_us=0`.

The native self-test covers deterministic sorted directory hashing,
unchanged-tree observation, added higher-priority lookup candidate, changed
included contents, state round-trip, corrupt/oversized/symlink/FIFO state,
symlink input ineligibility, length framing and the rule that a content match
never authorizes reuse. The original prototype passed 10/10; the hardened
source passed 12/12. These controls test the observer's mechanics, not cache
authority.

## License context

At the inspected base `fcf5790`, Buster's first-party grant remains unselected
under [#621](https://github.com/buster14a/buster/issues/621); see
[`LICENSES/README.md`](../../../../LICENSES/README.md). The Clang compiler is
covered by Apache-2.0 WITH LLVM-exception in
[`LICENSES/llvm-LICENSE.txt`](../../../../LICENSES/llvm-LICENSE.txt). This
change adds no imported upstream implementation. This is not a license audit
of distribution components.

The result is a no-go for this bounded whole-tree approach as an analyzer
reuse qualification: it spent measurable discovery time hashing almost the
entire budget yet still had no complete input proof, authenticated producer,
eligible full run or saved analyzer work. It does not disprove a narrower
per-invocation cache with complete dependency and negative-lookup tracking,
explicit host/toolchain constraints and trusted producer authentication.
