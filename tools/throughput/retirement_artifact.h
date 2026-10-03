/* Independent retirement artifact inspection. This reader never loads, links,
 * relocates or executes an artifact, and shares no compiler object reader.
 * tp_retirement_artifact inspects bounded ELF64, COFF/PE32+ and Mach-O64 bytes;
 * only x86-64/AArch64 little-endian objects and executables are admitted.
 * Code is the concatenation of section payloads in ascending file-offset
 * order. Headers, relocations, debug data and PE file-alignment padding are
 * excluded. Empty code is an explicit fact, not an unavailable observation.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_ARTIFACT_H
#define BUSTER_THROUGHPUT_RETIREMENT_ARTIFACT_H
#include <buster/lib/hash.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TP_RETIREMENT_ARTIFACT_BYTES UINT64_C(1073741824)
#define TP_RETIREMENT_ARTIFACT_SECTIONS 65535u

typedef struct TpRetirementArtifact
{
    uint64_t file_bytes, code_bytes;
    unsigned format, machine, executable, sections;
    char file_sha256[65], code_sha256[65];
} TpRetirementArtifact;

typedef struct TpRetirementArtifactSpan
{
    uint64_t offset, bytes;
    int code;
} TpRetirementArtifactSpan;

typedef struct TpRetirementArtifactReader
{
    unsigned char const* data;
    uint64_t bytes;
    TpRetirementArtifactSpan* spans;
    unsigned count, capacity;
    TpRetirementArtifact facts;
} TpRetirementArtifactReader;

static uint64_t tp_retirement_artifact_uint(unsigned char const* p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint64_t)p[i] << (i * 8);
    return value;
}

static int tp_retirement_artifact_range(TpRetirementArtifactReader const* r, uint64_t offset, uint64_t bytes)
{
    int ok = offset <= r->bytes && bytes <= r->bytes - offset;
    return ok;
}

static int tp_retirement_artifact_span(TpRetirementArtifactReader* r, uint64_t offset, uint64_t bytes, int code)
{
    int ok = tp_retirement_artifact_range(r, offset, bytes) && r->count < r->capacity;
    if (ok && bytes) r->spans[r->count++] = (TpRetirementArtifactSpan){offset, bytes, code};
    return ok;
}

static int tp_retirement_artifact_elf(TpRetirementArtifactReader* r)
{
    unsigned char const* h = r->data;
    int ok = r->bytes >= 64 && !memcmp(h, "\177ELF\2\1\1", 7);
    uint64_t table = 0, programs = 0;
    unsigned count = 0, program_count = 0;
    if (ok)
    {
        unsigned type = (unsigned)tp_retirement_artifact_uint(h + 16, 2);
        unsigned machine = (unsigned)tp_retirement_artifact_uint(h + 18, 2);
        table = tp_retirement_artifact_uint(h + 40, 8);
        programs = tp_retirement_artifact_uint(h + 32, 8);
        count = (unsigned)tp_retirement_artifact_uint(h + 60, 2);
        program_count = (unsigned)tp_retirement_artifact_uint(h + 56, 2);
        ok = type >= 1 && type <= 3 && (machine == 62 || machine == 183) &&
            tp_retirement_artifact_uint(h + 20, 4) == 1 && tp_retirement_artifact_uint(h + 52, 2) == 64 &&
            tp_retirement_artifact_uint(h + 58, 2) == 64 && table >= 64 && count &&
            program_count != 65535 && tp_retirement_artifact_uint(h + 62, 2) < count &&
            tp_retirement_artifact_range(r, table, (uint64_t)count * 64) &&
            (!program_count || (programs >= 64 && tp_retirement_artifact_uint(h + 54, 2) == 56));
        r->facts.format = 1;
        r->facts.machine = machine == 62 ? 1 : 2;
        r->facts.executable = type != 1;
        r->facts.sections = count - 1;
    }
    if (ok) ok = tp_retirement_artifact_span(r, 0, 64, 0) &&
        tp_retirement_artifact_span(r, table, (uint64_t)count * 64, 0) &&
        (!program_count || tp_retirement_artifact_span(r, programs, (uint64_t)program_count * 56, 0));
    for (unsigned i = 0; ok && i < program_count; ++i)
    {
        unsigned char const* p = h + programs + (uint64_t)i * 56;
        uint64_t size = tp_retirement_artifact_uint(p + 32, 8);
        ok = tp_retirement_artifact_range(r, tp_retirement_artifact_uint(p + 8, 8), size) &&
            (tp_retirement_artifact_uint(p, 4) != 1 || size <= tp_retirement_artifact_uint(p + 40, 8));
    }
    for (unsigned i = 0; ok && i < count; ++i)
    {
        unsigned char const* s = h + table + (uint64_t)i * 64;
        uint64_t type = tp_retirement_artifact_uint(s + 4, 4);
        uint64_t flags = tp_retirement_artifact_uint(s + 8, 8);
        uint64_t offset = tp_retirement_artifact_uint(s + 24, 8);
        uint64_t size = tp_retirement_artifact_uint(s + 32, 8);
        uint64_t alignment = tp_retirement_artifact_uint(s + 48, 8);
        int code = (flags & 4) != 0;
        ok = (!alignment || !(alignment & (alignment - 1))) &&
            (!code || (type == 1 && !(flags & UINT64_C(0x800))));
        if (ok && !i)
        {
            for (unsigned j = 0; ok && j < 64; ++j) ok = s[j] == 0;
        }
        else if (ok && type != 8) ok = tp_retirement_artifact_span(r, offset, size, code);
        else if (ok) ok = !code;
    }
    return ok;
}

static int tp_retirement_artifact_coff(TpRetirementArtifactReader* r, int pe)
{
    unsigned char const* data = r->data;
    uint64_t header = 0, table = 0, table_end = 0, headers_end = 0;
    unsigned count = 0;
    int ok = r->bytes >= 20;
    if (ok && pe)
    {
        ok = r->bytes >= 64;
        if (ok)
        {
            header = tp_retirement_artifact_uint(data + 60, 4);
            ok = header >= 64 && tp_retirement_artifact_range(r, header, 24) &&
                !memcmp(data + header, "PE\0\0", 4);
            header += 4;
        }
    }
    if (ok)
    {
        unsigned char const* h = data + header;
        unsigned machine = (unsigned)tp_retirement_artifact_uint(h, 2);
        unsigned optional = (unsigned)tp_retirement_artifact_uint(h + 16, 2);
        count = (unsigned)tp_retirement_artifact_uint(h + 2, 2);
        table = header + 20 + optional;
        table_end = table + (uint64_t)count * 40;
        headers_end = table_end;
        ok = (machine == 0x8664 || machine == 0xaa64) && count &&
            tp_retirement_artifact_range(r, table, (uint64_t)count * 40) &&
            (pe ? optional >= 112 : optional == 0);
        if (ok && pe) ok = tp_retirement_artifact_uint(h + 20, 2) == 0x20b &&
            (tp_retirement_artifact_uint(h + 18, 2) & 2) &&
            tp_retirement_artifact_uint(h + 20 + 60, 4) >= table_end &&
            tp_retirement_artifact_uint(h + 20 + 60, 4) <= r->bytes;
        if (ok && pe) headers_end = tp_retirement_artifact_uint(h + 20 + 60, 4);
        r->facts.format = pe ? 3 : 2;
        r->facts.machine = machine == 0x8664 ? 1 : 2;
        r->facts.executable = pe != 0;
        r->facts.sections = count;
    }
    if (ok) ok = tp_retirement_artifact_span(r, 0, headers_end, 0);
    for (unsigned i = 0; ok && i < count; ++i)
    {
        unsigned char const* s = data + table + (uint64_t)i * 40;
        uint64_t size = tp_retirement_artifact_uint(s + 16, 4);
        uint64_t offset = tp_retirement_artifact_uint(s + 20, 4);
        uint64_t flags = tp_retirement_artifact_uint(s + 36, 4);
        uint64_t virtual_size = pe ? tp_retirement_artifact_uint(s + 8, 4) : 0;
        int code = (flags & UINT64_C(0x20000020)) != 0;
        int zero = !pe && (flags & 0x80) && !offset;
        ok = (!code || !(flags & 0x80)) && (zero || ((!size || offset >= headers_end) &&
            tp_retirement_artifact_range(r, offset, size)));
        uint64_t payload = pe && virtual_size && virtual_size < size ? virtual_size : size;
        if (ok && code && pe) ok = virtual_size <= size;
        if (ok && !zero) ok = tp_retirement_artifact_span(r, offset, payload, code);
        if (ok && !zero && size > payload) ok = tp_retirement_artifact_span(r, offset + payload, size - payload, 0);
        if (ok && !pe)
        {
            uint64_t relocations = tp_retirement_artifact_uint(s + 32, 2);
            uint64_t relocation_offset = tp_retirement_artifact_uint(s + 24, 4);
            if (flags & UINT64_C(0x01000000))
            {
                ok = relocations == 65535 && tp_retirement_artifact_range(r, relocation_offset, 10);
                if (ok)
                {
                    relocations = tp_retirement_artifact_uint(data + relocation_offset, 4);
                    ok = relocations > 65535;
                }
            }
            if (ok && relocations) ok = tp_retirement_artifact_span(r, relocation_offset, relocations * 10, 0);
        }
    }
    if (ok && !pe)
    {
        uint64_t symbols = tp_retirement_artifact_uint(data + header + 8, 4);
        uint64_t symbol_count = tp_retirement_artifact_uint(data + header + 12, 4);
        ok = symbols || !symbol_count;
        if (ok && symbols)
        {
            uint64_t strings = symbols + symbol_count * 18;
            ok = tp_retirement_artifact_span(r, symbols, symbol_count * 18, 0) &&
                tp_retirement_artifact_range(r, strings, 4);
            if (ok)
            {
                uint64_t size = tp_retirement_artifact_uint(data + strings, 4);
                ok = size >= 4 && tp_retirement_artifact_span(r, strings, size, 0);
            }
        }
    }
    return ok;
}

static int tp_retirement_artifact_macho(TpRetirementArtifactReader* r)
{
    unsigned char const* h = r->data;
    uint64_t position = 32, end = 0;
    unsigned commands = 0;
    int ok = r->bytes >= 32 && tp_retirement_artifact_uint(h, 4) == UINT64_C(0xfeedfacf);
    if (ok)
    {
        uint64_t machine = tp_retirement_artifact_uint(h + 4, 4);
        uint64_t type = tp_retirement_artifact_uint(h + 12, 4);
        commands = (unsigned)tp_retirement_artifact_uint(h + 16, 4);
        end = 32 + tp_retirement_artifact_uint(h + 20, 4);
        ok = (machine == 0x01000007 || machine == 0x0100000c) &&
            (type == 1 || type == 2 || type == 6 || type == 8) && commands &&
            commands <= TP_RETIREMENT_ARTIFACT_SECTIONS && end <= r->bytes &&
            tp_retirement_artifact_span(r, 0, end, 0);
        r->facts.format = 4;
        r->facts.machine = machine == 0x01000007 ? 1 : 2;
        r->facts.executable = type != 1;
    }
    for (unsigned i = 0; ok && i < commands; ++i)
    {
        ok = position <= end && end - position >= 8;
        uint64_t size = ok ? tp_retirement_artifact_uint(h + position + 4, 4) : 0;
        if (ok) ok = size >= 8 && !(size & 7) && size <= end - position;
        if (ok && tp_retirement_artifact_uint(h + position, 4) == 0x19)
        {
            ok = size >= 72;
            unsigned count = ok ? (unsigned)tp_retirement_artifact_uint(h + position + 64, 4) : 0;
            if (ok) ok = size == 72 + (uint64_t)count * 80 &&
                count <= TP_RETIREMENT_ARTIFACT_SECTIONS - r->facts.sections &&
                tp_retirement_artifact_range(r, tp_retirement_artifact_uint(h + position + 40, 8),
                                               tp_retirement_artifact_uint(h + position + 48, 8));
            if (ok) r->facts.sections += count;
            for (unsigned j = 0; ok && j < count; ++j)
            {
                unsigned char const* s = h + position + 72 + (uint64_t)j * 80;
                uint64_t flags = tp_retirement_artifact_uint(s + 64, 4);
                unsigned type = (unsigned)(flags & 255);
                int code = (flags & UINT64_C(0x80000400)) != 0 || type == 8;
                int zero = type == 1 || type == 12 || type == 18;
                ok = tp_retirement_artifact_uint(s + 52, 4) < 64 && (!code || !zero);
                if (ok && !zero) ok = tp_retirement_artifact_span(r,
                    tp_retirement_artifact_uint(s + 48, 4), tp_retirement_artifact_uint(s + 40, 8), code);
                uint64_t relocations = tp_retirement_artifact_uint(s + 60, 4);
                if (ok && relocations) ok = tp_retirement_artifact_span(r,
                    tp_retirement_artifact_uint(s + 56, 4), relocations * 8, 0);
            }
        }
        position += size;
    }
    if (ok) ok = position == end && r->facts.sections;
    return ok;
}

/* In-place heapsort has a fixed memory bound and no callback/recursion. */
static void tp_retirement_artifact_sift(TpRetirementArtifactSpan* spans, unsigned root, unsigned count)
{
    while (root < count / 2)
    {
        unsigned child = root * 2 + 1;
        if (child + 1 < count && spans[child].offset < spans[child + 1].offset) ++child;
        if (spans[root].offset >= spans[child].offset) break;
        TpRetirementArtifactSpan value = spans[root];
        spans[root] = spans[child];
        spans[child] = value;
        root = child;
    }
}

