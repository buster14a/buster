/* Hand-encoded format fixtures keep the independent inspector independent of
 * the compiler writer. Payload bytes are intentionally not executable code. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_ARTIFACT_TEST_H
#define BUSTER_THROUGHPUT_RETIREMENT_ARTIFACT_TEST_H
#include "retirement_artifact.h"

static void test_artifact_put(unsigned char* bytes, unsigned at, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; ++i) bytes[at + i] = (unsigned char)(value >> (8 * i));
}

static unsigned test_artifact_fixture(unsigned char bytes[1024], unsigned format, unsigned machine)
{
    memset(bytes, 0, 1024);
    unsigned payload = 0;
    if (format == 1)
    {
        memcpy(bytes, "\177ELF\2\1\1", 7);
        test_artifact_put(bytes, 16, 1, 2);
        test_artifact_put(bytes, 18, machine == 1 ? 62 : 183, 2);
        test_artifact_put(bytes, 20, 1, 4);
        test_artifact_put(bytes, 40, 64, 8);
        test_artifact_put(bytes, 52, 64, 2);
        test_artifact_put(bytes, 58, 64, 2);
        test_artifact_put(bytes, 60, 2, 2);
        test_artifact_put(bytes, 132, 1, 4);
        test_artifact_put(bytes, 136, 6, 8);
        test_artifact_put(bytes, 152, 192, 8);
        test_artifact_put(bytes, 160, 13, 8);
        test_artifact_put(bytes, 176, 1, 8);
        payload = 192;
    }
    else if (format == 2 || format == 3)
    {
        unsigned header = format == 3 ? 68 : 0;
        unsigned optional = format == 3 ? 112 : 0;
        unsigned section = header + 20 + optional;
        if (format == 3)
        {
            memcpy(bytes, "MZ", 2);
            test_artifact_put(bytes, 60, 64, 4);
            memcpy(bytes + 64, "PE\0\0", 4);
            test_artifact_put(bytes, header + 18, 2, 2);
            test_artifact_put(bytes, header + 20, 0x20b, 2);
            test_artifact_put(bytes, header + 80, section + 40, 4);
            test_artifact_put(bytes, section + 8, 13, 4);
        }
        test_artifact_put(bytes, header, machine == 1 ? 0x8664 : 0xaa64, 2);
        test_artifact_put(bytes, header + 2, 1, 2);
        test_artifact_put(bytes, header + 16, optional, 2);
        memcpy(bytes + section, ".text", 5);
        test_artifact_put(bytes, section + 16, format == 3 ? 32 : 13, 4);
        payload = section + 40;
        test_artifact_put(bytes, section + 20, payload, 4);
        test_artifact_put(bytes, section + 36, 0x60000020, 4);
    }
    else if (format == 4)
    {
        test_artifact_put(bytes, 0, 0xfeedfacf, 4);
        test_artifact_put(bytes, 4, machine == 1 ? 0x01000007 : 0x0100000c, 4);
        test_artifact_put(bytes, 12, 1, 4);
        test_artifact_put(bytes, 16, 1, 4);
        test_artifact_put(bytes, 20, 152, 4);
        test_artifact_put(bytes, 32, 0x19, 4);
        test_artifact_put(bytes, 36, 152, 4);
        test_artifact_put(bytes, 72, 184, 8);
        test_artifact_put(bytes, 80, 13, 8);
        test_artifact_put(bytes, 96, 1, 4);
        memcpy(bytes + 104, "__text", 6);
        test_artifact_put(bytes, 144, 13, 8);
        test_artifact_put(bytes, 152, 184, 4);
        test_artifact_put(bytes, 168, 0x80000400, 4);
        payload = 184;
    }
    memcpy(bytes + payload, "fixture-code\n", 13);
    return payload + (format == 3 ? 32 : 13);
}

static void test_retirement_artifact(char const* executable, char const* root)
{
    unsigned char bytes[1024], changed[1024];
    char expected[65];
    Sha256 hash;
    sha256_init(&hash); sha256_add(&hash, "fixture-code\n", 13); sha256_finish_hex(&hash, expected);
    TpRetirementArtifact facts;
    for (unsigned format = 1; format <= 4; ++format)
    {
        for (unsigned machine = 1; machine <= 2; ++machine)
        {
            unsigned size = test_artifact_fixture(bytes, format, machine);
            CHECK(tp_retirement_artifact(bytes, size, &facts));
            CHECK(facts.format == format && facts.machine == machine && facts.sections == 1);
            CHECK(facts.file_bytes == size && facts.code_bytes == 13 && !strcmp(facts.code_sha256, expected));
            CHECK(facts.executable == (format == 3));
            CHECK(strcmp(facts.file_sha256, facts.code_sha256));
            /* Every prefix is malformed. PE padding remains required even
             * though it is excluded from the code metric. */
            for (unsigned prefix = 0; prefix < size; ++prefix)
            {
                CHECK(!tp_retirement_artifact(bytes, prefix, &facts));
                CHECK(!facts.file_bytes && !facts.code_bytes && !facts.file_sha256[0]);
            }
            CHECK(!tp_retirement_artifact(NULL, size, &facts));
            CHECK(!tp_retirement_artifact(bytes, size, NULL));
            CHECK(!tp_retirement_artifact(bytes, TP_RETIREMENT_ARTIFACT_BYTES + 1, &facts));
            char path[TP_PATH_CAP], leaf[64];
            snprintf(leaf, sizeof(leaf), "retirement-artifact-%u-%u.bin", format, machine);
            CHECK(tp_path(path, root, leaf));
            FILE* out = fopen(path, "wb");
            CHECK(out && fwrite(bytes, 1, size, out) == size);
            if (out) CHECK(fclose(out) == 0);
        }
    }
    /* ELF: data sections cannot masquerade as code, duplicate payloads and
     * header overlap cannot double count, and extended/foreign formats fail. */
    unsigned size = test_artifact_fixture(bytes, 1, 1);
    unsigned const offsets[] = {4, 5, 18, 20, 40, 52, 58, 60, 132, 152, 160, 176};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
    {
        memcpy(changed, bytes, sizeof(bytes));
        changed[offsets[i]] = 255;
        CHECK(!tp_retirement_artifact(changed, size, &facts));
    }
    memcpy(changed, bytes, sizeof(bytes));
    test_artifact_put(changed, 136, 2, 8);
    CHECK(tp_retirement_artifact(changed, size, &facts) && !facts.code_bytes);
    test_artifact_put(changed, 136, 0x806, 8);
    CHECK(!tp_retirement_artifact(changed, size, &facts));
    test_artifact_put(changed, 136, 6, 8);
    test_artifact_put(changed, 152, 32, 8);
    CHECK(!tp_retirement_artifact(changed, size, &facts));
    test_artifact_put(changed, 152, UINT64_MAX - 3, 8);
    CHECK(!tp_retirement_artifact(changed, size, &facts));
    /* Reversed section-table order still hashes physical payload order. The
     * same section ranges may not be counted twice or hide behind data flags. */
    test_artifact_fixture(bytes, 1, 1);
    memset(bytes + 192, 0, 64);
    test_artifact_put(bytes, 60, 3, 2);
    test_artifact_put(bytes, 152, 260, 8);
    test_artifact_put(bytes, 160, 4, 8);
    memcpy(bytes + 192, bytes + 128, 64);
    test_artifact_put(bytes, 216, 256, 8);
    memcpy(bytes + 256, "abcdefgh", 8);
    sha256_init(&hash); sha256_add(&hash, "abcdefgh", 8); sha256_finish_hex(&hash, facts.code_sha256);
    char ordered[65];
    memcpy(ordered, facts.code_sha256, sizeof(ordered));
    CHECK(tp_retirement_artifact(bytes, 264, &facts) && facts.code_bytes == 8 && !strcmp(facts.code_sha256, ordered));
    test_artifact_put(bytes, 216, 260, 8);
    CHECK(!tp_retirement_artifact(bytes, 264, &facts));
    test_artifact_put(bytes, 200, 2, 8);
    CHECK(!tp_retirement_artifact(bytes, 264, &facts));
    /* COFF BSS is a virtual size with no on-disk payload. */
    size = test_artifact_fixture(bytes, 2, 1);
    test_artifact_put(bytes, 40, 0, 4);
    test_artifact_put(bytes, 56, 0x80, 4);
    CHECK(tp_retirement_artifact(bytes, size, &facts) && !facts.code_bytes);
    test_artifact_put(bytes, 56, 0x20000080, 4);
    CHECK(!tp_retirement_artifact(bytes, size, &facts));
    size = test_artifact_fixture(bytes, 3, 1);
    /* PE raw alignment padding changes file identity, never code identity. */
    bytes[size - 1] = 127;
    CHECK(tp_retirement_artifact(bytes, size, &facts) && facts.code_bytes == 13 && !strcmp(facts.code_sha256, expected));
    test_artifact_put(bytes, 208, 33, 4);
    CHECK(!tp_retirement_artifact(bytes, size, &facts));
    size = test_artifact_fixture(bytes, 4, 2);
    unsigned const macho_offsets[] = {4, 12, 16, 20, 36, 96, 144, 152};
    for (unsigned i = 0; i < sizeof(macho_offsets) / sizeof(macho_offsets[0]); ++i)
    {
        memcpy(changed, bytes, sizeof(bytes)); changed[macho_offsets[i]] = 255;
        CHECK(!tp_retirement_artifact(changed, size, &facts));
    }
    test_artifact_put(bytes, 168, 0x80000401, 4);
    CHECK(!tp_retirement_artifact(bytes, size, &facts));
    /* The host's actual test executable covers linked ELF/PE/Mach-O section
     * populations, including real debug metadata in sanitizer builds. */
    FILE* input = fopen(executable, "rb");
    CHECK(input != NULL);
    if (input)
    {
        CHECK(fseek(input, 0, SEEK_END) == 0);
        long length = ftell(input);
        CHECK(length > 0 && (uint64_t)length <= TP_RETIREMENT_ARTIFACT_BYTES);
        unsigned char* data = length > 0 ? (unsigned char*)malloc((size_t)length) : NULL;
        CHECK(data != NULL && fseek(input, 0, SEEK_SET) == 0);
        if (data)
        {
            CHECK(fread(data, 1, (size_t)length, input) == (size_t)length);
            CHECK(tp_retirement_artifact(data, (uint64_t)length, &facts));
            CHECK(facts.executable && facts.code_bytes > 0 && facts.code_bytes < facts.file_bytes);
        }
        free(data);
        CHECK(fclose(input) == 0);
    }
}
#endif
