// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original raw forced-include context controls. Include after pp_test_prelude_failures and the file-writing helpers.

static bool pp_test_raw_forced_include_contexts(const char* directory)
{
    char header[PATH_MAX];
    char failed[PATH_MAX];
    const int named = snprintf(header, sizeof(header), "%s/context\"1616.h", directory);
    const int failed_name = snprintf(failed, sizeof(failed), "%s/fatal\"1616.h", directory);
    const bool paths = named > 0 && (size_t)named < sizeof(header) && failed_name > 0 && (size_t)failed_name < sizeof(failed);
    bool result = paths && pp_test_write_file(header, "#define RAW_CONTEXT_1616 1\n") &&
                  pp_test_write_file(failed, "#include \"missing-raw-context-1616.h\"\n");
    for (uint32_t scalar = 0; result && scalar < 2; scalar += 1)
    {
        for (uint32_t fault = 0; result && fault < 4; fault += 1)
        {
            static InternTable table;
            static Preprocessor pp;
            char* diagnostics = NULL;
            size_t diagnostics_length = 0;
            FILE* stream = open_memstream(&diagnostics, &diagnostics_length);
            const PPOptions options = {
                .language = { .standard = 2017, .gnu = true },
                .scalar = scalar != 0,
                .diagnostics = stream,
            };
            bool checked = stream != NULL && intern_initialize(&table) && pp_initialize(&pp, &options, &table);
            if (checked)
            {
                const uint32_t ranges = pp.range_count;
                if (fault == 1)
                {
                    // Refuse the synthetic parent before it can push a file context.
                    pp.options.location_limit = pp.location_top + 1;
                }
                else if (fault == 2)
                {
                    // Admit only the synthetic parent's range, then refuse the raw file's range.
                    pp.options.location_limit = pp.location_top + sizeof("#if 0\n#endif\n") + 1;
                }
                const bool ran = pp_command_include_raw(&pp, fault == 3 ? failed : header);
                fflush(stream);
                const IdentId macro = intern_find_or_add(&table, (const uint8_t*)"RAW_CONTEXT_1616", 16);
                const bool defined = macro != 0 && pp.macro_of[macro] != 0;
                const char* fatal = diagnostics != NULL ? strstr(diagnostics, "fatal error: ") : NULL;
                const bool one_fatal = fatal != NULL && strstr(fatal + 1, "fatal error: ") == NULL;
                if (fault == 0)
                {
                    checked = ran && !pp.fatal && pp.errors == 0 && pp.context_count == 0 && pp.include_depth == 0 && defined && fatal == NULL;
                }
                else if (fault == 3)
                {
                    // A fatal directive leaves the entered raw context alive: its depth still belongs to that stack.
                    checked = !ran && pp.fatal && pp.context_count == 1 && pp.include_depth == 1 && !defined && one_fatal &&
                              strstr(fatal, "file not found") != NULL && pp.range_count == ranges + 2;
                }
                else
                {
                    checked = !ran && pp.fatal && pp.context_count == 0 && pp.include_depth == 0 && !defined && one_fatal &&
                              strstr(fatal, "location space") != NULL && pp.range_count == ranges + (fault == 2);
                }
                if (!checked)
                {
                    printf("raw include context scalar %u fault %u: ran %u fatal %u contexts %u depth %u ranges %u macro %u: %s\n",
                           scalar, fault, ran, pp.fatal, pp.context_count, pp.include_depth, pp.range_count, defined,
                           diagnostics != NULL ? diagnostics : "(missing)");
                }
            }
            if (stream != NULL)
            {
                fclose(stream);
            }
            free(diagnostics);
            pp_release(&pp);
            intern_release(&table);
            result = checked;
        }
    }
    if (paths)
    {
        unlink(header);
        unlink(failed);
    }
    printf("raw forced include contexts: %s (both kernels, entry failures and fatal live context)\n", result ? "pass" : "FAIL");
    return result;
}
