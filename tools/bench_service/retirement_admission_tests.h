/* Retirement queue seam (#881 P1): the portable admission predicate, the
 * fixed-recipe submission, the recipe-identity operation, journal
 * compatibility, the profile cap and the coordinator's recipe-specific gates.
 * Included by tests.c after its fixtures.
 *
 * bq_test_retirement_admit switches queue.c's test-only profile seam
 * (bq_retirement_profile_test_override) between the compiled blocked profile
 * and an admitted stand-in: the compiled bytes with their status line
 * replaced by status=admitted. The stand-in carries no worker-unit pins, so
 * bq_retirement_profile_complete still refuses it.
 *
 * Covered: bq_recipe_profile_admitted's exact status rule; the predicates
 * closed under the compiled profile and open under the stand-in; the gateway's
 * submit-retirement and the client's submit refusing while blocked and
 * encoding only the five canonical fields when admitted, with override
 * attempts refused (bq_test_retirement_submission); recipe-identity digests
 * equal to the file hashes, capabilities v2 byte-identical, and the reply
 * validator (bq_test_retirement_identity); a retirement journal replayed under
 * the admitted stand-in through FINALIZING, cancel and failure cleanup, then
 * reopened under the blocked build with its jobs inert, and a schema-2
 * retirement SUBMIT refused (bq_test_retirement_journal); an admitted profile
 * of the full pin shape read by bq_installed_recipe (bq_test_retirement_profile_cap);
 * an admitted but incomplete compiled profile refused at `serve`, at submission
 * and before reservation, a complete one served through the exclusive submit
 * only (bq_test_retirement_servable); (#881) no A/A policy pin among the
 * worker-unit pins, an admitted profile of the full pin shape complete and
 * servable without one, and the blocked profile unchanged
 * (bq_test_retirement_aa_no_policy_pin);
 * the coordinator's gates refusing an incomplete seam profile while the
 * queue's predicate admits (bq_test_retirement_coordinator_gates).
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_ADMISSION_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_ADMISSION_TESTS_H

#define BQ_TEST_RETIREMENT_BASE "1111111111111111111111111111111111111111"
#define BQ_TEST_RETIREMENT_CANDIDATE "2222222222222222222222222222222222222222"

BUSTER_GLOBAL_LOCAL char bq_test_retirement_admitted_profile[BQ_RECIPE_PROFILE_CAP + 1];

/* The compiled profile with its one status line replaced by status=admitted. */
BUSTER_GLOBAL_LOCAL void bq_test_retirement_admit(bool admitted)
{
    char const* compiled = bq_native_retirement_blocked_profile;
    char const* status = strstr(compiled, "\nstatus=blocked\n");
    int length = status ? snprintf(bq_test_retirement_admitted_profile, sizeof(bq_test_retirement_admitted_profile),
                                   "%.*s\nstatus=admitted\n%s", (int)(status - compiled), compiled,
                                   status + strlen("\nstatus=blocked\n")) : -1;
    bool built = length > 0 && (u32)length < sizeof(bq_test_retirement_admitted_profile);
    BQ_CHECK(built);
    bq_retirement_profile_test_override = admitted && built ?
        string_from_pointer(bq_test_retirement_admitted_profile) : (String8){0};
}

BUSTER_GLOBAL_LOCAL bool bq_test_retirement_request(char const* principal, char const* key, BqRequest* request)
{
    String8 fields[BQ_FIELD_COUNT] = {string_from_pointer(principal), string_from_pointer(key),
                                      S8("native-retirement-performance-v1"), S8(BQ_TEST_RETIREMENT_BASE),
                                      S8(BQ_TEST_RETIREMENT_CANDIDATE)};
    bool made = bq_request_make(fields, request) == BQ_OK;
    return made;
}

BUSTER_GLOBAL_LOCAL void bq_test_retirement_predicate(void)
{
    char const* refused[] = {"", "status=admitted", "status=blocked\n", "status=admitted \n", "status=admitted\r\n",
                             "status=admitted\nstatus=admitted\n", "status=blocked\nstatus=admitted\n",
                             "status=admitted-v2\n", "status=\n", "recipe=x\n"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused); index += 1)
        BQ_CHECK(!bq_recipe_profile_admitted(string_from_pointer(refused[index])));
    BQ_CHECK(!bq_recipe_profile_admitted((String8){0}) &&
             bq_recipe_profile_admitted(S8("status=admitted\n")) &&
             bq_recipe_profile_admitted(S8("schema=1\nstatus=admitted\nrequires=x\n")));
    /* An unterminated final line is refused, whatever it says. */
    BQ_CHECK(!bq_recipe_profile_admitted(S8("schema=1\nstatus=admitted\ntrailing-unterminated")) &&
             !bq_recipe_profile_admitted(S8("status=admitted\nstatus=blocked")));
    /* The compiled profile keeps everything closed. */
    bq_test_retirement_admit(false);
    BqRequest request = {0};
    BQ_CHECK(!bq_recipe_retirement_admitted() && bq_recipe_blocked(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) &&
             !bq_recipe_admitted(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) &&
             !bq_recipe_service(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) &&
             !bq_test_retirement_request("github-actions", "closed", &request));
    /* The admitted stand-in opens exactly this recipe's predicates. */
    bq_test_retirement_admit(true);
    BQ_CHECK(bq_recipe_retirement_admitted() && !bq_recipe_blocked(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) &&
             bq_recipe_admitted(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) &&
             bq_recipe_service(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) &&
             bq_test_retirement_request("github-actions", "open", &request) && bq_recipe_real(&request) &&
             bq_recipe_real_journal(&request) && !bq_recipe_fake(&request) &&
             bq_recipe_service(BQ_RECIPE_VALIDATE_BUSTER) && !bq_recipe_service(BQ_RECIPE_FAKE_SUCCESS));
    bq_test_retirement_admit(false);
    BQ_CHECK(!bq_recipe_real(&request) && bq_recipe_real_journal(&request) && !bq_request_valid(&request) &&
             bq_request_valid_admitting(&request, true));
}

