// Identity freeze for disabled sampling research; included after the ledger.
// Ownership: trusted build driver. Entry: compiler_sampling_freeze_parse and
// compiler_sampling_freeze_matches_family. Map: *_decimal validates exact
// calibration bounds; *_self_test exercises parser and actual-family binding.
// Parsed String8 fields borrow the input buffer; keep it alive through use.
// Successful parsing never authenticates, admits or qualifies a campaign.

typedef struct CompilerSamplingFreeze CompilerSamplingFreeze;
struct CompilerSamplingFreeze
{
    String8 schema;
    String8 phase;
    String8 campaign_parent;
    String8 base;
    String8 base_tree;
    String8 request_head;
    String8 baseline_revision;
    String8 aa_candidate_revision;
    String8 ab1_revision;
    String8 ab2_revision;
    String8 protocol_sha256;
    String8 lab_sha256;
    String8 python_sha256;
    String8 driver_sha256;
    String8 closure_sha256;
    String8 baseline_sha256;
    String8 aa_candidate_sha256;
    String8 ab1_candidate_sha256;
    String8 ab2_candidate_sha256;
    String8 candidate_pairs;
    String8 selected_candidate;
    String8 calibration_ab1_low_percent;
    String8 calibration_ab1_high_percent;
    String8 calibration_ab2_low_percent;
    String8 calibration_ab2_high_percent;
    u64 calibration_micropercent[4];
    bool valid;
};

typedef struct CompilerSamplingFreezeActual CompilerSamplingFreezeActual;
struct CompilerSamplingFreezeActual
{
    String8 phase;
    String8 campaign_parent;
    String8 base;
    String8 base_tree;
    String8 request_head;
    String8 baseline_revision;
    String8 candidate_revision;
    String8 protocol_sha256;
    String8 lab_sha256;
    String8 python_sha256;
    String8 driver_sha256;
    String8 closure_sha256;
    String8 baseline_sha256;
    String8 candidate_sha256;
};

#define BUSTER_SAMPLING_FREEZE_MAX_BYTES (16u * 1024u)
#define BUSTER_SAMPLING_CALIBRATION_MIN 2000000u
#define BUSTER_SAMPLING_CALIBRATION_MAX 2500000u

