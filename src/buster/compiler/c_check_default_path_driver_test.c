// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2850's default path: three rules answer early what a longer way answered (CheckDefaultPathCount, c_check_input.c). A
// logical operator's right operand that can only be 0 or 1 is not replayed for -Wconstant-logical-operand; a comparison
// no check of the CFG's can decide is answered before its operands' common type is worked out; a constant's read of a
// const object keeps its no by node. With BUSTER_TEST_DEFAULT_PATH_LOG set to a path each early answer is also worked
// out the long way where it is given, and the command writes a line of counts there.
//
// The unit below has each rule's cases and the cases beside them that the rule must leave: the line's counts of what
// differs are 0, its object and its diagnostics are those of a compile without the switch, and the warning is still
// given where the right operand is a constant that is neither 0 nor 1.
static const char check_driver_default_path_unit[] =
    "static const int limit = 3;\n"
    "int f(int a, int b, int c)\n"
    "{\n"
    "    int r = 0;\n"
    // The right operand a comparison, a `!` and a logical operator: none is replayed.
    "    if (a && b < c) { r += 1; }\n"
    "    if (a || !b) { r += 2; }\n"
    "    if (a && (b || c)) { r += 4; }\n"
    // A plain value, and a sum of two literals: replayed, and the second is warned of.
    "    if (a && b) { r += 8; }\n"
    "    if (a && (1 + 1)) { r += 16; }\n"
    // A comparison no check can decide, by each check; and four that one decides: the equality's by a `&` and by a known
    // boolean beside a literal over 1, the relational's by the same, each with the literal on either side.
    "    if (a == 3) { r += 32; }\n"
    "    if (b > 10) { r += 64; }\n"
    "    if ((a & 8) == 4) { r += 128; }\n"
    "    if ((a < b) == 2) { r += 256; }\n"
    "    if (2 == (a < b)) { r += 512; }\n"
    "    if ((a < b) > 2) { r += 1024; }\n"
    "    if (2 < (a < b)) { r += 2048; }\n"
    // A condition that is no constant, which each pass asks again; and a const object's read, which is one.
    "    if (c) { r += 4096; }\n"
    "    if (limit) { r += 8192; }\n"
    "    if (limit == 3 && c) { r += 16384; }\n"
    "    return r;\n"
    "}\n";

static void check_test_driver_default_path(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    (void)clang;
    char source[512];
    char plain[512];
    char checked[512];
    char log[512];
    snprintf(source, sizeof(source), "%s/default-path.c", directory);
    snprintf(plain, sizeof(plain), "%s/default-path.o", directory);
    snprintf(checked, sizeof(checked), "%s/default-path-checked.o", directory);
    snprintf(log, sizeof(log), "%s/default-path.log", directory);
    // The test's switch is the environment's: what it held is taken off for the compiles here and put back.
    const char* outer = getenv("BUSTER_TEST_DEFAULT_PATH_LOG");
    char* restored = outer != NULL ? strdup(outer) : NULL;
    unsetenv("BUSTER_TEST_DEFAULT_PATH_LOG");
    const bool written = parse_test_write(source, check_driver_default_path_unit, sizeof(check_driver_default_path_unit) - 1) && parse_test_write(log, "", 0);
    char* plain_arguments[] = { (char*)(executable != NULL ? executable : "checker"), "--lanes", "1", "-std=gnu17", "-c", "-o", plain, source, NULL };
    char* checked_arguments[] = { (char*)(executable != NULL ? executable : "checker"), "--lanes", "1", "-std=gnu17", "-c", "-o", checked, source, NULL };
    char* plain_text = NULL;
    char* checked_text = NULL;
    const int plain_status = written ? check_test_driver_buster_command(plain_arguments, &plain_text, false, executable != NULL) : -1;
    setenv("BUSTER_TEST_DEFAULT_PATH_LOG", log, 1);
    const int checked_status = written ? check_test_driver_buster_command(checked_arguments, &checked_text, false, executable != NULL) : -1;
    unsetenv("BUSTER_TEST_DEFAULT_PATH_LOG");
    // The warning, once, at the sum of two literals, with the switch and without.
    const char* warned = plain_text != NULL ? strstr(plain_text, "use of logical '&&' with constant operand") : NULL;
    const bool once = warned != NULL && strstr(warned + 1, "use of logical '&&' with constant operand") == NULL && strstr(plain_text, "default-path.c:9:") != NULL;
    check_test_expect(test, plain_status == 0 && once, "default-path: a right operand that is a constant neither 0 nor 1 is warned of, and no other");
    // The same object and the same diagnostics with the switch: it changes no answer.
    const bool same = plain_status == 0 && checked_status == 0 && check_test_driver_node_bodies_same(plain, checked) && plain_text != NULL && checked_text != NULL &&
                      strcmp(plain_text, checked_text) == 0;
    check_test_expect(test, same, "default-path: with the test's switch the object and the diagnostics are the same");
    // The line: three operands not replayed, none an event and none a type's maker; comparisons answered early and none
    // of them otherwise by its check; kept answers read and none of them otherwise by the walk.
    char line[1024] = { 0 };
    FILE* file = written ? fopen(log, "rb") : NULL;
    const size_t length = file != NULL ? fread(line, 1, sizeof(line) - 1, file) : 0;
    if (file != NULL)
    {
        fclose(file);
    }
    line[length] = 0;
    unsigned long long skipped = 0;
    unsigned long long events = 1;
    unsigned long long made = 1;
    unsigned long long early = 0;
    unsigned long long early_different = 1;
    unsigned long long kept = 0;
    unsigned long long kept_different = 1;
    const bool counted = sscanf(line,
                                "default-path: logical-operands-not-replayed=%llu of-them-an-event=%llu of-them-a-type-made=%llu comparisons-answered-early=%llu "
                                "of-them-different=%llu constants'-kept-answers-read=%llu of-them-different=%llu",
                                &skipped, &events, &made, &early, &early_different, &kept, &kept_different) == 7;
    const bool held = counted && skipped == 3 && events == 0 && made == 0 && early >= 2 && early_different == 0 && kept >= 1 && kept_different == 0;
    check_test_expect(test, held, "default-path: each rule's early answers are the long way's");
    if (!(plain_status == 0 && once) || !same || !held)
    {
        fprintf(stderr, "test-checker-driver: default-path: status %d and %d with the switch\n%s%s----\n%s", plain_status, checked_status, line,
                plain_text != NULL ? plain_text : "", checked_text != NULL ? checked_text : "");
    }
    free(plain_text);
    free(checked_text);
    if (restored != NULL)
    {
        setenv("BUSTER_TEST_DEFAULT_PATH_LOG", restored, 1);
    }
    free(restored);
    unlink(source);
    unlink(plain);
    unlink(checked);
    unlink(log);
}
