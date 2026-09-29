/* #881-D store-based campaign import, bind and B -> D ready handoff fixtures.
 * Included by the preparation test runner after retirement_unit_oracle_tests.h.
 *
 * The attempt is the real unit-oracle fixture (#1020 PR 3/4): prepare, the
 * fixture broker's matched builds, the census projection, the oracle
 * authority and reference producer, then lane B's step 9 issuer
 * (bq_retirement_unit_gate_pinned: B's stand-in required checks run in the
 * unit, this campaign's row evidence joins, the #509 batch authority is
 * granted by bq_retirement_correctness_authorize) and the ready record written
 * for that unit gate (bq_prep_campaign_issue). On that record this file
 * checks the ready import
 * (every field re-derived by the coordinator replay; the template and
 * inventory compared with this attempt's policy), refusals of a mutated,
 * forged, re-addressed, foreign-job or foreign-token record and of a moved
 * reference descriptor, the join of the unit gate and its correctness seal to
 * the record (each field check also alone), and the
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
 * retained evidence. The row evidence is B's passing test evidence with this
 * campaign's commands and the required checks are B's stand-ins, so the gate
 * is not a #509 admission, and no timed child runs. */
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
    /* Lane B's issued unit gate; gate is its correctness gate, and rows and
     * facts are the evidence arrays until it is issued, then the gate's. */
    BqRetirementUnitGate unit_gate;
    BqCheckTestEvidence evidence;
    BqRetirementCorrectness* gate;
    BqRetirementTrustedRow* rows;
    BqRetirementRowFact* facts;
    /* The validator's timed partition of the pinned rows, which the gate's
     * frozen batch groups follow. */
    BqRetirementDocumentPopulation population;
    BqRetirementDocumentPartition partition;
    TpRetirementBatchInput inputs[BQ_PREP_CAMPAIGN_CAP];
    char fixtures[BQ_PREP_CAMPAIGN_CAP][64], leaves[BQ_PREP_CAMPAIGN_CAP][32];
    TpRetirementBatchContract contracts[BQ_PREP_CAMPAIGN_CAP];
    BqRetirementBatchGroup frozen[BQ_PREP_CAMPAIGN_CAP];
    char object_outputs[BQ_PREP_CAMPAIGN_CAP][65];
    unsigned frozen_count;
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
    char artifact_sha[65], batch_output[65], budget_sha[65];
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
    if (campaign->unit_gate.owned) BQ_PREP_CHECK(bq_retirement_unit_gate_release(&campaign->unit_gate));
    bq_check_test_evidence_release(&campaign->evidence);
    bq_retirement_documents_partition_release(&campaign->partition);
    bq_retirement_documents_population_release(&campaign->population);
    campaign->gate = NULL;
    campaign->rows = NULL;
    campaign->facts = NULL;
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
                                     object ? campaign->object_outputs[slot] : campaign->batch_output;
                campaign->commands[stage][index] = (TpRetirementMeasuredCommand){.unit = runtime ? row : slot,
                    .kind = runtime, .variant = variant, .argument_count = 2, .arguments = argv,
                    .environment = campaign->environment, .environment_count = 1, .directory = "/tmp",
                    .artifact = runtime || object ? NULL : argv[1], .batch = object ? &campaign->contracts[slot] : NULL,
                    .timeout_seconds = 2, .command_sha256 = campaign->command_sha[stage][index],
                    .output_sha256 = output};
                ok = ok && tp_retirement_command_hash(&campaign->commands[stage][index],
                                                      campaign->command_sha[stage][index]);
            }
    return ok;
}

/* The row evidence lane B's step 9 issuer joins (bq_retirement_unit_gate),
 * over the projection's own rows (population hash unchanged): B's passing
 * test evidence for every row (bq_check_test_evidence), with this campaign's
 * commands for the timed rows. The timed groups are the validator's
 * partition of the pinned rows (bq_retirement_documents_partition): object
 * rows sharing a configuration and frozen argv form one frozen batch group
 * (members first, in row order, each input the row's identity fixture),
 * every timed link or self-host row is a singleton, runtime for the
 * singletons the reference oracle observed. The issuer then runs the
 * installed stand-in required checks and grants the #509 batch authority;
 * nothing here writes it. */
