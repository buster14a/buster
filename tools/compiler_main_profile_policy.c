// Dormant authenticated MAIN profile policy and immutable campaign inventory.
// Included after sampling controller helpers. Entry: compiler_main_route_main.
// The existing hosted collector supplies API-selected bytes; native C owns
// feature eligibility. A completed report is distinct from either feature gate.
// No command here starts a compiler, measures a host or declares independence.
#ifndef BUSTER_COMPILER_MAIN_PROFILE_POLICY_INCLUDED
#define BUSTER_COMPILER_MAIN_PROFILE_POLICY_INCLUDED
#define BUSTER_MAIN_ROUTE_RECORD_LIMIT (32ull << 10)
#define BUSTER_MAIN_ROUTE_TABLE_LIMIT (128ull << 10)

BUSTER_GLOBAL_LOCAL String8 compiler_main_policy_names[] = {S8("schema"), S8("repository"), S8("state"), S8("measurement_revision"), S8("main_profile"), S8("preparation_policy"), S8("phase_schema"), S8("lab_sha256"), S8("python_path"), S8("python_sha256"), S8("driver_sha256"), S8("compare_sha256"), S8("receipt_sha256"), S8("owned_phase_sha256"), S8("owned_plan_sha256"), S8("certificate_revision"), S8("certificate_sha256"), S8("previous_policy_revision"), S8("trusted_root"), S8("candidate_root"), S8("work_root"), S8("evidence_root")};
BUSTER_GLOBAL_LOCAL String8 compiler_main_fact_names[] = {S8("schema"), S8("repository"), S8("executor_run"), S8("executor_attempt"), S8("policy_revision"), S8("executor_workflow"), S8("executor_event"), S8("executor_branch"), S8("request_run"), S8("request_attempt"), S8("request_head"), S8("request_workflow"), S8("request_event"), S8("request_branch"), S8("request_status"), S8("request_conclusion")};
BUSTER_GLOBAL_LOCAL String8 compiler_main_certificate_names[] = {S8("schema"), S8("state"), S8("repository"), S8("measurement_revision"), S8("profile"), S8("facts_revision"), S8("facts_sha256"), S8("archive_revision"), S8("archive_sha256"), S8("reviews_revision"), S8("reviews_sha256"), S8("acquisition_freeze_revision"), S8("acquisition_freeze_sha256"), S8("pilot_freeze_revision"), S8("pilot_freeze_sha256"), S8("confirmation_freeze_revision"), S8("confirmation_freeze_sha256"), S8("preparation_plan_revision"), S8("preparation_plan_sha256"), S8("utility_plan_revision"), S8("utility_plan_sha256"), S8("lab_sha256"), S8("python_path"), S8("python_sha256"), S8("driver_sha256"), S8("compare_sha256"), S8("receipt_sha256"), S8("owned_phase_sha256"), S8("owned_plan_sha256"), S8("source_base"), S8("source_ab1"), S8("source_ab2"), S8("prepared_sha256"), S8("closure_sha256"), S8("baseline_sha256"), S8("baseline_bytes"), S8("ab1_sha256"), S8("ab1_bytes"), S8("ab2_sha256"), S8("ab2_bytes"), S8("methodology_preparation_policy"), S8("criteria_revision"), S8("criteria_sha256")};
BUSTER_GLOBAL_LOCAL String8 compiler_main_review_names[] = {S8("schema"), S8("measurement_revision"), S8("sampling_independence"), S8("sampling_calibration"), S8("sampling_correctness"), S8("sampling_methodology"), S8("sampling_utility"), S8("closure_equivalence"), S8("closure_utility"), S8("archive_status"), S8("sampling_review_revision"), S8("calibration_review_revision"), S8("correctness_review_revision"), S8("methodology_review_revision"), S8("closure_review_revision"), S8("utility_review_revision"), S8("archive_review_revision")};

typedef struct CompilerMainCampaign CompilerMainCampaign;
struct CompilerMainCampaign
{
    bool valid;
    bool sampling_complete;
    bool sampling_observed;
    bool snapshot_complete;
    bool archive_complete;
    bool sampling_attempted;
    bool sampling_phase_attempted[3];
    bool sampling_acquired;
    bool exploratory_wall_available;
    bool confirmatory_wall_available;
    u64 attempted;
    u64 aa_trials;
    u64 aa_errors;
    u64 ab1_trials;
    u64 ab1_detections;
    u64 ab2_trials;
    u64 ab2_detections;
    u64 long_trials;
    u64 exploratory_wall_us;
    u64 confirmatory_wall_us;
};

typedef struct CompilerMainRoute CompilerMainRoute;
struct CompilerMainRoute
{
    String8 policy[22];
    bool valid;
    bool owned;
    bool sampling_eligible;
    bool snapshot_eligible;
};

