/* MCP regressions use the production parser, adapter and authenticated socket.
 * bq_test_mcp_protocol covers lifecycle/schema/bounds without a live service;
 * bq_test_mcp_artifacts checks typed sealed-export receipt/slice retrieval;
 * bq_test_mcp_receipts checks reply integrity and private-path suppression;
 * bq_test_mcp_socket uses a disposable real queue daemon with no worker config.
 * It never starts a recipe, manager unit, remote connection or benchmark.
 */
BUSTER_GLOBAL_LOCAL char const bq_test_mcp_initialize[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\",\"version\":\"1\"}}}";
BUSTER_GLOBAL_LOCAL char const bq_test_mcp_initialized[] = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";

BUSTER_GLOBAL_LOCAL bool bq_test_mcp_contains(BqMcpBuffer const* buffer, char const* expected)
{
    u32 length = (u32)strlen(expected);
    bool found = false;
    for (u32 i = 0; length <= buffer->count && i <= buffer->count - length; i += 1)
        found = found || !memcmp(buffer->bytes + i, expected, length);
    return found;
}

BUSTER_GLOBAL_LOCAL void bq_test_mcp_ready(BqMcpSession* session, char const* socket_path)
{
    BqMcpBuffer output;
    bq_mcp_message(session, socket_path, string_from_pointer(bq_test_mcp_initialize), &output);
    BQ_CHECK(!output.failed && session->phase == 1 && bq_test_mcp_contains(&output, "2025-11-25"));
    bq_mcp_message(session, socket_path, string_from_pointer(bq_test_mcp_initialized), &output);
    BQ_CHECK(!output.failed && !output.count && session->phase == 2);
}

BUSTER_GLOBAL_LOCAL bool bq_test_mcp_result(BqMcpBuffer const* output, BqMcpJson* json, u32* result)
{
    bool ok = !output->failed && bq_mcp_json_parse(json, (String8){(char8*)output->bytes, output->count});
    u32 envelope = ok ? bq_mcp_member(json, 0, S8("result")) : BQ_MCP_NONE;
    *result = bq_mcp_member(json, envelope, S8("structuredContent"));
    u32 content = bq_mcp_member(json, envelope, S8("content"));
    String8 text;
    ok = ok && *result < json->count && content < json->count && json->tokens[content].kind == BQ_MCP_ARRAY &&
         bq_mcp_text(json, bq_mcp_member(json, content + 1, S8("text")), &text);
    if (ok)
    {
        BqMcpJson decoded;
        ok = bq_mcp_json_parse(&decoded, text) && decoded.tokens[0].kind == BQ_MCP_OBJECT;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_test_mcp_protocol(void)
{
    char const* missing = "/tmp/buster-mcp-no-service.sock";
#ifdef __linux__
    char const* transport_error = "io-uncertain";
#else
    char const* transport_error = "unsupported";
#endif
    BqMcpJson json;
    char const* bad_json[] = {"", "{", "{}{}", "{\"a\":1,}", "[1,]", "{\"a\":01}", "{\"a\":1.}",
        "{\"a\":1e+}", "{\"a\":-}", "{\"a\":truex}", "{\"a\":nul}", "{\"a\":\"\\x\"}",
        "{\"a\":\"\\uDC00\"}", "{\"a\":\"\\uD800\"}", "{\"a\":\"\\uD800\\u0000\"}", "{\"a\":\"\001\"}",
        "{\"name\":1,\"\\u006eame\":2}", "{\"a\":{\"x\":1,\"x\":2}}", "{\"a\":\"\xc0\x80\"}",
        "{\"a\":\"\xed\xa0\x80\"}", "{\"a\":\"\xf4\x90\x80\x80\"}", "{\"a\":\"\x80\"}"};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(bad_json); i += 1)
        BQ_CHECK(!bq_mcp_json_parse(&json, string_from_pointer(bad_json[i])));
    BQ_CHECK(bq_mcp_json_parse(&json, S8("{\"a\":[{},[],true,false,null,0,-1,2.3,4e+5],\"u\":\"\\uD83D\\uDE00\\u0000\"}")));
    String8 unicode;
    BQ_CHECK(bq_mcp_text(&json, bq_mcp_member(&json, 0, S8("u")), &unicode) && unicode.length == 5 &&
             !memcmp(unicode.pointer, "\xf0\x9f\x98\x80\0", 5));
    char nested[BQ_MCP_DEPTH_CAP * 2 + 3];
    for (u32 i = 0; i < BQ_MCP_DEPTH_CAP + 1; i += 1)
    {
        nested[i] = '[';
        nested[BQ_MCP_DEPTH_CAP + 1 + i] = ']';
    }
    nested[BQ_MCP_DEPTH_CAP * 2 + 2] = 0;
    BQ_CHECK(!bq_mcp_json_parse(&json, string_from_pointer(nested)));
    BQ_CHECK(bq_mcp_json_parse(&json, (String8){(char8*)nested + 1, BQ_MCP_DEPTH_CAP * 2}));
    char token_overflow[BQ_MCP_TOKEN_CAP * 2 + 3];
    token_overflow[0] = '[';
    for (u32 i = 0; i < BQ_MCP_TOKEN_CAP; i += 1)
    {
        token_overflow[1 + i * 2] = '0';
        token_overflow[2 + i * 2] = i + 1 == BQ_MCP_TOKEN_CAP ? ']' : ',';
    }
    token_overflow[BQ_MCP_TOKEN_CAP * 2 + 1] = 0;
    BQ_CHECK(!bq_mcp_json_parse(&json, string_from_pointer(token_overflow)));
    BQ_CHECK(!bq_mcp_json_parse(&json, (String8){NULL, BQ_MCP_MESSAGE_CAP + 1u}));

    BqMcpSession session = {0};
    BqMcpBuffer output;
    bq_mcp_message(&session, missing, S8("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"), &output);
    BQ_CHECK(bq_test_mcp_contains(&output, "-32002") && !session.phase);
    bq_mcp_message(&session, missing, S8("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}"), &output);
    BQ_CHECK(bq_test_mcp_contains(&output, "\"result\":{}") && !session.phase);
    bq_test_mcp_ready(&session, missing);
    bq_mcp_message(&session, missing, string_from_pointer(bq_test_mcp_initialize), &output);
    BQ_CHECK(bq_test_mcp_contains(&output, "-32602") && session.phase == 2);
    bq_mcp_message(&session, missing, S8("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}"), &output);
    BQ_CHECK(!output.failed && bq_mcp_json_parse(&json, (String8){(char8*)output.bytes, output.count}));
    u32 result = bq_mcp_member(&json, 0, S8("result"));
    u32 tools = bq_mcp_member(&json, result, S8("tools")), count = 0;
    BQ_CHECK(tools < json.count && json.tokens[tools].kind == BQ_MCP_ARRAY);
    if (tools < json.count)
    {
        for (u32 i = tools + 1; i < json.tokens[tools].end; i = json.tokens[i].end)
        {
            count += 1;
            String8 name;
            BQ_CHECK(bq_mcp_text(&json, bq_mcp_member(&json, i, S8("name")), &name));
            u32 operation = bq_mcp_tool(name), annotations = bq_mcp_member(&json, i, S8("annotations"));
            u32 readonly = bq_mcp_member(&json, annotations, S8("readOnlyHint"));
            u32 expected = count <= 6 ? count : BQ_OP_NATIVE_BEGIN + count - 7;
            bool write = operation == BQ_OP_SUBMIT || operation == BQ_OP_CANCEL || operation >= BQ_OP_NATIVE_BEGIN;
            BQ_CHECK(operation == expected && readonly < json.count && string_equal(json.tokens[readonly].text,
                write ? S8("false") : S8("true")));
        }
    }
    BQ_CHECK(count == 9);
    char const* invalid_envelopes[] = {
        "[]", "null", "{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"ping\"}",
        "{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"ping\"}",
        "{\"jsonrpc\":\"2.0\",\"id\":true,\"method\":\"ping\"}",
        "{\"jsonrpc\":\"2.0\",\"id\":1.0,\"method\":\"ping\"}",
        "{\"jsonrpc\":\"2.0\",\"id\":9007199254740992,\"method\":\"ping\"}",
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"extra\":1}"};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(invalid_envelopes); i += 1)
    {
        bq_mcp_message(&session, missing, string_from_pointer(invalid_envelopes[i]), &output);
        BQ_CHECK(!output.failed && bq_test_mcp_contains(&output, "-32600") && bq_test_mcp_contains(&output, "\"id\":null"));
    }
    char const* bad_metadata[] = {"null", "[]", "1", "true", "{\"progressToken\":null}", "{\"progressToken\":false}", "{\"progressToken\":[]}", "{\"progressToken\":{}}"};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(bad_metadata); i += 1)
    {
        char message[512];
        snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"ping\",\"params\":{\"_meta\":%s}}", bad_metadata[i]);
        bq_mcp_message(&session, missing, string_from_pointer(message), &output);
        BQ_CHECK(bq_test_mcp_contains(&output, "-32602"));
        BqMcpSession initializing = {.phase = 1};
        snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\",\"params\":{\"_meta\":%s}}", bad_metadata[i]);
        bq_mcp_message(&initializing, missing, string_from_pointer(message), &output);
        BQ_CHECK(!output.count && initializing.phase == 1);
    }
    bq_mcp_message(&session, missing, S8("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/list\",\"params\":{\"_meta\":{\"progressToken\":\"test\"}}}"), &output);
    BQ_CHECK(bq_test_mcp_contains(&output, "bench_capabilities") && !bq_test_mcp_contains(&output, "\"error\""));
    bq_mcp_message(&session, missing, S8("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_capabilities\",\"_meta\":{\"progressToken\":1}}}"), &output);
    BQ_CHECK(bq_test_mcp_contains(&output, "structuredContent") && bq_test_mcp_contains(&output, transport_error));
    char const* bad_arguments[] = {"null", "[]", "{}", "{\"job_id\":null}", "{\"job_id\":true}",
        "{\"job_id\":1}", "{\"job_id\":1.0}", "{\"job_id\":{}}", "{\"job_id\":\"0\"}",
        "{\"job_id\":\"01\"}", "{\"job_id\":\"-1\"}", "{\"job_id\":\"1e2\"}", "{\"job_id\":\"18446744073709551616\"}",
        "{\"job_id\":\"1\",\"request_id\":\"unsupported\"}", "{\"job_id\":\"1\",\"max_bytes\":5}",
        "{\"job_id\":\"1\",\"cursor\":\"0\"}"};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(bad_arguments); i += 1)
    {
        char message[1024];
        snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_cancel\",\"arguments\":%s}}", bad_arguments[i]);
        bq_mcp_message(&session, missing, string_from_pointer(message), &output);
        BQ_CHECK(!output.failed && bq_test_mcp_contains(&output, "-32602") && !bq_test_mcp_contains(&output, "structuredContent"));
    }
    bq_mcp_message(&session, missing, S8("{\"jsonrpc\":\"2.0\",\"id\":\"quote\\\"\\u0000\",\"method\":\"ping\"}"), &output);
    BQ_CHECK(bq_mcp_json_parse(&json, (String8){(char8*)output.bytes, output.count}) &&
             bq_test_mcp_contains(&output, "quote\\\"\\u0000"));
    bq_mcp_message(&session, missing, S8("{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"bench_cancel\",\"arguments\":{\"job_id\":\"1\"}}}"), &output);
    BQ_CHECK(!output.count && session.phase == 2);
    bq_mcp_message(&session, missing, S8("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":7}}"), &output);
    BQ_CHECK(!output.count && session.phase == 2);
    bq_mcp_message(&session, missing, S8("{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_status\",\"arguments\":{\"job_id\":\"18446744073709551615\"}}}"), &output);
    BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"isError\":true"));

    FILE* input = tmpfile();
    FILE* reply = tmpfile();
    BQ_CHECK(input && reply);
    if (input && reply)
    {
        fprintf(input, "%s\n%s\n{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}\n", bq_test_mcp_initialize, bq_test_mcp_initialized);
        rewind(input);
        BQ_CHECK(bq_mcp_run(missing, input, reply) == BQ_OK);
        rewind(reply);
        char line[2048];
        u32 lines = 0;
        while (fgets(line, sizeof(line), reply))
        {
            lines += 1;
            BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(line)));
        }
        BQ_CHECK(lines == 2);
    }
    if (input) fclose(input);
    if (reply) fclose(reply);
    input = tmpfile();
    reply = tmpfile();
    BQ_CHECK(input && reply);
    if (input && reply)
    {
        for (u32 i = 0; i < BQ_MCP_MESSAGE_CAP + 1; i += 1) fputc('x', input);
        rewind(input);
        BQ_CHECK(bq_mcp_run(missing, input, reply) == BQ_BAD_REQUEST && ftell(input) == BQ_MCP_MESSAGE_CAP + 1);
        rewind(reply);
        char line[512];
        BQ_CHECK(fgets(line, sizeof(line), reply) && strstr(line, "-32700"));
    }
    if (input) fclose(input);
    if (reply) fclose(reply);
    input = tmpfile();
    reply = tmpfile();
    BQ_CHECK(input && reply);
    if (input && reply)
    {
        fputs(bq_test_mcp_initialize, input);
        rewind(input);
        BQ_CHECK(bq_mcp_run(missing, input, reply) == BQ_BAD_REQUEST);
        rewind(reply);
        char line[512];
        BQ_CHECK(fgets(line, sizeof(line), reply) && strstr(line, "-32700"));
    }
    if (input) fclose(input);
    if (reply) fclose(reply);
}

