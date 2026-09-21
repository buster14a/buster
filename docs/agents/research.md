# Research lifecycle

[Agent instructions](../../AGENTS.md) · [Issue workflow](workflow.md) · Paths and commands below are relative to the repository root.

Research state belongs on the GitHub issue that owns the question. Do not build a second dashboard, status file, or manually synchronized ledger. Preserve historical issue bodies and measurements; when the current state has moved, add a compact current-state comment and change the issue's lifecycle label.

## Primary lifecycle label

A research, performance, experiment, or architecture issue has at most one primary label from this set:

| Label | Meaning and next transition |
| --- | --- |
| `status/needs-reproduction` | The concern is source-supported, inferred, or reported, but its cheapest behavior-level confirmation has not run on the stated source. Reproduce or falsify it before designing a production repair. |
| `status/needs-census` | The behavior or mechanism is credible, but its current population, attribution, ownership, or materiality is unknown. Obtain the smallest current-source census/profile that can rank or narrow it. |
| `status/needs-benchmark` | A bounded candidate or completed mechanism is ready for its predeclared controlled acceptance measurement. Do not use this label for an unimplemented idea or an unknown population. |
| `status/blocked` | A named dependency, owner handoff, decision, unavailable environment, or prerequisite is the immediate gate. State the blocker and the event that clears it. |
| `status/ready-implementation` | The premise is established, the owner/overlap search is current, and a bounded implementation plus validation contract is defined. Normal implementation work can start. |
| `status/measured-neutral` | A controlled experiment found no material effect under its predeclared envelope. Retain the evidence and close unless a distinct follow-up question remains. |
| `status/rejected` | Evidence shows the candidate is unsafe, incorrect, regressive, or not worthwhile under the declared rule. Preserve the negative result and normally close as not planned. |
| `status/superseded` | A newer issue, implementation, architecture, or evidence source owns the question. Link the successor and normally close as duplicate or not planned. |
| `status/completed` | The requested research result or accepted implementation landed and the issue remains useful as historical evidence. Close as completed and link the result. |

The labels describe the **immediate research gate**, not every future requirement. For example, an issue blocked on a prerequisite is `status/blocked`, not simultaneously `status/needs-benchmark`; change it to `status/needs-benchmark` after the blocker clears. Area, architecture, kind, and priority labels are orthogonal and may coexist.

Do not force a false state. If even the immediate gate is genuinely unknown, leave the lifecycle label unset, add a current-state comment naming the missing decision/evidence, and make classification the next bounded task.

Useful queries include:

```text
repo:buster14a/buster is:issue is:open label:"status/needs-reproduction"
repo:buster14a/buster is:issue is:open label:"status/needs-census"
repo:buster14a/buster is:issue is:open label:"status/ready-implementation"
repo:buster14a/buster is:issue label:"status/measured-neutral"
repo:buster14a/buster is:issue label:"status:completed"
```

Use `label:"status/completed"` for the last query; the colon spelling above is intentionally invalid and must not be copied.

## Current-state comments and transitions

Do not rewrite an old filing merely because its source anchor or dependency moved. Add a short authoritative comment instead:

```text
Research lifecycle: `status/needs-census`
Evidence anchor: <source/tree, run, artifact, or source observation>
Current gate: <the one missing fact, owner, or decision>
Owner/overlap: <active issue/PR/branch, or the search that found none>
Next transition: <objective event and destination lifecycle state>
```

A comment need not repeat facts that remain accurate in the body. It must distinguish observations from inference, link the current owner, and avoid claiming an unrun gate. Change the primary label in the same update. Before closing, record the final disposition and exact result or successor.

Lifecycle changes are evidence changes, not progress percentages:

- source inspection followed by an unrun fixture remains `status/needs-reproduction`;
- a reproduced hotspot with unknown incidence becomes `status/needs-census`;
- a selected, implemented candidate awaiting controlled A/B evidence becomes `status/needs-benchmark`;
- a current census plus a bounded repair contract can become `status/ready-implementation`;
- a predeclared experiment ends as accepted/implemented work, `status/measured-neutral`, or `status/rejected` rather than remaining indefinitely open;
- work transferred to a stronger owner becomes `status/superseded`, with the replacement linked.

## Filing a research issue

Write the issue so a fresh owner can falsify it cheaply and can tell later whether its evidence is stale. When applicable, include:

1. the exact source commit/tree and relevant file, symbol, run, job, or artifact identities;
2. an explicit evidence class: observed source, reproduced behavior, diagnostic measurement, acceptance evidence, or inference/proposal;
3. the current owner and overlap search, including related issues, PRs, and active branches;
4. the cheapest falsification or first discriminating experiment;
5. the metric and denominator, keeping unavailable values unknown rather than zero;
6. a predeclared stop/acceptance rule, including correctness, resource, and performance boundaries that matter; and
7. a final disposition when known, or an explicit unresolved state and next gate.

Use the lightest contract that can answer the question. Small correctness defects do not need benchmark ceremony, and a microbenchmark is not whole-program acceptance unless the issue explicitly says so. Diagnostic instrumentation can explain a result without becoming the acceptance build. Retain failed, cancelled, incomplete, neutral, and rejected attempts when they affect the inference; do not rerun until a preferred result appears.

## Triage discipline

Triage does not expand an implementation owner's scope or acceptance contract. Resolve historical references, inspect current source and overlapping ownership, then label the immediate gate. Use `status/needs-census` when an old population or performance ranking must be refreshed; use `status/superseded` only when a concrete successor or changed architecture actually answers the old question.

A portfolio-wide refresh may update many issue labels and compact current-state comments, but its durable repository output is this convention—not a snapshot table that immediately goes stale.