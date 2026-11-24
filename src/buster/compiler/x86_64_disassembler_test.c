// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Catalogue coverage, independently hand-written bytes, random byte rejection and guarded logical tails. Included
// after x86_64_encoder_test.c so its original random operand generator is shared, without a second opcode database.

#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ctype.h>

static bool x86_decode_test_process(char* const* arguments, const char* output)
{
    const pid_t child = fork();
    bool success = child >= 0;
    if (child == 0)
    {
        const int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0 && dup2(fd, STDOUT_FILENO) >= 0 && dup2(fd, STDERR_FILENO) >= 0)
        {
            close(fd);
            execvp(arguments[0], arguments);
        }
        _exit(127);
    }
    if (child > 0)
    {
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        success = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    return success;
}

static bool x86_decode_test_mnemonic(const char* expected, int32_t immediate, const char* actual)
{
    // GNU's conventional aliases; the instruction catalogue contains the same semantic names.
    static const char* const aliases[][2] =
    {
        {"je", "jz"},
        {"jne", "jnz"},
        {"sal", "shl"},
        {"wait", "fwait"},
        {"mov", "movabs"},
        {"movabs", "mov"},
        {"xlatb", "xlat"},
        {"feni", "fneni"},
        {"fdisi", "fndisi"},
        {"fsetpm", "fnsetpm"},
        {"retfd", "retf"},
        {"iretd", "iret"},
        {"pushfq", "pushf"},
        {"popfq", "popf"},
        {"xstore", "xstore-rng"},
        {"xcryptecb", "xcrypt-ecb"},
        {"xcryptcbc", "xcrypt-cbc"},
        {"xcryptctr", "xcrypt-ctr"},
        {"xcryptcfb", "xcrypt-cfb"},
        {"xcryptofb", "xcrypt-ofb"},
        {"movsb", "movs"},
        {"movsw", "movs"},
        {"movsd", "movs"},
        {"movsq", "movs"},
        {"stosb", "stos"},
        {"stosw", "stos"},
        {"stosd", "stos"},
        {"stosq", "stos"},
        {"lodsb", "lods"},
        {"lodsw", "lods"},
        {"lodsd", "lods"},
        {"lodsq", "lods"},
        {"scasb", "scas"},
        {"scasw", "scas"},
        {"scasd", "scas"},
        {"scasq", "scas"},
        {"cmpsb", "cmps"},
        {"cmpsw", "cmps"},
        {"cmpsd", "cmps"},
        {"cmpsq", "cmps"},
        {"insb", "ins"},
        {"insw", "ins"},
        {"insd", "ins"},
        {"outsb", "outs"},
        {"outsw", "outs"},
        {"outsd", "outs"},
        {"cmpnbxadd", "cmpaexadd"},
        {"cmpzxadd", "cmpexadd"},
        {"cmpnzxadd", "cmpnexadd"},
        {"cmpnbexadd", "cmpaxadd"},
        {"cmpnlxadd", "cmpgexadd"},
        {"cmpnlexadd", "cmpgxadd"},
    };
    const bool word_relative = strncmp(expected, "data16 ", 7) == 0;
    const char* last = strrchr(expected, ' ');
    expected = last != 0 ? last + 1 : expected;
    static const char* const float_conditions[32] =
    {
        "eq_oq", "lt_os", "le_os", "unord_q", "neq_uq", "nlt_us", "nle_us", "ord_q",
        "eq_uq", "nge_us", "ngt_us", "false_oq", "neq_oq", "ge_os", "gt_os", "true_uq",
        "eq_os", "lt_oq", "le_oq", "unord_s", "neq_us", "nlt_uq", "nle_uq", "ord_s",
        "eq_us", "nge_uq", "ngt_uq", "false_os", "neq_os", "ge_oq", "gt_oq", "true_us",
    };
    static const char* const short_conditions[16] = {"eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord", "eq_uq", "nge", "ngt", "false", "neq_oq", "ge", "gt", "true"};
    static const char* const integer_conditions[8] = {"eq", "lt", "le", "false", "neq", "nlt", "nle", "true"};
    static const char* const xop_conditions[8] = {"lt", "le", "gt", "ge", "eq", "neq", "false", "true"};
    static const char* const compares[] = {"cmpps", "cmppd", "cmpss", "cmpsd", "vcmpps", "vcmppd", "vcmpss", "vcmpsd", "vcmpph", "vcmpsh", "vcmpbf16", "vpcmpb", "vpcmpw", "vpcmpd", "vpcmpq", "vpcmpub", "vpcmpuw", "vpcmpud", "vpcmpuq", "vpcomb", "vpcomw", "vpcomd", "vpcomq", "vpcomub", "vpcomuw", "vpcomud", "vpcomuq"};
    char predicate[2][64] = {{0}};
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(compares); i += 1)
    {
        const bool integer = i >= 11;
        const uint32_t prefix = integer ? 5 : i < 4 ? 3 : i == 10 ? 6 : 4;
        const uint32_t limit = integer || i < 4 ? 8 : 32;
        if (strcmp(expected, compares[i]) == 0 && immediate >= 0 && (uint32_t)immediate < limit)
        {
            const char* condition = i >= 19 ? xop_conditions[immediate] : integer ? integer_conditions[immediate] : float_conditions[immediate];
            snprintf(predicate[0], sizeof(predicate[0]), "%.*s%s%s", (int)prefix, expected, condition, expected + prefix);
            if (!integer && immediate < 16)
            {
                snprintf(predicate[1], sizeof(predicate[1]), "%.*s%s%s", (int)prefix, expected, short_conditions[immediate], expected + prefix);
            }
        }
    }
    if ((strcmp(expected, "pclmulqdq") == 0 || strcmp(expected, "vpclmulqdq") == 0) && (immediate == 0 || immediate == 1 || immediate == 2 || immediate == 3 || immediate == 16 || immediate == 17))
    {
        static const char* const halves[4] = {"lqlq", "hqlq", "lqhq", "hqhq"};
        // GNU objdump also displays 02/03 as the high-second-source aliases (observed in 2.47), although the
        // architectural selector is bit 4. This is a display-only allowance: GNU-as roundtrip still checks the
        // exact immediate byte, and our formatter retains the explicit immediate rather than that lossy alias.
        const uint32_t half = immediate == 2 || immediate == 3 ? (uint32_t)immediate : (immediate & 1) + ((immediate >> 3) & 2);
        snprintf(predicate[0], sizeof(predicate[0]), "%spclmul%sdq", expected[0] == 'v' ? "v" : "", halves[half]);
    }
    bool match = false;
    for (uint32_t alias = 0; alias <= BUSTER_ARRAY_LENGTH(aliases) + 2 && !match; alias += 1)
    {
        const char* name = alias == 0 ? expected : alias > BUSTER_ARRAY_LENGTH(aliases) ? predicate[alias - BUSTER_ARRAY_LENGTH(aliases) - 1] : strcmp(expected, aliases[alias - 1][0]) == 0 ? aliases[alias - 1][1] : 0;
        if (name != 0 && *name != 0)
        {
            const uint64_t length = strlen(name);
            const char* at = actual;
            while (*at != 0 && !match)
            {
                while (*at == ' ' || *at == '\t')
                {
                    at += 1;
                }
                const char* end = at;
                // GNU annotates obsolete x87 instructions as, for example, fsetpm(287 only).
                while (*end != 0 && *end != ' ' && *end != '\t' && *end != '\n' && *end != '(')
                {
                    end += 1;
                }
                match = (uint64_t)(end - at) == length && memcmp(at, name, length) == 0;
                match = match || (word_relative && (uint64_t)(end - at) == length + 1 && at[length] == 'w' && memcmp(at, name, length) == 0);
                at = *end != 0 ? end + 1 : end;
            }
        }
    }
    return match;
}

