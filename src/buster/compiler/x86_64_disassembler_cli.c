// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Raw binary command-line adapter; the core decoder does no allocation or I/O.

static int run_x86_disassembler(int argc, char** argv)
{
    bool scalar = false;
    bool assembly = false;
    const char* path = 0;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--scalar") == 0)
        {
            scalar = true;
        }
        else if (strcmp(argv[i], "--assembly") == 0)
        {
            assembly = true;
        }
        else if (path == 0)
        {
            path = argv[i];
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && path != 0;
    int fd = valid ? open(path, O_RDONLY | O_CLOEXEC) : -1;
    struct stat info;
    valid = valid && fd >= 0 && fstat(fd, &info) == 0 && info.st_size >= 0 && (uint64_t)info.st_size <= UINT32_MAX;
    const uint64_t size = valid ? (uint64_t)info.st_size : 0;
    uint8_t* bytes = valid && size != 0 ? mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0) : 0;
    valid = valid && bytes != MAP_FAILED && x86_encoder_initialize() && x86_encoder_initialize_address32() && x86_disassembler_initialize();
    if (fd >= 0)
    {
        close(fd);
    }
    if (valid && assembly)
    {
        printf(".intel_syntax noprefix\n.text\n");
    }
    uint64_t consumed = 0;
    X86DecodeStatus status = X86_DECODE_OK;
    while (valid && consumed < size)
    {
        X86Decoded decoded[1024];
        const X86DecodeResult result = scalar ? x86_disassemble_scalar(bytes + consumed, size - consumed, decoded, BUSTER_ARRAY_LENGTH(decoded)) : x86_disassemble(bytes + consumed, size - consumed, decoded, BUSTER_ARRAY_LENGTH(decoded));
        for (uint64_t i = 0; i < result.count && valid; i += 1)
        {
            char text[512];
            valid = x86_disassembly_format(&decoded[i], text, sizeof(text)) >= 0;
            if (valid)
            {
                if (!assembly)
                {
                    printf("%08llx  ", (unsigned long long)(consumed + decoded[i].offset));
                    for (uint32_t j = 0; j < 15; j += 1)
                    {
                        if (j < decoded[i].length)
                        {
                            printf("%02x ", bytes[consumed + decoded[i].offset + j]);
                        }
                        else
                        {
                            printf("   ");
                        }
                    }
                    printf(" ");
                }
                printf("%s\n", text);
            }
        }
        consumed += result.consumed;
        status = result.status;
        valid = valid && (status == X86_DECODE_OK || status == X86_DECODE_OUTPUT_FULL);
    }
    if (bytes != 0 && bytes != MAP_FAILED)
    {
        munmap(bytes, size);
    }
    if (!valid)
    {
        fprintf(stderr, "disassemble: %s at byte 0x%llx; usage: disassemble [--scalar] [--assembly] raw-binary\n", status == X86_DECODE_TRUNCATED ? "truncated instruction" : status == X86_DECODE_UNSUPPORTED ? "unsupported instruction or prefix sequence" : "input or formatting failure", (unsigned long long)consumed);
    }
    return valid && !ferror(stdout) ? 0 : 1;
}
