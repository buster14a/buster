/* #881-D store-based campaign import, bind and B -> D ready handoff fixtures.
 * Included by the preparation test runner after retirement_unit_oracle_tests.h.
 *
 * The attempt is the real unit-oracle fixture (#1020 PR 3/4): prepare, the
 * fixture broker's matched builds, the census projection, the oracle
 * authority and reference producer, then the ready record written through the
 * test-only admitted gate. On that record this file checks the ready import
 * (every field re-derived by the coordinator replay; the template and
 * inventory compared with this attempt's policy), refusals of a mutated,
 * forged, re-addressed, foreign-job or foreign-token record and of a moved
 * reference descriptor, the join of a correctness gate to the record, and the
 * held re-import from the record's build and binary digests. The heavy
 * import runs under a SETTLING acknowledgement (bq_prep_campaign_imports:
 * each phase and channel refusal, a cancellation, an expired deadline, a
 * deadline that expires during the import, a live holder, a gate whose source
 * manifests differ, the blocked production profile); the cheap bind runs
 * under MEASURING on the imported pair (bq_prep_campaign_binds: the bind
 * derives its own plan and pre-sample context and marks the binding with the
 * record's digest; each refusal leaves the caller's held pair and record).
 * bq_prep_campaign_timed_rows checks lane E's per-timed-row layout from the
 * pinned performance rows.
 * bq_prep_campaign_driver runs the in-unit driver from its SETTLING
 * acknowledgement through the import to the first untimed launch of the
 * imported held binary, which fails because the census fixture's matched
 * builds are text files: the failure's coordinates and process facts are the
 * retained evidence. The correctness gate here is built from the projection
 * with synthetic row facts and the #509 batch authority stand-in; it is not a
 * #509 admission, and no timed child runs. */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_CAMPAIGN_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_CAMPAIGN_TESTS_H

#define BQ_PREP_CAMPAIGN_CAP 64u

typedef struct BqPrepCampaignStage
{
    TpRetirementExecution execution;
    TpRetirementTranscript transcript;
    TpRetirementSamples samples;
    TpRetirementMetricsShards metrics;
    TpRetirementSampleRow rows[BQ_PREP_CAMPAIGN_CAP];
    TpRetirementSampleGroup groups[BQ_PREP_CAMPAIGN_CAP];
    unsigned members[BQ_PREP_CAMPAIGN_CAP], workspace[BQ_PREP_CAMPAIGN_CAP * 4];
    FILE* stream;
    FILE* spool;
    FILE* metrics_stream;
} BqPrepCampaignStage;

typedef struct BqPrepCampaign
{
    BqRetirementCorrectness gate;
    BqRetirementTrustedRow* rows;
    BqRetirementRowFact* facts;
    uint8_t* assigned;
    TpRetirementBatchInput inputs[BQ_PREP_CAMPAIGN_CAP];
    char fixtures[BQ_PREP_CAMPAIGN_CAP][32], leaves[BQ_PREP_CAMPAIGN_CAP][32];
    TpRetirementBatchContract contract;
    BqRetirementBatchGroup group;
    TpRetirementCampaignBudget budget;
    TpRetirementCampaignReview review;
    unsigned group_stages[BQ_PREP_CAMPAIGN_CAP];
    unsigned row_ids[BQ_PREP_CAMPAIGN_CAP], row_metrics[BQ_PREP_CAMPAIGN_CAP], kinds[BQ_PREP_CAMPAIGN_CAP];
    unsigned offsets[BQ_PREP_CAMPAIGN_CAP + 1], members[BQ_PREP_CAMPAIGN_CAP], runtime[BQ_PREP_CAMPAIGN_CAP];
    unsigned group_row[BQ_PREP_CAMPAIGN_CAP];
    unsigned timed, groups, runtime_count, objects;
    TpRetirementMeasuredCommand commands[2][2 * BQ_PREP_CAMPAIGN_CAP];
    char* arguments[2][2 * BQ_PREP_CAMPAIGN_CAP][3];
    char words[2][2 * BQ_PREP_CAMPAIGN_CAP][32];
    char command_sha[2][2 * BQ_PREP_CAMPAIGN_CAP][65];
    char* environment[2];
    char artifact_sha[65], batch_output[65], object_output[65], budget_sha[65];
    BqPrepCampaignStage stages[2];
    TpRetirementCampaign campaign;
    TpRetirementCampaignCommand snapshots[4 * BQ_PREP_CAMPAIGN_CAP];
    unsigned identities[2 * BQ_PREP_CAMPAIGN_CAP];
    TpRetirementPlan plan;
    char plan_sha[65], context_sha[65];
    BqRetirementHeldBinaries held;
    BqRetirementCampaignBinding binding;
    BqRetirementCampaignReady ready;
} BqPrepCampaign;

/* Supervisor stand-in: acknowledges `answers` phase messages. */
BUSTER_GLOBAL_LOCAL pid_t bq_prep_campaign_peer(BqPhaseChannel* channel, u64 job, u64 token, u32 answers)
{
    int pair[2] = {-1, -1};
    bool paired = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0;
    pid_t child = paired ? fork() : -1;
    if (child == 0)
    {
        close(pair[0]);
        bool ok = true;
        for (u32 index = 0; ok && index < answers; index += 1)
        {
            unsigned char message[BQ_PHASE_MESSAGE_BYTES] = {0};
            ok = recv(pair[1], message, sizeof(message), 0) == BQ_PHASE_MESSAGE_BYTES;
            bq_phase_put(message + 40, 1u);
            ok = ok && send(pair[1], message, sizeof(message), MSG_NOSIGNAL) == BQ_PHASE_MESSAGE_BYTES;
        }
        close(pair[1]);
        _exit(ok ? 0 : 1);
    }
    if (pair[1] >= 0) close(pair[1]);
    bool ready = child > 0 && bq_phase_init(channel, pair[0], job, token);
    if (!ready && pair[0] >= 0) close(pair[0]);
    if (!ready) *channel = (BqPhaseChannel){.descriptor = -1, .failed = 1};
    return child;
}

BUSTER_GLOBAL_LOCAL void bq_prep_campaign_stage_close(BqPrepCampaignStage* stage)
{
    if (stage->stream) fclose(stage->stream);
    if (stage->spool) fclose(stage->spool);
    if (stage->metrics_stream) fclose(stage->metrics_stream);
    stage->stream = stage->spool = stage->metrics_stream = NULL;
}

BUSTER_GLOBAL_LOCAL void bq_prep_campaign_release(BqPrepCampaign* campaign)
{
    for (u32 stage = 0; stage < 2; stage += 1) bq_prep_campaign_stage_close(&campaign->stages[stage]);
    if (campaign->held.owned) bq_retirement_binaries_release(&campaign->held);
    bq_retirement_campaign_ready_release(&campaign->ready);
    free(campaign->rows);
    free(campaign->facts);
    free(campaign->assigned);
    campaign->rows = NULL;
    campaign->facts = NULL;
    campaign->assigned = NULL;
}

/* The campaign's commands: per stage [slot * 2 + variant], the timed groups
 * (dense order) then the runtime rows. The second A/A label and the A/B
 * candidate name other executables, as the service tests do. */