/* Failure first: every override attempt is refused before any packet exists;
 * only then does the admitted stand-in open the fixed recipe. */
BUSTER_GLOBAL_LOCAL void bq_test_retirement_submission(void)
{
    char* gateway[] = {"submit-retirement", "run-881-1", BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE};
    char* client[] = {"submit", "github-actions", "run-881-1", "native-retirement-performance-v1",
                      BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE};
    BqPacket fixed = {0}, typed = {0};
    u32 operation = 0;
    bq_test_retirement_admit(false);
    BQ_CHECK(!bq_client_arguments(4, gateway, true, &fixed, &operation) && !fixed.size);
    BQ_CHECK(!bq_client_arguments(6, client, false, &typed, &operation) && !typed.size);
    bq_test_retirement_admit(true);
    BQ_CHECK(bq_client_arguments(4, gateway, true, &fixed, &operation) && operation == BQ_OP_SUBMIT);
    BQ_CHECK(bq_client_arguments(6, client, false, &typed, &operation) && operation == BQ_OP_SUBMIT &&
             fixed.size == typed.size && !memcmp(fixed.bytes, typed.bytes, fixed.size));
    /* The body is exactly the five canonical fields: nothing else rides along. */
    BqRequest canonical = {0};
    BQ_CHECK(bq_test_retirement_request("github-actions", "run-881-1", &canonical) &&
             fixed.size == BQ_CONTROL_HEADER + canonical.size &&
             !memcmp(fixed.bytes + BQ_CONTROL_HEADER, canonical.bytes, canonical.size));
    /* Extra arguments (flags, samples, thresholds, workloads, a recipe or an
     * environment assignment), a key that is itself an assignment, abbreviated,
     * symbolic, uppercase or mismatched source identities, and the retirement
     * form outside the gateway are all refused. */
    char* extra[][5] = {
        {"submit-retirement", "run-881-1", BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE, "--pairs=254"},
        {"submit-retirement", "run-881-1", BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE, "samples=1"},
        {"submit-retirement", "run-881-1", BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE, "threshold=0"},
        {"submit-retirement", "run-881-1", BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE, "workload=smoke"},
        {"submit-retirement", "run-881-1", BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE,
         "validate-buster-v1"},
        {"submit-retirement", "run-881-1", BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE, "CC=/bin/sh"},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(extra); index += 1)
    {
        typed = (BqPacket){0};
        BQ_CHECK(!bq_client_arguments(5, extra[index], true, &typed, &operation) && !typed.size);
    }
    char* short_form[] = {"submit-retirement", BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE};
    BQ_CHECK(!bq_client_arguments(3, short_form, true, &typed, &operation) && !typed.size);
    char const* keys[] = {"samples=1", "--pairs 254", "run;id", ""};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(keys); index += 1)
    {
        char* attempt[] = {"submit-retirement", (char*)keys[index], BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE};
        typed = (BqPacket){0};
        BQ_CHECK(!bq_client_arguments(4, attempt, true, &typed, &operation) && !typed.size);
    }
    char const* identities[] = {"main", "HEAD", "1111111", "--help", "$(id)", "../installed",
                                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                                "3333333333333333333333333333333333333333333333333333333333333333"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(identities); index += 1)
    {
        char* attempt[] = {"submit-retirement", "run-881-1", (char*)identities[index], BQ_TEST_RETIREMENT_CANDIDATE};
        typed = (BqPacket){0};
        BQ_CHECK(!bq_client_arguments(4, attempt, true, &typed, &operation) && !typed.size);
    }
    char* outside[] = {"submit-retirement", "github-actions", "run-881-1", BQ_TEST_RETIREMENT_BASE,
                       BQ_TEST_RETIREMENT_CANDIDATE};
    BQ_CHECK(!bq_client_arguments(5, outside, false, &typed, &operation));
    BQ_CHECK(!bq_client_arguments(4, gateway, false, &typed, &operation));
    /* The smoke form still fixes its own recipe. */
    char* smoke[] = {"submit", "run-881-1", BQ_TEST_RETIREMENT_BASE, BQ_TEST_RETIREMENT_CANDIDATE};
    BQ_CHECK(bq_client_arguments(4, smoke, true, &typed, &operation) && typed.size > BQ_CONTROL_HEADER);
    BqRequest smoke_request = {.size = typed.size > BQ_CONTROL_HEADER ? typed.size - BQ_CONTROL_HEADER : 0};
    memcpy(smoke_request.bytes, typed.bytes + BQ_CONTROL_HEADER, smoke_request.size);
    BQ_CHECK(bq_request_recipe(&smoke_request) == BQ_RECIPE_VALIDATE_BUSTER);
    bq_test_retirement_admit(false);
}