static bool x86_decode_test_objdump(uint64_t* state, uint32_t count, const char* raw, bool catalogue)
{
    if (catalogue)
    {
        count = 0;
        for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
        {
            if (x86_decode_form_supported(row))
            {
                count += 1 + !x86_form_is_bound_memory(row);
            }
        }
    }
    char directory[] = "/tmp/buster-disassembler-XXXXXX";
    bool valid = mkdtemp(directory) != 0;
    char binary[256];
    char listing[256];
    char assembly[256];
    char object[256];
    char assembled[256];
    char log[256];
    snprintf(binary, sizeof(binary), "%s/input.bin", directory);
    snprintf(listing, sizeof(listing), "%s/objdump.txt", directory);
    snprintf(assembly, sizeof(assembly), "%s/decoded.s", directory);
    snprintf(object, sizeof(object), "%s/decoded.o", directory);
    snprintf(assembled, sizeof(assembled), "%s/decoded.bin", directory);
    snprintf(log, sizeof(log), "%s/tools.txt", directory);
    const uint32_t generated_count = count;
    uint32_t capacity = count * 16 + 64;
    struct stat info;
    if (raw != 0)
    {
        valid = valid && stat(raw, &info) == 0 && info.st_size > 0 && info.st_size <= 16000000;
        capacity = valid ? (uint32_t)info.st_size : 0;
        count = capacity;
    }
    uint8_t* bytes = malloc(capacity);
    X86Decoded* expected = calloc(raw != 0 ? count : count * 2, sizeof(*expected));
    valid = valid && bytes != 0 && expected != 0;
    uint32_t size = 0;
    if (valid && raw != 0)
    {
        FILE* input = fopen(raw, "rb");
        valid = input != 0 && fread(bytes, 1, capacity, input) == capacity;
        if (input != 0)
        {
            fclose(input);
        }
        size = capacity;
        // The whole file decodes; objdump then checks every record's boundary and mnemonic.
        const X86DecodeResult decoded = x86_disassemble(bytes, size, expected, count);
        valid = valid && decoded.status == X86_DECODE_OK && decoded.consumed == size;
        count = (uint32_t)decoded.count;
        if (!valid)
        {
            fprintf(stderr, "raw oracle coverage failed at %llu/%u bytes, status=%u\n", (unsigned long long)decoded.consumed, size, decoded.status);
        }
    }
    uint32_t catalogue_cursor = 2;
    uint32_t published = 0;
    for (uint32_t i = 0; valid && raw == 0 && i < generated_count; i += 1)
    {
        x86_test_mode = (i & 1) ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64;
        uint16_t form = 0;
        if (catalogue)
        {
            // Every admitted row at both address sizes, independent of the random stream's distribution.
            while (form == 0)
            {
                const uint16_t candidate = (uint16_t)(catalogue_cursor >> 1);
                const bool address32 = (catalogue_cursor & 1) != 0;
                catalogue_cursor += 1;
                if (x86_decode_form_supported(candidate) && !(address32 && x86_form_is_bound_memory(candidate)))
                {
                    form = candidate;
                    x86_test_mode = address32 ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64;
                }
            }
        }
        else
        {
            form = x86_random_form(state, false);
            while (!x86_decode_form_supported(form) || (x86_test_mode == X86_ENCODER_MODE_ADDRESS32 && x86_form_is_bound_memory(form)))
            {
                form = x86_random_form(state, false);
            }
        }
        const X86Instruction instruction = x86_random_instruction(state, form, true);
        const uint32_t length = encode_one(&instruction, bytes + size, x86_test_mode);
        expected[published].instruction = instruction;
        expected[published].offset = size;
        expected[published].length = (uint8_t)length;
        published += 1;
        size += length;
        if (form == X86_FORM_FWAIT)
        {
            // GNU objdump folds repeated FWAITs into a following waiting-x87 instruction. Isolate the
            // standalone row from the next generated row, so every catalogue boundary is independently known.
            expected[published] = (X86Decoded){.instruction = {.form = X86_FORM_NOP}, .offset = size, .length = 1};
            bytes[size] = 0x90;
            size += 1;
            published += 1;
        }
    }
    if (raw == 0)
    {
        count = published;
    }
    FILE* output = valid ? fopen(binary, "wb") : 0;
    valid = valid && output != 0 && fwrite(bytes, 1, size, output) == size;
    if (output != 0)
    {
        valid = fclose(output) == 0 && valid;
    }
    output = valid && raw == 0 ? fopen(assembly, "w") : 0;
    valid = valid && (raw != 0 || output != 0);
    if (valid && raw == 0)
    {
        fprintf(output, ".intel_syntax noprefix\n.arch .mpx\n.arch .amx_transpose\n.arch .amx_tf32\n.text\n");
        for (uint32_t i = 0; i < count && valid; i += 1)
        {
            X86Decoded decoded[16];
            const X86DecodeResult result = x86_disassemble(bytes + expected[i].offset, expected[i].length, decoded, BUSTER_ARRAY_LENGTH(decoded));
            char text[512];
            valid = result.status == X86_DECODE_OK && result.count == 1 && x86_disassembly_format(decoded, text, sizeof(text)) >= 0;
            if (valid)
            {
                fprintf(output, "%s\n", text);
            }
            else
            {
                fprintf(stderr, "oracle preparation failed for instruction %u (%s), status=%u count=%llu\n", i, x86_form_description(expected[i].instruction.form)->mnemonic, result.status, (unsigned long long)result.count);
            }
        }
    }
    if (output != 0)
    {
        valid = fclose(output) == 0 && valid;
    }
    if (valid && raw == 0)
    {
        char* const arguments[] = {"as", assembly, "-o", object, 0};
        valid = x86_decode_test_process(arguments, log);
    }
    if (valid && raw == 0)
    {
        char* const arguments[] = {"objcopy", "-O", "binary", "-j", ".text", object, assembled, 0};
        valid = x86_decode_test_process(arguments, log);
    }
    FILE* rebuilt = valid && raw == 0 ? fopen(assembled, "rb") : 0;
    valid = valid && (raw != 0 || rebuilt != 0);
    if (rebuilt != 0)
    {
        for (uint32_t offset = 0; offset < size && valid; offset += 1)
        {
            const int byte = fgetc(rebuilt);
            valid = byte == bytes[offset];
            if (!valid)
            {
                uint32_t instruction = 0;
                while (instruction + 1 < count && expected[instruction + 1].offset <= offset)
                {
                    instruction += 1;
                }
                fprintf(stderr, "GNU-as operand roundtrip mismatch at byte %u, instruction %u (%s), expected=%02x actual=%02x\n", offset, instruction, x86_form_description(expected[instruction].instruction.form)->mnemonic, bytes[offset], byte);
            }
        }
        valid = valid && fgetc(rebuilt) == EOF;
        fclose(rebuilt);
    }
    if (valid)
    {
        char* const arguments[] = {"objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel", "--insn-width=16", binary, 0};
        valid = x86_decode_test_process(arguments, listing);
    }
    FILE* input = valid ? fopen(listing, "r") : 0;
    valid = valid && input != 0;
    uint32_t seen = 0;
    char line[2048];
    const bool prepared = valid;
    uint32_t mismatches = 0;
    while (prepared && fgets(line, sizeof(line), input) != 0)
    {
        char* end;
        const uint64_t offset = strtoull(line, &end, 16);
        if (end != line && *end == ':')
        {
            char* cursor = end + 1;
            uint32_t length = 0;
            while (true)
            {
                while (*cursor == ' ' || *cursor == '\t')
                {
                    cursor += 1;
                }
                if (!isxdigit((unsigned char)cursor[0]) || !isxdigit((unsigned char)cursor[1]) || !isspace((unsigned char)cursor[2]))
                {
                    break;
                }
                length += 1;
                cursor += 2;
            }
            X86Decoded decoded[16];
            const X86DecodeResult result = offset < size && length <= size - offset ? x86_disassemble(bytes + offset, length, decoded, BUSTER_ARRAY_LENGTH(decoded)) : (X86DecodeResult){.status = X86_DECODE_BAD_ARGUMENT};
            bool same = seen < count && offset == expected[seen].offset && length == expected[seen].length && result.status == X86_DECODE_OK && result.count == 1 && result.consumed == length;
            same = same && x86_decode_test_mnemonic(x86_form_description(decoded[0].instruction.form)->mnemonic, decoded[0].instruction.immediate, cursor);
            if (!same && mismatches < 64)
            {
                fprintf(stderr, "objdump oracle mismatch at instruction %u: %s", seen, line);
                if (seen < count)
                {
                    fprintf(stderr, "expected offset=%u length=%u mnemonic=%s; decoder status=%u count=%llu\n", expected[seen].offset, expected[seen].length, x86_form_description(expected[seen].instruction.form)->mnemonic, result.status, (unsigned long long)result.count);
                }
            }
            mismatches += !same;
            seen += 1;
        }
    }
    valid = valid && mismatches == 0 && seen == count;
    if (input != 0)
    {
        fclose(input);
    }
    if (valid)
    {
        unlink(binary);
        unlink(listing);
        unlink(assembly);
        unlink(object);
        unlink(assembled);
        unlink(log);
        rmdir(directory);
    }
    else
    {
        fprintf(stderr, "disassembler oracle diagnostics retained in %s\n", directory);
    }
    free(expected);
    free(bytes);
    x86_test_mode = X86_ENCODER_MODE_64;
    printf("disassembler GNU objdump oracle (%s): %u/%u instructions, %s\n", raw != 0 ? raw : catalogue ? "catalogue, operands reassembled" : "generated, operands reassembled", seen, count, valid ? "passed" : "FAILED");
    return valid;
}

