/* Native execution admission/publication regression tests. Disposable programs
 * are fixed, known bytes; no host manager/account configuration is changed. */
#ifdef __linux__
BUSTER_GLOBAL_LOCAL void bq_test_native_program(u8 bytes[256])
{
    memset(bytes, 0, 256);
    Elf64_Ehdr header = {0};
    memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_type = ET_EXEC;
    header.e_machine = EM_X86_64;
    header.e_version = EV_CURRENT;
    header.e_entry = 0x400080;
    header.e_phoff = sizeof(header);
    header.e_ehsize = sizeof(header);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = 1;
    Elf64_Phdr segment = {.p_type = PT_LOAD, .p_flags = PF_R | PF_X, .p_vaddr = 0x400000,
        .p_paddr = 0x400000, .p_filesz = 256, .p_memsz = 256, .p_align = 4096};
    memcpy(bytes, &header, sizeof(header));
    memcpy(bytes + sizeof(header), &segment, sizeof(segment));
    u8 const code[] = {0xb8,1,0,0,0,0xbf,1,0,0,0,0x48,0x8d,0x35,47,0,0,0,
        0xba,15,0,0,0,0x0f,0x05,0xb8,60,0,0,0,0x31,0xff,0x0f,0x05};
    memcpy(bytes + 128, code, sizeof(code));
    memcpy(bytes + 192, "native-fixture\n", 15);
}