static int tp_retirement_artifact(void const* data, uint64_t bytes, TpRetirementArtifact* output)
{
    TpRetirementArtifactReader r = {.data = (unsigned char const*)data, .bytes = bytes,
        .capacity = TP_RETIREMENT_ARTIFACT_SECTIONS * 3 + 8};
    int ok = data && output && bytes >= 4 && bytes <= TP_RETIREMENT_ARTIFACT_BYTES;
    if (ok && bytes / 8 + 8 < r.capacity) r.capacity = (unsigned)(bytes / 8 + 8);
    if (ok) r.spans = (TpRetirementArtifactSpan*)malloc((size_t)r.capacity * sizeof(*r.spans));
    ok = ok && r.spans;
    if (ok)
    {
        if (!memcmp(data, "\177ELF", 4)) ok = tp_retirement_artifact_elf(&r);
        else if (!memcmp(data, "MZ", 2)) ok = tp_retirement_artifact_coff(&r, 1);
        else if (tp_retirement_artifact_uint(r.data, 4) == UINT64_C(0xfeedfacf)) ok = tp_retirement_artifact_macho(&r);
        else ok = tp_retirement_artifact_coff(&r, 0);
    }
    if (ok)
    {
        for (unsigned i = r.count / 2; i; --i) tp_retirement_artifact_sift(r.spans, i - 1, r.count);
        for (unsigned i = r.count; i > 1; --i)
        {
            TpRetirementArtifactSpan value = r.spans[0];
            r.spans[0] = r.spans[i - 1];
            r.spans[i - 1] = value;
            tp_retirement_artifact_sift(r.spans, 0, i - 1);
        }
        Sha256 code;
        sha256_init(&code);
        uint64_t end = 0;
        for (unsigned i = 0; ok && i < r.count; ++i)
        {
            TpRetirementArtifactSpan const* span = r.spans + i;
            ok = span->offset >= end;
            end = span->offset + span->bytes;
            if (ok && span->code)
            {
                sha256_add(&code, r.data + span->offset, span->bytes);
                r.facts.code_bytes += span->bytes;
            }
        }
        if (ok)
        {
            sha256_finish_hex(&code, r.facts.code_sha256);
            Sha256 file;
            sha256_init(&file);
            sha256_add(&file, data, bytes);
            sha256_finish_hex(&file, r.facts.file_sha256);
            r.facts.file_bytes = bytes;
        }
    }
    if (output) *output = ok ? r.facts : (TpRetirementArtifact){0};
    free(r.spans);
    return ok;
}
#endif
