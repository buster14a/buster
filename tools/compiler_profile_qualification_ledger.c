// Versioned sampling schedule and non-retryable reservations. Included by
// compiler_profile_qualification.c. Ledger integrity is not producer authority;
// a publisher must independently join GitHub requests/executor attempts.
typedef struct CompilerSamplingSlot CompilerSamplingSlot;
struct CompilerSamplingSlot
{
    String8 profile;
    u64 ordinal;
};

typedef struct CompilerSamplingPacket CompilerSamplingPacket;
struct CompilerSamplingPacket
{
    String8 family;
    CompilerSamplingSlot slots[5];
    u64 count;
    u64 short_trials;
    u64 reservation_seconds;
    bool valid;
};

BUSTER_GLOBAL_LOCAL CompilerSamplingPacket compiler_sampling_schedule(String8 phase, u64 packet)
{
    CompilerSamplingPacket result = {0};
    String8 families[] = {S8("aa"), S8("ab1"), S8("ab2")};
    if (string_equal(phase, S8("pilot")) && packet < 3)
    {
        result.family = families[packet];
        result.slots[0] = (CompilerSamplingSlot){.profile = S8("compiler-compare-v1")};
        result.slots[1] = (CompilerSamplingSlot){.profile = S8("compiler-main-40pairs-candidate-v1")};
        result.slots[2] = (CompilerSamplingSlot){.profile = S8("compiler-main-80pairs-candidate-v1")};
        result.count = 3;
        result.short_trials = 2;
        result.reservation_seconds = 3600;
        result.valid = true;
    }
    else if (string_equal(phase, S8("confirm")) && packet < 40)
    {
        u64 family = packet % 4 == 1 ? 1 : packet % 4 == 3 ? 2 : 0;
        u64 block = packet / 4;
        u64 count = packet == 37 || packet == 39 ? 3 : 4;
        bool comparator = block < 3 && packet % 4 != 2;
        bool comparator_first = (block + family) % 2 == 0;
        u64 ordinal = family ? block * 4 : (block * 2 + (packet % 4 == 2)) * 4;
        result.family = families[family];
        result.short_trials = count;
        result.reservation_seconds = comparator ? 1440 : 960;
        if (comparator && comparator_first)
        {
            result.slots[result.count++] = (CompilerSamplingSlot){.profile = S8("compiler-compare-v1"), .ordinal = block};
        }
        for (u64 i = 0; i < count; i += 1)
        {
            result.slots[result.count++] = (CompilerSamplingSlot){.profile = S8("compiler-main-40pairs-candidate-v1"), .ordinal = ordinal + i};
        }
        if (comparator && !comparator_first)
        {
            result.slots[result.count++] = (CompilerSamplingSlot){.profile = S8("compiler-compare-v1"), .ordinal = block};
        }
        result.valid = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_hex(String8 value, u64 length)
{
    bool result = value.length == length;
    for (u64 i = 0; result && i < value.length; i += 1)
    {
        u8 byte = value.pointer[i];
        result = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_sampling_reservation(Arena* arena, String8 campaign, String8 phase, u64 packet)
{
    CompilerSamplingPacket schedule = compiler_sampling_schedule(phase, packet);
    String8 result = string_format(arena,
        S8("schema\tbuster-main-sampling-reservation-v1\ncampaign\t{S8}\nphase\t{S8}\npacket\t{u64}\n"
           "family\t{S8}\nreservation_seconds\t{u64}\nretry_allowed\tfalse\n"),
        campaign, phase, packet, schedule.family, schedule.reservation_seconds);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_ledger_claim(Arena* arena, String8 root, String8 campaign,
                                                       String8 phase, u64 packet, String8* claim)
{
    bool result = compiler_sampling_hex(campaign, 64) && compiler_sampling_schedule(phase, packet).valid;
    String8 directory = path_join(arena, root, campaign);
    OsDirectoryCreateResult root_result = {0}, campaign_result = {0};
    if (result)
    {
        root_result = os_make_directory(root);
        result = !root_result.error.v && (root_result.created || root_result.existing_directory) &&
            generate_path_kind(arena, root) == GENERATE_PATH_DIRECTORY;
    }
    if (result)
    {
        campaign_result = os_make_directory_exclusive(directory);
        bool existing = campaign_result.already_exists && generate_path_kind(arena, directory) == GENERATE_PATH_DIRECTORY;
        result = (!campaign_result.error.v && campaign_result.created) || existing;
        String8 identity = string_format(arena, S8("schema\tbuster-main-sampling-ledger-v1\ncampaign\t{S8}\n"), campaign);
        String8 identity_path = path_join(arena, directory, S8("campaign.tsv"));
        if (result && campaign_result.created)
        {
            result = file_write(identity_path, BUSTER_SLICE_TO_BYTE_SLICE(identity));
        }
        else if (result)
        {
            String8 observed = BYTE_SLICE_TO_STRING(8, file_read(arena, identity_path, (FileReadOptions){.map_required = 0}));
            result = string_equal(identity, observed);
        }
    }
    MuslDirectoryEntry* entries = 0;
    u64 entry_count = 0;
    if (result) result = musl_list_directory(arena, directory, &entries, &entry_count);
    for (u64 entry = 0; result && entry < entry_count; entry += 1)
    {
        bool known = string_equal(entries[entry].name, S8("campaign.tsv")) && !entries[entry].is_directory;
        String8 phases[] = {S8("pilot"), S8("confirm")};
        for (u64 phase_i = 0; !known && phase_i < BUSTER_ARRAY_LENGTH(phases); phase_i += 1)
        {
            u64 count = phase_i ? 40 : 3;
            for (u64 previous = 0; !known && previous < count; previous += 1)
            {
                String8 name = string_format(arena, S8("{S8}-{u64}"), phases[phase_i], previous);
                known = entries[entry].is_directory && string_equal(entries[entry].name, name);
            }
        }
        result = known;
    }
    u64 reserved = 0;
    u64 packet_count = string_equal(phase, S8("pilot")) ? 3 : 40;
    u64 budget = string_equal(phase, S8("pilot")) ? 10800 : 43200;
    for (u64 i = 0; result && i < packet_count; i += 1)
    {
        String8 previous = path_join(arena, directory, string_format(arena, S8("{S8}-{u64}"), phase, i));
        GeneratePathKind kind = generate_path_kind(arena, previous);
        if (kind == GENERATE_PATH_DIRECTORY)
        {
            String8 expected = compiler_sampling_reservation(arena, campaign, phase, i);
            String8 observed = BYTE_SLICE_TO_STRING(8, file_read(arena, path_join(arena, previous, S8("reservation.tsv")),
                (FileReadOptions){.map_required = 0}));
            result = string_equal(expected, observed) && i != packet;
            reserved += compiler_sampling_schedule(phase, i).reservation_seconds;
        }
        else if (kind != GENERATE_PATH_MISSING)
        {
            result = false;
        }
    }
    CompilerSamplingPacket schedule = compiler_sampling_schedule(phase, packet);
    result = result && reserved <= budget && schedule.reservation_seconds <= budget - reserved;
    if (result)
    {
        *claim = path_join(arena, directory, string_format(arena, S8("{S8}-{u64}"), phase, packet));
        OsDirectoryCreateResult created = os_make_directory_exclusive(*claim);
        result = !created.error.v && created.created && !created.already_exists;
        if (result)
        {
            String8 record = compiler_sampling_reservation(arena, campaign, phase, packet);
            result = file_write(path_join(arena, *claim, S8("reservation.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(record));
        }
    }
    // Even an incomplete claim is retained: no deletion, retry or replacement.
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_schedule_self_test(Arena* arena)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    u64 counts[3] = {0}, comparators[3] = {0}, reserve = 0, first = 0;
    String8 families[] = {S8("aa"), S8("ab1"), S8("ab2")};
    for (u64 packet = 0; packet < 40; packet += 1)
    {
        CompilerSamplingPacket plan = compiler_sampling_schedule(S8("confirm"), packet);
        if (!plan.valid || plan.count > 5 || plan.short_trials > 4) result = PROCESS_RESULT_FAILED;
        reserve += plan.reservation_seconds;
        for (u64 family = 0; family < 3; family += 1)
        {
            if (string_equal(plan.family, families[family]))
            {
                counts[family] += plan.short_trials;
                for (u64 slot = 0; slot < plan.count; slot += 1)
                {
                    if (string_equal(plan.slots[slot].profile, S8("compiler-compare-v1")))
                    {
                        comparators[family] += 1;
                        first += slot == 0;
                    }
                }
            }
        }
    }
    bool good = counts[0] == 80 && counts[1] == 39 && counts[2] == 39 &&
        comparators[0] == 3 && comparators[1] == 3 && comparators[2] == 3 && first == 5 && reserve == 42720 &&
        !compiler_sampling_schedule(S8("confirm"), 40).valid && !compiler_sampling_schedule(S8("pilot"), 3).valid;
    String8 directory = {0};
    bool owned = summary_self_test_claim_directory(arena, S8("sampling-ledger"), &directory);
    good = good && owned;
    if (owned)
    {
        String8 campaign = S8("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        String8 claim = {0};
        good = good && compiler_sampling_ledger_claim(arena, directory, campaign, S8("pilot"), 0, &claim);
        good = good && !compiler_sampling_ledger_claim(arena, directory, campaign, S8("pilot"), 0, &claim);
        String8 reservation = path_join(arena, claim, S8("reservation.tsv"));
        good = good && file_write(reservation, BUSTER_SLICE_TO_BYTE_SLICE(S8("tampered\n")));
        good = good && !compiler_sampling_ledger_claim(arena, directory, campaign, S8("pilot"), 1, &claim);
        good = remove_path_recursive(arena, directory) && good;
    }
    if (!good) result = PROCESS_RESULT_FAILED;
    string_print(S8("COMPILER_SAMPLING_LEDGER_SELF_TEST status={S8} aa={u64} ab1={u64} ab2={u64} reserved_seconds={u64}\n"),
        result == PROCESS_RESULT_SUCCESS ? S8("pass") : S8("fail"), counts[0], counts[1], counts[2], reserve);
    return result;
}