BUSTER_GLOBAL_LOCAL void bq_test_mcp_program_codec(void)
{
    char const* hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    BqMcpJson json;
    BqPacket request, response;
    char text[2048];
    snprintf(text, sizeof(text), "{\"program_sha256\":\"%s\",\"program_size\":\"256\",\"offset\":\"0\",\"bytes_hex\":\"00aaff\"}", hash);
    BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) &&
             bq_mcp_arguments(&json, 0, BQ_OP_NATIVE_WRITE, &request) && request.size == BQ_CONTROL_HEADER + 83 &&
             bq_u32(request.bytes + 8) == BQ_OP_NATIVE_WRITE && bq_u64(request.bytes + BQ_CONTROL_HEADER + 64) == 256 &&
             !memcmp(request.bytes + BQ_CONTROL_HEADER + 80, "\0\xaa\xff", 3));
    u8 body[76] = {0};
    char manifest[BQ_NATIVE_MANIFEST_CAP], identity[65];
    BQ_CHECK(bq_native_manifest(manifest, (u8 const*)hash, 256, identity) > 0);
    memcpy(body + 12, identity, 64);
    bq_put64(body + 4, 3);
    bq_packet(&response, BQ_OP_NATIVE_WRITE | 0x80000000u, 1, body, sizeof(body));
    BqMcpBuffer result = {0};
    BQ_CHECK(bq_mcp_service_result(&request, &response, &result) &&
             bq_test_mcp_contains(&result, "\"cursor\":\"3\"") && bq_test_mcp_contains(&result, "\"committed\":false"));
    bq_put64(response.bytes + BQ_CONTROL_HEADER + 4, 2);
    result = (BqMcpBuffer){0};
    BQ_CHECK(!bq_mcp_service_result(&request, &response, &result) && !result.count);
    char const* bad_write[] = {"{}", "[]", "null", "{\"offset\":\"0\"}",
        "{\"program_sha256\":\"a\",\"program_size\":\"256\",\"offset\":\"0\",\"bytes_hex\":\"00\"}"};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(bad_write); i += 1)
        BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(bad_write[i])) &&
                 !bq_mcp_arguments(&json, 0, BQ_OP_NATIVE_WRITE, &request) && !request.size);
    char const* bad_hex[] = {"", "0", "0A", "zz", "00 ", "\\u0000"};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(bad_hex); i += 1)
    {
        snprintf(text, sizeof(text), "{\"program_sha256\":\"%s\",\"program_size\":\"256\",\"offset\":\"0\",\"bytes_hex\":\"%s\"}", hash, bad_hex[i]);
        BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) &&
                 !bq_mcp_arguments(&json, 0, BQ_OP_NATIVE_WRITE, &request) && !request.size);
    }
    char const* bad_offset[] = {"\"256\"", "\"257\"", "\"18446744073709551615\"", "\"01\"", "\"-1\"", "0", "null"};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(bad_offset); i += 1)
    {
        snprintf(text, sizeof(text), "{\"program_sha256\":\"%s\",\"program_size\":\"256\",\"offset\":%s,\"bytes_hex\":\"00\"}", hash, bad_offset[i]);
        BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) &&
                 !bq_mcp_arguments(&json, 0, BQ_OP_NATIVE_WRITE, &request) && !request.size);
    }
    char const* sizes[] = {"\"63\"", "\"4194305\"", "\"01\"", "\"18446744073709551615\"", "256", "null"};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        snprintf(text, sizeof(text), "{\"program_sha256\":\"%s\",\"program_size\":%s}", hash, sizes[i]);
        BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) &&
                 !bq_mcp_arguments(&json, 0, BQ_OP_NATIVE_BEGIN, &request) && !request.size);
    }
    char hex[867];
    memset(hex, '0', sizeof(hex) - 1);
    hex[sizeof(hex) - 1] = 0;
    snprintf(text, sizeof(text), "{\"program_sha256\":\"%s\",\"program_size\":\"4194304\",\"offset\":\"0\",\"bytes_hex\":\"%s\"}", hash, hex);
    BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) && !bq_mcp_arguments(&json, 0, BQ_OP_NATIVE_WRITE, &request));
    hex[864] = 0;
    snprintf(text, sizeof(text), "{\"program_sha256\":\"%s\",\"program_size\":\"4194304\",\"offset\":\"4193872\",\"bytes_hex\":\"%s\"}", hash, hex);
    BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) && bq_mcp_arguments(&json, 0, BQ_OP_NATIVE_WRITE, &request) &&
             request.size == BQ_CONTROL_CAP);
    snprintf(text, sizeof(text), "{\"program_sha256\":\"%s\",\"program_size\":\"256\",\"command\":\"program\"}", hash);
    BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) && !bq_mcp_arguments(&json, 0, BQ_OP_NATIVE_FINISH, &request));
    BQ_CHECK(bq_mcp_recipe_available(S8("service-recipes=validate-buster-v1,native-execute-v1\n"), S8(BQ_NATIVE_RECIPE)));
    BQ_CHECK(!bq_mcp_recipe_available(S8("blocked-recipes=native-execute-v1\n"), S8(BQ_NATIVE_RECIPE)));
    BQ_CHECK(!bq_mcp_recipe_available(S8("service-recipes=native-execute-v1-extra\n"), S8(BQ_NATIVE_RECIPE)));
    BQ_CHECK(!bq_mcp_recipe_available(S8("xservice-recipes=native-execute-v1\n"), S8(BQ_NATIVE_RECIPE)));
}

