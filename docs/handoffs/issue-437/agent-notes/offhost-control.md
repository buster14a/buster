# Off-host implementation handoff

Stopped at the user's handoff request. No host, forge, settings, SSH connection or push occurred.

Checkout: work/off-host-control
Branch: codex/437-off-host-control
Current clean HEAD: 9bec5c64b1a32e9fa7cb50ae80792b16b0bef911
Implementation checkpoint: ea0aa6783db21bf5f505ac2511fb921fd2f4a147
History: MCP a2d325ef2 -> offhost checkpoint d09f8d11f -> native05a integration f7e2da8fe -> offhost checkpoint ea0aa6783.
No dirty tracked or untracked files in the checkout. No builds remain running; sessions 46452, 11924, 78697 and 28182 finished.

Latest exact-source normal suite: 33,135 assertions, 0 failures. Evidence: work/offhost-evidence/final-normal.log.
Earlier sanitized suite: 33,037 assertions, 0 failures, BEFORE final custody, restart-gap, cancellation and observation validation refinements. Evidence: work/offhost-evidence/earlier-sanitized.log. This is not sanitizer validation of current HEAD.
Quiet-load receipt: 100 submits (7 accepted, 93 full), 1000 cached reads, zero ordinary worker messages, one exceptional cancellation.
Current git diff --check passed before checkpoint. Generated source-size acknowledgement is committed as 9bec5c64b1a32e9fa7cb50ae80792b16b0bef911; no implementation changed.

Implemented new offhost_session.c / offhost_control.c / offhost_transport.c / offhost_tests.c; unity registration in main.c/tests.c.
- One control-host journal/cache writer with separate public and authenticated worker sockets; no synchronous worker execution in the control daemon.
- Assigned journal generation 7 preserves control job/token, materialize_reserved API, assigned worker adoption of retained same-OFD lease.
- Worker-initiated fixed operator SSH config and command; no request-selected host/commands. Client keepalive, TCP keepalive and timed rekey disabled. Operator-bound worker SHA separate from actual install/boot custody digests.
- Whole admitted job quiet; cached last durable phase remains reserved. No synthesized live measuring events; public cached logs filter post-completion intermediate transitions.
- Original sealed receipt/index/archive transfer, verified private cache unpack and original provenance-root binding validated at cache descriptor; public export reads cached original sealed bytes.
- BQOBS001 32-byte typed response trailer and MCP/CLI rendering of observed UTC milliseconds, stale/paused/reconciliation/quarantine/lifecycle-unavailable and cancellation intent/applied/too-late.
- Native-execute immutable program CAS transfers control -> worker before quiet while physical lease held; repeated committed local CAS is reverified without remote reads. Runtime/custom C remote recipes are explicitly unsupported at this checkpoint.
- Exceptional cancel is retained even if received before execution and applied through existing durable worker cancellation lifecycle; terminal result remains authoritative for applied vs too-late.
- Execution child retains original OFD as post-job custodian until matching terminal-ACK IPC. Parent EOF transfers delivery ownership without new execution. Tests cover ACK, orphan failure quarantine and mismatched IPC ACK.
- Startup with durable lease-custody/execution-entered and no ACK performs necessary local recovery, then records lease-gap and retains reconciled lease in quarantine before SSH. No automatic operator override/resume. This does not claim OFD preservation across whole-cgroup stop/reboot.
- Equivalent Windows heap-fixture fix from parent f4d14d1 is included in tests.c. Root already has that fix; avoid duplicating semantic changes.

Remaining:
1. Run current normal/sanitized suites after any changes. Latest sanitizer is stale.
2. Integrate native-runtime source 3ea60463c35afcfecd11ea6bb9f8ee19624267bb in isolated branch (parent approved just before stop). It defines normal journal 6, BQ_RECIPE_NATIVE_RUNTIME enum 7, bq_recipe_native(execute/runtime), same BQ-NATIVE-V1 store and upload ops 14-16. Extend off-host admission, worker/control CAS predicates and capabilities using that helper. Preserve assigned 7. Custom C enum 8 / separate source store and ops 17-19 requires an actual separate source transfer; explicitly refuse until then. Comparison uses shared normal generation 6, no off-host source-tree transfer contract.
3. Deployment documentation and reference units/config are NOT saved. A multi-file documentation patch failed atomically because of a context mismatch; do not assume OFFHOST_CONTROL.md or new units exist. Document fixed same UID/GID forced-command SSH account (peer authenticator checks exact UID/GID), root-controlled worker identity/key/host pins, effective SSHd quiet policy, no public web/plugin installation, actual physical/systemd admission gates.
4. Provide new worker-agent reference with KillMode=process and Restart=no if reviewed; existing installed unit defaults cannot be claimed to retain custodian on whole-cgroup stop. Document actual source lease-gap quarantine and lack of automated override. No live systemd stop test has occurred.
5. Add cache corruption/missing-cache and source-size/combined validation if warranted, independently review final production source. Existing transfer byte/order/corrupt receipt and ACK/restart fault tests pass.
6. Separate/flatten off-host changes from the intermediate native cherry-pick for parent integration. Root already integrated native05a as f679fa66e and heap fix f4d14d1, and separately owns MCP program upload/export tools and documentation. Do not blindly include native source-size ACK or overwrite root MCP changes. Parent serializes shared-file conflicts, preserving off-host observation rendering plus root uploads/exports.
7. Operator/account gates remain real: control host/account, fixed SSH and pins, installed broker/helpers/native profiles, exact systemd policy/stop behavior, distinct live roots and retained lease, physical benchmark qualification, actual workload/result/load evidence, and authenticated web/MCP deployment. Local fixtures do not close #437.

## Source-size publication follow-up

Only the parent-authorized source-size follow-up was performed after handoff.
Measured implementation HEAD ea0aa6783 against original base 1d4898682304cc7e50ed4a112d08dd39ef257bce.
The initial report exited 0: production growth 0 / within-limit; build growth +177792 bytes / acknowledged. The pre-existing MCP baseline row already satisfied the ratchet; there was no failing verdict to conceal. Full output is work/offhost-evidence/source-size-before-ack.log.
Ran source_size --write-baseline and committed only generated docs/source-size-baseline.txt as 9bec5c64b1a32e9fa7cb50ae80792b16b0bef911. Its measured revision is ea0aa6783, production 16467670 bytes, build 9108946 bytes. Generator output is work/offhost-evidence/source-size-write-baseline.log.
The growth covers all MCP/native/off-host source present on this isolated branch relative to the original base. It does not acknowledge runtime/custom C/comparison or later root MCP export additions; the eventual combined root must generate its own acknowledgement.
Branch remains clean, no running build, no source/docs/runtime implementation or deployment added, and no push/forge/host action performed. Parent handles publication.
