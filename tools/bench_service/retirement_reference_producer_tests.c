/* Standalone private #1020 issuer fixture. It runs a real held compiler and
 * checks the installed-pin, held-source, live-token and oracle boundaries.
 * The host cc is a mechanical fixture, not production trusted-Clang evidence.
 */
/* glibc declares realpath only for X/Open or default/misc feature sets; plain
 * _POSIX_C_SOURCE hides it under -std=c11. Ubuntu GCC's default fortify
 * wrapper masked the missing declaration while Clang rejected it. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../../src/buster/lib/hash.c"
#include "retirement_correctness.c"
#include "retirement_artifact_service.c"
#include "retirement_correctness_oracle.c"
#define BQ_RETIREMENT_REFERENCE_PRODUCER_LINKED 1
#include "retirement_oracle_authority.c"
#include "retirement_reference_producer.c"
#include "retirement_reference_template.c"

static unsigned assertions, failures;
#define CHECK(expression) do { \
    assertions += 1; \
    if (!(expression)) { \
        failures += 1; \
        fprintf(stderr, "RETIREMENT_REFERENCE_PRODUCER_TEST line=%d: %s\n", \
            __LINE__, #expression); \
    } \
} while (0)

typedef struct ReferenceFixture
{
    char root[128];
    int directory, source_roots[2], output, inventory, clang, cancellation[2];
    BqRetirementReferenceSourceIdentity source[2];
    BqRetirementTrustedRow rows[2];
    BqRetirementPrepared prepared;
    BqRetirementOracleTemplateRow approved;
    BqRetirementOracleTemplate template;
    BqRetirementReferencePlanRow plan_row;
    char flags[3][BQ_RETIREMENT_REFERENCE_FIELD_CAP];
    char build_environment[3][BQ_RETIREMENT_REFERENCE_FIELD_CAP];
    char runtime_environment[2][BQ_RETIREMENT_REFERENCE_FIELD_CAP];
    BqRetirementReferencePlan plan;
    BqRetirementOracleReference references[1];
    BqRetirementReferenceSourceFile source_workspace[1];
    BqRetirementOracleAuthority authority;
    BqRetirementReferenceProducer producer;
    char inventory_sha256[65], template_sha256[65];
} ReferenceFixture;

static void fill_hex(char* value, size_t count, char character)
{
    memset(value, character, count);
    value[count] = 0;
}

static bool create_frozen(int directory, char const* name,
    char const* bytes, size_t length, mode_t mode)
{
    int writer = openat(directory, name, O_WRONLY | O_CREAT | O_EXCL |
        O_CLOEXEC | O_NOFOLLOW, 0600);
    bool ok = writer >= 3 && write(writer, bytes, length) == (ssize_t)length &&
        fsync(writer) == 0 && fchmod(writer, mode) == 0;
    if (writer >= 0 && close(writer) != 0) ok = false;
    return ok;
}

static bool copy_compiler(int folder, char const* path)
{
    int input = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int output = input >= 3 ? openat(folder, "held-compiler",
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    bool ok = input >= 3 && output >= 3;
    char bytes[16384];
    ssize_t count = 0;
    while (ok && (count = read(input, bytes, sizeof(bytes))) > 0)
    {
        size_t done = 0;
        while (ok && done < (size_t)count)
        {
            ssize_t written = write(output, bytes + done, (size_t)count - done);
            ok = written > 0;
            if (ok) done += (size_t)written;
        }
    }
    ok = ok && count == 0 && fsync(output) == 0 && fchmod(output, 0500) == 0;
    if (input >= 0 && close(input) != 0) ok = false;
    if (output >= 0 && close(output) != 0) ok = false;
    return ok;
}

static bool compiler_search_prefix(char const* query, char output[256])
{
    FILE* stream = popen(query, "r");
    char path[256] = {0};
    bool ok = stream && fgets(path, sizeof(path), stream) && path[0] == '/';
    if (stream && pclose(stream) != 0) ok = false;
    char* slash = ok ? strrchr(path, '/') : NULL;
    if (ok) ok = slash != NULL &&
        (size_t)(slash - path) + 4 < BQ_RETIREMENT_REFERENCE_FIELD_CAP;
    if (ok)
    {
        slash[1] = 0;
        ok = snprintf(output, 256, "-B%s", path) > 0;
    }
    return ok;
}

static bool fixture_init(ReferenceFixture* fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->directory = fixture->source_roots[0] =
        fixture->source_roots[1] = fixture->output = fixture->inventory =
        fixture->clang = fixture->cancellation[0] = fixture->cancellation[1] = -1;
    /* Release closes held producer descriptors; a never-begun producer must
     * not name the standard streams. */
    fixture->producer.source_file = fixture->producer.binary_file =
        fixture->producer.receipt_file = -1;
    strcpy(fixture->root, "/tmp/bq-reference-producer-XXXXXX");
    bool ok = mkdtemp(fixture->root) != NULL;
    fixture->directory = ok ? open(fixture->root,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    ok = ok && fixture->directory >= 3 &&
        mkdirat(fixture->directory, "baseline", 0700) == 0 &&
        mkdirat(fixture->directory, "candidate", 0700) == 0 &&
        mkdirat(fixture->directory, "output", 0700) == 0;
    if (ok)
    {
        fixture->source_roots[0] = openat(fixture->directory, "baseline",
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        fixture->source_roots[1] = openat(fixture->directory, "candidate",
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        fixture->output = openat(fixture->directory, "output",
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        static char const source[] =
            "extern int puts(const char*);\n"
            "int main(void) { puts(\"independent\"); return 0; }\n";
        ok = fixture->source_roots[0] >= 3 && fixture->source_roots[1] >= 3 &&
            fixture->output >= 3 && create_frozen(fixture->source_roots[0],
                "reference.c", source, sizeof(source) - 1, 0400);
    }
    int source = ok ? openat(fixture->source_roots[0], "reference.c",
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    char source_sha256[65] = {0};
    if (ok) ok = source >= 3 && bq_retirement_oracle_file_hash(source,
        BQ_REF_SOURCE_CAP, false, source_sha256);
    if (source >= 0 && close(source) != 0) ok = false;
    char compiler[4096] = {0};
    if (ok) ok = realpath("/usr/bin/cc", compiler) != NULL &&
        copy_compiler(fixture->directory, compiler);
    fixture->clang = ok ? openat(fixture->directory, "held-compiler",
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    char clang_sha256[65] = {0};
    if (ok) ok = fixture->clang >= 3 &&
        bq_retirement_oracle_file_hash(fixture->clang,
            BQ_REF_CLANG_CAP, true, clang_sha256) &&
        pipe(fixture->cancellation) == 0 &&
        fcntl(fixture->cancellation[0], F_SETFD, FD_CLOEXEC) == 0 &&
        fcntl(fixture->cancellation[1], F_SETFD, FD_CLOEXEC) == 0;
    for (unsigned side = 0; side < 2; side += 1)
    {
        fill_hex(fixture->source[side].commit, 40, side ? 'b' : 'a');
        fill_hex(fixture->source[side].tree, 40, side ? 'd' : 'c');
        fill_hex(fixture->source[side].manifest_sha256, 64, side ? '2' : '1');
        memcpy(fixture->prepared.source_sha256[side],
            fixture->source[side].manifest_sha256, 65);
        fill_hex(fixture->prepared.binary_sha256[side], 64, side ? '4' : '3');
    }
    fill_hex(fixture->prepared.preparation_sha256, 64, '5');
    fill_hex(fixture->prepared.support_sha256, 64, '6');
    fill_hex(fixture->prepared.census_sha256, 64, '7');
    fixture->prepared.rows = 2;
    fixture->prepared.object_rows = 1;
    fixture->prepared.native_target = 1;
    for (unsigned i = 0; i < 2; i += 1)
    {
        BqRetirementTrustedRow* row = fixture->rows + i;
        row->row = i;
        row->census_row = 0;
        row->target = 1;
        row->stage = i ? BQ_RETIREMENT_STAGE_LINK : BQ_RETIREMENT_STAGE_OBJECT;
        row->classification = 1;
        row->compiler_eligible = 1;
        row->execution_obligation = i;
        fill_hex(row->identity_sha256, 64, i ? '8' : '9');
        memcpy(row->source_sha256, source_sha256, 65);
        fill_hex(row->configuration_sha256, 64, 'a');
    }
    fixture->approved.row = 1;
    fixture->approved.census_row = 0;
    fixture->approved.target = 1;
    memcpy(fixture->approved.source_sha256, source_sha256, 65);
    memcpy(fixture->approved.configuration_sha256,
        fixture->rows[1].configuration_sha256, 65);
    strcpy(fixture->approved.output_name, "oracle-output");
    fixture->template.references = &fixture->approved;
    fixture->template.population_rows = 2;
    fixture->template.object_rows = 1;
    fixture->template.native_target = 1;
    fixture->template.reference_count = 1;
    memcpy(fixture->template.support_sha256,
        fixture->prepared.support_sha256, 65);
    memcpy(fixture->template.census_sha256,
        fixture->prepared.census_sha256, 65);
    for (unsigned side = 0; side < 2; side += 1)
    {
        memcpy(fixture->template.source_commit[side],
            fixture->source[side].commit, 41);
        memcpy(fixture->template.source_tree[side],
            fixture->source[side].tree, 41);
        memcpy(fixture->template.source_sha256[side],
            fixture->source[side].manifest_sha256, 65);
    }
    fill_hex(fixture->template.toolchain_identity_sha256, 64, 'b');
    BqRetirementReferencePlanRow* plan = &fixture->plan_row;
    plan->row = 1;
    plan->source_side = 0;
    strcpy(plan->source_path, "reference.c");
    memcpy(plan->source_sha256, source_sha256, 65);
    plan->flag_count = 3;
    strcpy(fixture->flags[0], "-std=c11");
    if (ok) ok = compiler_search_prefix(
        "/usr/bin/cc -print-prog-name=cc1", fixture->flags[1]) &&
        compiler_search_prefix(
            "/usr/bin/cc -print-libgcc-file-name", fixture->flags[2]);
    for (unsigned i = 0; i < 3; i += 1)
        plan->flags[i] = fixture->flags[i];
    plan->build_environment_count = 3;
    strcpy(fixture->build_environment[0], "HOME=/nonexistent");
    strcpy(fixture->build_environment[1], "LC_ALL=C");
    strcpy(fixture->build_environment[2], "PATH=/usr/bin");
    for (unsigned i = 0; i < 3; i += 1)
        plan->build_environment[i] = fixture->build_environment[i];
    plan->runtime_argument_count = 1;
    plan->runtime_environment_count = 2;
    strcpy(fixture->runtime_environment[0], "HOME=/nonexistent");
    strcpy(fixture->runtime_environment[1], "LC_ALL=C");
    for (unsigned i = 0; i < 2; i += 1)
        plan->runtime_environment[i] = fixture->runtime_environment[i];
    fixture->plan.template = &fixture->template;
    fixture->plan.rows = plan;
    fixture->plan.count = 1;
    memcpy(fixture->plan.clang_sha256, clang_sha256, 65);
    if (ok) ok = bq_ref_plan_row(plan, &fixture->approved,
            fixture->approved.build_command_sha256) &&
        bq_ref_runtime_command(plan, fixture->approved.logical_command_sha256) &&
        bq_retirement_oracle_population_hash(fixture->rows, 2,
            fixture->template.population_sha256) &&
        bq_retirement_oracle_template_hash(&fixture->template,
            fixture->template_sha256);
    int writer = ok ? openat(fixture->directory, "inventory",
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    if (ok) ok = writer >= 3 &&
        bq_retirement_reference_inventory_encode(&fixture->plan,
            fixture->source, fixture->template.toolchain_identity_sha256,
            writer, fixture->inventory_sha256) && fchmod(writer, 0400) == 0;
    if (writer >= 0 && close(writer) != 0) ok = false;
    fixture->inventory = ok ? openat(fixture->directory, "inventory",
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    return ok && fixture->inventory >= 3;
}

static void fixture_release(ReferenceFixture* fixture)
{
    bool ok = bq_retirement_reference_producer_release(&fixture->producer);
    if (fixture->output >= 3)
    {
        char const* names[] = {"reference-00000000", "reference-log-00000000",
            "reference-receipt-00000000", "oracle-output"};
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i += 1)
            if (unlinkat(fixture->output, names[i], 0) != 0 && errno != ENOENT)
                ok = false;
    }
    if (fixture->source_roots[0] >= 3 &&
        unlinkat(fixture->source_roots[0], "reference.c", 0) != 0 &&
        errno != ENOENT) ok = false;
    if (fixture->directory >= 3)
    {
        if (unlinkat(fixture->directory, "inventory", 0) != 0 &&
            errno != ENOENT) ok = false;
        if (unlinkat(fixture->directory, "held-compiler", 0) != 0 &&
            errno != ENOENT) ok = false;
        if (unlinkat(fixture->directory, "baseline", AT_REMOVEDIR) != 0)
            ok = false;
        if (unlinkat(fixture->directory, "candidate", AT_REMOVEDIR) != 0)
            ok = false;
        if (unlinkat(fixture->directory, "output", AT_REMOVEDIR) != 0)
            ok = false;
    }
    if (fixture->inventory >= 0) ok = close(fixture->inventory) == 0 && ok;
    if (fixture->clang >= 0) ok = close(fixture->clang) == 0 && ok;
    for (unsigned side = 0; side < 2; side += 1)
        if (fixture->source_roots[side] >= 0)
            ok = close(fixture->source_roots[side]) == 0 && ok;
    if (fixture->output >= 0) ok = close(fixture->output) == 0 && ok;
    for (unsigned i = 0; i < 2; i += 1)
        if (fixture->cancellation[i] >= 0)
            ok = close(fixture->cancellation[i]) == 0 && ok;
    if (fixture->directory >= 0) ok = close(fixture->directory) == 0 && ok;
    if (fixture->directory >= 3 && rmdir(fixture->root) != 0) ok = false;
    CHECK(ok);
}

static void test_fail_closed(void)
{
    ReferenceFixture changed;
    CHECK(fixture_init(&changed));
    if (changed.inventory >= 3)
    {
        char candidate_inventory[65] = {0};
        changed.plan_row.source_side = 1;
        CHECK(!bq_retirement_reference_inventory_encode(&changed.plan,
            changed.source, changed.template.toolchain_identity_sha256,
            -1, candidate_inventory));
        changed.plan_row.source_side = 0;
        CHECK(bq_retirement_oracle_authority_begin(&changed.authority,
            &changed.template, changed.template_sha256, &changed.prepared,
            changed.rows, changed.references, 1, 13, 17));
        BqRetirementReferenceSourceIdentity wrong[2] = {
            changed.source[0], changed.source[1]};
        wrong[0].commit[0] ^= 1;
        CHECK(!bq_retirement_reference_producer_begin(&changed.producer,
            &changed.plan, changed.inventory_sha256,
            changed.template_sha256, changed.inventory, wrong,
            changed.source_roots, changed.template.toolchain_identity_sha256,
            changed.clang, changed.output, changed.source_workspace, 1,
            &changed.authority));
        changed.producer = (BqRetirementReferenceProducer){0};
        CHECK(bq_retirement_reference_producer_begin(&changed.producer,
            &changed.plan, changed.inventory_sha256,
            changed.template_sha256, changed.inventory, changed.source,
            changed.source_roots, changed.template.toolchain_identity_sha256,
            changed.clang, changed.output, changed.source_workspace, 1,
            &changed.authority));
        char saved = changed.flags[0][0];
        changed.flags[0][0] = 'x';
        uint64_t now = 0;
        BqRetirementOracleVerifiedBuild const* token = NULL;
        CHECK(bq_retirement_oracle_clock(&now));
        CHECK(!bq_retirement_reference_producer_next(&changed.producer,
            changed.cancellation[0], now + UINT64_C(30000000000), &token));
        struct stat artifact = {0};
        CHECK(!token && changed.authority.ledger.failed &&
            fstatat(changed.output, "reference-log-00000000", &artifact,
                AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
        changed.flags[0][0] = saved;
    }
    fixture_release(&changed);

    ReferenceFixture replaced;
    CHECK(fixture_init(&replaced));
    if (replaced.inventory >= 3)
    {
        CHECK(bq_retirement_oracle_authority_begin(&replaced.authority,
            &replaced.template, replaced.template_sha256, &replaced.prepared,
            replaced.rows, replaced.references, 1, 13, 19) &&
            bq_retirement_reference_producer_begin(&replaced.producer,
                &replaced.plan, replaced.inventory_sha256,
                replaced.template_sha256, replaced.inventory, replaced.source,
                replaced.source_roots, replaced.template.toolchain_identity_sha256,
                replaced.clang, replaced.output, replaced.source_workspace, 1,
                &replaced.authority));
        static char const bytes[] = "extern int puts(const char*);\n"
            "int main(void) { puts(\"independent\"); return 0; }\n";
        CHECK(create_frozen(replaced.source_roots[0], "reference-new.c", bytes,
                sizeof(bytes) - 1, 0400) &&
            renameat(replaced.source_roots[0], "reference-new.c",
                replaced.source_roots[0], "reference.c") == 0);
        uint64_t now = 0;
        BqRetirementOracleVerifiedBuild const* token = NULL;
        CHECK(bq_retirement_oracle_clock(&now) &&
            !bq_retirement_reference_producer_next(&replaced.producer,
                replaced.cancellation[0], now + UINT64_C(30000000000),
                &token) && !token && replaced.authority.ledger.failed);
    }
    fixture_release(&replaced);
}

static void put_u32(uint8_t* bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; i += 1) bytes[i] = (uint8_t)(value >> (i * 8));
}

/* Canonical inventory bytes through the producer's own writer, which only
 * accepts a fresh single-link file; read the exact bytes back. */
static bool inventory_bytes(int directory, BqRetirementReferencePlan const* plan,
    BqRetirementReferenceSourceIdentity const* source, char const toolchain[65],
    uint8_t* bytes, uint64_t capacity, uint64_t* length)
{
    char digest[65] = {0};
    struct stat info = {0};
    int writer = openat(directory, "codec-inventory",
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    bool ok = writer >= 3 && bq_retirement_reference_inventory_encode(plan, source,
        toolchain, writer, digest) && fstat(writer, &info) == 0 &&
        info.st_size > 0 && (uint64_t)info.st_size <= capacity;
    if (writer >= 0 && close(writer) != 0) ok = false;
    int reader = ok ? openat(directory, "codec-inventory",
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && reader >= 3 && pread(reader, bytes, (size_t)info.st_size, 0) == info.st_size;
    if (reader >= 0 && close(reader) != 0) ok = false;
    if (writer >= 0 && unlinkat(directory, "codec-inventory", 0) != 0) ok = false;
    *length = ok ? (uint64_t)info.st_size : 0;
    return ok;
}

static bool template_accepts(uint8_t const* bytes, uint64_t length, uint32_t slots,
    char digest[65])
{
    BqRetirementOracleTemplateRow rows[4];
    BqRetirementOracleTemplate decoded;
    bool ok = bq_retirement_reference_template_decode(bytes, length, rows, slots,
        &decoded, digest);
    return ok;
}

static bool inventory_accepts(uint8_t const* bytes, uint64_t length,
    BqRetirementOracleTemplate const* template, uint64_t text_capacity, char digest[65])
{
    BqRetirementReferencePlanRow rows[4];
    char text[4096];
    BqRetirementReferencePlan plan;
    BqRetirementReferenceSourceIdentity source[2];
    char toolchain[65];
    bool ok = text_capacity <= sizeof(text) &&
        bq_retirement_reference_inventory_decode(bytes, length, template, rows, 4,
            text, text_capacity, &plan, source, toolchain, digest);
    return ok;
}

/* Offsets follow the documented streams: template header 592 bytes (row 0
 * starts there); inventory count at 517 and row 0 at 521. */
static void test_codec(void)
{
    ReferenceFixture fixture;
    CHECK(fixture_init(&fixture));
    if (fixture.inventory >= 3)
    {
        uint8_t template[4096], mutated[4097];
        uint64_t template_length = 0;
        char digest[65] = {0};
        CHECK(bq_retirement_reference_template_write(&fixture.template, template,
            sizeof(template), &template_length) && template_length == 592u + 285u);
        BqRetirementOracleTemplateRow rows[4];
        BqRetirementOracleTemplate decoded;
        CHECK(bq_retirement_reference_template_decode(template, template_length, rows, 4,
                &decoded, digest) && !strcmp(digest, fixture.template_sha256) &&
            decoded.references == rows && decoded.reference_count == 1 &&
            decoded.population_rows == 2 && rows[0].row == 1 &&
            !strcmp(rows[0].output_name, "oracle-output") &&
            !strcmp(rows[0].build_command_sha256, fixture.approved.build_command_sha256) &&
            !strcmp(decoded.source_tree[1], fixture.template.source_tree[1]));
        CHECK(!template_accepts(template, template_length, 0, digest));
        CHECK(!template_accepts(template, template_length - 1, 4, digest));
        memcpy(mutated, template, template_length);
        mutated[template_length] = 0;
        CHECK(!template_accepts(mutated, template_length + 1, 4, digest));
        mutated[0] ^= 1;
        CHECK(!template_accepts(mutated, template_length, 4, digest));
        memcpy(mutated, template, template_length);
        put_u32(mutated + 588, UINT32_MAX);
        CHECK(!template_accepts(mutated, template_length, 4, digest));
        put_u32(mutated + 588, 0);
        CHECK(!template_accepts(mutated, template_length, 4, digest));
        memcpy(mutated, template, template_length);
        mutated[32] = 'G';
        CHECK(!template_accepts(mutated, template_length, 4, digest));
        memcpy(mutated, template, template_length);
        put_u32(mutated + 592, 2);
        CHECK(!template_accepts(mutated, template_length, 4, digest));
        memcpy(mutated, template, template_length);
        put_u32(mutated + 600, 2);
        CHECK(!template_accepts(mutated, template_length, 4, digest));
        memcpy(mutated, template, template_length);
        mutated[592 + 12 + 256 + 4] = '/';
        CHECK(!template_accepts(mutated, template_length, 4, digest));
        /* A flipped digest byte can decode, but only as a different pin. */
        memcpy(mutated, template, template_length);
        mutated[448] = mutated[448] == '0' ? '1' : '0';
        CHECK(!template_accepts(mutated, template_length, 4, digest) ||
            strcmp(digest, fixture.template_sha256));

        BqRetirementOracleTemplateRow pair[2] = {fixture.approved, fixture.approved};
        BqRetirementReferencePlanRow pair_rows[2] = {fixture.plan_row, fixture.plan_row};
        BqRetirementOracleTemplate two = fixture.template;
        two.population_rows = 3;
        two.reference_count = 2;
        two.references = pair;
        pair[1].row = pair_rows[1].row = 2;
        CHECK(bq_ref_plan_row(&pair_rows[1], &pair[1], pair[1].build_command_sha256));
        uint8_t pair_template[4096];
        uint64_t pair_length = 0;
        CHECK(bq_retirement_reference_template_write(&two, pair_template,
                sizeof(pair_template), &pair_length) &&
            template_accepts(pair_template, pair_length, 4, digest));
        put_u32(pair_template + 592, 2);
        put_u32(pair_template + 592 + 285, 1);
        CHECK(!template_accepts(pair_template, pair_length, 4, digest));

        uint8_t inventory[4096];
        struct stat info = {0};
        CHECK(fstat(fixture.inventory, &info) == 0 && info.st_size > 521 &&
            (uint64_t)info.st_size < sizeof(inventory) &&
            pread(fixture.inventory, inventory, (size_t)info.st_size, 0) == info.st_size);
        uint64_t inventory_length = (uint64_t)info.st_size;
        BqRetirementReferencePlanRow plan_rows[2];
        char text[4096], toolchain[65] = {0};
        BqRetirementReferencePlan plan;
        BqRetirementReferenceSourceIdentity source[2];
        CHECK(bq_retirement_reference_inventory_decode(inventory, inventory_length,
                &decoded, plan_rows, 2, text, sizeof(text), &plan, source, toolchain,
                digest) && !strcmp(digest, fixture.inventory_sha256) &&
            plan.template == &decoded && plan.rows == plan_rows && plan.count == 1 &&
            !strcmp(plan.clang_sha256, fixture.plan.clang_sha256) &&
            plan_rows[0].row == 1 && plan_rows[0].source_side == 0 &&
            !strcmp(plan_rows[0].source_path, "reference.c") &&
            plan_rows[0].flag_count == 3 && !strcmp(plan_rows[0].flags[1], fixture.flags[1]) &&
            plan_rows[0].build_environment_count == 3 &&
            !strcmp(plan_rows[0].build_environment[2], "PATH=/usr/bin") &&
            plan_rows[0].runtime_argument_count == 1 &&
            plan_rows[0].runtime_environment_count == 2 &&
            !strcmp(source[1].tree, fixture.source[1].tree) &&
            !strcmp(toolchain, fixture.template.toolchain_identity_sha256));
        CHECK(!inventory_accepts(inventory, inventory_length - 1, &decoded, 4096, digest));
        CHECK(!inventory_accepts(inventory, inventory_length, &decoded, 4, digest));
        memcpy(mutated, inventory, inventory_length);
        mutated[inventory_length] = 0;
        CHECK(!inventory_accepts(mutated, inventory_length + 1, &decoded, 4096, digest));
        mutated[0] ^= 1;
        CHECK(!inventory_accepts(mutated, inventory_length, &decoded, 4096, digest));
        memcpy(mutated, inventory, inventory_length);
        mutated[40] = mutated[40] == '0' ? '1' : '0';
        CHECK(!inventory_accepts(mutated, inventory_length, &decoded, 4096, digest));
        uint32_t const counts[] = {0, 2, UINT32_MAX};
        for (unsigned i = 0; i < 3; i += 1)
        {
            memcpy(mutated, inventory, inventory_length);
            put_u32(mutated + 517, counts[i]);
            CHECK(!inventory_accepts(mutated, inventory_length, &decoded, 4096, digest));
        }
        uint32_t const flag_counts[] = {BQ_RETIREMENT_REFERENCE_FLAGS_CAP + 1u, UINT32_MAX};
        for (unsigned i = 0; i < 2; i += 1)
        {
            memcpy(mutated, inventory, inventory_length);
            put_u32(mutated + 608, flag_counts[i]);
            CHECK(!inventory_accepts(mutated, inventory_length, &decoded, 4096, digest));
        }
        for (uint32_t side = 1; side < 3; side += 1)
        {
            memcpy(mutated, inventory, inventory_length);
            put_u32(mutated + 525, side);
            CHECK(!inventory_accepts(mutated, inventory_length, &decoded, 4096, digest));
        }
        memcpy(mutated, inventory, inventory_length);
        put_u32(mutated + 521, 0);
        CHECK(!inventory_accepts(mutated, inventory_length, &decoded, 4096, digest));
        memcpy(mutated, inventory, inventory_length);
        memcpy(mutated + 533, "../", 3);
        CHECK(!inventory_accepts(mutated, inventory_length, &decoded, 4096, digest));
        memcpy(mutated, inventory, inventory_length);
        mutated[533] = '/';
        CHECK(!inventory_accepts(mutated, inventory_length, &decoded, 4096, digest));
        memcpy(mutated, inventory, inventory_length);
        mutated[460] = mutated[460] == '0' ? '1' : '0';
        CHECK(!inventory_accepts(mutated, inventory_length, &decoded, 4096, digest) ||
            strcmp(digest, fixture.inventory_sha256));
        /* The embedded template hash binds the inventory to one template. */
        BqRetirementOracleTemplateRow renamed = fixture.approved;
        BqRetirementOracleTemplate other = fixture.template;
        strcpy(renamed.output_name, "other-output");
        other.references = &renamed;
        CHECK(!inventory_accepts(inventory, inventory_length, &other, 4096, digest));

        BqRetirementReferencePlan pair_plan = fixture.plan;
        pair_plan.template = &two;
        pair_plan.rows = pair_rows;
        pair_plan.count = 2;
        uint8_t pair_inventory[4096];
        uint64_t pair_inventory_length = 0;
        CHECK(inventory_bytes(fixture.directory, &pair_plan, fixture.source,
                two.toolchain_identity_sha256, pair_inventory, sizeof(pair_inventory),
                &pair_inventory_length) &&
            inventory_accepts(pair_inventory, pair_inventory_length, &two, 4096, digest));
        uint64_t row_bytes = 4u + 4u + 4u + strlen(fixture.plan_row.source_path) + 64u + 4u * 4u;
        for (unsigned i = 0; i < fixture.plan_row.flag_count; i += 1)
            row_bytes += 4u + strlen(fixture.plan_row.flags[i]);
        for (unsigned i = 0; i < fixture.plan_row.build_environment_count; i += 1)
            row_bytes += 4u + strlen(fixture.plan_row.build_environment[i]);
        for (unsigned i = 0; i < fixture.plan_row.runtime_environment_count; i += 1)
            row_bytes += 4u + strlen(fixture.plan_row.runtime_environment[i]);
        CHECK(521u + 2u * row_bytes == pair_inventory_length);
        put_u32(pair_inventory + 521, 2);
        put_u32(pair_inventory + 521 + row_bytes, 1);
        CHECK(!inventory_accepts(pair_inventory, pair_inventory_length, &two, 4096, digest));
    }
    fixture_release(&fixture);
}

int main(void)
{
    test_fail_closed();
    test_codec();
    ReferenceFixture fixture;
    CHECK(fixture_init(&fixture));
    if (fixture.inventory >= 3)
    {
        char bad_pin[65] = {0};
        memcpy(bad_pin, fixture.inventory_sha256, 65);
        bad_pin[0] ^= 1;
        CHECK(bq_retirement_oracle_authority_begin(&fixture.authority,
            &fixture.template, fixture.template_sha256, &fixture.prepared,
            fixture.rows, fixture.references, 1, 7, 11));
        CHECK(!bq_retirement_reference_producer_begin(&fixture.producer,
            &fixture.plan, bad_pin, fixture.template_sha256, fixture.inventory,
            fixture.source, fixture.source_roots,
            fixture.template.toolchain_identity_sha256, fixture.clang,
            fixture.output, fixture.source_workspace, 1, &fixture.authority));
        fixture.producer = (BqRetirementReferenceProducer){0};
        CHECK(bq_retirement_reference_producer_begin(&fixture.producer,
            &fixture.plan, fixture.inventory_sha256, fixture.template_sha256,
            fixture.inventory, fixture.source, fixture.source_roots,
            fixture.template.toolchain_identity_sha256, fixture.clang,
            fixture.output, fixture.source_workspace, 1, &fixture.authority));
        char saved = fixture.flags[0][0];
        fixture.flags[0][0] = 'x';
        CHECK(!bq_retirement_reference_producer_ready(&fixture.producer));
        fixture.flags[0][0] = saved;
        uint64_t now = 0;
        CHECK(bq_retirement_oracle_clock(&now));
        BqRetirementOracleVerifiedBuild const* token = NULL;
        CHECK(bq_retirement_reference_producer_next(&fixture.producer,
            fixture.cancellation[0], now + UINT64_C(30000000000), &token));
        if (token)
        {
            BqRetirementOracleVerifiedBuild counterfeit = *token;
            CHECK(!bq_retirement_reference_producer_token_valid(&counterfeit,
                &fixture.authority) &&
                bq_retirement_reference_producer_token_valid(token,
                    &fixture.authority));
            char original_binary = fixture.producer.token.binary_sha256[0];
            fixture.producer.token.binary_sha256[0] ^= 1;
            CHECK(!bq_retirement_reference_producer_token_valid(token,
                &fixture.authority));
            fixture.producer.token.binary_sha256[0] = original_binary;
            char executable[64] = {0}, cwd[64] = {0};
            CHECK(snprintf(executable, sizeof(executable),
                    "/proc/self/fd/%d", token->binary) > 0 &&
                snprintf(cwd, sizeof(cwd), "/proc/self/fd/%d",
                    fixture.output) > 0);
            char* argv[] = {executable, NULL};
            char* env[] = {fixture.runtime_environment[0],
                fixture.runtime_environment[1], NULL};
            BqRetirementProcessCommand command = {argv, env, cwd, 1, 2};
            BqRetirementArtifactLocation output = {fixture.output, "oracle-output"};
            CHECK(bq_retirement_oracle_authority_next(&fixture.authority, token,
                &command, output, fixture.cancellation[0],
                now + UINT64_C(30000000000)) &&
                !bq_retirement_reference_producer_token_valid(token,
                    &fixture.authority) &&
                bq_retirement_oracle_authority_finish(&fixture.authority) &&
                bq_retirement_reference_producer_ready(&fixture.producer));
            fixture.rows[0].identity_sha256[0] ^= 1;
            CHECK(!bq_retirement_reference_producer_ready(&fixture.producer));
            fixture.rows[0].identity_sha256[0] ^= 1;
            CHECK(bq_retirement_reference_producer_ready(&fixture.producer));
            CHECK(fchmodat(fixture.source_roots[0], "reference.c", 0600, 0) == 0);
            int source = openat(fixture.source_roots[0], "reference.c",
                O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
            CHECK(source >= 3 && pwrite(source, "X", 1, 0) == 1 &&
                fchmod(source, 0400) == 0);
            if (source >= 0) CHECK(close(source) == 0);
            CHECK(!bq_retirement_reference_producer_ready(&fixture.producer));
        }
    }
    fixture_release(&fixture);
    fprintf(stdout, "RETIREMENT_REFERENCE_PRODUCER_TEST assertions=%u failures=%u\n",
        assertions, failures);
    return failures ? 1 : 0;
}