/* The digest value after `key` in the reply text. */
BUSTER_GLOBAL_LOCAL bool bq_test_retirement_identity_value(char const* text, char const* key,
                                                           char value[SHA256_HEX_CAPACITY])
{
    char const* found = strstr(text, key);
    bool ok = found && (found == text || found[-1] == '\n');
    if (ok)
    {
        memcpy(value, found + strlen(key), SHA256_HEX_CAPACITY - 1);
        value[SHA256_HEX_CAPACITY - 1] = 0;
        ok = found[strlen(key) + SHA256_HEX_CAPACITY - 1] == '\n';
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_test_retirement_identity(void)
{
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqPacket request = {0}, response = {0};
    u32 operation = 0;
    char* identity[] = {"recipe-identity"};
    bq_test_retirement_admit(false);
    BQ_CHECK(bq_client_arguments(1, identity, true, &request, &operation) && operation == BQ_OP_RECIPE_IDENTITY &&
             request.size == BQ_CONTROL_HEADER);
    BQ_CHECK(bq_dispatch(&queue, request.bytes, request.size, &response) == BQ_OK &&
             bq_public_response_valid(&request, &response));
    char text[BQ_CONTROL_BODY + 1] = {0};
    u32 length = response.size >= BQ_CONTROL_HEADER + 4 ? response.size - BQ_CONTROL_HEADER - 4 : 0;
    memcpy(text, response.bytes + BQ_CONTROL_HEADER + 4, length);
    char profile[SHA256_HEX_CAPACITY], contract[SHA256_HEX_CAPACITY], support[SHA256_HEX_CAPACITY];
    char profile_file[SHA256_HEX_CAPACITY], contract_file[SHA256_HEX_CAPACITY], support_file[SHA256_HEX_CAPACITY];
    BQ_CHECK(!strncmp(text, "schema=1 recipe=native-retirement-performance-v1 status=blocked\n",
                      strlen("schema=1 recipe=native-retirement-performance-v1 status=blocked\n")) &&
             bq_test_retirement_identity_value(text, "profile-sha256=", profile) &&
             bq_test_retirement_identity_value(text, "contract-sha256=", contract) &&
             bq_test_retirement_identity_value(text, "support-declaration-sha256=", support));
    BQ_CHECK(bq_test_file_sha256("tools/bench_service/profiles/native-retirement-performance-v1.blocked", profile_file) &&
             bq_test_file_sha256("docs/native-retirement-performance-contract.md", contract_file) &&
             bq_test_file_sha256("docs/native-retirement-support-v1.tsv", support_file) &&
             !strcmp(profile, profile_file) && !strcmp(contract, contract_file) && !strcmp(support, support_file));
    /* A reply of any other shape, or to a request with a body, is refused. */
    for (u32 cut = BQ_CONTROL_HEADER + 4; cut < response.size; cut += 1)
    {
        BqPacket truncated = {0};
        bq_packet(&truncated, BQ_OP_RECIPE_IDENTITY | 0x80000000u, bq_u64(request.bytes + 16),
                  response.bytes + BQ_CONTROL_HEADER, cut - BQ_CONTROL_HEADER);
        BQ_CHECK(!bq_public_response_valid(&request, &truncated));
    }
    u32 mutated[] = {4, 9, 60, 70, 100};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(mutated); index += 1)
    {
        BqPacket changed = response;
        changed.bytes[BQ_CONTROL_HEADER + 4 + mutated[index]] = 'Z';
        BQ_CHECK(!bq_public_response_valid(&request, &changed));
    }
    /* One extra byte, or a second status line, after a valid reply. */
    char const* const appended[] = {"x", "status=admitted\n"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(appended); index += 1)
    {
        u8 longer[BQ_CONTROL_BODY] = {0};
        u32 extra = (u32)strlen(appended[index]);
        u32 body_size = response.size - BQ_CONTROL_HEADER;
        memcpy(longer, response.bytes + BQ_CONTROL_HEADER, body_size);
        memcpy(longer + body_size, appended[index], extra);
        BqPacket grown = {0};
        bq_packet(&grown, BQ_OP_RECIPE_IDENTITY | 0x80000000u, bq_u64(request.bytes + 16), longer, body_size + extra);
        BQ_CHECK(grown.size == response.size + extra && !bq_public_response_valid(&request, &grown));
    }
    BqPacket with_body = {0};
    u8 body[4] = {0};
    bq_packet(&with_body, BQ_OP_RECIPE_IDENTITY, 1, body, sizeof(body));
    BQ_CHECK(bq_dispatch(&queue, with_body.bytes, with_body.size, &response) != BQ_OK);
    bq_packet_schema(&with_body, 1, BQ_OP_RECIPE_IDENTITY, 1, NULL, 0);
    BQ_CHECK(bq_dispatch(&queue, with_body.bytes, with_body.size, &response) != BQ_OK);
    /* The admitted stand-in reports its own digest and status. */
    bq_test_retirement_admit(true);
    char stand_in[SHA256_HEX_CAPACITY];
    bq_digest(bq_test_retirement_admitted_profile, (u32)strlen(bq_test_retirement_admitted_profile), stand_in);
    BQ_CHECK(bq_dispatch(&queue, request.bytes, request.size, &response) == BQ_OK &&
             bq_public_response_valid(&request, &response));
    memset(text, 0, sizeof(text));
    length = response.size >= BQ_CONTROL_HEADER + 4 ? response.size - BQ_CONTROL_HEADER - 4 : 0;
    memcpy(text, response.bytes + BQ_CONTROL_HEADER + 4, length);
#ifdef __linux__
    /* The stand-in has no worker-unit pins, so the service reports it
     * incomplete rather than admitted. */
    BQ_CHECK(strstr(text, " status=incomplete\n") != NULL);
#else
    BQ_CHECK(strstr(text, " status=admitted\n") != NULL);
#endif
    BQ_CHECK(bq_test_retirement_identity_value(text, "profile-sha256=", profile) &&
             !strcmp(profile, stand_in) && bq_test_retirement_identity_value(text, "contract-sha256=", contract) &&
             !strcmp(contract, contract_file));
    bq_test_retirement_admit(false);
    /* Capabilities v2 stays byte for byte what the dispatch workflow greps. */
#ifdef __linux__
    static char const expected[] =
        "schema=2 journal=3 legacy-journal=1 executor=supervisor pending=8 jobs=512\n"
        "local-recipes=fake-success-v1,fake-failure-v1 service-recipes=validate-buster-v1,zen5-calibration-v1 "
        "blocked-recipes=native-retirement-performance-v1\n"
        "validity=not-evaluated materialization=read-only workspace=per-attempt\n"
        "worker=fixed-systemd-service dispatch=fixed-registry admission=idle-only-atomic "
        "export=1 transport=unix-seqpacket authentication=peer-uid-gid\n"
        "storage=private-local-posix-directory\n";
    BQ_CHECK(sizeof(expected) == sizeof(bq_capabilities_v2) && !memcmp(expected, bq_capabilities_v2, sizeof(expected)));
    BQ_CHECK(bq_transport_public_operation(BQ_OP_RECIPE_IDENTITY) == BQ_OK);
    bq_packet(&with_body, BQ_OP_RECIPE_IDENTITY, 1, body, sizeof(body));
    BQ_CHECK(bq_transport_public_request(with_body.bytes, with_body.size) == BQ_BAD_REQUEST &&
             bq_transport_public_request(request.bytes, request.size) == BQ_OK);
#endif
}

#ifndef _WIN32
BUSTER_GLOBAL_LOCAL BqError bq_test_retirement_advance(BqQueue* queue, u64 id, BqPhase phase, BqOutcome outcome)
{
    BqJob* job = bq_job(&queue->state, id);
    BqError error = job ? bq_real_advance(queue, job, phase, outcome) : BQ_NOT_FOUND;
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_test_retirement_journal(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        BqQueue* queue = &fixture.queue;
        BqRequest first = {0}, second = {0}, third = {0}, fourth = {0};
        u64 id = 0, token = 0;
        /* Under the blocked build a retirement submission never reaches the
         * journal, through either admission entry. */
        bq_test_retirement_admit(true);
        BQ_CHECK(bq_test_retirement_request("github-actions", "retire-1", &first) &&
                 bq_test_retirement_request("github-actions", "retire-2", &second) &&
                 bq_test_retirement_request("github-actions", "retire-3", &third) &&
                 bq_test_retirement_request("github-actions", "retire-4", &fourth));
        bq_test_retirement_admit(false);
        BQ_CHECK(bq_submit(queue, &first, &id) == BQ_BAD_REQUEST && bq_submit_exclusive(queue, &first, &id) == BQ_BAD_REQUEST &&
                 queue->state.job_count == 0 && queue->bytes == 0);
        /* Admitted: FINALIZING's worker success, cancel during FINALIZING and
         * the cancelled cleanup, then a failure cleanup, then a queued job. */
        bq_test_retirement_admit(true);
        BQ_CHECK(bq_submit(queue, &first, &id) == BQ_OK && id == 1 && bq_reserve(queue, &id, &token) == BQ_OK &&
                 bq_test_retirement_advance(queue, 1, BQ_PREPARING, BQ_NO_OUTCOME) == BQ_OK &&
                 bq_test_retirement_advance(queue, 1, BQ_SETTLING, BQ_NO_OUTCOME) == BQ_OK &&
                 bq_test_retirement_advance(queue, 1, BQ_MEASURING, BQ_NO_OUTCOME) == BQ_OK &&
                 bq_test_retirement_advance(queue, 1, BQ_FINALIZING, BQ_SUCCEEDED) == BQ_OK &&
                 bq_cancel(queue, 1) == BQ_OK && bq_job(&queue->state, 1)->cancel_requested &&
                 bq_test_retirement_advance(queue, 1, BQ_CLEANING, BQ_CANCELLED) == BQ_OK &&
                 bq_test_retirement_advance(queue, 1, BQ_FINISHED, BQ_CANCELLED) == BQ_OK);
        BQ_CHECK(bq_submit(queue, &second, &id) == BQ_OK && id == 10 && bq_reserve(queue, &id, &token) == BQ_OK &&
                 bq_test_retirement_advance(queue, 10, BQ_PREPARING, BQ_NO_OUTCOME) == BQ_OK &&
                 bq_test_retirement_advance(queue, 10, BQ_CLEANING, BQ_FAILED) == BQ_OK &&
                 bq_test_retirement_advance(queue, 10, BQ_FINISHED, BQ_FAILED) == BQ_OK);
        BQ_CHECK(bq_submit(queue, &third, &id) == BQ_OK && id == 15 && !queue->state.active_id);
        u64 admitted_bytes = queue->bytes;
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && queue->bytes == admitted_bytes &&
                 queue->state.job_count == 3 && !queue->needs_reconciliation);
        BqJob* job = bq_job(&queue->state, 1);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_CANCELLED && job->cancel_requested);
        job = bq_job(&queue->state, 10);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED);
        job = bq_job(&queue->state, 15);
        BQ_CHECK(job && job->phase == BQ_QUEUED && bq_recipe_real(&job->request));
        /* Blocked build: the same journal replays byte for byte, and its
         * retirement jobs are inert. */
        bq_test_retirement_admit(false);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && queue->bytes == admitted_bytes &&
                 queue->state.job_count == 3 && !queue->needs_reconciliation);
        job = bq_job(&queue->state, 1);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_CANCELLED);
        job = bq_job(&queue->state, 15);
        BQ_CHECK(job && job->phase == BQ_QUEUED && !bq_recipe_real(&job->request) &&
                 bq_recipe_real_journal(&job->request));
        u64 token_out = 0;
        BQ_CHECK(bq_submit(queue, &third, &id) == BQ_BAD_REQUEST && bq_submit(queue, &fourth, &id) == BQ_BAD_REQUEST &&
                 bq_materialize(queue, S8("/nonexistent-installed"), S8("/nonexistent-workspaces"), &id, &token_out) ==
                 BQ_UNSUPPORTED && !queue->state.active_id && queue->bytes == admitted_bytes);