BUSTER_GLOBAL_LOCAL bool compiler_sampling_freeze_decimal(String8 text, u64* output)
{
    bool result = text.length >= 1 && text.length <= 8 && text.pointer && text.pointer[0] == '2';
    u64 value = BUSTER_SAMPLING_CALIBRATION_MIN;
    if (result && text.length > 1)
    {
        result = text.length >= 3 && text.pointer[1] == '.';
        u64 place = 100000;
        for (u64 i = 2; result && i < text.length; i += 1)
        {
            u8 byte = text.pointer[i];
            result = byte >= '0' && byte <= '9' && place;
            if (result)
            {
                value += (u64)(byte - '0') * place;
                place /= 10;
            }
        }
    }
    result = result && value >= BUSTER_SAMPLING_CALIBRATION_MIN && value <= BUSTER_SAMPLING_CALIBRATION_MAX;
    if (result) *output = value;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingFreeze compiler_sampling_freeze_parse(String8 text)
{
    CompilerSamplingFreeze result = {0};
    String8 names[] = {S8("schema"), S8("phase"), S8("campaign_parent"), S8("base"), S8("base_tree"), S8("request_head"),
        S8("baseline_revision"), S8("aa_candidate_revision"), S8("ab1_revision"), S8("ab2_revision"),
        S8("protocol_sha256"), S8("lab_sha256"), S8("python_sha256"), S8("driver_sha256"), S8("closure_sha256"),
        S8("baseline_sha256"), S8("aa_candidate_sha256"), S8("ab1_candidate_sha256"), S8("ab2_candidate_sha256"),
        S8("candidate_pairs"), S8("selected_candidate"), S8("calibration_ab1_low_percent"),
        S8("calibration_ab1_high_percent"), S8("calibration_ab2_low_percent"), S8("calibration_ab2_high_percent")};
    String8* values[] = {&result.schema, &result.phase, &result.campaign_parent, &result.base, &result.base_tree,
        &result.request_head, &result.baseline_revision, &result.aa_candidate_revision, &result.ab1_revision,
        &result.ab2_revision, &result.protocol_sha256, &result.lab_sha256, &result.python_sha256, &result.driver_sha256,
        &result.closure_sha256, &result.baseline_sha256, &result.aa_candidate_sha256, &result.ab1_candidate_sha256,
        &result.ab2_candidate_sha256, &result.candidate_pairs, &result.selected_candidate,
        &result.calibration_ab1_low_percent, &result.calibration_ab1_high_percent,
        &result.calibration_ab2_low_percent, &result.calibration_ab2_high_percent};
    bool valid = text.pointer && text.length && text.length <= BUSTER_SAMPLING_FREEZE_MAX_BYTES;
    u64 seen = 0;
    for (u64 begin = 0; valid && begin < text.length;)
    {
        u64 end = begin, tab = text.length;
        while (valid && end < text.length && text.pointer[end] != '\n')
        {
            u8 byte = text.pointer[end];
            if (byte == '\t')
            {
                valid = tab == text.length;
                tab = end;
            }
            else valid = byte >= 32 && byte <= 126;
            end += 1;
        }
        valid = valid && end < text.length && tab > begin && tab < end && end - tab <= 257 && tab - begin <= 64;
        bool matched = false;
        if (valid)
        {
            String8 name = string_slice(text, begin, tab);
            String8 value = string_slice(text, tab + 1, end);
            for (u64 i = 0; !matched && i < BUSTER_ARRAY_LENGTH(names); i += 1)
            {
                if (string_equal(name, names[i]))
                {
                    matched = true;
                    valid = value.length && !(seen & (1ull << i));
                    if (valid)
                    {
                        *values[i] = value;
                        seen |= 1ull << i;
                    }
                }
            }
        }
        valid = valid && matched;
        begin = end + 1;
    }
    valid = valid && seen == (1ull << BUSTER_ARRAY_LENGTH(names)) - 1 &&
        string_equal(result.schema, S8("buster-main-sampling-freeze-v1"));
    bool pilot = string_equal(result.phase, S8("pilot"));
    bool confirm = string_equal(result.phase, S8("confirm"));
    valid = valid && (pilot || confirm) &&
        (pilot ? string_equal(result.campaign_parent, S8("-")) : compiler_sampling_hex(result.campaign_parent, 64));
    String8 revisions[] = {result.base, result.base_tree, result.request_head, result.baseline_revision,
        result.aa_candidate_revision, result.ab1_revision, result.ab2_revision};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(revisions); i += 1)
    {
        valid = compiler_sampling_hex(revisions[i], 40);
    }
    String8 digests[] = {result.protocol_sha256, result.lab_sha256, result.python_sha256, result.driver_sha256,
        result.closure_sha256, result.baseline_sha256, result.aa_candidate_sha256,
        result.ab1_candidate_sha256, result.ab2_candidate_sha256};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(digests); i += 1)
    {
        valid = compiler_sampling_hex(digests[i], 64);
    }
    valid = valid && string_equal(result.baseline_revision, result.base) &&
        string_equal(result.aa_candidate_revision, result.base) &&
        string_equal(result.aa_candidate_sha256, result.baseline_sha256) &&
        !string_equal(result.ab1_revision, result.base) && !string_equal(result.ab2_revision, result.base) &&
        !string_equal(result.ab1_revision, result.ab2_revision) &&
        !string_equal(result.ab1_candidate_sha256, result.baseline_sha256) &&
        !string_equal(result.ab2_candidate_sha256, result.baseline_sha256) &&
        !string_equal(result.ab1_candidate_sha256, result.ab2_candidate_sha256) &&
        (pilot ? string_equal(result.candidate_pairs, S8("0")) && string_equal(result.selected_candidate, S8("exploratory")) :
            string_equal(result.candidate_pairs, S8("40")) && string_equal(result.selected_candidate, S8("buster-main-sampling-p40")));
    String8 calibration[] = {result.calibration_ab1_low_percent, result.calibration_ab1_high_percent,
        result.calibration_ab2_low_percent, result.calibration_ab2_high_percent};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(calibration); i += 1)
    {
        valid = pilot ? string_equal(calibration[i], S8("-")) :
            compiler_sampling_freeze_decimal(calibration[i], &result.calibration_micropercent[i]);
    }
    valid = valid && (pilot || (result.calibration_micropercent[0] <= result.calibration_micropercent[1] &&
        result.calibration_micropercent[2] <= result.calibration_micropercent[3]));
    result.valid = valid;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_freeze_matches_family(CompilerSamplingFreeze freeze,
    CompilerSamplingFreezeActual actual, String8 family)
{
    String8 frozen[] = {freeze.phase, freeze.campaign_parent, freeze.base, freeze.base_tree, freeze.request_head,
        freeze.baseline_revision, freeze.protocol_sha256, freeze.lab_sha256, freeze.python_sha256,
        freeze.driver_sha256, freeze.closure_sha256, freeze.baseline_sha256};
    String8 observed[] = {actual.phase, actual.campaign_parent, actual.base, actual.base_tree, actual.request_head,
        actual.baseline_revision, actual.protocol_sha256, actual.lab_sha256, actual.python_sha256,
        actual.driver_sha256, actual.closure_sha256, actual.baseline_sha256};
    bool result = freeze.valid;
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(frozen); i += 1)
    {
        result = string_equal(frozen[i], observed[i]);
    }
    String8 candidate_revision = {0}, candidate_digest = {0};
    if (string_equal(family, S8("aa")))
    {
        candidate_revision = freeze.aa_candidate_revision;
        candidate_digest = freeze.aa_candidate_sha256;
    }
    else if (string_equal(family, S8("ab1")))
    {
        candidate_revision = freeze.ab1_revision;
        candidate_digest = freeze.ab1_candidate_sha256;
    }
    else if (string_equal(family, S8("ab2")))
    {
        candidate_revision = freeze.ab2_revision;
        candidate_digest = freeze.ab2_candidate_sha256;
    }
    result = result && candidate_revision.length && candidate_digest.length &&
        string_equal(actual.candidate_revision, candidate_revision) &&
        string_equal(actual.candidate_sha256, candidate_digest);
    return result;
}

