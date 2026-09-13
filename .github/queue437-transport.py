# One-shot authoring transport, removed from the published implementation tree.
# GitHub connector cannot patch the >1 MiB build.c through Contents API.
from pathlib import Path
import subprocess

assert subprocess.check_output(['git', 'rev-parse', 'HEAD:build.c'], text=True).strip() == '9f13df2ea32c3e98f9d557e1e066f880bfb25363'

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f'expected one exact match, got {text.count(old)}: {old[:150]}')
    return text.replace(old, new, 1)

path = Path('build.c')
text = path.read_text()
header = 'BUSTER_GLOBAL_LOCAL void bench_throughput_add(Arena* arena, SliceString8 arguments)\n{'
start = text.index(header)
end = text.index('\n}\n', start) + 3
function = text[start:end]
function = replace_once(function, header, 'BUSTER_GLOBAL_LOCAL void native_foundation_tool_add(Arena* arena, SliceString8 arguments, bool service)\n{')
function = replace_once(function, 'make_directory_recursive(arena, S8("build/throughput-tools"));', 'make_directory_recursive(arena, service ? S8("build/bench-service-tools") : S8("build/throughput-tools"));')
function = replace_once(function, '#endif\n    // Resolve', '''#endif
    if (service)
    {
        String8 name = sanitize ? S8("service-tests-sanitized") : self_test ? S8("service-tests") : S8("service");
#if BUSTER_WINDOWS
        String8 suffix = S8(".exe");
#else
        String8 suffix = S8("");
#endif
        executable = string_format(arena, S8("build/bench-service-tools/{S8}{S8}"), name, suffix);
    }
    // Resolve''')
function = replace_once(function, 'self_test ? S8("tools/throughput/tests.c") : S8("tools/throughput/throughput.c")', '(service ? (self_test ? S8("tools/bench_service/tests.c") : S8("tools/bench_service/main.c")) : (self_test ? S8("tools/throughput/tests.c") : S8("tools/throughput/throughput.c")))')
function = replace_once(function, 'sanitize ? S8("build/throughput-tool-tests-sanitized") : S8("build/throughput-tool-tests")', 'service ? S8("build/bench-service-tests") : (sanitize ? S8("build/throughput-tool-tests-sanitized") : S8("build/throughput-tool-tests"))')
function += '''
BUSTER_GLOBAL_LOCAL void bench_throughput_add(Arena* arena, SliceString8 arguments)
{
    native_foundation_tool_add(arena, arguments, false);
}

BUSTER_GLOBAL_LOCAL void bench_service_add(Arena* arena, SliceString8 arguments)
{
    native_foundation_tool_add(arena, arguments, true);
}
'''
text = text[:start] + function + text[end:]
text = replace_once(text, 'BUSTER_GLOBAL_LOCAL void bench_throughput_add(Arena* arena, SliceString8 arguments);', 'BUSTER_GLOBAL_LOCAL void bench_throughput_add(Arena* arena, SliceString8 arguments);\nBUSTER_GLOBAL_LOCAL void bench_service_add(Arena* arena, SliceString8 arguments);')
text = replace_once(text, '    bench_throughput_add(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(throughput_self_test));', '''    bench_throughput_add(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(throughput_self_test));
    bench_service_add(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(throughput_self_test));
#if !BUSTER_WINDOWS
    String8 service_sanitized_test[] = {S8("self-test"), S8("--sanitize")};
    bench_service_add(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(service_sanitized_test));
#endif''')
text = replace_once(text, '    BUILD_COMMAND_BENCH_THROUGHPUT,', '    BUILD_COMMAND_BENCH_SERVICE,\n    BUILD_COMMAND_BENCH_THROUGHPUT,')
text = replace_once(text, '        [BUILD_COMMAND_BENCH_THROUGHPUT] = S8_INITIALIZER("bench_throughput"),', '        [BUILD_COMMAND_BENCH_SERVICE] = S8_INITIALIZER("bench_service"),\n        [BUILD_COMMAND_BENCH_THROUGHPUT] = S8_INITIALIZER("bench_throughput"),')
text = replace_once(text, 'if (command == BUILD_COMMAND_BENCH_THROUGHPUT || command == BUILD_COMMAND_BENCH_THROUGHPUT_CI)', 'if (command == BUILD_COMMAND_BENCH_SERVICE || command == BUILD_COMMAND_BENCH_THROUGHPUT || command == BUILD_COMMAND_BENCH_THROUGHPUT_CI)')
text = replace_once(text, '        case BUILD_COMMAND_BENCH_THROUGHPUT:\n        {', '''        case BUILD_COMMAND_BENCH_SERVICE:
        {
            bench_service_add(arena, string8_list_to_slice(arena, throughput_arguments));
        }
        break;
        case BUILD_COMMAND_BENCH_THROUGHPUT:
        {''')
path.write_text(text)

path = Path('tools/bench_service/queue.c')
text = path.read_text()
text = replace_once(text, '            memcpy(request->bytes + request->size, fields[i].pointer, (size_t)fields[i].length);', '''            if (fields[i].length)
            {
                memcpy(request->bytes + request->size, fields[i].pointer, (size_t)fields[i].length);
            }''')
