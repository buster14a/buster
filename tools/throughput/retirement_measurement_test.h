/* Real child executions with deterministic fixture output, never performance
 * acceptance. The fixture checks cwd, explicit environment, stdin and descriptor
 * isolation. The ordinary native and sanitized harnesses both run this path. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_MEASUREMENT_TEST_H
#define BUSTER_THROUGHPUT_RETIREMENT_MEASUREMENT_TEST_H
#include "retirement_measurement.h"

#ifdef __linux__
static int test_retirement_measurement_child(int argc, char** argv)
{
    int result = 2;
    if (argc == 6)
    {
        char byte;
        char const* marker = getenv("TP_RETIREMENT_TEST");
        int leak = atoi(argv[5]);
        int ok = marker && !strcmp(marker, "explicit") && !getenv("TP_RETIREMENT_AMBIENT") &&
            read(STDIN_FILENO, &byte, 1) == 0 && fcntl(leak, F_GETFD) < 0 && errno == EBADF &&
            access("cwd-marker", F_OK) == 0;
        if (ok && !strcmp(argv[4], "timeout")) test_delay(5000);
        if (ok && !strcmp(argv[4], "fail")) result = 7;
        else if (ok && !strcmp(argv[4], "missing")) result = 0;
        else if (ok)
        {
            char const* bytes = !strcmp(argv[4], "wrong") ? "wrong\n" : "fixture-code\n";
            if (!strcmp(argv[2], "compiler"))
            {
                if (!strcmp(argv[4], "symlink")) ok = symlink("oracle-artifact", argv[3]) == 0;
                else if (!strcmp(argv[4], "hardlink")) ok = link("oracle-artifact", argv[3]) == 0;
                else
                {
                    FILE* output = fopen(argv[3], "wb");
                    unsigned char artifact[1024];
                    unsigned count = test_artifact_fixture(artifact, 1, 1);
                    ok = output && (!strcmp(argv[4], "wrong") ? fputs(bytes, output) >= 0 :
                        fwrite(artifact, 1, count, output) == count);
                    if (output && fclose(output) != 0) ok = 0;
                }
            }
            else if (!strcmp(argv[2], "runtime")) ok = fputs(bytes, stdout) >= 0 && fflush(stdout) == 0;
            else ok = 0;
            result = ok ? 0 : 3;
        }
    }
    return result;
}

static void test_retirement_measurement_command_file(char const* root, char const* leaf,
    TpRetirementMeasuredCommand const* command)
{
    char path[TP_PATH_CAP];
    CHECK(tp_path(path, root, leaf));
    FILE* file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file)
    {
        fputs("{\"argv\":[", file);
        for (unsigned i = 0; i < command->argument_count; ++i)
        {
            if (i) fputc(',', file);
            tp_json_string(file, command->arguments[i]);
        }
        fputs("],\"cwd\":", file); tp_json_string(file, command->directory);
        fputs(",\"environment\":[", file);
        for (unsigned i = 0; i < command->environment_count; ++i)
        {
            if (i) fputc(',', file);
            tp_json_string(file, command->environment[i]);
        }
        fputs("]}", file);
        CHECK(fclose(file) == 0);
    }
}

static void test_retirement_measurement(char const* executable_path, char const* root)
{
    char directory[TP_PATH_CAP], executable_copy[TP_PATH_CAP], path[TP_PATH_CAP];
    CHECK(tp_path(directory, root, "retirement-measured"));
    CHECK(tp_mkdirs(directory));
    CHECK(test_text(directory, "cwd-marker", "fixed cwd\n"));
    CHECK(test_text(directory, "oracle-artifact", "fixture-code\n"));
    CHECK(tp_path(executable_copy, directory, "frozen-child"));
    CHECK(tp_copy_file(executable_path, executable_copy));
    CHECK(chmod(executable_copy, 0500) == 0);
    int binary = open(executable_copy, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int cwd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int leak = fcntl(binary, F_DUPFD, 512); /* Intentionally lacks CLOEXEC. */
    char leak_text[32];
    snprintf(leak_text, sizeof(leak_text), "%d", leak);
    CHECK(binary >= 3 && cwd >= 3 && leak >= 3);
    char digest[65], expected_output[65], expected_artifact[65], malformed_digest[65];
    uint64_t bytes = 0;
    CHECK(tp_retirement_file_hash(binary, digest, &bytes));
    CHECK(bytes > 0);
    TpRetirementExecutable executable;
    CHECK(!tp_retirement_executable_init(&executable, binary, "not-a-digest"));
    CHECK(!executable.valid && executable.descriptor == -1);
    CHECK(!tp_retirement_executable_init(&executable, leak, digest));
    char wrong_digest[65];
    memcpy(wrong_digest, digest, sizeof(digest));
    wrong_digest[0] = wrong_digest[0] == 'a' ? 'b' : 'a';
    CHECK(!tp_retirement_executable_init(&executable, binary, wrong_digest));
    CHECK(chmod(executable_copy, 0700) == 0);
    CHECK(!tp_retirement_executable_init(&executable, binary, digest));
    CHECK(chmod(executable_copy, 0500) == 0);
    CHECK(tp_retirement_executable_init(&executable, binary, digest));
    Sha256 hash;
    sha256_init(&hash); sha256_add(&hash, "fixture-code\n", 13); sha256_finish_hex(&hash, expected_output);
    unsigned char artifact[1024];
    unsigned artifact_bytes = test_artifact_fixture(artifact, 1, 1);
    sha256_init(&hash); sha256_add(&hash, artifact, artifact_bytes); sha256_finish_hex(&hash, expected_artifact);
    sha256_init(&hash); sha256_add(&hash, "wrong\n", 6); sha256_finish_hex(&hash, malformed_digest);
    char* environment[] = {"LC_ALL=C", "TP_RETIREMENT_TEST=explicit", NULL};
    char* arguments[] = {executable_copy, "retirement-child", "compiler", "artifact.bin", "ok", leak_text, NULL};
    char command_digest[65];
    TpRetirementMeasuredCommand command = {.arguments = arguments, .argument_count = 6,
        .environment = environment, .environment_count = 2, .directory = directory,
        .artifact = "artifact.bin", .timeout_seconds = 2, .command_sha256 = command_digest,
        .output_sha256 = expected_artifact, .code_section_sha256 = expected_output, .code_section_bytes = 13};
    CHECK(tp_retirement_command_hash(&command, command_digest));
    test_retirement_measurement_command_file(root, "retirement-measured-command-compiler.json", &command);
    arguments[4] = "literal \\ and \"quotes\"";
    CHECK(tp_retirement_command_hash(&command, command_digest));
    test_retirement_measurement_command_file(root, "retirement-measured-command-escaped.json", &command);
    CHECK(test_text(root, "retirement-measured-command-escaped.sha256", command_digest));
    arguments[4] = "bad\nargument";
    CHECK(!tp_retirement_command_hash(&command, command_digest) && !command_digest[0]);
    char* huge = (char*)malloc(TP_RETIREMENT_COMMAND_BYTES + 1);
    CHECK(huge != NULL);
    if (huge)
    {
        memset(huge, 'x', TP_RETIREMENT_COMMAND_BYTES);
        huge[TP_RETIREMENT_COMMAND_BYTES] = 0;
        arguments[4] = huge;
        CHECK(!tp_retirement_command_hash(&command, command_digest));
        free(huge);
    }
    arguments[4] = "ok";
    environment[1] = "LC_ALL=D";
    CHECK(!tp_retirement_command_hash(&command, command_digest));
    environment[1] = "1INVALID=value";
    CHECK(!tp_retirement_command_hash(&command, command_digest));
    environment[1] = "TP_RETIREMENT_TEST=explicit";
    command.argument_count = TP_RETIREMENT_COMMAND_ARGUMENTS + 1;
    CHECK(!tp_retirement_command_hash(&command, command_digest));
    command.argument_count = 6;
    command.environment_count = TP_RETIREMENT_COMMAND_ENVIRONMENT + 1;
    CHECK(!tp_retirement_command_hash(&command, command_digest));
    command.environment_count = 2;
    CHECK(setenv("TP_RETIREMENT_AMBIENT", "must-not-leak", 1) == 0);
    /* A complete 60-pair/two-round collection passes through real fresh
     * processes for compiler and runtime, including every warmup. */
    TpSampleTest test;
    CHECK(test_sample_open(&test, 1));
    test.transcript.cpu = tp_first_allowed_cpu();
    TpRetirementInvocation invocation;
    int ok = test.transcript.cpu >= 0;
    while (ok && tp_retirement_execution_peek(&test.execution, &invocation) == TP_RETIREMENT_NEXT_READY)
    {
        command.kind = invocation.kind;
        command.variant = invocation.variant;
        arguments[2] = invocation.kind ? "runtime" : "compiler";
        command.artifact = invocation.kind ? NULL : "artifact.bin";
        command.code_section_bytes = invocation.kind ? 0 : 13;
        command.code_section_sha256 = invocation.kind ? NULL : expected_output;
        command.output_sha256 = invocation.kind ? expected_output : expected_artifact;
        CHECK(tp_retirement_command_hash(&command, command_digest));
        if (invocation.kind && !invocation.phase && !invocation.warmup && !invocation.variant)
            test_retirement_measurement_command_file(root, "retirement-measured-command-runtime.json", &command);
        int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        CHECK(log >= 3);
        TpProcessInputs inputs = {binary, cwd, log, environment};
        TpRetirementMeasurementResult result;
        ok = tp_retirement_measurement_run(&test.samples, &command, &executable, &inputs, cwd, &result);
        CHECK(ok);
        CHECK(result.status == TP_RETIREMENT_MEASUREMENT_COMPLETE &&
              result.output_bytes == (invocation.kind ? 13 : artifact_bytes));
        if (log >= 0) CHECK(close(log) == 0);
        if (ok)
        {
            CHECK(unlinkat(cwd, "child.log", 0) == 0);
            if (!invocation.kind) CHECK(unlinkat(cwd, "artifact.bin", 0) == 0);
        }
    }
    CHECK(ok && tp_retirement_execution_complete(&test.execution));
    TpRetirementShard transcript_shard, sample_shard;
    CHECK(tp_retirement_transcript_end_shard(&test.transcript, &transcript_shard));
    CHECK(tp_retirement_transcript_finish(&test.transcript, tp_process_monotonic_ns()));
    CHECK(tp_retirement_samples_begin_export(&test.samples));
    CHECK(tp_path(path, root, "retirement-measured-samples.jsonl"));
    FILE* output = fopen(path, "wb+");
    CHECK(output && tp_retirement_samples_write_shard(&test.samples, output, &sample_shard));
    if (output) CHECK(fclose(output) == 0);
    CHECK(tp_retirement_samples_finish(&test.samples));
    CHECK(transcript_shard.records == 488 && sample_shard.records == 120);
    CHECK(tp_path(path, root, "retirement-measured-execution.jsonl"));
    output = fopen(path, "wb");
    CHECK(output && fseek(test.stream, 0, SEEK_SET) == 0);
    if (output)
    {
        unsigned char buffer[4096];
        size_t count;
        while ((count = fread(buffer, 1, sizeof(buffer), test.stream)) != 0)
            CHECK(fwrite(buffer, 1, count, output) == count);
        CHECK(!ferror(test.stream) && fclose(output) == 0);
    }
    test_sample_close(&test);

    /* A failed command cannot advance or restart the same attempt. All output
     * remains present for the service's failure retention/sealing path. */
    for (unsigned failure = 0; failure < 23; ++failure)
    {
        CHECK(test_sample_open(&test, 1));
        test.transcript.cpu = tp_first_allowed_cpu();
        unsigned behavior = failure >= 16 ? failure - 16 : failure;
        command.kind = failure >= 16 && failure < 20;
        command.variant = 0;
        command.artifact = command.kind ? NULL : "artifact.bin";
        command.code_section_bytes = command.kind ? 0 : 13;
        command.code_section_sha256 = command.kind ? NULL : expected_output;
        command.output_sha256 = command.kind ? expected_output : expected_artifact;
        arguments[2] = command.kind ? "runtime" : "compiler";
        arguments[4] = behavior == 0 ? "fail" : behavior == 1 ? "wrong" : behavior == 2 ? "missing" :
                       behavior == 3 ? "timeout" : behavior == 14 ? "symlink" : behavior == 15 ? "hardlink" : "ok";
        if (failure == 20) command.code_section_bytes = 12;
        if (failure == 21) command.code_section_sha256 = expected_artifact;
        if (failure == 22) { arguments[4] = "wrong"; command.output_sha256 = malformed_digest; }
        command.timeout_seconds = behavior == 3 ? 1 : 2;
        if (command.kind)
        {
            while (tp_retirement_execution_peek(&test.execution, &invocation) == TP_RETIREMENT_NEXT_READY &&
                   !invocation.kind)
                CHECK(test_sample_observe(&test, 0, 0));
        }
        uint64_t before = test.execution.sequence;
        command.row = failure == 4 ? 1 : 0;
        CHECK(tp_retirement_command_hash(&command, command_digest));
        if (failure == 5) command_digest[0] = command_digest[0] == 'a' ? 'b' : 'a';
        if (failure == 6) command.code_section_bytes = 0;
        if (failure == 7) CHECK(test_text(directory, "artifact.bin", "fixture-code\n"));
        if (failure == 8) CHECK(symlinkat("cwd-marker", cwd, "artifact.bin") == 0);
        if (failure == 9) CHECK(chmod(executable_copy, 0700) == 0);
        int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        CHECK(log >= 3);
        TpProcessInputs inputs = {binary, cwd, log, environment};
        if (failure == 10) CHECK(write(log, "stale", 5) == 5);
        if (failure == 11) inputs.environment = NULL;
        if (failure == 12) CHECK(fcntl(log, F_SETFD, 0) == 0);
        if (failure == 13) command.directory = "/";
        TpRetirementMeasurementResult result;
        CHECK(!tp_retirement_measurement_run(&test.samples, &command, &executable, &inputs, cwd, &result));
        if (behavior == 0 || behavior == 3)
        {
            CHECK(result.status == TP_RETIREMENT_MEASUREMENT_PROCESS_FAILED && result.observed.valid);
            if (!behavior) CHECK(result.process.exit_code == 7 && !result.process.timed_out);
            else CHECK(result.process.timed_out && result.process.signal_number != 0);
        }
        else if (behavior == 1 || behavior == 2 || failure == 14 || failure == 15)
        {
            CHECK(result.status == TP_RETIREMENT_MEASUREMENT_OUTPUT_INVALID && result.observed.valid);
            if (behavior == 1) CHECK(result.output_bytes == 6 && strcmp(result.output_sha256, expected_output));
        }
        CHECK(test.samples.failed && test.execution.sequence == before && test.samples.collected == before);
        CHECK(!tp_retirement_measurement_run(&test.samples, &command, &executable, &inputs, cwd, &result));
        CHECK(result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID && !result.observed.valid);
        struct stat retained;
        CHECK(fstatat(cwd, "child.log", &retained, AT_SYMLINK_NOFOLLOW) == 0);
        if (failure == 1 || failure == 7 || failure == 8 || failure == 14 || failure == 15)
            CHECK(fstatat(cwd, "artifact.bin", &retained, AT_SYMLINK_NOFOLLOW) == 0);
        if (log >= 0) CHECK(close(log) == 0);
        CHECK(unlinkat(cwd, "child.log", 0) == 0);
        if (fstatat(cwd, "artifact.bin", &retained, AT_SYMLINK_NOFOLLOW) == 0)
            CHECK(unlinkat(cwd, "artifact.bin", 0) == 0);
        CHECK(chmod(executable_copy, 0500) == 0);
        CHECK(tp_retirement_executable_init(&executable, binary, digest));
        command.directory = directory;
        test_sample_close(&test);
    }
    CHECK(unsetenv("TP_RETIREMENT_AMBIENT") == 0);
    if (binary >= 0) CHECK(close(binary) == 0);
    if (cwd >= 0) CHECK(close(cwd) == 0);
    if (leak >= 0) CHECK(close(leak) == 0);
    CHECK(unlink(executable_copy) == 0);
}
#endif
#endif
