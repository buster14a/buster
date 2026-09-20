# Native-retirement dependency rebinding

Native-retirement dependency state has three owners. They are intentionally
separate so an ordinary source change does not become an editor of reviewed
trust policy or of several consumer-specific hash copies.

## Authorities

1. `docs/native-retirement-dependencies-v1.json` is reviewed policy. It owns
   inventory, destinations, provenance, external checkout revisions,
   generated-external rules, SDK/resource pins, archived replay, and every
   other non-derived declaration. Eligible `repo:<source>` project records do
   **not** contain `bytes` or `sha256`.
2. `docs/native-retirement-repository-sources-v1.json` is generated state. It
   contains exactly one sorted `{source, bytes, sha256}` record for every
   eligible repository-owned project source and binds those records to the
   exact policy bytes.
3. `tools/native_retirement_dependency_binding.generated.h` is the single
   generated aggregate authority. It binds the policy and source snapshot to
   the materializer receipt, project closure, and dependency ledger. The C
   census includes it directly; Python parses the same canonical file.

`docs/native-retirement-dependencies-legacy-v1.json` is an immutable
compatibility input. It preserves the monolithic descriptor used by archived
#508/#510 evidence. Refresh never writes it. The live materializer reconstructs
that v1 projection from policy plus snapshot and independently checks it before
publication.

## Commands

From a checkout containing all pinned SDK/external inputs:

```sh
python3 tools/native_retirement_rebind.py check --repo-root "$(pwd)"
python3 tools/native_retirement_rebind.py refresh --repo-root "$(pwd)"
```

`check` is read-only and exits 2 when generated state is stale. `refresh` may
replace only the generated source snapshot and aggregate header. A second
refresh is byte-identical. Both commands reject policy drift, malformed or
incomplete snapshots, duplicate records, external/SDK changes presented as
repository refreshes, source TOCTOU, and a quartet that does not match an
independent materialization.

## Integration order

For two branches changing admitted repository sources:

1. merge the substantive source changes;
2. retain the reviewed policy from either side when it is otherwise unchanged;
3. run `refresh` once on the integrated tree;
4. review the generated snapshot delta and the single aggregate-header delta;
5. run `check` and the materializer/contract/census tests.

Two changes in different sources, or in different regions of one source, never
require hand-merging policy byte/hash literals. The combined tree deterministically
produces the only valid snapshot and aggregate binding.

Policy changes remain ordinary reviewed changes. Changing external revisions,
SDK/resource declarations, provenance, destinations, corpus projections, or
applicability is not a rebind and must be reviewed with the corresponding trust
contract. The repository-source path fails closed for those changes.

## Evidence compatibility

New evidence carries copies of the reviewed policy, generated source snapshot,
and resolved v1 descriptor. The validator reconstructs the resolved descriptor,
recomputes the materializer ledger, and cross-checks the receipt/project closure
rather than trusting producer output. Evidence without the generated snapshot
is treated as legacy only when all frozen previous identities match exactly.

This change does not implement #864 or #865. Ordinary feature-PR ownership and
cutover policy remain separate follow-up work.
