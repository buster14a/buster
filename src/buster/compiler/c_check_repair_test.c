// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original malformed-body diagnostics; no repaired input is accepted or sent to lowering. Include after the
// checker tests and c_check_repair.c. Every oracle reads the original source, never a synthesized replacement.

#ifndef BUSTER_CHECK_REPAIR_TEST_IMPLEMENTATION
#define BUSTER_CHECK_REPAIR_TEST_IMPLEMENTATION 1

typedef struct CheckRepairFixture CheckRepairFixture;
struct CheckRepairFixture
{
    const char* name;
    const char* source;
    uint32_t line;
    CheckRepairStatus status;
};

// Use the production renderer so macro-body, paste and forwarding diagnostics are measured at their actual
// primary location, rather than the spelling location of the expanded token.
static uint32_t check_test_repair_line(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    uint32_t result = 0;
    FILE* stream = tmpfile();
    if (stream != 0)
    {
        FILE* diagnostics = pp->options.diagnostics;
        const bool quiet = pp->options.quiet;
        const uint32_t errors = pp->errors;
        pp->options.diagnostics = stream;
        pp->options.quiet = false;
        pp_diagnose_output(pp, PP_SEVERITY_ERROR, parser->tokens.sources[token], "repair fixture");
        pp->options.diagnostics = diagnostics;
        pp->options.quiet = quiet;
        pp->errors = errors;
        rewind(stream);
        char line[512];
        if (fgets(line, sizeof(line), stream) != 0)
        {
            sscanf(line, "%*[^:]:%u", &result);
        }
        fclose(stream);
    }
    return result;
}

static void check_test_repair_faults(CheckTest* test, const Parser* parser, Preprocessor* pp)
{
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    const bool initialized = output.base != 0 && scratch.base != 0;
    check_test_expect(test, initialized, "repair fault storage initializes");
    if (initialized)
    {
        memset(output.base, 0xa5, output.capacity);
        memset(scratch.base, 0x5a, scratch.capacity);
        output.length = 33;
        scratch.length = 19;
        CheckRepair repair;
        const bool ready = check_repair_make(&repair, parser, pp, &output, &scratch) == CHECK_REPAIR_READY;
        check_test_expect(test, ready && scratch.length == 19, "repair initializes poisoned storage and restores scratch");
        if (ready)
        {
            const uint64_t used = output.length;
            const uint64_t capacity = output.capacity;
            for (uint64_t limit = 33; limit < used; limit += 97)
            {
                output.length = 33;
                output.capacity = limit;
                const CheckRepairStatus status = check_repair_make(&repair, parser, pp, &output, &scratch);
                check_test_expect(test, status == CHECK_REPAIR_RESOURCE && repair.original_end == 0 && repair.parser.tokens.count == 0 &&
                                      output.length == 33 && scratch.length == 19, "repair rolls back every exhausted output allocation");
            }
            output.capacity = capacity;
            output.length = 33;
            const uint64_t scratch_capacity = scratch.capacity;
            bool recovered = false;
            for (uint64_t limit = 19; limit < scratch_capacity && !recovered; limit += 67)
            {
                output.length = 33;
                scratch.capacity = limit;
                const CheckRepairStatus status = check_repair_make(&repair, parser, pp, &output, &scratch);
                recovered = status == CHECK_REPAIR_READY;
                check_test_expect(test, recovered ? repair.original_end != 0 && scratch.length == 19 :
                                      status == CHECK_REPAIR_RESOURCE && repair.original_end == 0 && output.length == 33 && scratch.length == 19,
                                  "repair rolls back exhausted delimiter/parser/order scratch");
            }
            check_test_expect(test, recovered, "repair recovers when its complete explicit stacks fit scratch");
            scratch.capacity = scratch_capacity;
            output.length = 33;
            check_test_expect(test, check_repair_make(&repair, parser, pp, &output, &output) == CHECK_REPAIR_INVALID && output.length == 33,
                              "repair refuses aliased arenas");
            check_test_expect(test, check_repair_make(&repair, parser, pp, &output, &scratch) == CHECK_REPAIR_READY && scratch.length == 19,
                              "repair remains usable after every resource failure");
        }
    }
    arena_release(&scratch);
    arena_release(&output);
}