BUSTER_GLOBAL_LOCAL bool bq_prep_campaign_commands(BqPrepCampaign* campaign)
{
    bool ok = true;
    u32 slots = campaign->groups + campaign->runtime_count;
    for (u32 stage = 0; ok && stage < 2; stage += 1)
        for (u32 slot = 0; ok && slot < slots; slot += 1)
            for (u32 variant = 0; ok && variant < 2; variant += 1)
            {
                u32 index = slot * 2 + variant;
                bool runtime = slot >= campaign->groups;
                bool object = !runtime && campaign->kinds[slot] == TP_RETIREMENT_GROUP_OBJECT;
                u32 row = runtime ? campaign->runtime[slot - campaign->groups] : campaign->group_row[slot];
                char** argv = campaign->arguments[stage][index];
                argv[0] = variant ? (stage ? "/fixture/candidate-ide" : "/fixture/base-ide-second-label") :
                                    "/fixture/base-ide";
                int length = object ? snprintf(campaign->words[stage][index], 32, "@retirement-inputs.rsp") :
                             snprintf(campaign->words[stage][index], 32, runtime ? "runtime-%u" : "g%u-%u.bin",
                                      runtime ? row : slot, variant);
                ok = length > 0 && length < 32;
                argv[1] = campaign->words[stage][index];
                argv[2] = NULL;
                char const* output = runtime ? campaign->rows[row].independent_oracle_sha256 :
                                     object ? campaign->object_output : campaign->batch_output;
                campaign->commands[stage][index] = (TpRetirementMeasuredCommand){.unit = runtime ? row : slot,
                    .kind = runtime, .variant = variant, .argument_count = 2, .arguments = argv,
                    .environment = campaign->environment, .environment_count = 1, .directory = "/tmp",
                    .artifact = runtime || object ? NULL : argv[1], .batch = object ? &campaign->contract : NULL,
                    .timeout_seconds = 2, .command_sha256 = campaign->command_sha[stage][index],
                    .output_sha256 = output};
                ok = ok && tp_retirement_command_hash(&campaign->commands[stage][index],
                                                      campaign->command_sha[stage][index]);
            }
    return ok;
}

/* A sealed correctness gate over the projection's own rows (population hash
 * unchanged): every native-target compiler-eligible object row in one frozen
 * object batch group, every other timed row a singleton, runtime for the
 * singleton rows the reference oracle observed. Row facts are synthetic and
 * the #509 batch authority is the test stand-in. */