text = replace_once(text, 'if (job->phase == BQ_FINISHED || job->cancel_requested)', 'if (job->phase >= BQ_FINALIZING || job->cancel_requested)')
text = replace_once(text, 'if (error == BQ_OK && job->phase != BQ_FINISHED && !job->cancel_requested)', 'if (error == BQ_OK && job->phase < BQ_FINALIZING && !job->cancel_requested)')
text = replace_once(text, '''                job->phase = BQ_FINISHED;
                job->outcome = job->cancel_requested ? BQ_CANCELLED : BQ_INTERRUPTED;''', '''                job->outcome = job->cancel_requested ? BQ_CANCELLED : job->outcome != BQ_NO_OUTCOME ? job->outcome : BQ_INTERRUPTED;
                job->phase = BQ_FINISHED;''')
path.write_text(text)
path = Path('tools/bench_service/protocol.c')
text = replace_once(path.read_text(), '#define BQ_LOG_PAGE 8u', '#define BQ_LOG_PAGE 4u')
path.write_text(text)

path = Path('tools/bench_service/tests.c')
text = path.read_text()
additional = r'''
BUSTER_GLOBAL_LOCAL void bq_test_completion_failures(void)
{
    for (u32 scenario = 0; scenario < 3; scenario += 1)
    {
        BqFixture fixture;
        if (bq_test_begin(&fixture))
        {
            BqQueue* queue = &fixture.queue;
            BqRequest request = bq_test_request(1, false), next_request = bq_test_request(2, false);
            u64 id = 0, token = 0, next_id = 0;
            BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_submit(queue, &next_request, &next_id) == BQ_OK);
            BQ_CHECK(bq_reserve(queue, &id, &token) == BQ_OK);
            for (u32 phase = BQ_PREPARING; phase <= BQ_CLEANING; phase += 1)
            {
                BQ_CHECK(bq_fake_step(queue, id, token) == BQ_OK);
            }
            u64 before = queue->state.sequence;
            BQ_CHECK(bq_cancel(queue, id) == BQ_OK && queue->state.sequence == before);
            queue->fault.fail_write_at = scenario == 0 ? 2 : 0;
            queue->fault.after_sync = scenario == 1;
            queue->fault.before_sync = scenario == 2;
            BQ_CHECK(bq_fake_step(queue, id, token) == BQ_IO && queue->state.active_id == id);
            u64 ignored_id = 0, ignored_token = 0;
            BQ_CHECK(bq_reserve(queue, &ignored_id, &ignored_token) == BQ_IO);
            bq_close(queue);
            BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK);
            if (scenario == 0)
            {
                BQ_CHECK(queue->needs_reconciliation);
                BQ_CHECK(bq_reserve(queue, &ignored_id, &ignored_token) == BQ_RECONCILIATION_REQUIRED);
                BQ_CHECK(bq_fake_reconcile(queue, id, token) == BQ_OK);
            }
            BqJob* job = bq_job(&queue->state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED && job->validity == BQ_NOT_EVALUATED);
            BQ_CHECK(bq_fake_run(queue, &ignored_id) == BQ_OK && ignored_id == next_id);
            bq_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_protocol_mutations(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        BqQueue* queue = &fixture.queue;
        BqRequest submission = bq_test_request(1, false);
        BqPacket request, response;
        bq_packet(&request, BQ_OP_SUBMIT, 9, submission.bytes, submission.size);
        for (u32 prefix = 0; prefix < request.size; prefix += 1)
        {
            BQ_CHECK(bq_dispatch(queue, request.bytes, prefix, &response) == BQ_BAD_REQUEST);
            BQ_CHECK(queue->state.job_count == 0);
        }
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_OK);
        u64 id = bq_u64(response.bytes + BQ_CONTROL_HEADER + 4);
        /* Drop that response, restart, and retry identical wire bytes. */
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_OK);
        BQ_CHECK(bq_u64(response.bytes + BQ_CONTROL_HEADER + 4) == id && queue->state.job_count == 1);
        u64 sequence = queue->state.sequence;
        u8 concatenated[BQ_CONTROL_CAP * 2];
        memcpy(concatenated, request.bytes, request.size);
        memcpy(concatenated + request.size, request.bytes, request.size);
        BQ_CHECK(bq_dispatch(queue, concatenated, request.size * 2, &response) == BQ_BAD_REQUEST);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size + 1, &response) == BQ_BAD_REQUEST);
        bq_put32(request.bytes + BQ_CONTROL_HEADER, UINT32_MAX);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        bq_packet(&request, 999, 10, NULL, 0);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        u8 body[16] = {0};
        bq_put64(body, id);
        bq_packet(&request, BQ_OP_STATUS, 10, body, 9);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        bq_put64(body + 8, UINT64_MAX);
        bq_packet(&request, BQ_OP_LOGS, 10, body, 16);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        BQ_CHECK(queue->state.sequence == sequence);
        bq_test_end(&fixture);
    }
}
'''
text = replace_once(text, '#endif\n\nint main(int argc, char** argv)', additional + '#endif\n\nint main(int argc, char** argv)')
text = replace_once(text, '    bq_test_phase_restarts();', '    bq_test_phase_restarts();\n    bq_test_completion_failures();\n    bq_test_protocol_mutations();')
path.write_text(text)
print('Applied exact, guarded build integration and static-review corrections; no tests run by this script.')
