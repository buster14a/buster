// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original GNU assembly semantic fixtures. Clang checks each unchanged source independently. Parser/reader,
// typed operand metadata and the complete semantic program are compared across both paths by the corpus helper.
// Include after c_check_corpus.c; no third-party source fixtures are stored here.
#ifndef BUSTER_C_CHECK_ASM_TEST_INCLUDED
#define BUSTER_C_CHECK_ASM_TEST_INCLUDED

typedef struct CheckAsmFixture CheckAsmFixture;
struct CheckAsmFixture
{
    const char* name;
    const char* source;
};

static const CheckAsmFixture check_asm_fixtures[] =
{
    {
        "basic-file",
        "__asm__(\".text\\n\" \".p2align 2\");int f(void){return 1;}",
    },
    {
        "basic-function",
        "int f(void){asm(\"nop\");return 1;}",
    },
    {
        "register-variable",
        "int f(void){register long value asm(\"r12\")=3;return value;}",
    },
    {
        "symbol-label-is-not-register",
        "extern int value asm(\"not_a_register\");int f(void){return value;}",
    },
    {
        "symbol-object-conflict",
        "int x asm(\"a\");\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-function-conflict",
        "int x(void) asm(\"a\");\nint x(void) asm(\"b\");\n",
    },
    {
        "symbol-identical-labels",
        "int x asm(\"a\");\nextern int x asm(\"a\");\nint f(void) asm(\"b\");\nint f(void) asm(\"b\");\n",
    },
    {
        "symbol-ignored-label-promoted",
        "#pragma clang diagnostic error \"-Wignored-attributes\"\nint x=1;\nextern int x asm(\"other\");\n",
    },
    {
        "symbol-ignored-label-before-syntax",
        "#pragma clang diagnostic error \"-Wignored-attributes\"\nint x=1;\nextern int x asm(\"other\");\nint broken=;\n",
    },
    {
        "symbol-ignored-label-policy-pop",
        "#pragma clang diagnostic error \"-Wignored-attributes\"\n#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wignored-attributes\"\n"
        "int x=1;\nextern int x asm(\"other\");\n#pragma clang diagnostic pop\nint f(void){return 1;}\nint f(void) asm(\"renamed\");\n",
    },
    {
        "symbol-late-read",
        "int x;\nint f(void){return x;}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-late-call",
        "int x(void);\nint f(void){return x();}\nint x(void) asm(\"b\");\n",
    },
    {
        "symbol-late-address",
        "int x;\nint *p=&x;\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-late-function-address",
        "int x(void);\nint (*p)(void)=x;\nint x(void) asm(\"b\");\n",
    },
    {
        "symbol-late-original-name",
        "int x;\nint f(void){return x;}\nextern int x asm(\"x\");\n",
    },
    {
        "symbol-after-object-definition",
        "int x=1;\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-after-function-definition",
        "int x(void){return 1;}\nint x(void) asm(\"b\");\n",
    },
    {
        "symbol-sizeof-is-not-use",
        "int x;\nint f(void){return sizeof(x)+sizeof x+sizeof(&x);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-alignof-is-not-use",
        "int x;\nint f(void){return __alignof__(x);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-typeof-is-not-use",
        "int x;\n__typeof__(x) y;\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-typeof-call-is-not-use",
        "int x(void);\n__typeof__(x()) y;\nint x(void) asm(\"b\");\n",
    },
    {
        "symbol-conditional-reference-is-use",
        "int x;\nint f(void){return 1?0:x;}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-short-circuit-reference-is-use",
        "int x;\nint f(void){return 0&&x;}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-generic-control-is-not-use",
        "int x;\nint f(void){return _Generic(x,int:0);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-generic-selected-reference-is-use",
        "int x;\nint f(void){return _Generic(0,int:x,default:0);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-generic-unselected-reference-is-use",
        "int x;\nint f(void){return _Generic(0,int:0,default:x);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-choose-unselected-reference-is-use",
        "int x;\nint f(void){return __builtin_choose_expr(1,0,x);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-constant-query-reference-is-use",
        "int x;\nint f(void){return __builtin_constant_p(x);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-object-size-reference-is-use",
        "int x;\nint f(void){return __builtin_object_size(&x,0);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-sizeof-vla-bound-is-use",
        "int x;\nint f(void){return sizeof(int[x]);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-typeof-vla-bound-is-use",
        "int x;\nint f(void){__typeof__(int[x]) a;return sizeof(a);}\nextern int x asm(\"b\");\n",
    },
    {
        "symbol-linked-local-conflict",
        "void f(void){extern int x asm(\"a\");}\nvoid g(void){extern int x asm(\"b\");}\n",
    },
    {
        "symbol-local-label-after-use",
        "int x;\nint f(void){int y=x;extern int x asm(\"b\");return y;}\n",
    },
    {
        "symbol-identical-label-after-use",
        "int x asm(\"a\");\nint f(void){return x;}\nextern int x asm(\"a\");\n",
    },
    {
        "symbol-automatic-shadow-is-not-use",
        "int x;\nint f(void){int x=1;return x;}\nextern int x asm(\"a\");\n",
    },
    {
        "symbol-embedded-null-label-conflict",
        "int x asm(\"a\\0b\");\nextern int x asm(\"a\\0c\");\n",
    },
    {
        "symbol-empty-label",
        "int x asm(\"\");\n",
    },
    {
        "symbol-conflict-before-input-error",
        "int x asm(\"a\");\nextern int x asm(\"b\");\nint f(void){return missing;}\n",
    },
    {
        "symbol-conflict-before-syntax-error",
        "int x asm(\"a\");\nextern int x asm(\"b\");\nint f(void){return ; +}\n",
    },
    {
        "invalid-register-before-syntax",
        "int f(void){\nregister long value\nasm(\"$4\")=1;\nasm(\"\":::MISSING);\nreturn value;\n}",
    },
    {
        "empty-extended",
        "void f(void){asm(\"\":::);}",
    },
    {
        "register-output",
        "int f(int x){int y;asm(\"mov %1,%0\":\"=r\"(y):\"r\"(x));return y;}",
    },
    {
        "read-write",
        "int f(int x){asm volatile inline(\"add $1,%0\":\"+r\"(x)::\"cc\");return x;}",
    },
    {
        "named-match",
        "int f(int x){int y;asm(\"%[dst] %[src]\":[dst]\"=r\"(y):[src]\"[dst]\"(x):\"cc\");return y;}",
    },
    {
        "numeric-match",
        "long double f(long double x){long double y;asm(\"frndint\":\"=t\"(y):\"0\"(x));return y;}",
    },
    {
        "alternative-match",
        "int f(int x){int y;asm(\"\":\"=r,m\"(y):\"0,0\"(x));return y;}",
    },
    {
        "calls-evaluated-once",
        "int get(void);int*slot(void);void f(void){asm(\"\":\"=m\"(*slot()):\"r\"(get()):\"memory\");}",
    },
    {
        "input-memory-array",
        "void f(int*p){asm(\"\"::\"m\"(*(int(*)[4])p));}",
    },
    {
        "input-array-decay",
        "void f(void){int a[4];asm(\"\"::\"r\"(a));}",
    },
    {
        "function-decay",
        "void f(void){asm(\"\"::\"r\"(f));}",
    },
    {
        "symbolic-immediate",
        "int x;void f(void){asm(\"\"::\"i\"(&x));}",
    },
    {
        "dynamic-immediate-syntax",
        "void f(int x){asm(\"\"::\"i\"(x));}",
    },
    {
        "record-memory",
        "struct S{int x;int y;};void f(struct S s){asm(\"\":\"=m\"(s):\"m\"(s));}",
    },
    {
        "record-register",
        "struct S{int x;int y;};void f(struct S s){asm(\"\":\"=r\"(s):\"r\"(s));}",
    },
    {
        "bitfield-register",
        "struct S{unsigned x:3;};void f(struct S*s){asm(\"\":\"=r\"(s->x));}",
    },
    {
        "memory-read-write",
        "void f(int*p){asm(\"\":\"+m\"(*p)::\"memory\");}",
    },
    {
        "flag-output",
        "int f(int x){int c;asm(\"test %1,%1\":\"=@ccz\"(c):\"r\"(x));return c;}",
    },
    {
        "goto-forward",
        "int f(int x){asm goto(\"test %0,%0; jz %l[zero]\"::\"r\"(x):\"cc\":zero);return 1;zero:return 0;}",
    },
    {
        "goto-output",
        "int f(int x){asm goto(\"%0 %l2\":\"+r\"(x):::done);return x;done:return x+1;}",
    },
    {
        "goto-nested-escape",
        "int f(int x){int y=({asm goto(\"%l[done]\"::::done);x;});return y;done:return 1;}",
    },
    {
        "clobber-registers",
        "void f(void){asm volatile(\"\"::: \"rax\",\"r8d\",\"st(1)\",\"xmm15\",\"cc\",\"memory\");}",
    },
    {
        "fixed-register-constraint",
        "int f(int x){int y;asm(\"\":\"={eax}\"(y):\"{ebx}\"(x));return y;}",
    },
    {
        "literal-concatenation",
        "void f(int x){asm(\"\" \"%0\"::\"r\" \"m\"(x));}",
    },
    {
        "percent-escapes",
        "void f(int x){asm(\"%%eax %= %{ %| %} %c0\"::\"i\"(1));}",
    },
    {
        "const-output",
        "void f(void){const int x=0;asm(\"\":\"=r\"(x));}",
    },
    {
        "rvalue-output",
        "void f(int x){asm(\"\":\"=r\"(x+1));}",
    },
    {
        "array-output",
        "void f(void){int x[2];asm(\"\":\"=m\"(x));}",
    },
    {
        "const-array-output",
        "void f(void){const int x[2]={0};asm(\"\":\"=m\"(x));}",
    },
    {
        "const-typedef-array-output",
        "typedef int A[2];void f(void){const A x={0};asm(\"\":\"=m\"(x));}",
    },
    {
        "const-typedef-array-member-output",
        "typedef int A[2];struct S{const A x;};void f(struct S s){asm(\"\":\"=m\"(s));}",
    },
    {
        "function-output",
        "void f(void){asm(\"\":\"=r\"(f));}",
    },
    {
        "void-input",
        "void f(void){asm(\"\"::\"r\"((void)0));}",
    },
    {
        "memory-rvalue",
        "void f(int x){asm(\"\"::\"m\"(x+1));}",
    },
    {
        "memory-register",
        "void f(void){register int x=1;asm(\"\"::\"m\"(x));}",
    },
    {
        "memory-bitfield",
        "struct S{unsigned x:3;};void f(struct S*s){asm(\"\"::\"m\"(s->x));}",
    },
    {
        "const-record-output",
        "struct S{const int x;};void f(struct S s){asm(\"\":\"=m\"(s));}",
    },
    {
        "invalid-output-prefix",
        "void f(int x){asm(\"\":\"r\"(x));}",
    },
    {
        "invalid-input-prefix",
        "void f(int x){asm(\"\"::\"=r\"(x));}",
    },
    {
        "empty-constraint",
        "void f(int x){asm(\"\"::\"\"(x));}",
    },
    {
        "compound-constraint",
        "void f(int x){asm(\"\"::\"?invalid\"(x));}",
    },
    {
        "invalid-clobber",
        "void f(void){asm(\"\"::: \"not_a_register\");}",
    },
    {
        "invalid-match-index",
        "void f(int x){asm(\"\"::\"0\"(x));}",
    },
    {
        "invalid-match-name",
        "void f(int x){int y;asm(\"\":\"=r\"(y):\"[missing]\"(x));}",
    },
    {
        "matching-integer-widths",
        "void f(long x){char y;asm(\"\":\"=r\"(y):\"0\"(x));}",
    },
    {
        "invalid-match-domain",
        "void f(int x){float y;asm(\"\":\"=r\"(y):\"0\"(x));}",
    },
    {
        "invalid-alternative-count",
        "void f(int x){int y;asm(\"\":\"=r,m\"(y):\"r\"(x));}",
    },
    {
        "invalid-duplicate-name",
        "void f(int x){int y;asm(\"\":[same]\"=r\"(y):[same]\"r\"(x));}",
    },
    {
        "invalid-template-index",
        "void f(void){asm(\"%0\":::);}",
    },
    {
        "invalid-template-name",
        "void f(int x){asm(\"%[missing]\"::\"r\"(x));}",
    },
    {
        "invalid-template-percent",
        "void f(void){asm(\"%\":::);}",
    },
    {
        "invalid-goto-label",
        "void f(void){asm goto(\"%l[missing]\"::::missing);}",
    },
    {
        "invalid-goto-entry",
        "void f(void){asm goto(\"%l[label]\"::::label);({label:1;});}",
    },
    {
        "invalid-goto-name-collision",
        "void f(int x){asm goto(\"\"::[label]\"r\"(x)::label);label:;}",
    },
    {
        "embedded-nul-template",
        "void f(void){asm(\"nop\\0nop\");}",
    },
    {
        "embedded-nul-constraint",
        "void f(int x){asm(\"\"::\"r\\0q\"(x));}",
    },
    {
        "embedded-nul-clobber",
        "void f(void){asm(\"\"::: \"eax\\0unknown\");}",
    },
    {
        "wide-template",
        "void f(void){asm(L\"nop\");}",
    },
    {
        "input-undeclared",
        "void f(void){asm(\"\"::\"r\"(missing));}",
    },
    {
        "multiline-output-constraint",
        "void f(int x){\nasm(\"\"\n:\n\"r\"(x));\n}",
    },
    {
        "multiline-output-value",
        "void f(int x){\nasm(\"\"\n:\"=r\"(\nx+1));\n}",
    },
    {
        "multiline-input-value",
        "void f(void){\nasm(\"\"\n::\"r\"(\n(void)0));\n}",
    },
    {
        "multiline-clobber",
        "void f(void){\nasm(\"\"\n:::\n\"unknown_register\");\n}",
    },
    {
        "multiline-template",
        "void f(void){\nasm(\n\"%9\"\n:::);\n}",
    },
    {
        "multiline-goto-label",
        "void f(void){\nasm goto(\"%l[missing]\"\n::::\nmissing);\n}",
    },
    {
        "earlier-output-later-binding",
        "void f(void){\nconst int x=1;\nasm(\"\":\"=r\"(x));\nmissing;\n}",
    },
    {
        "earlier-output-later-graph",
        "void f(void){\nconst int x=1;\nasm(\"\":\"=r\"(x));\n1();\n}",
    },
    {
        "earlier-output-later-syntax",
        "void f(void){\nconst int x=1;\nasm(\"\":\"=r\"(x));\nint broken=;\n}",
    },
    {
        "earlier-output-later-input-operand",
        "void f(void){\nconst int x=1;\nasm(\"\":\"=r\"(x):\n\"r\"(missing));\n}",
    },
    {
        "earlier-initializer-later-output",
        "void f(void){\nint a[1]=1;\nconst int x=1;\nasm(\"\":\"=r\"(x));\n}",
    },
    {
        "tainted-output-later-syntax",
        "void f(void){\nasm(\"\":\"=r\"(missing));\nint broken=;\n}",
    },
};