BUSTER_GLOBAL_LOCAL BqError bq_test_native_frame(BqQueue* queue, u32 operation, u8 const* body, u32 length,
                                                 BqPacket* response)
{
    BqPacket request;
    bq_packet(&request, operation, 71, body, length);
    BqError error = bq_transport_dispatch(queue, request.bytes, request.size, response);
    BQ_CHECK(error != BQ_OK || bq_public_response_valid(&request, response));
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_test_native_upload(BqQueue* queue, u8 const bytes[256], char identity[65])
{
    u8 body[512] = {0};
    char hash[65];
    bq_native_hash(bytes, 256, hash);
    memcpy(body, hash, 64);
    bq_put64(body + 64, 256);
    BqPacket response;
    bool ok = bq_test_native_frame(queue, BQ_OP_NATIVE_BEGIN, body, 72, &response) == BQ_OK;
    bq_put64(body + 72, 0);
    memcpy(body + 80, bytes, 256);
    ok = ok && bq_test_native_frame(queue, BQ_OP_NATIVE_WRITE, body, 336, &response) == BQ_OK &&
         bq_test_native_frame(queue, BQ_OP_NATIVE_FINISH, body, 72, &response) == BQ_OK;
    if (ok) { memcpy(identity, response.bytes + BQ_CONTROL_HEADER + 12, 64); identity[64] = 0; }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_test_native_profile_install(BqMaterialFixture* fixture)
{
    char recipes[512], path[512];
    snprintf(recipes, sizeof(recipes), "%s/recipes", fixture->installed);
    snprintf(path, sizeof(path), "%s/recipes/" BQ_NATIVE_RECIPE ".recipe", fixture->installed);
    BQ_CHECK(chmod(fixture->installed, 0700) == 0 && chmod(recipes, 0700) == 0 &&
        bq_test_write_path(path, bq_native_profile, 0400) && chmod(recipes, 0500) == 0 &&
        chmod(fixture->installed, 0500) == 0);
}

BUSTER_GLOBAL_LOCAL BqRequest bq_test_native_request(char const* identity)
{
    BqRequest request;
    String8 fields[] = {S8("github-actions"), S8("native-fixture"), S8(BQ_NATIVE_RECIPE),
        string_from_pointer(identity), string_from_pointer(identity)};
    BQ_CHECK(bq_request_make(fields, &request) == BQ_OK);
    return request;
}

BUSTER_GLOBAL_LOCAL void bq_test_native_upload_recovery(void)
{
    for (u32 crash = 0; crash <= 5; crash += 1)
    {
        BqFixture fixture;
        if (bq_test_begin(&fixture))
        {
            u8 bytes[256], body[512] = {0};
            bq_test_native_program(bytes);
            char hash[65];
            bq_native_hash(bytes, 256, hash);
            memcpy(body, hash, 64);
            bq_put64(body + 64, 256);
            BqPacket response;
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_BEGIN, body, 72, &response) == BQ_OK);
            memcpy(body + 80, bytes, 128);
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_WRITE, body, 208, &response) == BQ_OK);
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_WRITE, body, 208, &response) == BQ_OK);
            body[80] ^= 1;
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_WRITE, body, 208, &response) == BQ_CONFLICT);
            body[80] ^= 1;
            bq_close(&fixture.queue);
            BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_OK);
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_BEGIN, body, 72, &response) == BQ_OK &&
                     bq_u64(response.bytes + BQ_CONTROL_HEADER + 4) == 128);
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_FINISH, body, 72, &response) == BQ_SOURCE_MISMATCH);
            bq_put64(body + 72, 128);
            memcpy(body + 80, bytes + 128, 128);
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_WRITE, body, 208, &response) == BQ_OK);
            bq_native_test_finish_crash = crash;
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_FINISH, body, 72, &response) == (crash ? BQ_IO : BQ_OK));
            bq_native_test_finish_crash = 0;
            bq_close(&fixture.queue);
            BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_OK);
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_FINISH, body, 72, &response) == BQ_OK);
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_BEGIN, body, 72, &response) == BQ_OK &&
                     bq_u64(response.bytes + BQ_CONTROL_HEADER + 4) == 256);
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_FINISH, body, 71, &response) == BQ_BAD_REQUEST);
            bq_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_native_staging_refusal(void)
{
    for (u32 defect = 0; defect < 3; defect += 1)
    {
        BqFixture fixture;
        if (bq_test_begin(&fixture))
        {
            u8 bytes[256], body[512] = {0}; bq_test_native_program(bytes);
            char hash[65], manifest[BQ_NATIVE_MANIFEST_CAP], identity[65];
            bq_native_hash(bytes, 256, hash); memcpy(body, hash, 64); bq_put64(body + 64, 256);
            memcpy(body + 80, bytes, 256);
            BqPacket response;
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_BEGIN, body, 72, &response) == BQ_OK &&
                     bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_WRITE, body, 336, &response) == BQ_OK);
            BQ_CHECK(bq_native_manifest(manifest, body, 256, identity) > 0);
            bq_native_test_finish_crash = 2;
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_FINISH, body, 72, &response) == BQ_IO);
            bq_native_test_finish_crash = 0;
            int store = bq_native_store(&fixture.queue);
            char pending[80]; snprintf(pending, sizeof(pending), ".%s", identity);
            int directory = openat(store, pending, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            BQ_CHECK(directory >= 0);
            if (defect == 0)
            {
                BQ_CHECK(fchmodat(directory, "program", 0600, 0) == 0);
                int file = openat(directory, "program", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
                u8 wrong = 0;
                BQ_CHECK(file >= 0 && pwrite(file, &wrong, 1, 0) == 1 && fsync(file) == 0);
                if (file >= 0) close(file);
            }
            else if (defect == 1) BQ_CHECK(linkat(directory, "program", store, "retained-link", 0) == 0);
            else
            {
                int file = openat(directory, "extra", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
                BQ_CHECK(file >= 0);
                if (file >= 0) close(file);
            }
            struct stat before = {0}, after = {0};
            BQ_CHECK(fstat(directory, &before) == 0 &&
                bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_FINISH, body, 72, &response) == BQ_RECONCILIATION_REQUIRED &&
                fstatat(store, pending, &after, AT_SYMLINK_NOFOLLOW) == 0 &&
                before.st_dev == after.st_dev && before.st_ino == after.st_ino);
            if (directory >= 0) close(directory);
            if (store >= 0) close(store);
            bq_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_native_rejections(void)
{
    for (u32 defect = 0; defect < 7; defect += 1)
    {
        BqFixture fixture;
        if (bq_test_begin(&fixture))
        {
            u8 bytes[256], body[512] = {0};
            bq_test_native_program(bytes);
            Elf64_Ehdr header;
            Elf64_Phdr segment;
            memcpy(&header, bytes, sizeof(header));
            memcpy(&segment, bytes + sizeof(header), sizeof(segment));
            if (defect == 0) header.e_machine = EM_AARCH64;
            if (defect == 1) header.e_type = ET_DYN;
            if (defect == 2) segment.p_type = PT_INTERP;
            if (defect == 3) segment.p_type = PT_DYNAMIC;
            if (defect == 4) segment.p_flags |= PF_W;
            if (defect == 5) segment.p_filesz = 257;
            if (defect == 6) header.e_entry += 512;
            memcpy(bytes, &header, sizeof(header));
            memcpy(bytes + sizeof(header), &segment, sizeof(segment));
            char hash[65];
            bq_native_hash(bytes, 256, hash);
            memcpy(body, hash, 64); bq_put64(body + 64, 256); memcpy(body + 80, bytes, 256);
            BqPacket response;
            BQ_CHECK(bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_BEGIN, body, 72, &response) == BQ_OK &&
                     bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_WRITE, body, 336, &response) == BQ_OK &&
                     bq_test_native_frame(&fixture.queue, BQ_OP_NATIVE_FINISH, body, 72, &response) == BQ_SOURCE_MISMATCH);
            bq_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_native_materialization_execution(void)
{
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 0))
    {
        bq_test_native_profile_install(&fixture);
        u8 bytes[256];
        bq_test_native_program(bytes);
        char identity[65] = {0};
        BQ_CHECK(bq_test_native_upload(&fixture.queue.queue, bytes, identity));
        BqRequest request = bq_test_native_request(identity);
        BqState old = {0};
        BQ_CHECK(bq_apply(&old, BQ_SCHEMA_WORKER, BQ_SUBMIT, 1, request.bytes, request.size) == BQ_BAD_REQUEST);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK &&
            bq_materialize(&fixture.queue.queue, string_from_pointer(fixture.installed),
                string_from_pointer(fixture.workspaces), &id, &token) == BQ_OK);
        char source[512];
        snprintf(source, sizeof(source), "%s/job-%" PRIu64 "-attempt-%" PRIu64 "/candidate/source",
            fixture.workspaces, (uint64_t)id, (uint64_t)token);
        BQ_CHECK(bq_native_payload(source, identity) == BQ_CONFIGURATION_MISMATCH);
#if defined(__x86_64__)
        int stream[2];
        BQ_CHECK(pipe(stream) == 0);
        pid_t child = fork();
        BQ_CHECK(child >= 0);
        if (child == 0)
        {
            close(stream[0]);
            bool prepared = dup2(stream[1], STDOUT_FILENO) >= 0 &&
                            prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL) == 0;
            close(stream[1]);
            if (prepared) bq_native_execute(source, identity, geteuid());
            _exit(126);
        }
        close(stream[1]);
        char transcript[64] = {0};
        ssize_t count = read(stream[0], transcript, sizeof(transcript));
        close(stream[0]);
        int status = 0;
        BQ_CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
                 count == 15 && !memcmp(transcript, "native-fixture\n", 15));
