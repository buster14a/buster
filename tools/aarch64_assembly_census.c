// Native fixture-derived AArch64 constant-instruction differential census.
// Included by build.c after the other native tools. The corpus is deliberately
// constant-only: symbolic instructions are printed as explicit exclusions and
// remain covered by the separate relocation fixtures and link witnesses.
#include <stdio.h>
#if BUSTER_WINDOWS
#include <windows.h>
#else
#include <errno.h>
#include <dirent.h>
#include <sys/wait.h>
#endif

#define A64C_MAX_FIXTURES 4096u
#define A64C_MAX_LINES 100000u
#define A64C_SET_CAPACITY 262144u
#define A64C_BATCH_LINES 128u
#define A64C_OPTIMIZATION_COUNT 3u
#define A64C_MAX_CHILDREN 20000u
#define A64C_MAX_CAPTURE_BYTES BUSTER_MB(1)
#define A64C_MAX_CAPTURE_TOTAL BUSTER_MB(2)
#define A64C_MAX_TOTAL_CAPTURED BUSTER_MB(512)
#define A64C_DEFAULT_TIMEOUT_SECONDS 30u
#define A64C_MAX_RUN_SECONDS 1800u

typedef enum A64CChildKind
{
    A64C_CHILD_FAILURE,
    A64C_CHILD_SUCCESS,
    A64C_CHILD_NORMAL_NONZERO,
} A64CChildKind;

typedef enum A64CRefusalKind
{
    A64C_REFUSAL_UNKNOWN,
    A64C_REFUSAL_DOCUMENTED,
} A64CRefusalKind;

typedef struct A64CChild A64CChild;
struct A64CChild
{
    String8 output;
    String8 error;
    A64CChildKind kind;
    u32 raw_status;
    u32 spawn_error;
    bool timed_out;
};

typedef struct A64CStringSet A64CStringSet;
struct A64CStringSet
{
    String8* slots;
    u32 count;
    bool full;
};

typedef struct A64CFixtures A64CFixtures;
struct A64CFixtures
{
    String8* paths;
    u32 count;
    bool full;
    bool enumeration_error;
};

typedef struct A64CSettings A64CSettings;
struct A64CSettings
{
    Arena* arena;
    String8 self_executable;
    String8 ide;
    String8 clang;
    String8 llvm_mc;
    String8 llvm_objdump;
    String8 output_directory;
    FILE* report;
    u64 timeout_seconds;
    u64 child_count;
    u64 captured_bytes;
    u64 run_start_us;
    u32 fixture_count;
    u32 skipped_include_count;
    u32 compile_attempt_count;
    u32 compile_failure_count;
    u32 compiled_fixture_count;
    u32 compiled_listing_count;
    u32 assembly_line_count;
    u32 symbolic_exclusion_count;
    u32 unique_line_count;
    u32 compared_line_count;
    u32 documented_refusal_count;
    u32 unknown_refusal_count;
    u32 difference_count;
    bool io_failed;
    bool child_budget_exceeded;
};