#ifdef __linux__
        BQ_CHECK(!bq_transport_queue_admissible(queue) && !bq_transport_has_real_job(queue));
#endif
        u8 status_body[8];
        bq_put64(status_body, 1);
        BqPacket status, response;
        bq_packet_schema(&status, 1, BQ_OP_STATUS, 71, status_body, sizeof(status_body));
        BQ_CHECK(bq_dispatch(queue, status.bytes, status.size, &response) == BQ_UNSUPPORTED);
        /* The operator can still cancel the inert queued job. */
        BQ_CHECK(bq_cancel(queue, 15) == BQ_OK && bq_job(&queue->state, 15)->outcome == BQ_CANCELLED);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && bq_job(&queue->state, 15)->phase == BQ_FINISHED);
        /* An active retirement attempt reopened by the blocked build waits for
         * reconciliation that this build refuses to perform. */
        bq_test_retirement_admit(true);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && bq_submit(queue, &fourth, &id) == BQ_OK &&
                 bq_reserve(queue, &id, &token) == BQ_OK &&
                 bq_test_retirement_advance(queue, id, BQ_PREPARING, BQ_NO_OUTCOME) == BQ_OK);
        u64 active = id;
        bq_test_retirement_admit(false);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && queue->needs_reconciliation &&
                 queue->state.active_id == active &&
                 bq_workspace_reconcile(queue, S8("/nonexistent-workspaces"), active, token) == BQ_UNSUPPORTED &&
                 bq_reserve(queue, &id, &token_out) == BQ_RECONCILIATION_REQUIRED);
        bq_test_retirement_admit(true);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && queue->needs_reconciliation &&
                 queue->state.active_id == active);
        /* Retirement records exist only in schema 3: a schema-2 SUBMIT is
         * corrupt even under the admitted stand-in. */
        u8 image[BQ_RECORD_CAP];
        bq_frame_schema(image, BQ_SCHEMA_MATERIALIZATION, BQ_SUBMIT, 1, first.bytes, first.size);
        bq_test_image(&fixture, image, BQ_HEADER_SIZE + first.size);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_CORRUPT);
        bq_frame_schema(image, BQ_SCHEMA, BQ_SUBMIT, 1, first.bytes, first.size);
        bq_test_image(&fixture, image, BQ_HEADER_SIZE + first.size);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && queue->state.job_count == 1);
        bq_test_retirement_admit(false);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && queue->state.job_count == 1);
        bq_test_end(&fixture);
    }
    bq_test_retirement_admit(false);
}
#endif