// The lane index (x86_decode_lane_indices, x86_decode_cells, x86_decode_lists) against its definition, a scan of the
// bucket's compact signatures (x86_decode_compact): for a key, the rows whose signatures match, in catalogue order;
// none, one, or the cell's list. Keys are each signature with random bits where its mask doesn't look, and random keys.
static uint64_t x86_decode_test_index(uint64_t* state)
{
    uint64_t failures = 0;
    uint64_t checked = 0;
    uint64_t listed = 0;
    for (uint32_t b = 0; b < BUSTER_X86_DECODE_BUCKETS; b += 1)
    {
        const X86DecodeBucket bucket = x86_decode_buckets[b];
        const X86DecodeLaneIndex* index = &x86_decode_lane_indices[b];
        for (uint32_t sample = 0; sample < bucket.count * 4 + (bucket.count != 0 ? 16 : 0); sample += 1)
        {
            const uint32_t entry = bucket.first + sample / 4;
            const uint32_t noise = (uint32_t)x86_test_random(state) & ((1u << 24) - 1);
            const uint32_t key = sample < bucket.count * 4
                                     ? x86_decode_compact(x86_decode_keys[entry], false) | (noise & ~x86_decode_compact(x86_decode_masks[entry], true))
                                     : noise;
            uint16_t expected[65];
            uint32_t matches = 0;
            for (uint32_t j = 0; j < bucket.count; j += 1)
            {
                const uint32_t candidate = bucket.first + j;
                expected[matches] = x86_decode_forms[candidate];
                matches += (key & x86_decode_compact(x86_decode_masks[candidate], true)) == x86_decode_compact(x86_decode_keys[candidate], false);
            }
            expected[matches] = 0;
            const bool common = (key & index->common_mask) == index->common_key;
            const uint16_t cell = common ? x86_decode_cells[index->first + _pext_u32(key, index->varying)] : 0;
            const bool list = (cell & BUSTER_X86_DECODE_LIST) != 0;
            const uint16_t single[2] = { cell, 0 };
            const uint16_t* actual = list ? x86_decode_lists + (cell & ~BUSTER_X86_DECODE_LIST) : single;
            bool same = list == (matches > 1);
            for (uint32_t k = 0; same && k <= matches; k += 1)
            {
                same = actual[k] == expected[k];
            }
            if (!same && failures < 8)
            {
                fprintf(stderr, "disassembler index: bucket %u key %06x gives %u (list %u), its signatures %u forms, the first %u\n", b, key, cell, list, matches, expected[0]);
            }
            failures += !same;
            listed += list;
            checked += 1;
        }
    }
    printf("disassembler index: %llu keys in every bucket against their signatures (%llu by a cell's list), failures=%llu\n", (unsigned long long)checked,
           (unsigned long long)listed, (unsigned long long)failures);
    return failures;
}

// The length pass's hints (x86_decode_lengths) at random instances of every supported form, in 64-bit and x32
// addressing: an instruction's hint is its length, or none for a bucket the tails couldn't describe (0F 78's, whose
// immediates differ between unprefixed and F2 forms); no other hint is wrong, and no other bucket has none.
static uint64_t x86_decode_test_hints(uint64_t* state)
{
    uint64_t failures = x86_decode_tails_unknown != 1;
    uint64_t checked = 0;
    uint64_t unhinted = 0;
    const X86EncoderMode saved = x86_test_mode;
    for (uint32_t address32 = 0; address32 < 2; address32 += 1)
    {
        x86_test_mode = address32 ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64;
        for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
        {
            for (uint32_t sample = 0; sample < 4 && x86_decode_form_supported(row) && !(address32 && x86_form_is_bound_memory(row)); sample += 1)
            {
                const X86Instruction instruction = x86_random_instruction(state, (uint16_t)row, false);
                uint8_t bytes[128] = {0};
                const uint32_t size = encode_one(&instruction, bytes, x86_test_mode);
                uint8_t lengths[64];
                X86DecodeWindow window;
                x86_decode_lengths(bytes, size, lengths, &window);
                const X86FormDescription* form = &x86_form_descriptions[row];
                uint32_t opcode_index;
                const uint32_t map = x86_decode_form_map(form, &opcode_index);
                const bool known = x86_decode_tails[x86_decode_bucket(form->family, map, x86_opcode_byte(form, opcode_index))] != 0;
                const bool right = known ? lengths[0] == size : lengths[0] == 0 || lengths[0] == size;
                if (!right && failures < 8)
                {
                    fprintf(stderr, "disassembler hints: %s (%u bytes) hinted %u\n", form->mnemonic, size, lengths[0]);
                }
                failures += !right;
                unhinted += lengths[0] == 0;
                checked += 1;
            }
        }
    }
    x86_test_mode = saved;
    printf("disassembler hints: %llu instances' lengths, %llu without a hint (buckets without one: %u), failures=%llu\n", (unsigned long long)checked,
           (unsigned long long)unhinted, x86_decode_tails_unknown, (unsigned long long)failures);
    return failures;
}

// The prefix classifier's answer, from the prefix groups as the SDM lists them rather than from the decoder's
// tables: REX (40-4F), the segment overrides (26 2E 36 3E 64 65), operand and address size (66 67), LOCK and the
// REP forms (F0 F2 F3), and FWAIT (9B), which the decoder reads as the prefix of a waiting x87 form.
static uint64_t x86_decode_test_prefix_mask(const uint8_t* bytes, uint32_t length)
{
    static const uint8_t prefixes[] = {0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67, 0x9b, 0xf0, 0xf2, 0xf3};
    uint64_t mask = 0;
    for (uint32_t i = 0; i < length; i += 1)
    {
        bool prefix = (bytes[i] & 0xf0) == 0x40;
        for (uint32_t j = 0; j < sizeof(prefixes); j += 1)
        {
            prefix = prefix || bytes[i] == prefixes[j];
        }
        mask |= (uint64_t)prefix << i;
    }
    return mask;
}

