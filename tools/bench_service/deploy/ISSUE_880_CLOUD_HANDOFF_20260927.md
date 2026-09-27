# Cloud integration handoff — 2026-09-27

Refs #36/#880/#881. This is an unmerged source/validator preparation packet.
It does not authorize installation or dispatch. The existing execution writer
and paired LOCAL session retain their distinct authority.

## Exact identities

| Role | Commit | Tree / binding |
|---|---|---|
| Protected main and smoke baseline | `ade6ac4b6ecb21f30b61b656439bac476c145e2f` | `4c5306221fdb22fccc929b55e333163742de17d0` |
| Integrated source candidate | `5c5c3eb1fbd6ea1b82730da63be87643d21aca64` | `6f16db7e6dabb681f64bd10142793f609c7adab1` |
| Candidate build driver blob | same source candidate | `9825adbe2a3f488fcea55a6cd533be6193030d9b` |
| Source manifest | 375 files, 42,585 bytes | SHA-256 `35617564a362e47ff44decf7d06887823ef6800e251773a91667c079b8b05b2c` |
| Existing execution ref, unchanged | `f52601c2f160d72e66a929878411840bc7451d39` | `a0940592817b9c61447b0a1e44cfcfd2a4dc9385` |
| Reviewed gate component source #1595 | `4026e1923653ce813c88c6c900f497c3311b00eb` | `29480a9caa56b4a4c72172bd06dd7c7a1a3a740b` |

The source candidate has explicit parents for #1595, #1540/#1494 recovery, and
#1548 GET-only admission verification. This also reconciles protected-main
ancestry into the older #1159 service stack. The source/IR/compiler files match
pinned main; no generated retirement identity was manually edited. Generated
binding/admission is not claimed current: only the existing trusted rebinder
may refresh coordinated identities after its own authorization and review.
The transport commit containing this packet is distinct from the source
candidate, its PR test merge, any future merge group, and protected main.

## Existing full-service validator handoff

The existing `.github/scripts/issue1162_exact_systemd_slice.sh` now pins the
source candidate, tree, driver blob, and freshly computed exact source manifest.
It builds and runs the registered broker/gate regression; installs the static
credential gate; checks its ELF; installs the canonical six-number root-owned
0444 account receipt; and compiles the current read-only state probe signature.
All previous full-service observers, deadline, cleanup, export and replay
requirements remain. The workflow trigger and sole execution branch are
unchanged. No additional full-service or physical executor was created.

The existing #1159/#1494/execution owner must first post an exact-head/scope
handoff or retain ownership and adopt this transport through normal review.
Then run the complete existing hosted systemd test on the pinned source and
retain all outcomes. Latest old-source Attempt21 failed; ordinary CI and the
credential component cannot replace it. This packet has not run that full test.

Local preparation checks: strict GCC embedded state-probe compilation, build
syntax, shell syntax, exact source-manifest bytes, policy/admission tests;
observer 158, consumer145, live88, lease13, stage35, workspace25, denial56,
frozen-tree38 and ancestry9 fixture checks. These are source/fixture checks,
not full-systemd execution evidence.

## Credential component proof and limits

Run `36327060642`, job `108641737477`, source #1595 above: eight cases passed
(two clean, six changed UID/GID/supplementary-group cases), with actual PID1
credential probes, two admitted payload markers, zero forbidden markers,
six gate exits126, and sixteen unit/cgroup cleanup readbacks. Artifact
`10933883953` ZIP SHA-256
`1ec0faf3c6c5d4c4cd2d9c5ec3a5fad5ce82e8c3116493b562d4d001b172e369`
was preserved and independently replayed. Review `5330766504` is a technical
COMMENT, not GitHub approval. #1543 disposition is comment `5856923600`.
That ZIP records executable hashes and ELF headers but has no executable bytes
for independent binary rehash. This transport adds those bytes to subsequent
component artifacts; no old evidence claim is upgraded retroactively.

Failed component attempts remain retained: `36326178806` static Clang flag
failure; `36326343790` isolation refusal; `36326597889` missing guest-runtime
opt-in; `36326832793` seven cases passed, GID setup assumption failed. They are
not successful complete attempts. None is a physical host or performance job.

## Maintainer-owned inaccessible administration

The connected read surface cannot retrieve environment or Actions-variable
administration endpoints. A maintainer with the relevant access must publish
these exact readbacks and disposition before installation or admission:

1. Preserve main ruleset `22537199` and its required checks/queue/protections;
   keep the existing actor/runner restrictions. Do not use a bypass to integrate.
2. Read `BENCH_SERVICE_DISPATCH_ENABLED`; stage it `false` only outside a live
   authorized lease, and leave dispatch disabled while these gates are open.
   Publish a readable value; do not treat an inaccessible endpoint as `false`.
3. Resolve and record that `davidgmbb` remains repository administrator while
   `buster14a14a` is the proposed independent required environment reviewer.
   Verify numeric identities, repository permissions, environment required
   reviewers, prevent-self-review and main-only branch restriction. Preserve
   existing protections; no automatic replacement of reviewer/admin roles.
4. Read organization runner-group selected repository and exact allowed workflow
   `.github/workflows/9700x-service-dispatch.yml@refs/heads/main`; prove the single
   intended runner membership and absence of repository-scoped substitutes.
5. After reviewed protected integration, run the documented read-only admission
   preflight from that exact protected main and publish its GET readbacks. A
   technical agent comment is never the required human environment approval.

## LOCAL installation and recovery boundary

Only after full hosted validation, reviewed protected integration and maintainer
admission: cloud supplies the exact protected commit/tree, trusted binary hashes,
unit/config/recipe/profile hashes, canonical numeric account receipt and this
packet's successor. LOCAL alone authenticates, installs and reads them back.
Use `ISSUE_880_OPERATOR_PACKET.md` and `SYSTEMD_BROKER.md`: disable admission,
drain and prove old units/processes/cgroups absent; retain rollback binaries and
receipts; install root-owned static gate and exact account receipt; restart and
read actual daemon/runner/broker credentials as well as NSS membership. A
trusted-root receipt replacement between requests is an operator boundary,
not a code-enforced whole-lease pin. Any authorization change requires a new
safe disabled/drained installation window. Keep TERM/KILL recovery available.

LOCAL must publish all five authenticated recovery scenarios, a safe subsequent
job, export and clean replay receipts before #880 acceptance. Cloud then alone
submits a frozen physical plan through the existing protected-main gateway,
after shared request ID/readiness and independent authorizations are present.
No such submission is authorized by this preparation packet.

## Retirement sequence remains blocked

#923 remains owner-held. #1584's bounded oracle producer is a partial B handoff,
not an A–F production caller. #1022 owns the demonstrated full-population export
inventory capacity blocker; do not lower samples or alter #508/#511. #1367 must
bootstrap before #1368 policy rebinding, and #1360's 24 Doom iOS rows and current
same-attempt semantic/no-fallback proof remain open.

Required order remains #880 live acceptance, #881 reviewed admission, applicable
#422/#426 qualification, independently replayed #882 on exact pre-cutover
candidate, #522 cutover, #1014 direct-native-only deletion, exact final-main
correctness gates, and a new full independently replayed #883. Neither #882 nor
#883 has an attempt ID in this handoff. #512/#36 remain open.