#ifdef __linux__
/* A private directory under $TMPDIR (absolute) or /tmp. */
BUSTER_GLOBAL_LOCAL bool bq_test_retirement_private_directory(char path[BQ_PATH_CAP + 1], char const* name)
{
    char const* base = getenv("TMPDIR");
    base = base && base[0] == '/' ? base : "/tmp";
    int length = snprintf(path, BQ_PATH_CAP + 1, "%s/%s-XXXXXX", base, name);
    bool made = length > 0 && (u32)length <= BQ_PATH_CAP && mkdtemp(path) != NULL;
    if (!made) path[0] = 0;
    return made;
}

/* An admitted profile of the full pin shape: the compiled descriptive lines,
 * one line per worker-unit pin the compiled profile lacks, lane D's four
 * campaign values in range and status=admitted; about 2.7 KB. */
BUSTER_GLOBAL_LOCAL bool bq_test_retirement_full_profile(char* profile, u32 capacity, u32* used)
{
    char const* compiled = bq_native_retirement_blocked_profile;
    char const* status = strstr(compiled, "\nstatus=blocked\n");
    *used = status ? (u32)(status - compiled) + 1u : 0;
    bool ok = status && *used < capacity;
    if (ok) memcpy(profile, compiled, *used);
    char const* after = status ? status + strlen("\nstatus=blocked\n") : "";
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(bq_retirement_worker_unit_pins); index += 1)
    {
        char const* key = bq_retirement_worker_unit_pins[index];
        if (!strstr(compiled, key))
        {
            int line = snprintf(profile + *used, capacity - *used, "%s%064u\n", key, index);
            ok = line > 0 && (u32)line < capacity - *used && (u32)line <= BQ_RECIPE_PROFILE_LINE_CAP;
            *used += ok ? (u32)line : 0;
        }
    }
    int tail = ok ? snprintf(profile + *used, capacity - *used,
                             "%s" BQ_RETIREMENT_UNIT_CAMPAIGN_SEED_KEY "881\n" BQ_RETIREMENT_UNIT_CAMPAIGN_PAIRS_KEY
                             "254\n" BQ_RETIREMENT_UNIT_CAMPAIGN_RESAMPLES_KEY "100000\n"
                             BQ_RETIREMENT_UNIT_CAMPAIGN_BOOTSTRAP_KEY "64\n" BQ_RECIPE_PROFILE_ADMITTED_STATUS "\n",
                             after) : -1;
    ok = ok && tail > 0 && (u32)tail < capacity - *used;
    *used += ok ? (u32)tail : 0;
    return ok;
}