// The decoder's own contract, checked without a second decoder (#2924). Records tile the consumed bytes from the
// start, and the stop is explained. Each record decodes alone to itself: a one-instruction slice is a window of one
// lane, where the stream's record was one of up to 16. Its text is the same at every capacity that holds it (the planned
// writer at a full buffer, the bounded writer at its exact length). The listing's address and byte columns are
// the ones written here, and the record re-encodes to an instruction that decodes back to it. Bytes that stop the
// decode decode alone to nothing, with the same status.
static bool x86_decode_test_check(const uint8_t* bytes, uint32_t size, bool verbose)
{
    X86Decoded records[128];
    memset(records, 0xa5, sizeof(records));
    const X86DecodeResult result = x86_disassemble(bytes, size, records, BUSTER_ARRAY_LENGTH(records));
    bool valid = result.count <= BUSTER_ARRAY_LENGTH(records);
    valid = valid && (result.status == X86_DECODE_OK || result.status == X86_DECODE_UNSUPPORTED || result.status == X86_DECODE_TRUNCATED || result.status == X86_DECODE_OUTPUT_FULL);
    uint64_t end = 0;
    for (uint64_t i = 0; valid && i < result.count; i += 1)
    {
        valid = records[i].offset == end && records[i].length != 0 && records[i].length <= 15 && records[i].reserved == 0 && end + records[i].length <= size;
        end += records[i].length;
    }
    valid = valid && end == result.consumed && (result.status != X86_DECODE_OK || result.consumed == size);
    valid = valid && (result.status != X86_DECODE_OUTPUT_FULL || (result.count == BUSTER_ARRAY_LENGTH(records) && result.consumed < size));
    // Nothing is written past the published records: the poisoned tail still equals its own first byte, 0xa5.
    const uint8_t* tail = valid ? (const uint8_t*)&records[result.count] : 0;
    const uint64_t tail_size = valid ? (BUSTER_ARRAY_LENGTH(records) - result.count) * sizeof(*records) : 0;
    valid = valid && (tail_size == 0 || (tail[0] == 0xa5 && memcmp(tail, tail + 1, tail_size - 1) == 0));
    const char* failure = valid ? 0 : "records don't tile the consumed bytes";
    for (uint64_t i = 0; valid && i < result.count; i += 1)
    {
        const X86Decoded* decoded = &records[i];
        X86Decoded alone[2];
        memset(alone, 0x5a, sizeof(alone));
        const X86DecodeResult single = x86_disassemble(bytes + decoded->offset, decoded->length, alone, BUSTER_ARRAY_LENGTH(alone));
        valid = single.status == X86_DECODE_OK && single.count == 1 && single.consumed == decoded->length && alone[0].offset == 0 && alone[0].length == decoded->length &&
                alone[0].address32 == decoded->address32 && memcmp(&alone[0].instruction, &decoded->instruction, sizeof(X86Instruction)) == 0;
        failure = valid ? 0 : "a record decodes alone to another";
        char text[512];
        char exact[512];
        const int64_t text_length = valid ? x86_disassembly_format(decoded, text, sizeof(text)) : -1;
        valid = valid && text_length >= 0 && (uint64_t)text_length == strlen(text);
        const int64_t exact_length = valid ? x86_disassembly_format(decoded, exact, (uint64_t)text_length + 1) : -1;
        valid = valid && exact_length == text_length && memcmp(exact, text, (uint64_t)text_length + 1) == 0;
        failure = valid || failure != 0 ? failure : "the text depends on the capacity";
        if (valid)
        {
            // Eight address digits, two spaces, fifteen three-byte slots (two hex digits and a space, or three
            // spaces past the instruction) and one more space.
            static const char digits[] = "0123456789abcdef";
            const uint32_t base = 0xfedcba98u;
            const uint32_t address = base + decoded->offset;
            char columns[56];
            memset(columns, ' ', sizeof(columns));
            for (uint32_t j = 0; j < 8; j += 1)
            {
                columns[j] = digits[(address >> (28 - 4 * j)) & 15];
            }
            for (uint32_t j = 0; j < decoded->length; j += 1)
            {
                columns[10 + 3 * j] = digits[bytes[decoded->offset + j] >> 4];
                columns[11 + 3 * j] = digits[bytes[decoded->offset + j] & 15];
            }
            char listing[600];
            const int64_t listing_length = x86_disassembly_listing(decoded, bytes, base, listing, sizeof(listing));
            valid = listing_length == 56 + text_length && memcmp(listing, columns, 56) == 0 && memcmp(listing + 56, text, (uint64_t)text_length + 1) == 0;
            failure = valid ? 0 : "the listing's columns or text differ";
        }
        uint8_t encoded[32];
        const X86EncoderMode mode = decoded->address32 ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64;
        valid = valid && instruction_is_valid(&decoded->instruction, mode);
        const uint32_t length = valid ? encode_one(&decoded->instruction, encoded, mode) : 0;
        X86Decoded roundtrip[16];
        const X86DecodeResult again = x86_disassemble(encoded, length, roundtrip, BUSTER_ARRAY_LENGTH(roundtrip));
        valid = valid && again.status == X86_DECODE_OK && again.count == 1 && memcmp(&decoded->instruction, &roundtrip[0].instruction, sizeof(X86Instruction)) == 0;
        failure = valid || failure != 0 ? failure : "the record doesn't round-trip through the encoder";
    }
    if (valid && (result.status == X86_DECODE_UNSUPPORTED || result.status == X86_DECODE_TRUNCATED))
    {
        X86Decoded rest[4];
        const X86DecodeResult stop = x86_disassemble(bytes + result.consumed, size - result.consumed, rest, BUSTER_ARRAY_LENGTH(rest));
        valid = stop.status == result.status && stop.count == 0 && stop.consumed == 0;
        failure = valid ? 0 : "the stopping bytes decode alone to another answer";
    }
    if (!valid && verbose)
    {
        fprintf(stderr, "disassembler check failed (%s): length=%u status=%u count=%llu consumed=%llu bytes=", failure, size, result.status, (unsigned long long)result.count, (unsigned long long)result.consumed);
        for (uint32_t i = 0; i < size && i < 64; i += 1)
        {
            fprintf(stderr, "%02x", bytes[i]);
        }
        fprintf(stderr, "\n");
    }
    return valid;
}

// Every slice of a stream keeps the stream's records that end inside it, and a slice that ends on a record
// boundary decodes to exactly those records: no record's decode depends on the bytes after it.
static uint64_t x86_decode_test_prefixes(const uint8_t* bytes, uint32_t size, bool verbose)
{
    X86Decoded whole[128];
    X86Decoded part[128];
    const X86DecodeResult full = x86_disassemble(bytes, size, whole, BUSTER_ARRAY_LENGTH(whole));
    uint64_t failures = 0;
    uint64_t complete = 0;
    for (uint32_t length = 0; length <= size; length += 1)
    {
        while (complete < full.count && whole[complete].offset + whole[complete].length <= length)
        {
            complete += 1;
        }
        const X86DecodeResult result = x86_disassemble(bytes, length, part, BUSTER_ARRAY_LENGTH(part));
        const bool boundary = complete == 0 ? length == 0 : whole[complete - 1].offset + whole[complete - 1].length == length;
        bool same = result.count >= complete && memcmp(part, whole, complete * sizeof(*part)) == 0;
        same = same && (!boundary || (result.status == X86_DECODE_OK && result.count == complete && result.consumed == length));
        if (!same && verbose)
        {
            fprintf(stderr, "disassembler slice %u of %u bytes lost the stream's records: %llu kept of %llu, status=%u\n", length, size, (unsigned long long)result.count, (unsigned long long)complete, result.status);
        }
        failures += !same;
        failures += !x86_decode_test_check(bytes, length, verbose);
    }
    return failures;
}

// A supported encoding cut short is TRUNCATED, never UNSUPPORTED: the matcher leaves the missing bytes out of the
// signatures it compares. A cut can first complete shorter instructions (9B DB E3's FWAIT).
static uint64_t x86_decode_test_truncations(const uint8_t* bytes, uint32_t length, bool verbose)
{
    uint64_t failures = 0;
    for (uint32_t cut = 0; cut < length; cut += 1)
    {
        X86Decoded records[16];
        const X86DecodeResult result = x86_disassemble(bytes, cut, records, BUSTER_ARRAY_LENGTH(records));
        const bool valid = result.status == X86_DECODE_TRUNCATED || (result.status == X86_DECODE_OK && result.consumed == cut);
        if (!valid && verbose && failures == 0)
        {
            fprintf(stderr, "disassembler: %u of a %u-byte instruction's bytes decode with status %u, not truncated\n", cut, length, result.status);
        }
        failures += !valid;
    }
    return failures;
}

// Bytes the encoder produced decode to the instructions that produced them: the same boundaries, each record
// re-encoding to its own bytes (alias-proof), and, where the record is the generated instruction itself, counted.
static bool x86_decode_test_generated(const uint8_t* bytes, uint32_t size, const X86Instruction* instructions, const uint8_t* lengths, uint32_t count, X86EncoderMode mode, uint64_t* identical)
{
    X86Decoded records[64];
    const X86DecodeResult result = count <= BUSTER_ARRAY_LENGTH(records) ? x86_disassemble(bytes, size, records, BUSTER_ARRAY_LENGTH(records)) : (X86DecodeResult){.status = X86_DECODE_BAD_ARGUMENT};
    bool valid = result.status == X86_DECODE_OK && result.count == count && result.consumed == size;
    uint32_t offset = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        uint8_t encoded[32];
        const uint32_t length = records[i].offset == offset && records[i].length == lengths[i] ? encode_one(&records[i].instruction, encoded, mode) : 0;
        valid = length == lengths[i] && memcmp(encoded, bytes + offset, length) == 0;
        *identical += valid && memcmp(&records[i].instruction, &instructions[i], sizeof(X86Instruction)) == 0;
        offset += lengths[i];
    }
    return valid;
}

