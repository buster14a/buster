# Git v2.51.0 first blocker census

Owning issue: [#3180](https://github.com/buster14a/buster/issues/3180).
Compiler prerequisite: [#2302](https://github.com/buster14a/buster/issues/2302).

The retained ZIP is the byte-exact GitHub-hosted diagnostic artifact from
[run 38131303110, attempt 1](https://github.com/buster14a/buster/actions/runs/38131303110),
artifact ID `11699805279`. It contains logs, exact compiler action records,
dependency files, identities, test counts, CMake metadata and license notices.
It contains no Git sources, objects or executables. Its original upload selected
only the 231 root-level reference depfile bodies, omitting nested `.depend`
directories from the 532-action build. Complete dependency closure cannot be
reconstructed from this archive. The corrected workflow recursively selects
`.depend/**/*.d`; that selection fix is not execution evidence for this ZIP.

- Candidate/census revision: `5c9d1a1a77aa21d6a43acf5dc55e0daf482d338d`
- Candidate tree: `48da91a4699a3b18bf87936f5975603f9d07af6d`
- Compiler source base: `abb1c5afbcf55e120d83283ffbad402fc75bd80a`
- Git pin: `c44beea485f0f2feaf460e2ac87fdd5608d63cf0`
- Buster executable SHA-256: `540a6e7a4535bfe8093f28a4d7f764680f55121ba1850a43ff1b26a3ea4bf19d`
- Independent Clang executable SHA-256: `78e9dd2ccc9e88859b3a29109a44bfab00e3d6cb70f00f3daae063eed3d05251`
- ZIP SHA-256: `1eff9e0babfabbcab67d9e6ac3c34998d9de1aad41657db893f03449c2887cb7`
- ZIP size: 2,428,353 bytes

```sh
sha256sum docs/compatibility-evidence/git-v2.51.0/census-5c9d1a1a.zip
unzip -p docs/compatibility-evidence/git-v2.51.0/census-5c9d1a1a.zip _temp/git-qualification/summary.tsv
```

## Observations

| Gate | Recorded result |
|---|---|
| Native routing/quoting controls | PASS |
| Dirty, untracked, ignored, wrong-pin, assume-unchanged and skip-worktree controls | Each rejected as required; restored pristine clone accepted |
| Process failure/timeout controls | Preserved exit 7 and timeout 124 |
| Independent Clang C build | 532 successful compile actions; 18 successful explicitly attributed host links |
| Upstream C unit tests, independent build | 212/212 assertions in 23 Clar suites |
| Six named integration scripts, independent build | 459 declared cases; 450 pass; 9 prerequisite skips; no failures |
| Deterministic local-operation witness, independent build | PASS: conflict stages, successful merge, local clone/push/fetch, repack, full fsck, expected missing-ref failure and final literal content |
| Buster build | 532 attempted C actions, all refused at `-MF`; zero source compilations |
| Buster link/runtime/upstream tests and paired object comparison | NOT RUN: required C build failed |
| Original Git inputs before/after | Pristine |
| Overall qualification process | Exit 1 / red, as required for the Buster build failure |
| Full integration suite / incremental-edit / memory-size / Zen 5 acceptance | NOT RUN |

Integration skips are four MINGW cases in `t0001-init`, two
`!PTHREADS` cases in `t5300-pack-object`, and one MINGW plus two
CASE_INSENSITIVE_FS cases in `t5601-clone`. Exact per-script counts and
TAP descriptions are retained in the archive. The six scripts are named in
the [compatibility guide](../../agents/compatibility/git.md); this is not the
entire upstream test inventory.

The later quoting-capacity hardening is separately tested with Clang and GCC
at `9e55d5f7ae532ff651f9928712e79578b8bac832`,
[run 38131733787](https://github.com/buster14a/buster/actions/runs/38131733787).
That run validates the native controls and does not replay the full census;
do not assign the retained candidate's compiler digest to a later revision.

## Depfile-candidate follow-up

The separate, normal dependency-enabled [run 38133232841](https://github.com/buster14a/buster/actions/runs/38133232841),
attempt 1, job `114451715285`, layers this harness/workflow on
[#3465](https://github.com/buster14a/buster/pull/3465) exact head
`68aad2f4a168f010b34c2e1d5efedfd55077a179`. It does not establish acceptance
for either PR head alone. Raw artifact `11700301348` is retained byte-exact as
`depfile-candidate-709f24d8.zip`:

- Combined candidate: `709f24d8f5b6905eca0b02d56c4fd0175611238c`
- Combined tree: `7cfc81397bf659288882d24a3dd5dd244f44b8e2`
- Buster SHA-256: `ea30ca2a2b10545acb10406bfc7e26370a49d3e6a547b96cff6dc9abfa752fc7`
- Clang SHA-256: `78e9dd2ccc9e88859b3a29109a44bfab00e3d6cb70f00f3daae063eed3d05251`
- ZIP SHA-256: `11d69d9bfb2e7b73ab44d0190d23563eed33c93e3046666edb42f05e2e4558b1`
- ZIP size: 2,506,020 bytes

All 532 original `-MF/-MQ/-MMD/-MP` C commands reach source compilation:
333 exit 0; 199 exit 1. Post-build hashes record 333 objects and 333 depfiles.
Of the source refusals, 174 name the recursive volatile-list function type
([#3477](https://github.com/buster14a/buster/issues/3477)). The other 25 name
constant/static/local-array, obstack, offsetof and assignment-expression seams;
the [full observed ledger](https://github.com/buster14a/buster/issues/3180#issuecomment-6108296807)
retains exact paths/counts and distinguishes related issues from unproved causes.

The independent Clang cell again builds/links, passes 212 unit assertions,
450 integration cases with 9 prerequisite skips out of 459, and the deterministic
operation witness. All pristine/process controls pass. Candidate runtime and
paired object witnesses are NOT RUN; the full build fails and the census exits 1.
This archive has the same original nested-depfile selection gap: 231 root
reference depfile bodies and 122 root candidate bodies; nested output hashes
are present but their depfile bodies are absent. It is not complete closure or
incremental/performance evidence.

An earlier explicitly source-only diagnostic, `COMPUTE_HEADER_DEPENDENCIES=no`,
at `cef9e57354251a52ae4574d43fb4ca92cd03030a`
([run 38133021670](https://github.com/buster14a/buster/actions/runs/38133021670))
produced the same 333/199 split. Its artifact `11699833310` has ZIP SHA-256
`b65f6323d246b528b03f2b5a8df51ea0643ed045168968b087bee1b543149eef`.
It is a separately named source diagnostic and cannot qualify dependency or
header/source incremental builds; its raw ZIP is not copied here.

## Boundaries and next action

The repeated error is an existing driver capability gap, not 532 independent
Git/frontend defects. The depfile implementation already has an owner on #2302.
Its normal Makefile spelling must retain `-MF/-MQ/-MMD/-MP`; no flag stripping,
fabricated dependency rules or reference-object replacement is authorized.
The separate candidate follow-up establishes that the proposed #3465 driver
gets past that option gate. Integrate/retest the unchanged configuration after
the owner fix lands, and reduce the now-reported source failures.

The ZIP's machine/header/package records and timings are cloud diagnostics.
They establish no 9700X performance result, full system input closure or
whole-project compatibility. The compiler and shared frontend/IR/allocator
files are unchanged by this harness PR.

Git's main grant is GPL-2.0-only with component exceptions; the exact-pin
inventory and package notice boundary are in the compatibility guide.
Notices in the archive retain their upstream terms. Buster's first-party
license remains unselected under `LICENSES/README.md`; this work selects no
license, vendors no implementation and redistributes no compiled Git artifact.