#define BQ_TEST_ARTIFACT_COUNT 6000u
/* Typed sealed-export retrieval: only job identities and an offset cross the
 * boundary, a slice comes from the aligned backend chunk containing it, and a
 * reply bound to another receipt or cursor is refused. */
BUSTER_GLOBAL_LOCAL void bq_test_mcp_artifacts(void)
{
    char const* full = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    char const* receipt = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    BqMcpJson json;
    BqPacket request, response;
    BqMcpBuffer result = {0};
    u64 slice = 0;
    char text[512];
    snprintf(text, sizeof(text), "{\"job_id\":\"7\",\"attempt_token\":\"8\",\"full_result_sha256\":\"%s\"}", full);
    BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) &&
             bq_mcp_artifact_arguments(&json, 0, false, &request, &slice) && slice == UINT64_MAX &&
             request.size == BQ_CONTROL_HEADER + BQ_EXPORT_REQUEST_CAP && bq_u32(request.bytes + 8) == BQ_OP_EXPORT &&
             bq_u64(request.bytes + BQ_CONTROL_HEADER) == 7 && bq_u64(request.bytes + BQ_CONTROL_HEADER + 8) == 8 &&
             !memcmp(request.bytes + BQ_CONTROL_HEADER + 16, full, 64) &&
             bq_u64(request.bytes + BQ_CONTROL_HEADER + 80) == UINT64_MAX);
    /* A read needs the receipt digest and an offset; a receipt takes neither. */
    BQ_CHECK(!bq_mcp_artifact_arguments(&json, 0, true, &request, &slice));
    snprintf(text, sizeof(text), "{\"job_id\":\"7\",\"attempt_token\":\"8\",\"full_result_sha256\":\"%s\",\"path\":\"/etc/passwd\"}", full);
    BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) && !bq_mcp_artifact_arguments(&json, 0, false, &request, &slice));
    snprintf(text, sizeof(text), "{\"job_id\":\"0\",\"attempt_token\":\"8\",\"full_result_sha256\":\"%s\"}", full);
    BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) && !bq_mcp_artifact_arguments(&json, 0, false, &request, &slice));
    snprintf(text, sizeof(text), "{\"job_id\":\"7\",\"attempt_token\":\"8\",\"full_result_sha256\":\"%s\",\"receipt_sha256\":\"%s\",\"offset\":\"18446744073709551615\"}", full, receipt);
    BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) && !bq_mcp_artifact_arguments(&json, 0, true, &request, &slice));
    snprintf(text, sizeof(text), "{\"job_id\":\"7\",\"attempt_token\":\"8\",\"full_result_sha256\":\"%s\",\"receipt_sha256\":\"%s\",\"offset\":\"70000\"}", full, receipt);
    BQ_CHECK(bq_mcp_json_parse(&json, string_from_pointer(text)) &&
             bq_mcp_artifact_arguments(&json, 0, true, &request, &slice) && slice == 70000 &&
             bq_u64(request.bytes + BQ_CONTROL_HEADER + 80) == BQ_EXPORT_CHUNK_CAP &&
             !memcmp(request.bytes + BQ_CONTROL_HEADER + 88, receipt, 64));

    /* The final 6000-byte chunk of a 71536-byte archive. Its tail spells an
     * observation magic, which export replies must never be parsed for. */
    u64 total = BQ_EXPORT_CHUNK_CAP + BQ_TEST_ARTIFACT_COUNT;
    u8 body[BQ_EXPORT_REPLY_HEADER + BQ_TEST_ARTIFACT_COUNT] = {0};
    bq_put64(body + 4, 7);
    bq_put64(body + 12, 8);
    bq_put64(body + 20, BQ_EXPORT_CHUNK_CAP);
    bq_put64(body + 28, total);
    bq_put64(body + 36, total);
    bq_put32(body + 44, BQ_TEST_ARTIFACT_COUNT);
    memcpy(body + 48, receipt, 64);
    for (u32 i = 0; i < BQ_TEST_ARTIFACT_COUNT; i += 1) body[BQ_EXPORT_REPLY_HEADER + i] = (u8)i;
    memcpy(body + sizeof(body) - BQ_OBSERVATION_SIZE, "BQOBS001", 8);
    bq_packet(&response, BQ_OP_EXPORT | 0x80000000u, 1, body, sizeof(body));
    BQ_CHECK(!bq_observation(&response) && bq_public_response_valid(&request, &response));
    /* Offset 70000 is byte 4464 of the chunk: 1536 bytes remain, to the end. */
    BQ_CHECK(bq_mcp_artifact_result(&request, &response, slice, &result) &&
             bq_test_mcp_contains(&result, "\"job_id\":\"7\"") && bq_test_mcp_contains(&result, "\"attempt_token\":\"8\"") &&
             bq_test_mcp_contains(&result, "\"archive_bytes\":\"71536\"") &&
             bq_test_mcp_contains(&result, "\"offset\":\"70000\"") && bq_test_mcp_contains(&result, "\"bytes_hex\":\"70717273") &&
             bq_test_mcp_contains(&result, "\"next_offset\":\"71536\"") && bq_test_mcp_contains(&result, "\"eof\":true") &&
             bq_test_mcp_contains(&result, "\"sealed\":false") && !bq_test_mcp_contains(&result, "/"));
    /* A full slice is capped, and its duplicated reply still fits one frame. */
    result = (BqMcpBuffer){0};
    BQ_CHECK(bq_mcp_artifact_result(&request, &response, BQ_EXPORT_CHUNK_CAP, &result) &&
             bq_test_mcp_contains(&result, "\"bytes_hex\":\"00010203") &&
             bq_test_mcp_contains(&result, "\"next_offset\":\"68608\"") && bq_test_mcp_contains(&result, "\"eof\":false") &&
             result.count >= 2u * BQ_MCP_ARTIFACT_SLICE && result.count * 2u + 1024u < BQ_MCP_OUTPUT_CAP);
    /* Offsets outside the served chunk, and a receipt request, do not match. */
    result = (BqMcpBuffer){0};
    BQ_CHECK(!bq_mcp_artifact_result(&request, &response, total, &result));
    result = (BqMcpBuffer){0};
    BQ_CHECK(!bq_mcp_artifact_result(&request, &response, BQ_EXPORT_CHUNK_CAP - 1u, &result));
    result = (BqMcpBuffer){0};
    BQ_CHECK(!bq_mcp_artifact_result(&request, &response, UINT64_MAX, &result));
    /* A chunk bound to a different receipt is refused before rendering. */
    body[48] = 'd';
    bq_packet(&response, BQ_OP_EXPORT | 0x80000000u, 1, body, sizeof(body));
    result = (BqMcpBuffer){0};
    BQ_CHECK(!bq_mcp_artifact_result(&request, &response, slice, &result));
    BQ_CHECK(bq_mcp_tool(S8("bench_artifact_receipt")) == BQ_MCP_ARTIFACT_RECEIPT &&
             bq_mcp_tool(S8("bench_artifact_read")) == BQ_MCP_ARTIFACT_READ &&
             bq_mcp_capability_contains(S8("admission=idle-only-atomic export=1 transport=x\n"), S8(" export=1")) &&
             !bq_mcp_capability_contains(S8("admission=idle-only-atomic transport=x\n"), S8(" export=1")));
}