static void check_test_repair_fixture(CheckTest* test, const CheckRepairFixture* fixture, const char* path, const ParseTestClang* oracle, bool faults)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser original;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool initialized = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                             pp_run_source(&pp, path, (const uint8_t*)fixture->source, (uint32_t)strlen(fixture->source));
    check_test_expect(test, initialized, "repair original source initializes");
    uint32_t lines[2] = { 0 };
    for (uint32_t path_index = 0; initialized && path_index < 2; path_index += 1)
    {
        Arena output = arena_reserve(UINT64_C(1) << 20);
        Arena input_storage = arena_reserve(UINT64_C(1) << 24);
        Arena values = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        CheckTestTypes types = { 0 };
        const ParseOptions parse_options = { .scalar = path_index == 0 };
        const bool ready = output.base != 0 && input_storage.base != 0 && values.base != 0 && scratch.base != 0 &&
                           check_test_types_open(&types) && parse_initialize(&original, &parse_options, options.language);
        check_test_expect(test, ready, "repair parser/checker storage initializes");
        if (ready)
        {
            const bool parsed = parse_translation_unit(&original, &pp);
            const ParseTestOutcome outcome = parse_test_outcome(&original, &pp);
            const uint32_t errors = original.errors;
            const uint32_t diagnostics = original.diagnostic_count;
            const uint32_t pp_count = pp.output.count;
            check_test_expect(test, !parsed && errors != 0 && original.fatal == PARSE_MESSAGE_NONE, "repair starts from an original rejected parse");
            CheckRepair repair;
            const CheckRepairStatus status = check_repair_make(&repair, &original, &pp, &output, &scratch);
            char description[320];
            snprintf(description, sizeof(description), "%s: repair status %u, expected %u", fixture->name, status, fixture->status);
            check_test_expect(test, status == fixture->status, description);
            lines[path_index] = outcome.line;
            if (status == CHECK_REPAIR_READY)
            {
                const Parser* view = &repair.parser;
                const uint32_t cut = repair.original_end;
                const uint32_t count = view->tokens.count;
                bool original_tokens = cut != 0 && cut < count && cut <= original.diagnostics[repair.diagnostic].token &&
                                       memcmp(view->tokens.kinds, original.tokens.kinds, cut) == 0 &&
                                       memcmp(view->tokens.names, original.tokens.names, (uint64_t)cut * sizeof(uint32_t)) == 0 &&
                                       memcmp(view->tokens.sources, original.tokens.sources, (uint64_t)cut * sizeof(uint32_t)) == 0 &&
                                       memcmp(view->tokens.flags, original.tokens.flags, cut) == 0;
                check_test_expect(test, original_tokens, "repair preserves every original token, name, source and flag before its cutoff");
                const Preprocessor* view_pp = repair.preprocessor;
                bool same_output = view_pp != 0 && view_pp->output.count >= pp_count &&
                                   memcmp(view_pp->output.kinds, pp.output.kinds, pp_count) == 0 &&
                                   memcmp(view_pp->output.flags, pp.output.flags, pp_count) == 0 &&
                                   memcmp(view_pp->output.values, pp.output.values, (uint64_t)pp_count * sizeof(uint32_t)) == 0 &&
                                   memcmp(view_pp->output.locations, pp.output.locations, (uint64_t)pp_count * sizeof(uint32_t)) == 0;
                if (view_pp != &pp)
                {
                    same_output &= view_pp->output.count == pp_count + 1 && view_pp->output.kinds[pp_count] == BUSTER_PP_KIND_INTEGER &&
                                   view_pp->output.values[pp_count] == 0 && view_pp->output.locations[pp_count] == 0 && view_pp->output.flags[pp_count] == 0;
                }
                check_test_expect(test, same_output, "repair PP view retains all original output and appends only canonical integer zero when needed");
                bool padded = true;
                for (uint32_t i = 0; i < BUSTER_PARSE_TOKEN_PADDING; i += 1)
                {
                    const int64_t offsets[2] = { -1 - (int64_t)i, (int64_t)count + i };
                    for (uint32_t side = 0; side < 2; side += 1)
                    {
                        const int64_t token = offsets[side];
                        padded &= view->tokens.kinds[token] == 0 && view->tokens.names[token] == 0 && view->tokens.sources[token] == 0 &&
                                  view->tokens.flags[token] == 0 && view->tree.roles[token] == 0 && view->tree.parents[token] == 0 &&
                                  view->tree.depths[token] == 0 && view->postorder[token] == 0 && view->sizes[token] == 0;
                    }
                }
                check_test_expect(test, padded, "repair retains padded parser and tree column sentinels");
                bool bounded = true;
                for (uint32_t token = 0; token < count; token += 1)
                {
                    const uint32_t parent = view->tree.parents[token];
                    bounded &= parent == BUSTER_PARSE_ROOT || (parent < count && parent != token);
                    bounded &= view->postorder[token] < count && view->sizes[token] != 0 && view->sizes[token] <= token + 1;
                    if (token >= cut)
                    {
                        const uint8_t kind = view->tokens.kinds[token];
                        bounded &= kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_L_PAREN ||
                                   kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_SEMI;
                        bounded &= !check_repair_warning_token(&repair, token);
                    }
                }
                check_test_expect(test, bounded, "repair synthetic suffix and complete tree have bounded roles and intervals");
                CheckInput input;
                const bool adapted = check_input_fragment(&input, view, repair.preprocessor, &types.table, &input_storage, &scratch, cut);
                check_test_expect(test, !input.resource, "repair fragment elaboration has sufficient storage");
                uint32_t first = BUSTER_CHECK_NONE;
                for (uint32_t i = 0; i < input.report_count; i += 1)
                {
                    const CheckInputReport* report = input.reports + i;
                    if (check_repair_input_report(&repair, report))
                    {
                        first = report->token < first ? report->token : first;
                    }
                }
                for (uint32_t token = 0; token < input.token_count; token += 1)
                {
                    uint32_t warnings = check_repair_warning_token(&repair, token) ? input.constant_warnings[token] : 0;
                    while (warnings != 0)
                    {
                        const uint32_t warning = warnings & (0u - warnings);
                        const bool ignored = input.constant_warning_ignored != 0 && (input.constant_warning_ignored[token] & warning) != 0;
                        if (check_diagnostic_severity_default(&input, token, warning, ignored) >= CHECK_WARNING_ERROR)
                        {
                            first = token < first ? token : first;
                        }
                        warnings &= warnings - 1;
                    }
                }
                if (adapted || input.diagnostic_graph)
                {
                    const CheckOptions check_options = { .scalar = path_index == 0 };
                    CheckStream stream;
                    const bool checked = check_graph_run(&types.table, &input.graph, &values, &scratch, &check_options, &stream);
                    check_test_expect(test, checked, "repair diagnostic graph is valid");
                    for (uint32_t i = 0; checked && i < stream.report_count; i += 1)
                    {
                        const CheckReport* report = stream.reports + i;
                        if (check_repair_graph_report(&repair, &input.graph, report))
                        {
                            first = report->token < first ? report->token : first;
                        }
                    }
                    CheckInitDiagnostic initializer;
                    const bool initialized = checked && check_initializers_diagnose(&initializer, &input, &stream, view, &types.table,
                                                                                     repair.record_begin, &output, &scratch);
                    check_test_expect(test, initialized && initializer.status != CHECK_INIT_RESOURCE, "repair initializer diagnostic scan completes");
                    if (initialized && initializer.status != CHECK_INIT_OK && initializer.token < repair.record_begin)
                    {
                        first = initializer.token < first ? initializer.token : first;
                    }
                    CheckProgramDiagnostic control;
                    const bool diagnosed = checked && check_program_diagnose(&control, &input, &stream, view, &types.table, cut, &scratch);
                    check_test_expect(test, diagnosed && control.status != CHECK_PROGRAM_RESOURCE, "repair control diagnostic scan has sufficient storage");
                    if (diagnosed && control.status == CHECK_PROGRAM_INVALID && control.token < cut && control.position < cut)
                    {
                        first = control.token < first ? control.token : first;
                    }
                }
                if (first != BUSTER_CHECK_NONE)
                {
                    check_test_expect(test, first < cut, "repair selected diagnostic belongs to an original completed statement");
                    lines[path_index] = check_test_repair_line(&original, &pp, first);
                }
                if (faults && path_index == 0)
                {
                    check_test_repair_faults(test, &original, &pp);
                }
            }
            else
            {
                check_test_expect(test, repair.parser.tokens.count == 0 && repair.preprocessor == 0 && repair.original_end == 0 &&
                                      output.length == 0 && scratch.length == 0, "unavailable repair publishes no partial tree or type and restores arenas");
            }
            snprintf(description, sizeof(description), "%s: %s first line %u, Clang %u, expected %u", fixture->name,
                     path_index == 0 ? "scalar" : "vector", lines[path_index], oracle->line, fixture->line);
            check_test_expect(test, oracle->rejected && lines[path_index] == oracle->line && oracle->line == fixture->line, description);
            check_test_expect(test, original.errors == errors && original.diagnostic_count == diagnostics && original.postorder == 0 &&
                                  pp.output.count == pp_count, "repair preserves original rejection, parser diagnostics and preprocessor output");
        }
        parse_release(&original);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&values);
        arena_release(&input_storage);
        arena_release(&output);
    }
    check_test_expect(test, lines[0] == lines[1], "repair original scalar/vector parse and graph diagnostics agree");
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_repair_report_boundaries(CheckTest* test)
{
    uint8_t kinds[] = { PARSE_KIND_IDENTIFIER, PARSE_KIND_AMPAMP, PARSE_KIND_R_BRACE };
    uint8_t roles[] = { PARSE_ROLE_OPERAND, PARSE_ROLE_PREFIX, PARSE_ROLE_CLOSE_BODY };
    uint32_t parents[] = { 1, 2, BUSTER_PARSE_ROOT };
    CheckRepair repair =
    {
        .parser = { .tokens = { .kinds = kinds, .count = 3 }, .tree = { .roles = roles, .parents = parents } },
        .status = CHECK_REPAIR_READY,
        .original_end = 2,
        .record_begin = 3,
    };
    CheckInputReport input = { .token = 0, .message = CHECK_INPUT_UNDECLARED };
    check_test_expect(test, !check_repair_input_report(&repair, &input), "repair never treats a GNU forward label address as an ordinary missing name");
    kinds[0] = PARSE_KIND_GOTO;
    check_test_expect(test, !check_repair_input_report(&repair, &input), "repair excludes synthetic end-of-body undefined labels");
    repair.original_end = 3;
    check_test_expect(test, check_repair_input_report(&repair, &input), "repair retains undefined labels from an earlier complete body");
    kinds[0] = PARSE_KIND_IDENTIFIER;
    kinds[1] = PARSE_KIND_PLUS;
    input.message = CHECK_INPUT_DECLARATION;
    check_test_expect(test, !check_repair_input_report(&repair, &input), "repair does not infer declaration diagnostics from synthetic closure");
    input.message = CHECK_INPUT_RESOURCE;
    check_test_expect(test, !check_repair_input_report(&repair, &input), "repair does not replace a syntax error with a resource failure");
    input = (CheckInputReport){ .token = 3, .message = CHECK_INPUT_UNDECLARED };
    check_test_expect(test, !check_repair_input_report(&repair, &input), "repair excludes all synthetic input-report tokens before reading token columns");
    uint32_t ends[] = { 1 };
    CheckGraph graph = { .count = 1, .source_positions = ends };
    CheckReport report = { .token = 0, .node = 0, .message = CHECK_MESSAGE_ACTION, .diagnostic = CHECK_DIAG_CALL };
    check_test_expect(test, check_repair_graph_report(&repair, &graph, &report), "repair permits an original fully resolved invalid call");
    ends[0] = 4;
    check_test_expect(test, !check_repair_graph_report(&repair, &graph, &report), "repair excludes graph nodes whose expression extends into synthetic tokens");
    ends[0] = 1;
    report.diagnostic = CHECK_DIAG_UNSUPPORTED;
    check_test_expect(test, !check_repair_graph_report(&repair, &graph, &report), "repair does not turn unsupported checking into a source error");
    report.diagnostic = CHECK_DIAG_INVALID_TYPE;
    check_test_expect(test, !check_repair_graph_report(&repair, &graph, &report), "repair excludes invalid internal type placeholders");
    report.message = CHECK_MESSAGE_CONSTANT;
    report.fault = CHECK_FAULT_UNSUPPORTED_CONSTANT;
    check_test_expect(test, !check_repair_graph_report(&repair, &graph, &report), "repair excludes unsupported constant evaluation");
    report.fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    check_test_expect(test, check_repair_graph_report(&repair, &graph, &report), "repair permits a resolved original constant division error");
    report.token = repair.original_end;
    check_test_expect(test, !check_repair_graph_report(&repair, &graph, &report), "repair excludes every synthetic graph report token");
}