BUSTER_GLOBAL_LOCAL bool a64c_space(char8 value)
{
    bool result = value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_alpha(char8 value)
{
    bool result = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    return result;
}

BUSTER_GLOBAL_LOCAL char8 a64c_lower(char8 value)
{
    char8 result = value >= 'A' && value <= 'Z' ? (char8)(value + ('a' - 'A')) : value;
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_equal_folded(String8 left, String8 right)
{
    bool result = left.length == right.length;
    for (u64 index = 0; result && index < left.length; index += 1)
    {
        result &= a64c_lower(left.pointer[index]) == a64c_lower(right.pointer[index]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_contains(String8 text, String8 pattern)
{
    bool result = false;
    if (pattern.length == 0) { result = true; }
    for (u64 index = 0; !result && index + pattern.length <= text.length; index += 1)
    {
        result = memcmp(text.pointer + index, pattern.pointer, (size_t)pattern.length) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int a64c_compare_strings(String8 left, String8 right)
{
    int result = 0;
    u64 count = BUSTER_MIN(left.length, right.length);
    for (u64 index = 0; result == 0 && index < count; index += 1)
    {
        u8 a = (u8)left.pointer[index];
        u8 b = (u8)right.pointer[index];
        result = a < b ? -1 : a > b ? 1 : 0;
    }
    if (result == 0) { result = left.length < right.length ? -1 : left.length > right.length ? 1 : 0; }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 a64c_trim(String8 text)
{
    u64 first = 0;
    u64 last = text.length;
    while (first < last && a64c_space(text.pointer[first])) { first += 1; }
    while (last > first && a64c_space(text.pointer[last - 1])) { last -= 1; }
    String8 result = string_slice(text, first, last);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 a64c_normalize(Arena* arena, String8 text)
{
    String8 result = {0};
    char8* memory = arena_allocate(arena, char8, text.length + 1);
    u64 length = 0;
    bool pending_space = false;
    for (u64 index = 0; index < text.length; index += 1)
    {
        char8 value = text.pointer[index];
        if (a64c_space(value)) { pending_space = length != 0; }
        else
        {
            if (pending_space) { memory[length++] = ' '; }
            memory[length++] = a64c_lower(value);
            pending_space = false;
        }
    }
    memory[length] = 0;
    result = (String8){.pointer = memory, .length = length};
    return result;
}

BUSTER_GLOBAL_LOCAL u64 a64c_hash(String8 text)
{
    u64 result = 1469598103934665603ull;
    for (u64 index = 0; index < text.length; index += 1)
    {
        result ^= (u8)text.pointer[index];
        result *= 1099511628211ull;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_set_insert(A64CStringSet* set, String8 text)
{
    bool result = true;
    u32 mask = A64C_SET_CAPACITY - 1;
    u32 slot = (u32)(a64c_hash(text) & mask);
    bool found = false;
    for (u32 probe = 0; probe < A64C_SET_CAPACITY && !found; probe += 1)
    {
        String8 existing = set->slots[slot];
        if (!existing.length)
        {
            if (set->count >= A64C_MAX_LINES) { set->full = true; result = false; }
            else { set->slots[slot] = text; set->count += 1; }
            found = true;
        }
        else if (string_equal(existing, text)) { found = true; }
        else { slot = (slot + 1) & mask; }
    }
    if (!found) { set->full = true; result = false; }
    return result;
}

BUSTER_GLOBAL_LOCAL void a64c_sort_strings(String8* items, u32 count)
{
    for (u32 start = count / 2; start > 0; start -= 1)
    {
        u32 root = start - 1;
        bool moved = true;
        while (moved)
        {
            u64 child64 = (u64)root * 2 + 1;
            if (child64 >= count) { moved = false; }
            else
            {
                u32 child = (u32)child64;
                if (child + 1 < count && a64c_compare_strings(items[child], items[child + 1]) < 0) { child += 1; }
                if (a64c_compare_strings(items[root], items[child]) < 0)
                {
                    String8 swap = items[root]; items[root] = items[child]; items[child] = swap; root = child;
                }
                else { moved = false; }
            }
        }
    }
    for (u32 end = count; end > 1; end -= 1)
    {
        String8 swap = items[0]; items[0] = items[end - 1]; items[end - 1] = swap;
        u32 root = 0;
        bool moved = true;
        while (moved)
        {
            u64 child64 = (u64)root * 2 + 1;
            if (child64 >= end - 1) { moved = false; }
            else
            {
                u32 child = (u32)child64;
                if (child + 1 < end - 1 && a64c_compare_strings(items[child], items[child + 1]) < 0) { child += 1; }
                if (a64c_compare_strings(items[root], items[child]) < 0)
                {
                    swap = items[root]; items[root] = items[child]; items[child] = swap; root = child;
                }
                else { moved = false; }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL bool a64c_emit(A64CSettings* settings, String8 text)
{
    bool result = true;
    string_print(S8("{S8}"), text);
    if (settings->report) { result = fwrite(text.pointer, 1, (size_t)text.length, settings->report) == text.length; }
    settings->io_failed |= !result;
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_record(A64CSettings* settings, String8 text)
{
    bool result = !settings->report || fwrite(text.pointer, 1, (size_t)text.length, settings->report) == text.length;
    settings->io_failed |= !result;
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_write_text(String8 path, String8 text)
{
    bool result = file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text));
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_normal_exit_nonzero(u32 raw_status)
{
    bool result = false;
#if BUSTER_WINDOWS
    result = raw_status != 0;
#else
    int status = (int)raw_status;
    result = WIFEXITED(status) && WEXITSTATUS(status) != 0;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL A64CChild a64c_child_run(A64CSettings* settings, SliceString8 argv)
{
    A64CChild result = {.kind = A64C_CHILD_FAILURE};
    u64 now = os_now_microseconds();
    u64 elapsed = settings->run_start_us && now >= settings->run_start_us ? now - settings->run_start_us : 0;
    u64 run_limit = (u64)A64C_MAX_RUN_SECONDS * 1000000;
    bool run_expired = settings->run_start_us && elapsed >= run_limit;
    if (settings->child_count >= A64C_MAX_CHILDREN || run_expired) { settings->child_budget_exceeded = true; }
    else
    {
        settings->child_count += 1;
        u64 timeout = settings->timeout_seconds * 1000000;
        if (settings->run_start_us) { timeout = BUSTER_MIN(timeout, run_limit - elapsed); }
        ProcessSpawnOptions options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = true,
            .new_process_group = !BUSTER_WINDOWS,
            .search_path = true,
            .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = A64C_MAX_CAPTURE_BYTES,
                                                [STANDARD_STREAM_ERROR] = A64C_MAX_CAPTURE_BYTES},
                               .total = A64C_MAX_CAPTURE_TOTAL},
            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
        };
        ProcessSpawnResult spawn = os_process_spawn(argv, (SliceString8){0}, (SliceString8){0}, options);
        result.spawn_error = spawn.error.v;
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_deadline(settings->arena, spawn, timeout);
            ByteSlice output = wait.streams[STANDARD_STREAM_OUTPUT];
            ByteSlice error = wait.streams[STANDARD_STREAM_ERROR];
            result.output = (String8){.pointer = (char8*)output.pointer, .length = output.length};
            result.error = (String8){.pointer = (char8*)error.pointer, .length = error.length};
            result.raw_status = wait.platform_status;
            result.timed_out = wait.timed_out != 0;
            bool capture_bad = wait.capture_failed || wait.capture_limit_exceeded || wait.output_truncated ||
                wait.process_tree_cleanup_failed || wait.process_group_reservation_retained || wait.process_group_ownership_lost;
            bool budget_bad = wait.captured_total > A64C_MAX_TOTAL_CAPTURED - BUSTER_MIN(settings->captured_bytes, A64C_MAX_TOTAL_CAPTURED);
            if (!budget_bad) { settings->captured_bytes += wait.captured_total; }
            if (!capture_bad && !budget_bad && !result.timed_out && wait.result == PROCESS_RESULT_SUCCESS)
            {
                result.kind = A64C_CHILD_SUCCESS;
            }
            else if (!capture_bad && !budget_bad && !result.timed_out && a64c_normal_exit_nonzero(wait.platform_status))
            {
                result.kind = A64C_CHILD_NORMAL_NONZERO;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_child_error_text(A64CChild child)
{
    bool result = a64c_contains(child.error, S8("error:")) || a64c_contains(child.output, S8("error:"));
    return result;
}

BUSTER_GLOBAL_LOCAL String8 a64c_diagnostic_excerpt(Arena* arena, A64CChild child)
{
    String8 result = child.error.length ? child.error : child.output;
    if (result.length > 320) { result = string_slice(result, 0, 320); }
    result = a64c_normalize(arena, result);
    return result;
}

BUSTER_GLOBAL_LOCAL A64CRefusalKind a64c_refusal_kind(Arena* arena, String8 instruction, A64CChild child)
{
    // This is the one exact refusal recorded by #2688 and still named as an
    // unsupported operand example by docs/agents/driver.md at the frozen base.
    // Keep it exact: a mnemonic-wide FMLA exemption would hide supported SIMD.
    String8 normalized = a64c_normalize(arena, instruction);
    bool exact_documented_shape = string_equal(normalized, S8("fmla v0.4s, v1.4s, v2.s[0]"));
    A64CRefusalKind result = child.kind == A64C_CHILD_NORMAL_NONZERO && a64c_child_error_text(child) && exact_documented_shape ?
        A64C_REFUSAL_DOCUMENTED : A64C_REFUSAL_UNKNOWN;
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_has_suffix(String8 value, String8 suffix)
{
    bool result = value.length >= suffix.length && string_equal(string_slice(value, value.length - suffix.length, value.length), suffix);
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_fixture_push(Arena* arena, A64CFixtures* fixtures, String8 name)
{
    bool result = true;
    if (a64c_has_suffix(name, S8(".c")))
    {
        if (fixtures->count >= A64C_MAX_FIXTURES) { fixtures->full = true; result = false; }
        else { fixtures->paths[fixtures->count++] = path_join(arena, S8("tests"), name); }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_fixtures_enumerate(Arena* arena, A64CFixtures* fixtures)
{
    bool result = true;
    fixtures->paths = arena_allocate(arena, String8, A64C_MAX_FIXTURES);
#if BUSTER_WINDOWS
    String8 pattern = path_join(arena, S8("tests"), S8("*.c"));
    String8 pattern_z = string_duplicate_arena(arena, pattern, true);
    WIN32_FIND_DATAA data = {0};
    HANDLE handle = FindFirstFileA((char const*)pattern_z.pointer, &data);
    if (handle == INVALID_HANDLE_VALUE) { result = false; }
    else
    {
        bool active = true;
        DWORD enumeration_status = ERROR_NO_MORE_FILES;
        while (active && result)
        {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                result = a64c_fixture_push(arena, fixtures, (String8){.pointer = (char8*)data.cFileName, .length = strlen(data.cFileName)});
            }
            active = FindNextFileA(handle, &data) != 0;
            if (!active) { enumeration_status = GetLastError(); }
        }
        FindClose(handle);
        result &= enumeration_status == ERROR_NO_MORE_FILES;
    }
#else
    DIR* directory = opendir("tests");
    if (!directory) { result = false; }
    else
    {
        struct dirent* entry = 0;
        bool active = true;
        while (active && result)
        {
            errno = 0;
            entry = readdir(directory);
            if (!entry)
            {
                fixtures->enumeration_error = errno != 0;
                active = false;
            }
            else
            {
                String8 name = {.pointer = (char8*)entry->d_name, .length = strlen(entry->d_name)};
                result = a64c_fixture_push(arena, fixtures, name);
            }
        }
        result &= closedir(directory) == 0;
        result &= !fixtures->enumeration_error;
    }
#endif
    if (fixtures->full) { result = false; }
    a64c_sort_strings(fixtures->paths, fixtures->count);
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_angle_include(String8 source)
{
    bool found = false;
    u64 line_start = 0;
    for (u64 index = 0; !found && index <= source.length; index += 1)
    {
        if (index == source.length || source.pointer[index] == '\n')
        {
            String8 line = a64c_trim(string_slice(source, line_start, index));
            line_start = index + 1;
            if (line.length && line.pointer[0] == '#')
            {
                u64 at = 1;
                while (at < line.length && a64c_space(line.pointer[at])) { at += 1; }
                String8 directive = S8("include");
                bool include = line.length >= at + directive.length &&
                    memcmp(line.pointer + at, directive.pointer, (size_t)directive.length) == 0;
                at += include ? directive.length : 0;
                if (include && at < line.length && !a64c_alpha(line.pointer[at]))
                {
                    while (at < line.length && a64c_space(line.pointer[at])) { at += 1; }
                    found = at < line.length && line.pointer[at] == '<';
                }
            }
        }
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool a64c_register_token(String8 token)
{
    bool result = false;
    if (token.length >= 2 && a64c_alpha(token.pointer[0]) && token.pointer[1] >= '0' && token.pointer[1] <= '9')
    {
        char8 prefix = a64c_lower(token.pointer[0]);
        bool register_prefix = prefix == 'x' || prefix == 'w' || prefix == 'v' || prefix == 'q' || prefix == 'd' ||
            prefix == 's' || prefix == 'h' || prefix == 'b' || prefix == 'z' || prefix == 'p' || prefix == 'c';
        u64 at = 1;
        u32 number = 0;
        while (at < token.length && token.pointer[at] >= '0' && token.pointer[at] <= '9')
        {
            number = number * 10 + (u32)(token.pointer[at] - '0');
            at += 1;
        }
        bool tail_valid = true;
        while (at < token.length)
        {
            char8 value = token.pointer[at++];
            tail_valid &= a64c_alpha(value) || (value >= '0' && value <= '9') || value == '.' || value == '[' || value == ']';
        }
        result = register_prefix && number <= 31 && tail_valid;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_operand_word(String8 word)
{
    bool result = a64c_register_token(word);
    String8 const words[] = {
        S8("sp"), S8("wsp"), S8("xzr"), S8("wzr"), S8("fp"), S8("lr"), S8("ip0"), S8("ip1"),
        S8("lsl"), S8("lsr"), S8("asr"), S8("ror"), S8("uxtw"), S8("sxtw"), S8("sxtx"),
        S8("uxtb"), S8("uxth"), S8("sxtb"), S8("sxth"), S8("msl"), S8("m"), S8("z"),
        S8("eq"), S8("ne"), S8("cs"), S8("hs"), S8("cc"), S8("lo"), S8("mi"), S8("pl"),
        S8("vs"), S8("vc"), S8("hi"), S8("ls"), S8("ge"), S8("lt"), S8("gt"), S8("le"), S8("al"), S8("nv"),
        S8("sy"), S8("st"), S8("ld"), S8("ish"), S8("ishst"), S8("ishld"), S8("nsh"), S8("nshst"),
        S8("nshld"), S8("osh"), S8("oshst"), S8("oshld"),
        S8("nzcv"), S8("daif"), S8("fpcr"), S8("fpsr"), S8("tpidr_el0"), S8("tpidrro_el0"),
        S8("ctr_el0"), S8("dczid_el0"), S8("cntfrq_el0"), S8("cntpct_el0"), S8("cntvct_el0"),
        S8("midr_el1"), S8("mpidr_el1"), S8("currentel"), S8("cvau"), S8("cvac"), S8("civac"),
        S8("ivac"), S8("zva"), S8("isw"), S8("cisw"), S8("civa"), S8("cvap"),
    };
    for (u32 index = 0; !result && index < BUSTER_ARRAY_LENGTH(words); index += 1) { result = a64c_equal_folded(word, words[index]); }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 a64c_comment_start(String8 line)
{
    u64 result = line.length;
    for (u64 index = 0; index + 1 < line.length; index += 1)
    {
        if (line.pointer[index] == '/' && line.pointer[index + 1] == '/') { result = index; break; }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_instruction_line(String8 line)
{
    line = a64c_trim(string_slice(line, 0, a64c_comment_start(line)));
    bool result = line.length && line.pointer[0] != '.' && line.pointer[0] != '#';
    if (result && line.pointer[line.length - 1] == ':') { result = false; }
    if (result && !a64c_alpha(line.pointer[0])) { result = false; }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_constant_instruction(String8 line)
{
    line = a64c_trim(string_slice(line, 0, a64c_comment_start(line)));
    u64 first_space = 0;
    while (first_space < line.length && !a64c_space(line.pointer[first_space])) { first_space += 1; }
    bool constant = true;
    u64 index = first_space;
    while (index < line.length && constant)
    {
        char8 value = line.pointer[index];
        bool identifier = a64c_alpha(value) || value == '_' || value == '.' || value == '$';
        if (identifier)
        {
            u64 start = index++;
            while (index < line.length)
            {
                char8 next = line.pointer[index];
                bool part = a64c_alpha(next) || (next >= '0' && next <= '9') || next == '_' || next == '.' ||
                    next == '$' || next == '[' || next == ']';
                if (!part) { break; }
                index += 1;
            }
            String8 token = string_slice(line, start, index);
            if (!a64c_operand_word(token)) { constant = false; }
        }
        else if (value >= '0' && value <= '9')
        {
            index += 1;
            while (index < line.length)
            {
                char8 next = line.pointer[index];
                if (!((next >= '0' && next <= '9') || a64c_alpha(next) || next == '.')) { break; }
                index += 1;
            }
        }
        else { index += 1; }
    }
    return constant;
}

BUSTER_GLOBAL_LOCAL bool a64c_listing_scan(A64CSettings* settings, A64CStringSet* set,
    String8 source_path, String8 optimization, String8 listing)
{
    bool result = true;
    u64 line_start = 0;
    for (u64 index = 0; index <= listing.length && result; index += 1)
    {
        if (index == listing.length || listing.pointer[index] == '\n')
        {
            String8 line = a64c_trim(string_slice(listing, line_start, index));
            line_start = index + 1;
            if (a64c_instruction_line(line))
            {
                settings->assembly_line_count += 1;
                if (a64c_constant_instruction(line))
                {
                    String8 normalized = a64c_normalize(settings->arena,
                        string_slice(line, 0, a64c_comment_start(line)));
                    if (!a64c_set_insert(set, normalized)) { result = false; }
                }
                else
                {
                    settings->symbolic_exclusion_count += 1;
                    String8 note = string_format(settings->arena,
                        S8("CENSUS_SYMBOLIC_OR_NONCONSTANT_EXCLUSION path={S8} opt={S8} line={S8}\n"),
                        source_path, optimization, line);
                    a64c_record(settings, note);
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_hex(char8 value)
{
    bool result = (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
    return result;
}

BUSTER_GLOBAL_LOCAL u32 a64c_hex_value(char8 value)
{
    char8 lower = a64c_lower(value);
    u32 result = lower >= '0' && lower <= '9' ? (u32)(lower - '0') : (u32)(lower - 'a' + 10);
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_hex_token(String8 line, u64 start, u64 end)
{
    bool result = end > start;
    for (u64 index = start; index < end; index += 1) { result &= a64c_hex(line.pointer[index]); }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 a64c_hex_token_value(String8 line, u64 start, u64 end)
{
    u32 result = 0;
    for (u64 index = start; index < end; index += 1) { result = (result << 4) | a64c_hex_value(line.pointer[index]); }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_objdump_words(String8 output, u32 expected, u32* words)
{
    bool valid = output.length != 0;
    u32 count = 0;
    u64 line_start = 0;
    for (u64 index = 0; index <= output.length && valid; index += 1)
    {
        if (index == output.length || output.pointer[index] == '\n')
        {
            String8 line = string_slice(output, line_start, index);
            line_start = index + 1;
            u64 colon = 0;
            while (colon < line.length && line.pointer[colon] != ':') { colon += 1; }
            if (colon < line.length)
            {
                u64 start = colon + 1;
                while (start < line.length && a64c_space(line.pointer[start])) { start += 1; }
                if (start < line.length)
                {
                    u64 end = start;
                    while (end < line.length && !a64c_space(line.pointer[end])) { end += 1; }
                    u64 token_length = end - start;
                    bool token_is_hex = a64c_hex_token(line, start, end);
                    if (token_length == 8 && token_is_hex)
                    {
                        u64 next = end;
                        while (next < line.length && a64c_space(line.pointer[next])) { next += 1; }
                        u64 next_end = next;
                        while (next_end < line.length && !a64c_space(line.pointer[next_end])) { next_end += 1; }
                        u64 next_length = next_end - next;
                        bool extra_raw = (next_length == 2 || next_length == 8) && a64c_hex_token(line, next, next_end);
                        if (extra_raw || count >= expected) { valid = false; }
                        else { words[count++] = a64c_hex_token_value(line, start, end); }
                    }
                    else if (token_length == 2 && token_is_hex)
                    {
                        u32 word = 0;
                        u64 cursor = start;
                        bool bytes_valid = true;
                        for (u32 byte_index = 0; byte_index < 4 && bytes_valid; byte_index += 1)
                        {
                            u64 byte_end = cursor;
                            while (byte_end < line.length && !a64c_space(line.pointer[byte_end])) { byte_end += 1; }
                            bytes_valid = byte_end - cursor == 2 && a64c_hex_token(line, cursor, byte_end);
                            if (bytes_valid)
                            {
                                u32 byte = a64c_hex_token_value(line, cursor, byte_end);
                                word |= byte << (byte_index * 8);
                                cursor = byte_end;
                                while (cursor < line.length && a64c_space(line.pointer[cursor])) { cursor += 1; }
                            }
                        }
                        u64 extra_end = cursor;
                        while (extra_end < line.length && !a64c_space(line.pointer[extra_end])) { extra_end += 1; }
                        u64 extra_length = extra_end - cursor;
                        bool extra_raw = (extra_length == 2 || extra_length == 8) && a64c_hex_token(line, cursor, extra_end);
                        if (!bytes_valid || extra_raw || count >= expected) { valid = false; }
                        else { words[count++] = word; }
                    }
                    else if (token_is_hex) { valid = false; }
                }
            }
        }
    }
    valid &= count == expected && expected != 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64c_row_partition_complete(A64CSettings* settings)
{
    u64 accounted = (u64)settings->compared_line_count + settings->documented_refusal_count + settings->unknown_refusal_count;
    bool result = settings->unique_line_count != 0 && accounted == settings->unique_line_count;
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_source_complete(A64CSettings* settings)
{
    bool result = a64c_row_partition_complete(settings) && settings->unknown_refusal_count == 0 && settings->difference_count == 0;
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_counts_valid(A64CSettings* settings)
{
    bool fixture_partition_valid = settings->fixture_count != 0 && settings->skipped_include_count <= settings->fixture_count;
    u64 expected_compile_attempt_count = fixture_partition_valid ?
        (u64)(settings->fixture_count - settings->skipped_include_count) * A64C_OPTIMIZATION_COUNT : UINT64_MAX;
    bool compile_attempts_accounted = fixture_partition_valid &&
        settings->compile_attempt_count == expected_compile_attempt_count &&
        (u64)settings->compiled_listing_count + settings->compile_failure_count == settings->compile_attempt_count;
    bool result = fixture_partition_valid && compile_attempts_accounted && a64c_source_complete(settings) &&
        settings->compiled_fixture_count != 0 && settings->compiled_listing_count != 0 && settings->unique_line_count != 0 &&
        settings->compared_line_count != 0 && !settings->io_failed && !settings->child_budget_exceeded;
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_summary_pass(A64CSettings* settings, bool scan_complete)
{
    bool result = scan_complete && a64c_counts_valid(settings);
    return result;
}

BUSTER_GLOBAL_LOCAL A64CChild a64c_stub_child(A64CSettings* settings, String8 role, String8 mode)
{
    String8 args[] = {settings->self_executable, S8("test_aarch64_assembly_census"), S8("--stub-tool"), role, mode};
    A64CChild result = a64c_child_run(settings, (SliceString8){.pointer = args, .length = BUSTER_ARRAY_LENGTH(args)});
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult a64c_stub_tool(SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length == 3)
    {
        String8 role = arguments.pointer[1];
        String8 mode = arguments.pointer[2];
        if (string_equal(role, S8("ide")) && string_equal(mode, S8("refusal")))
        {
            fputs("error: unsupported AArch64 instruction\n", stderr);
        }
        else if (string_equal(role, S8("llvm-mc")) && string_equal(mode, S8("failure")))
        {
            fputs("stub observer failure\n", stderr);
        }
        else if (string_equal(role, S8("llvm-objdump")) && string_equal(mode, S8("failure")))
        {
            fputs("stub observer failure\n", stderr);
        }
        else if (string_equal(role, S8("llvm-objdump")) && string_equal(mode, S8("bytes")))
        {
            fputs("0000000000000000: e0 03 00 91\tadd x0, sp, #0\n", stdout);
            result = PROCESS_RESULT_SUCCESS;
        }
        else if (string_equal(role, S8("llvm-objdump")) && string_equal(mode, S8("word")))
        {
            fputs("0000000000000000: 910003e0\tadd x0, sp, #0\n", stdout);
            result = PROCESS_RESULT_SUCCESS;
        }
        else if (string_equal(role, S8("llvm-objdump")) && string_equal(mode, S8("extra")))
        {
            fputs("0000000000000000: e0 03 00 91\tadd x0, sp, #0\n0000000000000004: 1f 20 03 d5\tnop\n", stdout);
            result = PROCESS_RESULT_SUCCESS;
        }
        else if (string_equal(role, S8("llvm-objdump")) && string_equal(mode, S8("missing")))
        {
            fputs("0000000000000000: e0 03 00\tadd x0, sp, #0\n", stdout);
            result = PROCESS_RESULT_SUCCESS;
        }
        else if (string_equal(role, S8("llvm-objdump")) && string_equal(mode, S8("extra-byte")))
        {
            fputs("0000000000000000: e0 03 00 91 00\tadd x0, sp, #0\n", stdout);
            result = PROCESS_RESULT_SUCCESS;
        }
        else if (string_equal(role, S8("llvm-objdump")) && string_equal(mode, S8("empty")))
        {
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_word_equal(u32 reference, u32 candidate)
{
    bool result = reference == candidate;
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult a64c_self_test(Arena* arena, String8 self_executable)
{
    A64CSettings settings = {.arena = arena, .self_executable = self_executable,
        .timeout_seconds = 10};
    A64CChild refusal = a64c_stub_child(&settings, S8("ide"), S8("refusal"));
    A64CRefusalKind positive = a64c_refusal_kind(arena, S8("fmla v0.4s, v1.4s, v2.s[0]"), refusal);
    A64CRefusalKind negative = a64c_refusal_kind(arena, S8("add x0, x0, #1"), refusal);
    A64CChild observer = a64c_stub_child(&settings, S8("llvm-mc"), S8("failure"));
    A64CChild dump_error = a64c_stub_child(&settings, S8("llvm-objdump"), S8("failure"));
    A64CChild empty = a64c_stub_child(&settings, S8("llvm-objdump"), S8("empty"));
    A64CChild bytes = a64c_stub_child(&settings, S8("llvm-objdump"), S8("bytes"));
    A64CChild word = a64c_stub_child(&settings, S8("llvm-objdump"), S8("word"));
    A64CChild extra = a64c_stub_child(&settings, S8("llvm-objdump"), S8("extra"));
    A64CChild missing = a64c_stub_child(&settings, S8("llvm-objdump"), S8("missing"));
    A64CChild extra_byte = a64c_stub_child(&settings, S8("llvm-objdump"), S8("extra-byte"));
    A64CSettings complete_counts = {.fixture_count = 1, .compile_attempt_count = 3, .compiled_fixture_count = 1,
        .compiled_listing_count = 3, .unique_line_count = 2, .compared_line_count = 1, .documented_refusal_count = 1};
    A64CSettings unknown_counts = complete_counts;
    unknown_counts.documented_refusal_count = 0;
    unknown_counts.unknown_refusal_count = 1;
    A64CSettings difference_counts = complete_counts;
    difference_counts.documented_refusal_count = 0;
    difference_counts.compared_line_count = 2;
    difference_counts.difference_count = 1;
    A64CSettings incomplete_counts = complete_counts;
    incomplete_counts.unique_line_count = 3;
    u32 byte_word[1] = {0};
    u32 hex_word[1] = {0};
    bool bytes_valid = bytes.kind == A64C_CHILD_SUCCESS && a64c_objdump_words(bytes.output, 1, byte_word);
    bool word_valid = word.kind == A64C_CHILD_SUCCESS && a64c_objdump_words(word.output, 1, hex_word);
    bool controls[] = {
        refusal.kind == A64C_CHILD_NORMAL_NONZERO && a64c_contains(refusal.error, S8("error: unsupported AArch64 instruction")),
        positive == A64C_REFUSAL_DOCUMENTED,
        negative == A64C_REFUSAL_UNKNOWN,
        observer.kind == A64C_CHILD_NORMAL_NONZERO && a64c_contains(observer.error, S8("stub observer failure")),
        dump_error.kind == A64C_CHILD_NORMAL_NONZERO && a64c_contains(dump_error.error, S8("stub observer failure")),
        empty.kind == A64C_CHILD_SUCCESS && empty.output.length == 0 && !a64c_objdump_words(empty.output, 1, byte_word),
        bytes_valid && byte_word[0] == 0x910003e0u,
        word_valid && hex_word[0] == 0x910003e0u,
        bytes_valid && !a64c_word_equal(0xd503201fu, byte_word[0]),
        extra.kind == A64C_CHILD_SUCCESS && !a64c_objdump_words(extra.output, 1, byte_word),
        missing.kind == A64C_CHILD_SUCCESS && !a64c_objdump_words(missing.output, 1, byte_word),
        extra_byte.kind == A64C_CHILD_SUCCESS && !a64c_objdump_words(extra_byte.output, 1, byte_word),
        a64c_row_partition_complete(&complete_counts) && a64c_source_complete(&complete_counts) &&
            a64c_summary_pass(&complete_counts, true),
        a64c_row_partition_complete(&unknown_counts) && !a64c_source_complete(&unknown_counts) &&
            !a64c_summary_pass(&unknown_counts, true),
        a64c_row_partition_complete(&difference_counts) && !a64c_source_complete(&difference_counts) &&
            !a64c_summary_pass(&difference_counts, true),
        !a64c_row_partition_complete(&incomplete_counts),
        !a64c_counts_valid(&(A64CSettings){.fixture_count = 0, .compiled_fixture_count = 0,
            .compiled_listing_count = 0, .unique_line_count = 0, .compared_line_count = 0}),
        !settings.child_budget_exceeded && !settings.io_failed,
    };
    u32 failures = 0;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(controls); index += 1) { failures += !controls[index]; }
    string_print(S8("AARCH64_ASSEMBLY_CENSUS_SELF_TEST controls={u32} failures={u32}\n"),
        (u32)BUSTER_ARRAY_LENGTH(controls), failures);
    ProcessResult result = failures == 0 ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_parse_number(String8 text, u64* value)
{
    bool valid = text.length != 0;
    u64 result = 0;
    for (u64 index = 0; index < text.length && valid; index += 1)
    {
        char8 digit = text.pointer[index];
        if (digit < '0' || digit > '9' || result > (UINT64_MAX - (u64)(digit - '0')) / 10) { valid = false; }
        else { result = result * 10 + (u64)(digit - '0'); }
    }
    if (valid) { *value = result; }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64c_make_directory(String8 path)
{
    OsDirectoryCreateResult result = os_make_directory_exclusive(path);
    bool success = result.created && !result.error.v;
    return success;
}

BUSTER_GLOBAL_LOCAL bool a64c_create_output(A64CSettings* settings, String8 requested)
{
    String8 output = requested.length ? requested : string_format(settings->arena,
        S8("build/aarch64-assembly-census-{u64}"), os_now_microseconds());
    bool result = a64c_make_directory(output);
    if (result)
    {
        settings->output_directory = os_path_absolute(settings->arena, output, true);
        result = a64c_make_directory(path_join(settings->arena, settings->output_directory, S8("listings")));
        result &= a64c_make_directory(path_join(settings->arena, settings->output_directory, S8("batches")));
    }
    if (result)
    {
        String8 report_path = path_join(settings->arena, settings->output_directory, S8("report.txt"));
        String8 report_z = string_duplicate_arena(settings->arena, report_path, true);
#if BUSTER_WINDOWS
        settings->report = fopen((char const*)report_z.pointer, "wbN");
#else
        settings->report = fopen((char const*)report_z.pointer, "wb");
#endif
        result = settings->report != 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL A64CChild a64c_compile_listing(A64CSettings* settings, String8 fixture, String8 optimization, String8 output)
{
    String8 args[] = {settings->clang, S8("--target=aarch64-linux-gnu"), S8("-nostdinc"), S8("-ffreestanding"),
        optimization, S8("-S"), S8("-o"), output, fixture};
    A64CChild result = a64c_child_run(settings, (SliceString8){.pointer = args, .length = BUSTER_ARRAY_LENGTH(args)});
    return result;
}

BUSTER_GLOBAL_LOCAL String8 a64c_make_batch_source(Arena* arena, String8* lines, u32 start, u32 count)
{
    u64 size = 6;
    for (u32 index = 0; index < count; index += 1) { size += lines[start + index].length + 1; }
    char8* memory = arena_allocate(arena, char8, size);
    u64 at = 0;
    memcpy(memory + at, ".text\n", 6); at += 6;
    for (u32 index = 0; index < count; index += 1)
    {
        String8 line = lines[start + index];
        memcpy(memory + at, line.pointer, (size_t)line.length); at += line.length;
        memory[at++] = '\n';
    }
    String8 result = {.pointer = memory, .length = at};
    return result;
}

BUSTER_GLOBAL_LOCAL A64CChild a64c_assemble(A64CSettings* settings, String8 tool, bool reference,
    String8 name, String8* lines, u32 start, u32 count, String8* object_out)
{
    String8 base = string_format(settings->arena, S8("batches/{S8}"), name);
    String8 source = path_join(settings->arena, settings->output_directory, string_format(settings->arena, S8("{S8}.s"), base));
    String8 object = path_join(settings->arena, settings->output_directory, string_format(settings->arena, S8("{S8}.o"), base));
    String8 content = a64c_make_batch_source(settings->arena, lines, start, count);
    bool wrote = a64c_write_text(source, content);
    String8 args[9] = {0};
    u32 arg_count = 0;
    if (reference)
    {
        args[arg_count++] = tool;
        args[arg_count++] = S8("-triple=aarch64-linux-gnu");
        args[arg_count++] = S8("-filetype=obj");
        args[arg_count++] = source;
        args[arg_count++] = S8("-o");
        args[arg_count++] = object;
    }
    else
    {
        args[arg_count++] = tool;
        args[arg_count++] = S8("cc");
        args[arg_count++] = S8("-target");
        args[arg_count++] = S8("aarch64-linux");
        args[arg_count++] = S8("-c");
        args[arg_count++] = source;
        args[arg_count++] = S8("-o");
        args[arg_count++] = object;
    }
    settings->io_failed |= !wrote;
    A64CChild result = wrote ? a64c_child_run(settings, (SliceString8){.pointer = args, .length = arg_count}) : (A64CChild){.kind = A64C_CHILD_FAILURE};
    *object_out = object;
    return result;
}

BUSTER_GLOBAL_LOCAL A64CChild a64c_objdump(A64CSettings* settings, String8 object, String8 dump_path)
{
    String8 args[] = {settings->llvm_objdump, S8("-d"), object};
    A64CChild result = a64c_child_run(settings, (SliceString8){.pointer = args, .length = BUSTER_ARRAY_LENGTH(args)});
    if (result.kind == A64C_CHILD_SUCCESS && !a64c_write_text(dump_path, result.output)) { settings->io_failed = true; result.kind = A64C_CHILD_FAILURE; }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_compare_words(A64CSettings* settings, String8* lines,
    u32 start, u32 count, u32* reference, u32* candidate, u32 batch)
{
    bool result = !settings->io_failed;
    for (u32 index = 0; index < count && result; index += 1)
    {
        settings->compared_line_count += 1;
        if (!a64c_word_equal(reference[index], candidate[index]))
        {
            settings->difference_count += 1;
            a64c_emit(settings, string_format(settings->arena,
                S8("CENSUS_ENCODING_DIFFERENCE batch={u32} instruction={S8} ide_word={u32} llvm_word={u32}\n"),
                batch, lines[start + index], candidate[index], reference[index]));
            result = !settings->io_failed;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_single_candidate(A64CSettings* settings, String8 line, String8 name,
    u32 reference_word, u32 batch)
{
    String8* one_line = arena_allocate(settings->arena, String8, 1);
    one_line[0] = line;
    String8 object = {0};
    A64CChild assembled = a64c_assemble(settings, settings->ide, false, name, one_line, 0, 1, &object);
    bool result = true;
    if (assembled.kind == A64C_CHILD_SUCCESS)
    {
        String8 dump_path = path_join(settings->arena, settings->output_directory,
            string_format(settings->arena, S8("{S8}.dump"), name));
        A64CChild dump = a64c_objdump(settings, object, dump_path);
        u32 candidate_word = 0;
        if (dump.kind != A64C_CHILD_SUCCESS || !a64c_objdump_words(dump.output, 1, &candidate_word))
        {
            a64c_emit(settings, string_format(settings->arena,
                S8("CENSUS_OBSERVER_FAILURE observer=llvm-objdump mode=candidate batch={u32} instruction={S8}\n"), batch, line));
            result = false;
        }
        else
        {
            u32 expected[] = {reference_word};
            u32 observed[] = {candidate_word};
            result = a64c_compare_words(settings, one_line, 0, 1, expected, observed, batch);
        }
    }
    else if (assembled.kind == A64C_CHILD_NORMAL_NONZERO && a64c_child_error_text(assembled))
    {
        A64CRefusalKind refusal = a64c_refusal_kind(settings->arena, line, assembled);
        if (refusal == A64C_REFUSAL_DOCUMENTED)
        {
            settings->documented_refusal_count += 1;
            a64c_emit(settings, string_format(settings->arena,
                S8("CENSUS_DOCUMENTED_REFUSAL batch={u32} instruction={S8}\n"), batch, line));
        }
        else
        {
            settings->unknown_refusal_count += 1;
            a64c_emit(settings, string_format(settings->arena,
                S8("CENSUS_UNKNOWN_REFUSAL batch={u32} instruction={S8} llvm_word={u32} diagnostic={S8}\n"),
                batch, line, reference_word, a64c_diagnostic_excerpt(settings->arena, assembled)));
        }
        result = !settings->io_failed;
    }
    else
    {
        a64c_emit(settings, string_format(settings->arena,
            S8("CENSUS_CANDIDATE_PROCESS_FAILURE batch={u32} status={u32} timeout={u32} spawn_error={u32} instruction={S8} diagnostic={S8}\n"),
            batch, assembled.raw_status, (u32)assembled.timed_out, assembled.spawn_error, line,
            a64c_diagnostic_excerpt(settings->arena, assembled)));
        result = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_compare_batch(A64CSettings* settings, String8* lines,
    u32 start, u32 count, u32 batch)
{
    String8 reference_object = {0};
    String8 candidate_object = {0};
    String8 batch_name = string_format(settings->arena, S8("batch-{u32}"), batch);
    A64CChild reference = a64c_assemble(settings, settings->llvm_mc, true, string_format(settings->arena,
        S8("{S8}-reference"), batch_name), lines, start, count, &reference_object);
    bool result = reference.kind == A64C_CHILD_SUCCESS;
    if (!result)
    {
        a64c_emit(settings, string_format(settings->arena,
            S8("CENSUS_OBSERVER_FAILURE observer=llvm-mc batch={u32} status={u32} spawn_error={u32} diagnostic={S8}\n"),
            batch, reference.raw_status, reference.spawn_error, a64c_diagnostic_excerpt(settings->arena, reference)));
    }
    String8 reference_dump_path = path_join(settings->arena, settings->output_directory,
        string_format(settings->arena, S8("batches/{S8}-reference.dump"), batch_name));
    A64CChild reference_dump = result ? a64c_objdump(settings, reference_object, reference_dump_path) : (A64CChild){0};
    u32* reference_words = arena_allocate(settings->arena, u32, count);
    if (result && (reference_dump.kind != A64C_CHILD_SUCCESS || !a64c_objdump_words(reference_dump.output, count, reference_words)))
    {
        a64c_emit(settings, string_format(settings->arena,
            S8("CENSUS_OBSERVER_FAILURE observer=llvm-objdump mode=reference batch={u32} expected={u32}\n"), batch, count));
        result = false;
    }
    if (result)
    {
        A64CChild candidate = a64c_assemble(settings, settings->ide, false, string_format(settings->arena,
            S8("{S8}-candidate"), batch_name), lines, start, count, &candidate_object);
        if (candidate.kind == A64C_CHILD_SUCCESS)
        {
            String8 candidate_dump_path = path_join(settings->arena, settings->output_directory,
                string_format(settings->arena, S8("batches/{S8}-candidate.dump"), batch_name));
            A64CChild candidate_dump = a64c_objdump(settings, candidate_object, candidate_dump_path);
            u32* candidate_words = arena_allocate(settings->arena, u32, count);
            if (candidate_dump.kind != A64C_CHILD_SUCCESS || !a64c_objdump_words(candidate_dump.output, count, candidate_words))
            {
                a64c_emit(settings, string_format(settings->arena,
                    S8("CENSUS_OBSERVER_FAILURE observer=llvm-objdump mode=candidate batch={u32} expected={u32}\n"), batch, count));
                result = false;
            }
            else { result = a64c_compare_words(settings, lines, start, count, reference_words, candidate_words, batch); }
        }
        else if (candidate.kind == A64C_CHILD_NORMAL_NONZERO && a64c_child_error_text(candidate))
        {
            u64 refusal_outcomes_before = (u64)settings->documented_refusal_count + settings->unknown_refusal_count;
            result = a64c_emit(settings, string_format(settings->arena,
                S8("CENSUS_BATCH_REFUSAL_ISOLATION batch={u32} lines={u32}\n"), batch, count));
            for (u32 index = 0; index < count && result; index += 1)
            {
                String8 single_name = string_format(settings->arena, S8("{S8}-single-{u32}"), batch_name, index);
                result = a64c_single_candidate(settings, lines[start + index], single_name, reference_words[index], batch);
            }
            u64 refusal_outcomes_after = (u64)settings->documented_refusal_count + settings->unknown_refusal_count;
            if (result && refusal_outcomes_after == refusal_outcomes_before)
            {
                a64c_emit(settings, string_format(settings->arena,
                    S8("CENSUS_UNKNOWN_BATCH_FAILURE batch={u32} diagnostic={S8}\n"), batch,
                    a64c_diagnostic_excerpt(settings->arena, candidate)));
                result = false;
            }
        }
        else
        {
            a64c_emit(settings, string_format(settings->arena,
                S8("CENSUS_CANDIDATE_PROCESS_FAILURE batch={u32} status={u32} timeout={u32} spawn_error={u32} diagnostic={S8}\n"),
                batch, candidate.raw_status, (u32)candidate.timed_out, candidate.spawn_error,
                a64c_diagnostic_excerpt(settings->arena, candidate)));
            result = false;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool a64c_fixture_census(A64CSettings* settings, A64CStringSet* set)
{
    bool result = true;
    A64CFixtures fixtures = {0};
    result = a64c_fixtures_enumerate(settings->arena, &fixtures);
    settings->fixture_count = fixtures.count;
    if (!result || fixtures.count == 0)
    {
        a64c_emit(settings, S8("CENSUS_FIXTURE_ENUMERATION_FAILURE path=tests/*.c\n"));
        result = false;
    }
    String8 optimizations[A64C_OPTIMIZATION_COUNT] = {S8("-O0"), S8("-O1"), S8("-O2")};
    for (u32 fixture_index = 0; fixture_index < fixtures.count && result; fixture_index += 1)
    {
        String8 fixture = fixtures.paths[fixture_index];
        FileReadResult source_file = file_read_checked(settings->arena, fixture, (FileReadOptions){0});
        if (source_file.status != OS_FILE_READ_OK)
        {
            a64c_emit(settings, string_format(settings->arena, S8("CENSUS_FIXTURE_READ_FAILURE path={S8}\n"), fixture));
            result = false;
        }
        else
        {
            String8 source = {.pointer = (char8*)source_file.bytes.pointer, .length = source_file.bytes.length};
            if (a64c_angle_include(source))
            {
                settings->skipped_include_count += 1;
                a64c_emit(settings, string_format(settings->arena,
                    S8("CENSUS_FIXTURE_EXCLUSION path={S8} reason=angle-include\n"), fixture));
            }
            else
            {
                bool fixture_compiled = false;
                for (u32 optimization_index = 0; optimization_index < BUSTER_ARRAY_LENGTH(optimizations) && result; optimization_index += 1)
                {
                    String8 optimization = optimizations[optimization_index];
                    String8 listing_name = string_format(settings->arena,
                        S8("listings/fixture-{u32}-{S8}.s"), fixture_index, string_slice(optimization, 1, optimization.length));
                    String8 listing_path = path_join(settings->arena, settings->output_directory, listing_name);
                    settings->compile_attempt_count += 1;
                    A64CChild compiled = a64c_compile_listing(settings, fixture, optimization, listing_path);
                    if (compiled.kind == A64C_CHILD_SUCCESS)
                    {
                        FileReadResult listing_file = file_read_checked(settings->arena, listing_path, (FileReadOptions){0});
                        if (listing_file.status != OS_FILE_READ_OK)
                        {
                            a64c_emit(settings, string_format(settings->arena,
                                S8("CENSUS_LISTING_READ_FAILURE path={S8}\n"), listing_path));
                            result = false;
                        }
                        else
                        {
                            String8 listing = {.pointer = (char8*)listing_file.bytes.pointer, .length = listing_file.bytes.length};
                            settings->compiled_listing_count += 1;
                            fixture_compiled = true;
                            result = a64c_listing_scan(settings, set, fixture, optimization, listing);
                        }
                    }
                    else if (compiled.kind == A64C_CHILD_NORMAL_NONZERO)
                    {
                        settings->compile_failure_count += 1;
                        String8 note = string_format(settings->arena,
                            S8("CENSUS_FIXTURE_COMPILE_FAILURE path={S8} opt={S8} status={u32} diagnostic={S8}\n"),
                            fixture, optimization, compiled.raw_status, a64c_diagnostic_excerpt(settings->arena, compiled));
                        a64c_emit(settings, note);
                    }
                    else
                    {
                        a64c_emit(settings, string_format(settings->arena,
                            S8("CENSUS_CLANG_PROCESS_FAILURE path={S8} opt={S8} status={u32} timeout={u32} spawn_error={u32}\n"),
                            fixture, optimization, compiled.raw_status, (u32)compiled.timed_out, compiled.spawn_error));
                        result = false;
                    }
                }
                settings->compiled_fixture_count += fixture_compiled;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult a64c_run(Arena* arena, SliceString8 arguments, String8 self_executable)
{
    A64CSettings settings = {.arena = arena, .self_executable = self_executable,
        .ide = S8("build/Release/ide"), .clang = S8("clang"), .llvm_mc = S8("llvm-mc"),
        .llvm_objdump = S8("llvm-objdump"), .timeout_seconds = A64C_DEFAULT_TIMEOUT_SECONDS};
    settings.run_start_us = os_now_microseconds();
    A64CStringSet set = {0};
    set.slots = arena_allocate(arena, String8, A64C_SET_CAPACITY);
    String8 requested_output = {0};
    bool valid = true;
    bool self_test = false;
    bool help = false;
    for (u64 index = 0; index < arguments.length && valid; index += 1)
    {
        String8 arg = arguments.pointer[index];
        if (string_equal(arg, S8("--self-test"))) { self_test = true; }
        else if (string_equal(arg, S8("--help")) || string_equal(arg, S8("-h"))) { help = true; }
        else if (index + 1 < arguments.length)
        {
            String8 value = arguments.pointer[++index];
            if (string_equal(arg, S8("--ide"))) { settings.ide = value; }
            else if (string_equal(arg, S8("--clang"))) { settings.clang = value; }
            else if (string_equal(arg, S8("--llvm-mc"))) { settings.llvm_mc = value; }
            else if (string_equal(arg, S8("--llvm-objdump"))) { settings.llvm_objdump = value; }
            else if (string_equal(arg, S8("--out"))) { requested_output = value; }
            else if (string_equal(arg, S8("--timeout-seconds")))
            {
                u64 timeout = 0;
                bool parsed = a64c_parse_number(value, &timeout) && timeout > 0 && timeout <= 3600;
                valid &= parsed;
                if (parsed) { settings.timeout_seconds = timeout; }
            }
            else { valid = false; }
        }
        else { valid = false; }
    }
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length && string_equal(arguments.pointer[0], S8("--stub-tool")))
    {
        result = a64c_stub_tool(arguments);
    }
    else if (help)
    {
        string_print(S8("usage: test_aarch64_assembly_census [--self-test] [--ide path] [--clang path] [--llvm-mc path] [--llvm-objdump path] [--out new-directory] [--timeout-seconds 1..3600]\n"));
        result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (valid && self_test)
    {
        result = a64c_self_test(arena, self_executable);
    }
    else if (valid)
    {
        bool ready = a64c_create_output(&settings, requested_output);
        if (!ready)
        {
            string_print(S8("error: census output directory must be new and its parent must exist\n"));
        }
        else
        {
            a64c_emit(&settings, string_format(arena,
                S8("AARCH64_ASSEMBLY_CENSUS version=2 target=aarch64-linux-gnu source_fixtures=tests/*.c optimizations=O0,O1,O2 ide={S8} clang={S8} llvm_mc={S8} llvm_objdump={S8} timeout_seconds={u64} max_run_seconds={u32} max_children={u32}\n"),
                settings.ide, settings.clang, settings.llvm_mc, settings.llvm_objdump, settings.timeout_seconds,
                A64C_MAX_RUN_SECONDS, A64C_MAX_CHILDREN));
            bool corpus_ok = a64c_fixture_census(&settings, &set);
            settings.unique_line_count = set.count;
            String8* ordered = arena_allocate(arena, String8, set.count);
            u32 ordered_count = 0;
            for (u32 index = 0; index < A64C_SET_CAPACITY; index += 1)
            {
                if (set.slots[index].length) { ordered[ordered_count++] = set.slots[index]; }
            }
            a64c_sort_strings(ordered, ordered_count);
            String8 lines_path = path_join(arena, settings.output_directory, S8("constant-instructions.txt"));
            u64 lines_bytes = 1;
            for (u32 index = 0; index < ordered_count; index += 1) { lines_bytes += ordered[index].length + 1; }
            char8* line_memory = arena_allocate(arena, char8, lines_bytes);
            u64 line_at = 0;
            for (u32 index = 0; index < ordered_count; index += 1)
            {
                String8 line = ordered[index];
                memcpy(line_memory + line_at, line.pointer, (size_t)line.length); line_at += line.length;
                line_memory[line_at++] = '\n';
            }
            bool lines_written = a64c_write_text(lines_path, (String8){.pointer = line_memory, .length = line_at});
            bool compare_ok = corpus_ok && lines_written && ordered_count != 0;
            if (compare_ok)
            {
                for (u32 start = 0, batch = 0; start < ordered_count && compare_ok; batch += 1)
                {
                    u32 count = BUSTER_MIN(A64C_BATCH_LINES, ordered_count - start);
                    compare_ok = a64c_compare_batch(&settings, ordered, start, count, batch);
                    start += count;
                }
            }
            if (ordered_count == 0) { a64c_emit(&settings, S8("CENSUS_EMPTY_INSTRUCTION_CORPUS\n")); }
            if (settings.compiled_fixture_count == 0) { a64c_emit(&settings, S8("CENSUS_NO_COMPILED_FIXTURE\n")); }
            if (settings.compared_line_count == 0) { a64c_emit(&settings, S8("CENSUS_ZERO_COMPARED_LINES\n")); }
            bool row_partition_complete = a64c_row_partition_complete(&settings);
            bool source_complete = a64c_source_complete(&settings);
            bool complete = a64c_summary_pass(&settings, compare_ok);
            String8 source_coverage = settings.compile_failure_count ? S8("producer-exclusions") :
                settings.skipped_include_count ? S8("fixture-exclusions") : S8("complete");
            String8 outcome = complete ?
                (settings.compile_failure_count || settings.skipped_include_count ? S8("PASS_WITH_EXCLUSIONS") : S8("PASS")) : S8("FAIL");
            u32 expected_compile_attempt_count = settings.fixture_count >= settings.skipped_include_count ?
                (settings.fixture_count - settings.skipped_include_count) * A64C_OPTIMIZATION_COUNT : 0;
            String8 summary = string_format(arena,
                S8("AARCH64_ASSEMBLY_CENSUS_SUMMARY output={S8} fixtures={u32} angle_include_exclusions={u32} compiled_fixtures={u32} compiled_listings={u32} compile_attempts={u32} expected_compile_attempts={u32} compile_failures={u32} source_coverage={S8} assembly_lines={u32} symbolic_or_nonconstant_exclusions={u32} unique_constant_lines={u32} compared_lines={u32} documented_refusals={u32} unknown_refusals={u32} differences={u32} row_partition_complete={u32} source_complete={u32} child_processes={u64} captured_bytes={u64} budget_exceeded={u32} result={S8}\n"),
                settings.output_directory, settings.fixture_count, settings.skipped_include_count, settings.compiled_fixture_count,
                settings.compiled_listing_count, settings.compile_attempt_count, expected_compile_attempt_count,
                settings.compile_failure_count, source_coverage, settings.assembly_line_count,
                settings.symbolic_exclusion_count, settings.unique_line_count, settings.compared_line_count,
                settings.documented_refusal_count, settings.unknown_refusal_count, settings.difference_count,
                (u32)row_partition_complete, (u32)source_complete, settings.child_count,
                settings.captured_bytes, (u32)settings.child_budget_exceeded, outcome);
            a64c_emit(&settings, summary);
            string_print(S8("census evidence: {S8}\n"), settings.output_directory);
            if (settings.report) { settings.io_failed |= fclose(settings.report) != 0; }
            result = complete && !settings.io_failed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
    }
    else
    {
        string_print(S8("error: invalid test_aarch64_assembly_census arguments; use --help\n"));
    }
    return result;
}

// build.c dispatch contract:
//   aarch64_assembly_census_main(arena, owned_arguments, arguments.pointer[0])
BUSTER_GLOBAL_LOCAL ProcessResult aarch64_assembly_census_main(Arena* arena, SliceString8 arguments, String8 self_executable)
{
    ProcessResult result = a64c_run(arena, arguments, self_executable);
    return result;
}