static uint64_t x86_decode_test_simple(uint64_t seed)
{
    bool present[X86_FORM_COUNT] = {0};
    uint16_t forms[X86_FORM_COUNT];
    uint32_t form_count = 0;
    // The ordinary legacy forms (no prefix, a one-byte opcode or 0F's, general registers, the compact checks) whose
    // opcode has a length hint: dense streams of them fill whole windows of lanes.
    for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
    {
        const X86FormDescription* form = &x86_form_descriptions[row];
        const uint8_t second = x86_opcode_byte(form, 1);
        const uint32_t map = form->opcode_bytes == 1 ? 0 : form->opcode_bytes == 2 && x86_opcode_byte(form, 0) == 0x0f && second != 0x38 && second != 0x3a ? 1 : 2;
        const bool ordinary = x86_decode_form_supported(row) && (x86_decode_checks[row].flags & 1) && form->family == BUSTER_X86_FAMILY_LEGACY && form->prefixes == 0 &&
                              map < 2 && form->opcode != 0x9b && form->opcode != 0x90 && (form->reg & BUSTER_X86_OPERAND_CLASS) <= BUSTER_X86_CLASS_GPR64 &&
                              (form->rm & BUSTER_X86_OPERAND_CLASS) <= BUSTER_X86_CLASS_GPR64;
        if (ordinary && x86_decode_legacy_tails[map * 256 + (form->opcode & 0xff)] != 0x0f && !present[row])
        {
            present[row] = true;
            forms[form_count++] = row;
        }
    }
    uint64_t failures = form_count == 0;
    uint64_t state = seed ^ 0xd73b4e29156ac08full;
    const X86EncoderMode saved = x86_test_mode;
    x86_test_mode = X86_ENCODER_MODE_64;
    uint32_t streams = 0;
    uint64_t identical = 0;
    for (uint32_t batch = 0; batch < 4096 && failures == 0; batch += 1)
    {
        uint8_t storage[32 * 15 + 64];
        uint8_t* bytes = storage + (batch & 63);
        uint32_t size = 0;
        X86Instruction instructions[32];
        uint8_t lengths[32];
        for (uint32_t i = 0; i < 32; i += 1)
        {
            const uint16_t row = forms[x86_random_below(&state, form_count)];
            instructions[i] = x86_random_instruction(&state, row, false);
            lengths[i] = (uint8_t)encode_one(&instructions[i], bytes + size, X86_ENCODER_MODE_64);
            size += lengths[i];
        }
        failures += !x86_decode_test_generated(bytes, size, instructions, lengths, 32, X86_ENCODER_MODE_64, &identical);
        failures += !x86_decode_test_check(bytes, size, true);
        // The first 64 streams exercise every byte tail at every alignment. The rest vary one tail and four
        // mutations per stream, keeping dense eligible runs around varied bases, indices, high bytes and immediates.
        if (batch < 64)
        {
            failures += x86_decode_test_prefixes(bytes, size, true);
        }
        else
        {
            failures += !x86_decode_test_check(bytes, x86_random_below(&state, size), true);
        }
        for (uint32_t mutation = 0; mutation < 4; mutation += 1)
        {
            const uint32_t at = x86_random_below(&state, size);
            const uint8_t original = bytes[at];
            bytes[at] ^= (uint8_t)(1u << x86_random_below(&state, 8));
            failures += !x86_decode_test_check(bytes, size, true);
            bytes[at] = original;
        }
        if (failures != 0)
        {
            fprintf(stderr, "ordinary lane stream failed: seed=%llu batch=%u bytes=%u\n", (unsigned long long)seed, batch, size);
        }
        streams += 1;
    }
    x86_test_mode = saved;
    printf("disassembler lane streams: %u streams of 32 instructions, %u forms, %llu records the generated instruction itself, failures=%llu\n", streams, form_count, (unsigned long long)identical, (unsigned long long)failures);
    return failures;
}

static uint64_t x86_decode_test_format_bounds(uint64_t* state)
{
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, (uint64_t)page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0;
    uint64_t failures = !mapped;
    uint64_t checked = 0;
    if (mapped)
    {
        for (uint32_t address32 = 0; address32 < 2; address32 += 1)
        {
            x86_test_mode = address32 ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64;
            for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
            {
                if (x86_decode_form_supported(row) && !(address32 && x86_form_is_bound_memory(row)))
                {
                    const X86Instruction instruction = x86_random_instruction(state, (uint16_t)row, false);
                    uint8_t bytes[32];
                    const uint32_t size = encode_one(&instruction, bytes, x86_test_mode);
                    X86Decoded decoded[16] = {0};
                    const X86DecodeResult result = x86_disassemble(bytes, size, decoded, BUSTER_ARRAY_LENGTH(decoded));
                    // The reference is the planned writer's text at a full buffer; smaller capacities that still hold it
                    // take the bounded writer, which must write the same.
                    char reference[512];
                    const int64_t length = result.status == X86_DECODE_OK && result.count == 1 ? x86_disassembly_format(decoded, reference, sizeof(reference)) : -1;
                    const X86DisassemblyPlan* plan = &x86_disassembly_plans[x86_form_description(decoded[0].instruction.form) - x86_form_descriptions];
                    const uint32_t bound = plan->bound;
                    failures += length < 0 || bound == 0 || bound > sizeof(reference) || (uint64_t)length + 8 > bound;
                    if (length >= 0 && bound <= sizeof(reference))
                    {
                        for (uint32_t style = 0; style < 2; style += 1)
                        {
                            const int64_t expected_length = style == 0 ? length : x86_disassembly_listing(decoded, bytes, 0xfedcba98u, reference, sizeof(reference));
                            failures += expected_length < 0;
                            for (uint32_t capacity = 0; capacity <= bound + 8 + style * 56; capacity += 1)
                            {
                                char* output = (char*)mapping + 2 * page - capacity;
                                output[-1] = (char)0xa5;
                                const int64_t actual = style == 0 ? x86_disassembly_format(decoded, output, capacity) : x86_disassembly_listing(decoded, bytes, 0xfedcba98u, output, capacity);
                                const bool same = capacity <= (uint64_t)expected_length ? actual == -1 : actual == expected_length && memcmp(output, reference, (uint64_t)expected_length + 1) == 0;
                                failures += !same || (uint8_t)output[-1] != 0xa5;
                                checked += 1;
                            }
                        }
                    }
                }
            }
        }
        munmap(mapping, (uint64_t)page * 3);
    }
    else if (mapping != MAP_FAILED)
    {
        munmap(mapping, (uint64_t)page * 3);
    }
    x86_test_mode = X86_ENCODER_MODE_64;
    printf("disassembler formatter bounds: %llu guarded capacities, failures=%llu\n", (unsigned long long)checked, (unsigned long long)failures);
    return failures;
}

static bool x86_decode_test_same_file(const char* left, const char* right)
{
    FILE* a = fopen(left, "rb");
    FILE* b = fopen(right, "rb");
    bool valid = a != 0 && b != 0;
    uint8_t bytes_a[4096];
    uint8_t bytes_b[4096];
    while (valid)
    {
        const uint64_t size_a = fread(bytes_a, 1, sizeof(bytes_a), a);
        const uint64_t size_b = fread(bytes_b, 1, sizeof(bytes_b), b);
        valid = size_a == size_b && memcmp(bytes_a, bytes_b, size_a) == 0 && !ferror(a) && !ferror(b);
        if (size_a == 0 || size_b == 0)
        {
            break;
        }
    }
    if (a != 0)
    {
        fclose(a);
    }
    if (b != 0)
    {
        fclose(b);
    }
    return valid;
}