/* An admitted profile of the full pin shape fits the queue's cap and passes
 * bq_installed_recipe; one byte over the cap is refused. */
BUSTER_GLOBAL_LOCAL void bq_test_retirement_profile_cap(void)
{
    static char profile[BQ_RECIPE_PROFILE_CAP + 2];
    u32 used = 0;
    bool ok = bq_test_retirement_full_profile(profile, sizeof(profile), &used);
    /* About 2.7 KB, past the former 1024-byte cap and within the new one. */
    BQ_CHECK(ok && used > 2048 && used <= BQ_RECIPE_PROFILE_CAP);
    char root[BQ_PATH_CAP + 1], recipes[BQ_PATH_CAP + 16], path[BQ_PATH_CAP + 128];
    BqRecipeFiles files = {0};
    bool made = ok && bq_test_retirement_private_directory(root, "bq-retirement-profile") &&
                bq_recipe_files(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, &files);
    snprintf(recipes, sizeof(recipes), "%s/recipes", root);
    snprintf(path, sizeof(path), "%s/%s", recipes, files.profile);
    BQ_CHECK(made && mkdir(recipes, 0700) == 0);
    for (u32 pass = 0; made && pass < 2; pass += 1)
    {
        /* Pass 1 pads the same profile with one more terminated line, to one
         * byte past the cap. */
        u32 size = used;
        if (pass == 1)
        {
            memset(profile + used, '#', BQ_RECIPE_PROFILE_CAP - used);
            profile[BQ_RECIPE_PROFILE_CAP] = '\n';
            size = BQ_RECIPE_PROFILE_CAP + 1;
        }
        profile[size] = 0;
        bq_retirement_profile_test_override = (String8){(char8*)profile, size};
        BQ_CHECK(chmod(recipes, 0700) == 0 && (unlink(path) == 0 || errno == ENOENT) &&
                 bq_test_write_path(path, profile, 0400) && chmod(recipes, 0500) == 0);
        int installed = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        BQ_CHECK(installed >= 0 && bq_installed_recipe(installed, BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) == (pass == 0) &&
                 bq_recipe_retirement_admitted());
        if (installed >= 0) close(installed);
    }
    bq_retirement_profile_test_override = (String8){0};
    BQ_CHECK(!made || (chmod(recipes, 0700) == 0 && unlink(path) == 0 && rmdir(recipes) == 0 && rmdir(root) == 0));
}

