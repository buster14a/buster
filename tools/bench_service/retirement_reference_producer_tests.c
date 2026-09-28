/* Standalone private #1020 issuer fixture. It runs a real held compiler and
 * checks the installed-pin, held-source, live-token and oracle boundaries.
 * The host cc is a mechanical fixture, not production trusted-Clang evidence.
 */
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

int main(void)
{
    test_fail_closed();
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