BUSTER_GLOBAL_LOCAL bool bq_prep_campaign_gate(BqPrepCampaign* campaign, BqRetirementProjection const* projection)
{
    u32 count = projection->prepared.rows;
    campaign->rows = calloc(count, sizeof(*campaign->rows));
    campaign->facts = calloc(count, sizeof(*campaign->facts));
    campaign->assigned = calloc(count, 1);
    bool ok = campaign->rows && campaign->facts && campaign->assigned;
    if (ok) memcpy(campaign->rows, projection->rows, (size_t)count * sizeof(*campaign->rows));
    campaign->environment[0] = "LC_ALL=C";
    campaign->budget = bq_campaign_service_budget();
    static char const artifact[] = "fixture artifact bytes\n";
    static char const empty[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    bq_digest(artifact, sizeof(artifact) - 1, (char8*)campaign->artifact_sha);
    char const* objects[] = {campaign->artifact_sha};
    ok = ok && tp_retirement_batch_output_digest(objects, 1, campaign->batch_output) &&
        tp_retirement_budget_digest(&campaign->budget, campaign->budget_sha);
    u32 object_group = UINT32_MAX;
    for (u32 row = 0; ok && row < count; row += 1)
    {
        campaign->facts[row].row = row;
        campaign->facts[row].census_row = campaign->rows[row].census_row;
        BqRetirementTrustedRow const* trusted = &campaign->rows[row];
        if (!(trusted->compiler_eligible && trusted->target == projection->prepared.native_target)) continue;
        bool object = trusted->stage == BQ_RETIREMENT_STAGE_OBJECT;
        bool runtime = !object && trusted->independent_oracle_sha256[0];
        ok = campaign->timed < BQ_PREP_CAMPAIGN_CAP && campaign->groups < BQ_PREP_CAMPAIGN_CAP;
        if (!ok) break;
        u32 dense = campaign->timed++;
        campaign->row_ids[dense] = row;
        campaign->row_metrics[dense] = runtime ? TP_RETIREMENT_SAMPLE_RUNTIME : 0;
        campaign->facts[row].compiler_eligible = 1;
        campaign->facts[row].runtime_eligible = runtime;
        if (runtime) campaign->runtime[campaign->runtime_count++] = row;
        if (object)
        {
            u32 input = campaign->objects++;
            snprintf(campaign->fixtures[input], 32, "tests/r%u.c", row);
            snprintf(campaign->leaves[input], 32, "r%u.o", row);
            campaign->inputs[input] = (TpRetirementBatchInput){campaign->fixtures[input], "ok", "driver.none", empty,
                campaign->artifact_sha, campaign->leaves[input], 1, row};
            if (object_group == UINT32_MAX)
            {
                object_group = campaign->groups++;
                campaign->kinds[object_group] = TP_RETIREMENT_GROUP_OBJECT;
                campaign->group_row[object_group] = row;
                campaign->group_stages[object_group] = TP_RETIREMENT_BUDGET_STAGE_OBJECT;
            }
        }
        else
        {
            u32 group = campaign->groups++;
            campaign->kinds[group] = TP_RETIREMENT_GROUP_SINGLETON;
            campaign->group_row[group] = row;
            campaign->group_stages[group] = trusted->stage == BQ_RETIREMENT_STAGE_LINK ?
                TP_RETIREMENT_BUDGET_STAGE_LINK : TP_RETIREMENT_BUDGET_STAGE_SELF_HOST;
        }
    }
    /* Dense groups in order of their smallest member; members as dense rows. */
    u32 used = 0;
    for (u32 group = 0; ok && group < campaign->groups; group += 1)
    {
        campaign->offsets[group] = used;
        for (u32 dense = 0; dense < campaign->timed; dense += 1)
        {
            BqRetirementTrustedRow const* trusted = &campaign->rows[campaign->row_ids[dense]];
            bool member = campaign->kinds[group] == TP_RETIREMENT_GROUP_OBJECT ?
                trusted->stage == BQ_RETIREMENT_STAGE_OBJECT : campaign->row_ids[dense] == campaign->group_row[group];
            if (member) campaign->members[used++] = dense;
        }
    }
    campaign->offsets[campaign->groups] = used;
    ok = ok && used == campaign->timed && campaign->groups && campaign->runtime_count;
    campaign->contract = (TpRetirementBatchContract){"x86_64-linux", "none", "batch.metrics", campaign->inputs,
        campaign->objects, 0, 0};
    ok = ok && (!campaign->objects ||
        (tp_retirement_budget_metrics_bytes(&campaign->budget, campaign->objects, &campaign->contract.metrics_bytes_max) &&
         tp_retirement_batch_contract_output(&campaign->contract, campaign->object_output))) &&
        bq_prep_campaign_commands(campaign);
    /* Row facts and trusted commands are the A/B stage's frozen commands. */
    for (u32 group = 0; ok && group < campaign->groups; group += 1)
        for (u32 dense = campaign->offsets[group]; dense < campaign->offsets[group + 1]; dense += 1)
        {
            u32 row = campaign->row_ids[campaign->members[dense]];
            BqRetirementTrustedRow* trusted = &campaign->rows[row];
            memset(trusted->batch_key_sha256, 'c', 64);
            if (campaign->kinds[group] != TP_RETIREMENT_GROUP_OBJECT) trusted->batch_key_sha256[0] = 0;
            for (u32 side = 0; side < 2; side += 1)
            {
                BqRetirementObservedSide* observed = &campaign->facts[row].side[side];
                memcpy(trusted->compiler_command_sha256[side], campaign->command_sha[1][group * 2 + side], 65);
                memcpy(observed->compiler_command_sha256, campaign->command_sha[1][group * 2 + side], 65);
                memcpy(observed->artifact_sha256, campaign->artifact_sha, 65);
            }
        }
    for (u32 runtime = 0; ok && runtime < campaign->runtime_count; runtime += 1)
        for (u32 side = 0; side < 2; side += 1)
            memcpy(campaign->facts[campaign->runtime[runtime]].side[side].runtime_command_sha256,
                   campaign->command_sha[1][(campaign->groups + runtime) * 2 + side], 65);
    BqRetirementCorrectness* gate = &campaign->gate;
    gate->prepared = projection->prepared;
    gate->trusted_rows = campaign->rows;
    gate->facts = campaign->facts;
    gate->rows_done = count;
    gate->eligible_rows = campaign->timed;
    if (ok && campaign->objects)
    {
        campaign->group = (BqRetirementBatchGroup){{campaign->contract, campaign->contract}, {{0}}};
        memcpy(campaign->group.command_sha256[0], campaign->command_sha[1][object_group * 2], 65);
        memcpy(campaign->group.command_sha256[1], campaign->command_sha[1][object_group * 2 + 1], 65);
        ok = bq_retirement_correctness_batches(gate, &campaign->group, 1, campaign->assigned, count);
        gate->batch_authority = 1;
    }
    gate->finished = 1;
    /* The v3 A/A second-command aggregate, one entry per timed group. */
    Sha256 hash;
    sha256_init(&hash);
    static char const domain[] = BQ_RETIREMENT_AA_SECOND_COMMANDS_DOMAIN;
    sha256_add(&hash, domain, sizeof(domain) - 1);
    u32 runtime_slot = 0;
    for (u32 group = 0; ok && group < campaign->groups; group += 1)
    {
        u32 row = campaign->group_row[group];
        uint8_t ordinal[4] = {(uint8_t)row, (uint8_t)(row >> 8), (uint8_t)(row >> 16), (uint8_t)(row >> 24)};
        uint8_t runtime = campaign->kinds[group] == TP_RETIREMENT_GROUP_SINGLETON &&
            campaign->facts[row].runtime_eligible ? 1 : 0;
        sha256_add(&hash, ordinal, sizeof(ordinal));
        sha256_add(&hash, campaign->command_sha[0][group * 2 + 1], 64);
        sha256_add(&hash, &runtime, sizeof(runtime));
        if (runtime) sha256_add(&hash, campaign->command_sha[0][(campaign->groups + runtime_slot++) * 2 + 1], 64);
    }
    sha256_finish_hex(&hash, gate->prepared.aa_second_commands_sha256);
    bq_retirement_correctness_seal(gate, gate->sealed_sha256);
    campaign->review = (TpRetirementCampaignReview){&campaign->budget, campaign->group_stages, NULL, NULL, NULL,
        campaign->groups, 0};
    return ok && bq_retirement_correctness_ready(gate);
}

/* Fresh A/A and A/B stages for job/token, bound now. */
BUSTER_GLOBAL_LOCAL bool bq_prep_campaign_stages(BqPrepCampaign* campaign, u64 job, u64 token, u64 seed)
{
    char label[129];
    bool ok = bq_retirement_campaign_job_label(label, job);
    u64 bound = tp_process_monotonic_ns();
    TpRetirementLayout layout = {campaign->timed, campaign->groups, campaign->row_ids, campaign->row_metrics,
        campaign->kinds, campaign->offsets, campaign->members};
    for (u32 index = 0; ok && index < 2; index += 1)
    {
        BqPrepCampaignStage* stage = &campaign->stages[index];
        bq_prep_campaign_stage_close(stage);
        *stage = (BqPrepCampaignStage){0};
        stage->stream = tmpfile();
        stage->spool = tmpfile();
        stage->metrics_stream = tmpfile();
        ok = stage->stream && stage->spool && stage->metrics_stream &&
            tp_retirement_execution_init(&stage->execution, seed, campaign->groups, campaign->runtime,
                campaign->runtime_count, campaign->gate.prepared.rows, 60, stage->workspace,
                (size_t)campaign->groups * 3 + campaign->runtime_count) &&
            tp_retirement_transcript_init(&stage->transcript, &stage->execution, label, token, "boot-fixture", 0, bound) &&
            tp_retirement_transcript_begin_shard(&stage->transcript, stage->stream) &&
            tp_retirement_samples_init(&stage->samples, &stage->transcript, stage->spool, &layout, stage->rows,
                stage->groups, stage->members) &&
            tp_retirement_metrics_shards_init(&stage->metrics, index ? "ab" : "aa", stage->metrics_stream) &&
            (!campaign->objects || tp_retirement_samples_attach_metrics(&stage->samples, &stage->metrics));
    }
    campaign->campaign = (TpRetirementCampaign){0};
    campaign->binding = (BqRetirementCampaignBinding){0};
    return ok;
}

BUSTER_GLOBAL_LOCAL BqRetirementCampaignRequest bq_prep_campaign_request(BqPrepCampaign* campaign)
{
    size_t slots = (size_t)campaign->groups + campaign->runtime_count;
    BqRetirementCampaignRequest request = {&campaign->gate, &campaign->campaign, &campaign->plan,
        &campaign->stages[0].samples, &campaign->stages[1].samples, campaign->commands[0], campaign->commands[1],
        campaign->snapshots, slots * 4, campaign->identities, slots, &campaign->review, campaign->plan_sha,
        campaign->context_sha};
    return request;
}

/* A refused import leaves nothing held and no record. */
BUSTER_GLOBAL_LOCAL bool bq_prep_campaign_unheld(BqPrepCampaign const* campaign)
{
    bool ok = !campaign->held.owned && campaign->held.descriptors[0] < 0 && campaign->held.descriptors[1] < 0 &&
        !campaign->ready.owned && !campaign->ready.text;
    return ok;
}

/* A refused bind leaves no binding and both stages poisoned; the imported
 * held pair and record stay the caller's. */
BUSTER_GLOBAL_LOCAL bool bq_prep_campaign_refused(BqPrepCampaign const* campaign)
{
    bool ok = campaign->held.owned && campaign->ready.owned && campaign->ready.text &&
        !campaign->binding.campaign && !campaign->binding.held_binaries && !campaign->binding.unit_ready_sha256[0] &&
        campaign->campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && campaign->stages[0].samples.failed &&
        campaign->stages[1].samples.failed && !campaign->stages[0].execution.sequence;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_prep_campaign_drop(BqPrepCampaign* campaign)
{
    if (campaign->held.owned) bq_retirement_binaries_release(&campaign->held);
    bq_retirement_campaign_ready_release(&campaign->ready);
    campaign->held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
}

typedef struct BqPrepCampaignAttempt
{
    BqPrepOracleFixture* fixture;
    BqPrepOracleAttempt* attempt;
    BqRetirementCampaignUnitStore unit;
    char profile[5120];
    char ready_sha256[SHA256_HEX_CAPACITY];
    u64 import_ns;
} BqPrepCampaignAttempt;

/* A channel for this attempt (or another job or token) whose first `steps`
 * phases are acknowledged; the peer answers exactly `steps` messages. */
BUSTER_GLOBAL_LOCAL pid_t bq_prep_campaign_channel(BqPhaseChannel* phases, u64 job, u64 token, u32 steps)
{
    pid_t peer = bq_prep_campaign_peer(phases, job, token, steps);
    bool exchanged = peer > 0;
    for (u32 phase = 1; exchanged && phase <= steps; phase += 1) exchanged = bq_phase_exchange(phases, phase);
    BQ_PREP_CHECK(exchanged);
    return peer;
}

BUSTER_GLOBAL_LOCAL BqError bq_prep_campaign_import(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign,
    BqPhaseChannel const* phases, int cancel, u64 deadline)
{
    BqPrepUnitAttempt const* attempt = &context->attempt->attempt;
    BqError result = bq_retirement_campaign_service_import_unit_pinned(&context->unit, phases, cancel, deadline,
        attempt->job.id, attempt->job.token, attempt->digest, context->ready_sha256, &campaign->gate, &campaign->held,
        &campaign->ready);
    return result;
}

/* The import under SETTLING: the phase and channel states it refuses before
 * it reads anything, then the import itself and its refusals. */
BUSTER_GLOBAL_LOCAL void bq_prep_campaign_imports(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign,
    int cancel[2])
{
    BqPrepUnitAttempt const* attempt = &context->attempt->attempt;
    u64 job = attempt->job.id, token = attempt->job.token, generous = bq_phase_clock() + 300000000000ull;
    for (u32 answers = 0; answers < 6; answers += 1)
    {
        BqPhaseChannel phases;
        u64 channel_job = answers == 4 ? job + 1 : job, channel_token = answers == 5 ? token + 1 : token;
        pid_t peer = bq_prep_campaign_channel(&phases, channel_job, channel_token, answers < 4 ? answers : 2);
        u64 started = bq_phase_clock();
        BqError result = bq_prep_campaign_import(context, campaign, &phases, cancel[0], generous);
        /* Only the SETTLING acknowledgement of this job and attempt imports. */
        if (answers == 2)
        {
            context->import_ns = bq_phase_clock() - started;
            BQ_PREP_CHECK(result == BQ_OK && campaign->held.owned && campaign->ready.owned &&
                          !strcmp(campaign->ready.ready_sha256, context->ready_sha256) &&
                          campaign->ready.job.id == job && campaign->ready.job.token == token &&
                          bq_retirement_campaign_ready_holds(&campaign->ready, &campaign->held));
            /* A live holder is never overwritten or released. */
            BQ_PREP_CHECK(bq_prep_campaign_import(context, campaign, &phases, cancel[0], generous) ==
                          BQ_INVALID_TRANSITION && campaign->held.owned && campaign->ready.owned &&
                          bq_retirement_campaign_ready_holds(&campaign->ready, &campaign->held));
        }
        else BQ_PREP_CHECK(result == BQ_INVALID_TRANSITION && bq_prep_campaign_unheld(campaign));
        bq_prep_campaign_drop(campaign);
        BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    }
    /* A readable cancellation pipe, an expired deadline, and a deadline that
     * passes while the import runs (the SETTLING recheck after it). */
    for (u32 trial = 0; trial < 3; trial += 1)
    {
        BqPhaseChannel phases;
        pid_t peer = bq_prep_campaign_channel(&phases, job, token, 2);
        char byte = 1;
        if (!trial) BQ_PREP_CHECK(write(cancel[1], &byte, 1) == 1);
        u64 span = context->import_ns / 8u ? context->import_ns / 8u : 1u;
        u64 deadline = trial == 1 ? bq_phase_clock() : trial == 2 ? bq_phase_clock() + span : generous;
        BQ_PREP_CHECK(bq_prep_campaign_import(context, campaign, &phases, cancel[0], deadline) ==
                      BQ_INVALID_TRANSITION && bq_prep_campaign_unheld(campaign));
        char drained[8];
        if (!trial) BQ_PREP_CHECK(read(cancel[0], drained, sizeof(drained)) == 1);
        BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    }
    /* A gate sealed over other source manifests; the compiled blocked
     * profile of the production wrapper. */
    for (u32 trial = 0; trial < 2; trial += 1)
    {
        BqPhaseChannel phases;
        pid_t peer = bq_prep_campaign_channel(&phases, job, token, 2);
        char* source = campaign->gate.prepared.source_sha256[1];
        char saved = source[0];
        BqError result = BQ_OK;
        if (!trial)
        {
            source[0] = saved == '0' ? '1' : '0';
            bq_retirement_correctness_seal(&campaign->gate, campaign->gate.sealed_sha256);
            result = bq_prep_campaign_import(context, campaign, &phases, cancel[0], generous);
            source[0] = saved;
            bq_retirement_correctness_seal(&campaign->gate, campaign->gate.sealed_sha256);
            BQ_PREP_CHECK(result == BQ_SOURCE_MISMATCH);
        }
        else
        {
            result = bq_retirement_campaign_service_import_unit(attempt->store, context->fixture->workspaces_fd,
                context->fixture->installed_fd, &phases, cancel[0], generous, job, token, attempt->digest,
                context->ready_sha256, &campaign->gate, &campaign->held, &campaign->ready);
            BQ_PREP_CHECK(result != BQ_OK);
        }
        BQ_PREP_CHECK(bq_prep_campaign_unheld(campaign) && bq_retirement_correctness_ready(&campaign->gate));
        bq_prep_campaign_drop(campaign);
        BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    }
}

/* The ready record's import, its refusals and the gate join. */
BUSTER_GLOBAL_LOCAL void bq_prep_campaign_ready_cases(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign,
    BqRetirementUnitOracle const* oracle, BqRetirementUnitGate const* admitted)
{
    BqPrepOracleAttempt* success = context->attempt;
    BqRetirementUnitPrepared const* unit = &success->attempt.unit;
    int attempt = success->attempt.attempt;
    BqRetirementCampaignReady ready = {0};
    BQ_PREP_CHECK(bq_retirement_campaign_ready_import(&context->unit, unit, context->ready_sha256, &ready) == BQ_OK &&
                  ready.owned && ready.job_id == unit->job.id && ready.attempt_token == unit->job.token &&
                  ready.rows == BQ_PREP_ORACLE_POPULATION && ready.object_rows == BQ_PREP_ORACLE_ROWS &&
                  ready.native_target == BQ_RETIREMENT_NATIVE_TIMED_TARGET &&
                  ready.observed_rows == BQ_PREP_ORACLE_REFERENCES &&
                  !strcmp(ready.binary_record_sha256, success->built.binary_record_sha256) &&
                  !strcmp(ready.build_record_sha256, success->built.build_record_sha256) &&
                  !strcmp(ready.binary_sha256[0], success->built.binaries.verified.binary_sha256[0]) &&
                  !strcmp(ready.binary_sha256[1], success->built.binaries.verified.binary_sha256[1]) &&
                  !strcmp(ready.gate_sha256, admitted->seal_sha256) &&
                  !strcmp(ready.template_sha256, unit->policy.template_sha256) &&
                  !strcmp(ready.inventory_sha256, unit->policy.inventory_sha256) &&
                  !strcmp(ready.population_sha256, success->projection.population_sha256) &&
                  !strcmp(ready.oracle_attempt_sha256, oracle->attempt_sha256) &&
                  !strcmp(ready.preparation_sha256, success->attempt.digest));
    /* A second import into a live holder, an unknown digest, another token's
     * attempt and a malformed digest are refused. */
    BQ_PREP_CHECK(bq_retirement_campaign_ready_import(&context->unit, unit, context->ready_sha256, &ready) ==
                  BQ_BAD_REQUEST && ready.owned);
    BqRetirementCampaignReady refused = {0};
    char wrong[SHA256_HEX_CAPACITY];
    memcpy(wrong, context->ready_sha256, sizeof(wrong));
    wrong[0] = wrong[0] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_retirement_campaign_ready_import(&context->unit, unit, wrong, &refused) == BQ_WORKSPACE_MISMATCH &&
                  !refused.owned && !refused.text);
    BqRetirementUnitPrepared other = *unit;
    other.job.token += 1;
    BQ_PREP_CHECK(bq_retirement_campaign_ready_import(&context->unit, &other, context->ready_sha256, &refused) != BQ_OK &&
                  !refused.owned);
    other = *unit;
    other.job.id += 1;
    BQ_PREP_CHECK(bq_retirement_campaign_ready_import(&context->unit, &other, context->ready_sha256, &refused) != BQ_OK &&
                  !refused.owned);
    char malformed[SHA256_HEX_CAPACITY];
    memset(malformed, 'Z', 64);
    malformed[64] = 0;
    BQ_PREP_CHECK(bq_retirement_campaign_ready_import(&context->unit, unit, malformed, &refused) == BQ_BAD_REQUEST);
    /* The replay authenticates the record against the store; the template
     * and inventory it names must also be this attempt's imported policy. */
    for (u32 field = 0; field < 2; field += 1)
    {
        other = *unit;
        char* digest = field ? other.policy.inventory_sha256 : other.policy.template_sha256;
        digest[0] = digest[0] == '0' ? '1' : '0';
        BQ_PREP_CHECK(bq_retirement_campaign_ready_import(&context->unit, &other, context->ready_sha256, &refused) ==
                      BQ_CORRUPT && !refused.owned && !refused.text);
    }

    /* Mutated fields, re-addressed so only a re-derivation can catch them:
     * another job or token, a build or binary record digest, the gate seal,
     * the template, the inventory, a reference row's descriptor numbers or
     * command digest. The original record's address then names nothing. */
    char original[BQ_PREP_READY_CAP], bytes[BQ_PREP_READY_CAP];
    char name[80], digest[SHA256_HEX_CAPACITY];
    snprintf(name, sizeof(name), "ready-%s", context->ready_sha256);
    int directory = openat(attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    u32 length = directory >= 0 ? bq_prep_test_read_at(directory, name, original, sizeof(original)) : 0;
    if (directory >= 0) close(directory);
    BQ_PREP_CHECK(length > 0);
    static BqPrepReadyTamper const tampers[] = {{"job=", 0, BQ_CORRUPT}, {"attempt=", 0, BQ_CORRUPT},
        {"binaries=", 0, BQ_CORRUPT}, {"matched-builds=", 0, BQ_CORRUPT}, {"binary-candidate=", 0, BQ_CORRUPT},
        {"template=", 0, BQ_CORRUPT}, {"inventory=", 0, BQ_CORRUPT}, {"gate=", 1, BQ_RECIPE_MISMATCH},
        {"observed=", 9, BQ_SOURCE_MISMATCH}, {NULL, 4, BQ_SOURCE_MISMATCH}, {NULL, 5, BQ_SOURCE_MISMATCH}};
    for (u32 index = 0; length && index < BUSTER_ARRAY_LENGTH(tampers); index += 1)
    {
        memcpy(bytes, original, length + 1u);
        u32 changed = length;
        bool tampered = tampers[index].key ? bq_prep_test_ready_tamper(bytes, tampers[index].key, tampers[index].field) :
            bq_prep_test_ready_set(bytes, &changed, "observed=", tampers[index].field,
                oracle->descriptors[tampers[index].field - 4u] +
                (oracle->descriptors[tampers[index].field - 4u] + 1 == oracle->descriptors[5u - tampers[index].field] ? 2 : 1));
        memcpy(digest, context->ready_sha256, sizeof(digest));
        BQ_PREP_CHECK(tampered && bq_prep_test_ready_install(attempt, digest, bytes, changed));
        BqError result = bq_retirement_campaign_ready_import(&context->unit, unit, digest, &refused);
        if (result != tampers[index].expected)
            fprintf(stderr, "RETIREMENT_PREP campaign ready tamper %u returned %d\n", index, (int)result);
        BQ_PREP_CHECK(result == tampers[index].expected && !refused.owned);
        BQ_PREP_CHECK(bq_retirement_campaign_ready_import(&context->unit, unit, context->ready_sha256, &refused) ==
                      BQ_WORKSPACE_MISMATCH && !refused.owned);
        BQ_PREP_CHECK(bq_prep_test_ready_install(attempt, digest, original, length) &&
                      !strcmp(digest, context->ready_sha256));
    }

    /* The gate join: the projection's gate joins; a changed A, candidate
     * binary, support or census digest, row population, reference oracle
     * output or an extra oracle row does not. */
    BqRetirementCorrectness* gate = &campaign->gate;
    BQ_PREP_CHECK(bq_retirement_campaign_ready_gate(&ready, gate) == BQ_OK);
    char* fields[] = {gate->prepared.preparation_sha256, gate->prepared.binary_sha256[1],
                      gate->prepared.support_sha256, gate->prepared.census_sha256,
                      campaign->rows[7].configuration_sha256,
                      campaign->rows[campaign->runtime[0]].independent_oracle_sha256,
                      campaign->rows[5].independent_oracle_sha256};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fields); index += 1)
    {
        char saved[SHA256_HEX_CAPACITY];
        memcpy(saved, fields[index], sizeof(saved));
        if (fields[index][0]) fields[index][0] = fields[index][0] == '0' ? '1' : '0';
        else memset(fields[index], 'a', 64);
        bq_retirement_correctness_seal(gate, gate->sealed_sha256);
        BQ_PREP_CHECK(bq_retirement_correctness_ready(gate) &&
                      bq_retirement_campaign_ready_gate(&ready, gate) == BQ_SOURCE_MISMATCH);
        memcpy(fields[index], saved, sizeof(saved));
        bq_retirement_correctness_seal(gate, gate->sealed_sha256);
    }
    BQ_PREP_CHECK(bq_retirement_correctness_ready(gate) && bq_retirement_campaign_ready_gate(&ready, gate) == BQ_OK);
    /* An unready (resealed-after-change) gate never joins. */
    campaign->rows[7].census_row += 1;
    BQ_PREP_CHECK(!bq_retirement_correctness_ready(gate) &&
                  bq_retirement_campaign_ready_gate(&ready, gate) == BQ_SOURCE_MISMATCH);
    campaign->rows[7].census_row -= 1;

    /* The held pair comes back from the record's digests and matches it. */
    BqRetirementHeldBinaries held = {.descriptors = {-1, -1}};
    BQ_PREP_CHECK(bq_retirement_campaign_ready_held(&context->unit, unit, &ready, &held) == BQ_OK && held.owned &&
                  held.descriptors[0] >= 3 && held.descriptors[1] >= 3 && held.descriptors[0] != held.descriptors[1] &&
                  !strcmp(held.verified.binary_sha256[0], ready.binary_sha256[0]) &&
                  !strcmp(held.verified.binary_sha256[1], ready.binary_sha256[1]));
    BQ_PREP_CHECK(bq_retirement_campaign_ready_held(&context->unit, unit, &ready, &held) == BQ_BAD_REQUEST && held.owned);
    bq_retirement_binaries_release(&held);
    held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
    BqRetirementCampaignReady forged = ready;
    forged.binary_record_sha256[0] = forged.binary_record_sha256[0] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_retirement_campaign_ready_held(&context->unit, unit, &forged, &held) != BQ_OK && !held.owned);
    /* The record's build is authentic, but a binary or A digest it names is
     * not the one that build holds. */
    for (u32 field = 0; field < 3; field += 1)
    {
        forged = ready;
        char* digest = field < 2 ? forged.binary_sha256[field] : forged.preparation_sha256;
        digest[0] = digest[0] == '0' ? '1' : '0';
        BQ_PREP_CHECK(bq_retirement_campaign_ready_held(&context->unit, unit, &forged, &held) == BQ_SOURCE_MISMATCH &&
                      !held.owned && held.descriptors[0] < 0 && held.descriptors[1] < 0);
    }
    bq_retirement_campaign_ready_release(&ready);
}

