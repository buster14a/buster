/* Atomic admission for the dedicated benchmark host.
 *
 * The authenticated service owns the queue writer, so checking for unfinished
 * work and appending the new submission in this call closes the drain/submit
 * race.  Exact idempotent retries still return their original job before the
 * idle check; a reused key with different bytes remains a conflict.
 */
BUSTER_GLOBAL_LOCAL BqError bq_submit_exclusive(BqQueue* queue, BqRequest const* request, u64* id)
{
    *id = 0;
    BqError error = queue->poisoned ? BQ_IO : !bq_request_valid(request) ? BQ_BAD_REQUEST :
                    !bq_recipe_real(request) ? BQ_UNSUPPORTED : BQ_OK;
    for (u32 index = 0; error == BQ_OK && !*id && index < queue->state.job_count; index += 1)
    {
        BqJob* job = queue->state.jobs + index;
        if (bq_same_key(request, &job->request))
        {
            if (request->size == job->request.size && !memcmp(request->bytes, job->request.bytes, request->size))
            {
                *id = job->id;
            }
            else
            {
                error = BQ_CONFLICT;
            }
        }
    }
    if (error == BQ_OK && !*id)
    {
        error = queue->needs_reconciliation ? BQ_RECONCILIATION_REQUIRED :
                queue->state.active_id || bq_pending(&queue->state) ? BQ_BUSY : BQ_OK;
    }
    if (error == BQ_OK && !*id)
    {
        error = bq_append(queue, BQ_SUBMIT, request->bytes, request->size);
        if (error == BQ_OK)
        {
            *id = queue->state.sequence;
        }
    }
    return error;
}