static uint64_t x86_decode_test_cli(void)
{
    char directory[] = "/tmp/buster-disassembler-cli-XXXXXX";
    bool valid = mkdtemp(directory) != 0;
    char binary[256];
    char expected[256];
    char actual[256];
    snprintf(binary, sizeof(binary), "%s/input.bin", directory);
    snprintf(expected, sizeof(expected), "%s/expected.txt", directory);
    snprintf(actual, sizeof(actual), "%s/actual.txt", directory);
    // 3,073 instructions cross both the 1,024-record decode chunks and the text buffer's flush boundary.
    static const uint8_t pattern[] = {0x90, 0x48, 0x89, 0xc8, 0x48, 0x83, 0xc0, 0x01, 0x75, 0xf8, 0xc5, 0xf8, 0x77, 0xc3};
    uint8_t bytes[sizeof(pattern) * 512 + 1];
    for (uint32_t i = 0; i < 512; i += 1)
    {
        memcpy(bytes + i * sizeof(pattern), pattern, sizeof(pattern));
    }
    bytes[sizeof(bytes) - 1] = 0x90;
    X86Decoded* decoded = malloc(sizeof(bytes) * sizeof(*decoded));
    valid = valid && decoded != 0;
    uint64_t failures = !valid;
    uint64_t checked = 0;
    const uint32_t sizes[] = {0, 1, sizeof(bytes)};
    for (uint32_t sample = 0; valid && sample < BUSTER_ARRAY_LENGTH(sizes); sample += 1)
    {
        const uint32_t size = sizes[sample];
        FILE* input = fopen(binary, "wb");
        valid = input != 0 && fwrite(bytes, 1, size, input) == size;
        if (input != 0)
        {
            valid = fclose(input) == 0 && valid;
        }
        const X86DecodeResult result = x86_disassemble(bytes, size, decoded, sizeof(bytes));
        valid = valid && result.status == X86_DECODE_OK && result.consumed == size;
        for (uint32_t assembly = 0; valid && assembly < 2; assembly += 1)
        {
            FILE* reference = fopen(expected, "wb");
            valid = reference != 0;
            if (valid && assembly)
            {
                valid = fprintf(reference, ".intel_syntax noprefix\n.arch .mpx\n.arch .amx_transpose\n.arch .amx_tf32\n.text\n") >= 0;
            }
            for (uint32_t i = 0; valid && i < result.count; i += 1)
            {
                // The listing's address and byte columns are written here, independently of the command's.
                if (!assembly)
                {
                    valid = fprintf(reference, "%08x  ", decoded[i].offset) >= 0;
                    for (uint32_t j = 0; j < 15; j += 1)
                    {
                        valid = (j < decoded[i].length ? fprintf(reference, "%02x ", bytes[decoded[i].offset + j]) : fprintf(reference, "   ")) >= 0 && valid;
                    }
                    valid = fprintf(reference, " ") >= 0 && valid;
                }
                char text[512];
                valid = x86_disassembly_format(decoded + i, text, sizeof(text)) >= 0 && valid;
                if (valid)
                {
                    valid = fprintf(reference, "%s\n", text) >= 0;
                }
            }
            if (reference != 0)
            {
                valid = fclose(reference) == 0 && valid;
            }
            if (valid)
            {
                char* arguments[5] = {"/proc/self/exe", "disassemble"};
                uint32_t count = 2;
                if (assembly)
                {
                    arguments[count++] = "--assembly";
                }
                arguments[count] = binary;
                const bool same = x86_decode_test_process(arguments, actual) && x86_decode_test_same_file(expected, actual);
                failures += !same;
                checked += 1;
                // Empty listing writes nothing. Every other case must catch even a delayed stdio flush error.
                if (size != 0 || assembly)
                {
                    failures += x86_decode_test_process(arguments, "/dev/full");
                    checked += 1;
                }
            }
        }
        failures += !valid;
    }
    free(decoded);
    if (failures == 0)
    {
        unlink(binary);
        unlink(expected);
        unlink(actual);
        rmdir(directory);
    }
    else
    {
        fprintf(stderr, "disassembler CLI diagnostics retained in %s\n", directory);
    }
    printf("disassembler CLI: %llu output/status checks, failures=%llu\n", (unsigned long long)checked, (unsigned long long)failures);
    return failures;
}