BUSTER_GLOBAL_LOCAL bool compiler_main_columns(String8 text, u64* cursor, String8* output, u64 count)
{
    bool result = text.pointer && *cursor < text.length && count && count <= 64;
    u64 begin = *cursor, column = 0;
    for (u64 i = begin; result && i < text.length; i += 1)
    {
        u8 byte = text.pointer[i];
        if (byte == '\t' || byte == '\n')
        {
            result = column < count && i > begin && i - begin <= 512;
            if (result) output[column++] = string_slice(text, begin, i);
            begin = i + 1;
            if (byte == '\n')
            {
                result = result && column == count;
                *cursor = i + 1;
                break;
            }
        }
        else result = byte >= 32 && byte <= 126;
    }
    result = result && column == count && begin == *cursor;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_fields(String8 text, SliceString8 names, String8* output)
{
    bool result = text.pointer && text.length && text.length <= BUSTER_MAIN_ROUTE_RECORD_LIMIT;
    u64 cursor = 0;
    for (u64 i = 0; result && i < names.length; i += 1)
    {
        String8 row[2] = {0};
        result = compiler_main_columns(text, &cursor, row, 2) && string_equal(row[0], names.pointer[i]);
        if (result) output[i] = row[1];
    }
    result = result && cursor == text.length;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_decimal(String8 text, u64* output, bool positive)
{
    u64 value = 0;
    bool result = compiler_sampling_admission_decimal(text, &value) && (!positive || value > 0);
    if (result) *output = value;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_signed(String8 text, s64* output)
{
    bool negative = text.length && text.pointer[0] == '-';
    String8 digits = negative ? string_slice(text, 1, text.length) : text;
    u64 value = 0;
    bool result = compiler_main_decimal(digits, &value, false) && value <= 1000000ull &&
        !(negative && value == 0);
    if (result) *output = negative ? -(s64)value : (s64)value;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_absolute(String8 value)
{
    bool result = value.length > 1 && value.length <= 512 && value.pointer[0] == '/' &&
        value.pointer[value.length - 1] != '/';
    for (u64 i = 1; result && i < value.length; i += 1)
    {
        u8 byte = value.pointer[i];
        result = byte >= 33 && byte <= 126 && byte != '\\' && byte != '"' && byte != '\'' &&
            !(byte == '/' && value.pointer[i - 1] == '/');
    }
    for (u64 begin = 1; result && begin < value.length;)
    {
        u64 end = begin;
        while (end < value.length && value.pointer[end] != '/') end += 1;
        String8 part = string_slice(value, begin, end);
        result = !string_equal(part, S8(".")) && !string_equal(part, S8(".."));
        begin = end + 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_review(String8 state, String8 revision)
{
    bool result = (string_equal(state, S8("accepted")) || string_equal(state, S8("rejected"))) ?
        compiler_sampling_hex(revision, 40) :
        string_equal(state, S8("unreviewed")) && string_equal(revision, S8("-"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_outcomes(String8 text, String8* values, u64 count, bool validations)
{
    bool result = text.length && count <= 5;
    u64 begin = 0, used = 0;
    for (u64 i = 0; result && i <= text.length; i += 1)
    {
        if (i == text.length || text.pointer[i] == ',')
        {
            result = used < count && i > begin;
            String8 value = string_slice(text, begin, i);
            bool known = validations ?
                (string_equal(value, S8("valid")) || string_equal(value, S8("invalid")) || string_equal(value, S8("not-run"))) :
                (string_equal(value, S8("unchanged")) || string_equal(value, S8("below-floor")) ||
                 string_equal(value, S8("slower")) || string_equal(value, S8("faster")) ||
                 string_equal(value, S8("inconclusive")) || string_equal(value, S8("not-run")));
            result = result && known;
            if (result) values[used++] = value;
            begin = i + 1;
        }
    }
    result = result && used == count;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerMainCampaign compiler_main_campaign(String8 text, String8 archive, String8* certificate)
{
    CompilerMainCampaign result = {0};
    bool valid = text.length && text.length <= BUSTER_MAIN_ROUTE_TABLE_LIMIT &&
        archive.length && archive.length <= BUSTER_MAIN_ROUTE_TABLE_LIMIT;
    u64 cursor = 0, archive_cursor = 0;
    String8 header[27] = {0}, archive_header[13] = {0};
    String8 header_names[] = {S8("phase"), S8("packet"), S8("family"), S8("request_run"), S8("request_attempt"), S8("executor_run"), S8("executor_attempt"), S8("policy_revision"), S8("measurement_revision"), S8("freeze_revision"), S8("freeze_sha256"), S8("parent_freeze_sha256"), S8("artifact_id"), S8("artifact_sha256"), S8("artifact_bytes"), S8("job_wall_us"), S8("native_wall_us"), S8("state"), S8("slots"), S8("slot_validations"), S8("corpus_cells"), S8("calibration_low_ppm"), S8("calibration_high_ppm"), S8("short_halfwidth_ppm"), S8("raw_replay_sha256"), S8("terminal_api_sha256"), S8("terminal_api_bytes")};
    String8 archive_names[] = {S8("phase"), S8("packet"), S8("request_run"), S8("executor_run"), S8("artifact_id"), S8("artifact_sha256"), S8("artifact_bytes"), S8("archive_kind"), S8("archive_reference"), S8("archive_version"), S8("archive_sha256"), S8("archive_bytes"), S8("archive_receipt_sha256")};
    valid = valid && compiler_main_columns(text, &cursor, header, 27) &&
        compiler_main_columns(archive, &archive_cursor, archive_header, 13);
    for (u64 i = 0; valid && i < 27; i += 1) valid = string_equal(header[i], header_names[i]);
    for (u64 i = 0; valid && i < 13; i += 1) valid = string_equal(archive_header[i], archive_names[i]);
    bool sampling_complete = true, calibration = true, determinate = true, archive_complete = true, closure_complete = true;
    bool exploratory_available = true, confirmatory_available = true;
    String8 request_ids[46] = {0}, executor_ids[46] = {0}, artifact_ids[46] = {0};
    u64 last_request = 0;
    for (u64 index = 0; valid && index < 46; index += 1)
    {
        String8 row[27] = {0}, saved[13] = {0};
        valid = compiler_main_columns(text, &cursor, row, 27) &&
            compiler_main_columns(archive, &archive_cursor, saved, 13);
        String8 phase = index == 0 ? S8("acquire") : index < 4 ? S8("pilot") :
            index < 44 ? S8("confirm") : index == 44 ? S8("preparation") : S8("utility");
        u64 packet = index < 4 ? (index ? index - 1 : 0) : index < 44 ? index - 4 : 0, observed_packet = 0;
        CompilerSamplingPacket schedule = index < 44 ? compiler_sampling_schedule(phase, packet) : (CompilerSamplingPacket){0};
        String8 family = index < 44 ? schedule.family : phase;
        valid = valid && string_equal(row[0], phase) && compiler_main_decimal(row[1], &observed_packet, false) &&
            observed_packet == packet && string_equal(row[2], family);
        bool complete = string_equal(row[17], S8("complete"));
        bool not_run = string_equal(row[17], S8("not_run"));
        bool known_state = complete || not_run || string_equal(row[17], S8("failed")) ||
            string_equal(row[17], S8("cancelled")) || string_equal(row[17], S8("hostless")) ||
            string_equal(row[17], S8("invalid")) || string_equal(row[17], S8("incomplete"));
        valid = valid && known_state;
        u64 request = 0, executor = 0, artifact = 0, bytes = 0, wall = 0, native_wall = 0, cells = 0;
        if (not_run)
        {
            u64 dash[] = {3,4,5,6,7,8,9,10,11,12,13,15,16,18,19,21,22,23,24,25};
            for (u64 j = 0; valid && j < BUSTER_ARRAY_LENGTH(dash); j += 1)
                valid = string_equal(row[dash[j]], S8("-"));
            valid = valid && string_equal(row[14], S8("0")) && string_equal(row[20], S8("0")) && string_equal(row[26], S8("0"));
        }
        else
        {
            result.attempted += 1;
            result.sampling_attempted = result.sampling_attempted || index < 44;
            if (index < 44) result.sampling_phase_attempted[index == 0 ? 0 : index < 4 ? 1 : 2] = true;
            valid = valid && compiler_main_decimal(row[3], &request, true) && string_equal(row[4], S8("1")) &&
                (compiler_main_decimal(row[5], &executor, true) || (!complete && string_equal(row[5], S8("-")))) &&
                string_equal(row[6], S8("1")) && compiler_sampling_hex(row[7], 40) &&
                string_equal(row[8], certificate[3]) && compiler_sampling_hex(row[9], 40) &&
                compiler_sampling_hex(row[10], 64) && compiler_sampling_hex(row[24], 64);
            valid = valid && request != executor && (index >= 44 || request > last_request);
            if (index < 44) last_request = request;
            for (u64 j = 0; valid && j < index; j += 1)
                valid = !string_equal(row[3], request_ids[j]) &&
                    (!executor || !string_equal(row[5], executor_ids[j]));
            request_ids[index] = row[3]; executor_ids[index] = row[5];
            u64 freeze_ref = index == 0 ? 11 : index < 4 ? 13 : index < 44 ? 15 : index == 44 ? 17 : 19;
            valid = valid && string_equal(row[9], certificate[freeze_ref]) &&
                string_equal(row[10], certificate[freeze_ref + 1]);
            if (index == 0 || index >= 44) valid = valid && string_equal(row[11], S8("-"));
            else valid = valid && string_equal(row[11], certificate[index < 4 ? 12 : 14]);
            u64 terminal_bytes = 0;
            bool terminal_available = compiler_sampling_hex(row[25], 64) &&
                compiler_main_decimal(row[26], &terminal_bytes, true) && terminal_bytes <= (8ull << 20);
            valid = valid && (terminal_available || (string_equal(row[25], S8("-")) && string_equal(row[26], S8("0"))));
            bool has_artifact = compiler_main_decimal(row[12], &artifact, true);
            valid = valid && (has_artifact || (!complete && string_equal(row[12], S8("-"))));
            if (has_artifact)
            {
                valid = valid && compiler_sampling_hex(row[13], 64) &&
                    compiler_main_decimal(row[14], &bytes, true) && bytes <= (2ull << 30);
                for (u64 j = 0; valid && j < index; j += 1) valid = !string_equal(row[12], artifact_ids[j]);
                artifact_ids[index] = row[12];
            }
            else valid = valid && string_equal(row[13], S8("-")) && string_equal(row[14], S8("0"));
            bool wall_available = compiler_main_decimal(row[15], &wall, true);
            bool native_available = compiler_main_decimal(row[16], &native_wall, true);
            valid = valid && (wall_available || (!complete && string_equal(row[15], S8("-")))) &&
                (native_available || (!complete && string_equal(row[16], S8("-")))) &&
                (!complete || native_wall <= wall) && compiler_main_decimal(row[20], &cells, false);
            u64 allocation = index < 44 ? schedule.reservation_seconds : 5400;
            if (wall_available && wall > allocation * 1000000ull) complete = false;
            if (index < 4)
            {
                exploratory_available = exploratory_available && wall_available;
                valid = valid && wall <= ~(u64)0 - result.exploratory_wall_us;
                if (valid) result.exploratory_wall_us += wall;
            }
            else if (index < 44)
            {
                confirmatory_available = confirmatory_available && wall_available;
                valid = valid && wall <= ~(u64)0 - result.confirmatory_wall_us;
                if (valid) result.confirmatory_wall_us += wall;
            }
            if (complete)
            {
                valid = valid && cells == (index == 0 ? 0 : index < 44 ? 12 : index == 44 ? 60 : 24);
                if (index == 0 || index >= 44)
                    valid = valid && string_equal(row[18], S8("-")) && string_equal(row[19], S8("-"));
                else
                {
                    String8 outcomes[5] = {0}, checks[5] = {0};
                    valid = valid && compiler_main_outcomes(row[18], outcomes, schedule.count, false) &&
                        compiler_main_outcomes(row[19], checks, schedule.count, true);
                    for (u64 slot = 0; valid && slot < schedule.count; slot += 1)
                    {
                        complete = complete && string_equal(checks[slot], S8("valid")) &&
                            !string_equal(outcomes[slot], S8("not-run"));
                        bool is_long = string_equal(schedule.slots[slot].profile, S8("compiler-compare-v1"));
                        if (index >= 4 && is_long) result.long_trials += 1;
                        else if (index >= 4)
                        {
                            determinate = determinate && !string_equal(outcomes[slot], S8("inconclusive"));
                            if (string_equal(family, S8("aa")))
                            {
                                result.aa_trials += 1;
                                result.aa_errors += string_equal(outcomes[slot], S8("slower")) ||
                                    string_equal(outcomes[slot], S8("faster"));
                            }
                            else if (string_equal(family, S8("ab1")))
                            {
                                result.ab1_trials += 1;
                                result.ab1_detections += string_equal(outcomes[slot], S8("slower"));
                            }
                            else
                            {
                                result.ab2_trials += 1;
                                result.ab2_detections += string_equal(outcomes[slot], S8("slower"));
                            }
                        }
                    }
                }
                if (index > 0 && index < 4)
                {
                    u64 halfwidth = 0;
                    calibration = calibration && compiler_main_decimal(row[23], &halfwidth, false) && halfwidth <= 10000;
                    if (index > 1)
                    {
                        s64 low = 0, high = 0;
                        calibration = calibration && compiler_main_signed(row[21], &low) &&
                            compiler_main_signed(row[22], &high) && low >= 20000 && high <= 25000 && low <= high;
                    }
                    else valid = valid && string_equal(row[21], S8("-")) && string_equal(row[22], S8("-"));
                }
                else valid = valid && string_equal(row[21], S8("-")) && string_equal(row[22], S8("-")) &&
                    string_equal(row[23], S8("-"));
            }
        }
        u64 joins[] = {0,1,3,5,12,13,14};
        for (u64 j = 0; valid && j < BUSTER_ARRAY_LENGTH(joins); j += 1)
            valid = string_equal(saved[j], row[joins[j]]);
        u64 terminal_bytes = 0;
        bool terminal_available = !artifact && !not_run && compiler_sampling_hex(row[25], 64) &&
            compiler_main_decimal(row[26], &terminal_bytes, true);
        if (!not_run && (artifact || terminal_available))
        {
            u64 archived_bytes = 0, version = 0;
            String8 expected_sha = artifact ? row[13] : row[25];
            u64 expected_bytes = artifact ? bytes : terminal_bytes;
            bool retained = string_equal(saved[7], S8("library")) && saved[8].length == 40 &&
                string_equal(string_slice(saved[8], 0, 8), S8("libfile_")) &&
                compiler_sampling_hex(string_slice(saved[8], 8, saved[8].length), 32) &&
                compiler_main_decimal(saved[9], &version, false) && string_equal(saved[10], expected_sha) &&
                compiler_main_decimal(saved[11], &archived_bytes, true) && archived_bytes == expected_bytes &&
                compiler_sampling_hex(saved[12], 64);
            // Actual transfer/Library metadata acceptance is an independently
            // reviewed protected record, never an opaque URI's syntax alone.
            archive_complete = archive_complete && retained;
        }
        else
        {
            valid = valid && string_equal(saved[7], S8("-")) && string_equal(saved[8], S8("-")) &&
                string_equal(saved[9], S8("-")) && string_equal(saved[10], S8("-")) &&
                string_equal(saved[11], S8("0")) && string_equal(saved[12], S8("-"));
            archive_complete = archive_complete && not_run;
        }
        if (index == 0) result.sampling_acquired = complete;
        if (index < 44) sampling_complete = sampling_complete && complete;
        else closure_complete = closure_complete && complete;
    }
    result.valid = valid && cursor == text.length && archive_cursor == archive.length;
    result.sampling_complete = result.valid && sampling_complete;
    result.exploratory_wall_available = result.valid && exploratory_available;
    result.confirmatory_wall_available = result.valid && confirmatory_available;
    result.sampling_observed = result.sampling_complete && calibration && determinate && exploratory_available && confirmatory_available && result.aa_trials == 80 &&
        result.aa_errors == 0 && result.ab1_trials == 39 && result.ab1_detections == 39 &&
        result.ab2_trials == 39 && result.ab2_detections == 39 && result.long_trials == 9 &&
        result.exploratory_wall_us <= 10800000000ull && result.confirmatory_wall_us <= 43200000000ull;
    result.snapshot_complete = result.valid && closure_complete;
    result.archive_complete = result.valid && archive_complete;
    return result;
}


BUSTER_GLOBAL_LOCAL bool compiler_main_criteria(String8 text, String8* certificate, String8 campaign)
{
    String8 names[] = {S8("schema"), S8("measurement_revision"), S8("preparation_plan_sha256"),
        S8("preparation_raw_replay_sha256"), S8("legacy_immutable_aa_low_ppm"), S8("legacy_immutable_aa_high_ppm"),
        S8("snapshot_immutable_aa_low_ppm"), S8("snapshot_immutable_aa_high_ppm"),
        S8("snapshot_cross_build_aa_low_ppm"), S8("snapshot_cross_build_aa_high_ppm"),
        S8("aa_corpus_regressions"), S8("utility_plan_sha256"), S8("utility_raw_replay_sha256"),
        S8("utility_legacy_wall_us"), S8("utility_snapshot_wall_us"), S8("utility_job_wall_us")};
    String8 values[16] = {0};
    bool result = compiler_main_fields(text, (SliceString8)BUSTER_ARRAY_TO_SLICE(names), values) &&
        string_equal(values[0], S8("buster-compiler-main-profile-criteria-v1")) &&
        string_equal(values[1], certificate[3]) && string_equal(values[2], certificate[18]) &&
        string_equal(values[11], certificate[20]) && compiler_sampling_hex(values[3], 64) &&
        compiler_sampling_hex(values[12], 64);
    u64 cursor = 0;
    String8 row[27] = {0};
    for (u64 index = 0; result && index < 47; index += 1)
    {
        result = compiler_main_columns(campaign, &cursor, row, 27);
        if (index == 45) result = result && string_equal(row[24], values[3]);
        if (index == 46) result = result && string_equal(row[24], values[12]);
    }
    for (u64 low_index = 4; result && low_index < 10; low_index += 2)
    {
        s64 low = 0, high = 0;
        result = compiler_main_signed(values[low_index], &low) && compiler_main_signed(values[low_index + 1], &high) &&
            low >= -5000 && high <= 5000 && low <= high;
    }
    u64 regressions = 0, legacy = 0, snapshot = 0, job = 0;
    result = result && compiler_main_decimal(values[10], &regressions, false) && regressions == 0 &&
        compiler_main_decimal(values[13], &legacy, true) && compiler_main_decimal(values[14], &snapshot, true) &&
        compiler_main_decimal(values[15], &job, true) && legacy <= 5400000000ull && snapshot <= 5400000000ull &&
        job <= 5400000000ull && legacy <= ~(u64)0 - snapshot && legacy <= ~(u64)0 / 2 &&
        job >= legacy + snapshot && job < 2 * legacy;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerMainRoute compiler_main_route_validate(Arena* arena, String8 policy, String8 facts,
    String8 certificate, String8 campaign, String8 archive, String8 reviews, String8 criteria, String8 previous)
{
    CompilerMainRoute result = {0};
    String8 actual[16] = {0};
    bool valid = compiler_main_fields(policy, (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_main_policy_names), result.policy) &&
        compiler_main_fields(facts, (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_main_fact_names), actual);
    String8* p = result.policy;
    valid = valid && string_equal(p[0], S8("buster-compiler-main-profile-routing-v1")) &&
        string_equal(p[1], S8("buster14a/buster")) && string_equal(actual[0], S8("buster-compiler-main-route-github-facts-v1")) &&
        string_equal(actual[1], p[1]) && compiler_sampling_hex(actual[4], 40) &&
        compiler_sampling_hex(actual[10], 40) && string_equal(actual[5], S8(".github/workflows/9700x-direct-bench.yml")) &&
        string_equal(actual[6], S8("workflow_run")) && string_equal(actual[7], S8("main")) &&
        string_equal(actual[11], S8(".github/workflows/9700x-compiler-request.yml")) &&
        string_equal(actual[12], S8("push")) && string_equal(actual[13], S8("main")) &&
        string_equal(actual[14], S8("completed")) && string_equal(actual[15], S8("success"));
    u64 number = 0;
    u64 numeric[] = {2,3,8,9};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(numeric); i += 1)
        valid = compiler_main_decimal(actual[numeric[i]], &number, true);
    bool disabled = string_equal(p[2], S8("disabled"));
    bool owned_long = string_equal(p[2], S8("owned-long"));
    bool short_profile = string_equal(p[2], S8("qualified-40"));
    bool rollback = string_equal(p[2], S8("rollback"));
    valid = valid && (disabled || owned_long || short_profile || rollback);
    bool cert_required = short_profile || string_equal(p[5], S8("snapshot-v1"));
    if (disabled)
    {
        valid = valid && !certificate.length && !campaign.length && !archive.length && !reviews.length && !criteria.length && !previous.length && string_equal(p[4], S8("compiler-compare-v1")) &&
            string_equal(p[5], S8("legacy-rebuild"));
        u64 absent[] = {3,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
        for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(absent); i += 1)
            valid = string_equal(p[absent[i]], S8("-"));
    }
    else
    {
        valid = valid && compiler_sampling_hex(p[3], 40) &&
            string_equal(p[4], short_profile ? S8("compiler-main-40pairs-v1") : S8("compiler-compare-v1")) &&
            string_equal(p[6], S8("buster-compiler-main-owned-phases-v1")) &&
            compiler_main_absolute(p[8]) &&
            (string_equal(p[5], S8("legacy-rebuild")) || string_equal(p[5], S8("snapshot-v1")));
        for (u64 i = 18; valid && i < 22; i += 1) valid = compiler_main_absolute(p[i]);
        u64 hashes[] = {7,9,10,11,12,13,14};
        for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(hashes); i += 1)
            valid = compiler_sampling_hex(p[hashes[i]], 64);
        if (cert_required)
        {
            String8 c[43] = {0}, r[17] = {0};
            valid = valid && compiler_sampling_hex(p[15], 40) && compiler_sampling_hex(p[16], 64) &&
                string_equal(p[16], stage_object_sha256_bytes(arena, (u8*)certificate.pointer, certificate.length)) &&
                compiler_main_fields(certificate, (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_main_certificate_names), c) &&
                compiler_main_fields(reviews, (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_main_review_names), r);
            valid = valid && string_equal(c[0], S8("buster-compiler-main-profile-certificate-v1")) &&
                string_equal(c[1], S8("reviewed")) && string_equal(c[2], p[1]) && string_equal(c[3], p[3]) &&
                string_equal(c[4], S8("compiler-main-40pairs-v1")) &&
                string_equal(c[6], stage_object_sha256_bytes(arena, (u8*)campaign.pointer, campaign.length)) &&
                string_equal(c[8], stage_object_sha256_bytes(arena, (u8*)archive.pointer, archive.length)) &&
                string_equal(c[10], stage_object_sha256_bytes(arena, (u8*)reviews.pointer, reviews.length));
            u64 refs[] = {5,7,9,17,19,29,30,41};
            u64 digests[] = {6,8,10,18,20,21,23,24,25,26,27,28,42};
            for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(refs); i += 1)
                valid = compiler_sampling_hex(c[refs[i]], 40);
            for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(digests); i += 1)
                valid = compiler_sampling_hex(c[digests[i]], 64);
            u64 runtime_p[] = {7,8,9,10,11,12,13,14};
            for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(runtime_p); i += 1)
                valid = string_equal(p[runtime_p[i]], c[21 + i]);
            valid = valid && string_equal(c[42], stage_object_sha256_bytes(arena, (u8*)criteria.pointer, criteria.length)) &&
                string_equal(c[40], p[5]) &&
                string_equal(r[0], S8("buster-compiler-main-profile-reviews-v1")) && string_equal(r[1], p[3]);
            u64 review_ref[] = {10,11,12,13,10,14,15,16};
            for (u64 i = 0; valid && i < 8; i += 1)
                valid = compiler_main_review(r[2 + i], r[review_ref[i]]);
            CompilerMainCampaign observed = valid ? compiler_main_campaign(campaign, archive, c) : (CompilerMainCampaign){0};
            valid = valid && observed.valid;
            for (u64 phase = 0; valid && phase < 3; phase += 1)
            {
                u64 ref = 11 + phase * 2;
                valid = observed.sampling_phase_attempted[phase] ?
                    compiler_sampling_hex(c[ref], 40) && compiler_sampling_hex(c[ref + 1], 64) :
                    string_equal(c[ref], S8("-")) && string_equal(c[ref + 1], S8("-"));
            }
            valid = valid && (observed.sampling_attempted ? compiler_sampling_hex(c[31], 40) : string_equal(c[31], S8("-")));
            u64 sampling_hashes[] = {32,33,34,36,38};
            u64 sampling_sizes[] = {35,37,39};
            for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(sampling_hashes); i += 1)
                valid = observed.sampling_acquired ? compiler_sampling_hex(c[sampling_hashes[i]], 64) :
                    string_equal(c[sampling_hashes[i]], S8("-"));
            for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(sampling_sizes); i += 1)
                valid = observed.sampling_acquired ?
                    compiler_main_decimal(c[sampling_sizes[i]], &number, true) && number <= 536870912ull :
                    string_equal(c[sampling_sizes[i]], S8("-"));
            if (observed.sampling_acquired)
                valid = valid && !string_equal(c[34], c[36]) && !string_equal(c[34], c[38]) && !string_equal(c[36], c[38]);
            bool positive_snapshot = valid && compiler_main_criteria(criteria, c, campaign);
            bool archive_review = valid && observed.archive_complete && string_equal(r[9], S8("accepted"));
            bool sampling_review = archive_review;
            for (u64 i = 2; sampling_review && i <= 6; i += 1) sampling_review = string_equal(r[i], S8("accepted"));
            result.sampling_eligible = sampling_review && observed.sampling_observed;
            result.snapshot_eligible = archive_review && observed.snapshot_complete && positive_snapshot &&
                string_equal(r[7], S8("accepted")) && string_equal(r[8], S8("accepted"));
            valid = valid && (!short_profile || result.sampling_eligible) &&
                (!string_equal(p[5], S8("snapshot-v1")) || result.snapshot_eligible);
        }
        else valid = valid && string_equal(p[15], S8("-")) && string_equal(p[16], S8("-")) &&
            !certificate.length && !campaign.length && !archive.length && !reviews.length && !criteria.length;
        if (rollback)
        {
            String8 old[22] = {0};
            valid = valid && string_equal(p[5], S8("legacy-rebuild")) && compiler_sampling_hex(p[17], 40) &&
                !string_equal(p[17], actual[4]) &&
                compiler_main_fields(previous, (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_main_policy_names), old) &&
                string_equal(old[0], S8("buster-compiler-main-profile-routing-v1")) &&
                (string_equal(old[2], S8("owned-long")) || string_equal(old[2], S8("qualified-40")) || string_equal(old[2], S8("rollback"))) &&
                string_equal(old[3], p[3]);
            u64 stable[] = {1,6,7,8,9,10,11,12,13,14,18,19,20,21};
            for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(stable); i += 1)
                valid = string_equal(p[stable[i]], old[stable[i]]);
        }
        else valid = valid && string_equal(p[17], S8("-")) && !previous.length;
    }
    result.valid = valid;
    result.owned = valid && !disabled;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_route_read(Arena* arena, String8 path, u64 limit, String8* output)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 terminated = string_duplicate_arena(arena, path, true);
    int descriptor = open((char*)terminated.pointer, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat before = {0}, after = {0};
    result = descriptor >= 0 && fstat(descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
        before.st_size >= 0 && (u64)before.st_size <= limit;
    u8* bytes = result ? arena_allocate(arena, u8, (u64)before.st_size + 1) : 0;
    u64 used = 0;
    bool eof = false;
    while (result && !eof)
    {
        ssize_t count = read(descriptor, bytes + used, (u64)before.st_size + 1 - used);
        if (count > 0) { used += (u64)count; result = used <= (u64)before.st_size; }
        else if (count == 0) eof = true;
        else result = errno == EINTR;
    }
    result = result && eof && fstat(descriptor, &after) == 0 && before.st_dev == after.st_dev &&
        before.st_ino == after.st_ino && before.st_mode == after.st_mode && before.st_size == after.st_size &&
        used == (u64)after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
        before.st_mtim.tv_nsec == after.st_mtim.tv_nsec && before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
        before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
    if (descriptor >= 0) result = close(descriptor) == 0 && result;
    if (result) *output = (String8){(char8*)bytes, used};
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(path); BUSTER_UNUSED(limit); BUSTER_UNUSED(output);

#endif
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_main_route_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    bool valid = arguments.length == 10 && string_equal(arguments.pointer[0], S8("--resolve-main-route"));
    String8 data[8] = {0};
    for (u64 i = 0; valid && i < 8; i += 1)
        valid = compiler_main_route_read(arena, arguments.pointer[1 + i],
            i == 3 || i == 4 ? BUSTER_MAIN_ROUTE_TABLE_LIMIT : BUSTER_MAIN_ROUTE_RECORD_LIMIT, &data[i]);
    CompilerMainRoute resolved = valid ? compiler_main_route_validate(arena, data[0], data[1], data[2],
        data[3], data[4], data[5], data[6], data[7]) : (CompilerMainRoute){0};
    if (resolved.valid)
    {
        String8* p = resolved.policy;
        String8 text = string_format(arena,
            S8("main_owned={S8}\nmain_profile={S8}\nmain_preparation_policy={S8}\nmain_phase_schema={S8}\n"
               "main_measurement_revision={S8}\nmain_certificate_revision={S8}\nmain_certificate_sha256={S8}\n"
               "lab_sha256={S8}\npython_path={S8}\npython_sha256={S8}\ndriver_sha256={S8}\n"
               "compare_sha256={S8}\nreceipt_sha256={S8}\nowned_phase_sha256={S8}\nowned_plan_sha256={S8}\ntrusted_root={S8}\ncandidate_root={S8}\nwork_root={S8}\nevidence_root={S8}\n"),
            resolved.owned ? S8("true") : S8("false"), p[4], p[5], p[6], p[3], p[15], p[16],
            p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[18], p[19], p[20], p[21]);
        if (file_write(arguments.pointer[9], BUSTER_SLICE_TO_BYTE_SLICE(text))) result = PROCESS_RESULT_SUCCESS;
    }
    if (result != PROCESS_RESULT_SUCCESS) string_print(S8("COMPILER_MAIN_ROUTE_REFUSED immutable profile eligibility or API facts incomplete\n"));
    return result;
}
// Physical MAIN runtime proof: native reads pinned files before the Python
// comparator starts. The hosted publisher binds this data to original API P/H;
// the publisher's own Python executable is not the measured host executable.
BUSTER_GLOBAL_LOCAL ProcessResult compiler_main_runtime_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    String8 route_names[] = {S8("main_owned"), S8("main_profile"), S8("main_preparation_policy"), S8("main_phase_schema"), S8("main_measurement_revision"), S8("main_certificate_revision"), S8("main_certificate_sha256"), S8("lab_sha256"), S8("python_path"), S8("python_sha256"), S8("driver_sha256"), S8("compare_sha256"), S8("receipt_sha256"), S8("owned_phase_sha256"), S8("owned_plan_sha256"), S8("trusted_root"), S8("candidate_root"), S8("work_root"), S8("evidence_root"), S8("main_policy_revision")};
    String8 proof_names[] = {S8("schema"), S8("measurement_revision"), S8("policy_revision"), S8("executor_run"), S8("executor_attempt"), S8("request_run"), S8("request_attempt"), S8("request_head"), S8("trusted_root"), S8("candidate_root"), S8("work_root"), S8("evidence_root"), S8("python_path"), S8("python_sha256"), S8("python_bytes"), S8("driver_path"), S8("driver_sha256"), S8("driver_bytes"), S8("lab_path"), S8("lab_sha256"), S8("lab_bytes"), S8("compare_path"), S8("compare_sha256"), S8("compare_bytes"), S8("receipt_path"), S8("receipt_sha256"), S8("receipt_bytes"), S8("owned_phase_path"), S8("owned_phase_sha256"), S8("owned_phase_bytes"), S8("owned_plan_path"), S8("owned_plan_sha256"), S8("owned_plan_bytes")};
    String8 route_text = {0}, facts_text = {0};
    String8 route[20] = {0}, facts[16] = {0}, proof[33] = {0};
    bool valid = arguments.length == 9 && string_equal(arguments.pointer[0], S8("--verify-main-runtime")) &&
        compiler_main_route_read(arena, arguments.pointer[1], BUSTER_MAIN_ROUTE_RECORD_LIMIT, &route_text) &&
        compiler_main_route_read(arena, arguments.pointer[2], BUSTER_MAIN_ROUTE_RECORD_LIMIT, &facts_text) &&
        compiler_main_fields(route_text, (SliceString8)BUSTER_ARRAY_TO_SLICE(route_names), route) &&
        compiler_main_fields(facts_text, (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_main_fact_names), facts) &&
        string_equal(route[0], S8("true")) && compiler_sampling_hex(route[4], 40) &&
        compiler_sampling_hex(route[19], 40) && string_equal(route[19], facts[4]) &&
        string_equal(route[3], S8("buster-compiler-main-owned-phases-v1")) &&
        (string_equal(route[1], S8("compiler-compare-v1")) || string_equal(route[1], S8("compiler-main-40pairs-v1"))) &&
        (string_equal(route[2], S8("legacy-rebuild")) || string_equal(route[2], S8("snapshot-v1"))) &&
        string_equal(facts[0], S8("buster-compiler-main-route-github-facts-v1")) &&
        string_equal(facts[1], S8("buster14a/buster"));
    // Borrowing active ownership is permitted only for an exact live ancestor
    // in the existing host context; an UNKNOWN marker never self-clears.
    valid = valid && compiler_experiment_cleanup_guard(arena);
    for (u64 i = 0; valid && i < 4; i += 1)
    {
        String8 actual = os_path_absolute(arena, arguments.pointer[3 + i], true);
        valid = compiler_main_absolute(route[15 + i]) && string_equal(actual, route[15 + i]) &&
            string_equal(os_path_absolute(arena, actual, false), actual);
    }
    String8 paths[7] = {route[8], arguments.pointer[7],
        path_join(arena, route[15], S8("tools/uarch_lab.py")),
        path_join(arena, route[15], S8("tools/bench_direct/compiler_compare.py")),
        path_join(arena, route[15], S8("tools/bench_direct/compiler_receipt.py")),
        path_join(arena, route[15], S8("tools/bench_direct/compiler_owned_phase.py")),
        path_join(arena, route[15], S8("tools/bench_direct/compiler_owned_plan.py"))};
    u64 expected[] = {9,10,7,11,12,13,14};
#if BUSTER_LINUX && !BUSTER_ANDROID
    char executable[4096] = {0};
    ssize_t executable_bytes = valid ? readlink("/proc/self/exe", executable, sizeof(executable) - 1) : -1;
    valid = valid && executable_bytes > 0 && (u64)executable_bytes < sizeof(executable) - 1 &&
        string_equal(os_path_absolute(arena, (String8){(char8*)executable, (u64)executable_bytes}, true),
            os_path_absolute(arena, paths[1], true));
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(paths); i += 1)
    {
        String8 actual = os_path_absolute(arena, paths[i], true), digest = {0};
        struct stat status = {0};
        valid = compiler_main_absolute(actual) && string_equal(actual, paths[i]) &&
            compiler_sampling_controller_hash(arena, actual, &digest, &status) &&
            string_equal(digest, route[expected[i]]) && status.st_size > 0;
        if (valid)
        {
            proof[12 + i * 3] = actual;
            proof[13 + i * 3] = digest;
            proof[14 + i * 3] = string_format(arena, S8("{u64}"), (u64)status.st_size);
        }
    }
#else
    valid = false;
    BUSTER_UNUSED(paths); BUSTER_UNUSED(expected);
#endif
    // All paths must be detached checkout roots or approved siblings. This
    // verifies checkout identity without starting git or trusting an artifact.
    String8 trusted_head = {0}, candidate_head = {0};
    if (valid)
    {
        valid = compiler_main_route_read(arena, path_join(arena, route[15], S8(".git/HEAD")), 64, &trusted_head) &&
            compiler_main_route_read(arena, path_join(arena, route[16], S8(".git/HEAD")), 64, &candidate_head) &&
            trusted_head.length == 41 && trusted_head.pointer[40] == '\n' &&
            string_equal(string_slice(trusted_head, 0, 40), route[4]) &&
            candidate_head.length == 41 && candidate_head.pointer[40] == '\n' &&
            string_equal(string_slice(candidate_head, 0, 40), facts[10]);
    }
    if (valid)
    {
        proof[0] = S8("buster-compiler-main-runtime-v1");
        proof[1] = route[4]; proof[2] = route[19]; proof[3] = facts[2]; proof[4] = facts[3];
        proof[5] = facts[8]; proof[6] = facts[9]; proof[7] = facts[10];
        for (u64 i = 0; i < 4; i += 1) proof[8 + i] = route[15 + i];
        String8 text = compiler_sampling_admission_fixture_fields(arena,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(proof_names), (SliceString8)BUSTER_ARRAY_TO_SLICE(proof));
        if (file_write(arguments.pointer[8], BUSTER_SLICE_TO_BYTE_SLICE(text))) result = PROCESS_RESULT_SUCCESS;
    }
    if (result != PROCESS_RESULT_SUCCESS)
        string_print(S8("COMPILER_MAIN_RUNTIME_REFUSED canonical H/tool pins before comparator launch\n"));
    return result;
}

#endif
