/* Bounded stdio MCP adapter for the existing authenticated public socket.
 * bq_mcp_json_parse owns an explicit-stack JSON tree and decoded string storage;
 * bq_mcp_message owns lifecycle, schemas and translation to public BQP1 only;
 * bq_mcp_run owns newline framing and protocol-only stdout. No queue is opened,
 * no worker is launched and disconnect/request cancellation never cancels jobs.
 */
#define BQ_MCP_MESSAGE_CAP 16384u
#define BQ_MCP_TOKEN_CAP 512u
#define BQ_MCP_DEPTH_CAP 32u
#define BQ_MCP_OUTPUT_CAP 16384u
#define BQ_MCP_NONE UINT32_MAX
#define BQ_MCP_VERSION "2025-11-25"

typedef enum BqMcpJsonKind
{
    BQ_MCP_OBJECT, BQ_MCP_ARRAY, BQ_MCP_STRING, BQ_MCP_NUMBER, BQ_MCP_LITERAL
} BqMcpJsonKind;

typedef struct BqMcpJsonToken
{
    BqMcpJsonKind kind;
    String8 text;
    u32 end;
} BqMcpJsonToken;

typedef struct BqMcpJson
{
    BqMcpJsonToken tokens[BQ_MCP_TOKEN_CAP];
    char8 strings[BQ_MCP_MESSAGE_CAP];
    u32 count, used;
} BqMcpJson;

typedef struct BqMcpFrame
{
    u32 index, state;
} BqMcpFrame;

typedef struct BqMcpBuffer
{
    char bytes[BQ_MCP_OUTPUT_CAP];
    u32 count;
    bool failed;
} BqMcpBuffer;

typedef struct BqMcpSession
{
    u32 phase;
} BqMcpSession;

BUSTER_GLOBAL_LOCAL void bq_mcp_append(BqMcpBuffer* buffer, String8 text)
{
    if (!buffer->failed && text.length <= sizeof(buffer->bytes) - buffer->count)
    {
        if (text.length) memcpy(buffer->bytes + buffer->count, text.pointer, (size_t)text.length);
        buffer->count += (u32)text.length;
    }
    else buffer->failed = true;
}