BUSTER_GLOBAL_LOCAL void bq_test_mcp_receipts(void)
{
    u8 arguments[8] = {0}, body[BQ_CONTROL_BODY] = {0};
    bq_put64(arguments, 7);
    bq_put64(body + 4, 7);
    bq_put64(body + 12, 8);
    bq_put32(body + 28, BQ_FINISHED);
    bq_put32(body + 32, BQ_SUCCEEDED);
    memset(body + 56, 'a', 64);
    char const* private_path = "/private/service/job-7";
    bq_put32(body + 124, (u32)strlen(private_path));
    memcpy(body + 128, private_path, strlen(private_path));
    memset(body + 320, 'b', 64);
    memset(body + 384, 'c', 64);
    memset(body + 448, 'd', 64);
    BqPacket request, response;
    bq_packet(&request, BQ_OP_RESULT, 41, arguments, sizeof(arguments));
    bq_packet(&response, BQ_OP_RESULT | 0x80000000u, 41, body, sizeof(body));
    BqMcpBuffer result = {0};
    BQ_CHECK(bq_mcp_service_result(&request, &response, &result) && !bq_test_mcp_contains(&result, private_path) &&
             !bq_test_mcp_contains(&result, "result-root") && bq_test_mcp_contains(&result, "\"result_bound\":true") &&
             bq_test_mcp_contains(&result, "full_result_sha256"));
    u32 offsets[] = {0, 4, 8, 12, 16, BQ_CONTROL_HEADER + 4, BQ_CONTROL_HEADER + 56,
                     BQ_CONTROL_HEADER + 320, BQ_CONTROL_HEADER + 384, BQ_CONTROL_HEADER + 448};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(offsets); i += 1)
    {
        BqPacket tampered = response;
        tampered.bytes[offsets[i]] = 0xff;
        result = (BqMcpBuffer){0};
        BQ_CHECK(!bq_mcp_service_result(&request, &tampered, &result) && !result.count);
    }
    bq_packet(&response, BQ_OP_RESULT | 0x80000000u, 41, body, 124);
    result = (BqMcpBuffer){0};
    BQ_CHECK(bq_mcp_service_result(&request, &response, &result) && bq_test_mcp_contains(&result, "\"result_bound\":false") &&
             !bq_test_mcp_contains(&result, "full_result_sha256"));
    bq_put32(body + 40, 1);
    bq_put32(body + 28, BQ_MEASURING);
    bq_put32(body + 32, BQ_NO_OUTCOME);
    bq_packet(&request, BQ_OP_CANCEL, 41, arguments, sizeof(arguments));
    bq_packet(&response, BQ_OP_CANCEL | 0x80000000u, 41, body, 124);
    result = (BqMcpBuffer){0};
    BQ_CHECK(bq_mcp_service_result(&request, &response, &result) && bq_test_mcp_contains(&result, "\"cancel_requested\":true") &&
             bq_test_mcp_contains(&result, "\"phase\":\"measuring\"") && !bq_test_mcp_contains(&result, "cancelled"));
}