static uint64_t x86_decode_test_fixtures(void)
{
    // Hand-written bytes and semantic operands, independent of the form table and encoder. Include deliberately
    // non-shortest disp32 and VEX3 encodings, Clang's nine-byte alignment NOP, high EVEX registers and FS addressing.
    static const struct
    {
        uint8_t bytes[15];
        uint8_t length;
        uint8_t reg;
        uint8_t rm;
        uint8_t index;
        uint8_t vvvv;
        uint8_t mode;
        uint8_t decorations;
        uint8_t address32;
        int32_t displacement;
        const char* mnemonic;
        int32_t immediate;
        uint8_t operand_bytes;
        uint8_t broadcast_bytes;
    } fixtures[] =
    {
        {{0x48, 0x8b, 0x83, 1, 0, 0, 0}, 7, 0, 3, 0, 0, BUSTER_X86_MEMORY_BASE, 0, 0, 1, "mov"},
        {{0x66, 0x0f, 0x1f, 0x84, 0, 0, 0, 0, 0}, 9, 0, 0, 0, 0, BUSTER_X86_MEMORY_BASE_INDEX(0), 0, 0, 0, "nop"},
        {{0xc4, 0xe1, 0x78, 0x58, 0xc1}, 5, 0, 1, 0, 0, 0, 0, 0, 0, "vaddps"},
        {{0x62, 0xf1, 0x6c, 0x48, 0x58, 0xcb}, 6, 1, 3, 0, 2, 0, 0, 0, 0, "vaddps"},
        {{0x62, 0x91, 0x6c, 0xc3, 0x58, 0xcc}, 6, 1, 28, 0, 18, 0, 0x83, 0, 0, "vaddps"},
        {{0x64, 0x48, 0x8b, 4, 0x25, 0, 0, 0, 0}, 9, 0, 0, 0, 0, BUSTER_X86_MEMORY_ABSOLUTE, 0, 0, 0, "mov"},
        {{0x8b, 5, 0x78, 0x56, 0x34, 0x12}, 6, 0, 0, 0, 0, BUSTER_X86_MEMORY_RIP, 0, 0, 0x12345678, "mov"},
        {{0xeb, 0xfe}, 2, 0, 0, 0, 0, 0, 0, 0, -2, "jmp"},
        {{0x67, 0x8b, 4, 0x8d, 0, 0x10, 0, 0}, 8, 0, 0, 1, 0, BUSTER_X86_MEMORY_INDEX(2), 0, 1, 4096, "mov"},
        {{0x48, 0x83, 0xc0, 0xff}, 4, 0, 0, 0, 0, 0, 0, 0, 0, "add", 255, 8},
        {{0xe8, 0xfc, 0xff, 0xff, 0xff}, 5, 0, 0, 0, 0, 0, 0, 0, -4, "call"},
        {{0x62, 0xf1, 0x6c, 0x58, 0x58, 0x4b, 1}, 7, 1, 3, 0, 2, BUSTER_X86_MEMORY_BASE, 0, 0, 4, "vaddps", 0, 64, 4},
        {{0x62, 0xf1, 0x6c, 0x78, 0x58, 0xcb}, 6, 1, 3, 0, 2, 0, 0x60, 0, 0, "vaddps", 0, 64},
        {{0x0f, 0xff, 0xc1}, 3, 0, 1, 0, 0, 0, 0, 0, 0, "ud0", 0, 4},
        {{0x0f, 0xb9, 0xc1}, 3, 0, 1, 0, 0, 0, 0, 0, 0, "ud1", 0, 4},
        {{0x41, 0x90}, 2, 0, 8, 0, 0, 0, 0, 0, 0, "xchg", 0, 4},
        {{0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0, 0, 0, 0, 0}, 15, 0, 0, 0, 0, BUSTER_X86_MEMORY_BASE_INDEX(0), 0, 0, 0, "nop", 0, 2},
        {{0x62, 0xf1, 0x7c, 0x48, 0xc2, 0xca, 1}, 7, 1, 2, 0, 0, 0, 0, 0, 0, "vcmpps", 1, 64},
        {{0x66, 0x44, 0x0f, 0x3a, 0x44, 0xd9, 0x02}, 7, 11, 1, 0, 0, 0, 0, 0, 0, "pclmulqdq", 2, 16},
    };
    uint64_t failures = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        uint8_t storage[96];
        for (uint32_t alignment = 0; alignment < 64; alignment += 1)
        {
            memcpy(storage + alignment, fixtures[i].bytes, fixtures[i].length);
            X86Decoded decoded[16];
            const X86DecodeResult result = x86_disassemble(storage + alignment, fixtures[i].length, decoded, BUSTER_ARRAY_LENGTH(decoded));
            const X86Instruction* instruction = &decoded[0].instruction;
            const bool valid = result.status == X86_DECODE_OK && result.count == 1 && result.consumed == fixtures[i].length &&
                               instruction->reg == fixtures[i].reg && instruction->rm == fixtures[i].rm && instruction->index == fixtures[i].index &&
                               instruction->vvvv == fixtures[i].vvvv && instruction->mode == fixtures[i].mode && instruction->decorations == fixtures[i].decorations &&
                               instruction->displacement == fixtures[i].displacement && decoded[0].address32 == fixtures[i].address32 &&
                               strcmp(x86_form_description(instruction->form)->mnemonic, fixtures[i].mnemonic) == 0 &&
                               instruction->immediate == fixtures[i].immediate &&
                               (fixtures[i].operand_bytes == 0 || x86_form_description(instruction->form)->operand_bytes == fixtures[i].operand_bytes) &&
                               (fixtures[i].broadcast_bytes == 0 || ((x86_form_description(instruction->form)->evex & BUSTER_X86_EVEX_BROADCAST) && x86_form_description(instruction->form)->disp8_scale == fixtures[i].broadcast_bytes));
            failures += !valid;
            if (!valid && alignment == 0)
            {
                fprintf(stderr, "independent fixture %u failed, status=%u count=%llu\n", i, result.status, (unsigned long long)result.count);
            }
            failures += !x86_decode_test_check(storage + alignment, fixtures[i].length, true);
            if (valid)
            {
                char formatted[512];
                const int64_t length = x86_disassembly_format(decoded, formatted, sizeof(formatted));
                failures += length < 0;
                for (uint32_t capacity = 0; length >= 0 && capacity <= (uint32_t)length; capacity += 1)
                {
                    char small[512];
                    memset(small, 0xa5, sizeof(small));
                    failures += x86_disassembly_format(decoded, small, capacity) != -1;
                    for (uint32_t j = capacity; j < sizeof(small); j += 1)
                    {
                        failures += (uint8_t)small[j] != 0xa5;
                    }
                }
            }
        }
    }
    static const uint8_t bad[][15] =
    {
        {0x62, 0xf9, 0x7c, 0x48, 0x58, 0xc0},
        {0x62, 0xf1, 0x78, 0x48, 0x58, 0xc0},
        {0x62, 0xf1, 0x7c, 0xc8, 0x58, 0xc0},
        {0xc5, 0xe0, 0x77},
        {0x0f, 0xff},
        {0x0f, 0x20, 0xc8},
        // APX BMI NF: EVEX maps 1--3 do not by themselves identify an AVX-512 instruction.
        {0x62, 0x52, 0x44, 0x0c, 0xf2, 0xee},
        {0x62, 0xf2, 0x7c, 0x0c, 0xf7, 0xfd},
        {0x0f, 0xb9},
        {0x8d},
        {0x0f, 0x01},
        {0xc4, 0xe1},
        {0x62, 0xf1, 0x7c},
        {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f},
        {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f},
    };
    static const uint8_t bad_lengths[] = {6, 6, 6, 3, 2, 3, 6, 6, 2, 1, 2, 2, 3, 15, 8};
    static const X86DecodeStatus bad_status[] =
    {
        X86_DECODE_UNSUPPORTED,
        X86_DECODE_UNSUPPORTED,
        X86_DECODE_UNSUPPORTED,
        X86_DECODE_UNSUPPORTED,
        X86_DECODE_TRUNCATED,
        X86_DECODE_UNSUPPORTED,
        X86_DECODE_UNSUPPORTED,
        X86_DECODE_UNSUPPORTED,
        X86_DECODE_TRUNCATED,
        X86_DECODE_TRUNCATED,
        X86_DECODE_TRUNCATED,
        X86_DECODE_TRUNCATED,
        X86_DECODE_TRUNCATED,
        X86_DECODE_UNSUPPORTED,
        X86_DECODE_TRUNCATED,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bad); i += 1)
    {
        X86Decoded output[8];
        const X86DecodeResult result = x86_disassemble(bad[i], bad_lengths[i], output, BUSTER_ARRAY_LENGTH(output));
        failures += result.status != bad_status[i] || result.count != 0 || result.consumed != 0;
        if (result.status != bad_status[i])
        {
            fprintf(stderr, "invalid fixture %u status=%u expected=%u\n", i, result.status, bad_status[i]);
        }
    }
    uint8_t nops[128];
    static const uint8_t waits[] = {0x9b, 0x9b, 0xdb, 0xe3};
    X86Decoded waiting[4];
    const X86DecodeResult wait_result = x86_disassemble(waits, sizeof(waits), waiting, BUSTER_ARRAY_LENGTH(waiting));
    failures += wait_result.status != X86_DECODE_OK || wait_result.count != 2 || wait_result.consumed != sizeof(waits) ||
                waiting[0].length != 1 || waiting[0].instruction.form != X86_FORM_FWAIT || waiting[1].length != 3 || waiting[1].instruction.form != X86_FORM_FINIT;
    failures += !x86_decode_test_check(waits, sizeof(waits), true);
    memset(nops, 0x90, sizeof(nops));
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, (uint64_t)page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0;
    failures += !mapped;
    if (mapped)
    {
        for (uint32_t capacity = 0; capacity <= 128; capacity += 1)
        {
            X86Decoded* output = (X86Decoded*)(mapping + 2 * page - capacity * sizeof(X86Decoded));
            const X86DecodeResult result = x86_disassemble(nops, sizeof(nops), output, capacity);
            failures += result.count != capacity || result.consumed != capacity || result.status != (capacity == 128 ? X86_DECODE_OK : X86_DECODE_OUTPUT_FULL);
        }
        static const uint8_t pattern[] = {0x48, 0x8b, 0x44, 0x24, 8, 0x48, 0x83, 0xe8, 3, 0x75, 0xf5, 0x41, 0x54};
        uint8_t stream[sizeof(pattern) * 8];
        for (uint32_t i = 0; i < sizeof(stream); i += 1)
        {
            stream[i] = pattern[i % sizeof(pattern)];
        }
        X86Decoded whole[64];
        const X86DecodeResult full = x86_disassemble(stream, sizeof(stream), whole, BUSTER_ARRAY_LENGTH(whole));
        failures += full.status != X86_DECODE_OK || full.count != 32 || full.consumed != sizeof(stream);
        for (uint32_t capacity = 0; capacity <= 32; capacity += 1)
        {
            X86Decoded* output = (X86Decoded*)(mapping + 2 * page - capacity * sizeof(X86Decoded));
            const X86DecodeResult actual = x86_disassemble(stream, sizeof(stream), output, capacity);
            // A smaller capacity keeps the first records of the whole stream's decode and stops where they end.
            const uint64_t kept = capacity < full.count ? capacity : full.count;
            failures += actual.count != kept || actual.status != (kept < full.count ? X86_DECODE_OUTPUT_FULL : X86_DECODE_OK) ||
                        actual.consumed != (kept == 0 ? 0 : whole[kept - 1].offset + whole[kept - 1].length) || memcmp(whole, output, kept * sizeof(*output)) != 0;
        }
        for (uint32_t length = 0; length <= sizeof(stream); length += 1)
        {
            memcpy(mapping + 2 * page - length, stream, length);
            failures += !x86_decode_test_check(mapping + 2 * page - length, length, true);
        }
        munmap(mapping, (uint64_t)page * 3);
    }
    X86Decoded output;
    failures += x86_disassemble(0, 0, 0, 0).status != X86_DECODE_OK;
    failures += x86_disassemble(0, 1, &output, 1).status != X86_DECODE_BAD_ARGUMENT;
    failures += x86_disassemble(nops, UINT64_MAX, &output, 1).status != X86_DECODE_BAD_ARGUMENT;
    failures += x86_disassemble(nops, 1, 0, 1).status != X86_DECODE_BAD_ARGUMENT;
    return failures;
}