static void check_test_asm_capacity(CheckTest* test)
{
    static const char source[] = "int get(void);int*slot(void);void f(void){asm volatile(\"\":\"+m\"(*slot()):\"r\"(get()):\"memory\");}";
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options =
    {
        .language =
        {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
    };
    CheckCorpusPath path = { 0 };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                 pp_run_source(&pp, "asm-capacity.c", (const uint8_t*)source, sizeof(source) - 1) &&
                 check_corpus_check(&path, &parser, &pp, false);
    check_test_expect(test, ready, "typed assembly capacity source is accepted");
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    ready = ready && output.base != NULL && scratch.base != NULL;
    check_test_expect(test, ready, "typed assembly fault arenas initialize");
    if (ready)
    {
        uint32_t calls = 0;
        for (uint32_t node = 0; node < path.input.graph.count; node += 1)
        {
            calls += path.input.graph.ops[node] == CHECK_OP_CALL;
        }
        check_test_expect(test, calls == 2 && path.input.asm_count == 1 && path.asms.count == 2,
                          "asm operand parentheses are not calls and operand evaluation roots occur once");
        const CheckInputAsm* assembly = path.input.asm_statements;
        check_test_expect(test, assembly->is_volatile && assembly->memory_clobber && assembly->output_count == 1 && assembly->input_count == 1 &&
                              (assembly->operands[0].flags & (CHECK_ASM_READ | CHECK_ASM_WRITE | CHECK_ASM_MEMORY)) ==
                              (CHECK_ASM_READ | CHECK_ASM_WRITE | CHECK_ASM_MEMORY), "asm read/write memory and qualifier effects are retained");
        memset(output.base, 0xa5, output.capacity);
        memset(scratch.base, 0x5a, scratch.capacity);
        output.length = 17;
        scratch.length = 19;
        CheckAsms result;
        ready = check_asms_run(&result, &path.input, &path.stream, &path.types, &output, &scratch);
        check_test_expect(test, ready && result.count == 2 && result.status == CHECK_ASM_OK && scratch.length == 19,
                          "typed asm publication ignores poisoned storage and restores scratch");
        const uint64_t used = output.length;
        const uint64_t capacity = output.capacity;
        for (uint64_t limit = 17; ready && limit < used; limit += 1)
        {
            output.length = 17;
            output.capacity = limit;
            const bool accepted = check_asms_run(&result, &path.input, &path.stream, &path.types, &output, &scratch);
            check_test_expect(test, !accepted && result.status == CHECK_ASM_RESOURCE && result.count == 0 && result.operands == NULL &&
                                  output.length == 17 && scratch.length == 19, "each asm output exhaustion rolls back metadata and storage");
        }
        output.capacity = capacity;
        output.length = 17;
        check_test_expect(test, !check_asms_run(&result, &path.input, &path.stream, &path.types, &output, &output) &&
                              result.status == CHECK_ASM_INVALID && output.length == 17, "typed asm rejects aliased output and scratch");
        check_test_expect(test, check_asms_run(&result, &path.input, &path.stream, &path.types, &output, &scratch) && result.count == 2,
                          "typed asm remains usable after every exhaustion");
        CheckAsmDiagnostic diagnostic;
        const uint64_t scratch_capacity = scratch.capacity;
        check_test_expect(test, check_asms_diagnose(&diagnostic, &path.input, &path.stream, &path.types, BUSTER_CHECK_NONE, &scratch) &&
                              diagnostic.status == CHECK_ASM_OK && diagnostic.token == BUSTER_CHECK_NONE && scratch.length == 19,
                          "diagnostic-only asm scan publishes no operands and restores scratch");
        scratch.capacity = scratch.length;
        check_test_expect(test, !check_asms_diagnose(&diagnostic, &path.input, &path.stream, &path.types, BUSTER_CHECK_NONE, &scratch) &&
                              diagnostic.status == CHECK_ASM_RESOURCE && scratch.length == 19,
                          "diagnostic-only asm closure exhaustion is a resource failure and rolls back");
        scratch.capacity = scratch_capacity;
    }
    arena_release(&scratch);
    arena_release(&output);
    check_corpus_close(&path);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
}

static bool check_test_asm_fixture(CheckTest* test, CheckCorpusTally* tally, const CheckAsmFixture* source, const char* directory, const char* clang)
{
    const PPOptions options =
    {
        .language =
        {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
    };
    const char* arguments[] =
    {
        "--no-default-config",
        "--target=x86_64-linux-gnu",
        "-std=gnu17",
        "-nostdinc",
        "-x",
        "c",
    };
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.c", directory, source->name);
    bool ready = parse_test_write(path, source->source, strlen(source->source));
    CheckCorpusOracle oracle;
    ready = ready && check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), path, &oracle);
    if (ready)
    {
        const bool different = check_corpus_file(tally, path, source->name, "asm-fixture", &options, &oracle, NULL, 64);
        check_test_expect(test, !different, source->name);
        if (different)
        {
            fprintf(stderr, "%s: Clang: %s\n", source->name, oracle.message);
        }
        ready = unlink(path) == 0;
    }
    return ready;
}

