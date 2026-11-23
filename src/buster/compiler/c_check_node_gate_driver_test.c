// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The node machine's producer against a unit's diagnostics (#2850, c_check_node_bodies.c): with
// BUSTER_TEST_NODE_BODIES_LOG set to a path, a compile runs the producer beside the unit's ordinary read, nothing masked,
// and writes a line for each diagnostic at a token of a body the producer takes. Each unit here has a body whose
// expression the checker rejects and one that says nothing: the producer takes the second alone, and the log has no
// such line.
//
// The first four are an update of an array's lvalue, which converts to a pointer as a value and is no modifiable lvalue:
// T's rule for a pointer's update asks the destination's own type (node_machine_update_pointer_rule). The reader reports
// them only after the producer has decided, where the unit is compiled to an object.
static const char* const check_driver_node_gate_units[] = {
    "void f(void) { int a[2]; a++; }\nint beside(int x) { return x + 2; }\n",
    "void f(void) { int a[2]; --a; }\nint beside(int x) { return x + 2; }\n",
    "void f(void) { int a[2]; a += 1; }\nint beside(int x) { return x + 2; }\n",
    "void f(void) { int a[2][2]; a[0]++; }\nint beside(int x) { return x + 2; }\n",
    "struct S { int m; } gs;\nvoid f(void) { gs++; }\nint beside(int x) { return x + 2; }\n",
    "int g;\nvoid f(void) { (g + 1)++; }\nint beside(int x) { return x + 2; }\n",
};

static void check_test_driver_node_gate(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    (void)clang;
    char source[512];
    char object[512];
    char log[512];
    snprintf(source, sizeof(source), "%s/node-gate.c", directory);
    snprintf(object, sizeof(object), "%s/node-gate.o", directory);
    snprintf(log, sizeof(log), "%s/node-gate.log", directory);
    // The tests' switches are the environment's: what they held is taken off for the compiles here and put back.
    const char* outer_log = getenv("BUSTER_TEST_NODE_BODIES_LOG");
    const char* outer_forced = getenv("BUSTER_TEST_NODE_BODIES");
    char* restored_log = outer_log != NULL ? strdup(outer_log) : NULL;
    char* restored_forced = outer_forced != NULL ? strdup(outer_forced) : NULL;
    unsetenv("BUSTER_TEST_NODE_BODIES");
    for (uint32_t u = 0; u < BUSTER_ARRAY_LENGTH(check_driver_node_gate_units); u += 1)
    {
        const char* text = check_driver_node_gate_units[u];
        const bool written = parse_test_write(source, text, strlen(text)) && parse_test_write(log, "", 0);
        char* arguments[] = { (char*)(executable != NULL ? executable : "checker"), "--lanes", "1", "-std=gnu17", "-c", "-o", object, source, NULL };
        char* diagnostics = NULL;
        setenv("BUSTER_TEST_NODE_BODIES_LOG", log, 1);
        const int status = written ? check_test_driver_buster_command(arguments, &diagnostics, false, executable != NULL) : 0;
        unsetenv("BUSTER_TEST_NODE_BODIES_LOG");
        // The log's line of the unit: the producer ran, took one body of two, and no diagnostic is at a token of it.
        char line[1024] = { 0 };
        FILE* file = written ? fopen(log, "rb") : NULL;
        const size_t length = file != NULL ? fread(line, 1, sizeof(line) - 1, file) : 0;
        if (file != NULL)
        {
            fclose(file);
        }
        line[length] = 0;
        const bool held = written && status != 0 && diagnostics != NULL && strstr(diagnostics, "error:") != NULL && strstr(line, "producer-ran=1 bodies=2 taken=1 ") != NULL &&
                          strstr(line, " in-a-taken-body=0 ") != NULL && strstr(line, "IN-A-TAKEN-BODY") == NULL;
        check_test_expect(test, held, "node-gate: a body whose expression the checker rejects is not the producer's, and no diagnostic is at a token of one it takes");
        if (!held)
        {
            fprintf(stderr, "test-checker-driver: node-gate: unit %u: status %d\n%s%s", u, status, line, diagnostics != NULL ? diagnostics : "");
        }
        free(diagnostics);
    }
    if (restored_log != NULL)
    {
        setenv("BUSTER_TEST_NODE_BODIES_LOG", restored_log, 1);
    }
    if (restored_forced != NULL)
    {
        setenv("BUSTER_TEST_NODE_BODIES", restored_forced, 1);
    }
    free(restored_log);
    free(restored_forced);
    unlink(source);
    unlink(object);
    unlink(log);
}
