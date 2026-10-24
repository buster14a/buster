// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #360: failures through initialization and actual __TIMESTAMP__ expansion, in both preprocessing modes.
// Included by c_preprocessor_test.c; its timestamp test owns and restores TZ and SOURCE_DATE_EPOCH.

static bool pp_test_time_errors(const char* path)
{
    static const struct
    {
        const char* epoch;
        const char* diagnostic;
    } invalid[] = {
        { "", "invalid SOURCE_DATE_EPOCH" },
        { "garbage", "invalid SOURCE_DATE_EPOCH" },
        { "123garbage", "invalid SOURCE_DATE_EPOCH" },
        { "123 ", "invalid SOURCE_DATE_EPOCH" },
        { " 123", "invalid SOURCE_DATE_EPOCH" },
        { "\t123", "invalid SOURCE_DATE_EPOCH" },
        { "+123", "invalid SOURCE_DATE_EPOCH" },
        { "-1", "invalid SOURCE_DATE_EPOCH" },
        { "-0", "invalid SOURCE_DATE_EPOCH" },
        { "0x10", "invalid SOURCE_DATE_EPOCH" },
        { "1.0", "invalid SOURCE_DATE_EPOCH" },
        { "9223372036854775808", "invalid SOURCE_DATE_EPOCH" },
        { "999999999999999999999999999999999999999", "invalid SOURCE_DATE_EPOCH" },
        { "9223372036854775807", "can't convert SOURCE_DATE_EPOCH" },
        { "67768036191676799", "outside the supported calendar range" },
        { "253402300800", "outside the supported calendar range" },
    };
    static const struct
    {
        int64_t seconds;
        bool epoch;
    } modified[] = {
        { INT64_MAX, false },
        { INT64_MIN, false },
        { 253402300800, false },
        { INT64_MAX, true },
    };
    uint32_t failures = 0;
    for (uint32_t k = 0; k < 2; k += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
        {
            char* diagnostic = 0;
            size_t length = 0;
            FILE* stream = open_memstream(&diagnostic, &length);
            const PPOptions options = {
                .language = { .standard = 2017, .gnu = true },
                .scalar = k != 0,
                .no_standard_includes = true,
                .diagnostics = stream,
            };
            InternTable table = { 0 };
            static Preprocessor pp;
            pp = (Preprocessor){ 0 };
            bool result = stream != 0 && setenv("SOURCE_DATE_EPOCH", invalid[i].epoch, 1) == 0 && intern_initialize(&table);
            if (result)
            {
                const bool initialized = pp_initialize(&pp, &options, &table);
                result = !initialized && pp.fatal && pp.errors == 1 && pp.output.count == 0 && !pp.has_epoch &&
                         pp.date[0] == 0 && pp.time[0] == 0 && pp.timestamp[0] == 0;
            }
            if (stream != 0)
            {
                result = fclose(stream) == 0 && result;
            }
            result = result && diagnostic != 0 && strstr(diagnostic, invalid[i].diagnostic) != 0;
            if (!result)
            {
                printf("timestamp refusal: mode %u, epoch '%s': %s\n", k, invalid[i].epoch, diagnostic != 0 ? diagnostic : "no diagnostic");
            }
            failures += !result;
            free(diagnostic);
            pp_release(&pp);
            intern_release(&table);
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(modified); i += 1)
        {
            char* diagnostic = 0;
            size_t length = 0;
            FILE* stream = open_memstream(&diagnostic, &length);
            const PPOptions options = {
                .language = { .standard = 2017, .gnu = true },
                .scalar = k != 0,
                .no_standard_includes = true,
                .diagnostics = stream,
            };
            InternTable table = { 0 };
            static Preprocessor pp;
            pp = (Preprocessor){ 0 };
            bool result = stream != 0 && (modified[i].epoch ? setenv("SOURCE_DATE_EPOCH", "0", 1) : unsetenv("SOURCE_DATE_EPOCH")) == 0 &&
                          intern_initialize(&table) && pp_initialize(&pp, &options, &table);
            if (result)
            {
                const uint32_t file = pp_lookup_path(&pp, path, (uint32_t)strlen(path), BUSTER_PP_USER_FILE);
                result = file != BUSTER_PP_NONE && pp.files[file].data != BUSTER_PP_NONE;
                if (result)
                {
                    // File systems may clamp extreme mtimes. Set the metadata at its consumer boundary instead,
                    // then run the real file and builtin expansion, retaining its source location for the diagnostic.
                    PPFileData* data = &pp.datas[pp.files[file].data];
                    data->modified = modified[i].seconds;
                    data->has_modified = true;
                    const bool ran = pp_run(&pp, path);
                    result = modified[i].epoch ? ran && !pp.fatal && pp.errors == 0 && pp.output.count == 2
                                              : !ran && pp.fatal && pp.errors == 1 && pp.output.count == 0;
                }
            }
            if (stream != 0)
            {
                result = fclose(stream) == 0 && result;
            }
            result = result && diagnostic != 0 && (modified[i].epoch ? length == 0 : strstr(diagnostic, "timestamp.c:1:1:") != 0 &&
                     strstr(diagnostic, "file modification time for __TIMESTAMP__") != 0);
            if (!result)
            {
                printf("timestamp mtime: mode %u, case %u: %s\n", k, i, diagnostic != 0 ? diagnostic : "no diagnostic");
            }
            failures += !result;
            free(diagnostic);
            pp_release(&pp);
            intern_release(&table);
        }
    }
    printf("timestamp errors: %u of %lu invalid-epoch and file-mtime cases fail\n", failures,
           (unsigned long)(2 * (BUSTER_ARRAY_LENGTH(invalid) + BUSTER_ARRAY_LENGTH(modified))));
    return failures == 0;
}
