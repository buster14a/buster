// Trusted native admission policy for disabled #3212 sampling research.
// Included after compiler_profile_qualification_freeze.c. The existing hosted
// adapter collects authenticated API facts; it does not select profiles or
// declare qualification. Entry: compiler_sampling_admission_validate.
// Map: *_fields rejects ambiguous TSV; *_history bounds all prior attempts;
// *_self_test exercises admission without executing any benchmark process.

typedef struct CompilerSamplingAdmission CompilerSamplingAdmission;
struct CompilerSamplingAdmission
{
    String8 phase;
    String8 family;
    String8 freeze_revision;
    String8 freeze_sha256;
    String8 campaign_parent;
    String8 parent_freeze_revision;
    String8 protocol_sha256;
    String8 history_since;
    String8 reason;
    u64 packet;
    u64 reservation_seconds;
    bool valid;
};

typedef enum CompilerSamplingConfigField
{
    SAMPLING_CONFIG_SCHEMA,
    SAMPLING_CONFIG_STATE,
    SAMPLING_CONFIG_FREEZE_REVISION,
    SAMPLING_CONFIG_FREEZE_SHA,
    SAMPLING_CONFIG_PARENT_SHA,
    SAMPLING_CONFIG_PARENT_REVISION,
    SAMPLING_CONFIG_PROTOCOL_SHA,
    SAMPLING_CONFIG_HISTORY_SINCE,
    SAMPLING_CONFIG_REPOSITORY,
    SAMPLING_CONFIG_OWNER_LOGIN,
    SAMPLING_CONFIG_OWNER_ID,
    SAMPLING_CONFIG_COUNT,
} CompilerSamplingConfigField;

typedef enum CompilerSamplingFactField
{
    SAMPLING_FACT_SCHEMA,
    SAMPLING_FACT_REPOSITORY,
    SAMPLING_FACT_REQUEST_RUN,
    SAMPLING_FACT_REQUEST_ATTEMPT,
    SAMPLING_FACT_EXECUTOR_RUN,
    SAMPLING_FACT_EXECUTOR_ATTEMPT,
    SAMPLING_FACT_REQUEST_HEAD,
    SAMPLING_FACT_TRUSTED_REVISION,
    SAMPLING_FACT_OWNER_LOGIN,
    SAMPLING_FACT_OWNER_ID,
    SAMPLING_FACT_ACTOR_LOGIN,
    SAMPLING_FACT_ACTOR_ID,
    SAMPLING_FACT_TRIGGERING_LOGIN,
    SAMPLING_FACT_TRIGGERING_ID,
    SAMPLING_FACT_PULL_AUTHOR_LOGIN,
    SAMPLING_FACT_PULL_AUTHOR_ID,
    SAMPLING_FACT_REQUEST_REPOSITORY,
    SAMPLING_FACT_HEAD_REPOSITORY,
    SAMPLING_FACT_PULL_REPOSITORY,
    SAMPLING_FACT_PULL_STATE,
    SAMPLING_FACT_PARENT_COUNT,
    SAMPLING_FACT_FRESH_PARENT_0,
    SAMPLING_FACT_FRESH_PARENT_1,
    SAMPLING_FACT_COUNT,
} CompilerSamplingFactField;

typedef enum CompilerSamplingHistoryField
{
    SAMPLING_HISTORY_PHASE,
    SAMPLING_HISTORY_PACKET,
    SAMPLING_HISTORY_REQUEST_RUN,
    SAMPLING_HISTORY_REQUEST_ATTEMPT,
    SAMPLING_HISTORY_EXECUTOR_RUN,
    SAMPLING_HISTORY_EXECUTOR_ATTEMPT,
    SAMPLING_HISTORY_STATE,
    SAMPLING_HISTORY_WALL_US,
    SAMPLING_HISTORY_CAMPAIGN,
    SAMPLING_HISTORY_FREEZE_REVISION,
    SAMPLING_HISTORY_ACTOR_LOGIN,
    SAMPLING_HISTORY_ACTOR_ID,
    SAMPLING_HISTORY_TRIGGERING_LOGIN,
    SAMPLING_HISTORY_TRIGGERING_ID,
    SAMPLING_HISTORY_PULL_AUTHOR_LOGIN,
    SAMPLING_HISTORY_PULL_AUTHOR_ID,
    SAMPLING_HISTORY_COUNT,
} CompilerSamplingHistoryField;

#define BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES (128u * 1024u)
#define BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_ROWS 43u

