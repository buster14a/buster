// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original GNU field-designator fixtures; Clang is an external oracle, not a source of copied tests.
// Include after the initializer source/ELF helpers.

static void check_init_test_old_style_bounds(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0 &&
                         mprotect(mapping + page * 3, page, PROT_NONE) == 0;
    check_test_expect(test, guarded, "old-style designator token kinds and roles have independent guard pages");
    static Parser parser;
    bool bounded = guarded;
    for (uint32_t count = 0; bounded && count <= 130; count += 1)
    {
        parser.tokens.count = count;
        parser.tokens.kinds = mapping + page - count;
        parser.tree.roles = mapping + page * 3 - count;
        memset(parser.tokens.kinds, PARSE_KIND_NUMBER, count);
        memset(parser.tree.roles, PARSE_ROLE_NONE, count);
        if (count != 0)
        {
            parser.tokens.kinds[0] = PARSE_KIND_L_BRACE;
        }
        bounded = !check_init_old_style_designator(&parser, count, count) &&
                  (count == 0 || !check_init_old_style_designator(&parser, count - 1, count));
        for (uint32_t path = 0; bounded && path < 2; path += 1)
        {
            parser.options.scalar = path == 0;
            bounded = !check_init_history_needed(&parser, 0, count);
        }
        if (count >= 3)
        {
            parser.tokens.kinds[count - 2] = PARSE_KIND_IDENTIFIER;
            parser.tokens.kinds[count - 1] = PARSE_KIND_COLON;
            parser.tree.roles[count - 1] = PARSE_ROLE_CLOSE_TERNARY;
            for (uint32_t path = 0; bounded && path < 2; path += 1)
            {
                parser.options.scalar = path == 0;
                bounded = !check_init_old_style_designator(&parser, count - 2, count) &&
                          !check_init_history_needed(&parser, 0, count);
                parser.tree.roles[count - 1] = PARSE_ROLE_ASSIGN;
                bounded = bounded && check_init_old_style_designator(&parser, count - 2, count) &&
                          check_init_history_needed(&parser, 0, count);
                parser.tree.roles[count - 1] = PARSE_ROLE_CLOSE_TERNARY;
            }
        }
    }
    check_test_expect(test, bounded, "old-style detection and scalar/vector history agree at every guarded tail and 64-token boundary");
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 4);
    }
}

static void check_init_test_old_style(CheckTest* test, const char* clang)
{
    check_init_test_old_style_bounds(test);
    static const char source[] =
        "struct P{int x,y,z;};\n"
        "struct P old_reordered={y:2,x:1};struct P old_prefix={y:-2};\n"
        "struct N{int x;int y[2];} old_nested={y:{1,2}};\n"
        "struct P old_continued={y:3,4};struct P old_mixed={y:2,.x=1,z:3};\n"
        "union U{struct P s;long n;} old_union={s:{z:9,x:7}};\n"
        "struct A{struct{int x,y;};int z;} old_anonymous={y:2,3};\n"
        "struct P old_inferred[]={{z:3,x:1},{y:5,6}};\n"
        "const struct P old_const={y:7,x:3};int old_const_read=old_const.x+old_const.y;\n"
        "struct R{int a[2][2];int b;} old_replay={.a[0 ... 1]=7,b:9};\n"
        "struct P old_conditional={x:1?2:3,y:0?4:5,z:6};\n"
        "typedef int member;struct T{int member;char text[4];} old_names={text:\"abc\",member:11};\n"
        "struct P old_compound=(struct P){z:8,x:4};\n";
    static const CheckInitExpected expected[] = {
        { .name = "old_reordered", .bytes = { [0] = 1, [4] = 2 }, .size = 12, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_prefix", .bytes = { [4] = 254, [5] = 255, [6] = 255, [7] = 255 }, .size = 12, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_nested", .bytes = { [4] = 1, [8] = 2 }, .size = 12, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_continued", .bytes = { [4] = 3, [8] = 4 }, .size = 12, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_mixed", .bytes = { [0] = 1, [4] = 2, [8] = 3 }, .size = 12, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_union", .bytes = { [0] = 7, [8] = 9 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_anonymous", .bytes = { [4] = 2, [8] = 3 }, .size = 12, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_inferred", .bytes = { [0] = 1, [8] = 3, [16] = 5, [20] = 6 }, .size = 24, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_const", .bytes = { [0] = 3, [4] = 7 }, .size = 12, .class = BUSTER_LINK_CLASS_RODATA },
        { .name = "old_const_read", .bytes = { 10 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_replay", .bytes = { [0] = 7, [8] = 7, [16] = 9 }, .size = 20, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_conditional", .bytes = { [0] = 2, [4] = 5, [8] = 6 }, .size = 12, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_names", .bytes = { 11, 0, 0, 0, 'a', 'b', 'c', 0 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "old_compound", .bytes = { [0] = 4, [8] = 8 }, .size = 12, .class = BUSTER_LINK_CLASS_DATA },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    check_init_test_source_language(test, "struct S{int x,y;};constexpr struct S s={y:2,x:1};static_assert(s.x+s.y==3);",
                                    NULL, 0, true, false, clang, (CLanguage){ .standard = 2023, .gnu = true });
    static const char* const invalid[] = {
        "struct T{int x;} t={z:1};",
        "struct T{int x;} t={.z=1};",
        "struct T{int x;} t[]={{z:1}};",
        "struct T{int *x;} t={x:1};",
        "int a[2]={x:1};",
        "int a={x:1};",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        check_init_test_source(test, invalid[i], NULL, 0, false, false, clang);
    }
}
