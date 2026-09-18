# Native-retirement dependency rebinding

`tools/native_retirement_rebind.py` reconstructs the four native-retirement
dependency identities after source branches have been integrated. It is a
contract repair command, not a merge strategy and not an inventory generator.

Only project records whose provenance is exactly `repo:<source>` are eligible
for byte-count and SHA-256 refresh. External checkout declarations remain bound
to the independent constants in `tools/native_retirement_contract.py`; SDK
records remain bound to `docs/native-retirement-sdks-v1.json`; resource records,
archived replay data, the support/applicability inventories, fixture decisions,
and historical evidence are never refreshed by this command.
The current project-only descriptor omits the optional `resources` section;
checking and refreshing preserve that omission without adding resource records.

## Prepare the complete pinned closure

The command is offline and requires the same complete source tree as the census
materializer. From the exact integrated checkout, prepare and verify the pinned
SDK and external inputs first:

```sh
python3 tools/native_retirement_sdks.py --source-root "$(pwd)"
python3 tools/native_retirement_external.py prepare \
  --manifest docs/native-retirement-dependencies-v1.json \
  --source-root "$(pwd)"
```

The external verifier proves the declared Git revisions, remotes, tracked blobs,
clean worktrees, and generated musl headers. The SDK preparer proves the archive
and selected-member identities. Rebinding then materializes the entire candidate
dependency closure through `native_retirement_materializer.py`; a missing,
changed, symbolic, hard-linked, malformed, or unpinned input fails before any
binding file is replaced.

## Check and refresh

Read-only validation is:

```sh
python3 tools/native_retirement_rebind.py check --repo-root "$(pwd)"
```

`check` does not modify the worktree. It exits 0 when the bindings are current,
2 when a deterministic refresh is required, and 1 when the contract cannot be
reconstructed safely. Its JSON report lists every changed repository source
record with old/new byte counts and SHA-256 values, every changed aggregate
identity, and every tracked file that refresh would replace.

Apply an explicit refresh with:

```sh
python3 tools/native_retirement_rebind.py refresh --repo-root "$(pwd)"
```

The refresh prepares all replacements and validates the complete closure before
writing. It rechecks every admitted source immediately before publication,
stages same-filesystem replacements, preserves file modes, and rolls back a
failed multi-file replacement. The only possible tracked outputs are:

- `docs/native-retirement-dependencies-v1.json`
- `docs/native-retirement-census.md`
- `tools/native_retirement_census.c`
- `tools/native_retirement_contract.py`

A second refresh on unchanged inputs reports `current` and produces no diff.
Neither mode fetches dependencies, edits third-party/SDK pins, adds descriptor
records, changes the frozen corpus, or treats either parent's aggregate hashes
as an expected result.

## Ordered landing procedure

For overlapping dependency changes, use this order:

1. Land the preceding prerequisite branch.
2. Integrate the new `main` into the next branch and resolve the actual source
   changes first. Related work may instead be stacked deliberately on the same
   reviewed predecessor.
3. For aggregate-only conflicts, make the three declaration files syntactically
   well formed and mutually consistent by selecting one complete parent quartet
   as a *provisional* resolution. Do not calculate or approve hashes by hand.
   Resolve descriptor structure normally; repository-owned byte/hash fields are
   reconstructed from the combined source tree.
4. Prepare the exact pinned SDK and external closure using the commands above.
5. Run `check`, inspect its source and identity report, then run `refresh` once.
6. Review that the diff contains only the intended source changes and the four
   binding files listed above. Any external, SDK, inventory, applicability, or
   historical-evidence change needs its own reviewed contract update.
7. Run `refresh` again and require no diff, then run `check` and the contract and
   materializer suites on the exact combined head:

   ```sh
   python3 tools/native_retirement_rebind_test.py -v
   python3 tools/native_retirement_materializer.py self-test
   python3 tools/native_retirement_materializer_test.py -v
   python3 tools/native_retirement_contract.py self-test
   python3 tools/native_retirement_contract_test.py -v
   python3 tools/native_retirement_rebind.py check --repo-root "$(pwd)"
   ```

8. If another overlapping branch lands before this branch, repeat from step 2
   against the new combined head. Never carry forward a previously generated
   aggregate quartet without rematerializing that exact tree.

`git merge-tree` is useful for conflict preflight, and `git rerere` may reuse a
reviewed textual resolution, but neither may supply the regenerated identities.
An `ours` or `union` merge driver is intentionally not installed. Merge-queue
enablement remains a separate repository-policy review: `merge_group` checks can
validate a combined candidate, but GitHub removes content-conflicted changes
rather than resolving or rebinding them.