BUSTER_GLOBAL_LOCAL bool compiler_sampling_admission_decimal(String8 text, u64* output)
{
    bool result = text.pointer && text.length && text.length <= 18 && (text.length == 1 || text.pointer[0] != '0');
    u64 value = 0;
    for (u64 i = 0; result && i < text.length; i += 1)
    {
        u8 byte = text.pointer[i];
        result = byte >= '0' && byte <= '9';
        if (result) value = value * 10 + (u64)(byte - '0');
    }
    if (result) *output = value;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_admission_fields(String8 text, SliceString8 names, String8* values)
{
    bool result = text.pointer && text.length && text.length <= 16384 && names.length && names.length < 64;
    u64 seen = 0;
    for (u64 begin = 0; result && begin < text.length;)
    {
        u64 end = begin, tab = text.length;
        while (result && end < text.length && text.pointer[end] != '\n')
        {
            u8 byte = text.pointer[end];
            if (byte == '\t')
            {
                result = tab == text.length;
                tab = end;
            }
            else result = byte >= 32 && byte <= 126;
            end += 1;
        }
        result = result && end < text.length && tab > begin && tab < end && tab - begin <= 64 && end - tab <= 513;
        bool found = false;
        if (result)
        {
            String8 name = string_slice(text, begin, tab);
            for (u64 i = 0; !found && i < names.length; i += 1)
            {
                if (string_equal(name, names.pointer[i]))
                {
                    found = true;
                    String8 value = string_slice(text, tab + 1, end);
                    result = value.length && !(seen & (1ull << i));
                    if (result)
                    {
                        values[i] = value;
                        seen |= 1ull << i;
                    }
                }
            }
        }
        result = result && found;
        begin = end + 1;
    }
    result = result && seen == (1ull << names.length) - 1;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_admission_timestamp(String8 text)
{
    bool result = text.pointer && text.length == 20;
    String8 pattern = S8("0000-00-00T00:00:00Z");
    for (u64 i = 0; result && i < pattern.length; i += 1)
    {
        u8 byte = text.pointer[i], wanted = pattern.pointer[i];
        result = wanted == '0' ? byte >= '0' && byte <= '9' : byte == wanted;
    }
    if (result)
    {
        u64 year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
        // Fixed decimal pairs allow leading zero; the general ID parser does not.
        for (u64 i = 0; i < 4; i += 1) year = year * 10 + (u64)(text.pointer[i] - '0');
        month = (u64)(text.pointer[5] - '0') * 10 + (u64)(text.pointer[6] - '0');
        day = (u64)(text.pointer[8] - '0') * 10 + (u64)(text.pointer[9] - '0');
        hour = (u64)(text.pointer[11] - '0') * 10 + (u64)(text.pointer[12] - '0');
        minute = (u64)(text.pointer[14] - '0') * 10 + (u64)(text.pointer[15] - '0');
        second = (u64)(text.pointer[17] - '0') * 10 + (u64)(text.pointer[18] - '0');
        u64 days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) days[2] = 29;
        result = year >= 2020 && month >= 1 && month <= 12 && day >= 1 && day <= days[month] &&
            hour <= 23 && minute <= 59 && second <= 59;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_admission_history(String8 history, String8* config, String8* facts,
    String8 phase, u64 packet)
{
    String8 header = S8("phase\tpacket\trequest_run_id\trequest_run_attempt\texecutor_run_id\texecutor_run_attempt\tstate\tphysical_wall_us\tcampaign\tfreeze_revision\tactor_login\tactor_id\ttriggering_login\ttriggering_id\tpull_author_login\tpull_author_id\n");
    bool result = history.pointer && history.length >= header.length &&
        history.length <= BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES &&
        string_equal(string_slice(history, 0, header.length), header);
    u64 pilot_count = 0, confirm_count = 0, pilot_reserved = 0, confirm_reserved = 0, rows = 0;
    String8 request_ids[BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_ROWS] = {0};
    String8 executor_ids[BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_ROWS] = {0};
    for (u64 begin = header.length; result && begin < history.length;)
    {
        String8 values[SAMPLING_HISTORY_COUNT] = {0};
        u64 column = 0, field_begin = begin, end = begin;
        while (result && end < history.length && history.pointer[end] != '\n')
        {
            u8 byte = history.pointer[end];
            if (byte == '\t')
            {
                result = column < SAMPLING_HISTORY_COUNT && end > field_begin;
                if (result) values[column++] = string_slice(history, field_begin, end);
                field_begin = end + 1;
            }
            else result = byte >= 32 && byte <= 126 && end - field_begin < 256;
            end += 1;
        }
        result = result && end < history.length && column == SAMPLING_HISTORY_COUNT - 1 &&
            end > field_begin && rows < BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_ROWS;
        if (result) values[column] = string_slice(history, field_begin, end);
        u64 row_packet = 0, request_id = 0, executor_id = 0, wall = 0;
        result = result && compiler_sampling_admission_decimal(values[SAMPLING_HISTORY_PACKET], &row_packet) &&
            compiler_sampling_admission_decimal(values[SAMPLING_HISTORY_REQUEST_RUN], &request_id) && request_id &&
            compiler_sampling_admission_decimal(values[SAMPLING_HISTORY_EXECUTOR_RUN], &executor_id) && executor_id &&
            compiler_sampling_admission_decimal(values[SAMPLING_HISTORY_WALL_US], &wall) && wall &&
            string_equal(values[SAMPLING_HISTORY_REQUEST_ATTEMPT], S8("1")) &&
            string_equal(values[SAMPLING_HISTORY_EXECUTOR_ATTEMPT], S8("1")) &&
            string_equal(values[SAMPLING_HISTORY_STATE], S8("complete")) &&
            !string_equal(values[SAMPLING_HISTORY_REQUEST_RUN], facts[SAMPLING_FACT_REQUEST_RUN]) &&
            !string_equal(values[SAMPLING_HISTORY_EXECUTOR_RUN], facts[SAMPLING_FACT_EXECUTOR_RUN]);
        for (u64 previous = 0; result && previous < rows; previous += 1)
        {
            result = !string_equal(values[SAMPLING_HISTORY_REQUEST_RUN], request_ids[previous]) &&
                !string_equal(values[SAMPLING_HISTORY_EXECUTOR_RUN], executor_ids[previous]);
        }
        if (result)
        {
            request_ids[rows] = values[SAMPLING_HISTORY_REQUEST_RUN];
            executor_ids[rows] = values[SAMPLING_HISTORY_EXECUTOR_RUN];
            rows += 1;
        }
        for (u64 role = SAMPLING_HISTORY_ACTOR_LOGIN; result && role < SAMPLING_HISTORY_COUNT; role += 2)
        {
            result = string_equal(values[role], config[SAMPLING_CONFIG_OWNER_LOGIN]) &&
                string_equal(values[role + 1], config[SAMPLING_CONFIG_OWNER_ID]);
        }
        CompilerSamplingPacket planned = compiler_sampling_schedule(values[SAMPLING_HISTORY_PHASE], row_packet);
        result = result && planned.valid && wall <= planned.reservation_seconds * 1000000ull;
        bool pilot = string_equal(values[SAMPLING_HISTORY_PHASE], S8("pilot"));
        bool confirming = string_equal(phase, S8("confirm"));
        if (result && pilot)
        {
            result = !confirm_count && row_packet == pilot_count &&
                string_equal(values[SAMPLING_HISTORY_CAMPAIGN], config[confirming ? SAMPLING_CONFIG_PARENT_SHA : SAMPLING_CONFIG_FREEZE_SHA]) &&
                string_equal(values[SAMPLING_HISTORY_FREEZE_REVISION], config[confirming ? SAMPLING_CONFIG_PARENT_REVISION : SAMPLING_CONFIG_FREEZE_REVISION]);
            pilot_count += 1;
            pilot_reserved += planned.reservation_seconds;
        }
        else if (result)
        {
            result = confirming && pilot_count == 3 && row_packet == confirm_count &&
                string_equal(values[SAMPLING_HISTORY_CAMPAIGN], config[SAMPLING_CONFIG_FREEZE_SHA]) &&
                string_equal(values[SAMPLING_HISTORY_FREEZE_REVISION], config[SAMPLING_CONFIG_FREEZE_REVISION]);
            confirm_count += 1;
            confirm_reserved += planned.reservation_seconds;
        }
        result = result && pilot_reserved <= 10800 && confirm_reserved <= 43200;
        begin = end + 1;
    }
    CompilerSamplingPacket current = compiler_sampling_schedule(phase, packet);
    bool confirming = string_equal(phase, S8("confirm"));
    result = result && current.valid && (confirming ? pilot_count == 3 && confirm_count == packet :
        pilot_count == packet && confirm_count == 0) &&
        (confirming ? confirm_reserved + current.reservation_seconds <= 43200 :
            pilot_reserved + current.reservation_seconds <= 10800);
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingAdmission compiler_sampling_admission_validate(Arena* arena, String8 allowlist,
    String8 marker, String8 facts_text, String8 history, String8 freeze_text)
{
    CompilerSamplingAdmission result = {.reason = S8("disabled-or-invalid-sampling-admission")};
    String8 config_names[] = {S8("schema"), S8("state"), S8("freeze_revision"), S8("freeze_sha256"), S8("campaign_parent"),
        S8("parent_freeze_revision"), S8("protocol_sha256"), S8("history_since"), S8("repository"), S8("owner_login"), S8("owner_id")};
    String8 config[SAMPLING_CONFIG_COUNT] = {0};
    String8 fact_names[] = {S8("schema"), S8("repository"), S8("request_run_id"), S8("request_run_attempt"),
        S8("executor_run_id"), S8("executor_run_attempt"), S8("request_head"), S8("trusted_revision"),
        S8("owner_login"), S8("owner_id"), S8("actor_login"), S8("actor_id"), S8("triggering_login"), S8("triggering_id"),
        S8("pull_author_login"), S8("pull_author_id"), S8("request_repository"), S8("request_head_repository"),
        S8("pull_repository"), S8("pull_state"), S8("parent_count"), S8("fresh_parent_0"), S8("fresh_parent_1")};
    String8 facts[SAMPLING_FACT_COUNT] = {0};
    bool valid = compiler_sampling_admission_fields(allowlist, (SliceString8)BUSTER_ARRAY_TO_SLICE(config_names), config) &&
        string_equal(config[SAMPLING_CONFIG_SCHEMA], S8("buster-main-sampling-admission-v1")) &&
        (string_equal(config[SAMPLING_CONFIG_STATE], S8("pilot")) || string_equal(config[SAMPLING_CONFIG_STATE], S8("confirm"))) &&
        string_equal(config[SAMPLING_CONFIG_REPOSITORY], S8("buster14a/buster")) &&
        string_equal(config[SAMPLING_CONFIG_OWNER_LOGIN], S8("davidgmbb")) &&
        string_equal(config[SAMPLING_CONFIG_OWNER_ID], S8("39247043")) &&
        compiler_sampling_hex(config[SAMPLING_CONFIG_FREEZE_REVISION], 40) &&
        compiler_sampling_hex(config[SAMPLING_CONFIG_FREEZE_SHA], 64) &&
        compiler_sampling_hex(config[SAMPLING_CONFIG_PROTOCOL_SHA], 64) &&
        compiler_sampling_admission_timestamp(config[SAMPLING_CONFIG_HISTORY_SINCE]);
    bool confirming = string_equal(config[SAMPLING_CONFIG_STATE], S8("confirm"));
    valid = valid && (confirming ? compiler_sampling_hex(config[SAMPLING_CONFIG_PARENT_SHA], 64) &&
        compiler_sampling_hex(config[SAMPLING_CONFIG_PARENT_REVISION], 40) :
        string_equal(config[SAMPLING_CONFIG_PARENT_SHA], S8("-")) &&
        string_equal(config[SAMPLING_CONFIG_PARENT_REVISION], S8("-")));
    CompilerSamplingFreeze freeze = {0};
    if (valid)
    {
        freeze = compiler_sampling_freeze_parse(freeze_text);
        String8 actual_freeze_sha = stage_object_sha256_bytes(arena, (u8*)freeze_text.pointer, freeze_text.length);
        valid = freeze.valid && string_equal(actual_freeze_sha, config[SAMPLING_CONFIG_FREEZE_SHA]) &&
            string_equal(freeze.phase, config[SAMPLING_CONFIG_STATE]) &&
            string_equal(freeze.campaign_parent, config[SAMPLING_CONFIG_PARENT_SHA]) &&
            string_equal(freeze.protocol_sha256, config[SAMPLING_CONFIG_PROTOCOL_SHA]);
    }
    valid = valid && compiler_sampling_admission_fields(facts_text, (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_names), facts) &&
        string_equal(facts[SAMPLING_FACT_SCHEMA], S8("buster-main-sampling-github-facts-v1")) &&
        string_equal(facts[SAMPLING_FACT_PULL_STATE], S8("open")) &&
        compiler_sampling_hex(facts[SAMPLING_FACT_REQUEST_HEAD], 40) &&
        compiler_sampling_hex(facts[SAMPLING_FACT_TRUSTED_REVISION], 40) &&
        string_equal(facts[SAMPLING_FACT_REQUEST_ATTEMPT], S8("1")) &&
        string_equal(facts[SAMPLING_FACT_EXECUTOR_ATTEMPT], S8("1"));
    u64 request_run = 0, executor_run = 0, parent_count = 0;
    valid = valid && compiler_sampling_admission_decimal(facts[SAMPLING_FACT_REQUEST_RUN], &request_run) && request_run &&
        compiler_sampling_admission_decimal(facts[SAMPLING_FACT_EXECUTOR_RUN], &executor_run) && executor_run &&
        compiler_sampling_admission_decimal(facts[SAMPLING_FACT_PARENT_COUNT], &parent_count) &&
        (parent_count == 1 || parent_count == 2);
    u64 repositories[] = {SAMPLING_FACT_REPOSITORY, SAMPLING_FACT_REQUEST_REPOSITORY,
        SAMPLING_FACT_HEAD_REPOSITORY, SAMPLING_FACT_PULL_REPOSITORY};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(repositories); i += 1)
    {
        valid = string_equal(facts[repositories[i]], config[SAMPLING_CONFIG_REPOSITORY]);
    }
    for (u64 role = SAMPLING_FACT_OWNER_LOGIN; valid && role <= SAMPLING_FACT_PULL_AUTHOR_LOGIN; role += 2)
    {
        valid = string_equal(facts[role], config[SAMPLING_CONFIG_OWNER_LOGIN]) &&
            string_equal(facts[role + 1], config[SAMPLING_CONFIG_OWNER_ID]);
    }
    // The full fresh marker is matched for every Git parent, not just one.
    // Parsing never accepts a second active line or a generic profile prefix.
    if (valid)
    {
        if (marker.length && marker.pointer[marker.length - 1] == '\n') marker = string_slice(marker, 0, marker.length - 1);
        String8 prefix = confirming ? S8("profile: compiler-main-sampling-confirm-v1 packet: ") :
            S8("profile: compiler-main-sampling-pilot-v1 packet: ");
        valid = marker.length > prefix.length && marker.length <= 256 &&
            string_equal(string_slice(marker, 0, prefix.length), prefix);
        u64 separator = prefix.length;
        while (valid && separator < marker.length && marker.pointer[separator] != ' ') separator += 1;
        String8 suffix = S8(" freeze: ");
        valid = valid && separator > prefix.length && separator + suffix.length + 40 == marker.length &&
            string_equal(string_slice(marker, separator, separator + suffix.length), suffix);
        if (valid)
        {
            valid = compiler_sampling_admission_decimal(string_slice(marker, prefix.length, separator), &result.packet) &&
                string_equal(string_slice(marker, separator + suffix.length, marker.length), config[SAMPLING_CONFIG_FREEZE_REVISION]) &&
                string_equal(facts[SAMPLING_FACT_FRESH_PARENT_0], marker) &&
                (parent_count == 2 ? string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], marker) :
                    string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], S8("-")));
        }
    }
    CompilerSamplingPacket planned = compiler_sampling_schedule(config[SAMPLING_CONFIG_STATE], result.packet);
    valid = valid && planned.valid && compiler_sampling_admission_history(history, config, facts,
        config[SAMPLING_CONFIG_STATE], result.packet);
    if (valid)
    {
        result.phase = config[SAMPLING_CONFIG_STATE];
        result.family = planned.family;
        result.reservation_seconds = planned.reservation_seconds;
        result.freeze_revision = config[SAMPLING_CONFIG_FREEZE_REVISION];
        result.freeze_sha256 = config[SAMPLING_CONFIG_FREEZE_SHA];
        result.campaign_parent = config[SAMPLING_CONFIG_PARENT_SHA];
        result.parent_freeze_revision = config[SAMPLING_CONFIG_PARENT_REVISION];
        result.protocol_sha256 = config[SAMPLING_CONFIG_PROTOCOL_SHA];
        result.history_since = config[SAMPLING_CONFIG_HISTORY_SINCE];
        result.reason = S8("authenticated-bounded-research-only");
    }
    result.valid = valid;
    return result;
}

