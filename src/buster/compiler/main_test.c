// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original loader regressions, included by main.c and run by the existing test-lexer command.
// A child limits its address space to fail each source/token mapping in turn, without hooks in production code.
// Successful results must release the source and all output mappings; failed results own nothing.

#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>

static uint64_t main_test_mapped_pages(void)
{
    char text[64];
    const int fd = open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    ssize_t length = -1;
    if (fd >= 0)
    {
        do
        {
            length = read(fd, text, sizeof(text));
        } while (length < 0 && errno == EINTR);
        close(fd);
    }
    uint64_t result = 0;
    for (ssize_t index = 0; index < length && text[index] >= '0' && text[index] <= '9'; index += 1)
    {
        result = result * 10 + (uint64_t)(text[index] - '0');
    }
    return result;
}

static bool main_test_mapping_failures(String8 path)
{
    const pid_t child = fork();
    bool result = child >= 0;
    if (child == 0)
    {
        // Warm the loader and lexer's deepest stack before accounting for the child's mapped pages.
        PrimitiveTokenizeResult warm = primitive_tokenize_file(path);
        bool valid = !warm.error;
        primitive_tokenize_release(&warm);
        const long page_bytes = sysconf(_SC_PAGESIZE);
        const uint64_t pages = main_test_mapped_pages();
        struct rlimit limit;
        valid = valid && page_bytes >= 4 * BUSTER_C_TOKENIZER_CAPACITY(6) && pages != 0 && getrlimit(RLIMIT_AS, &limit) == 0;
        const char* operations[] =
        {
            "mmap source",
            "mmap token kinds",
            "mmap token starts",
            "mmap token ends",
        };
        const int before = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (before >= 0)
        {
            close(before);
        }
        valid = valid && before >= 0;
        // The six-byte file and each of its token arrays occupy one page. Allow precisely the preceding mappings.
        for (uint32_t index = 0; valid && index < BUSTER_ARRAY_LENGTH(operations); index += 1)
        {
            limit.rlim_cur = (pages + index) * (uint64_t)page_bytes;
            valid = limit.rlim_cur <= limit.rlim_max && setrlimit(RLIMIT_AS, &limit) == 0;
            if (valid)
            {
                PrimitiveTokenizeResult tokens = primitive_tokenize_file(path);
                const int after = open("/dev/null", O_RDONLY | O_CLOEXEC);
                valid = tokens.error && tokens.error_number == ENOMEM && tokens.token_count == 0
                    && tokens.operation != 0 && strcmp(tokens.operation, operations[index]) == 0
                    && tokens.content.pointer == 0 && tokens.kinds == 0 && tokens.starts == 0 && tokens.ends == 0
                    && after == before
                    && main_test_mapped_pages() == pages;
                if (after >= 0)
                {
                    close(after);
                }
                primitive_tokenize_release(&tokens);
            }
        }
        _exit(valid ? 0 : 1);
    }
    else if (child > 0)
    {
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        result = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    return result;
}

static int run_main_tests(void)
{
    char name[] = "/tmp/buster-loader-XXXXXX";
    const int fd = mkstemp(name);
    const String8 path =
    {
        .pointer = name,
        .length = strlen(name),
    };
    uint32_t failures = fd < 0;
    if (fd >= 0)
    {
        PrimitiveTokenizeResult empty = primitive_tokenize_file(path);
        failures += empty.error || empty.token_count != 0 || empty.content.length != 0 || empty.owns_content;
        primitive_tokenize_release(&empty);
        const bool written = write(fd, "int x;", 6) == 6;
        failures += !written;
        const int before = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (before >= 0)
        {
            close(before);
        }
        for (uint32_t iteration = 0; written && iteration < 16; iteration += 1)
        {
            PrimitiveTokenizeResult tokens = primitive_tokenize_file(path);
            const bool valid = !tokens.error && tokens.token_count == 3 && tokens.owns_content;
            failures += !valid;
            void* mappings[] =
            {
                tokens.content.pointer,
                tokens.kinds,
                tokens.starts,
                tokens.ends,
            };
            primitive_tokenize_release(&tokens);
            for (uint32_t index = 0; valid && index < BUSTER_ARRAY_LENGTH(mappings); index += 1)
            {
                unsigned char residency;
                failures += mincore(mappings[index], 1, &residency) != -1 || errno != ENOMEM;
            }
            failures += tokens.content.pointer != 0 || tokens.kinds != 0 || tokens.starts != 0 || tokens.ends != 0 || tokens.token_count != 0;
            primitive_tokenize_release(&tokens);
        }
        const int after = open("/dev/null", O_RDONLY | O_CLOEXEC);
        failures += before < 0 || after != before;
        if (after >= 0)
        {
            close(after);
        }
        failures += written && !main_test_mapping_failures(path);
        failures += close(fd) != 0;
        failures += unlink(name) != 0;
        PrimitiveTokenizeResult missing = primitive_tokenize_file(path);
        failures += !missing.error || missing.error_number != ENOENT || missing.token_count != 0
            || missing.operation == 0 || strcmp(missing.operation, "open") != 0;
        primitive_tokenize_release(&missing);
    }
    const String8 oversized =
    {
        .length = UINT32_MAX,
    };
    PrimitiveTokenizeResult too_big = primitive_tokenize_chunk(oversized);
    failures += !too_big.error || too_big.error_number != EFBIG || too_big.token_count != 0 || too_big.kinds != 0;
    primitive_tokenize_release(&too_big);
    printf("main loader: %u failures\n", failures);
    return failures == 0 ? 0 : 1;
}
