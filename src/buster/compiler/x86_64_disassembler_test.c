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
    static const char* const compares[] = {"cmpps", "cmppd", "cmpss", "cmpsd", "vcmpps", "vcmppd", "vcmpss", "vcmpsd", "vcmpph", "vcmpsh", "vcmpbf16", "vpcmpb", "vpcmpw", "vpcmpd", "vpcmpq", "vpcmpub", "vpcmpuw", "vpcmpud", "vpcmpuq"};
    char predicate[2][64] = {{0}};
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(compares); i += 1)
    {
        const bool integer = i >= 11;
        const uint32_t prefix = integer ? 5 : i < 4 ? 3 : 4;
        const uint32_t limit = integer || i < 4 ? 8 : 32;
        if (strcmp(expected, compares[i]) == 0 && immediate >= 0 && (uint32_t)immediate < limit)
        {
            const char* condition = integer ? integer_conditions[immediate] : float_conditions[immediate];
            snprintf(predicate[0], sizeof(predicate[0]), "%.*s%s%s", (int)prefix, expected, condition, expected + prefix);
            if (!integer && immediate < 16)
            {
                snprintf(predicate[1], sizeof(predicate[1]), "%.*s%s%s", (int)prefix, expected, short_conditions[immediate], expected + prefix);
            }
        }
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

static bool x86_decode_test_objdump(uint64_t* state, uint32_t count, const char* raw)
{
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
    uint32_t capacity = count * 15 + 64;
    struct stat info;
    if (raw != 0)
    {
        valid = valid && stat(raw, &info) == 0 && info.st_size > 0 && info.st_size <= 16000000;
        capacity = valid ? (uint32_t)info.st_size : 0;
        count = capacity;
    }
    uint8_t* bytes = malloc(capacity);
    X86Decoded* expected = calloc(count, sizeof(*expected));
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
        const X86DecodeResult reference = x86_disassemble_scalar(bytes, size, expected, count);
        valid = valid && reference.status == X86_DECODE_OK;
        X86Decoded* actual = malloc((uint64_t)count * sizeof(*actual));
        valid = valid && actual != 0;
        if (valid)
        {
            const X86DecodeResult vectorized = x86_disassemble(bytes, size, actual, count);
            valid = vectorized.status == X86_DECODE_OK && vectorized.count == reference.count && memcmp(actual, expected, reference.count * sizeof(*actual)) == 0;
            const X86DecodeResult batched = x86_disassemble_batched(bytes, size, actual, count);
            valid = valid && batched.status == X86_DECODE_OK && batched.count == reference.count && memcmp(actual, expected, reference.count * sizeof(*actual)) == 0;
        }
        free(actual);
        count = (uint32_t)reference.count;
        if (!valid)
        {
            fprintf(stderr, "raw oracle coverage/equivalence failed at %llu/%u bytes, status=%u\n", (unsigned long long)reference.consumed, size, reference.status);
        }
    }
    for (uint32_t i = 0; valid && raw == 0 && i < count; i += 1)
    {
        x86_test_mode = (i & 1) ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64;
        uint16_t form = x86_random_form(state, false);
        while (!x86_decode_form_supported(form) || (x86_test_mode == X86_ENCODER_MODE_ADDRESS32 && x86_form_is_bound_memory(form)))
        {
            form = x86_random_form(state, false);
        }
        const X86Instruction instruction = x86_random_instruction(state, form, true);
        const uint32_t length = encode_one(&instruction, bytes + size, x86_test_mode);
        expected[i].instruction = instruction;
        expected[i].offset = size;
        expected[i].length = (uint8_t)length;
        size += length;
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
            if (!same && mismatches < 16)
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
    printf("disassembler GNU objdump oracle (%s): %u/%u instructions, %s\n", raw != 0 ? raw : "generated, operands reassembled", seen, count, valid ? "passed" : "FAILED");
    return valid;
}

static bool x86_decode_test_equal(const uint8_t* bytes, uint32_t size, bool verbose)
{
    X86Decoded vectorized[128];
    X86Decoded scalar[128];
    X86Decoded batched[128];
    memset(vectorized, 0xa5, sizeof(vectorized));
    memset(scalar, 0xa5, sizeof(scalar));
    memset(batched, 0xa5, sizeof(batched));
    const X86DecodeResult a = x86_disassemble_impl(bytes, size, vectorized, BUSTER_ARRAY_LENGTH(vectorized), true, true);
    const X86DecodeResult b = x86_disassemble_scalar(bytes, size, scalar, BUSTER_ARRAY_LENGTH(scalar));
    const X86DecodeResult c = x86_disassemble(bytes, size, batched, BUSTER_ARRAY_LENGTH(batched));
    bool result = a.count == b.count && a.consumed == b.consumed && a.status == b.status && memcmp(vectorized, scalar, sizeof(scalar)) == 0;
    result = result && a.count == c.count && a.consumed == c.consumed && a.status == c.status && memcmp(vectorized, batched, sizeof(batched)) == 0;
    for (uint64_t i = 0; i < a.count && result; i += 1)
    {
        const X86Decoded* decoded = &vectorized[i];
        uint8_t encoded[32];
        const X86EncoderMode mode = decoded->address32 ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64;
        result = instruction_is_valid(&decoded->instruction, mode);
        const uint32_t length = result ? encode_one(&decoded->instruction, encoded, mode) : 0;
        X86Decoded roundtrip[16];
        const X86DecodeResult again = x86_disassemble(encoded, length, roundtrip, BUSTER_ARRAY_LENGTH(roundtrip));
        result = result && again.status == X86_DECODE_OK && again.count == 1 && memcmp(&decoded->instruction, &roundtrip[0].instruction, sizeof(X86Instruction)) == 0;
    }
    if (!result && verbose)
    {
        fprintf(stderr, "disassembler twins disagree: length=%u statuses=%u/%u/%u counts=%llu/%llu/%llu\n", size, a.status, b.status, c.status, (unsigned long long)a.count, (unsigned long long)b.count, (unsigned long long)c.count);
    }
    return result;
}

static uint64_t x86_decode_test_simple(uint64_t seed)
{
    bool present[X86_FORM_COUNT] = {0};
    uint16_t forms[X86_FORM_COUNT];
    uint32_t form_count = 0;
    for (uint32_t entry = 0; entry < BUSTER_ARRAY_LENGTH(x86_decode_simple); entry += 1)
    {
        const uint32_t metadata = x86_decode_simple[entry];
        const uint16_t row = (uint16_t)metadata;
        if (metadata != 0 && metadata != UINT32_MAX && x86_decode_length_table[entry >> 5] != 0 && !present[row])
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
    for (uint32_t batch = 0; batch < 4096 && failures == 0; batch += 1)
    {
        uint8_t storage[32 * 15 + 64];
        uint8_t* bytes = storage + (batch & 63);
        uint32_t size = 0;
        for (uint32_t i = 0; i < 32; i += 1)
        {
            const uint16_t row = forms[x86_random_below(&state, form_count)];
            const X86Instruction instruction = x86_random_instruction(&state, row, false);
            size += encode_one(&instruction, bytes + size, X86_ENCODER_MODE_64);
        }
        failures += !x86_decode_test_equal(bytes, size, true);
        // The first 64 streams exercise every byte tail at every alignment. The rest vary one tail and four
        // mutations per stream, keeping dense eligible runs around varied bases, indices, high bytes and immediates.
        if (batch < 64)
        {
            for (uint32_t length = 0; length < size; length += 1)
            {
                failures += !x86_decode_test_equal(bytes, length, true);
            }
        }
        else
        {
            failures += !x86_decode_test_equal(bytes, x86_random_below(&state, size), true);
        }
        for (uint32_t mutation = 0; mutation < 4; mutation += 1)
        {
            const uint32_t at = x86_random_below(&state, size);
            const uint8_t original = bytes[at];
            bytes[at] ^= (uint8_t)(1u << x86_random_below(&state, 8));
            failures += !x86_decode_test_equal(bytes, size, true);
            bytes[at] = original;
        }
        if (failures != 0)
        {
            fprintf(stderr, "ordinary lane stream failed: seed=%llu batch=%u bytes=%u\n", (unsigned long long)seed, batch, size);
        }
        streams += 1;
    }
    x86_test_mode = saved;
    printf("disassembler lane streams: %u streams of 32 instructions, %u forms, failures=%llu\n", streams, form_count, (unsigned long long)failures);
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
            failures += !x86_decode_test_equal(storage + alignment, fixtures[i].length, true);
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
            const X86DecodeResult batched = x86_disassemble_batched(nops, sizeof(nops), output, capacity);
            failures += batched.count != result.count || batched.consumed != result.consumed || batched.status != result.status;
        }
        static const uint8_t pattern[] = {0x48, 0x8b, 0x44, 0x24, 8, 0x48, 0x83, 0xe8, 3, 0x75, 0xf5, 0x41, 0x54};
        uint8_t stream[sizeof(pattern) * 8];
        for (uint32_t i = 0; i < sizeof(stream); i += 1)
        {
            stream[i] = pattern[i % sizeof(pattern)];
        }
        for (uint32_t capacity = 0; capacity <= 32; capacity += 1)
        {
            X86Decoded reference[32];
            X86Decoded* output = (X86Decoded*)(mapping + 2 * page - capacity * sizeof(X86Decoded));
            const X86DecodeResult expected = x86_disassemble_scalar(stream, sizeof(stream), reference, capacity);
            const X86DecodeResult actual = x86_disassemble_batched(stream, sizeof(stream), output, capacity);
            failures += actual.status != expected.status || actual.count != expected.count || actual.consumed != expected.consumed ||
                        memcmp(reference, output, expected.count * sizeof(*output)) != 0;
        }
        for (uint32_t length = 0; length <= sizeof(stream); length += 1)
        {
            memcpy(mapping + 2 * page - length, stream, length);
            failures += !x86_decode_test_equal(mapping + 2 * page - length, length, true);
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
    bool initialized = arguments && x86_disassembler_initialize();
    if (initialized)
    {
        uint64_t state = seed;
        failures += x86_decode_test_fixtures();
        failures += x86_decode_test_simple(seed);
        uint8_t bytes[128];
        for (uint32_t byte = 0; byte < 256; byte += 1)
        {
            memset(bytes, byte, sizeof(bytes));
            for (uint32_t length = 0; length <= 64; length += 1)
            {
                failures += x86_decode_classify(bytes, length) != x86_decode_classify_scalar(bytes, length);
            }
        }
        uint64_t forms = 0;
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
                    const bool valid = result.status == X86_DECODE_OK && result.count == 1 && result.consumed == length && x86_decode_test_equal(bytes, length, true);
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
        printf("disassembler catalogue: %llu instances, failures=%llu\n", (unsigned long long)forms, (unsigned long long)failures);
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
            const bool valid = result.status == X86_DECODE_OK && result.count == 1 && result.consumed == length && x86_decode_test_equal(bytes, length, true);
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
                failures += !x86_decode_test_equal(stream, stream_length, true);
                stream_length = 0;
            }
            memcpy(stream + stream_length, bytes, length);
            stream_length += length;
            for (uint32_t i = 0; i < length; i += 1)
            {
                failures += !x86_decode_test_equal(bytes, i, false);
                // Mutate each existing byte so prefix, opcode, ModRM/SIB and immediate fields reach meaningful
                // candidate validation, unlike random streams which often stop at their first unsupported opcode.
                const uint8_t original = bytes[i];
                bytes[i] ^= (uint8_t)(1u << (n & 7));
                failures += !x86_decode_test_equal(bytes, length, false);
                bytes[i] = original;
            }
            const uint32_t random_size = x86_random_below(&state, 128);
            for (uint32_t i = 0; i < random_size; i += 1)
            {
                bytes[i] = (uint8_t)x86_test_random(&state);
            }
            failures += !x86_decode_test_equal(bytes, random_size, true);
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
                    failures += !x86_decode_test_equal(source, length, true);
                    memset(source, 0x90, length);
                    failures += !x86_decode_test_equal(source, length, true);
                }
            }
            munmap(mapping, (uint64_t)page * 3);
        }
        x86_test_mode = X86_ENCODER_MODE_64;
        if (oracle)
        {
            failures += !x86_decode_test_objdump(&state, 10000, 0);
        }
        if (raw != 0)
        {
            failures += !x86_decode_test_objdump(&state, 0, raw);
        }
        printf("disassembler: checked=%llu random-bytes=%llu guarded=516 failures=%llu\n", (unsigned long long)checked, (unsigned long long)count, (unsigned long long)failures);
    }
    else
    {
        fprintf(stderr, "test-x86-disassembler [--count N] [--seed N] [--objdump] [--file raw-binary]: invalid arguments or initialization failed\n");
    }
    return initialized && failures == 0 ? 0 : 1;
}
