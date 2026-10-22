// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Raw binary command-line adapter. Initialization retains compact lookup storage; decoding itself allocates
// nothing and performs no I/O. The command buffers complete lines and reports delayed stdout failures too.

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
        else if (path == 0 && strncmp(argv[i], "--", 2) != 0)
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
    valid = valid && bytes != MAP_FAILED && x86_disassembler_initialize();
    if (fd >= 0)
    {
        close(fd);
    }
    if (valid && assembly)
    {
        valid = printf(".intel_syntax noprefix\n.arch .mpx\n.arch .amx_transpose\n.arch .amx_tf32\n.text\n") >= 0;
    }
    uint64_t consumed = 0;
    X86DecodeStatus status = X86_DECODE_OK;
    while (valid && consumed < size)
    {
        X86Decoded decoded[1024];
        const X86DecodeResult result = scalar ? x86_disassemble_scalar(bytes + consumed, size - consumed, decoded, BUSTER_ARRAY_LENGTH(decoded)) : x86_disassemble(bytes + consumed, size - consumed, decoded, BUSTER_ARRAY_LENGTH(decoded));
        char text[64 * 1024];
        uint32_t used = 0;
        for (uint64_t i = 0; i < result.count && valid; i += 1)
        {
            const int64_t length = assembly ? (scalar ? x86_disassembly_format_scalar(&decoded[i], text + used, sizeof(text) - used - 1) : x86_disassembly_format(&decoded[i], text + used, sizeof(text) - used - 1)) :
                                             x86_disassembly_listing(&decoded[i], bytes + consumed, (uint32_t)consumed, text + used, sizeof(text) - used - 1, scalar);
            valid = length >= 0;
            if (valid)
            {
                used += (uint32_t)length;
                text[used] = '\n';
                used += 1;
                if (sizeof(text) - used < 1024 || i + 1 == result.count)
                {
                    valid = fwrite(text, 1, used, stdout) == used;
                    used = 0;
                }
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
    const bool flushed = fflush(stdout) == 0;
    valid = valid && flushed && !ferror(stdout);
    if (!valid)
    {
        fprintf(stderr, "disassemble: %s at byte 0x%llx; usage: disassemble [--scalar] [--assembly] raw-binary\n", status == X86_DECODE_TRUNCATED ? "truncated instruction" : status == X86_DECODE_UNSUPPORTED ? "unsupported instruction or prefix sequence" : "input or formatting failure", (unsigned long long)consumed);
    }
    return valid ? 0 : 1;
}