/* #881 P1 fail-closed: the compiled profile is servable (blocked, or admitted
 * with every pin); an admitted but incomplete profile is refused at `serve`
 * and at submission, and the worker leaves such a job queued, unreserved and
 * without the lease; a complete one is served, through the exclusive submit
 * only. */
BUSTER_GLOBAL_LOCAL void bq_test_retirement_servable(void)
{
    bq_test_retirement_admit(false);
    BQ_CHECK(!bq_recipe_retirement_admitted() ||
             bq_retirement_profile_complete(bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED)));
    BQ_CHECK(bq_retirement_compiled_servable());
    BqRequest request = {0};
    BqPacket exclusive = {0}, plain = {0};
    bq_test_retirement_admit(true);
    BQ_CHECK(bq_test_retirement_request(BQ_EXPORT_PRINCIPAL, "servable", &request));
    bq_packet(&exclusive, BQ_OP_SUBMIT_EXCLUSIVE, 1, request.bytes, request.size);
    bq_packet(&plain, BQ_OP_SUBMIT, 2, request.bytes, request.size);
    /* Admitted but incomplete: refused before transport and at `serve`. */
    BQ_CHECK(bq_recipe_retirement_admitted() && !bq_retirement_compiled_servable() &&
             bq_transport_public_request(exclusive.bytes, exclusive.size) == BQ_RECIPE_MISMATCH &&
             bq_transport_public_request(plain.bytes, plain.size) == BQ_RECIPE_MISMATCH &&
             bq_transport_serve("/nonexistent-bench-state", "/nonexistent-bench.sock", NULL) == BQ_RECIPE_MISMATCH);
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, true))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest local = {0};
        u64 id = 0;
        BQ_CHECK(bq_test_retirement_request("test-principal", "incomplete", &local) &&
                 bq_submit(queue, &local, &id) == BQ_OK);
        u64 run = id;
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &run) == BQ_BAD_REQUEST);
        BqJob* job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_QUEUED && !job->token && !queue->state.active_id &&
                 !queue->needs_reconciliation && fixture.quarantine.descriptor < 0 && fixture.fake.starts == 0 &&
                 !bq_test_worker_probe_locked(fixture.lease));
        BQ_CHECK(bq_cancel(queue, id) == BQ_OK);
        bq_test_worker_end(&fixture);
    }
    /* Admitted with every pin: served, and only through the exclusive submit. */
    static char complete[BQ_RECIPE_PROFILE_CAP + 1];
    u32 used = 0;
    BQ_CHECK(bq_test_retirement_full_profile(complete, sizeof(complete), &used));
    bq_retirement_profile_test_override = (String8){(char8*)complete, used};
    BQ_CHECK(bq_retirement_profile_complete(bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED)) &&
             bq_retirement_compiled_servable() &&
             bq_transport_public_request(exclusive.bytes, exclusive.size) == BQ_OK &&
             bq_transport_public_request(plain.bytes, plain.size) == BQ_UNSUPPORTED);
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqPacket identity = {0}, response = {0};
    bq_packet(&identity, BQ_OP_RECIPE_IDENTITY, 3, NULL, 0);
    BQ_CHECK(bq_dispatch(&queue, identity.bytes, identity.size, &response) == BQ_OK &&
             bq_public_response_valid(&identity, &response) &&
             !memcmp(response.bytes + BQ_CONTROL_HEADER + 4,
                     "schema=1 recipe=native-retirement-performance-v1 status=admitted\n",
                     strlen("schema=1 recipe=native-retirement-performance-v1 status=admitted\n")));
    bq_test_retirement_admit(false);
}

/* #881 N1: with the queue's predicate admitting, every coordinator gate still
 * refuses a seam profile that bq_retirement_profile_complete refuses. */
