/* #1020 pre-timing correctness join. begin authenticates the shape of the
 * imported complete population; check and row poison on the first failure;
 * batches freezes the (A1) object batch-group contracts; finish seals every
 * source/check/row/output/batch fact; ready checks the seal again.
 * The service must obtain the input and receipt digests from trusted sources
 * and execute/replay their content before calling these private entry points.
 */
#include "retirement_correctness.h"
#include <stdlib.h>
#include <string.h>

BUSTER_GLOBAL_LOCAL bool bq_retirement_correctness_digest(char const* value)
{
    bool ok = value != NULL;
    for (uint32_t i = 0; ok && i < 64; i += 1)
        ok = (value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f');
    if (ok) ok = value[64] == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_correctness_empty(char const value[65])
{
    bool ok = value != NULL;
    for (uint32_t i = 0; ok && i < 65; i += 1) ok = value[i] == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_correctness_equal(char const left[65], char const right[65])
{
    bool ok = !memcmp(left, right, 65);
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_correctness_number(Sha256* hash, uint64_t value)
{
    uint8_t bytes[8];
    for (uint32_t i = 0; i < 8; i += 1) bytes[i] = (uint8_t)(value >> (i * 8));
    sha256_add(hash, bytes, sizeof(bytes));
}

BUSTER_GLOBAL_LOCAL void bq_retirement_correctness_text(Sha256* hash, char const text[65])
{
    sha256_add(hash, text, 65);
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_correctness_side_empty(BqRetirementObservedSide const* side)
{
    bool ok = bq_retirement_correctness_empty(side->compiler_command_sha256) &&
        bq_retirement_correctness_empty(side->artifact_sha256) &&
        bq_retirement_correctness_empty(side->code_sha256) &&
        bq_retirement_correctness_empty(side->diagnostic_sha256) &&
        bq_retirement_correctness_empty(side->runtime_command_sha256) &&
        bq_retirement_correctness_empty(side->runtime_output_sha256) && side->code_bytes == 0 &&
        side->semantic_pass == 0 && side->fallback_count == 0 &&
        side->timed_out == 0 && side->out_of_memory == 0 &&
        side->compiler_exit == 0 && side->runtime_exit == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_correctness_side_hash(Sha256* hash, BqRetirementObservedSide const* side)
{
    bq_retirement_correctness_text(hash, side->compiler_command_sha256);
    bq_retirement_correctness_text(hash, side->artifact_sha256);
    bq_retirement_correctness_text(hash, side->code_sha256);
    bq_retirement_correctness_text(hash, side->diagnostic_sha256);
    bq_retirement_correctness_text(hash, side->runtime_command_sha256);
    bq_retirement_correctness_text(hash, side->runtime_output_sha256);
    bq_retirement_correctness_number(hash, side->code_bytes);
    bq_retirement_correctness_number(hash, side->semantic_pass);
    bq_retirement_correctness_number(hash, side->fallback_count);
    bq_retirement_correctness_number(hash, side->timed_out);
    bq_retirement_correctness_number(hash, side->out_of_memory);
    bq_retirement_correctness_number(hash, (uint32_t)side->compiler_exit);
    bq_retirement_correctness_number(hash, (uint32_t)side->runtime_exit);
}

/* The aggregate of every frozen group's per-side contract digest and batch
 * command, in group order; false when a contract no longer validates. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_correctness_batch_digest(BqRetirementCorrectness const* gate,
    BqRetirementBatchGroup const* groups, uint32_t count, char digest[65])
{
    Sha256 hash;
    sha256_init(&hash);
    static char const domain[] = "bq-retirement-batch-groups-v1";
    sha256_add(&hash, domain, sizeof(domain) - 1);
    bq_retirement_correctness_number(&hash, count);
    bool ok = gate && (!count || groups);
    for (uint32_t g = 0; ok && g < count; g += 1)
        for (uint32_t side = 0; ok && side < 2; side += 1)
        {
            char contract[65] = {0};
            ok = tp_retirement_batch_contract_digest(&groups[g].contract[side], contract);
            bq_retirement_correctness_text(&hash, contract);
            bq_retirement_correctness_text(&hash, groups[g].command_sha256[side]);
        }
    if (ok) sha256_finish_hex(&hash, digest);
    else memset(digest, 0, 65);
    return ok;
}

/* The live groups still match the batches() snapshot (trivially without groups). */
BUSTER_GLOBAL_LOCAL bool bq_retirement_correctness_batches_unchanged(BqRetirementCorrectness const* gate)
{
    char live[65];
    bool ok = !gate->batches_frozen ? !gate->batch_group_count && bq_retirement_correctness_empty(gate->batch_groups_sha256) :
        bq_retirement_correctness_batch_digest(gate, gate->batch_groups, gate->batch_group_count, live) &&
        bq_retirement_correctness_equal(live, gate->batch_groups_sha256);
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_correctness_seal(BqRetirementCorrectness const* gate, char digest[65])
{
    Sha256 hash;
    sha256_init(&hash);
    static char const domain[] = "bq-retirement-correctness-private-v1";
    sha256_add(&hash, domain, sizeof(domain) - 1);
    BqRetirementPrepared const* prepared = &gate->prepared;
    bq_retirement_correctness_text(&hash, prepared->preparation_sha256);
    bq_retirement_correctness_text(&hash, prepared->support_sha256);
    bq_retirement_correctness_text(&hash, prepared->census_sha256);
    bq_retirement_correctness_text(&hash, prepared->aa_second_commands_sha256);
    for (uint32_t side = 0; side < 2; side += 1)
    {
        bq_retirement_correctness_text(&hash, prepared->source_sha256[side]);
        bq_retirement_correctness_text(&hash, prepared->binary_sha256[side]);
    }
    bq_retirement_correctness_number(&hash, prepared->rows);
    bq_retirement_correctness_number(&hash, prepared->object_rows);
    bq_retirement_correctness_number(&hash, prepared->native_target);
    bq_retirement_correctness_number(&hash, gate->check_count);
    bq_retirement_correctness_number(&hash, gate->eligible_rows);
    for (uint32_t i = 0; i < gate->check_count; i += 1)
    {
        BqRetirementRequiredCheck const* required = &gate->required_checks[i];
        BqRetirementCheckResult const* checked = &gate->check_facts[i];
        bq_retirement_correctness_number(&hash, required->kind);
        bq_retirement_correctness_number(&hash, required->target);
        bq_retirement_correctness_number(&hash, required->rows);
        bq_retirement_correctness_text(&hash, required->command_sha256);
        bq_retirement_correctness_text(&hash, required->configuration_sha256);
        bq_retirement_correctness_text(&hash, required->receipt_sha256);
        bq_retirement_correctness_number(&hash, checked->kind);
        bq_retirement_correctness_number(&hash, checked->target);
        bq_retirement_correctness_number(&hash, checked->rows);
        bq_retirement_correctness_number(&hash, checked->failures);
        bq_retirement_correctness_number(&hash, checked->timed_out);
        bq_retirement_correctness_number(&hash, checked->out_of_memory);
        bq_retirement_correctness_number(&hash, (uint32_t)checked->exit_code);
        bq_retirement_correctness_text(&hash, checked->command_sha256);
        bq_retirement_correctness_text(&hash, checked->configuration_sha256);
        bq_retirement_correctness_text(&hash, checked->receipt_sha256);
        bq_retirement_correctness_text(&hash, checked->preparation_sha256);
        for (uint32_t side = 0; side < 2; side += 1)
        {
            bq_retirement_correctness_text(&hash, checked->source_sha256[side]);
            bq_retirement_correctness_text(&hash, checked->binary_sha256[side]);
        }
    }
    for (uint32_t i = 0; i < prepared->rows; i += 1)
    {
        BqRetirementTrustedRow const* row = &gate->trusted_rows[i];
        BqRetirementRowFact const* fact = &gate->facts[i];
        bq_retirement_correctness_number(&hash, row->row);
        bq_retirement_correctness_number(&hash, row->census_row);
        bq_retirement_correctness_number(&hash, row->target);
        bq_retirement_correctness_number(&hash, row->stage);
        bq_retirement_correctness_number(&hash, row->classification);
        bq_retirement_correctness_number(&hash, row->compiler_eligible);
        bq_retirement_correctness_number(&hash, row->code_obligation);
        bq_retirement_correctness_number(&hash, row->execution_obligation);
        bq_retirement_correctness_number(&hash, row->batch_control);
        bq_retirement_correctness_text(&hash, row->batch_key_sha256);
        bq_retirement_correctness_text(&hash, row->identity_sha256);
        bq_retirement_correctness_text(&hash, row->source_sha256);
        bq_retirement_correctness_text(&hash, row->configuration_sha256);
        bq_retirement_correctness_text(&hash, row->skip_proof_sha256);
        bq_retirement_correctness_text(&hash, row->independent_oracle_sha256);
        for (uint32_t side = 0; side < 2; side += 1)
        {
            bq_retirement_correctness_text(&hash, row->compiler_command_sha256[side]);
            bq_retirement_correctness_text(&hash, row->runtime_command_sha256[side]);
        }
        bq_retirement_correctness_number(&hash, fact->row);
        bq_retirement_correctness_number(&hash, fact->census_row);
        bq_retirement_correctness_number(&hash, fact->compiler_eligible);
        bq_retirement_correctness_number(&hash, fact->runtime_eligible);
        bq_retirement_correctness_number(&hash, fact->code_eligible);
        for (uint32_t side = 0; side < 2; side += 1)
            bq_retirement_correctness_side_hash(&hash, &fact->side[side]);
    }
    /* (A1, L4) The frozen batch groups as batches() snapshotted them: each
     * side's complete contract digest (members, controls, objects, leaves,
     * input list, metrics bound) and batch command. finish and ready require
     * the live groups to match the snapshot. (M2) The #509 authority flag. */
    bq_retirement_correctness_number(&hash, gate->batches_frozen);
    bq_retirement_correctness_number(&hash, gate->batch_group_count);
    bq_retirement_correctness_text(&hash, gate->batch_groups_sha256);
    bq_retirement_correctness_number(&hash, gate->batch_authority);
    bq_retirement_correctness_text(&hash, gate->checks_sha256);
    sha256_finish_hex(&hash, digest);
}

bool bq_retirement_correctness_begin(BqRetirementCorrectness* gate,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow const* rows,
    BqRetirementRequiredCheck const* checks, uint32_t check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    uint32_t* identity_workspace, uint32_t identity_slots,
    uint8_t* census_workspace, uint32_t census_slots)
{
    bool ok = gate && prepared && rows && checks && check_facts && facts &&
        identity_workspace && census_workspace && prepared->rows &&
        prepared->rows <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP &&
        prepared->object_rows && prepared->object_rows <= prepared->rows &&
        identity_slots >= prepared->rows * 2u + 1u &&
        census_slots >= prepared->object_rows &&
        prepared->native_target >= 1 && prepared->native_target <= 12 &&
        check_count >= BQ_RETIREMENT_CHECK_COUNT - 1 &&
        check_count <= BQ_RETIREMENT_CORRECTNESS_CHECKS_CAP &&
        bq_retirement_correctness_digest(prepared->preparation_sha256) &&
        bq_retirement_correctness_digest(prepared->support_sha256) &&
        bq_retirement_correctness_digest(prepared->census_sha256) &&
        bq_retirement_correctness_digest(prepared->aa_second_commands_sha256);
    for (uint32_t side = 0; ok && side < 2; side += 1)
        ok = bq_retirement_correctness_digest(prepared->source_sha256[side]) &&
             bq_retirement_correctness_digest(prepared->binary_sha256[side]);
    uint32_t required_kinds = 0, eligible = 0, object_rows = 0;
    uint32_t native_link_rows = 0, native_self_host_rows = 0;
    for (uint32_t i = 0; ok && i < check_count; i += 1)
    {
        BqRetirementRequiredCheck const* check = &checks[i];
        ok = check->kind >= BQ_RETIREMENT_CHECK_CENSUS &&
             check->kind < BQ_RETIREMENT_CHECK_COUNT && check->rows > 0 &&
             check->rows <= prepared->rows && check->target <= 12 &&
             bq_retirement_correctness_digest(check->command_sha256) &&
             bq_retirement_correctness_digest(check->configuration_sha256) &&
             bq_retirement_correctness_digest(check->receipt_sha256);
        if (ok)
        {
            for (uint32_t previous = 0; ok && previous < i; previous += 1)
                ok = !(checks[previous].kind == check->kind && checks[previous].target == check->target &&
                       !strcmp(checks[previous].configuration_sha256, check->configuration_sha256));
            required_kinds |= 1u << check->kind;
        }
    }
    if (ok) ok = required_kinds == ((1u << BQ_RETIREMENT_CHECK_COUNT) - 2u);
    if (ok)
    {
        memset(identity_workspace, 0, (size_t)identity_slots * sizeof(*identity_workspace));
        memset(census_workspace, 0, census_slots);
    }
    for (uint32_t i = 0; ok && i < prepared->rows; i += 1)
    {
        BqRetirementTrustedRow const* row = &rows[i];
        ok = row->row == i && row->census_row < prepared->object_rows &&
             row->target >= 1 && row->target <= 12 &&
             row->stage >= BQ_RETIREMENT_STAGE_OBJECT && row->stage <= BQ_RETIREMENT_STAGE_SELF_HOST &&
             row->classification >= 1 && row->classification <= 5 &&
             row->compiler_eligible <= 1 && row->code_obligation <= 1 && row->execution_obligation <= 1 &&
             row->batch_control <= 1 &&
             (!row->batch_control || (!row->compiler_eligible && row->stage == BQ_RETIREMENT_STAGE_OBJECT &&
                 row->target == prepared->native_target)) &&
             (bq_retirement_correctness_empty(row->batch_key_sha256) ||
              bq_retirement_correctness_digest(row->batch_key_sha256)) &&
             bq_retirement_correctness_digest(row->identity_sha256) &&
             bq_retirement_correctness_digest(row->source_sha256) &&
             bq_retirement_correctness_digest(row->configuration_sha256) &&
             (row->compiler_eligible ? bq_retirement_correctness_empty(row->skip_proof_sha256) :
                 bq_retirement_correctness_digest(row->skip_proof_sha256));
        bool native_runtime = row->compiler_eligible && row->execution_obligation &&
            row->stage != BQ_RETIREMENT_STAGE_OBJECT && row->target == prepared->native_target;
        if (ok) ok = native_runtime ? bq_retirement_correctness_digest(row->independent_oracle_sha256) :
            bq_retirement_correctness_empty(row->independent_oracle_sha256);
        for (uint32_t side = 0; ok && side < 2; side += 1)
            ok = (row->compiler_eligible || row->batch_control ?
                  bq_retirement_correctness_digest(row->compiler_command_sha256[side]) :
                  bq_retirement_correctness_empty(row->compiler_command_sha256[side])) &&
                 (native_runtime ?
                  bq_retirement_correctness_digest(row->runtime_command_sha256[side]) :
                  bq_retirement_correctness_empty(row->runtime_command_sha256[side]));
        if (ok)
        {
            uint32_t slot = 2166136261u;
            for (uint32_t byte = 0; byte < 64; byte += 1)
                slot = (slot ^ (uint8_t)row->identity_sha256[byte]) * 16777619u;
            slot %= identity_slots;
            while (identity_workspace[slot] &&
                   !bq_retirement_correctness_equal(row->identity_sha256,
                       rows[identity_workspace[slot] - 1].identity_sha256))
                slot = slot + 1 == identity_slots ? 0 : slot + 1;
            ok = identity_workspace[slot] == 0;
            if (ok) identity_workspace[slot] = i + 1;
        }
        if (ok && row->stage == BQ_RETIREMENT_STAGE_OBJECT)
        {
            ok = census_workspace[row->census_row] == 0;
            if (ok)
            {
                census_workspace[row->census_row] = 1;
                object_rows += 1;
            }
        }
        if (ok && row->target == prepared->native_target && row->compiler_eligible)
        {
            if (row->stage == BQ_RETIREMENT_STAGE_LINK) native_link_rows += 1;
            if (row->stage == BQ_RETIREMENT_STAGE_SELF_HOST) native_self_host_rows += 1;
        }
        if (ok) eligible += row->compiler_eligible;
    }
    /* Both native executable stages are required compiler-latency cells.
     * A generated-runtime oracle is conditional on each row's obligation. */
    if (ok) ok = eligible > 0 && object_rows == prepared->object_rows &&
        native_link_rows > 0 && native_self_host_rows > 0;
    for (uint32_t i = 0; ok && i < check_count; i += 1)
    {
        BqRetirementRequiredCheck const* check = &checks[i];
        if (check->kind == BQ_RETIREMENT_CHECK_CENSUS) ok = check->rows == object_rows;
        if (check->kind == BQ_RETIREMENT_CHECK_NO_FALLBACK ||
            check->kind == BQ_RETIREMENT_CHECK_MATRIX) ok = check->rows == eligible;
    }
    if (gate)
    {
        *gate = (BqRetirementCorrectness){.failed = !ok};
        if (ok)
        {
            gate->prepared = *prepared;
            gate->trusted_rows = rows;
            gate->required_checks = checks;
            gate->check_facts = check_facts;
            gate->facts = facts;
            gate->check_count = check_count;
            gate->required_kinds = required_kinds;
            gate->eligible_rows = eligible;
            sha256_init(&gate->checks_hash);
        }
    }
    return ok;
}

bool bq_retirement_correctness_check(BqRetirementCorrectness* gate, BqRetirementCheckResult const* observed)
{
    bool ok = gate && !gate->failed && !gate->finished && observed &&
        gate->checks_done < gate->check_count;
    if (ok)
    {
        BqRetirementRequiredCheck const* required = &gate->required_checks[gate->checks_done];
        ok = observed->kind == required->kind && observed->target == required->target &&
             observed->rows == required->rows && observed->failures == 0 &&
             observed->exit_code == 0 && observed->timed_out == 0 && observed->out_of_memory == 0 &&
             bq_retirement_correctness_equal(observed->command_sha256, required->command_sha256) &&
             bq_retirement_correctness_equal(observed->configuration_sha256, required->configuration_sha256) &&
             bq_retirement_correctness_equal(observed->preparation_sha256, gate->prepared.preparation_sha256) &&
             bq_retirement_correctness_equal(observed->receipt_sha256, required->receipt_sha256);
        for (uint32_t side = 0; ok && side < 2; side += 1)
            ok = bq_retirement_correctness_equal(observed->source_sha256[side], gate->prepared.source_sha256[side]) &&
                 bq_retirement_correctness_equal(observed->binary_sha256[side], gate->prepared.binary_sha256[side]);
        if (ok)
        {
            bq_retirement_correctness_number(&gate->checks_hash, observed->kind);
            bq_retirement_correctness_number(&gate->checks_hash, observed->target);
            bq_retirement_correctness_number(&gate->checks_hash, observed->rows);
            bq_retirement_correctness_text(&gate->checks_hash, observed->receipt_sha256);
            gate->seen_kinds |= 1u << observed->kind;
            gate->check_facts[gate->checks_done] = *observed;
            gate->checks_done += 1;
        }
    }
    if (gate && !ok) gate->failed = 1;
    return ok;
}

bool bq_retirement_correctness_row(BqRetirementCorrectness* gate, BqRetirementRowFact const* observed)
{
    bool ok = gate && !gate->failed && !gate->finished && observed &&
        gate->rows_done < gate->prepared.rows && gate->checks_done == gate->check_count;
    if (ok)
    {
        BqRetirementTrustedRow const* expected = &gate->trusted_rows[gate->rows_done];
        bool compiler = expected->compiler_eligible != 0;
        bool runtime = compiler && expected->execution_obligation &&
            expected->stage != BQ_RETIREMENT_STAGE_OBJECT && expected->target == gate->prepared.native_target;
        bool code = compiler && expected->code_obligation && observed->side[0].code_bytes > 0;
        ok = observed->row == expected->row && observed->census_row == expected->census_row &&
             observed->compiler_eligible == compiler && observed->runtime_eligible == runtime &&
             observed->code_eligible == code;
        for (uint32_t side = 0; ok && side < 2; side += 1)
        {
            BqRetirementObservedSide const* facts = &observed->side[side];
            if (!compiler && expected->batch_control)
            {
                /* A batch control's standalone observation: its batch command,
                 * exit status and diagnostic, and an object only when it
                 * compiled; no code, runtime or semantic facts. */
                BqRetirementObservedSide control = *facts;
                ok = bq_retirement_correctness_equal(facts->compiler_command_sha256,
                         expected->compiler_command_sha256[side]) &&
                     bq_retirement_correctness_digest(facts->diagnostic_sha256) &&
                     facts->compiler_exit >= 0 && facts->compiler_exit <= 255 &&
                     (facts->compiler_exit ? bq_retirement_correctness_empty(facts->artifact_sha256) :
                                             bq_retirement_correctness_digest(facts->artifact_sha256));
                memset(control.compiler_command_sha256, 0, 65);
                memset(control.diagnostic_sha256, 0, 65);
                memset(control.artifact_sha256, 0, 65);
                control.compiler_exit = 0;
                ok = ok && bq_retirement_correctness_side_empty(&control);
            }
            else if (!compiler) ok = bq_retirement_correctness_side_empty(facts);
            else
            {
                ok = bq_retirement_correctness_equal(facts->compiler_command_sha256,
                         expected->compiler_command_sha256[side]) &&
                     bq_retirement_correctness_digest(facts->artifact_sha256) &&
                     (bq_retirement_correctness_empty(facts->diagnostic_sha256) ||
                      bq_retirement_correctness_digest(facts->diagnostic_sha256)) &&
                     facts->semantic_pass == 1 && facts->fallback_count == 0 &&
                     facts->compiler_exit == 0 && facts->timed_out == 0 && facts->out_of_memory == 0;
                if (ok) ok = expected->code_obligation ?
                    bq_retirement_correctness_digest(facts->code_sha256) &&
                    (facts->code_bytes || !strcmp(facts->code_sha256,
                        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) :
                    bq_retirement_correctness_empty(facts->code_sha256) && facts->code_bytes == 0;
                if (ok) ok = runtime ?
                    bq_retirement_correctness_equal(facts->runtime_command_sha256,
                        expected->runtime_command_sha256[side]) &&
                    bq_retirement_correctness_equal(facts->runtime_output_sha256, expected->independent_oracle_sha256) &&
                    facts->runtime_exit == 0 :
                    bq_retirement_correctness_empty(facts->runtime_command_sha256) &&
                    bq_retirement_correctness_empty(facts->runtime_output_sha256) &&
                    facts->runtime_exit == -1;
            }
        }
        if (ok)
        {
            gate->facts[gate->rows_done] = *observed;
            gate->rows_done += 1;
        }
    }
    if (gate && !ok) gate->failed = 1;
    return ok;
}

/* A timed object row: compiler eligible on the native target at the object stage. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_correctness_timed_object(BqRetirementCorrectness const* gate, uint32_t row)
{
    BqRetirementTrustedRow const* trusted = row < gate->prepared.rows ? &gate->trusted_rows[row] : NULL;
    bool timed = trusted && trusted->compiler_eligible && trusted->target == gate->prepared.native_target &&
        trusted->stage == BQ_RETIREMENT_STAGE_OBJECT;
    return timed;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_correctness_text_equal(char const* left, char const* right)
{
    bool equal = (!left && !right) || (left && right && !strcmp(left, right));
    return equal;
}

BUSTER_GLOBAL_LOCAL int bq_retirement_correctness_key_order(void const* left, void const* right)
{
    return strcmp(*(char const* const*)left, *(char const* const*)right);
}

/* A row control joins the gate's batch control row: native and untimed (by
 * begin), in the group's key, run with the group's batch command on each
 * side, with the frozen status, diagnostic and object equal to its facts. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_correctness_control(BqRetirementCorrectness const* gate,
    BqRetirementBatchGroup const* group, uint32_t index, char const* key)
{
    uint32_t row = group->contract[0].inputs[index].row;
    BqRetirementTrustedRow const* trusted = row < gate->prepared.rows ? &gate->trusted_rows[row] : NULL;
    bool ok = trusted && trusted->batch_control && trusted->target == gate->prepared.native_target &&
        key && !strcmp(trusted->batch_key_sha256, key);
    for (uint32_t side = 0; ok && side < 2; side += 1)
    {
        TpRetirementBatchInput const* input = &group->contract[side].inputs[index];
        BqRetirementObservedSide const* facts = &gate->facts[row].side[side];
        bool compiled = !strcmp(input->status, "ok");
        ok = bq_retirement_correctness_equal(trusted->compiler_command_sha256[side], group->command_sha256[side]) &&
            bq_retirement_correctness_equal(facts->compiler_command_sha256, group->command_sha256[side]) &&
            !strcmp(facts->diagnostic_sha256, input->diagnostic_sha256) && compiled == (facts->compiler_exit == 0) &&
            (compiled ? input->object_sha256 && !strcmp(facts->artifact_sha256, input->object_sha256) :
                        !input->object_sha256);
    }
    return ok;
}

bool bq_retirement_correctness_batches(BqRetirementCorrectness* gate,
    BqRetirementBatchGroup const* groups, uint32_t count, uint8_t* assigned_workspace, uint32_t workspace_slots)
{
    bool ok = gate && !gate->failed && !gate->finished && !gate->batches_frozen &&
        gate->checks_done == gate->check_count && gate->rows_done == gate->prepared.rows &&
        (!count || groups) && count <= gate->prepared.rows && assigned_workspace &&
        workspace_slots >= gate->prepared.rows;
    uint32_t previous_first = 0, timed_objects = 0, members_total = 0;
    char const** keys = ok && count ? (char const**)malloc((size_t)count * sizeof(*keys)) : NULL;
    ok = ok && (!count || keys);
    if (ok) memset(assigned_workspace, 0, gate->prepared.rows);
    for (uint32_t g = 0; ok && g < count; g += 1)
    {
        BqRetirementBatchGroup const* group = &groups[g];
        TpRetirementBatchContract const* base = &group->contract[0];
        TpRetirementBatchContract const* other = &group->contract[1];
        /* The native target's metrics name (the validator's TARGET_METRICS_NAMES). */
        ok = tp_retirement_batch_contract_valid(base) && tp_retirement_batch_contract_valid(other) &&
            base->input_count == other->input_count && base->exit_status == other->exit_status &&
            base->metrics_bytes_max == other->metrics_bytes_max && !strcmp(base->target, "x86_64-linux") &&
            !strcmp(other->target, base->target) && !strcmp(other->allocator, base->allocator) &&
            !strcmp(other->metrics, base->metrics) &&
            bq_retirement_correctness_digest(group->command_sha256[0]) &&
            bq_retirement_correctness_digest(group->command_sha256[1]);
        uint32_t members = 0;
        char const* key = NULL;
        for (uint32_t i = 0; ok && i < base->input_count; i += 1)
        {
            TpRetirementBatchInput const* left = &base->inputs[i];
            TpRetirementBatchInput const* right = &other->inputs[i];
            ok = left->member == right->member && left->row == right->row &&
                !strcmp(left->fixture, right->fixture) && !strcmp(left->status, right->status) &&
                !strcmp(left->error, right->error) && !strcmp(left->diagnostic_sha256, right->diagnostic_sha256) &&
                bq_retirement_correctness_text_equal(left->artifact, right->artifact);
            if (ok && left->member)
            {
                uint32_t row = left->row;
                ok = bq_retirement_correctness_timed_object(gate, row) && !assigned_workspace[row] &&
                    (members || !g || row > previous_first) &&
                    bq_retirement_correctness_digest(gate->trusted_rows[row].batch_key_sha256) &&
                    (!key || !strcmp(key, gate->trusted_rows[row].batch_key_sha256));
                if (ok && !key) key = gate->trusted_rows[row].batch_key_sha256;
                for (uint32_t side = 0; ok && side < 2; side += 1)
                {
                    TpRetirementBatchInput const* input = side ? right : left;
                    ok = bq_retirement_correctness_equal(gate->trusted_rows[row].compiler_command_sha256[side],
                             group->command_sha256[side]) &&
                         bq_retirement_correctness_equal(gate->facts[row].side[side].compiler_command_sha256,
                             group->command_sha256[side]) &&
                         !strcmp(gate->facts[row].side[side].artifact_sha256, input->object_sha256);
                }
                if (ok)
                {
                    if (!members) previous_first = row;
                    assigned_workspace[row] = 1;
                    members += 1;
                }
            }
            else if (ok && left->row != TP_RETIREMENT_BATCH_NO_ROW)
            {
                /* Members precede controls, so the group's key is known. */
                ok = bq_retirement_correctness_control(gate, group, i, key) && !assigned_workspace[left->row];
                if (ok) assigned_workspace[left->row] = 2;
            }
        }
        ok = ok && members && key;
        if (ok) keys[g] = key;
        members_total += members;
    }
    /* No two groups share a configuration and recipe key. */
    if (ok && count > 1)
    {
        qsort(keys, count, sizeof(*keys), bq_retirement_correctness_key_order);
        for (uint32_t g = 1; ok && g < count; g += 1) ok = strcmp(keys[g - 1], keys[g]) != 0;
    }
    free(keys);
    /* Every batch control row is claimed by exactly one frozen control. */
    for (uint32_t row = 0; ok && row < gate->prepared.rows; row += 1)
        if (gate->trusted_rows[row].batch_control) ok = assigned_workspace[row] == 2;
    for (uint32_t row = 0; ok && row < gate->prepared.rows; row += 1)
        if (bq_retirement_correctness_timed_object(gate, row))
        {
            ok = assigned_workspace[row] == 1;
            timed_objects += 1;
        }
    ok = ok && timed_objects == members_total;
    char snapshot[65];
    ok = ok && bq_retirement_correctness_batch_digest(gate, groups, count, snapshot);
    if (ok)
    {
        gate->batch_groups = groups;
        gate->batch_group_count = count;
        gate->batches_frozen = 1;
        memcpy(gate->batch_groups_sha256, snapshot, sizeof(snapshot));
    }
    else if (gate) gate->failed = 1;
    return ok;
}

bool bq_retirement_correctness_finish(BqRetirementCorrectness* gate)
{
    bool ok = gate && !gate->failed && !gate->finished &&
        gate->checks_done == gate->check_count && gate->rows_done == gate->prepared.rows &&
        gate->seen_kinds == gate->required_kinds && bq_retirement_correctness_batches_unchanged(gate);
    if (ok)
    {
        sha256_finish_hex(&gate->checks_hash, gate->checks_sha256);
        bq_retirement_correctness_seal(gate, gate->sealed_sha256);
        gate->finished = 1;
    }
    else if (gate) gate->failed = 1;
    return ok;
}

bool bq_retirement_correctness_ready(BqRetirementCorrectness const* gate)
{
    bool ok = gate && gate->finished && !gate->failed &&
        gate->checks_done == gate->check_count && gate->rows_done == gate->prepared.rows &&
        gate->seen_kinds == gate->required_kinds && bq_retirement_correctness_batches_unchanged(gate);
    char digest[65];
    if (ok)
    {
        bq_retirement_correctness_seal(gate, digest);
        ok = !strcmp(digest, gate->sealed_sha256);
    }
    return ok;
}