static int run_x86_disassembler_tests(int argc, char** argv)
{
    uint64_t count = 100000;
    uint64_t seed = 1;
    bool oracle = false;
    const char* raw = 0;
    bool arguments = true;
    for (int i = 0; i < argc; i += 1)
    {
        if ((strcmp(argv[i], "--count") == 0 || strcmp(argv[i], "--seed") == 0) && i + 1 < argc)
        {
            char* end;
            const uint64_t value = strtoull(argv[i + 1], &end, 10);
            arguments = arguments && *end == 0 && value != 0 && value <= 10000000;
            if (strcmp(argv[i], "--count") == 0)
            {
                count = value;
            }
            else
            {
                seed = value;
            }
            i += 1;
        }
        else if (strcmp(argv[i], "--objdump") == 0)
        {
            oracle = true;
        }
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        {
            raw = argv[++i];
        }
        else
        {
            arguments = false;
        }
    }
    uint64_t failures = 0;
    uint64_t checked = 0;
    // The objdump paths need binutils 2.46's forms and listings (2.47 is the qualified 9700X version): as reassembles
    // the catalogue's operands and objdump decodes them, and objcopy (any version: only -O binary -j .text) extracts the
    // reassembled bytes. Probe each once; a missing or older one skips those sections and leaves the rest's failures to
    // fail the command. The raw --file oracle runs objdump alone.
    const uint32_t minimum[] = { 2, 46 };
    TestTool as_tool = { 0 };
    TestTool objdump_tool = { 0 };
    TestTool objcopy_tool = { 0 };
    bool oracle_ready = false;
    bool raw_ready = false;
    bool skipped = false;
    if (oracle || raw != 0)
    {
        as_tool = test_tool_probe("as", "as", minimum, 2);
        objdump_tool = test_tool_probe("objdump", "objdump", minimum, 2);
        if (oracle)
        {
            objcopy_tool = test_tool_probe("objcopy", "objcopy", 0, 0);
        }
        oracle_ready = oracle && test_tool_available(&as_tool) && test_tool_available(&objdump_tool) && test_tool_available(&objcopy_tool);
        raw_ready = raw != 0 && test_tool_available(&objdump_tool);
    }
    if (oracle && !oracle_ready)
    {
        if (!test_tool_available(&as_tool))
        {
            test_tool_skip(&as_tool, "disassembler GNU objdump oracle");
        }
        if (!test_tool_available(&objdump_tool))
        {
            test_tool_skip(&objdump_tool, "disassembler GNU objdump oracle");
        }
        if (!test_tool_available(&objcopy_tool))
        {
            test_tool_skip(&objcopy_tool, "disassembler GNU objdump oracle");
        }
        skipped = true;
    }
    if (raw != 0 && !raw_ready)
    {
        if (!test_tool_available(&objdump_tool))
        {
            test_tool_skip(&objdump_tool, "disassembler GNU objdump oracle (raw file)");
        }
        skipped = true;
    }
    bool initialized = arguments && x86_disassembler_initialize();
    if (initialized)
    {
        uint64_t state = seed;
        failures += x86_decode_test_fixtures();
        failures += x86_decode_test_simple(seed);
        failures += x86_decode_test_format_bounds(&state);
        failures += x86_decode_test_index(&state);
        failures += x86_decode_test_hints(&state);
        failures += x86_decode_test_cli();
        uint8_t bytes[128];
        uint64_t classified = 0;
        for (uint32_t byte = 0; byte < 256; byte += 1)
        {
            memset(bytes, byte, sizeof(bytes));
            for (uint32_t length = 0; length <= 64; length += 1)
            {
                failures += x86_decode_classify(bytes, length) != x86_decode_test_prefix_mask(bytes, length);
                classified += 1;
            }
        }
        for (uint32_t sample = 0; sample < 4096; sample += 1)
        {
            for (uint32_t i = 0; i < 64; i += 1)
            {
                bytes[i] = (uint8_t)x86_test_random(&state);
            }
            const uint32_t length = x86_random_below(&state, 65);
            failures += x86_decode_classify(bytes, length) != x86_decode_test_prefix_mask(bytes, length);
            classified += 1;
        }
        printf("disassembler prefix classifier: %llu windows against the prefix table, failures=%llu\n", (unsigned long long)classified, (unsigned long long)failures);
        uint64_t forms = 0;
        uint64_t identical = 0;
        uint8_t stream[128];
        uint32_t stream_length = 0;
        for (uint32_t address32 = 0; address32 < 2; address32 += 1)
        {
            x86_test_mode = address32 ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64;
            for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
            {
                if (x86_decode_form_supported(row) && !(address32 && x86_form_is_bound_memory(row)))
                {
                    const X86Instruction instruction = x86_random_instruction(&state, (uint16_t)row, false);
                    const uint32_t length = encode_one(&instruction, bytes, x86_test_mode);
                    X86Decoded decoded[16];
                    const X86DecodeResult result = x86_disassemble(bytes, length, decoded, BUSTER_ARRAY_LENGTH(decoded));
                    const uint8_t encoded_length = (uint8_t)length;
                    bool valid = result.status == X86_DECODE_OK && result.count == 1 && result.consumed == length && x86_decode_test_check(bytes, length, true);
                    valid = x86_decode_test_generated(bytes, length, &instruction, &encoded_length, 1, x86_test_mode, &identical) && valid;
                    valid = x86_decode_test_truncations(bytes, length, failures < 30) == 0 && valid;
                    if (!valid && failures < 30)
                    {
                        fprintf(stderr, "form=%u %s address32=%u length=%u status=%u count=%llu bytes=", row, x86_form_descriptions[row].mnemonic, address32, length, result.status, (unsigned long long)result.count);
                        for (uint32_t i = 0; i < length; i += 1)
                        {
                            fprintf(stderr, "%02x", bytes[i]);
                        }
                        fprintf(stderr, "\n");
                    }
                    failures += !valid;
                    checked += 1;
                    forms += 1;
                }
            }
        }
        printf("disassembler catalogue: %llu instances, %llu records the generated instruction itself, failures=%llu\n", (unsigned long long)forms, (unsigned long long)identical, (unsigned long long)failures);
        identical = 0;
        for (uint64_t n = 0; n < count; n += 1)
        {
            x86_test_mode = (n & 1) ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64;
            uint16_t form = x86_random_form(&state, false);
            while (!x86_decode_form_supported(form))
            {
                form = x86_random_form(&state, false);
            }
            const X86Instruction instruction = x86_random_instruction(&state, form, false);
            const uint32_t length = encode_one(&instruction, bytes, x86_test_mode);
            X86Decoded decoded[16];
            const X86DecodeResult result = x86_disassemble(bytes, length, decoded, BUSTER_ARRAY_LENGTH(decoded));
            const uint8_t encoded_length = (uint8_t)length;
            bool valid = result.status == X86_DECODE_OK && result.count == 1 && result.consumed == length;
            valid = x86_decode_test_generated(bytes, length, &instruction, &encoded_length, 1, x86_test_mode, &identical) && valid;
            valid = x86_decode_test_truncations(bytes, length, failures < 30) == 0 && valid;
            if (!valid && failures < 30)
            {
                fprintf(stderr, "random form=%u %s mode=%u length=%u status=%u count=%llu bytes=", form, x86_form_descriptions[form].mnemonic, x86_test_mode, length, result.status, (unsigned long long)result.count);
                for (uint32_t i = 0; i < length; i += 1)
                {
                    fprintf(stderr, "%02x", bytes[i]);
                }
                fprintf(stderr, "\n");
            }
            failures += !valid;
            checked += 1;
            if (stream_length + length > sizeof(stream))
            {
                failures += !x86_decode_test_check(stream, stream_length, true);
                stream_length = 0;
            }
            memcpy(stream + stream_length, bytes, length);
            stream_length += length;
            // Every slice of the instruction, the instruction itself included, keeps the decoder's contract.
            failures += x86_decode_test_prefixes(bytes, length, failures < 30);
            for (uint32_t i = 0; i < length; i += 1)
            {
                // Mutate each existing byte so prefix, opcode, ModRM/SIB and immediate fields reach meaningful
                // candidate validation, unlike random streams which often stop at their first unsupported opcode.
                const uint8_t original = bytes[i];
                bytes[i] ^= (uint8_t)(1u << (n & 7));
                failures += !x86_decode_test_check(bytes, length, false);
                bytes[i] = original;
            }
            const uint32_t random_size = x86_random_below(&state, 128);
            for (uint32_t i = 0; i < random_size; i += 1)
            {
                bytes[i] = (uint8_t)x86_test_random(&state);
            }
            failures += !x86_decode_test_check(bytes, random_size, true);
        }
        const long page = sysconf(_SC_PAGESIZE);
        uint8_t* mapping = mmap(0, (uint64_t)page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const bool mapped = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0;
        failures += !mapped;
        if (mapped)
        {
            for (uint32_t length = 0; length <= 128; length += 1)
            {
                for (uint32_t edge = 0; edge < 2; edge += 1)
                {
                    uint8_t* source = mapping + (edge ? 2 * page - length : page);
                    for (uint32_t i = 0; i < length; i += 1)
                    {
                        source[i] = (uint8_t)x86_test_random(&state);
                    }
                    failures += !x86_decode_test_check(source, length, true);
                    memset(source, 0x90, length);
                    failures += !x86_decode_test_check(source, length, true);
                }
            }
            munmap(mapping, (uint64_t)page * 3);
        }
        x86_test_mode = X86_ENCODER_MODE_64;
        if (oracle && oracle_ready)
        {
            failures += !x86_decode_test_objdump(&state, 0, 0, true);
            failures += !x86_decode_test_objdump(&state, 10000, 0, false);
        }
        if (raw != 0 && raw_ready)
        {
            failures += !x86_decode_test_objdump(&state, 0, raw, false);
        }
        printf("disassembler: checked=%llu random-bytes=%llu generated records the instruction itself=%llu guarded=516 failures=%llu\n", (unsigned long long)checked, (unsigned long long)count, (unsigned long long)identical, (unsigned long long)failures);
    }
    else
    {
        fprintf(stderr, "test-x86-disassembler [--count N] [--seed N] [--objdump] [--file raw-binary]: invalid arguments or initialization failed\n");
    }
    return initialized && failures == 0 ? (skipped ? 77 : 0) : 1;
}