#ifdef __linux__
BUSTER_GLOBAL_LOCAL void bq_test_mcp_call(BqMcpSession* session, char const* socket_path, char const* tool, u64 id, BqMcpBuffer* output)
{
    char message[512];
    snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":{\"job_id\":\"%" PRIu64 "\"}}}", tool, (uint64_t)id);
    bq_mcp_message(session, socket_path, string_from_pointer(message), output);
}

BUSTER_GLOBAL_LOCAL void bq_test_mcp_program_socket(char const* socket_path)
{
    u8 bytes[256];
    bq_test_native_program(bytes);
    char hash[65], identity[65], manifest[BQ_NATIVE_MANIFEST_CAP], hex[513];
    bq_native_hash(bytes, sizeof(bytes), hash);
    BQ_CHECK(bq_native_manifest(manifest, (u8 const*)hash, sizeof(bytes), identity) > 0);
    char const digits[] = "0123456789abcdef";
    for (u32 i = 0; i < sizeof(bytes); i += 1)
    {
        hex[i * 2] = digits[bytes[i] >> 4];
        hex[i * 2 + 1] = digits[bytes[i] & 15];
    }
    hex[512] = 0;
    BqMcpSession session = {0};
    BqMcpBuffer output;
    BqMcpJson json;
    u32 result = BQ_MCP_NONE;
    char message[2048];
    bq_test_mcp_ready(&session, socket_path);
    snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_program_begin\",\"arguments\":{\"program_sha256\":\"%s\",\"program_size\":\"256\"}}}", hash);
    bq_mcp_message(&session, socket_path, string_from_pointer(message), &output);
    BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"cursor\":\"0\"") &&
             bq_test_mcp_contains(&output, "\"committed\":false"));
    snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_program_write\",\"arguments\":{\"program_sha256\":\"%s\",\"program_size\":\"256\",\"offset\":\"0\",\"bytes_hex\":\"%.256s\"}}}", hash, hex);
    for (u32 retry = 0; retry < 2; retry += 1)
    {
        bq_mcp_message(&session, socket_path, string_from_pointer(message), &output);
        BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"cursor\":\"128\"") &&
                 bq_test_mcp_contains(&output, "\"committed\":false"));
    }
    char* first = strstr(message, "\"bytes_hex\":\"");
    BQ_CHECK(first != NULL);
    if (first) first[13] = first[13] == '0' ? '1' : '0';
    bq_mcp_message(&session, socket_path, string_from_pointer(message), &output);
    BQ_CHECK(bq_test_mcp_contains(&output, "\"isError\":true") && bq_test_mcp_contains(&output, "conflicting-key"));
    session = (BqMcpSession){0};
    bq_test_mcp_ready(&session, socket_path);
    snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"id\":22,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_program_begin\",\"arguments\":{\"program_sha256\":\"%s\",\"program_size\":\"256\"}}}", hash);
    bq_mcp_message(&session, socket_path, string_from_pointer(message), &output);
    BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"cursor\":\"128\""));
    snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"id\":23,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_program_write\",\"arguments\":{\"program_sha256\":\"%s\",\"program_size\":\"256\",\"offset\":\"128\",\"bytes_hex\":\"%s\"}}}", hash, hex + 256);
    bq_mcp_message(&session, socket_path, string_from_pointer(message), &output);
    BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"cursor\":\"256\"") &&
             bq_test_mcp_contains(&output, "\"committed\":false"));
    snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"id\":24,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_program_finish\",\"arguments\":{\"program_sha256\":\"%s\",\"program_size\":\"256\"}}}", hash);
    for (u32 retry = 0; retry < 2; retry += 1)
    {
        bq_mcp_message(&session, socket_path, string_from_pointer(message), &output);
        String8 actual = {0};
        BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"committed\":true") &&
                 bq_mcp_text(&json, bq_mcp_member(&json, result, S8("program_manifest_sha256")), &actual) &&
                 string_equal(actual, string_from_pointer(identity)));
    }
    snprintf(message, sizeof(message), "{\"jsonrpc\":\"2.0\",\"id\":25,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_submit\",\"arguments\":{\"idempotency_key\":\"mcp-native-program\",\"recipe\":\"native-execute-v1\",\"baseline_sha\":\"%s\",\"candidate_sha\":\"%s\"}}}", identity, identity);
    u64 id = 0, repeated = 0;
    bq_mcp_message(&session, socket_path, string_from_pointer(message), &output);
    BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_mcp_decimal(&json, bq_mcp_member(&json, result, S8("job_id")), true, &id));
    bq_mcp_message(&session, socket_path, string_from_pointer(message), &output);
    BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_mcp_decimal(&json, bq_mcp_member(&json, result, S8("job_id")), true, &repeated) && repeated == id);
    bq_test_mcp_call(&session, socket_path, "bench_cancel", id, &output);
    BQ_CHECK(bq_test_mcp_contains(&output, "\"execution_outcome\":\"cancelled\""));
}

