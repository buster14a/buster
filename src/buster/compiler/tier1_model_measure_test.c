// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original manifest parser and operand-grouping regressions for #181. Included by tier1_model_measure.c so the
// tests exercise its private reader without a second implementation. Each fixture uses one private disk file;
// no generated program, timing loop or hardware counter runs here. Producer completion is not corpus coverage.

typedef struct Tier1ModelTest Tier1ModelTest;
struct Tier1ModelTest
{
    uint32_t checks;
    uint32_t failures;
    FILE* file;
    const char* path;
    uint8_t* allowed;
    Tier1ModelManifest* manifest;
};

static void tier1_model_test_expect(Tier1ModelTest* test, bool condition, const char* what)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 12)
        {
            fprintf(stderr, "tier1-model test: %s\n", what);
        }
        test->failures += 1;
    }
}

static bool tier1_model_test_begin(Tier1ModelTest* test)
{
    bool valid = fflush(test->file) == 0 && ftruncate(fileno(test->file), 0) == 0 && fseek(test->file, 0, SEEK_SET) == 0;
    if (valid)
    {
        valid = fputs("origin,record,form,mnemonic,mode,reg,rm,index,vvvv,decorations,immediate,displacement,cost_id,gpr_latency,throughput,retired_ops,flags_latency,load_extra,executed_uops,pipes\n", test->file) >= 0;
    }
    return valid;
}

static bool tier1_model_test_read(Tier1ModelTest* test, bool* accepted)
{
    const bool valid = !ferror(test->file) && fflush(test->file) == 0;
    *accepted = valid && tier1_model_read(test->path, test->allowed, test->manifest);
    return valid;
}

static void tier1_model_test_row(FILE* file, uint32_t serial, X86Instruction record)
{
    const uint8_t id = x86_zen5_id(&record);
    fprintf(file, "plan,%u,%u,%s,%u,%u,%u,%u,%u,%u,%d,%d,%u", serial, record.form, x86_form_description(record.form)->mnemonic, record.mode, record.reg, record.rm, record.index, record.vvvv, record.decorations, record.immediate, record.displacement, id);
    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
    {
        fprintf(file, ",%u", x86_zen5_columns[column][id]);
    }
    fputc('\n', file);
}

static void tier1_model_test_parser(Tier1ModelTest* test)
{
    static const char* const names[] =
    {
        "valid complete row",
        "unknown origin rejected",
        "nonconsecutive record rejected",
        "out-of-range immediate rejected",
        "incompatible cost ID rejected",
        "incompatible cost column rejected",
        "invalid physical register rejected",
        "missing column rejected",
        "extra column rejected",
        "unterminated record rejected",
        "negative cost ID rejected",
    };
    const X86Instruction record =
    {
        .form = X86_FORM_MOV_R32_RM32,
        .reg = X86_REGISTER_RAX,
        .rm = X86_REGISTER_RDI,
    };
    const uint8_t id = x86_zen5_id(&record);
    for (uint32_t variant = 0; variant < BUSTER_ARRAY_LENGTH(names); variant += 1)
    {
        bool valid = tier1_model_test_begin(test);
        if (valid)
        {
            fprintf(test->file, "%s,%u,%u,mov,0,%u,%u,0,0,0,%s,0,", variant == 1 ? "alien" : "plan", variant == 2 ? 0 : 1, record.form, record.reg, variant == 6 ? 99 : record.rm, variant == 3 ? "2147483648" : "0");
            if (variant == 10)
            {
                fputs("-1", test->file);
            }
            else
            {
                fprintf(test->file, "%u", variant == 4 ? (id == 0 ? 1 : 0) : id);
            }
            const uint32_t columns = X86_COST_COLUMN_COUNT - (variant == 7);
            for (uint32_t column = 0; column < columns; column += 1)
            {
                const uint16_t cost = x86_zen5_columns[column][id] ^ (variant == 5 && column == 0);
                fprintf(test->file, ",%u", cost);
            }
            if (variant == 8)
            {
                fputs(",65535", test->file);
            }
            if (variant != 9)
            {
                fputc('\n', test->file);
            }
            fputs("# tier1-manifest-end,version=1,plan=1,expansion=0,wrapper=0\n", test->file);
            bool accepted = false;
            valid = tier1_model_test_read(test, &accepted) && accepted == (variant == 0);
        }
        tier1_model_test_expect(test, valid, names[variant]);
    }
}