// Pure fixture serialization; the runner's actual evidence is still hashed.
BUSTER_GLOBAL_LOCAL String8 compiler_sampling_freeze_fixture(Arena* arena, CompilerSamplingFreeze fixture)
{
    String8 names[] = {S8("schema"), S8("phase"), S8("campaign_parent"), S8("base"), S8("base_tree"), S8("request_head"),
        S8("baseline_revision"), S8("aa_candidate_revision"), S8("ab1_revision"), S8("ab2_revision"),
        S8("protocol_sha256"), S8("lab_sha256"), S8("python_sha256"), S8("driver_sha256"), S8("closure_sha256"),
        S8("baseline_sha256"), S8("aa_candidate_sha256"), S8("ab1_candidate_sha256"), S8("ab2_candidate_sha256"),
        S8("candidate_pairs"), S8("selected_candidate"), S8("calibration_ab1_low_percent"),
        S8("calibration_ab1_high_percent"), S8("calibration_ab2_low_percent"), S8("calibration_ab2_high_percent")};
    String8 values[] = {fixture.schema, fixture.phase, fixture.campaign_parent, fixture.base, fixture.base_tree,
        fixture.request_head, fixture.baseline_revision, fixture.aa_candidate_revision, fixture.ab1_revision,
        fixture.ab2_revision, fixture.protocol_sha256, fixture.lab_sha256, fixture.python_sha256, fixture.driver_sha256,
        fixture.closure_sha256, fixture.baseline_sha256, fixture.aa_candidate_sha256, fixture.ab1_candidate_sha256,
        fixture.ab2_candidate_sha256, fixture.candidate_pairs, fixture.selected_candidate,
        fixture.calibration_ab1_low_percent, fixture.calibration_ab1_high_percent,
        fixture.calibration_ab2_low_percent, fixture.calibration_ab2_high_percent};
    String8List rows = {0};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        string8_list_push(arena, &rows, string_format(arena, S8("{S8}\t{S8}\n"), names[i], values[i]));
    }
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, rows), false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_freeze_self_test(Arena* arena)
{
    String8 a = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    String8 b = S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    String8 c = S8("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    String8 a40 = string_slice(a, 0, 40), b40 = string_slice(b, 0, 40), c40 = string_slice(c, 0, 40);
    CompilerSamplingFreeze fixture = {.schema = S8("buster-main-sampling-freeze-v1"), .phase = S8("pilot"),
        .campaign_parent = S8("-"), .base = a40, .base_tree = b40, .request_head = c40,
        .baseline_revision = a40, .aa_candidate_revision = a40, .ab1_revision = b40, .ab2_revision = c40,
        .protocol_sha256 = a, .lab_sha256 = a, .python_sha256 = a, .driver_sha256 = a, .closure_sha256 = a,
        .baseline_sha256 = a, .aa_candidate_sha256 = a, .ab1_candidate_sha256 = b, .ab2_candidate_sha256 = c,
        .candidate_pairs = S8("0"), .selected_candidate = S8("exploratory"),
        .calibration_ab1_low_percent = S8("-"), .calibration_ab1_high_percent = S8("-"),
        .calibration_ab2_low_percent = S8("-"), .calibration_ab2_high_percent = S8("-")};
    String8 pilot_text = compiler_sampling_freeze_fixture(arena, fixture);
    CompilerSamplingFreeze pilot = compiler_sampling_freeze_parse(pilot_text);
    bool result = pilot.valid;
    fixture.phase = S8("confirm");
    fixture.campaign_parent = a;
    fixture.candidate_pairs = S8("40");
    fixture.selected_candidate = S8("buster-main-sampling-p40");
    fixture.calibration_ab1_low_percent = S8("2.000001");
    fixture.calibration_ab1_high_percent = S8("2.5");
    fixture.calibration_ab2_low_percent = S8("2.1");
    fixture.calibration_ab2_high_percent = S8("2.500000");
    String8 confirm_text = compiler_sampling_freeze_fixture(arena, fixture);
    CompilerSamplingFreeze confirm = compiler_sampling_freeze_parse(confirm_text);
    CompilerSamplingFreezeActual actual = {.phase = fixture.phase, .campaign_parent = fixture.campaign_parent,
        .base = fixture.base, .base_tree = fixture.base_tree, .request_head = fixture.request_head,
        .baseline_revision = fixture.baseline_revision, .candidate_revision = fixture.ab1_revision,
        .protocol_sha256 = fixture.protocol_sha256, .lab_sha256 = fixture.lab_sha256, .python_sha256 = fixture.python_sha256,
        .driver_sha256 = fixture.driver_sha256, .closure_sha256 = fixture.closure_sha256,
        .baseline_sha256 = fixture.baseline_sha256, .candidate_sha256 = fixture.ab1_candidate_sha256};
    result = result && confirm.valid && confirm.calibration_micropercent[0] == 2000001 &&
        compiler_sampling_freeze_matches_family(confirm, actual, S8("ab1")) &&
        !compiler_sampling_freeze_matches_family(confirm, actual, S8("aa")) &&
        !compiler_sampling_freeze_matches_family(confirm, actual, S8("ab2")) &&
        !compiler_sampling_freeze_matches_family(confirm, actual, S8("unknown"));
    actual.candidate_revision = fixture.ab2_revision;
    actual.candidate_sha256 = fixture.ab2_candidate_sha256;
    result = result && compiler_sampling_freeze_matches_family(confirm, actual, S8("ab2"));
    actual.candidate_revision = fixture.aa_candidate_revision;
    actual.candidate_sha256 = fixture.aa_candidate_sha256;
    result = result && compiler_sampling_freeze_matches_family(confirm, actual, S8("aa"));
    String8* actual_fields[] = {&actual.phase, &actual.campaign_parent, &actual.base, &actual.base_tree, &actual.request_head,
        &actual.baseline_revision, &actual.protocol_sha256, &actual.lab_sha256, &actual.python_sha256,
        &actual.driver_sha256, &actual.closure_sha256, &actual.baseline_sha256, &actual.candidate_revision, &actual.candidate_sha256};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(actual_fields); i += 1)
    {
        String8 saved = *actual_fields[i];
        *actual_fields[i] = S8("tampered");
        result = result && !compiler_sampling_freeze_matches_family(confirm, actual, S8("aa"));
        *actual_fields[i] = saved;
    }
    result = result && !compiler_sampling_freeze_parse(string_slice(confirm_text, 0, confirm_text.length - 1)).valid &&
        !compiler_sampling_freeze_parse(string_format(arena, S8("{S8}unknown\tvalue\n"), confirm_text)).valid &&
        !compiler_sampling_freeze_parse(string_format(arena, S8("{S8}phase\tconfirm\n"), confirm_text)).valid &&
        !compiler_sampling_freeze_parse(S8("schema\tbuster-main-sampling-freeze-v1\n")).valid &&
        !compiler_sampling_freeze_parse(S8("schema\tbuster-main-sampling-freeze-v1\textra\n")).valid &&
        !compiler_sampling_freeze_parse(S8("schema\tbuster-main-sampling-freeze-v1\r\n")).valid &&
        !compiler_sampling_freeze_parse((String8){0}).valid;
    String8 invalid_calibration[] = {S8("1.999999"), S8("2.500001"), S8("2."), S8("+2.1"), S8("2e0"),
        S8("nan"), S8(" 2.1"), S8("2.0000001"), S8("-"), S8("2.1\t")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(invalid_calibration); i += 1)
    {
        u64 value = 0;
        result = result && !compiler_sampling_freeze_decimal(invalid_calibration[i], &value);
    }
    CompilerSamplingFreeze changed = fixture;
    String8* changed_fields[] = {&changed.phase, &changed.campaign_parent, &changed.base, &changed.baseline_revision,
        &changed.aa_candidate_revision, &changed.aa_candidate_sha256, &changed.ab1_revision, &changed.ab2_revision,
        &changed.ab1_candidate_sha256, &changed.ab2_candidate_sha256, &changed.candidate_pairs, &changed.selected_candidate,
        &changed.calibration_ab1_low_percent, &changed.calibration_ab1_high_percent};
    String8 changes[] = {S8("pilot"), S8("-"), S8("bad"), b40, b40, b, a40, b40, a, b, S8("80"),
        S8("exploratory"), S8("2.4"), S8("2.1")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(changed_fields); i += 1)
    {
        String8 saved = *changed_fields[i];
        *changed_fields[i] = changes[i];
        // Raising only the lower bound to 2.4 remains valid against 2.5;
        // force the accompanying high below it to exercise reversal.
        String8 saved_high = changed.calibration_ab1_high_percent;
        if (i == 12) changed.calibration_ab1_high_percent = S8("2.1");
        if (i == 13) changed.calibration_ab1_low_percent = S8("2.4");
        result = result && !compiler_sampling_freeze_parse(compiler_sampling_freeze_fixture(arena, changed)).valid;
        changed.calibration_ab1_low_percent = fixture.calibration_ab1_low_percent;
        changed.calibration_ab1_high_percent = saved_high;
        *changed_fields[i] = saved;
    }
    return result;
}