/* Lane E's per-timed-row layout from the pinned performance rows: every
 * timed row in ascending order with its campaign group, runtime flag and
 * dimension values; a short workspace, a gate row whose identity is not the
 * declared one, and a profile without the performance-row pin are refused. */
BUSTER_GLOBAL_LOCAL void bq_prep_campaign_timed_rows(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign)
{
    TpRetirementTimedRow* rows = calloc(BQ_PREP_CAMPAIGN_CAP, sizeof(*rows));
    unsigned count = 0;
    BQ_PREP_CHECK(rows && bq_retirement_campaign_service_timed_rows(&context->unit, &campaign->gate, rows,
                  BQ_PREP_CAMPAIGN_CAP, &count) == BQ_OK && count == campaign->timed);
    for (u32 dense = 0; rows && dense < count && dense < campaign->timed; dense += 1)
    {
        u32 row = campaign->row_ids[dense], group = UINT32_MAX;
        for (u32 g = 0; g < campaign->groups; g += 1)
            for (u32 k = campaign->offsets[g]; k < campaign->offsets[g + 1]; k += 1)
                if (campaign->members[k] == dense) group = g;
        u32 stage = campaign->rows[row].stage;
        char const* stage_name = stage == BQ_RETIREMENT_STAGE_OBJECT ? "object" :
            stage == BQ_RETIREMENT_STAGE_LINK ? "link" : "self-host-stage1";
        BQ_PREP_CHECK(rows[dense].id == row && rows[dense].group == group &&
                      rows[dense].runtime == (campaign->facts[row].runtime_eligible ? 1u : 0u) &&
                      !strcmp(rows[dense].dimensions[5], stage_name) && !strcmp(rows[dense].dimensions[0],
                      rows[0].dimensions[0]));
        for (u32 dimension = 0; dimension < TP_RETIREMENT_TIMED_DIMENSIONS; dimension += 1)
            BQ_PREP_CHECK(rows[dense].dimensions[dimension][0] != 0);
    }
    BQ_PREP_CHECK(rows && bq_retirement_campaign_service_timed_rows(&context->unit, &campaign->gate, rows, count - 1u,
                  &count) == BQ_SOURCE_MISMATCH && !count);
    char* identity = campaign->rows[campaign->row_ids[0]].identity_sha256;
    char saved = identity[0];
    identity[0] = saved == '0' ? '1' : '0';
    bq_retirement_correctness_seal(&campaign->gate, campaign->gate.sealed_sha256);
    BQ_PREP_CHECK(rows && bq_retirement_correctness_ready(&campaign->gate) &&
                  bq_retirement_campaign_service_timed_rows(&context->unit, &campaign->gate, rows, BQ_PREP_CAMPAIGN_CAP,
                  &count) == BQ_SOURCE_MISMATCH && !count);
    identity[0] = saved;
    bq_retirement_correctness_seal(&campaign->gate, campaign->gate.sealed_sha256);
    char unpinned[5120];
    memcpy(unpinned, context->profile, sizeof(unpinned));
    char* line = strstr(unpinned, "performance-rows-sha256=");
    char* end = line ? strchr(line, '\n') : NULL;
    if (end) memmove(line, end + 1, strlen(end + 1) + 1);
    BqRetirementCampaignUnitStore unit = context->unit;
    unit.profile = string_from_pointer(unpinned);
    BQ_PREP_CHECK(end && rows && bq_retirement_campaign_service_timed_rows(&unit, &campaign->gate, rows,
                  BQ_PREP_CAMPAIGN_CAP, &count) == BQ_SOURCE_MISMATCH && !count);
    BQ_PREP_CHECK(rows && bq_retirement_campaign_service_timed_rows(&context->unit, &campaign->gate, rows,
                  BQ_PREP_CAMPAIGN_CAP, &count) == BQ_OK && count == campaign->timed);
    free(rows);
}