BUSTER_GLOBAL_LOCAL void bq_mcp_string(BqMcpBuffer* buffer, String8 text)
{
    char const hex[] = "0123456789abcdef";
    bq_mcp_append(buffer, S8("\""));
    for (u64 i = 0; i < text.length && !buffer->failed; i += 1)
    {
        u8 c = (u8)text.pointer[i];
        if (c == '"' || c == '\\')
        {
            char escaped[2] = {'\\', (char)c};
            bq_mcp_append(buffer, (String8){(char8*)escaped, 2});
        }
        else if (c < 0x20)
        {
            char escaped[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            bq_mcp_append(buffer, (String8){(char8*)escaped, 6});
        }
        else bq_mcp_append(buffer, (String8){text.pointer + i, 1});
    }
    bq_mcp_append(buffer, S8("\""));
}

BUSTER_GLOBAL_LOCAL void bq_mcp_u64(BqMcpBuffer* buffer, u64 value)
{
    char text[32];
    int length = snprintf(text, sizeof(text), "\"%" PRIu64 "\"", (uint64_t)value);
    if (length > 0 && (u32)length < sizeof(text)) bq_mcp_append(buffer, (String8){(char8*)text, (u32)length});
    else buffer->failed = true;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_hex4(String8 input, u32* cursor, u32* value)
{
    bool ok = input.length - *cursor >= 4;
    *value = 0;
    for (u32 i = 0; ok && i < 4; i += 1)
    {
        u8 c = (u8)input.pointer[(*cursor)++];
        u32 digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10u :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10u : 16u;
        ok = digit < 16;
        *value = (*value << 4) | digit;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_codepoint(String8 input, u32* cursor, u32* codepoint)
{
    u8 c = (u8)input.pointer[(*cursor)++];
    u32 count = c < 0x80 ? 0u : c >= 0xc2 && c <= 0xdf ? 1u : c >= 0xe0 && c <= 0xef ? 2u :
                c >= 0xf0 && c <= 0xf4 ? 3u : 4u;
    u32 minimum = count == 1 ? 0x80u : count == 2 ? 0x800u : count == 3 ? 0x10000u : 0u;
    bool ok = count < 4 && input.length - *cursor >= count;
    *codepoint = count ? c & ((1u << (6 - count)) - 1) : c;
    for (u32 i = 0; ok && i < count; i += 1)
    {
        u8 next = (u8)input.pointer[(*cursor)++];
        ok = next >= 0x80 && next <= 0xbf;
        *codepoint = (*codepoint << 6) | (next & 63u);
    }
    ok = ok && *codepoint >= minimum && *codepoint <= 0x10ffff &&
         (*codepoint < 0xd800 || *codepoint > 0xdfff);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_parse_string(BqMcpJson* json, String8 input, u32* cursor, String8* text)
{
    u32 start = json->used;
    bool ok = *cursor < input.length && input.pointer[*cursor] == '"';
    bool ended = false;
    if (ok) *cursor += 1;
    while (ok && !ended && *cursor < input.length)
    {
        u32 codepoint = 0;
        u8 c = (u8)input.pointer[*cursor];
        if (c == '"')
        {
            *cursor += 1;
            ended = true;
        }
        else
        {
            if (c == '\\')
            {
                *cursor += 1;
                ok = *cursor < input.length;
                if (ok)
                {
                    c = (u8)input.pointer[(*cursor)++];
                    if (c == 'u')
                    {
                        ok = bq_mcp_hex4(input, cursor, &codepoint);
                        if (ok && codepoint >= 0xd800 && codepoint <= 0xdbff)
                        {
                            u32 low = 0;
                            ok = input.length - *cursor >= 6 && input.pointer[*cursor] == '\\' && input.pointer[*cursor + 1] == 'u';
                            if (ok) *cursor += 2;
                            if (ok) ok = bq_mcp_hex4(input, cursor, &low) && low >= 0xdc00 && low <= 0xdfff;
                            if (ok) codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) + low - 0xdc00u;
                        }
                        else if (ok && codepoint >= 0xdc00 && codepoint <= 0xdfff) ok = false;
                    }
                    else
                    {
                        ok = c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't';
                        codepoint = c == 'b' ? 8u : c == 'f' ? 12u : c == 'n' ? 10u : c == 'r' ? 13u : c == 't' ? 9u : c;
                    }
                }
            }
            else ok = c >= 0x20 && bq_mcp_codepoint(input, cursor, &codepoint);
            u32 count = codepoint < 0x80 ? 1u : codepoint < 0x800 ? 2u : codepoint < 0x10000 ? 3u : 4u;
            ok = ok && count <= sizeof(json->strings) - json->used;
            if (ok)
            {
                char8* output = json->strings + json->used;
                if (count == 1) output[0] = (char8)codepoint;
                else
                {
                    for (u32 i = count - 1; i > 0; i -= 1)
                    {
                        output[i] = (char8)(0x80u | (codepoint & 63u));
                        codepoint >>= 6;
                    }
                    output[0] = (char8)((count == 2 ? 0xc0u : count == 3 ? 0xe0u : 0xf0u) | codepoint);
                }
                json->used += count;
            }
        }
    }
    *text = (String8){json->strings + start, json->used - start};
    ok = ok && ended;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_number(String8 input, u32* cursor)
{
    if (input.pointer[*cursor] == '-') *cursor += 1;
    bool ok = *cursor < input.length;
    if (ok && input.pointer[*cursor] == '0') *cursor += 1;
    else
    {
        ok = ok && input.pointer[*cursor] >= '1' && input.pointer[*cursor] <= '9';
        while (ok && *cursor < input.length && input.pointer[*cursor] >= '0' && input.pointer[*cursor] <= '9') *cursor += 1;
    }
    if (ok && *cursor < input.length && input.pointer[*cursor] == '.')
    {
        *cursor += 1;
        u32 start = *cursor;
        while (*cursor < input.length && input.pointer[*cursor] >= '0' && input.pointer[*cursor] <= '9') *cursor += 1;
        ok = *cursor > start;
    }
    if (ok && *cursor < input.length && (input.pointer[*cursor] == 'e' || input.pointer[*cursor] == 'E'))
    {
        *cursor += 1;
        if (*cursor < input.length && (input.pointer[*cursor] == '-' || input.pointer[*cursor] == '+')) *cursor += 1;
        u32 start = *cursor;
        while (*cursor < input.length && input.pointer[*cursor] >= '0' && input.pointer[*cursor] <= '9') *cursor += 1;
        ok = *cursor > start;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_json_parse(BqMcpJson* json, String8 input)
{
    *json = (BqMcpJson){0};
    BqMcpFrame stack[BQ_MCP_DEPTH_CAP];
    u32 depth = 0, cursor = 0;
    bool ok = input.length && input.length <= BQ_MCP_MESSAGE_CAP, complete = false;
    while (ok && !complete)
    {
        while (cursor < input.length && (input.pointer[cursor] == ' ' || input.pointer[cursor] == '\t' ||
               input.pointer[cursor] == '\r' || input.pointer[cursor] == '\n')) cursor += 1;
        ok = cursor < input.length;
        u32 state = depth ? stack[depth - 1].state : 2u;
        u8 c = ok ? (u8)input.pointer[cursor] : 0;
        if (ok && (state == 0 || state == 4) && c == '"')
        {
            ok = json->count < BQ_MCP_TOKEN_CAP;
            if (ok)
            {
                BqMcpJsonToken* token = json->tokens + json->count;
                token->kind = BQ_MCP_STRING;
                ok = bq_mcp_parse_string(json, input, &cursor, &token->text);
                for (u32 i = stack[depth - 1].index + 1; ok && i < json->count; i = json->tokens[i + 1].end)
                    ok = !string_equal(json->tokens[i].text, token->text);
                token->end = ++json->count;
                stack[depth - 1].state = 1;
            }
        }
        else if (ok && state == 1)
        {
            ok = c == ':';
            cursor += 1;
            stack[depth - 1].state = 2;
        }
        else if (ok && (((state == 0 || state == 3) && c == '}') || ((state == 5 || state == 6) && c == ']')))
        {
            json->tokens[stack[depth - 1].index].end = json->count;
            depth -= 1;
            cursor += 1;
            complete = depth == 0;
        }
        else if (ok && (state == 3 || state == 6))
        {
            ok = c == ',';
            cursor += 1;
            stack[depth - 1].state = state == 3 ? 4u : 7u;
        }
        else if (ok && (state == 2 || state == 5 || state == 7))
        {
            ok = json->count < BQ_MCP_TOKEN_CAP;
            if (ok)
            {
                u32 index = json->count++;
                BqMcpJsonToken* token = json->tokens + index;
                token->end = json->count;
                if (depth) stack[depth - 1].state = state == 2 ? 3u : 6u;
                if (c == '{' || c == '[')
                {
                    ok = depth < BQ_MCP_DEPTH_CAP;
                    token->kind = c == '{' ? BQ_MCP_OBJECT : BQ_MCP_ARRAY;
                    if (ok) stack[depth++] = (BqMcpFrame){index, c == '{' ? 0u : 5u};
                    cursor += 1;
                }
                else if (c == '"')
                {
                    token->kind = BQ_MCP_STRING;
                    ok = bq_mcp_parse_string(json, input, &cursor, &token->text);
                }
                else if (c == '-' || (c >= '0' && c <= '9'))
                {
                    token->kind = BQ_MCP_NUMBER;
                    u32 start = cursor;
                    ok = bq_mcp_number(input, &cursor);
                    token->text = (String8){input.pointer + start, cursor - start};
                }
                else
                {
                    token->kind = BQ_MCP_LITERAL;
                    u32 count = c == 'n' || c == 't' ? 4u : c == 'f' ? 5u : 0u;
                    ok = count && input.length - cursor >= count;
                    if (ok)
                    {
                        token->text = (String8){input.pointer + cursor, count};
                        ok = string_equal(token->text, S8("null")) || string_equal(token->text, S8("true")) ||
                             string_equal(token->text, S8("false"));
                        cursor += count;
                    }
                }
                complete = depth == 0;
            }
        }
        else ok = false;
    }
    while (ok && cursor < input.length && (input.pointer[cursor] == ' ' || input.pointer[cursor] == '\t' ||
           input.pointer[cursor] == '\r' || input.pointer[cursor] == '\n')) cursor += 1;
    ok = ok && complete && cursor == input.length;
    return ok;
}

BUSTER_GLOBAL_LOCAL u32 bq_mcp_member(BqMcpJson const* json, u32 object, String8 key)
{
    u32 result = BQ_MCP_NONE;
    if (object < json->count && json->tokens[object].kind == BQ_MCP_OBJECT)
    {
        for (u32 i = object + 1; i < json->tokens[object].end; i = json->tokens[i + 1].end)
            if (string_equal(json->tokens[i].text, key)) result = i + 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_members(BqMcpJson const* json, u32 object, String8 const* names, u32 count)
{
    bool ok = object < json->count && json->tokens[object].kind == BQ_MCP_OBJECT;
    for (u32 i = object + 1; ok && i < json->tokens[object].end; i = json->tokens[i + 1].end)
    {
        bool found = false;
        for (u32 j = 0; j < count; j += 1) found = found || string_equal(json->tokens[i].text, names[j]);
        ok = found;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_text(BqMcpJson const* json, u32 index, String8* text)
{
    bool ok = index < json->count && json->tokens[index].kind == BQ_MCP_STRING;
    *text = ok ? json->tokens[index].text : (String8){0};
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_decimal(BqMcpJson const* json, u32 index, bool nonzero, u64* value)
{
    String8 text;
    bool ok = bq_mcp_text(json, index, &text) && text.length > 0 && text.length <= 20 && (text.length == 1 || text.pointer[0] != '0');
    IntegerParsingU64 parsed = string8_parse_u64_decimal(text);
    ok = ok && parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == text.length && (!nonzero || parsed.value != 0);
    *value = ok ? parsed.value : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL u32 bq_mcp_tool(String8 name)
{
    char const* names[] = {"bench_capabilities", "bench_submit", "bench_status", "bench_result", "bench_cancel", "bench_logs"};
    u32 result = 0;
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        if (string_equal(name, string_from_pointer(names[i]))) result = i + 1;
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_arguments(BqMcpJson const* json, u32 arguments, u32 operation, BqPacket* packet)
{
    String8 const names[] = {S8("idempotency_key"), S8("recipe"), S8("baseline_sha"), S8("candidate_sha")};
    String8 const job_names[] = {S8("job_id"), S8("cursor")};
    u8 body[BQ_CONTROL_BODY] = {0};
    u32 size = 0;
    bool ok = operation >= BQ_OP_CAPABILITIES && operation <= BQ_OP_LOGS &&
              ((operation == BQ_OP_CAPABILITIES && arguments == BQ_MCP_NONE) ||
               bq_mcp_members(json, arguments, operation == BQ_OP_SUBMIT ? names : job_names,
                             operation == BQ_OP_SUBMIT ? 4u : operation == BQ_OP_CAPABILITIES ? 0u : operation == BQ_OP_LOGS ? 2u : 1u));
    if (ok && operation == BQ_OP_SUBMIT)
    {
        String8 fields[BQ_FIELD_COUNT] = {S8(BQ_EXPORT_PRINCIPAL)};
        for (u32 i = 0; ok && i < 4; i += 1) ok = bq_mcp_text(json, bq_mcp_member(json, arguments, names[i]), fields + i + 1);
        BqRequest request;
        ok = ok && bq_request_make(fields, &request) == BQ_OK && bq_recipe_service(bq_request_recipe(&request));
        if (ok)
        {
            size = request.size;
            memcpy(body, request.bytes, size);
        }
    }
    else if (ok && operation != BQ_OP_CAPABILITIES)
    {
        u64 id = 0, cursor = 0;
        ok = bq_mcp_decimal(json, bq_mcp_member(json, arguments, S8("job_id")), true, &id);
        if (ok && operation == BQ_OP_LOGS)
        {
            u32 value = bq_mcp_member(json, arguments, S8("cursor"));
            ok = value == BQ_MCP_NONE || bq_mcp_decimal(json, value, false, &cursor);
        }
        bq_put64(body, id);
        bq_put64(body + 8, cursor);
        size = operation == BQ_OP_LOGS ? 16u : 8u;
    }
    *packet = (BqPacket){0};
    if (ok) bq_packet(packet, operation, 1, body, size);
    return ok;
}

#define BQ_MCP_JOB_SCHEMA "{\"type\":\"string\",\"pattern\":\"^[1-9][0-9]*$\",\"maxLength\":20,\"description\":\"Decimal uint64 from 1 to 18446744073709551615.\"}"
#define BQ_MCP_CURSOR_SCHEMA "{\"type\":\"string\",\"pattern\":\"^(0|[1-9][0-9]*)$\",\"maxLength\":20,\"description\":\"Decimal uint64 from 0 to 18446744073709551615.\"}"
#define BQ_MCP_SOURCE_SCHEMA "{\"type\":\"string\",\"pattern\":\"^([0-9a-f]{40}|[0-9a-f]{64})$\"}"
BUSTER_GLOBAL_LOCAL char const bq_mcp_tools[] =
    "{\"tools\":["
    "{\"name\":\"bench_capabilities\",\"description\":\"Read current service capabilities and adapter limits.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false},\"annotations\":{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"bench_submit\",\"description\":\"Durably queue immutable installed revisions under a fixed recipe. Identical key retries return the same job; do not generate a new key after a lost reply.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"idempotency_key\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":64,\"pattern\":\"^[A-Za-z0-9._-]+$\"},"
    "\"recipe\":{\"type\":\"string\",\"enum\":[\"validate-buster-v1\",\"zen5-calibration-v1\"]},"
    "\"baseline_sha\":" BQ_MCP_SOURCE_SCHEMA ",\"candidate_sha\":" BQ_MCP_SOURCE_SCHEMA "},\"required\":[\"idempotency_key\",\"recipe\",\"baseline_sha\",\"candidate_sha\"],\"additionalProperties\":false},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"bench_status\",\"description\":\"Read a service job receipt; the synchronous backend can time out while execution owns the host.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"job_id\":" BQ_MCP_JOB_SCHEMA "},\"required\":[\"job_id\"],\"additionalProperties\":false},\"annotations\":{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"bench_result\",\"description\":\"Read this job's bound manifest/bundle/full-result digests, if present. This tool returns receipts, not downloaded artifact bytes.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"job_id\":" BQ_MCP_JOB_SCHEMA "},\"required\":[\"job_id\"],\"additionalProperties\":false},\"annotations\":{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"bench_cancel\",\"description\":\"Request durable job cancellation. cancel_requested does not prove execution stopped. A transport timeout requires retry; closing MCP does not cancel a job.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"job_id\":" BQ_MCP_JOB_SCHEMA "},\"required\":[\"job_id\"],\"additionalProperties\":false},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"bench_logs\",\"description\":\"Read at most four lifecycle events with a stable per-job ordinal cursor; these are not build stdout logs.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"job_id\":" BQ_MCP_JOB_SCHEMA ",\"cursor\":" BQ_MCP_CURSOR_SCHEMA "},\"required\":[\"job_id\"],\"additionalProperties\":false},\"annotations\":{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}}]}";

BUSTER_GLOBAL_LOCAL bool bq_mcp_service_result(BqPacket const* request, BqPacket const* response, BqMcpBuffer* result)
{
    bool ok = bq_public_response_valid(request, response);
    if (ok)
    {
        u32 operation = bq_u32(request->bytes + 8);
        u8 const* data = response->bytes + BQ_CONTROL_HEADER;
        if (operation == BQ_OP_CAPABILITIES)
        {
            bq_mcp_append(result, S8("{\"service_capabilities\":"));
            bq_mcp_string(result, (String8){(char8*)data + 4, response->size - BQ_CONTROL_HEADER - 4});
            bq_mcp_append(result, S8(",\"adapter\":{\"transport\":\"stdio\",\"backend\":\"unix-seqpacket\",\"principal\":\"github-actions\",\"off_host_cache\":false,\"synchronous_backend\":true,\"fixed_recipe_only\":true,\"custom_workloads\":false,\"arbitrary_native_execution\":false,\"artifact_download\":false}}"));
        }
        else if (operation == BQ_OP_LOGS)
        {
            bq_mcp_append(result, S8("{\"job_id\":"));
            bq_mcp_u64(result, bq_u64(request->bytes + BQ_CONTROL_HEADER));
            bq_mcp_append(result, S8(",\"events\":["));
            for (u32 i = 0; i < bq_u32(data + 4); i += 1)
            {
                u8 const* event = data + 20 + i * 32;
                if (i) bq_mcp_append(result, S8(","));
                bq_mcp_append(result, S8("{\"cursor\":"));
                bq_mcp_u64(result, bq_u64(event));
                char kind[32];
                int length = snprintf(kind, sizeof(kind), ",\"event\":%u,\"phase\":", bq_u32(event + 16));
                if (length > 0 && (u32)length < sizeof(kind)) bq_mcp_append(result, (String8){(char8*)kind, (u32)length});
                else result->failed = true;
                bq_mcp_string(result, string_from_pointer(bq_phase_name(bq_u32(event + 20))));
                bq_mcp_append(result, S8(",\"execution_outcome\":"));
                bq_mcp_string(result, string_from_pointer(bq_outcome_name(bq_u32(event + 24))));
                bq_mcp_append(result, S8(",\"measurement_validity\":\"not_evaluated\"}"));
            }
            bq_mcp_append(result, S8("],\"next_cursor\":"));
            bq_mcp_u64(result, bq_u64(data + 8));
            bq_mcp_append(result, bq_u32(data + 16) ? S8(",\"more\":true}") : S8(",\"more\":false}"));
        }
        else
        {
            bq_mcp_append(result, S8("{\"job_id\":"));
            bq_mcp_u64(result, bq_u64(data + 4));
            bq_mcp_append(result, S8(",\"attempt_token\":"));
            bq_mcp_u64(result, bq_u64(data + 12));
            bq_mcp_append(result, S8(",\"phase\":"));
            bq_mcp_string(result, string_from_pointer(bq_phase_name(bq_u32(data + 28))));
            bq_mcp_append(result, S8(",\"execution_outcome\":"));
            bq_mcp_string(result, string_from_pointer(bq_outcome_name(bq_u32(data + 32))));
            bq_mcp_append(result, S8(",\"measurement_validity\":\"not_evaluated\",\"statistical_decision\":\"not_evaluated\",\"cancel_requested\":"));
            bq_mcp_append(result, bq_u32(data + 40) ? S8("true") : S8("false"));
            bq_mcp_append(result, S8(",\"request_sha256\":"));
            bq_mcp_string(result, (String8){(char8*)data + 56, 64});
            bq_mcp_append(result, S8(",\"failure\":"));
            bq_mcp_string(result, string_from_pointer(bq_error_name((BqError)bq_u32(data + 120))));
            bool bound = response->size == BQ_CONTROL_CAP;
            bq_mcp_append(result, bound ? S8(",\"result_bound\":true,\"artifacts\":{") : S8(",\"result_bound\":false}"));
            if (bound)
            {
                char const* names[] = {"manifest_sha256", "bundle_sha256", "full_result_sha256"};
                for (u32 i = 0; i < 3; i += 1)
                {
                    if (i) bq_mcp_append(result, S8(","));
                    bq_mcp_string(result, string_from_pointer(names[i]));
                    bq_mcp_append(result, S8(":"));
                    bq_mcp_string(result, (String8){(char8*)data + 320 + i * 64, 64});
                }
                bq_mcp_append(result, S8("}}"));
            }
        }
        ok = !result->failed;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_mcp_id(BqMcpBuffer* output, BqMcpJson const* json, u32 id)
{
    bq_mcp_append(output, S8("{\"jsonrpc\":\"2.0\",\"id\":"));
    if (id == BQ_MCP_NONE) bq_mcp_append(output, S8("null"));
    else if (json->tokens[id].kind == BQ_MCP_STRING) bq_mcp_string(output, json->tokens[id].text);
    else bq_mcp_append(output, json->tokens[id].text);
}

BUSTER_GLOBAL_LOCAL void bq_mcp_rpc_error(BqMcpBuffer* output, BqMcpJson const* json, u32 id, int code, char const* message)
{
    bq_mcp_id(output, json, id);
    char text[48];
    int length = snprintf(text, sizeof(text), ",\"error\":{\"code\":%d,\"message\":", code);
    if (length > 0 && (u32)length < sizeof(text)) bq_mcp_append(output, (String8){(char8*)text, (u32)length});
    else output->failed = true;
    bq_mcp_string(output, string_from_pointer(message));
    bq_mcp_append(output, S8("}}\n"));
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_request_id(BqMcpJson const* json, u32 id)
{
    bool ok = id == BQ_MCP_NONE;
    if (!ok && id < json->count)
    {
        BqMcpJsonToken const* token = json->tokens + id;
        ok = token->kind == BQ_MCP_STRING && token->text.length <= 256;
        if (token->kind == BQ_MCP_NUMBER)
        {
            u32 start = token->text.length && token->text.pointer[0] == '-' ? 1u : 0u;
            String8 digits = {token->text.pointer + start, token->text.length - start};
            IntegerParsingU64 parsed = string8_parse_u64_decimal(digits);
            ok = parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == digits.length && parsed.value <= 9007199254740991ull;
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_meta_valid(BqMcpJson const* json, u32 params)
{
    u32 meta = bq_mcp_member(json, params, S8("_meta"));
    bool ok = meta == BQ_MCP_NONE || json->tokens[meta].kind == BQ_MCP_OBJECT;
    u32 progress = ok ? bq_mcp_member(json, meta, S8("progressToken")) : BQ_MCP_NONE;
    ok = ok && (progress == BQ_MCP_NONE || json->tokens[progress].kind == BQ_MCP_STRING || json->tokens[progress].kind == BQ_MCP_NUMBER);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_mcp_empty_params(BqMcpJson const* json, u32 params)
{
    String8 const names[] = {S8("_meta")};
    bool ok = params == BQ_MCP_NONE || (bq_mcp_members(json, params, names, BUSTER_ARRAY_LENGTH(names)) && bq_mcp_meta_valid(json, params));
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_mcp_message(BqMcpSession* session, char const* socket_path, String8 input, BqMcpBuffer* output)
{
    BqMcpJson json;
    *output = (BqMcpBuffer){0};
    bool parsed = bq_mcp_json_parse(&json, input);
    u32 id = parsed ? bq_mcp_member(&json, 0, S8("id")) : BQ_MCP_NONE;
    String8 version, method;
    String8 const envelope[] = {S8("jsonrpc"), S8("id"), S8("method"), S8("params")};
    bool envelope_valid = parsed && bq_mcp_members(&json, 0, envelope, BUSTER_ARRAY_LENGTH(envelope)) &&
        bq_mcp_text(&json, bq_mcp_member(&json, 0, S8("jsonrpc")), &version) && string_equal(version, S8("2.0")) &&
        bq_mcp_text(&json, bq_mcp_member(&json, 0, S8("method")), &method) && bq_mcp_request_id(&json, id);
    if (!parsed) bq_mcp_rpc_error(output, &json, BQ_MCP_NONE, -32700, "Parse error or message limit exceeded");
    else if (!envelope_valid) bq_mcp_rpc_error(output, &json, BQ_MCP_NONE, -32600, "Invalid request");
    else if (id == BQ_MCP_NONE)
    {
        /* Only an initialized notification advances the connection. In
         * particular, tools/call notifications never reach the service. */
        if (string_equal(method, S8("notifications/initialized")) && session->phase == 1 &&
            bq_mcp_empty_params(&json, bq_mcp_member(&json, 0, S8("params")))) session->phase = 2;
    }
    else if (string_equal(method, S8("ping")))
    {
        if (!bq_mcp_empty_params(&json, bq_mcp_member(&json, 0, S8("params"))))
            bq_mcp_rpc_error(output, &json, id, -32602, "Invalid ping parameters");
        else
        {
            bq_mcp_id(output, &json, id);
            bq_mcp_append(output, S8(",\"result\":{}}\n"));
        }
    }
    else if (string_equal(method, S8("initialize")))
    {
        u32 params = bq_mcp_member(&json, 0, S8("params"));
        u32 info = bq_mcp_member(&json, params, S8("clientInfo"));
        u32 capabilities = bq_mcp_member(&json, params, S8("capabilities"));
        String8 requested, client_name, client_version;
        bool ok = !session->phase && bq_mcp_meta_valid(&json, params) && bq_mcp_text(&json, bq_mcp_member(&json, params, S8("protocolVersion")), &requested) &&
            capabilities < json.count && json.tokens[capabilities].kind == BQ_MCP_OBJECT &&
            bq_mcp_text(&json, bq_mcp_member(&json, info, S8("name")), &client_name) && client_name.length &&
            bq_mcp_text(&json, bq_mcp_member(&json, info, S8("version")), &client_version) && client_version.length;
        if (!ok) bq_mcp_rpc_error(output, &json, id, -32602, "Invalid initialization or already initialized");
        else
        {
            bq_mcp_id(output, &json, id);
            bq_mcp_append(output, S8(",\"result\":{\"protocolVersion\":"));
            bq_mcp_string(output, string_equal(requested, S8("2025-06-18")) ? requested : S8(BQ_MCP_VERSION));
            bq_mcp_append(output, S8(",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"buster-bench-service\",\"version\":\"1\"},\"instructions\":\"This adapter requires the existing authenticated Unix socket. The synchronous service may time out while a job runs. No off-host cache, custom workloads, arbitrary native execution, or artifact byte download is implemented. EOF and MCP request cancellation do not cancel jobs; use bench_cancel.\"}}\n"));
            session->phase = 1;
        }
    }
    else if (session->phase != 2) bq_mcp_rpc_error(output, &json, id, -32002, "Initialization is incomplete");
    else if (string_equal(method, S8("tools/list")))
    {
        u32 params = bq_mcp_member(&json, 0, S8("params"));
        if (!bq_mcp_empty_params(&json, params))
            bq_mcp_rpc_error(output, &json, id, -32602, "No tool-list cursor is supported");
        else
        {
            bq_mcp_id(output, &json, id);
            bq_mcp_append(output, S8(",\"result\":"));
            bq_mcp_append(output, string_from_pointer(bq_mcp_tools));
            bq_mcp_append(output, S8("}\n"));
        }
    }
    else if (string_equal(method, S8("tools/call")))
    {
        u32 params = bq_mcp_member(&json, 0, S8("params"));
        String8 const names[] = {S8("name"), S8("arguments"), S8("_meta")};
        String8 name;
        BqPacket request, response = {0};
        bool ok = bq_mcp_members(&json, params, names, BUSTER_ARRAY_LENGTH(names)) && bq_mcp_meta_valid(&json, params) &&
                  bq_mcp_text(&json, bq_mcp_member(&json, params, S8("name")), &name);
        u32 operation = ok ? bq_mcp_tool(name) : 0;
        ok = ok && bq_mcp_arguments(&json, bq_mcp_member(&json, params, S8("arguments")), operation, &request);
        if (!ok) bq_mcp_rpc_error(output, &json, id, -32602, "Unknown tool or invalid arguments");
        else
        {
            BqMcpBuffer result = {0};
            BqError error = bq_transport_request(socket_path, &request, &response);
            if (error == BQ_OK && !bq_mcp_service_result(&request, &response, &result)) error = BQ_BAD_REQUEST;
            if (error != BQ_OK)
            {
                result = (BqMcpBuffer){0};
                bq_mcp_append(&result, S8("{\"error\":"));
                bq_mcp_string(&result, string_from_pointer(bq_error_name(error)));
                bq_mcp_append(&result, S8(",\"execution_state\":\"unknown\",\"retry\":\"Retry the same operation and idempotency key; a lost reply does not prove a write failed.\"}"));
            }
            bq_mcp_id(output, &json, id);
            bq_mcp_append(output, error == BQ_OK ? S8(",\"result\":{\"isError\":false,\"structuredContent\":") : S8(",\"result\":{\"isError\":true,\"structuredContent\":"));
            String8 contents = {(char8*)result.bytes, result.count};
            bq_mcp_append(output, contents);
            bq_mcp_append(output, S8(",\"content\":[{\"type\":\"text\",\"text\":"));
            bq_mcp_string(output, contents);
            bq_mcp_append(output, S8("}]}}\n"));
        }
    }
    else bq_mcp_rpc_error(output, &json, id, -32601, "Method not found");
}

BUSTER_GLOBAL_LOCAL BqError bq_mcp_run(char const* socket_path, FILE* input, FILE* output)
{
    BqMcpSession session = {0};
    BqError error = BQ_OK;
    char8 line[BQ_MCP_MESSAGE_CAP];
    u32 count = 0;
    bool done = false;
    while (error == BQ_OK && !done)
    {
        int c = fgetc(input);
        if (c == EOF)
        {
            done = true;
            if (ferror(input)) error = BQ_IO;
        }
        bool oversized = c != EOF && c != '\n' && count == sizeof(line);
        if (c == '\n' || (done && count && error == BQ_OK) || oversized)
        {
            BqMcpBuffer reply;
            String8 text = {line, oversized || done ? BQ_MCP_MESSAGE_CAP + 1u : count};
            bq_mcp_message(&session, socket_path, text, &reply);
            if (reply.failed || (reply.count && fwrite(reply.bytes, 1, reply.count, output) != reply.count) || fflush(output) != 0) error = BQ_IO;
            count = 0;
            if (oversized || done)
            {
                /* Do not drain an attacker-controlled unterminated line. */
                if (error == BQ_OK) error = BQ_BAD_REQUEST;
                done = true;
            }
        }
        else if (!done && error == BQ_OK) line[count++] = (char8)c;
    }
    return error;
}