BUSTER_GLOBAL_LOCAL void bq_test_retirement_coordinator_gates(void)
{
    bq_test_retirement_admit(true);
    BqJob job = {.id = 1, .token = 2};
    BQ_CHECK(bq_test_retirement_request("github-actions", "gates", &job.request) && bq_request_valid(&job.request) &&
             bq_recipe_real(&job.request));
    BqRetirementWorkerUnitSeams stand_in = bq_retirement_worker_unit_installed();
    BqRetirementWorkerUnitSeams blocked = stand_in;
    blocked.profile = string_from_pointer(bq_native_retirement_blocked_profile);
    BqRetirementWorkerUnitSeams const* const refused[] = {NULL, &stand_in, &blocked};
    BQ_CHECK(!bq_retirement_profile_complete(stand_in.profile) &&
             !bq_retirement_request_valid_pinned(&job.request, stand_in.profile) &&
             !bq_retirement_request_valid_pinned(&job.request, blocked.profile) &&
             bq_worker_recipe_admitted(BQ_RECIPE_VALIDATE_BUSTER, NULL));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused); index += 1)
    {
        BqWorkerFinalization gate = {.result_directory = -1, .retirement = refused[index]};
        BqWorkerFinalization launch = {.result_directory = -1, .retirement = refused[index]};
        BQ_CHECK(bq_recipe_files(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, &launch.recipe));
        BQ_CHECK(!bq_worker_recipe_admitted(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, refused[index]) &&
                 !bq_worker_finalization_recipe(&job, &gate) && !bq_worker_recipe_launchable(&launch));
    }
    /* The unit refuses retirement before the lease handoff (BQ_BAD_REQUEST)
     * where the smoke recipe reaches it and fails there. */
    BQ_CHECK(bq_worker_unit(S8("/unsupported"), S8("1"), S8("2"), S8("native-retirement-performance-v1"),
                            S8("/workspace"), S8(BQ_TEST_RETIREMENT_BASE), S8(BQ_TEST_RETIREMENT_CANDIDATE),
                            S8("/workspace/result")) == BQ_BAD_REQUEST);
    BQ_CHECK(bq_worker_unit(S8("/unsupported"), S8("1"), S8("2"), S8("validate-buster-v1"), S8("/workspace"),
                            S8(BQ_TEST_RETIREMENT_BASE), S8(BQ_TEST_RETIREMENT_CANDIDATE),
                            S8("/workspace/result")) == BQ_CONFIGURATION_MISMATCH);
    bq_test_retirement_admit(false);
}
#endif

#ifdef __linux__
/* (#881) The A/A gate is the in-job A/A alone, so no #426 policy pin is a
 * worker-unit pin: an admitted profile of the full pin shape, which carries
 * no aa-policy-sha256= line, is complete and servable, and dropping any one
 * remaining worker-unit pin still makes it incomplete and not servable. The
 * installed blocked profile is unchanged: still status=blocked, still no
 * policy pin, incomplete and servable. */
BUSTER_GLOBAL_LOCAL void bq_test_retirement_aa_no_policy_pin(void)
{
    static char complete[BQ_RECIPE_PROFILE_CAP + 1], changed[BQ_RECIPE_PROFILE_CAP + 1];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(bq_retirement_worker_unit_pins); index += 1)
        BQ_CHECK(!strstr(bq_retirement_worker_unit_pins[index], "aa-policy"));
    u32 used = 0;
    bool built = bq_test_retirement_full_profile(complete, sizeof(complete), &used);
    complete[built ? used : 0] = 0;
    BQ_CHECK(built && !strstr(complete, "aa-policy-sha256="));
    bq_retirement_profile_test_override = (String8){(char8*)complete, used};
    BQ_CHECK(bq_recipe_retirement_admitted() &&
             bq_retirement_profile_complete(bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED)) &&
             bq_retirement_compiled_servable());
    /* Without the last worker-unit pin's line: incomplete. */
    char key[BQ_RECIPE_PROFILE_LINE_CAP];
    int key_length = snprintf(key, sizeof(key), "\n%s",
                              bq_retirement_worker_unit_pins[BUSTER_ARRAY_LENGTH(bq_retirement_worker_unit_pins) - 1]);
    char const* line = built && key_length > 0 ? strstr(complete, key) : NULL;
    char const* end = line ? strchr(line + 1, '\n') : NULL;
    u32 prefix = line && end ? (u32)(line - complete) + 1u : 0;
    u32 kept = line && end ? (u32)(end - complete) + 1u : 0;
    int length = line && end ? snprintf(changed, sizeof(changed), "%.*s%s", (int)prefix, complete, complete + kept) :
                 -1;
    bq_retirement_profile_test_override = length > 0 ? (String8){(char8*)changed, (u64)length} : (String8){0};
    BQ_CHECK(length > 0 && bq_recipe_retirement_admitted() &&
             !bq_retirement_profile_complete(bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED)) &&
             !bq_retirement_compiled_servable());
    bq_retirement_profile_test_override = (String8){0};
    /* The installed blocked profile, unchanged. */
    String8 blocked = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    BQ_CHECK(blocked.length && !bq_recipe_retirement_admitted() &&
             strstr(bq_native_retirement_blocked_profile, "\nstatus=blocked\n") &&
             !strstr(bq_native_retirement_blocked_profile, "aa-policy-sha256=") &&
             !bq_retirement_profile_complete(blocked) && bq_retirement_compiled_servable());
}
#endif

BUSTER_GLOBAL_LOCAL void bq_test_retirement_admission(void)
{
    bq_test_retirement_predicate();
    bq_test_retirement_submission();
    bq_test_retirement_identity();
#ifndef _WIN32
    bq_test_retirement_journal();
#endif
#ifdef __linux__
    bq_test_retirement_profile_cap();
    bq_test_retirement_servable();
    bq_test_retirement_aa_no_policy_pin();
    bq_test_retirement_coordinator_gates();
#endif
    bq_test_retirement_admit(false);
}
#endif