#endif
        BqJob* job = bq_job(&fixture.queue.queue.state, id);
        BqWorkerConfig config = {.workspace_root = string_from_pointer(fixture.workspaces), .production_path = true};
        BqWorkerFinalization finalization = {.config = &config, .result_directory = -1};
        BQ_CHECK(bq_worker_result_open(&config, job, &finalization, true) == BQ_OK);
        char bundle[65], recursive[65];
        char const log[] = "native-fixture\n";
        BQ_CHECK(bq_worker_result_control_publish(&finalization, "native-stage.log", log, sizeof(log) - 1, 0400) == BQ_OK &&
                 bq_worker_failure_bundle_publish(&finalization, bundle, recursive) == BQ_OK);
        char manifest[2048];
        int length = snprintf(manifest, sizeof(manifest),
            "schema=1\nrecipe=" BQ_NATIVE_RECIPE "\nstatus=succeeded\nstage=" BQ_NATIVE_STAGE_NAME "\nprocess-result=success\n"
            "job-id=%" PRIu64 "\nattempt-token=%" PRIu64 "\nworkspace-root=%s\nresult-root=%s\nbase-revision=%s\ncandidate-revision=%s\n"
            "program-manifest-sha256=%s\noperation=execute-once\nstage-exit-status=0\ncompilation=unavailable\nbenchmark-metrics=unavailable\nbundle-sha256=%s\n",
            (uint64_t)id, (uint64_t)token, fixture.workspaces, finalization.result_root, identity, identity, identity, bundle);
        BQ_CHECK(length > 0 && (u32)length < sizeof(manifest) &&
                 bq_worker_result_control_publish(&finalization, finalization.recipe.manifest, manifest, (u64)length, 0400) == BQ_OK &&
                 bq_worker_result_validate(&config, job, &finalization) == BQ_OK &&
                 bq_worker_finish(&fixture.queue.queue, &config, job, BQ_SUCCEEDED, BQ_OK, &finalization) == BQ_OK);
        BQ_CHECK(job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED && job->result_bound &&
                 bq_worker_result_binding_validate(job) == BQ_OK && bq_export_prepare(&fixture.queue.queue, job) == BQ_OK);
        if (finalization.masked) BQ_CHECK(sigprocmask(SIG_SETMASK, &finalization.prior_mask, NULL) == 0);
        if (finalization.result_directory >= 0) close(finalization.result_directory);
        bq_close(&fixture.queue.queue);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK);
        job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(job && job->result_bound && job->phase == BQ_FINISHED && bq_worker_result_binding_validate(job) == BQ_OK);
        bq_material_test_end(&fixture);
    }
}
/* Fake manager observations exercise the real reservation/lease/cleanup
 * state machine. Execution and sealed export are covered separately above. */
BUSTER_GLOBAL_LOCAL void bq_test_native_worker_outcomes(void)
{
    BqWorkerResult cases[] = {BQ_WORKER_SUCCEEDED, BQ_WORKER_EXECUTION_FAILED, BQ_WORKER_TIMED_OUT};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        BqWorkerFixture fixture;
        if (bq_test_worker_begin(&fixture, cases[index], false))
        {
            bq_test_native_profile_install(&fixture.material);
            u8 bytes[256]; bq_test_native_program(bytes);
            char identity[65] = {0};
            BQ_CHECK(bq_test_native_upload(&fixture.material.queue.queue, bytes, identity));
            BqRequest request = bq_test_native_request(identity);
            u64 id = 0;
            BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK);
            BqError executed = bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id);
            BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
            BQ_CHECK(executed == BQ_OK && job && job->phase == BQ_FINISHED &&
                     job->outcome == (index == 0 ? BQ_SUCCEEDED : BQ_FAILED) &&
                     !fixture.material.queue.queue.state.active_id && !bq_test_worker_probe_locked(fixture.lease));
            BQ_CHECK(fixture.fake.starts == 1 && fixture.fake.argv_valid);
            bq_test_worker_end(&fixture);
        }
    }
    bq_test_worker_child_stage_survives(BQ_NATIVE_STAGE_NAME, 137);
}
#endif
