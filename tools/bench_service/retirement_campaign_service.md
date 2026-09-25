# Private queue-aware campaign bind seam

`retirement_campaign_service.h` adds the internal
`bq_retirement_campaign_service_bind` entry. It reads the job and attempt token
from the service queue, requires that job to be the active measuring attempt,
and derives the transcript label as `job-<queue id>`. Callers do not provide a
separate transcript job string.

Before freezing the campaign, the entry independently calls the pinned A record
readback and import paths. It derives the A record SHA from the durable record,
then requires that SHA and both source manifest hashes to match the ready
correctness gate. It reads the durable binary record digest from the private
queue directory itself and passes that digest to the existing acquire importer,
which rereads the record and frozen files while opening the two held
descriptors. Only those acquired descriptors reach
`bq_retirement_campaign_bind_held`.

The pinned entry exists for the miniature fixture, whose installed inventory
and toolchain pins are test-created. The ordinary private entry obtains the
compiled profile from the queued request. Neither entry is called by the
production worker. The preparation test runner uses an injected active blocked
job because the public registry continues to reject that recipe.

The focused fixture reuses the preparation test's real miniature source
materializer and binary record/import helpers. It checks a successful same-job,
same-attempt durable A and held-binary bind, then refuses an unknown job,
wrong token, wrong queue phase, and a changed preparation record. The two
frozen executable files contain fixture bytes; the test does not establish
trusted Clang provenance or launch a timed child. Its one-row gate, plan hash,
and context hash are test data.

This is a binding prerequisite only. There is no production retirement runner
caller, admitted recipe, independent plan/context derivation, post-sample
receipt authority publication, or queue/export receipt replay for this path.
The campaign freeze context is pre-sample; the contract receipt context is
post-sample and includes raw measurements. Their relationship still needs a
producer-owned implementation. The recipe stays blocked, and this work makes
no full-corpus or performance claim.