// Hosted fixtures format typed data, then exercise the same strict validator.
BUSTER_GLOBAL_LOCAL String8 compiler_sampling_admission_fixture_fields(Arena* arena, SliceString8 names, SliceString8 values)
{
    String8List rows = {0};
    for (u64 i = 0; i < names.length; i += 1)
    {
        string8_list_push(arena, &rows, string_format(arena, S8("{S8}\t{S8}\n"), names.pointer[i], values.pointer[i]));
    }
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, rows), false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_admission_self_test(Arena* arena)
{
    String8 a = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    String8 b = S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    String8 c = S8("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    String8 a40 = string_slice(a, 0, 40), b40 = string_slice(b, 0, 40), c40 = string_slice(c, 0, 40);
    CompilerSamplingFreeze freeze = {.schema = S8("buster-main-sampling-freeze-v1"), .phase = S8("pilot"),
        .campaign_parent = S8("-"), .base = a40, .base_tree = b40, .request_head = c40,
        .baseline_revision = a40, .aa_candidate_revision = a40, .ab1_revision = b40, .ab2_revision = c40,
        .protocol_sha256 = a, .lab_sha256 = a, .python_sha256 = a, .driver_sha256 = a, .closure_sha256 = a,
        .baseline_sha256 = a, .aa_candidate_sha256 = a, .ab1_candidate_sha256 = b, .ab2_candidate_sha256 = c,
        .candidate_pairs = S8("0"), .selected_candidate = S8("exploratory"),
        .calibration_ab1_low_percent = S8("-"), .calibration_ab1_high_percent = S8("-"),
        .calibration_ab2_low_percent = S8("-"), .calibration_ab2_high_percent = S8("-")};
    String8 freeze_text = compiler_sampling_freeze_fixture(arena, freeze);
    String8 freeze_sha = stage_object_sha256_bytes(arena, (u8*)freeze_text.pointer, freeze_text.length);
    String8 names[] = {S8("schema"), S8("state"), S8("freeze_revision"), S8("freeze_sha256"), S8("campaign_parent"),
        S8("parent_freeze_revision"), S8("protocol_sha256"), S8("history_since"), S8("repository"), S8("owner_login"), S8("owner_id")};
    String8 config_values[] = {S8("buster-main-sampling-admission-v1"), S8("pilot"), b40, freeze_sha, S8("-"),
        S8("-"), a, S8("2026-10-09T00:00:00Z"), S8("buster14a/buster"), S8("davidgmbb"), S8("39247043")};
    String8 marker = string_format(arena, S8("profile: compiler-main-sampling-pilot-v1 packet: 0 freeze: {S8}"), b40);
    String8 fact_names[] = {S8("schema"), S8("repository"), S8("request_run_id"), S8("request_run_attempt"),
        S8("executor_run_id"), S8("executor_run_attempt"), S8("request_head"), S8("trusted_revision"),
        S8("owner_login"), S8("owner_id"), S8("actor_login"), S8("actor_id"), S8("triggering_login"), S8("triggering_id"),
        S8("pull_author_login"), S8("pull_author_id"), S8("request_repository"), S8("request_head_repository"),
        S8("pull_repository"), S8("pull_state"), S8("parent_count"), S8("fresh_parent_0"), S8("fresh_parent_1")};
    String8 fact_values[] = {S8("buster-main-sampling-github-facts-v1"), S8("buster14a/buster"), S8("100"), S8("1"),
        S8("200"), S8("1"), c40, a40, S8("davidgmbb"), S8("39247043"), S8("davidgmbb"), S8("39247043"),
        S8("davidgmbb"), S8("39247043"), S8("davidgmbb"), S8("39247043"), S8("buster14a/buster"),
        S8("buster14a/buster"), S8("buster14a/buster"), S8("open"), S8("1"), marker, S8("-")};
    String8 header = S8("phase\tpacket\trequest_run_id\trequest_run_attempt\texecutor_run_id\texecutor_run_attempt\tstate\tphysical_wall_us\tcampaign\tfreeze_revision\tactor_login\tactor_id\ttriggering_login\ttriggering_id\tpull_author_login\tpull_author_id\n");
    SliceString8 config_names = (SliceString8)BUSTER_ARRAY_TO_SLICE(names);
    SliceString8 config_fields = (SliceString8)BUSTER_ARRAY_TO_SLICE(config_values);
    SliceString8 github_names = (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_names);
    SliceString8 github_fields = (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_values);
    String8 config_text = compiler_sampling_admission_fixture_fields(arena, config_names, config_fields);
    String8 facts = compiler_sampling_admission_fixture_fields(arena, github_names, github_fields);
    CompilerSamplingAdmission admitted = compiler_sampling_admission_validate(arena, config_text, marker, facts, header, freeze_text);
    bool result = admitted.valid && admitted.packet == 0 && admitted.reservation_seconds == 3600 &&
        string_equal(admitted.phase, S8("pilot")) && string_equal(admitted.family, S8("aa"));
    u64 config_bad[] = {SAMPLING_CONFIG_STATE, SAMPLING_CONFIG_FREEZE_SHA, SAMPLING_CONFIG_OWNER_LOGIN,
        SAMPLING_CONFIG_OWNER_ID, SAMPLING_CONFIG_REPOSITORY, SAMPLING_CONFIG_HISTORY_SINCE, SAMPLING_CONFIG_PARENT_SHA};
    String8 config_changes[] = {S8("disabled"), a, S8("attacker"), S8("1"), S8("attacker/buster"),
        S8("2026-02-30T00:00:00Z"), a};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(config_bad); i += 1)
    {
        String8 saved = config_values[config_bad[i]];
        config_values[config_bad[i]] = config_changes[i];
        String8 changed = compiler_sampling_admission_fixture_fields(arena, config_names, config_fields);
        result = result && !compiler_sampling_admission_validate(arena, changed, marker, facts, header, freeze_text).valid;
        config_values[config_bad[i]] = saved;
    }
    u64 facts_bad[] = {SAMPLING_FACT_ACTOR_LOGIN, SAMPLING_FACT_ACTOR_ID, SAMPLING_FACT_TRIGGERING_LOGIN,
        SAMPLING_FACT_PULL_AUTHOR_ID, SAMPLING_FACT_REQUEST_REPOSITORY, SAMPLING_FACT_REQUEST_ATTEMPT,
        SAMPLING_FACT_EXECUTOR_ATTEMPT, SAMPLING_FACT_FRESH_PARENT_0, SAMPLING_FACT_PARENT_COUNT};
    String8 facts_changes[] = {S8("attacker"), S8("1"), S8("attacker"), S8("1"), S8("attacker/buster"),
        S8("2"), S8("2"), S8("-"), S8("3")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(facts_bad); i += 1)
    {
        String8 saved = fact_values[facts_bad[i]];
        fact_values[facts_bad[i]] = facts_changes[i];
        String8 changed = compiler_sampling_admission_fixture_fields(arena, github_names, github_fields);
        result = result && !compiler_sampling_admission_validate(arena, config_text, marker, changed, header, freeze_text).valid;
        fact_values[facts_bad[i]] = saved;
    }
    result = result && !compiler_sampling_admission_validate(arena, config_text,
        string_format(arena, S8("{S8}\n{S8}\n"), marker, marker), facts, header, freeze_text).valid &&
        !compiler_sampling_admission_validate(arena, string_format(arena, S8("{S8}state\tpilot\n"), config_text),
            marker, facts, header, freeze_text).valid &&
        !compiler_sampling_admission_validate(arena, config_text, S8("profile: compiler-main-sampling-unknown-v1"),
            facts, header, freeze_text).valid;
    // The next packet needs the exact earlier authenticated owner attempt.
    marker = string_format(arena, S8("profile: compiler-main-sampling-pilot-v1 packet: 1 freeze: {S8}"), b40);
    fact_values[SAMPLING_FACT_FRESH_PARENT_0] = marker;
    facts = compiler_sampling_admission_fixture_fields(arena, github_names, github_fields);
    String8 previous = string_format(arena,
        S8("pilot\t0\t99\t1\t199\t1\tcomplete\t1000000\t{S8}\t{S8}\tdavidgmbb\t39247043\tdavidgmbb\t39247043\tdavidgmbb\t39247043\n"),
        freeze_sha, b40);
    String8 complete = string_format(arena, S8("{S8}{S8}"), header, previous);
    admitted = compiler_sampling_admission_validate(arena, config_text, marker, facts, complete, freeze_text);
    result = result && admitted.valid && admitted.packet == 1 && string_equal(admitted.family, S8("ab1")) &&
        !compiler_sampling_admission_validate(arena, config_text, marker, facts, header, freeze_text).valid &&
        !compiler_sampling_admission_validate(arena, config_text, marker, facts,
            string_format(arena, S8("{S8}{S8}{S8}"), header, previous, previous), freeze_text).valid;
    String8 rejected_states[] = {S8("cancelled"), S8("failed"), S8("invalid"), S8("incomplete"), S8("queued"), S8("not_run")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(rejected_states); i += 1)
    {
        String8 row = string_format(arena,
            S8("{S8}pilot\t0\t99\t1\t-\t1\t{S8}\t-\t{S8}\t{S8}\tdavidgmbb\t39247043\tdavidgmbb\t39247043\tdavidgmbb\t39247043\n"),
            header, rejected_states[i], freeze_sha, b40);
        result = result && !compiler_sampling_admission_validate(arena, config_text, marker, facts, row, freeze_text).valid;
    }
    String8 overrun = string_format(arena,
        S8("{S8}pilot\t0\t99\t1\t199\t1\tcomplete\t3600000001\t{S8}\t{S8}\tdavidgmbb\t39247043\tdavidgmbb\t39247043\tdavidgmbb\t39247043\n"),
        header, freeze_sha, b40);
    result = result && !compiler_sampling_admission_validate(arena, config_text, marker, facts, overrun, freeze_text).valid;
    fact_values[SAMPLING_FACT_PARENT_COUNT] = S8("2");
    fact_values[SAMPLING_FACT_FRESH_PARENT_1] = marker;
    facts = compiler_sampling_admission_fixture_fields(arena, github_names, github_fields);
    result = result && compiler_sampling_admission_validate(arena, config_text, marker, facts, complete, freeze_text).valid;
    fact_values[SAMPLING_FACT_FRESH_PARENT_1] = S8("-");
    facts = compiler_sampling_admission_fixture_fields(arena, github_names, github_fields);
    result = result && !compiler_sampling_admission_validate(arena, config_text, marker, facts, complete, freeze_text).valid;
    // Confirmation has a distinct identity freeze and requires all three
    // completed pilot packets from the exact admitted parent campaign.
    String8 pilot_sha = freeze_sha;
    freeze.phase = S8("confirm");
    freeze.campaign_parent = pilot_sha;
    freeze.candidate_pairs = S8("40");
    freeze.selected_candidate = S8("compiler-main-40pairs-candidate-v1");
    freeze.calibration_ab1_low_percent = S8("2.0");
    freeze.calibration_ab1_high_percent = S8("2.5");
    freeze.calibration_ab2_low_percent = S8("2.0");
    freeze.calibration_ab2_high_percent = S8("2.5");
    freeze_text = compiler_sampling_freeze_fixture(arena, freeze);
    freeze_sha = stage_object_sha256_bytes(arena, (u8*)freeze_text.pointer, freeze_text.length);
    config_values[SAMPLING_CONFIG_STATE] = S8("confirm");
    config_values[SAMPLING_CONFIG_FREEZE_REVISION] = c40;
    config_values[SAMPLING_CONFIG_FREEZE_SHA] = freeze_sha;
    config_values[SAMPLING_CONFIG_PARENT_SHA] = pilot_sha;
    config_values[SAMPLING_CONFIG_PARENT_REVISION] = b40;
    config_text = compiler_sampling_admission_fixture_fields(arena, config_names, config_fields);
    marker = string_format(arena, S8("profile: compiler-main-sampling-confirm-v1 packet: 0 freeze: {S8}"), c40);
    fact_values[SAMPLING_FACT_PARENT_COUNT] = S8("1");
    fact_values[SAMPLING_FACT_FRESH_PARENT_0] = marker;
    facts = compiler_sampling_admission_fixture_fields(arena, github_names, github_fields);
    String8List prior = {0};
    string8_list_push(arena, &prior, header);
    for (u64 packet = 0; packet < 3; packet += 1)
    {
        string8_list_push(arena, &prior, string_format(arena,
            S8("pilot\\t{u64}\\t{u64}\\t1\\t{u64}\\t1\\tcomplete\\t1000000\\t{S8}\\t{S8}\\tdavidgmbb\\t39247043\\tdavidgmbb\\t39247043\\tdavidgmbb\\t39247043\\n"),
            packet, 80 + packet, 180 + packet, pilot_sha, b40));
    }
    String8 prior_complete = string_join_arena(arena, string8_list_to_slice(arena, prior), false);
    admitted = compiler_sampling_admission_validate(arena, config_text, marker, facts, prior_complete, freeze_text);
    result = result && admitted.valid && admitted.packet == 0 && admitted.reservation_seconds == 1440 &&
        string_equal(admitted.phase, S8("confirm")) &&
        !compiler_sampling_admission_validate(arena, config_text, marker, facts, header, freeze_text).valid &&
        !compiler_sampling_admission_validate(arena, config_text, marker, facts, complete, freeze_text).valid;
    String8 saved_parent = config_values[SAMPLING_CONFIG_PARENT_SHA];
    config_values[SAMPLING_CONFIG_PARENT_SHA] = a;
    config_text = compiler_sampling_admission_fixture_fields(arena, config_names, config_fields);
    result = result && !compiler_sampling_admission_validate(arena, config_text, marker, facts, prior_complete, freeze_text).valid;
    config_values[SAMPLING_CONFIG_PARENT_SHA] = saved_parent;
    return result;
}