static void tier1_model_test_regressions(Tier1ModelTest* test)
{
    static const char* const names[] =
    {
        "complete footer accepted",
        "record-boundary truncation rejected",
        "incorrect footer count rejected",
        "duplicate footer rejected",
        "record after footer rejected",
        "unsupported footer version rejected",
        "unterminated footer rejected",
        "high-byte and low-byte rows parsed",
        "ordinary and forced-displacement LEA rows parsed",
    };
    const X86Instruction move =
    {
        .form = X86_FORM_MOV_R32_RM32,
        .reg = X86_REGISTER_RAX,
        .rm = X86_REGISTER_RDI,
    };
    for (uint32_t variant = 0; variant < BUSTER_ARRAY_LENGTH(names); variant += 1)
    {
        bool valid = tier1_model_test_begin(test);
        if (valid)
        {
            if (variant == 7)
            {
                const X86Instruction high =
                {
                    .form = X86_FORM_MOV_R8_RM8,
                    .reg = X86_REGISTER_AH,
                    .rm = X86_REGISTER_RAX,
                };
                const X86Instruction low =
                {
                    .form = X86_FORM_MOV_R8_RM8,
                    .reg = X86_REGISTER_RAX,
                    .rm = X86_REGISTER_RCX,
                };
                tier1_model_test_row(test->file, 1, high);
                tier1_model_test_row(test->file, 2, low);
            }
            else if (variant == 8)
            {
                X86Instruction lea =
                {
                    .form = X86_FORM_LEA_R64_M,
                    .reg = X86_REGISTER_RAX,
                    .rm = X86_REGISTER_RDI,
                    .index = X86_REGISTER_RCX,
                    .mode = BUSTER_X86_MEMORY_BASE_INDEX(0),
                };
                tier1_model_test_row(test->file, 1, lea);
                lea.rm = X86_REGISTER_RBP;
                tier1_model_test_row(test->file, 2, lea);
            }
            else
            {
                tier1_model_test_row(test->file, 1, move);
            }
            if (variant != 1)
            {
                fprintf(test->file, "# tier1-manifest-end,version=%u,plan=%u,expansion=0,wrapper=0%s", variant == 5 ? 2 : 1, variant == 2 || variant >= 7 ? 2 : 1, variant == 6 ? "" : "\n");
            }
            if (variant == 3)
            {
                fputs("# tier1-manifest-end,version=1,plan=1,expansion=0,wrapper=0\n", test->file);
            }
            if (variant == 4)
            {
                tier1_model_test_row(test->file, 2, move);
            }
            bool accepted = false;
            valid = tier1_model_test_read(test, &accepted) && accepted == (variant == 0 || variant >= 7);
        }
        tier1_model_test_expect(test, valid, names[variant]);
        const Tier1ModelManifest* manifest = test->manifest;
        if (variant == 7)
        {
            const bool separate = valid && manifest->count == 2 && manifest->unknown == 2 && manifest->cases[0].refusal != 0 && manifest->cases[1].refusal != 0 && strcmp(manifest->cases[0].refusal, "high-byte-register") == 0 && strcmp(manifest->cases[1].refusal, "operand-width") == 0 && manifest->cases[0].occurrences == 1 && manifest->cases[1].occurrences == 1;
            tier1_model_test_expect(test, separate, "high-byte alias and unsupported width remain distinct refused cases");
        }
        if (variant == 8)
        {
            const bool separate = valid && manifest->count == 2 && manifest->unknown == 1 && manifest->cases[0].cost_id != 0 && manifest->cases[0].refusal == 0 && manifest->cases[1].cost_id == 0 && manifest->cases[1].refusal != 0 && strcmp(manifest->cases[1].refusal, "forced-zero-displacement") == 0 && manifest->cases[0].occurrences == 1 && manifest->cases[1].occurrences == 1;
            tier1_model_test_expect(test, separate, "unknown forced-displacement LEA cannot merge into a known eligible control");
        }
    }
}

// Test commands pass only their arguments, unlike the measurement entry point's argv[0] command convention.
int run_tier1_model_measure_tests(int argc, char** argv);
int run_tier1_model_measure_tests(int argc, char** argv)
{
    (void)argv;
    Tier1ModelTest test = {0};
    bool valid = argc == 0 && x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize();
    char path[4096];
    int descriptor = -1;
    if (valid)
    {
        test.allowed = malloc(X86_FORM_COUNT);
        test.manifest = malloc(sizeof(*test.manifest));
        const char* directory = getenv("TMPDIR");
        directory = directory != 0 && directory[0] != 0 ? directory : "/var/tmp";
        const int length = snprintf(path, sizeof(path), "%s/buster-tier1-model-XXXXXX", directory);
        valid = test.allowed != 0 && test.manifest != 0 && length > 0 && (uint64_t)length < sizeof(path);
    }
    if (valid)
    {
        descriptor = mkstemp(path);
        valid = descriptor >= 0;
    }
    if (valid)
    {
        test.path = path;
        test.file = fdopen(descriptor, "w+");
        valid = test.file != 0;
    }
    if (valid)
    {
        x86_target_forms(&(X86Target){ .architecture = X86_ARCH_BASELINE }, test.allowed);
        tier1_model_test_parser(&test);
        tier1_model_test_regressions(&test);
    }
    else
    {
        fprintf(stderr, "test-tier1-model: expected no arguments; initialization, allocation or temporary-file setup failed\n");
    }
    if (test.file != 0)
    {
        valid &= fclose(test.file) == 0;
    }
    else if (descriptor >= 0)
    {
        close(descriptor);
    }
    if (descriptor >= 0)
    {
        valid &= unlink(path) == 0;
    }
    free(test.manifest);
    free(test.allowed);
    printf("Tier 1 model manifest: %u checks, %u failures; parser/grouping only, no timing\n", test.checks, test.failures);
    return valid && test.failures == 0 ? 0 : 1;
}