BUSTER_GLOBAL_LOCAL void bq_test_mcp_socket(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        String8 fields[] = {S8("foreign-principal"), S8("foreign-key"), S8("validate-buster-v1"),
                           S8("1111111111111111111111111111111111111111"), S8("2222222222222222222222222222222222222222")};
        BqRequest submission;
        u64 foreign = 0;
        BQ_CHECK(bq_request_make(fields, &submission) == BQ_OK && bq_submit(&fixture.queue, &submission, &foreign) == BQ_OK &&
                 bq_cancel(&fixture.queue, foreign) == BQ_OK);
        bq_close(&fixture.queue);
        char socket_path[BQ_PATH_CAP + 1];
        snprintf(socket_path, sizeof(socket_path), "%s/mcp.sock", fixture.path);
        pid_t child = fork();
        BQ_CHECK(child >= 0);
        if (child == 0) _exit(bq_transport_serve(fixture.path, socket_path, NULL) == BQ_OK ? 0 : 1);
        if (child > 0)
        {
            bool ready = false;
            struct stat info;
            for (u32 i = 0; i < 200 && !ready; i += 1)
            {
                ready = lstat(socket_path, &info) == 0 && S_ISSOCK(info.st_mode);
                if (!ready) usleep(10000);
            }
            BQ_CHECK(ready);
            if (ready)
            {
                BqMcpSession session = {0};
                BqMcpBuffer output;
                BqMcpJson json;
                u32 result;
                u64 id = 0, repeated = 0;
                bq_test_mcp_ready(&session, socket_path);
                bq_mcp_message(&session, socket_path, S8("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_capabilities\",\"arguments\":{}}}"), &output);
                BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"isError\":false") &&
                         bq_test_mcp_contains(&output, "\"off_host_cache\":false") && bq_test_mcp_contains(&output, "\"custom_workloads\":false") &&
                         bq_test_mcp_contains(&output, "\"native_program_upload\":true") &&
                         bq_test_mcp_contains(&output, "\"custom_runtime_benchmarks\":true") &&
                         bq_test_mcp_contains(&output, "\"compiler_benchmarks\":false"));
                char const* submit = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"bench_submit\",\"arguments\":{\"idempotency_key\":\"mcp-lost-reply\",\"recipe\":\"validate-buster-v1\",\"baseline_sha\":\"1111111111111111111111111111111111111111\",\"candidate_sha\":\"2222222222222222222222222222222222222222\"}}}";
                bq_mcp_message(&session, socket_path, string_from_pointer(submit), &output);
                BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"isError\":false") &&
                         bq_mcp_decimal(&json, bq_mcp_member(&json, result, S8("job_id")), true, &id));
                session = (BqMcpSession){0};
                bq_test_mcp_ready(&session, socket_path);
                bq_mcp_message(&session, socket_path, string_from_pointer(submit), &output);
                BQ_CHECK(bq_test_mcp_result(&output, &json, &result) &&
                         bq_mcp_decimal(&json, bq_mcp_member(&json, result, S8("job_id")), true, &repeated) && repeated == id);
                char changed[1024];
                snprintf(changed, sizeof(changed), "%s", submit);
                char* revision = strstr(changed, "2222222222");
                BQ_CHECK(revision != NULL);
                if (revision) *revision = '3';
                bq_mcp_message(&session, socket_path, string_from_pointer(changed), &output);
                BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"isError\":true") &&
                         bq_test_mcp_contains(&output, "conflicting-key"));
                bq_test_mcp_call(&session, socket_path, "bench_status", id, &output);
                BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"phase\":\"queued\"") &&
                         bq_test_mcp_contains(&output, "\"cancel_requested\":false"));
                bq_test_mcp_call(&session, socket_path, "bench_result", id, &output);
                BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"result_bound\":false") &&
                         !bq_test_mcp_contains(&output, "full_result_sha256"));
                bq_test_mcp_call(&session, socket_path, "bench_logs", id, &output);
                BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"cursor\":\"1\"") &&
                         bq_test_mcp_contains(&output, "\"next_cursor\":\"1\""));
                bq_test_mcp_call(&session, socket_path, "bench_status", foreign, &output);
                BQ_CHECK(bq_test_mcp_contains(&output, "not-found"));
                BqMcpBuffer foreign_reply = output;
                bq_test_mcp_call(&session, socket_path, "bench_status", UINT64_MAX, &output);
                BQ_CHECK(foreign_reply.count == output.count && !memcmp(foreign_reply.bytes, output.bytes, output.count));
                char no_id[1024];
                snprintf(no_id, sizeof(no_id), "%s", submit);
                char* request_id = strstr(no_id, "\"id\":3,");
                if (request_id) memmove(request_id, request_id + 7, strlen(request_id + 7) + 1);
                char* key = strstr(no_id, "mcp-lost-reply");
                if (key) memcpy(key, "mcp-noid-key--", 13);
                bq_mcp_message(&session, socket_path, string_from_pointer(no_id), &output);
                BQ_CHECK(!output.count);
                char cancelled_notification[512];
                snprintf(cancelled_notification, sizeof(cancelled_notification), "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"bench_cancel\",\"arguments\":{\"job_id\":\"%" PRIu64 "\"}}}", (uint64_t)id);
                bq_mcp_message(&session, socket_path, string_from_pointer(cancelled_notification), &output);
                BQ_CHECK(!output.count);
                bq_test_mcp_call(&session, socket_path, "bench_status", id, &output);
                BQ_CHECK(bq_test_mcp_contains(&output, "\"cancel_requested\":false"));
                bq_test_mcp_call(&session, socket_path, "bench_cancel", id, &output);
                BQ_CHECK(bq_test_mcp_result(&output, &json, &result) && bq_test_mcp_contains(&output, "\"cancel_requested\":true") &&
                         bq_test_mcp_contains(&output, "\"execution_outcome\":\"cancelled\""));
                bq_test_mcp_call(&session, socket_path, "bench_cancel", id, &output);
                BQ_CHECK(bq_test_mcp_contains(&output, "\"isError\":false"));
                bq_test_mcp_program_socket(socket_path);
            }
            BQ_CHECK(kill(child, SIGTERM) == 0);
            int status = 0;
            BQ_CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
            BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_OK && fixture.queue.state.job_count == 3);
            BqJob* own = fixture.queue.state.job_count == 3 ? fixture.queue.state.jobs + 1 : NULL;
            BQ_CHECK(own && own->phase == BQ_FINISHED && own->cancel_requested && string_equal(bq_field(&own->request, 0), S8(BQ_EXPORT_PRINCIPAL)));
            BqJob* program = fixture.queue.state.job_count == 3 ? fixture.queue.state.jobs + 2 : NULL;
            BQ_CHECK(program && bq_request_recipe(&program->request) == BQ_RECIPE_NATIVE_EXECUTE &&
                     program->phase == BQ_FINISHED && program->outcome == BQ_CANCELLED);
            BqMcpSession disconnected = {.phase = 2};
            BqMcpBuffer output;
            bq_test_mcp_call(&disconnected, socket_path, "bench_status", own ? own->id : 1, &output);
            BQ_CHECK(bq_test_mcp_contains(&output, "\"isError\":true") && bq_test_mcp_contains(&output, "io-uncertain"));
        }
        bq_test_end(&fixture);
    }
}
#endif