static void check_test_asm_label_metadata(CheckTest* test)
{
    static const char source[] =
        "int kept asm(\"retained_name\");\nint object=1;\nextern int object asm(\"ignored_object\");\n"
        "int function(void){return 2;}\nint function(void) asm(\"ignored_function\");\n";
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    CheckCorpusPath path = { 0 };
    const bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                       pp_run_source(&pp, "asm-label-metadata.c", (const uint8_t*)source, sizeof(source) - 1) &&
                       check_corpus_check(&path, &parser, &pp, false);
    check_test_expect(test, ready, "late labels after actual definitions preserve Clang acceptance");
    if (ready)
    {
        uint32_t retained = 0;
        uint32_t ignored = 0;
        for (uint32_t token = 0; token < path.input.token_count; token += 1)
        {
            const CheckInputAttributes* attributes = path.input.attributes[token];
            for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL)
                {
                    retained += !attribute->ignored;
                    ignored += attribute->ignored;
                }
            }
        }
        check_test_expect(test, retained == 1 && ignored == 2, "late ignored labels cannot silently rename existing definitions");
        Arena scratch = arena_reserve(64);
        const uint64_t capacity = scratch.capacity;
        scratch.length = 17;
        scratch.capacity = 17;
        CheckInputReader reader = { .output = &path.input, .parser = &parser, .scratch = &scratch, .expression_limit = path.input.token_count };
        const uint32_t reports = path.input.report_count;
        check_input_asm_labels(&reader);
        check_test_expect(test, path.input.resource && path.input.report_count == reports && scratch.length == 17,
                          "asm-label identity-state exhaustion is a resource failure and restores scratch without semantic reports");
        path.input.resource = false;
        scratch.capacity = capacity;
        arena_release(&scratch);
    }
    check_corpus_close(&path);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
}