/* The bind under MEASURING on an imported pair: the phase and channel
 * states it refuses; each refusal keeps the caller's pair. */
BUSTER_GLOBAL_LOCAL void bq_prep_campaign_bind_phases(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign,
    int cancel[2])
{
    BqPrepUnitAttempt const* attempt = &context->attempt->attempt;
    u64 job = attempt->job.id, token = attempt->job.token, generous = bq_phase_clock() + 300000000000ull;
    TpRetirementShard untimed = {1, 1, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    for (u32 trial = 0; trial < 8; trial += 1)
    {
        /* 0..3 acknowledged phases; MEASURING of another job or token; a
         * readable cancellation pipe; an expired deadline. */
        BqPhaseChannel phases;
        u64 channel_job = trial == 4 ? job + 1 : job, channel_token = trial == 5 ? token + 1 : token;
        pid_t peer = bq_prep_campaign_channel(&phases, channel_job, channel_token, trial < 4 ? trial : 3);
        char byte = 1;
        if (trial == 6) BQ_PREP_CHECK(write(cancel[1], &byte, 1) == 1);
        BQ_PREP_CHECK(bq_prep_campaign_stages(campaign, job, token, 7));
        BqRetirementCampaignRequest request = bq_prep_campaign_request(campaign);
        BqError result = bq_retirement_campaign_service_bind_unit_pinned(&context->unit, &phases, cancel[0],
            trial == 7 ? bq_phase_clock() : generous, job, token, &untimed, &request, &campaign->held,
            &campaign->ready, &campaign->binding);
        /* The MEASURING acknowledgement of this job and attempt reaches the
         * plan derivation, which this record stream does not match. */
        BQ_PREP_CHECK(trial == 3 ? result == BQ_RECIPE_MISMATCH : result == BQ_INVALID_TRANSITION);
        BQ_PREP_CHECK(bq_prep_campaign_refused(campaign));
        char drained[8];
        if (trial == 6) BQ_PREP_CHECK(read(cancel[0], drained, sizeof(drained)) == 1);
        BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    }
}

enum
{
    BQ_PREP_CAMPAIGN_BIND, BQ_PREP_CAMPAIGN_LATE, BQ_PREP_CAMPAIGN_READY_JOB, BQ_PREP_CAMPAIGN_READY_TOKEN,
    BQ_PREP_CAMPAIGN_MOVED, BQ_PREP_CAMPAIGN_SWAPPED, BQ_PREP_CAMPAIGN_HELD_DIGEST, BQ_PREP_CAMPAIGN_HELD_PREPARATION,
    BQ_PREP_CAMPAIGN_OTHER_GATE, BQ_PREP_CAMPAIGN_BLOCKED_PROFILE, BQ_PREP_CAMPAIGN_UNPINNED,
    BQ_PREP_CAMPAIGN_PLAN_OVERRIDE, BQ_PREP_CAMPAIGN_CONTEXT_OVERRIDE, BQ_PREP_CAMPAIGN_UNTIMED_OVERRIDE,
    BQ_PREP_CAMPAIGN_MODES
};

/* One store-based bind under a MEASURING acknowledgement, on the imported
 * pair or a changed copy of it. */
BUSTER_GLOBAL_LOCAL BqError bq_prep_campaign_bind(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign,
    u32 mode, int cancel)
{
    BqPrepUnitAttempt const* attempt = &context->attempt->attempt;
    u64 job = attempt->job.id, token = attempt->job.token;
    BqPhaseChannel phases;
    pid_t peer = bq_prep_campaign_peer(&phases, job, token, 3);
    bool exchanged = peer > 0;
    for (u32 phase = 1; exchanged && phase <= 2; phase += 1) exchanged = bq_phase_exchange(&phases, phase);
    BQ_PREP_CHECK(exchanged);
    /* A pre-sample binding made before the MEASURING acknowledgement. */
    bool staged = mode == BQ_PREP_CAMPAIGN_LATE && bq_prep_campaign_stages(campaign, job, token, 7);
    BQ_PREP_CHECK(bq_phase_exchange(&phases, BQ_PHASE_MEASURING));
    if (mode != BQ_PREP_CAMPAIGN_LATE) staged = bq_prep_campaign_stages(campaign, job, token, 7);
    TpRetirementShard untimed = {640, 4, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    BqRetirementUnitCampaignPins pins = {0};
    BQ_PREP_CHECK(staged && bq_retirement_unit_campaign_pins(string_from_pointer(context->profile), &pins) &&
        bq_retirement_unit_campaign_derive(&campaign->gate, &pins, context->ready_sha256, &untimed,
            &campaign->stages[0].samples, &campaign->stages[1].samples, campaign->commands[0], campaign->commands[1],
            2 * ((size_t)campaign->groups + campaign->runtime_count), &campaign->plan, campaign->plan_sha,
            campaign->context_sha));
    if (mode == BQ_PREP_CAMPAIGN_PLAN_OVERRIDE) campaign->plan.cell_members_per_scope += 1;
    if (mode == BQ_PREP_CAMPAIGN_CONTEXT_OVERRIDE) campaign->context_sha[0] = campaign->context_sha[0] == '0' ? '1' : '0';
    if (mode == BQ_PREP_CAMPAIGN_UNTIMED_OVERRIDE) untimed.records += 1;
    BqRetirementCampaignReady ready = campaign->ready;
    BqRetirementHeldBinaries held = campaign->held;
    if (mode == BQ_PREP_CAMPAIGN_READY_JOB) ready.job_id += 1;
    if (mode == BQ_PREP_CAMPAIGN_READY_TOKEN) ready.attempt_token += 1;
    /* The record's stored bytes differ from the imported text. */
    char* moved = mode == BQ_PREP_CAMPAIGN_MOVED ? malloc((size_t)ready.length + 1u) : NULL;
    if (moved)
    {
        memcpy(moved, ready.text, (size_t)ready.length + 1u);
        moved[0] = moved[0] == 'B' ? 'C' : 'B';
        ready.text = moved;
    }
    if (mode == BQ_PREP_CAMPAIGN_SWAPPED)
    {
        held.descriptors[0] = campaign->held.descriptors[1];
        held.descriptors[1] = campaign->held.descriptors[0];
    }
    if (mode == BQ_PREP_CAMPAIGN_HELD_DIGEST)
        held.verified.binary_sha256[1][0] = held.verified.binary_sha256[1][0] == '0' ? '1' : '0';
    if (mode == BQ_PREP_CAMPAIGN_HELD_PREPARATION)
        held.verified.preparation_sha256[0] = held.verified.preparation_sha256[0] == '0' ? '1' : '0';
    char unpinned[5120];
    BqRetirementCampaignUnitStore unit = context->unit;
    if (mode == BQ_PREP_CAMPAIGN_UNPINNED)
    {
        memcpy(unpinned, context->profile, sizeof(unpinned));
        char* line = strstr(unpinned, "campaign-pairs=");
        char* end = line ? strchr(line, '\n') : NULL;
        if (end) memmove(line, end + 1, strlen(end + 1) + 1);
        unit.profile = string_from_pointer(unpinned);
    }
    char saved = campaign->gate.prepared.census_sha256[0];
    if (mode == BQ_PREP_CAMPAIGN_OTHER_GATE)
    {
        campaign->gate.prepared.census_sha256[0] = saved == '0' ? '1' : '0';
        bq_retirement_correctness_seal(&campaign->gate, campaign->gate.sealed_sha256);
    }
    /* The binding keeps the held pair it binds: only a refused copy is local. */
    bool changed_held = mode == BQ_PREP_CAMPAIGN_SWAPPED || mode == BQ_PREP_CAMPAIGN_HELD_DIGEST ||
        mode == BQ_PREP_CAMPAIGN_HELD_PREPARATION;
    bool changed_ready = mode == BQ_PREP_CAMPAIGN_READY_JOB || mode == BQ_PREP_CAMPAIGN_READY_TOKEN ||
        mode == BQ_PREP_CAMPAIGN_MOVED;
    BqRetirementHeldBinaries const* bound_held = changed_held ? &held : &campaign->held;
    BqRetirementCampaignReady const* bound_ready = changed_ready ? &ready : &campaign->ready;
    BqRetirementCampaignRequest request = bq_prep_campaign_request(campaign);
    u64 deadline = bq_phase_clock() + 300000000000ull;
    BqError result = mode == BQ_PREP_CAMPAIGN_BLOCKED_PROFILE ?
        bq_retirement_campaign_service_bind_unit(attempt->store, context->fixture->workspaces_fd,
            context->fixture->installed_fd, &phases, cancel, deadline, job, token, &untimed, &request, bound_held,
            bound_ready, &campaign->binding) :
        bq_retirement_campaign_service_bind_unit_pinned(&unit, &phases, cancel, deadline, job, token, &untimed,
            &request, bound_held, bound_ready, &campaign->binding);
    if (mode == BQ_PREP_CAMPAIGN_OTHER_GATE)
    {
        campaign->gate.prepared.census_sha256[0] = saved;
        bq_retirement_correctness_seal(&campaign->gate, campaign->gate.sealed_sha256);
    }
    free(moved);
    BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    return result;
}

BUSTER_GLOBAL_LOCAL void bq_prep_campaign_binds(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign, int cancel)
{
    static BqError const expected[BQ_PREP_CAMPAIGN_MODES] = {BQ_OK, BQ_INVALID_TRANSITION, BQ_INVALID_TRANSITION,
        BQ_INVALID_TRANSITION, BQ_WORKSPACE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH,
        BQ_SOURCE_MISMATCH, BQ_RECIPE_MISMATCH, BQ_RECIPE_MISMATCH, BQ_RECIPE_MISMATCH, BQ_RECIPE_MISMATCH,
        BQ_RECIPE_MISMATCH};
    for (u32 mode = 0; mode < BQ_PREP_CAMPAIGN_MODES; mode += 1)
    {
        BqError result = bq_prep_campaign_bind(context, campaign, mode, cancel);
        if (result != expected[mode]) fprintf(stderr, "RETIREMENT_PREP campaign bind mode %u returned %d\n", mode, (int)result);
        BQ_PREP_CHECK(result == expected[mode]);
        if (mode != BQ_PREP_CAMPAIGN_BIND)
        {
            BQ_PREP_CHECK(bq_prep_campaign_refused(campaign) &&
                          bq_retirement_campaign_ready_holds(&campaign->ready, &campaign->held));
            continue;
        }
        /* The launchable binding holds the record's binaries at the frozen
         * plan and pre-sample context this entry derived, marked with the
         * record's digest. */
        TpRetirementCampaign const* frozen = &campaign->campaign;
        BQ_PREP_CHECK(result == BQ_OK && campaign->binding.campaign == &campaign->campaign &&
                      campaign->binding.held_binaries == &campaign->held && campaign->held.owned &&
                      campaign->ready.owned && !strcmp(campaign->ready.ready_sha256, context->ready_sha256) &&
                      !strcmp(campaign->binding.unit_ready_sha256, context->ready_sha256) &&
                      campaign->binding.job_id == context->attempt->attempt.job.id &&
                      campaign->binding.attempt_token == context->attempt->attempt.job.token &&
                      frozen->phase == TP_RETIREMENT_CAMPAIGN_AA && frozen->groups == campaign->groups &&
                      frozen->runtime_count == campaign->runtime_count &&
                      !strcmp(frozen->plan_sha256, campaign->plan_sha) &&
                      !strcmp(frozen->context_sha256, campaign->context_sha) &&
                      !memcmp(&frozen->plan, &campaign->plan, sizeof(campaign->plan)) &&
                      bq_retirement_campaign_held_matches(&campaign->binding));
        /* The held descriptors, swapped, no longer bind. */
        BqRetirementHeldBinaries swapped = campaign->held;
        swapped.descriptors[0] = campaign->held.descriptors[1];
        swapped.descriptors[1] = campaign->held.descriptors[0];
        BQ_PREP_CHECK(bq_prep_campaign_stages(campaign, context->attempt->attempt.job.id,
                      context->attempt->attempt.job.token, 7));
        BqRetirementCampaignRequest request = bq_prep_campaign_request(campaign);
        BQ_PREP_CHECK(!bq_retirement_campaign_bind_held(&campaign->binding, request.gate, request.campaign, request.plan,
                      request.aa, request.ab, &swapped, context->attempt->attempt.job.id,
                      context->attempt->attempt.job.token, request.aa_commands, request.ab_commands,
                      request.command_workspace, request.command_count, request.identity_workspace,
                      request.identity_count, request.review, campaign->budget_sha, request.plan_sha256,
                      request.context_sha256) && !campaign->binding.campaign &&
                      campaign->campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID);
    }
}

/* SETTLING through the first untimed launch: the driver acknowledges
 * SETTLING, the import runs under it and holds the record's pair, and the
 * untimed production batch launches the held baseline. The fixture's matched
 * builds are text files, so the child cannot exec (exit 125): the driver
 * stops with the launch's coordinates and process facts and keeps its log. */
BUSTER_GLOBAL_LOCAL void bq_prep_campaign_driver(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign, int cancel)
{
    BqPrepUnitAttempt const* attempt = &context->attempt->attempt;
    u64 job = attempt->job.id, token = attempt->job.token;
    char root[] = "/tmp/bq-retirement-unit-campaign-XXXXXX";
    bool made = mkdtemp(root) != NULL;
    int work = made ? open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    bool ok = work >= 3 && mkdirat(work, "logs", 0700) == 0 && mkdirat(work, "code", 0700) == 0;
    int logs = ok ? openat(work, "logs", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int code_directory = ok ? openat(work, "code", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    BQ_PREP_CHECK(ok && logs >= 3 && code_directory >= 3);
    /* The peer answers PREPARING (the unit's build) and the driver's SETTLING. */
    BqPhaseChannel phases;
    pid_t peer = bq_prep_campaign_peer(&phases, job, token, 2);
    BQ_PREP_CHECK(peer > 0 && bq_phase_exchange(&phases, BQ_PHASE_PREPARING));
    BqRetirementUnitCampaign driver = {0};
    u64 deadline = bq_phase_clock() + 300000000000ull;
    ok = work >= 3 && logs >= 3 && code_directory >= 3 &&
        bq_retirement_unit_campaign_begin(&driver, &phases, cancel, deadline, work, logs) &&
        phases.sequence == BQ_PHASE_SETTLING &&
        bq_prep_campaign_import(context, campaign, &phases, cancel, deadline) == BQ_OK;
    BQ_PREP_CHECK(ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_SETTLING);
    /* One untimed singleton group on the imported pair. */
    char label[129], digests[2][65];
    char* arguments[2][3];
    char* leaves[2] = {"untimed-0.o", "untimed-1.o"};
    TpRetirementUntimedBatch batches[4];
    for (u32 variant = 0; variant < 2; variant += 1)
    {
        arguments[variant][0] = variant ? "candidate-ide" : "base-ide";
        arguments[variant][1] = leaves[variant];
        arguments[variant][2] = NULL;
        TpRetirementMeasuredCommand command = {.unit = 0, .kind = 0, .variant = variant, .arguments = arguments[variant],
            .argument_count = 2, .environment = campaign->environment, .environment_count = 1, .directory = root,
            .artifact = leaves[variant], .timeout_seconds = 2, .command_sha256 = digests[variant],
            .output_sha256 = campaign->batch_output};
        BQ_PREP_CHECK(tp_retirement_command_hash(&command, digests[variant]));
        for (u32 purpose = 0; purpose < 2; purpose += 1)
            batches[variant * 2 + purpose] = (TpRetirementUntimedBatch){command, 0, variant, purpose,
                TP_RETIREMENT_GROUP_SINGLETON};
    }
    unsigned inputs[1] = {1}, kinds[1] = {TP_RETIREMENT_GROUP_SINGLETON};
    unsigned stages[1] = {TP_RETIREMENT_BUDGET_STAGE_LINK}, rows[1] = {campaign->runtime[0]};
    TpRetirementCampaignReview review = {&campaign->budget, campaign->group_stages, inputs, kinds, stages,
        campaign->groups, 1};
    TpRetirementCodeRow codes[2];
    memset(codes, 0, sizeof(codes));
    BqRetirementUnitCampaignCode code = {rows, codes, 2, code_directory};
    BqRetirementUnitCampaignStreams none = {0};
    FILE* records = tmpfile();
    unsigned char reproduced[2];
    TpRetirementUntimed untimed;
    ok = ok && records && bq_retirement_campaign_job_label(label, job) &&
        tp_retirement_untimed_init(&untimed, records, NULL, &campaign->budget, 1, reproduced, label, token,
            "boot-fixture", tp_first_allowed_cpu(), tp_process_monotonic_ns(), UINT64_MAX - 1, 0);
    BQ_PREP_CHECK(ok);
    bool ran = ok && bq_retirement_unit_campaign_untimed(&driver, &untimed, batches, 4, &campaign->held, &review, &none,
                                                         &code);
    BqRetirementUnitCampaignFailure failure = bq_retirement_unit_campaign_failure(&driver);
    struct stat kept;
    if (ok && failure.exit_code != 125)
        fprintf(stderr, "RETIREMENT_PREP campaign driver stop %u status %d exit %d signal %d error %d\n", failure.reason,
                failure.status, failure.exit_code, failure.signal_number, failure.launch_error);
    BQ_PREP_CHECK(ok && !ran && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && untimed.failed &&
                  failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH && failure.launched &&
                  failure.step == BQ_RETIREMENT_UNIT_CAMPAIGN_SETTLING && !failure.stage && !failure.group &&
                  !failure.variant && failure.purpose == TP_RETIREMENT_UNTIMED_PRODUCTION && !failure.sequence &&
                  failure.exit_code == 125 && !failure.signal_number && !failure.timed_out && !failure.cancelled &&
                  failure.at_ns && !driver.launches[0] && !driver.code_count &&
                  fstatat(logs, BQ_RETIREMENT_UNIT_CAMPAIGN_LOG, &kept, AT_SYMLINK_NOFOLLOW) == 0 &&
                  kept.st_size == (off_t)failure.log_bytes && tp_retirement_digest(failure.log_sha256));
    /* Nothing further runs on a failed driver. */
    BQ_PREP_CHECK(!bq_retirement_unit_campaign_measuring(&driver) && phases.sequence == BQ_PHASE_SETTLING &&
                  bq_retirement_unit_campaign_failure(&driver).reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH);
    if (records) fclose(records);
    bq_prep_campaign_drop(campaign);
    BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    int descriptors[] = {code_directory, logs, work};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(descriptors); index += 1)
        if (descriptors[index] >= 0) BQ_PREP_CHECK(close(descriptors[index]) == 0);
    if (made) bq_prep_test_cleanup(root);
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_unit_campaign(void)
{
    u32 descriptors = bq_prep_test_open_descriptors();
    BqPrepOracleFixture* fixture = calloc(1, sizeof(*fixture));
    BqPrepOracleAttempt* success = calloc(1, sizeof(*success));
    BqPrepCampaign* campaign = calloc(1, sizeof(*campaign));
    BqPrepCampaignAttempt* context = calloc(1, sizeof(*context));
    int cancel[2] = {-1, -1};
    bool ok = fixture && success && campaign && context && pipe2(cancel, O_CLOEXEC | O_NONBLOCK) == 0 &&
        bq_prep_test_oracle_setup(fixture);
    /* Only an attempt the fixture began is closed: a zeroed one names fd 0. */
    bool attempted = ok;
    ok = ok && bq_prep_test_oracle_attempt(fixture, 91, cancel[0], success);
    BQ_PREP_CHECK(ok);
    if (campaign) campaign->held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
    BqRetirementUnitOracle oracle = {0};
    BqRetirementUnitGate admitted = {0};
    ok = ok && bq_retirement_unit_oracle_pinned(&success->attempt.unit, &success->projection, fixture->workspaces_fd,
            string_from_pointer(fixture->profile), cancel[0], bq_phase_clock() + 300000000000ull, &oracle) == BQ_OK &&
        bq_retirement_unit_gate_fixture_admit(&success->attempt.unit, &success->projection, &oracle, &admitted) &&
        bq_retirement_unit_ready(&success->attempt.unit, &success->built, &success->projection, &oracle, &admitted,
            fixture->workspaces_fd, context->ready_sha256) == BQ_OK &&
        bq_prep_campaign_gate(campaign, &success->projection);
    BQ_PREP_CHECK(ok);
    if (ok)
    {
        context->fixture = fixture;
        context->attempt = success;
        int length = snprintf(context->profile, sizeof(context->profile),
            "%scampaign-seed=7\ncampaign-pairs=60\ncampaign-resamples=100000\ncampaign-bootstrap-members=5\n"
            "campaign-budget-sha256=%s\n", fixture->profile, campaign->budget_sha);
        ok = length > 0 && (size_t)length < sizeof(context->profile);
        context->unit = (BqRetirementCampaignUnitStore){success->attempt.store, fixture->workspaces_fd,
            fixture->installed_fd, string_from_pointer(fixture->workspaces), string_from_pointer(context->profile),
            S8("self-test"), fixture->driver, fixture->toolchain_root, fixture->broker, fixture->workspaces};
        BQ_PREP_CHECK(ok && campaign->timed > campaign->groups && campaign->objects && campaign->runtime_count == 2);
    }
    if (ok)
    {
        bq_prep_campaign_imports(context, campaign, cancel);
        bq_prep_campaign_ready_cases(context, campaign, &oracle, &admitted);
        bq_prep_campaign_timed_rows(context, campaign);
        /* One import under SETTLING, then every bind on that pair. */
        BqPhaseChannel phases;
        pid_t peer = bq_prep_campaign_channel(&phases, success->attempt.job.id, success->attempt.job.token, 2);
        BQ_PREP_CHECK(bq_prep_campaign_import(context, campaign, &phases, cancel[0],
                                              bq_phase_clock() + 300000000000ull) == BQ_OK);
        BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
        if (campaign->held.owned && campaign->ready.owned)
        {
            bq_prep_campaign_bind_phases(context, campaign, cancel);
            bq_prep_campaign_binds(context, campaign, cancel[0]);
        }
        bq_prep_campaign_drop(campaign);
        bq_prep_campaign_driver(context, campaign, cancel[0]);
    }
    if (campaign) bq_prep_campaign_release(campaign);
    if (oracle.owned) BQ_PREP_CHECK(bq_retirement_unit_oracle_release(&oracle));
    if (attempted) BQ_PREP_CHECK(bq_prep_test_oracle_attempt_close(success));
    if (fixture) bq_prep_test_oracle_teardown(fixture);
    for (u32 side = 0; side < 2; side += 1)
        if (cancel[side] >= 0) close(cancel[side]);
    free(context);
    free(campaign);
    free(success);
    free(fixture);
    BQ_PREP_CHECK(bq_prep_test_open_descriptors() == descriptors && bq_prep_test_live_children() == 0);
}

#endif
