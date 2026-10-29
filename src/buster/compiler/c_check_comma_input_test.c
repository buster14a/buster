// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original guarded oracle for comma contexts, including legal non-source-order traversals used by reader views.

// Actual prefix/repair views are partial original inputs but complete permutations of their private token counts.
// Compare every context, including synthetic closers past a repair's original publication limit.
static void check_test_expression_comma_view(CheckTest* test, const Parser* view, uint32_t original_end)
{
    const uint32_t count = view->tokens.count;
    uint8_t* expected = malloc((uint64_t)count + 1);
    uint8_t* actual = malloc((uint64_t)count + 1);
    uint8_t* seen = calloc((uint64_t)count + 1, 1);
    bool same = expected != NULL && actual != NULL && seen != NULL && view->postorder != NULL && original_end <= count;
    for (uint32_t position = 0; same && position < count; position += 1)
    {
        const uint32_t token = view->postorder[position];
        same = token < count && seen[token] == 0;
        if (same)
        {
            seen[token] = 1;
        }
    }
    if (same)
    {
        memset(expected, 0xa5, (uint64_t)count + 1);
        memset(actual, 0x5a, (uint64_t)count + 1);
        CheckInputReader reader = { .parser = view, .postorder = view->postorder, .expression_commas = expected };
        check_input_expression_commas_scalar(&reader);
        reader.expression_commas = actual;
        if (!check_input_expression_commas_vector(&reader))
        {
            check_input_expression_commas_scalar(&reader);
        }
        same = memcmp(expected, actual, count) == 0 && expected[count] == 0xa5 && actual[count] == 0x5a;
    }
    check_test_expect(test, same, "actual prefix/repair comma contexts equal the original full-view postorder pass, including tokens past original_end");
    free(seen);
    free(actual);
    free(expected);
}

static void check_test_expression_commas(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[5];
    bool ready = page >= 258 * sizeof(uint32_t);
    for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(mappings); column += 1)
    {
        mappings[column] = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[column] != MAP_FAILED && mprotect(mappings[column] + page, page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "comma-context guarded columns are available");
    CheckTest random = { .random = 0x529c0a };
    for (uint32_t count = 0; ready && count <= 257; count += 1)
    {
        uint8_t* roles = mappings[0] + page - count;
        uint32_t* parents = (uint32_t*)(mappings[1] + page) - count;
        uint32_t* postorder = (uint32_t*)(mappings[2] + page) - count;
        uint8_t* expected = mappings[3] + page - count;
        uint8_t* actual = mappings[4] + page - count;
        Parser parser = { .tokens = { .count = count }, .tree = { .roles = roles, .parents = parents } };
        CheckInputReader reader = { .parser = &parser, .postorder = postorder, .expression_commas = expected };
        for (uint32_t shape = 0; shape < 6; shape += 1)
        {
            for (uint32_t position = 0; position < count; position += 1)
            {
                postorder[position] = shape == 2 ? count - position - 1 : position;
            }
            if (shape >= 3)
            {
                for (uint32_t position = count; position > 1; position -= 1)
                {
                    const uint32_t other = (uint32_t)(check_test_random(&random) % position);
                    const uint32_t token = postorder[position - 1];
                    postorder[position - 1] = postorder[other];
                    postorder[other] = token;
                }
            }
            for (uint32_t position = 0; position < count; position += 1)
            {
                const uint32_t token = postorder[position];
                const uint32_t left = count - position - 1;
                const uint32_t next = position + 1 + (left != 0 && shape >= 3 ? (uint32_t)(check_test_random(&random) % left) : 0);
                parents[token] = next < count ? postorder[next] : shape == 4 ? UINT32_MAX : count;
                roles[token] = shape < 3 ? PARSE_ROLE_SEPARATOR : shape == 3 ? (uint8_t)check_test_random(&random) :
                               shape == 4 ? (uint8_t)(token % PARSE_ROLE_COUNT) :
                               token % 3 == 0 ? PARSE_ROLE_TERMINATOR : token % 3 == 1 ? PARSE_ROLE_CLOSE_FOR : PARSE_ROLE_SEPARATOR;
            }
            if (count != 0 && shape < 3)
            {
                roles[postorder[count - 1]] = shape == 1 ? PARSE_ROLE_CLOSE_CALL : PARSE_ROLE_CLOSE_GROUP;
            }
            bool fallback = false;
            for (uint32_t token = 0; token < count; token += 1)
            {
                const uint32_t parent = parents[token];
                fallback |= roles[token] == PARSE_ROLE_SEPARATOR && parent < count && parent <= token && roles[parent] == PARSE_ROLE_SEPARATOR;
            }
            memset(expected, 0xa5, count);
            expected[-1] = 0x6d;
            reader.expression_commas = expected;
            check_input_expression_commas_scalar(&reader);
            memset(actual, 0x5a, count);
            actual[-1] = 0x3b;
            reader.expression_commas = actual;
            const bool fast = check_input_expression_commas_vector(&reader);
            if (!fast)
            {
                check_input_expression_commas_scalar(&reader);
            }
            bool same = fast == !fallback && memcmp(expected, actual, count) == 0 && expected[-1] == 0x6d && actual[-1] == 0x3b;
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                parser.options.scalar = scalar != 0;
                memset(actual, 0x5a, count);
                check_input_expression_commas(&reader);
                same &= memcmp(expected, actual, count) == 0 && actual[-1] == 0x3b;
            }
            check_test_expect(test, same, "comma contexts match the original postorder oracle at guarded tails, including backward dependencies and unknown roles");
        }
    }
    for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(mappings); column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], page * 2);
        }
    }
    enum { COUNT = 65536 };
    uint8_t* roles = malloc(COUNT);
    uint32_t* parents = malloc(COUNT * sizeof(uint32_t));
    uint32_t* postorder = malloc(COUNT * sizeof(uint32_t));
    uint8_t* actual = malloc(COUNT);
    ready = roles != NULL && parents != NULL && postorder != NULL && actual != NULL;
    bool same = ready;
    for (uint32_t backward = 0; ready && backward < 2; backward += 1)
    {
        for (uint32_t token = 0; token < COUNT; token += 1)
        {
            roles[token] = PARSE_ROLE_SEPARATOR;
            parents[token] = backward != 0 ? token != 0 ? token - 1 : COUNT : token + 1;
            postorder[token] = backward != 0 ? COUNT - token - 1 : token;
        }
        roles[backward != 0 ? 0 : COUNT - 1] = PARSE_ROLE_CLOSE_GROUP;
        Parser parser = { .tokens = { .count = COUNT }, .tree = { .roles = roles, .parents = parents } };
        CheckInputReader reader = { .parser = &parser, .postorder = postorder, .expression_commas = actual };
        check_input_expression_commas(&reader);
        for (uint32_t token = 0; token < COUNT; token += 1)
        {
            same &= actual[token] == 1;
        }
    }
    check_test_expect(test, same, "deep forward separator inheritance and backward postorder fallback retain every context without a recursive walk");
    free(actual);
    free(postorder);
    free(parents);
    free(roles);
}
