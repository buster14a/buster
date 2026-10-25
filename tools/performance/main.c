/* First-party performance resource tools: no added project dependencies.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core.c"
#include "json.c"
#include "schema.c"
#include "arrow.c"
#include "protobuf.c"
#include "dataset.c"

static unsigned int perf_checks;
static unsigned int perf_failures;

static void perf_expect(bool condition, const char *name)
{
    ++perf_checks;
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        ++perf_failures;
    }
}

static void perf_expect_schema(const char *schema_text, const char *data_text, bool accepted, const char *name)
{
    PerfJson schema = {0};
    PerfJson data = {0};
    bool parsed = perf_json_parse(&schema, perf_literal(schema_text)) && perf_json_parse(&data, perf_literal(data_text));
    bool result = parsed && perf_schema_validate(&schema, &data);
    perf_expect(parsed && result == accepted, name);
    perf_json_free(&schema);
    perf_json_free(&data);
}

static bool perf_self_test(void)
{
    const char *hash_inputs[] = {
        "",
        "abc",
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    };
    const char *hash_outputs[] = {
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
    };
    for (size_t i = 0; i < 3; ++i)
    {
        char digest[65];
        perf_expect(perf_sha256(perf_literal(hash_inputs[i]), digest) && strcmp(digest, hash_outputs[i]) == 0, "SHA-256 known digest");
        for (size_t split = 0; split <= strlen(hash_inputs[i]); ++split)
        {
            PerfSha256 hash = perf_sha256_start();
            PerfBytes first = {
                .data = (const unsigned char *)hash_inputs[i],
                .size = split
            };
            PerfBytes second = {
                .data = (const unsigned char *)hash_inputs[i] + split,
                .size = strlen(hash_inputs[i]) - split
            };
            bool valid = perf_sha256_add(&hash, first) && perf_sha256_add(&hash, second);
            perf_sha256_finish(&hash, digest);
            perf_expect(valid && strcmp(digest, hash_outputs[i]) == 0, "SHA-256 chunk boundaries");
        }
    }
    const char *valid[] = {
        "null",
        "true",
        "false",
        "[null,true,false,0,-0,1.25,1e+20,-1E-9]",
        "{\"a\":[],\"b\":{},\"c\":{\"d\":[1,2,3]}}",
        "{\"大\":\"\\uD83D\\uDE00\",\"zero\":\"\\u0000\"}",
        " \t\n {\"x\":18446744073709551615,\"y\":1.250000} \r\n"
    };
    const char *invalid[] = {
        "",
        " ",
        "nul",
        "null null",
        "truex",
        "[1,]",
        "{\"x\":1,}",
        "{x:1}",
        "{\"x\" 1}",
        "[1 2]",
        "[1",
        "{\"x\":1,\"x\":2}",
        "{\"x\":1,\"\\u0078\":2}",
        "01",
        "-01",
        "+1",
        "1.",
        "1e",
        "1e+",
        "NaN",
        "Infinity",
        "\"\\x12\"",
        "\"\\uD800\"",
        "\"\\uDC00\"",
        "\"\\uD800\\u0041\"",
        "\"\xc0\x80\"",
        "\"\xed\xa0\x80\"",
        "\"\xf4\x90\x80\x80\"",
        "\"line\nfeed\""
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
    {
        PerfJson json = {0};
        perf_expect(perf_json_parse(&json, perf_literal(valid[i])), "valid JSON");
        perf_json_free(&json);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        PerfJson json = {0};
        perf_expect(!perf_json_parse(&json, perf_literal(invalid[i])), "invalid JSON");
        perf_json_free(&json);
    }
    PerfJson json = {0};
    bool parsed = perf_json_parse(&json, perf_literal("{\"n\":18446744073709551615,\"d\":1.250000,\"a\":[],\"b\":[{},1]}"));
    perf_expect(parsed && perf_json_is(&json, perf_json_find(&json, 0, "n"), "18446744073709551615") && perf_json_is(&json, perf_json_find(&json, 0, "d"), "1.250000"), "numeric source precision retained");
    perf_json_free(&json);
    perf_expect_schema("{\"type\":\"integer\",\"maximum\":9007199254740991}", "9007199254740991", true, "safe numeric count");
    perf_expect_schema("{\"type\":\"integer\",\"maximum\":9007199254740991}", "9007199254740992", false, "unsafe numeric count");
    perf_expect_schema("{\"type\":\"integer\",\"maximum\":9007199254740991}", "9007199254740991.1", false, "fraction never rounds to an integer");
    perf_expect_schema("{\"type\":\"integer\",\"maximum\":9007199254740991}", "9007199254740991.0", true, "exact integral decimal");
    perf_expect_schema("{\"const\":1}", "1.00000000000000000000000001", false, "const never rounds");
    perf_expect_schema("{\"const\":1}", "1e0", true, "exact const exponent");
    perf_expect_schema("{\"type\":\"number\",\"minimum\":-32,\"maximum\":32}", "-32", true, "inclusive signed lower bound");
    perf_expect_schema("{\"type\":\"number\",\"minimum\":-32,\"maximum\":32}", "-32.000000000000000001", false, "exact signed lower bound");
    perf_expect_schema("{\"exclusiveMinimum\":0,\"exclusiveMaximum\":1}", "0.95", true, "coverage interval");
    perf_expect_schema("{\"exclusiveMinimum\":0,\"exclusiveMaximum\":1}", "1", false, "exclusive upper bound");
    perf_expect_schema("{\"exclusiveMinimum\":0,\"exclusiveMaximum\":1}", "-0.0000", false, "negative zero is zero");
    perf_expect_schema("{\"type\":\"object\",\"properties\":{\"a\":{\"type\":\"string\"}},\"required\":[\"a\"],\"additionalProperties\":false}", "{\"a\":\"x\"}", true, "closed object");
    perf_expect_schema("{\"type\":\"object\",\"properties\":{\"a\":{\"type\":\"string\"}},\"required\":[\"a\"],\"additionalProperties\":false}", "{\"a\":\"x\",\"b\":true}", false, "unknown closed field");
    perf_expect_schema("{\"oneOf\":[{\"required\":[\"path\"],\"not\":{\"required\":[\"url\"]}},{\"required\":[\"url\"],\"not\":{\"required\":[\"path\"]}}]}", "{\"path\":\"x\"}", true, "one resource location");
    perf_expect_schema("{\"oneOf\":[{\"required\":[\"path\"],\"not\":{\"required\":[\"url\"]}},{\"required\":[\"url\"],\"not\":{\"required\":[\"path\"]}}]}", "{\"path\":\"x\",\"url\":\"y\"}", false, "two resource locations");
    perf_expect_schema("{\"type\":\"array\",\"minItems\":2,\"maxItems\":2,\"items\":{\"type\":\"number\"}}", "[0.1,0.9]", true, "array cardinality/items");
    perf_expect_schema("{\"type\":\"array\",\"minItems\":2,\"maxItems\":2,\"items\":{\"type\":\"number\"}}", "[0.1,0.9,1]", false, "array maximum cardinality");
    perf_expect_schema("{\"type\":\"string\",\"minLength\":2}", "\"延迟\"", true, "Unicode codepoint length");
    perf_expect_schema("{\"type\":\"string\",\"minLength\":2}", "\"延\"", false, "Unicode byte length is not codepoint length");
    perf_expect(perf_timestamp(perf_literal("2024-02-29T23:59:59.123+02:00")), "valid leap date");
    perf_expect(!perf_timestamp(perf_literal("2026-02-29T23:59:59Z")), "invalid leap date");
    perf_expect(!perf_timestamp(perf_literal("2026-10-25T12:00:00+24:00")), "invalid timezone");
    perf_expect(perf_identifier(perf_literal("new.metric:1")) && !perf_identifier(perf_literal("id\n")), "exact identity framing");
    perf_expect(perf_resource_path(perf_literal("data/a.arrow")) && !perf_resource_path(perf_literal("data/../a.arrow")), "resource traversal");
    perf_expect(perf_https_url(perf_literal("https://example.com/samples.arrow")) && !perf_https_url(perf_literal("https://user:password@example.com/file")), "HTTPS authority has no credentials");
    uint64_t maximum = 0;
    perf_expect(perf_uint64(perf_literal("18446744073709551615"), &maximum) && maximum == UINT64_MAX && !perf_uint64(perf_literal("18446744073709551616"), &maximum), "uint64 bounds");
    printf("{\"first_party\":true,\"checks\":%u,\"failures\":%u}\n", perf_checks, perf_failures);
    return perf_failures == 0;
}

int main(int argc, char **argv)
{
    bool valid = false;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        valid = perf_self_test();
    }
    else if (argc >= 3 && strcmp(argv[1], "validate") == 0)
    {
        PerfDataset dataset = {0};
        const char *schema = "docs/performance/buster-manifest-v1.schema.json";
        valid = true;
        for (int i = 3; i < argc && valid; ++i)
        {
            if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc)
            {
                dataset.cache = argv[++i];
            }
            else if (strcmp(argv[i], "--evidence-root") == 0 && i + 1 < argc)
            {
                dataset.evidence_root = argv[++i];
            }
            else if (strcmp(argv[i], "--schema") == 0 && i + 1 < argc)
            {
                schema = argv[++i];
            }
            else
            {
                valid = false;
            }
        }
        valid = valid && perf_dataset_load(&dataset, argv[2], schema);
        if (valid)
        {
            printf("{\"validator\":\"buster-first-party-1\",\"valid\":true,\"first_party\":true,\"resources\":%zu,\"tables\":%zu,\"native_only_tables\":%zu,\"rows\":\"%llu\",\"typed_cells_compared\":\"%llu\"}\n", dataset.resource_count, dataset.table_count, dataset.native_only_tables, (unsigned long long)dataset.rows, (unsigned long long)dataset.cells_compared);
        }
        perf_dataset_free(&dataset);
    }
    else if (argc == 5 && strcmp(argv[1], "compare-jsonl") == 0)
    {
        PerfBuffer arrow = {0};
        PerfBuffer jsonl = {0};
        PerfBuffer sidecar = {0};
        PerfBuffer a = {0};
        PerfBuffer b = {0};
        PerfArrowInfo info = {0};
        PerfArrowInfo schema = {0};
        uint64_t rows = 0;
        valid = perf_read_file(argv[2], &arrow) && perf_read_file(argv[3], &jsonl) && perf_read_file(argv[4], &sidecar) && perf_arrow_read(perf_buffer_bytes(&arrow), &info, &a) && info.json_mapping && perf_arrow_inspect(perf_buffer_bytes(&sidecar), &schema) && schema.rows == 0 && perf_arrow_schemas_equal(&info.schema, &schema.schema) && perf_jsonl_read(perf_buffer_bytes(&jsonl), &info.schema, &b, &rows) && rows == info.rows && perf_bytes_equal(perf_buffer_bytes(&a), perf_buffer_bytes(&b));
        if (valid)
        {
            printf("{\"first_party\":true,\"rows\":\"%llu\",\"typed_cells_compared\":\"%llu\"}\n", (unsigned long long)rows, (unsigned long long)(rows * info.schema.roots));
        }
        else
        {
            fputs("Arrow/JSONL schema, mapping or exact values differ\n", stderr);
        }
        free(arrow.data);
        free(jsonl.data);
        free(sidecar.data);
        free(a.data);
        free(b.data);
        free(info.schema.fields);
        free(schema.schema.fields);
    }
    else if (argc == 3 && (strcmp(argv[1], "check-json") == 0 || strcmp(argv[1], "check-manifest") == 0 || strcmp(argv[1], "inspect-arrow") == 0 || strcmp(argv[1], "sha256") == 0))
    {
        PerfBuffer input = {0};
        valid = perf_read_file(argv[2], &input);
        if (valid && strcmp(argv[1], "sha256") == 0)
        {
            char digest[65];
            valid = perf_sha256(perf_buffer_bytes(&input), digest);
            if (valid)
            {
                printf("%s  %s\n", digest, argv[2]);
            }
        }
        else if (valid && strcmp(argv[1], "inspect-arrow") == 0)
        {
            PerfArrowInfo information = {0};
            valid = perf_arrow_inspect(perf_buffer_bytes(&input), &information);
            if (valid)
            {
                printf("{\"first_party\":true,\"rows\":\"%llu\",\"batches\":\"%llu\",\"root_fields\":%zu,\"all_fields\":%zu}\n", (unsigned long long)information.rows, (unsigned long long)information.batches, information.schema.roots, information.schema.count);
            }
            else
            {
                fputs("Unsupported or invalid Arrow IPC stream\n", stderr);
            }
            free(information.schema.fields);
        }
        else if (valid)
        {
            PerfJson document = {0};
            valid = perf_json_parse(&document, perf_buffer_bytes(&input));
            if (valid)
            {
                if (strcmp(argv[1], "check-manifest") == 0)
                {
                    PerfBuffer schema_bytes = {0};
                    PerfJson schema = {0};
                    valid = perf_read_file("docs/performance/buster-manifest-v1.schema.json", &schema_bytes) && perf_json_parse(&schema, perf_buffer_bytes(&schema_bytes)) && perf_schema_validate(&schema, &document);
                    free(schema_bytes.data);
                    perf_json_free(&schema);
                    if (valid)
                    {
                        puts("{\"structural_schema\":true,\"resources_verified\":false,\"first_party\":true}");
                    }
                    else
                    {
                        fputs("Manifest schema rejected\n", stderr);
                    }
                }
                else
                {
                    printf("{\"valid\":true,\"tokens\":%zu}\n", document.node_count);
                }
            }
            else
            {
                fprintf(stderr, "JSON rejected at byte %zu\n", document.error_offset);
            }
            perf_json_free(&document);
        }
        free(input.data);
    }
    else
    {
        fprintf(stderr, "usage: bmf --self-test | check-json FILE | check-manifest FILE | inspect-arrow FILE | sha256 FILE | compare-jsonl ARROW JSONL SCHEMA | validate MANIFEST [--schema FILE] [--cache DIR] [--evidence-root DIR]\n");
    }
    return valid ? 0 : 1;
}
