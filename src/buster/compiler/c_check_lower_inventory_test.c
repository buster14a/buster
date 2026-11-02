// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-lowering-templates' checks (#1048), on an original fixture: the owners' lowering gives the plain lowering's
// entries (lower_inventory_file fails the file otherwise), every entry has an owner and every node's pattern accounts
// for its subtree (no stray entry), the exceptions are the fixture's (one call, one && and one ?:), every ordinary key
// is deterministic, a local's read is the one entry LOCAL of its class, an operand's conversion is its node's ('V'), and
// the command refuses an unknown category and a missing file. Include after c_check_lower_inventory.c; test-checker
// calls this through its forward declaration.
#ifndef BUSTER_C_CHECK_LOWER_INVENTORY_TEST_INCLUDED
#define BUSTER_C_CHECK_LOWER_INVENTORY_TEST_INCLUDED

// Whether the inventory has a deterministic key with op `op` and class `class` whose pattern is `pattern`'s `length`
// bytes.
static bool lower_inventory_test_has(const LowerInventory* inventory, CheckOp op, uint8_t class, const uint8_t* pattern, uint32_t length)
{
    bool found = false;
    for (uint32_t i = 0; !found && i < BUSTER_LOWER_INVENTORY_TABLE; i += 1)
    {
        const LowerInventoryKey* entry = inventory->keys + i;
        found = entry->used && entry->key[0] == (uint8_t)op && entry->key[2] == class && entry->pattern_count == 1 && entry->other_patterns == 0 &&
                entry->pattern_lengths[0] == length && memcmp(entry->patterns[0], pattern, length) == 0;
    }
    return found;
}

static __attribute__((noinline)) void check_test_lowering_inventory(CheckTest* test)
{
    static const char fixture[] =
        "struct point { int x; int y; long z; };\n"
        "static int counter;\n"
        "int f(int a);\n"
        "int g(struct point* p, int n, unsigned char c)\n"
        "{\n"
        "    int i = 0;\n"
        "    long sum = 0;\n"
        "    for (i = 0; i < n; i++)\n"
        "    {\n"
        "        sum += p[i].x + p->y * c;\n"
        "    }\n"
        "    counter = counter + 1;\n"
        "    (void)f(n);\n"
        "    return i < n && c != 0 ? (int)sum : -1;\n"
        "}\n"
        "int f(int a) { return a; }\n";
    char directory[] = "/tmp/buster-lowering-templates-XXXXXX";
    char path[256];
    bool ready = mkdtemp(directory) != 0;
    if (ready)
    {
        snprintf(path, sizeof(path), "%s/fixture.c", directory);
        ready = parse_test_write(path, fixture, strlen(fixture));
    }
    check_test_expect(test, ready, "lowering-templates fixture is written");
    LowerInventory inventory = { .keys = ready ? calloc(BUSTER_LOWER_INVENTORY_TABLE, sizeof(LowerInventoryKey)) : 0 };
    char* options[] = { "-nostdinc" };
    // The report goes to stdout: the test's own, which the runner keeps for a failure.
    const bool ran = inventory.keys != 0 && lower_inventory_file(&inventory, path, options, BUSTER_ARRAY_LENGTH(options));
    check_test_expect(test, ran && inventory.files == 1 && inventory.failures == 0 && inventory.bodies == 2,
                      "lowering-templates lowers the fixture with owners, as the plain lowering does");
    uint64_t nodes = 0;
    for (uint32_t c = 0; c < LOWER_INVENTORY_CATEGORIES; c += 1)
    {
        nodes += inventory.category_nodes[c];
    }
    check_test_expect(test, ran && inventory.entries == inventory.entries_nodes + inventory.entries_conversions + inventory.entries_controls + inventory.entries_tokens &&
                                inventory.strays == 0 && inventory.long_patterns == 0 && nodes == inventory.node_count && nodes == inventory.pops,
                      "lowering-templates gives every entry an owner and every node's subtree its entries");
    check_test_expect(test, ran && inventory.category_nodes[LOWER_INVENTORY_CALL] == 1 && inventory.category_nodes[LOWER_INVENTORY_LOGICAL] == 2 &&
                                inventory.category_nodes[LOWER_INVENTORY_BIT_FIELD] == 0 && inventory.category_nodes[LOWER_INVENTORY_TRACKED] == 0,
                      "lowering-templates finds the fixture's exceptions: one call, one && and one ?:");
    bool deterministic = ran;
    for (uint32_t i = 0; deterministic && i < BUSTER_LOWER_INVENTORY_TABLE; i += 1)
    {
        deterministic = !inventory.keys[i].used || (inventory.keys[i].pattern_count == 1 && inventory.keys[i].other_patterns == 0);
    }
    check_test_expect(test, deterministic, "lowering-templates finds every ordinary key of the fixture deterministic");
    // `i` read as an int: LOCAL of I32, its one entry. `p->y * c`'s `c` (unsigned char) converted to int: LOCAL of U8, then
    // its conversion ('V') to I32.
    const uint8_t local[] = { 'K', BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32 };
    const uint8_t widened[] = { 'K', BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U8, 'V', BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32 };
    check_test_expect(test, ran && lower_inventory_test_has(&inventory, CHECK_OP_VALUE, BUSTER_TYPED_CLASS_I32, local, sizeof(local)),
                      "lowering-templates' pattern of a local's read is its LOCAL");
    check_test_expect(test, ran && lower_inventory_test_has(&inventory, CHECK_OP_VALUE, BUSTER_TYPED_CLASS_U8, widened, sizeof(widened)),
                      "lowering-templates gives an operand's conversion to the operand's node");
    free(inventory.node_own);
    free(inventory.node_parents);
    free(inventory.node_keys);
    free(inventory.keys);
    // The command refuses a category it doesn't know and a run without a file.
    char* unknown[] = { "--allow", "nothing", path };
    char* missing[] = { "--top", "1" };
    fflush(stdout);
    const int saved = dup(STDERR_FILENO);
    const int null = open("/dev/null", O_WRONLY);
    const bool quiet = saved >= 0 && null >= 0 && dup2(null, STDERR_FILENO) >= 0;
    const int refused = quiet ? run_lowering_inventory(BUSTER_ARRAY_LENGTH(unknown), unknown) : 0;
    const int empty = quiet ? run_lowering_inventory(BUSTER_ARRAY_LENGTH(missing), missing) : 0;
    if (saved >= 0)
    {
        dup2(saved, STDERR_FILENO);
        close(saved);
    }
    if (null >= 0)
    {
        close(null);
    }
    check_test_expect(test, quiet && refused == 1 && empty == 1, "measure-lowering-templates refuses an unknown category and no file");
    if (ready)
    {
        unlink(path);
        rmdir(directory);
    }
}

#endif