static int run_checker_asm_tests(int argc, char** argv)
{
    // SOURCE_DATE_EPOCH fixed where it isn't set, so that the compilers' runs it compares with can be kept
    // (oracle_epoch_begin, oracle_cache.c).
    const bool epoch = oracle_epoch_begin();
    const char* clang = argc == 2 && strcmp(argv[0], "--clang") == 0 ? argv[1] : "clang";
    CheckTest test = { 0 };
    CheckCorpusTally tally = { 0 };
    // The process's oracle cache, set up before any fixture runs clang through it (oracle_cache.c, #542).
    OracleCache* oracle = oracle_cache();
    char directory[] = "/tmp/buster-check-asm-XXXXXX";
    bool ready = (argc == 0 || argc == 2) && mkdtemp(directory) != NULL;
    for (uint32_t fixture = 0; ready && fixture < BUSTER_ARRAY_LENGTH(check_asm_fixtures); fixture += 1)
    {
        ready = check_test_asm_fixture(&test, &tally, check_asm_fixtures + fixture, directory, clang);
    }
    // Every printable one-byte constraint independently exercises fail-closed target validation. These are
    // generated original programs, not copies of a target constraint table or of the implementation's cases.
    for (uint32_t byte = 33; ready && byte <= 126; byte += 1)
    {
        for (uint32_t output = 0; ready && output < 2; output += 1)
        {
            char name[64];
            char source[160];
            snprintf(name, sizeof(name), "constraint-%s-%u", output ? "output" : "input", byte);
            snprintf(source, sizeof(source), "void f(int x){asm(\"\":%s\"%s%s%c\"(x));}", output ? "" : ":", output ? "=" : "",
                     byte == '"' || byte == '\\' ? "\\" : "", byte);
            const CheckAsmFixture fixture =
            {
                .name = name,
                .source = source,
            };
            ready = check_test_asm_fixture(&test, &tally, &fixture, directory, clang);
        }
    }
    static const char* const clobbers[] =
    {
        "r0",
        "r7",
        "r8",
        "r15",
        "r16",
        "r31",
        "r32",
        "r08",
        "r8b",
        "r8w",
        "r8d",
        "r8q",
        "xmm0",
        "xmm15",
        "xmm16",
        "xmm31",
        "xmm32",
        "xmm00",
        "ymm31",
        "zmm31",
        "mm7",
        "mm8",
        "k7",
        "k8",
        "st(0)",
        "st(7)",
        "st(8)",
        "%rax",
        "#rax",
        "flags",
        "dirflag",
        "fpsr",
        "fpcr",
        "redzone",
        "rip",
        "eip",
        "mxcsr",
        "eflags",
        "tmm0",
        "tmm7",
        "tmm8",
        "",
    };
    for (uint32_t clobber = 0; ready && clobber < BUSTER_ARRAY_LENGTH(clobbers); clobber += 1)
    {
        char name[64];
        char source[160];
        snprintf(name, sizeof(name), "clobber-%u", clobber);
        snprintf(source, sizeof(source), "void f(void){asm(\"\":::\"%s\");}", clobbers[clobber]);
        const CheckAsmFixture fixture =
        {
            .name = name,
            .source = source,
        };
        ready = check_test_asm_fixture(&test, &tally, &fixture, directory, clang);
        if (ready)
        {
            snprintf(name, sizeof(name), "register-variable-%u", clobber);
            snprintf(source, sizeof(source), "long f(void){register long value asm(\"%s\")=1;return value;}", clobbers[clobber]);
            ready = check_test_asm_fixture(&test, &tally, &fixture, directory, clang);
        }
    }
    for (uint32_t byte = 33; ready && byte <= 126; byte += 1)
    {
        for (uint32_t output = 0; ready && output < 2; output += 1)
        {
            char name[64];
            char source[160];
            snprintf(name, sizeof(name), "target-pair-%s-%u", output ? "output" : "input", byte);
            snprintf(source, sizeof(source), "void f(int x){asm(\"\":%s\"%sY%s%c\"(x));}", output ? "" : ":", output ? "=" : "",
                     byte == '"' || byte == '\\' ? "\\" : "", byte);
            const CheckAsmFixture fixture =
            {
                .name = name,
                .source = source,
            };
            ready = check_test_asm_fixture(&test, &tally, &fixture, directory, clang);
        }
    }
    if (ready)
    {
        ready = rmdir(directory) == 0;
    }
    check_test_expect(&test, ready, "assembly fixture process and source accounting");
    check_test_asm_capacity(&test);
    check_test_asm_label_metadata(&test);
    oracle_cache_report(oracle, "test-checker-asm");
    printf("test-checker-asm: %" PRIu64 " checks, %" PRIu64 " failures; sources=%" PRIu64 " accepted=%" PRIu64 "\n",
           test.checks, test.failures, tally.files, tally.checker_accepted);
    const int returned = test.failures == 0 ? 0 : 1;
    oracle_epoch_end(epoch);
    return returned;
}

#endif