static void check_test_repairs(CheckTest* test, const char* clang)
{
    const CheckRepairFixture fixtures[] =
    {
        {
            "ordinary missing name before syntax",
            "int f(void)\n{\n missing;\n return 0\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "invalid break before later name and reversed return",
            "int function(void)\n{\nbreak;\nmissing;\n 0 return ;\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "invalid break before later name and missing return terminator",
            "int function(void)\n{\nbreak;\nmissing;\nreturn 0 \n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "invalid break before incomplete identifier statement",
            "int function(void)\n{\nbreak;\nmissing \nreturn 0;\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "completed switch collision before malformed declaration",
            "int function(int value)\n{\nswitch (value)\n{\ncase 1: break;\ncase 1: break;\n}\n missing missing;\nreturn 0;\n}\n",
            6,
            CHECK_REPAIR_READY,
        },
        {
            "invalid continue before later syntax",
            "int f(void)\n{\ncontinue;\nreturn 0\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "valid loop break survives a synthetic outer close",
            "int f(int n)\n{\nwhile(n){break;}\nreturn 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "valid do continue does not acquire a false control error",
            "int f(int n)\n{\ndo {continue;}\nreturn 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "valid completed statements preserve syntax error",
            "int f(void)\n{\n int x = 1;\n return 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "forward label does not invent an error",
            "int f(void)\n{\n goto done;\n return 0\ndone:\n return 0;\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "forward label before invalid operand",
            "int f(void)\n{\n goto done;\n 1(2);\n return 0\ndone:\n return 0;\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "invalid operand before forward label",
            "int f(void)\n{\n 1(2);\n goto done;\n return 0\ndone:\n return 0;\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "invalid operand before unresolved name",
            "int f(void)\n{\n 1(2);\n missing;\n return 0\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "incomplete unary subtree cannot invent earlier error",
            "int f(void)\n{\n *\n missing;\n return 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "incomplete binary subtree cannot invent earlier error",
            "int f(void)\n{\n 1 +\n missing;\n return 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "incomplete call argument cannot invent earlier error",
            "int f(void)\n{\n 1(\n missing);\n return 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "earlier complete function retains missing label",
            "int first(void) { goto absent; }\nint later(void)\n{\n 1;\n return 0\n}\n",
            1,
            CHECK_REPAIR_READY,
        },
        {
            "duplicate original labels remain invalid",
            "int f(void)\n{\n again:;\n again:;\n return 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "record return keeps original type",
            "struct S { int x; };\nstruct S f(void)\n{\n return 1;\n int x =\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "typedef and completed tag survive",
            "typedef struct S { int x; } S;\nint f(S s)\n{\n s.missing;\n return 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "later tag completion cannot repair earlier sizeof",
            "struct S;\nint f(void)\n{\n sizeof(struct S);\n return 0\n}\nstruct S { int x; };\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "tentative object does not acquire premature TU error",
            "struct S tentative;\nint f(void)\n{\n int x;\n return 0\n}\nstruct S { int x; };\n",
            5,
            CHECK_REPAIR_READY,
        },
        {
            "later typedef cannot change earlier name binding",
            "int f(void)\n{\n T;\n return 0\n}\ntypedef int T;\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "conflicting original local declarations",
            "int f(void)\n{\n int x;\n double x;\n return 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "invalid original literal",
            "int f(void)\n{\n 123xyz;\n return 0\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "promoted warning in complete original enum before malformed body",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { N=2147483647+1 };\nint f(void){\n1;\nint broken=;\n}\n",
            2,
            CHECK_REPAIR_READY,
        },
        {
            "original local static initializer precedes later syntax error",
            "int get(void);\nint f(void){\nstatic int value=get();\nint broken=;\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "original incomplete local object precedes later syntax error",
            "int f(void){\nstruct Missing value;\nreturn 0\n}\n",
            2,
            CHECK_REPAIR_READY,
        },
        {
            "extern incomplete local object does not acquire a completion error",
            "int f(void){\nextern struct Missing value;\nreturn 0\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "initialized incomplete array still infers its extent",
            "int f(void){\nint values[]={1,2};\nreturn 0\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "original local static initializer precedes binding and syntax errors",
            "int get(void);\nint f(void){\nstatic int value=get();\nmissing;\nint broken=;\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "promoted warning in complete local enum before malformed statement",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nint f(void){\nenum { N=2147483647+1 };\nint broken=;\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "ignored original enum warning preserves later syntax diagnostic",
            "#pragma clang diagnostic ignored \"-Winteger-overflow\"\nint f(void){\nenum { N=2147483647+1 };\nint broken=;\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "complete invalid register annotation precedes malformed asm statement",
            "int f(void){\nregister long value\nasm(\"$4\")=1;\nasm(\"\":::MISSING);\nreturn value;\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "complete valid register annotation preserves later syntax diagnostic",
            "int f(void){\nregister long value\nasm(\"rax\")=1;\nasm(\"\":::MISSING);\nreturn value;\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "ordinary asm symbol name is not a register annotation",
            "int f(void){\nextern long value\nasm(\"$4\");\nasm(\"\":::MISSING);\nreturn value;\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "nested block retains completed expression",
            "int f(int n)\n{\n if (n) {\n missing;\n return 0\n }\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "known open do gets exact required tail",
            "int f(int n)\n{\n do {\n missing;\n return 0\n } while (n);\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "known open do without an original zero literal",
            "int f(int n)\n{\n do {\n missing;\n return 1\n } while (n);\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "nested known do tails remain bounded",
            "int f(int n)\n{\n do {\n do {\n missing;\n return 0\n } while(n);\n } while(n);\n}\n",
            5,
            CHECK_REPAIR_READY,
        },
        {
            "open do with forward label before invalid call",
            "int f(int n)\n{\n do {\n goto done;\n 1(2);\n return 0\n } while(n);\ndone: return 0;\n}\n",
            5,
            CHECK_REPAIR_READY,
        },
        {
            "earlier statement survives unfinished local record",
            "int f(void)\n{\n missing;\n struct S {\n int member;\n int broken =\n };\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "nested statement survives unfinished local record",
            "int f(int n)\n{\n if (n) {\n 1(2);\n struct S {\n int member;\n int broken =\n };\n }\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "nested unfinished type braces do not replace earlier statement",
            "int f(void)\n{\n missing;\n struct Outer {\n struct Inner {\n int member;\n int broken =\n };\n };\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "earlier statement survives unfinished initializer",
            "int f(void)\n{\n missing;\n int values[] = {\n 1;\n };\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "genuinely completed local type remains available",
            "int f(void)\n{\n struct S { int member; } s;\n s.absent;\n return 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "original invalid condition precedes a synthetic body close",
            "struct S { int member; };\nint f(struct S s)\n{\n if (s) {\n int x;\n return 0\n }\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "original invalid condition precedes later missing name",
            "struct S { int member; };\nint f(struct S s)\n{\n if (s) {\n missing;\n return 0\n }\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "macro body diagnostic retains original invocation line",
            "#define BAD missing\nint f(void)\n{\n BAD;\n return 0\n}\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "private DO output view retains macro history",
            "#define BAD missing\nint f(int n)\n{\n do {\n BAD;\n return 1\n } while(n);\n}\n",
            5,
            CHECK_REPAIR_READY,
        },
        {
            "private DO output view retains token-paste provenance",
            "#define CAT(a,b) a##b\nint f(int n)\n{\n do {\n CAT(miss,ing);\n return 1\n } while(n);\n}\n",
            5,
            CHECK_REPAIR_READY,
        },
        {
            "retained pragma cannot silently change record layout",
            "#pragma pack(1)\nstruct S { char a; int b; };\nint f(void)\n{\n _Static_assert(sizeof(struct S) == 5, \"packing\");\n return 0\n}\n",
            6,
            CHECK_REPAIR_READY,
        },
        {
            "type member cutoff cannot create a complete record",
            "struct S {\n int x;\n int broken[\n};\n",
            4,
            CHECK_REPAIR_READY,
        },
        {
            "incomplete member precedes malformed later member type",
            "struct Missing;\nstruct S{\nstruct Missing member;\nunknown_type later;\n};\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "incomplete union member precedes malformed later member type",
            "union Missing;\nunion S{\nunion Missing member;\nunknown_type later;\n};\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "nested incomplete member survives synthetic record tails",
            "struct Outer{\nstruct Inner{\nstruct Missing member;\nunknown_type later;\n} inner;\n};\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "local incomplete member survives synthetic record and body tails",
            "int f(void){\nstruct S{\nstruct Missing member;\nunknown_type later;\n};\n}\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "incomplete member remains invalid before later original tag completion",
            "struct S{\nstruct Missing member;\nstruct Missing{int x;} complete;\nunknown_type later;\n};\n",
            2,
            CHECK_REPAIR_READY,
        },
        {
            "pointer to incomplete member does not acquire a diagnostic",
            "struct S{\nstruct Missing *member;\nunknown_type later;\n};\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "record completion cannot invent a flexible-array diagnostic",
            "struct S{\nint member[];\nunknown_type later;\n};\n",
            3,
            CHECK_REPAIR_READY,
        },
        {
            "unfinished first statement cannot be fabricated",
            "int f(void)\n{\n return 0\n}\n",
            3,
            CHECK_REPAIR_UNAVAILABLE,
        },
        {
            "unterminated typedef cannot create a fake type",
            "typedef struct S { int x;\n",
            1,
            CHECK_REPAIR_READY,
        },
        {
            "initializer brace cannot become a statement block",
            "int f(void)\n{\n int a[] = {\n 1;\n };\n}\n",
            4,
            CHECK_REPAIR_UNAVAILABLE,
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-repair-XXXXXX";
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "repair first-error Clang oracle runs on original source");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        const bool faults = i == 0 || strcmp(fixtures[i].name, "known open do without an original zero literal") == 0;
        const bool warnings = strstr(fixtures[i].source, "#pragma clang diagnostic") != 0;
        const bool oracle = !warnings || check_test_prefix_clang_warnings(clang, paths[i], outcomes + i);
        check_test_expect(test, oracle, "repair warning-promotion Clang oracle runs without suppression");
        if (oracle)
        {
            check_test_repair_fixture(test, fixtures + i, paths[i], outcomes + i, faults);
        }
    }
    check_test_repair_report_boundaries(test);
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

#endif
