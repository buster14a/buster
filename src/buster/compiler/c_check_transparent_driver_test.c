// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// glibc's socket calls with _GNU_SOURCE (#147), whose address parameters are transparent unions (__SOCKADDR_ARG and
// __CONST_SOCKADDR_ARG): an original program linked by the direct path, by both paths into the same executable, which
// must print what clang's build of it prints. Include after c_checker_driver_test.c's helpers.

static void check_test_driver_sockets(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    // Two datagram sockets the kernel names (autobind, an address of the family alone), each name read back by
    // getsockname, and a datagram sent from one to the other's name by sendto and received with its sender's name by
    // recvfrom: the addresses passed as pointers to the unions' first member's type and to another member's.
    static const char program[] = "#define _GNU_SOURCE\n"
                                  "#include <stdio.h>\n"
                                  "#include <string.h>\n"
                                  "#include <sys/socket.h>\n"
                                  "#include <sys/un.h>\n"
                                  "#include <unistd.h>\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    int a = socket(AF_UNIX, SOCK_DGRAM, 0);\n"
                                  "    int b = socket(AF_UNIX, SOCK_DGRAM, 0);\n"
                                  "    struct sockaddr_un bound = { .sun_family = AF_UNIX };\n"
                                  "    struct sockaddr_un names[2];\n"
                                  "    struct sockaddr_un from;\n"
                                  "    socklen_t lengths[2] = { sizeof(names[0]), sizeof(names[1]) };\n"
                                  "    socklen_t from_length = sizeof(from);\n"
                                  "    char received[4] = { 0 };\n"
                                  "    int failed = a < 0 || b < 0 || bind(a, (struct sockaddr *)&bound, sizeof(sa_family_t)) != 0 ||\n"
                                  "                 bind(b, &bound, sizeof(sa_family_t)) != 0 || getsockname(a, (struct sockaddr *)&names[0], &lengths[0]) != 0 ||\n"
                                  "                 getsockname(b, &names[1], &lengths[1]) != 0 || sendto(b, \"x\", 1, 0, &names[0], lengths[0]) != 1 ||\n"
                                  "                 recvfrom(a, received, sizeof(received), 0, &from, &from_length) != 1;\n"
                                  "    int same = !failed && from_length == lengths[1] && memcmp(&from, &names[1], from_length) == 0;\n"
                                  "    printf(\"%d %d %d %s\\n\", failed, same, !failed && names[0].sun_family == AF_UNIX && lengths[0] > sizeof(sa_family_t), received);\n"
                                  "    close(a);\n"
                                  "    close(b);\n"
                                  "    return failed;\n"
                                  "}\n";
    char source[256];
    char outputs[3][256];
    snprintf(source, sizeof(source), "%s/sockets.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/sockets", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/sockets-scalar", directory);
    snprintf(outputs[2], sizeof(outputs[2]), "%s/sockets-clang", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "socket program written");
    char* text = NULL;
    bool linked = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "glibc's socket calls with _GNU_SOURCE link" : "glibc's socket calls with _GNU_SOURCE link, scalar");
        linked = linked && status == 0;
        free(text);
    }
    check_test_expect(test, linked && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable of socket calls");
    char* const oracle_arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-o", outputs[2], source, NULL };
    const int oracle = written ? check_test_driver_spawn(oracle_arguments, &text) : -1;
    free(text);
    char expected[64] = { 0 };
    char captured[64] = { 0 };
    FILE* output = oracle == 0 ? popen(outputs[2], "r") : NULL;
    const size_t expected_length = output != NULL ? fread(expected, 1, sizeof(expected) - 1, output) : 0;
    const int expected_status = output != NULL ? pclose(output) : -1;
    output = linked ? popen(outputs[0], "r") : NULL;
    const size_t got = output != NULL ? fread(captured, 1, sizeof(captured) - 1, output) : 0;
    const int closed = output != NULL ? pclose(output) : -1;
    check_test_expect(test, expected_status == 0 && strcmp(expected, "0 1 1 x\n") == 0, "clang's build of the socket program sends and receives");
    check_test_expect(test, closed == 0 && got == expected_length && memcmp(captured, expected, got) == 0, "the socket program prints what clang's build prints");
    unlink(source);
    for (uint32_t i = 0; i < 3; i += 1)
    {
        unlink(outputs[i]);
    }
}