BUSTER_GLOBAL_LOCAL bool bq_prep_campaign_evidence(BqPrepCampaign* campaign, BqRetirementProjection const* projection,
    int installed, String8 profile)
{
    u32 count = projection->prepared.rows;
    bool ok = bq_check_test_evidence(projection, &campaign->evidence) &&
        bq_retirement_documents_population(installed, profile, projection->rows, count,
            projection->prepared.native_target, &campaign->population) == BQ_OK &&
        bq_retirement_documents_partition(&campaign->population, 0, &campaign->partition);
    campaign->rows = campaign->evidence.rows;
    campaign->facts = campaign->evidence.facts;
    campaign->environment[0] = "LC_ALL=C";
    campaign->budget = bq_campaign_service_budget();
    static char const artifact[] = "fixture artifact bytes\n";
    static char const empty[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    bq_digest(artifact, sizeof(artifact) - 1, (char8*)campaign->artifact_sha);
    char const* objects[] = {campaign->artifact_sha};
    ok = ok && tp_retirement_batch_output_digest(objects, 1, campaign->batch_output) &&
        tp_retirement_budget_digest(&campaign->budget, campaign->budget_sha);
    /* The timed rows in ascending order (the samples' dense rows). */
    for (u32 row = 0; ok && row < count; row += 1)
    {
        campaign->facts[row].row = row;
        campaign->facts[row].census_row = campaign->rows[row].census_row;
        BqRetirementTrustedRow const* trusted = &campaign->rows[row];
        if (!(trusted->compiler_eligible && trusted->target == projection->prepared.native_target)) continue;
        bool object = trusted->stage == BQ_RETIREMENT_STAGE_OBJECT;
        bool runtime = !object && trusted->independent_oracle_sha256[0];
        ok = campaign->timed < BQ_PREP_CAMPAIGN_CAP;
        if (!ok) break;
        u32 dense = campaign->timed++;
        campaign->row_ids[dense] = row;
        campaign->row_metrics[dense] = runtime ? TP_RETIREMENT_SAMPLE_RUNTIME : 0;
        campaign->facts[row].compiler_eligible = 1;
        campaign->facts[row].runtime_eligible = runtime;
        if (runtime) campaign->runtime[campaign->runtime_count++] = row;
    }
    /* The partition's groups: their kinds, stages, dense members and, for an
     * object group, its frozen contract over its members' fixtures. */
    BqRetirementDocumentPartition const* partition = &campaign->partition;
    ok = ok && partition->count < BQ_PREP_CAMPAIGN_CAP && partition->row_count == campaign->timed;
    u32 used = 0;
    for (u32 group = 0; ok && group < partition->count; group += 1)
    {
        u32 first = partition->rows[partition->first[group]];
        bool object = partition->object[group] != 0;
        campaign->kinds[group] = object ? TP_RETIREMENT_GROUP_OBJECT : TP_RETIREMENT_GROUP_SINGLETON;
        campaign->group_row[group] = first;
        campaign->group_stages[group] = object ? TP_RETIREMENT_BUDGET_STAGE_OBJECT :
            campaign->rows[first].stage == BQ_RETIREMENT_STAGE_LINK ? TP_RETIREMENT_BUDGET_STAGE_LINK :
            TP_RETIREMENT_BUDGET_STAGE_SELF_HOST;
        campaign->offsets[group] = used;
        u32 start = campaign->objects;
        for (u32 member = partition->first[group]; ok && member < partition->first[group + 1]; member += 1)
        {
            u32 row = partition->rows[member], dense = 0;
            while (dense < campaign->timed && campaign->row_ids[dense] != row) dense += 1;
            ok = dense < campaign->timed;
            campaign->members[used++] = dense;
            if (!ok || !object) continue;
            u32 input = campaign->objects++;
            String8 fixture = bq_retirement_document_value(&campaign->population, row, BQ_RETIREMENT_DOCUMENT_FIXTURE);
            ok = fixture.length < sizeof(campaign->fixtures[input]);
            if (ok) memcpy(campaign->fixtures[input], fixture.pointer, (size_t)fixture.length);
            if (ok) campaign->fixtures[input][fixture.length] = 0;
            snprintf(campaign->leaves[input], sizeof(campaign->leaves[input]), "r%u.o", row);
            campaign->inputs[input] = (TpRetirementBatchInput){campaign->fixtures[input], "ok", "driver.none", empty,
                campaign->artifact_sha, campaign->leaves[input], 1, row};
        }
        if (ok && object)
        {
            campaign->contracts[group] = (TpRetirementBatchContract){"x86_64-linux", "none", "batch.metrics",
                campaign->inputs + start, campaign->objects - start, 0, 0};
            ok = tp_retirement_budget_metrics_bytes(&campaign->budget, campaign->objects - start,
                    &campaign->contracts[group].metrics_bytes_max) &&
                tp_retirement_batch_contract_output(&campaign->contracts[group], campaign->object_outputs[group]);
        }
    }
    campaign->groups = ok ? partition->count : 0;
    campaign->offsets[campaign->groups] = used;
    ok = ok && used == campaign->timed && campaign->groups && campaign->runtime_count && bq_prep_campaign_commands(campaign);
    /* Row facts and trusted commands are the A/B stage's frozen commands;
     * each object group has its own batch key. */
    for (u32 group = 0; ok && group < campaign->groups; group += 1)
    {
        char key[SHA256_HEX_CAPACITY], seed[32];
        int length = snprintf(seed, sizeof(seed), "prep-batch-key-%u", group);
        bq_digest(seed, (u32)length, (char8*)key);
        for (u32 dense = campaign->offsets[group]; dense < campaign->offsets[group + 1]; dense += 1)
        {
            u32 row = campaign->row_ids[campaign->members[dense]];
            BqRetirementTrustedRow* trusted = &campaign->rows[row];
            if (campaign->kinds[group] == TP_RETIREMENT_GROUP_OBJECT) memcpy(trusted->batch_key_sha256, key, 65);
            else memset(trusted->batch_key_sha256, 0, 65);
            for (u32 side = 0; side < 2; side += 1)
            {
                BqRetirementObservedSide* observed = &campaign->facts[row].side[side];
                memcpy(trusted->compiler_command_sha256[side], campaign->command_sha[1][group * 2 + side], 65);
                memcpy(observed->compiler_command_sha256, campaign->command_sha[1][group * 2 + side], 65);
                memcpy(observed->artifact_sha256, campaign->artifact_sha, 65);
            }
        }
        if (campaign->kinds[group] == TP_RETIREMENT_GROUP_OBJECT)
        {
            BqRetirementBatchGroup* frozen = &campaign->frozen[campaign->frozen_count++];
            *frozen = (BqRetirementBatchGroup){{campaign->contracts[group], campaign->contracts[group]}, {{0}}};
            memcpy(frozen->command_sha256[0], campaign->command_sha[1][group * 2], 65);
            memcpy(frozen->command_sha256[1], campaign->command_sha[1][group * 2 + 1], 65);
        }
    }
    for (u32 runtime = 0; ok && runtime < campaign->runtime_count; runtime += 1)
        for (u32 side = 0; side < 2; side += 1)
        {
            char const* command = campaign->command_sha[1][(campaign->groups + runtime) * 2 + side];
            memcpy(campaign->facts[campaign->runtime[runtime]].side[side].runtime_command_sha256, command, 65);
            memcpy(campaign->rows[campaign->runtime[runtime]].runtime_command_sha256[side], command, 65);
        }
    campaign->evidence.evidence.groups = campaign->frozen_count ? campaign->frozen : NULL;
    campaign->evidence.evidence.group_count = campaign->frozen_count;
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
    sha256_finish_hex(&hash, campaign->evidence.evidence.aa_second_commands_sha256);
    campaign->review = (TpRetirementCampaignReview){&campaign->budget, campaign->group_stages, NULL, NULL, NULL,
        campaign->groups, 0};
    return ok && count == campaign->evidence.evidence.row_count;
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
                campaign->runtime_count, campaign->gate->prepared.rows, 60, stage->workspace,
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
    BqRetirementCampaignRequest request = {campaign->gate, &campaign->campaign, &campaign->plan,
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
        attempt->job.id, attempt->job.token, attempt->digest, context->ready_sha256, &campaign->unit_gate, &campaign->held,
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
    /* A record store whose request names another recipe (the attempt's own
     * request with only its recipe field changed): refused before anything
     * is imported. */
    char root[] = "/tmp/bq-retirement-unit-recipe-XXXXXX";
    bool made = mkdtemp(root) != NULL;
    int directory = made ? open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    char request_name[48];
    BqRequest original = {0}, other = {0};
    bool staged = directory >= 3 && bq_record_name(request_name, "request", job) &&
        bq_record_read_at(attempt->store.directory, request_name, original.bytes, BQ_REQUEST_CAP, &original.size) ==
        BQ_OK && bq_request_recipe(&original) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED;
    String8 request_fields[BQ_FIELD_COUNT];
    for (u32 index = 0; staged && index < BQ_FIELD_COUNT; index += 1) request_fields[index] = bq_field(&original, index);
    if (staged) request_fields[2] = S8("fake-success-v1");
    staged = staged && bq_request_make(request_fields, &other) == BQ_OK &&
        bq_request_recipe(&other) == BQ_RECIPE_FAKE_SUCCESS;
    int record = staged ? openat(directory, request_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600) : -1;
    staged = record >= 0 && bq_write_all(record, other.bytes, other.size) && fchmod(record, 0400) == 0;
    if (record >= 0) BQ_PREP_CHECK(close(record) == 0);
    BQ_PREP_CHECK(staged);
    if (staged)
    {
        BqRetirementCampaignUnitStore unit = context->unit;
        unit.store.directory = directory;
        BqPhaseChannel phases;
        pid_t peer = bq_prep_campaign_channel(&phases, job, token, 2);
        BQ_PREP_CHECK(bq_retirement_campaign_service_import_unit_pinned(&unit, &phases, cancel[0], generous, job, token,
                          attempt->digest, context->ready_sha256, &campaign->unit_gate, &campaign->held,
                          &campaign->ready) == BQ_INVALID_TRANSITION && bq_prep_campaign_unheld(campaign));
        BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    }
    if (directory >= 0) BQ_PREP_CHECK(close(directory) == 0);
    if (made) bq_prep_test_cleanup(root);
    /* A gate sealed over other source manifests; the compiled blocked
     * profile of the production wrapper. */
    for (u32 trial = 0; trial < 2; trial += 1)
    {
        BqPhaseChannel phases;
        pid_t peer = bq_prep_campaign_channel(&phases, job, token, 2);
        char* source = campaign->gate->prepared.source_sha256[1];
        char saved = source[0];
        BqError result = BQ_OK;
        if (!trial)
        {
            source[0] = saved == '0' ? '1' : '0';
            bq_retirement_correctness_seal(campaign->gate, campaign->gate->sealed_sha256);
            result = bq_prep_campaign_import(context, campaign, &phases, cancel[0], generous);
            source[0] = saved;
            bq_retirement_correctness_seal(campaign->gate, campaign->gate->sealed_sha256);
            BQ_PREP_CHECK(result == BQ_SOURCE_MISMATCH);
        }
        else
        {
            result = bq_retirement_campaign_service_import_unit(attempt->store, context->fixture->workspaces_fd,
                context->fixture->installed_fd, &phases, cancel[0], generous, job, token, attempt->digest,
                context->ready_sha256, &campaign->unit_gate, &campaign->held, &campaign->ready);
            BQ_PREP_CHECK(result != BQ_OK);
        }
        BQ_PREP_CHECK(bq_prep_campaign_unheld(campaign) && bq_retirement_correctness_ready(campaign->gate));
        bq_prep_campaign_drop(campaign);
        BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    }
}

/* The ready record's import, its refusals and the gate join. */
BUSTER_GLOBAL_LOCAL void bq_prep_campaign_ready_cases(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign,
    BqRetirementUnitOracle const* oracle)
{
    BqRetirementUnitGate const* admitted = &campaign->unit_gate;
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
                  !strcmp(ready.correctness_sha256, admitted->correctness.sealed_sha256) &&
                  !strcmp(ready.checks_authority_sha256, admitted->authority_sha256) &&
                  !strcmp(ready.check_evidence_sha256, admitted->evidence_sha256) &&
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
        {"correctness=", 0, BQ_RECIPE_MISMATCH}, {"check-evidence=", 0, BQ_CORRUPT},
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

    /* The gate join: the issued unit gate joins. Its correctness gate must
     * carry the record's `correctness=` seal; each field check is also
     * exercised alone, against a record copy naming the resealed gate: a
     * changed A, candidate binary, support or census digest, row population,
     * reference oracle output or an extra oracle row does not join. */
    BqRetirementCorrectness* gate = campaign->gate;
    BQ_PREP_CHECK(bq_retirement_campaign_ready_gate(&ready, gate) == BQ_OK &&
                  bq_retirement_campaign_ready_unit_gate(&ready, admitted) == BQ_OK);
    BqRetirementCampaignReady joined = ready;
    joined.correctness_sha256[0] = joined.correctness_sha256[0] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_retirement_campaign_ready_gate(&joined, gate) == BQ_RECIPE_MISMATCH);
    /* The unit gate: released (not owned), unissued, or with another seal,
     * check authority or check evidence than the record's, never joins. */
    for (u32 trial = 0; trial < 5; trial += 1)
    {
        BqRetirementUnitGate other_gate = *admitted;
        if (trial == 4) other_gate.owned = 0;
        if (!trial) other_gate.issuer = 0;
        char* digest = trial == 1 ? other_gate.seal_sha256 : trial == 2 ? other_gate.authority_sha256 :
            other_gate.evidence_sha256;
        if (trial && trial < 4) digest[0] = digest[0] == '0' ? '1' : '0';
        BQ_PREP_CHECK(bq_retirement_campaign_ready_unit_gate(&ready, &other_gate) == BQ_RECIPE_MISMATCH);
    }
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
        joined = ready;
        memcpy(joined.correctness_sha256, gate->sealed_sha256, SHA256_HEX_CAPACITY);
        BQ_PREP_CHECK(bq_retirement_correctness_ready(gate) &&
                      bq_retirement_campaign_ready_gate(&ready, gate) == BQ_RECIPE_MISMATCH &&
                      bq_retirement_campaign_ready_gate(&joined, gate) == BQ_SOURCE_MISMATCH);
        memcpy(fields[index], saved, sizeof(saved));
        bq_retirement_correctness_seal(gate, gate->sealed_sha256);
    }
    BQ_PREP_CHECK(bq_retirement_correctness_ready(gate) && bq_retirement_campaign_ready_gate(&ready, gate) == BQ_OK);
    /* An unready (resealed-after-change) gate never joins. */
    campaign->rows[7].census_row += 1;
    BQ_PREP_CHECK(!bq_retirement_correctness_ready(gate) &&
                  bq_retirement_campaign_ready_gate(&ready, gate) == BQ_RECIPE_MISMATCH);
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

/* Census row `census`'s tab-separated fields in the installed rows.tsv (an
 * independent source for the dimension values): target, cpu, allocator,
 * frontend_lowering and PIC, as columns 3, 5, 7, 8 and 9. */
BUSTER_GLOBAL_LOCAL bool bq_prep_campaign_census_fields(char const* text, u32 census, char fields[5][65])
{
    static u32 const columns[5] = {3, 5, 7, 8, 9};
    char const* line = strchr(text, '\n');
    bool found = false;
    while (line && !found)
    {
        line += 1;
        char const* end = strchr(line, '\n');
        found = end && (u32)strtoul(line, NULL, 10) == census && line[0] >= '0' && line[0] <= '9';
        for (u32 index = 0; found && index < 5; index += 1)
        {
            char const* cursor = line;
            for (u32 column = 0; cursor && column < columns[index]; column += 1)
            {
                cursor = strchr(cursor, '\t');
                if (cursor) cursor += 1;
            }
            size_t length = cursor ? strcspn(cursor, "\t\n") : 0;
            found = cursor && length && length < 65;
            if (found) memcpy(fields[index], cursor, length);
            if (found) fields[index][length] = 0;
        }
        line = found ? line : end;
    }
    return found;
}

/* Lane E's per-timed-row layout from the pinned performance rows: every
 * timed row in ascending order with its campaign group, runtime flag and
 * dimension values; a short workspace, a gate row whose identity is not the
 * declared one, and a profile without the performance-row pin are refused. */
BUSTER_GLOBAL_LOCAL void bq_prep_campaign_timed_rows(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign)
{
    TpRetirementTimedRow* rows = calloc(BQ_PREP_CAMPAIGN_CAP, sizeof(*rows));
    unsigned count = 0;
    BQ_PREP_CHECK(rows && bq_retirement_campaign_service_timed_rows(&context->unit, campaign->gate, rows,
                  BQ_PREP_CAMPAIGN_CAP, &count) == BQ_OK && count == campaign->timed);
    char path[256];
    u32 const text_capacity = 1u << 20;
    char* census_rows = malloc(text_capacity);
    int named = snprintf(path, sizeof(path), "%s/rows.tsv", context->fixture->census);
    BQ_PREP_CHECK(census_rows && named > 0 && (size_t)named < sizeof(path) &&
                  bq_prep_test_read_text(path, census_rows, text_capacity) > 0);
    for (u32 dense = 0; rows && census_rows && dense < count && dense < campaign->timed; dense += 1)
    {
        /* The exact values: the census row's own rows.tsv columns. */
        char expected[5][65];
        BQ_PREP_CHECK(bq_prep_campaign_census_fields(census_rows, campaign->rows[rows[dense].id].census_row, expected));
        for (u32 dimension = 0; dimension < 5; dimension += 1)
        {
            bool same = !strcmp(rows[dense].dimensions[dimension], expected[dimension]);
            if (!same)
                fprintf(stderr, "RETIREMENT_PREP timed row %u dimension %u is %s, census has %s\n", rows[dense].id,
                        dimension, rows[dense].dimensions[dimension], expected[dimension]);
            BQ_PREP_CHECK(same);
        }
    }
    free(census_rows);
    /* The group total: without the last (singleton) timed row the ordinals
     * cannot cover the gate's timed groups. */
    TpRetirementTimedRow* partial = calloc(BQ_PREP_CAMPAIGN_CAP, sizeof(*partial));
    if (partial) memcpy(partial, rows, (size_t)count * sizeof(*partial));
    BQ_PREP_CHECK(partial && count > 1 && bq_retirement_campaign_timed_groups_assign(campaign->gate, partial, count) &&
                  !bq_retirement_campaign_timed_groups_assign(campaign->gate, partial, count - 1u));
    free(partial);
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
    BQ_PREP_CHECK(rows && bq_retirement_campaign_service_timed_rows(&context->unit, campaign->gate, rows, count - 1u,
                  &count) == BQ_SOURCE_MISMATCH && !count);
    char* identity = campaign->rows[campaign->row_ids[0]].identity_sha256;
    char saved = identity[0];
    identity[0] = saved == '0' ? '1' : '0';
    bq_retirement_correctness_seal(campaign->gate, campaign->gate->sealed_sha256);
    BQ_PREP_CHECK(rows && bq_retirement_correctness_ready(campaign->gate) &&
                  bq_retirement_campaign_service_timed_rows(&context->unit, campaign->gate, rows, BQ_PREP_CAMPAIGN_CAP,
                  &count) == BQ_SOURCE_MISMATCH && !count);
    identity[0] = saved;
    bq_retirement_correctness_seal(campaign->gate, campaign->gate->sealed_sha256);
    char unpinned[5120];
    memcpy(unpinned, context->profile, sizeof(unpinned));
    char* line = strstr(unpinned, "performance-rows-sha256=");
    char* end = line ? strchr(line, '\n') : NULL;
    if (end) memmove(line, end + 1, strlen(end + 1) + 1);
    BqRetirementCampaignUnitStore unit = context->unit;
    unit.profile = string_from_pointer(unpinned);
    BQ_PREP_CHECK(end && rows && bq_retirement_campaign_service_timed_rows(&unit, campaign->gate, rows,
                  BQ_PREP_CAMPAIGN_CAP, &count) == BQ_SOURCE_MISMATCH && !count);
    BQ_PREP_CHECK(rows && bq_retirement_campaign_service_timed_rows(&context->unit, campaign->gate, rows,
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
            trial == 7 ? bq_phase_clock() : generous, job, token, &untimed, &campaign->unit_gate, &request,
            &campaign->held, &campaign->ready, &campaign->binding);
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
    BQ_PREP_CAMPAIGN_OTHER_GATE, BQ_PREP_CAMPAIGN_OTHER_UNIT_GATE, BQ_PREP_CAMPAIGN_CALLER_GATE,
    BQ_PREP_CAMPAIGN_UNIT_GATE_COPY,
    BQ_PREP_CAMPAIGN_BLOCKED_PROFILE, BQ_PREP_CAMPAIGN_UNPINNED,
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
        bq_retirement_unit_campaign_derive(campaign->gate, &pins, context->ready_sha256, &untimed,
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
    char saved = campaign->gate->prepared.census_sha256[0];
    if (mode == BQ_PREP_CAMPAIGN_OTHER_GATE)
    {
        campaign->gate->prepared.census_sha256[0] = saved == '0' ? '1' : '0';
        bq_retirement_correctness_seal(campaign->gate, campaign->gate->sealed_sha256);
    }
    /* The binding keeps the held pair it binds: only a refused copy is local. */
    bool changed_held = mode == BQ_PREP_CAMPAIGN_SWAPPED || mode == BQ_PREP_CAMPAIGN_HELD_DIGEST ||
        mode == BQ_PREP_CAMPAIGN_HELD_PREPARATION;
    bool changed_ready = mode == BQ_PREP_CAMPAIGN_READY_JOB || mode == BQ_PREP_CAMPAIGN_READY_TOKEN ||
        mode == BQ_PREP_CAMPAIGN_MOVED;
    BqRetirementHeldBinaries const* bound_held = changed_held ? &held : &campaign->held;
    BqRetirementCampaignReady const* bound_ready = changed_ready ? &ready : &campaign->ready;
    BqRetirementCampaignRequest request = bq_prep_campaign_request(campaign);
    /* Another issued seal than the record's; a correctness gate the caller
     * holds (an identical copy) rather than the unit gate's own. */
    BqRetirementUnitGate other_gate = campaign->unit_gate;
    other_gate.seal_sha256[0] = other_gate.seal_sha256[0] == '0' ? '1' : '0';
    /* A whole-struct copy of the imported unit gate: identical, but not the
     * gate the import joined. */
    BqRetirementUnitGate whole = campaign->unit_gate;
    BqRetirementUnitGate const* unit_gate = mode == BQ_PREP_CAMPAIGN_OTHER_UNIT_GATE ? &other_gate :
        mode == BQ_PREP_CAMPAIGN_UNIT_GATE_COPY ? &whole : &campaign->unit_gate;
    BqRetirementCorrectness copied = campaign->unit_gate.correctness;
    if (mode == BQ_PREP_CAMPAIGN_CALLER_GATE) request.gate = &copied;
    if (mode == BQ_PREP_CAMPAIGN_UNIT_GATE_COPY) request.gate = &whole.correctness;
    u64 deadline = bq_phase_clock() + 300000000000ull;
    BqError result = mode == BQ_PREP_CAMPAIGN_BLOCKED_PROFILE ?
        bq_retirement_campaign_service_bind_unit(attempt->store, context->fixture->workspaces_fd,
            context->fixture->installed_fd, &phases, cancel, deadline, job, token, &untimed, unit_gate, &request,
            bound_held, bound_ready, &campaign->binding) :
        bq_retirement_campaign_service_bind_unit_pinned(&unit, &phases, cancel, deadline, job, token, &untimed,
            unit_gate, &request, bound_held, bound_ready, &campaign->binding);
    if (mode == BQ_PREP_CAMPAIGN_OTHER_GATE)
    {
        campaign->gate->prepared.census_sha256[0] = saved;
        bq_retirement_correctness_seal(campaign->gate, campaign->gate->sealed_sha256);
    }
    free(moved);
    BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    return result;
}

BUSTER_GLOBAL_LOCAL void bq_prep_campaign_binds(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign, int cancel)
{
    static BqError const expected[BQ_PREP_CAMPAIGN_MODES] = {BQ_OK, BQ_INVALID_TRANSITION, BQ_INVALID_TRANSITION,
        BQ_INVALID_TRANSITION, BQ_WORKSPACE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH,
        BQ_RECIPE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_RECIPE_MISMATCH, BQ_RECIPE_MISMATCH, BQ_RECIPE_MISMATCH, BQ_RECIPE_MISMATCH,
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
    bool ran = ok && bq_retirement_unit_campaign_untimed(&driver, &untimed, batches, 4, &campaign->held, campaign->gate,
                                                         &review, &none,
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

/* Lane B's step 9 and 10 on this attempt: the stand-in required checks are
 * installed and pinned (as B's own fixtures do), the issuer runs them in the
 * unit and joins this campaign's row evidence into a sealed correctness gate
 * with the #509 batch authority, and the ready record is written for it. */
BUSTER_GLOBAL_LOCAL bool bq_prep_campaign_issue(BqPrepOracleFixture* fixture, BqPrepOracleAttempt* success,
    BqRetirementUnitOracle const* oracle, BqPrepCampaign* campaign, int cancellation_fd,
    char ready_sha256[SHA256_HEX_CAPACITY])
{
    BqRetirementUnitPrepared const* unit = &success->attempt.unit;
    BqRetirementProjection const* projection = &success->projection;
    u32 eligible = 0;
    for (u32 row = 0; row < projection->prepared.rows; row += 1) eligible += projection->rows[row].compiler_eligible;
    BqCheckTestSpec specs[BQ_CHECK_TEST_CHECKS];
    bq_check_test_specs(specs, projection->prepared.object_rows, eligible, projection->prepared.native_target);
    char pin[256] = {0};
    bool ok = bq_prep_campaign_evidence(campaign, projection, fixture->installed_fd,
                                        string_from_pointer(fixture->profile)) &&
        bq_check_test_install(fixture->recipes, &unit->preparation, projection, specs, BQ_CHECK_TEST_CHECKS, 0, pin,
                              sizeof(pin)) && strlen(fixture->profile) + strlen(pin) < sizeof(fixture->profile);
    if (ok) strcat(fixture->profile, pin);
    String8 profile = string_from_pointer(fixture->profile);
    BqError issued = ok ? bq_retirement_unit_gate_pinned(unit, &success->built, projection, oracle,
        fixture->workspaces_fd, fixture->installed_fd, profile, &campaign->evidence.evidence, cancellation_fd,
        bq_phase_clock() + 300000000000ull, &campaign->unit_gate) : BQ_BAD_REQUEST;
    if (ok && issued != BQ_OK) fprintf(stderr, "RETIREMENT_PREP campaign unit gate returned %d\n", (int)issued);
    BqError written = issued == BQ_OK ? bq_retirement_unit_ready_pinned(unit, &success->built, projection, oracle,
        &campaign->unit_gate, fixture->workspaces_fd, profile, ready_sha256) : BQ_BAD_REQUEST;
    if (issued == BQ_OK && written != BQ_OK) fprintf(stderr, "RETIREMENT_PREP campaign ready returned %d\n", (int)written);
    ok = written == BQ_OK && campaign->unit_gate.issuer == BQ_RETIREMENT_UNIT_GATE_ISSUED &&
        campaign->unit_gate.correctness.batch_authority == 1;
    if (ok)
    {
        campaign->gate = &campaign->unit_gate.correctness;
        campaign->rows = campaign->unit_gate.rows;
        campaign->facts = campaign->unit_gate.facts;
    }
    return ok && bq_retirement_correctness_ready(campaign->gate);
}

/* A new document file `name` in `directory`, opened for writing. */
BUSTER_GLOBAL_LOCAL FILE* bq_prep_campaign_document_open(int directory, char const* name)
{
    int file = openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    FILE* stream = file >= 0 ? fdopen(file, "wb") : NULL;
    if (!stream && file >= 0) close(file);
    return stream;
}

/* The pre-sample and post-A/A documents the unit writes, accepted by the
 * validator itself: the documents, the pinned performance rows and the
 * expected context go to a scratch directory and
 * retirement_unit_documents_test.py runs the validator's own checks
 * (_performance_rows_with_sources, _check_execution_plan, _result_input_plan,
 * _workflow_phase and the pre-sample and post-A/A field joins) over them.
 * The untimed batches are synthetic (one per untimed group and variant, the
 * reproductions the gate's artifacts): they are never launched here. */
BUSTER_GLOBAL_LOCAL void bq_prep_campaign_documents(BqPrepCampaignAttempt* context, BqPrepCampaign* campaign)
{
    BqRetirementCorrectness const* gate = campaign->gate;
    String8 profile = string_from_pointer(context->profile);
    BqRetirementDocumentPopulation population = {0};
    BqRetirementDocumentPartition timed = {0}, untimed = {0};
    BqRetirementUnitCampaignPins pins = {0};
    TpRetirementPlan plan = {0};
    BqRetirementDocumentFamily family = {0};
    char support[SHA256_HEX_CAPACITY] = {0}, manifest[SHA256_HEX_CAPACITY] = {0}, rows_pin[SHA256_HEX_CAPACITY] = {0};
    bool ok = bq_retirement_documents_population(context->fixture->installed_fd, profile, gate->trusted_rows,
            gate->prepared.rows, gate->prepared.native_target, &population) == BQ_OK &&
        bq_retirement_documents_partition(&population, 0, &timed) &&
        bq_retirement_documents_partition(&population, 1, &untimed) &&
        bq_retirement_documents_family(&population, &timed, &family) &&
        bq_retirement_unit_campaign_pins(profile, &pins) && bq_retirement_unit_campaign_plan(gate, &pins, &plan) &&
        bq_retirement_profile_sha(profile, S8("support-declaration-sha256="), support) &&
        bq_retirement_profile_sha(profile, S8("census-manifest-sha256="), manifest) &&
        bq_retirement_profile_sha(profile, S8("census-rows-sha256="), rows_pin);
    /* The plan's counts are the derived family's; the partition is the
     * gate's frozen batch groups. */
    BQ_PREP_CHECK(ok && plan.bootstrap_members_per_scope == family.bootstrap_members &&
                  plan.cell_members_per_scope == family.cell_members && timed.count == campaign->groups &&
                  timed.object_groups == gate->batch_group_count && untimed.count);
    /* Every row's canonical identity (the driver's census-free join) is the
     * identity the gate sealed from the census. */
    for (u32 row = 0; ok && row < population.count; row += 1)
    {
        char identity[SHA256_HEX_CAPACITY];
        BQ_PREP_CHECK(bq_retirement_document_identity(&population, row, identity) &&
                      !strcmp(identity, gate->trusted_rows[row].identity_sha256));
    }
    u32 groups = untimed.count;
    TpRetirementUntimedBatch* batches = calloc((size_t)groups * 4u + 1u, sizeof(*batches));
    unsigned* untimed_rows = calloc((size_t)groups + 1u, sizeof(*untimed_rows));
    TpRetirementCodeRow* codes = calloc((size_t)untimed.row_count + 1u, sizeof(*codes));
    TpRetirementBatchContract* contracts = calloc((size_t)groups + 1u, sizeof(*contracts));
    TpRetirementBatchInput* inputs = calloc((size_t)untimed.row_count + 1u, sizeof(*inputs));
    char (*fixtures)[64] = calloc((size_t)untimed.row_count + 1u, sizeof(*fixtures));
    char (*commands)[65] = calloc((size_t)groups * 2u + 1u, sizeof(*commands));
    ok = ok && batches && untimed_rows && codes && contracts && inputs && fixtures && commands;
    static char const empty[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    for (u32 group = 0; ok && group < groups; group += 1)
    {
        bool object = untimed.object[group] != 0;
        u32 first = untimed.first[group], members = untimed.first[group + 1] - first;
        for (u32 member = 0; ok && member < members; member += 1)
        {
            u32 row = untimed.rows[first + member];
            String8 fixture = bq_retirement_document_value(&population, row, BQ_RETIREMENT_DOCUMENT_FIXTURE);
            ok = fixture.length < sizeof(fixtures[0]);
            if (ok) memcpy(fixtures[first + member], fixture.pointer, (size_t)fixture.length);
            inputs[first + member] = (TpRetirementBatchInput){fixtures[first + member], "ok", "driver.none", empty,
                gate->facts[row].side[0].artifact_sha256, "untimed.o", 1, row};
            codes[first + member].row = row;
            for (u32 side = 0; side < 2; side += 1)
            {
                memcpy(codes[first + member].sides[side].artifact_sha256, gate->facts[row].side[side].artifact_sha256, 65);
                memcpy(codes[first + member].sides[side].reproduction_sha256,
                       gate->facts[row].side[side].artifact_sha256, 65);
            }
        }
        if (object)
        {
            contracts[group] = (TpRetirementBatchContract){"x86_64-linux", "none", "untimed.metrics", inputs + first,
                members, 0, 0};
            ok = ok && tp_retirement_budget_metrics_bytes(&campaign->budget, members, &contracts[group].metrics_bytes_max);
        }
        else untimed_rows[group] = untimed.rows[first];
        for (u32 variant = 0; ok && variant < 2; variant += 1)
        {
            char seed[48];
            int length = snprintf(seed, sizeof(seed), "untimed-command-%u-%u", group, variant);
            bq_digest(seed, (u32)length, (char8*)commands[group * 2 + variant]);
            TpRetirementMeasuredCommand command = {.unit = group, .variant = variant,
                .command_sha256 = commands[group * 2 + variant], .batch = object ? &contracts[group] : NULL};
            for (u32 purpose = 0; purpose < 2; purpose += 1)
                batches[(group * 2 + variant) * 2 + purpose] = (TpRetirementUntimedBatch){command, group, variant,
                    purpose, object ? TP_RETIREMENT_GROUP_OBJECT : TP_RETIREMENT_GROUP_SINGLETON};
        }
    }
    /* Codes in ascending row order. */
    for (u32 entry = 1; ok && entry < untimed.row_count; entry += 1)
    {
        TpRetirementCodeRow moving = codes[entry];
        u32 at = entry;
        while (at && codes[at - 1].row > moving.row)
        {
            codes[at] = codes[at - 1];
            at -= 1;
        }
        codes[at] = moving;
    }
    BqRetirementDocumentInputs inputs_view = {gate, &population, &timed, &untimed, &campaign->budget, &plan, batches,
        groups * 4u, untimed_rows, codes, untimed.row_count, 2, support, manifest, rows_pin};
    char root[] = "/tmp/bq-retirement-unit-documents-XXXXXX";
    bool made = ok && mkdtemp(root) != NULL;
    int directory = made ? open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    static char const* const names[5] = {"oracle.json", "execution-plan.json", "result-input-plan.json",
        "pre-sample-plan.json", "post-aa-binding.json"};
    BqRetirementDocumentDescriptor descriptors[5];
    memset(descriptors, 0, sizeof(descriptors));
    BqRetirementUnitCampaignPartition manifests[2][BQ_RETIREMENT_DOCUMENT_PARTITIONS];
    unsigned manifest_counts[2] = {0, 0};
    static char const aa_admission[] = "abababababababababababababababababababababababababababababababab";
    for (u32 index = 0; directory >= 3 && index < 5; index += 1)
    {
        FILE* stream = bq_prep_campaign_document_open(directory, names[index]);
        BqRetirementDocumentPhase phase = {&family, descriptors[2].sha256, names[1], &descriptors[1],
            descriptors[3].sha256, aa_admission};
        bool written = stream &&
            (index == 0 ? bq_retirement_documents_oracle(stream, &inputs_view, &descriptors[0]) :
             index == 1 ? bq_retirement_documents_execution_plan(stream, &inputs_view, &descriptors[1]) :
             index == 2 ? bq_retirement_documents_result_input_plan(stream, &inputs_view, manifests, manifest_counts,
                                                                    &descriptors[2]) :
             bq_retirement_documents_phase(stream, &inputs_view, &phase, index == 4, &descriptors[index]));
        if (stream && fclose(stream) != 0) written = false;
        if (!written) fprintf(stderr, "RETIREMENT_PREP document %s was not written\n", names[index]);
        BQ_PREP_CHECK(written);
    }
    /* The expected context: the census the rows were pinned from, and the
     * values the unit derived (the test rederives every one it can). */
    FILE* expected = directory >= 3 ? bq_prep_campaign_document_open(directory, "context.json") : NULL;
    if (expected)
    {
        fprintf(expected, "{\"census\":\"%s\",\"cpu\":2,\"seed\":%" PRIu64 ",\"pairs_per_round\":%u,\"resamples\":%u,"
                "\"bootstrap_members_per_scope\":%u,\"cell_members_per_scope\":%u,\"family_sha256\":\"%s\","
                "\"support_declaration_sha256\":\"%s\",\"manifest_sha256\":\"%s\",\"rows_sha256\":\"%s\","
                "\"object_row_count\":%u,\"aa_admission_sha256\":\"%s\",\"performance_rows_sha256\":\"%s\","
                "\"manifest_counts\":[%u,%u],\"documents\":{",
                context->fixture->census, plan.seed, plan.pairs_per_round, plan.resamples, family.bootstrap_members, family.cell_members,
                family.sha256, support, manifest, rows_pin, gate->prepared.object_rows, aa_admission,
                population.performance_rows_sha256, manifest_counts[0], manifest_counts[1]);
        for (u32 index = 0; index < 5; index += 1)
            fprintf(expected, "%s\"%s\":{\"path\":\"%s\",\"bytes\":%" PRIu64 ",\"sha256\":\"%s\"}", index ? "," : "",
                    names[index], names[index], descriptors[index].bytes, descriptors[index].sha256);
        fprintf(expected, "}}\n");
        BQ_PREP_CHECK(fclose(expected) == 0);
    }
    char* argv[] = {"python3", "tools/bench_service/retirement_unit_documents_test.py", root, NULL};
    bool accepted = directory >= 3 && bq_prep_test_run(argv);
    if (!accepted) fprintf(stderr, "RETIREMENT_PREP the validator refused the unit's documents in %s\n", root);
    BQ_PREP_CHECK(accepted);
    /* A document over a gate row whose sealed identity changed is refused
     * by the population join (the documents never bind unsealed rows). */
    BqRetirementDocumentPopulation other = {0};
    char* identity = campaign->rows[campaign->row_ids[0]].identity_sha256;
    char saved = identity[0];
    identity[0] = saved == '0' ? '1' : '0';
    BQ_PREP_CHECK(campaign->rows == gate->trusted_rows &&
                  bq_retirement_documents_population(context->fixture->installed_fd, profile, gate->trusted_rows,
                  gate->prepared.rows, gate->prepared.native_target, &other) == BQ_SOURCE_MISMATCH && !other.rows);
    identity[0] = saved;
    if (directory >= 0) BQ_PREP_CHECK(close(directory) == 0);
    if (made && accepted) bq_prep_test_cleanup(root);
    free(batches);
    free(untimed_rows);
    free(codes);
    free(contracts);
    free(inputs);
    free(fixtures);
    free(commands);
    bq_retirement_documents_partition_release(&timed);
    bq_retirement_documents_partition_release(&untimed);
    bq_retirement_documents_population_release(&population);
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
    ok = ok && bq_retirement_unit_oracle_pinned(&success->attempt.unit, &success->projection, fixture->workspaces_fd,
            string_from_pointer(fixture->profile), cancel[0], bq_phase_clock() + 300000000000ull, &oracle) == BQ_OK &&
        bq_prep_campaign_issue(fixture, success, &oracle, campaign, cancel[0], context->ready_sha256);
    BQ_PREP_CHECK(ok);
    if (ok)
    {
        context->fixture = fixture;
        context->attempt = success;
        /* The bootstrap pin is the derived family's count, as the validator
         * requires of rules.sampling. */
        BqRetirementDocumentFamily family = {0};
        ok = bq_retirement_documents_family(&campaign->population, &campaign->partition, &family);
        int length = snprintf(context->profile, sizeof(context->profile),
            "%scampaign-seed=7\ncampaign-pairs=60\ncampaign-resamples=100000\ncampaign-bootstrap-members=%u\n"
            "campaign-budget-sha256=%s\n", fixture->profile, family.bootstrap_members, campaign->budget_sha);
        ok = ok && length > 0 && (size_t)length < sizeof(context->profile);
        context->unit = (BqRetirementCampaignUnitStore){success->attempt.store, fixture->workspaces_fd,
            fixture->installed_fd, string_from_pointer(fixture->workspaces), string_from_pointer(context->profile),
            S8("self-test"), fixture->driver, fixture->toolchain_root, fixture->broker, fixture->workspaces};
        BQ_PREP_CHECK(ok && campaign->timed == campaign->partition.row_count && campaign->objects &&
                      campaign->frozen_count && campaign->runtime_count == 2);
    }
    if (ok)
    {
        bq_prep_campaign_imports(context, campaign, cancel);
        bq_prep_campaign_ready_cases(context, campaign, &oracle);
        bq_prep_campaign_timed_rows(context, campaign);
        bq_prep_campaign_documents(context, campaign);
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
