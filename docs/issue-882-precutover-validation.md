# #882 offline validation log

All commands ran in a newly created detached worktree at
`850433a06c309195e86b0a4827f11d3e1f16febc` (tree
`a634dffdfad43a8e81da76c9b097f53961acc20f`). This was a clean local
checkout of reviewed source; it did not connect to the 9700X, submit a job,
download an actual export, build a measured compiler or run timing. The
contract, validators, recipe descriptor and export instructions at this commit
are byte-identical to those at the packet's later observed main commit.

## Synthetic result-input and binding replay

```sh
PYTHONPATH=tools python3 -m unittest -v \
  tools.native_retirement_result_input_test.ResultInputTests.test_duplicate_record_identity_across_shards_is_rejected \
  tools.native_retirement_result_input_test.ResultInputTests.test_duplicate_shard_identity_and_path_are_rejected \
  tools.native_retirement_result_input_test.ResultInputTests.test_declared_size_digest_and_byte_limits_are_enforced \
  tools.native_retirement_result_input_test.ResultInputTests.test_cross_shard_substitution_is_rejected \
  tools.native_retirement_performance_identity_test.InvocationEvidenceTests.test_complete_warmups_and_native_runtime_join \
  tools.native_retirement_performance_identity_test.InvocationEvidenceTests.test_wrong_order_missing_duplicate_and_extra_invocations_reject \
  tools.native_retirement_performance_identity_test.InvocationEvidenceTests.test_rehashed_wrong_context_or_plan_rejects \
  tools.native_retirement_performance_identity_test.InvocationEvidenceTests.test_receipt_bytes_are_authenticated_before_json_parsing \
  tools.native_retirement_performance_binding_test.BindingTests.test_bounded_validate_evidence_path_replays_sealed_workflow \
  tools.native_retirement_performance_binding_test.BindingTests.test_presample_partition_rejects_postmeasurement_descriptors \
  tools.native_retirement_performance_binding_test.BindingTests.test_sealed_manifest_must_match_frozen_partition \
  tools.native_retirement_performance_binding_test.BindingTests.test_mutable_or_short_identity_is_rejected \
  tools.native_retirement_performance_binding_test.BindingTests.test_incomplete_population_and_unknown_fields_are_rejected \
  tools.native_retirement_performance_binding_test.BindingTests.test_replay_bundle_digest_is_not_self_attested \
  tools.native_retirement_performance_binding_test.BindingTests.test_evidence_tampering_is_rejected
```

Actual result: **15 tests passed in 22.541 seconds**, exit 0. The complete
small-workflow test mocks real support-output and population producers and uses
synthetic receipts. It checks the production validator path but does not
authenticate a service or prove candidate acceptance. The negative cases
rejected missing, duplicate, reordered and extra invocations; duplicate or
substituted shards/records; stale plan context; untrusted receipt bytes;
post-measurement partition descriptors; incomplete population; mutable
identities; and digest mismatches.

```sh
PYTHONPATH=tools python3 -m unittest -v \
  tools.native_retirement_performance_binding_test.BindingTests.test_independent_archive_round_trip_is_streamed_and_bound \
  tools.native_retirement_performance_binding_test.BindingTests.test_independent_archive_rejects_unsafe_member_without_leaking \
  tools.native_retirement_performance_binding_test.BindingTests.test_bounded_result_partition_streams_small_complete_fixture \
  tools.native_retirement_performance_identity_test.PerformanceIdentityTests.test_current_population_and_stage_floor_fit_predeclared_partition \
  tools.native_retirement_performance_identity_test.PerformanceIdentityTests.test_current_population_over_cap_cannot_be_rescued_by_more_shards
```

Actual result: **5 tests passed in 0.018 seconds**, exit 0. The independent
archive fixture is synthetic, not an authenticated #878 export or durable #510
download. The capacity tests use the current support declaration and prove
that extra partitions cannot rescue an over-cap sample plan.

## Same-attempt and stale-identity negative controls

Using the existing `InvocationEvidenceTests` fixture in that same clean
worktree, a complete synthetic transcript joined **732 invocations**. Changing
one process-instance digest to use attempt 2 while its service receipt still
said attempt 1 produced:

```text
mixed_attempt: rejected: execution invocation process identity is not supervisor-bound
```

In a fresh fixture, changing the bound candidate binary SHA-256 without
changing the original receipt produced:

```text
stale_candidate_binary: rejected: execution receipt is not joined to this job's frozen binding and samples
```

Both cases used the production `_check_execution_transcript` validator and
ephemeral test values. They did not produce an accepted plan or verdict.

## Preparation-record checks

On the packet branch, `python3 -m json.tool` parsed the preparation JSON.
Read-only checks matched its admitted schema names to the production constants,
resolved the recorded main and archive source commit-to-tree pairs, rehashed
current contract/support/descriptor files, checked local Markdown links,
recomputed current population/capacity (78,912 object rows, 78,914 minimum
including two stage rows, at most 250 even pairs at that lower bound), and
confirmed all required measured binaries, receipts and the attempt ledger
remain unresolved/empty. The production binding validator was also exercised
against the preparation JSON and **rejected it** for missing required binding
fields. This is expected: it cannot be accepted as an executable record.

No `bench_service unpack-export` was run because no real `.bqexport` or
independently obtained #878 receipt exists. No actual #882 result-input,
statistics, candidate correctness, A/A qualification, service lifecycle or
durable-publication validation is claimed.
