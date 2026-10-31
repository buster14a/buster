// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// buster-dominators-v1, the text in which `test-ir-source --dominators-cfg` exports the CFGs it qualified and
// `benchmark-dominators --cfg` reads them back (#174). Include after dominators.c. dominators_export_begin,
// dominators_export_cfg and dominators_export_complete write it; dominators_export_read parses a whole export.
//
//   buster-dominators-v1
//   cfg FORM BLOCKS EDGES ENTRY      a record per CFG: the producer's form number, the counts and the entry block,
//   file HEX                         BUSTER_DOMINATOR_NONE for an empty CFG; then the source file's and the
//   name HEX                         function's names as lowercase hex, so that no byte of a path is ambiguous;
//   SOURCE TARGET                    EDGES successor edges in CSR order, sources ascending;
//   end
//   complete FILES FUNCTIONS FORMS   last, written only after every file, function and form has passed
//
// Decimals have no sign and no leading zero. The reader takes exactly that text: a missing completion record, a
// completion count other than the records', an edge out of range or out of order, or any other byte refuses the
// whole export. It rebuilds each CFG's predecessor view as the stable transpose of its successors, sorted by source,
// as ir_build_cfg builds it. This is measurement tooling outside the compile path, with no vector path.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define BUSTER_DOMINATOR_EXPORT_HEADER "buster-dominators-v1\n"

typedef struct DominatorExportGraph DominatorExportGraph;
struct DominatorExportGraph
{
    DominatorCfg cfg;
    // The producer's representation: test-ir-source numbers lowered, vector-promoted and scalar-promoted IR 0 to 2.
    uint32_t form;
    // The decoded names: bytes, not NUL-terminated.
    uint32_t file_length;
    uint32_t name_length;
    const uint8_t* file;
    const uint8_t* name;
};

typedef struct DominatorExport DominatorExport;
struct DominatorExport
{
    DominatorExportGraph* graphs;
    uint32_t count;
    // The completion record's counts of files and functions; its count of forms is the records'.
    uint64_t files;
    uint64_t functions;
    uint64_t blocks;
    uint64_t edges;
};

static bool dominators_export_hex(FILE* stream, const uint8_t* bytes, uint64_t length)
{
    static const char digits[] = "0123456789abcdef";
    bool valid = true;
    for (uint64_t i = 0; valid && i < length; i += 1)
    {
        const int high = fputc(digits[bytes[i] >> 4], stream);
        const int low = fputc(digits[bytes[i] & 15], stream);
        valid = high != EOF && low != EOF;
    }
    valid = fputc('\n', stream) != EOF && valid;
    return valid;
}

static bool dominators_export_begin(FILE* stream)
{
    return fputs(BUSTER_DOMINATOR_EXPORT_HEADER, stream) != EOF;
}

static bool dominators_export_cfg(FILE* stream, const DominatorCfg* cfg, const uint8_t* file, uint64_t file_length, const uint8_t* name, uint32_t name_length, uint32_t form)
{
    bool valid = fprintf(stream, "cfg %u %u %u %u\nfile ", form, cfg->block_count, cfg->edge_count, cfg->entry) >= 0;
    valid = valid && dominators_export_hex(stream, file, file_length);
    valid = valid && fputs("name ", stream) != EOF;
    valid = valid && dominators_export_hex(stream, name, name_length);
    for (uint32_t source = 0; valid && source < cfg->block_count; source += 1)
    {
        for (uint32_t edge = cfg->successor_offsets[source]; valid && edge < cfg->successor_offsets[(uint64_t)source + 1]; edge += 1)
        {
            valid = fprintf(stream, "%u %u\n", source, cfg->successors[edge]) >= 0;
        }
    }
    valid = valid && fputs("end\n", stream) != EOF && ferror(stream) == 0;
    return valid;
}

static bool dominators_export_complete(FILE* stream, uint64_t files, uint64_t functions, uint64_t forms)
{
    return fprintf(stream, "complete %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", files, functions, forms) >= 0;
}

typedef struct DominatorExportText DominatorExportText;
struct DominatorExportText
{
    const uint8_t* bytes;
    uint64_t length;
    uint64_t position;
};

static bool dominators_export_literal(DominatorExportText* text, const char* literal)
{
    const uint64_t length = strlen(literal);
    const bool valid = text->length - text->position >= length && memcmp(text->bytes + text->position, literal, length) == 0;
    text->position += valid ? length : 0;
    return valid;
}

// A decimal no larger than `maximum`, then `terminator`.
static bool dominators_export_number(DominatorExportText* text, uint64_t maximum, uint8_t terminator, uint64_t* value)
{
    uint64_t result = 0;
    uint64_t digits = 0;
    bool valid = true;
    while (valid && text->position < text->length && text->bytes[text->position] >= '0' && text->bytes[text->position] <= '9')
    {
        const uint64_t digit = (uint64_t)(text->bytes[text->position] - '0');
        valid = (digits == 0 || result != 0) && digit <= maximum && result <= (maximum - digit) / 10;
        result = result * 10 + digit;
        digits += 1;
        text->position += 1;
    }
    valid = valid && digits != 0 && text->position < text->length && text->bytes[text->position] == terminator;
    text->position += valid ? 1 : 0;
    *value = result;
    return valid;
}

// The rest of a line as lowercase hex, decoded into the arena.
static bool dominators_export_name(DominatorExportText* text, Arena* arena, const uint8_t** bytes, uint32_t* length)
{
    uint64_t end = text->position;
    while (end < text->length && ((text->bytes[end] >= '0' && text->bytes[end] <= '9') || (text->bytes[end] >= 'a' && text->bytes[end] <= 'f')))
    {
        end += 1;
    }
    const uint64_t digits = end - text->position;
    bool valid = end < text->length && text->bytes[end] == '\n' && digits % 2 == 0 && digits / 2 <= UINT32_MAX;
    uint8_t* decoded = valid ? arena_push(arena, digits / 2, 1) : 0;
    valid = valid && decoded != 0;
    for (uint64_t i = 0; valid && i < digits / 2; i += 1)
    {
        const uint8_t high = text->bytes[text->position + i * 2];
        const uint8_t low = text->bytes[text->position + i * 2 + 1];
        decoded[i] = (uint8_t)(((high <= '9' ? high - '0' : high - 'a' + 10) << 4) | (low <= '9' ? low - '0' : low - 'a' + 10));
    }
    if (valid)
    {
        *bytes = decoded;
        *length = (uint32_t)(digits / 2);
        text->position = end + 1;
    }
    return valid;
}

static bool dominators_export_graph(DominatorExportText* text, Arena* arena, DominatorExportGraph* graph)
{
    uint64_t form = 0;
    uint64_t blocks = 0;
    uint64_t edges = 0;
    uint64_t entry = 0;
    bool valid = dominators_export_literal(text, "cfg ") && dominators_export_number(text, UINT32_MAX, ' ', &form) && dominators_export_number(text, UINT32_MAX - 1, ' ', &blocks);
    valid = valid && dominators_export_number(text, UINT32_MAX, ' ', &edges) && dominators_export_number(text, UINT32_MAX, '\n', &entry);
    valid = valid && (blocks == 0 ? edges == 0 && entry == BUSTER_DOMINATOR_NONE : entry < blocks);
    valid = valid && dominators_export_literal(text, "file ") && dominators_export_name(text, arena, &graph->file, &graph->file_length);
    valid = valid && dominators_export_literal(text, "name ") && dominators_export_name(text, arena, &graph->name, &graph->name_length);
    uint32_t* successor_offsets = valid ? arena_push_array(arena, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* successors = valid ? arena_push_array(arena, edges, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* predecessor_offsets = valid ? arena_push_array(arena, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* predecessors = valid ? arena_push_array(arena, edges, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    const uint64_t cursor_start = arena->length;
    uint32_t* cursor = valid ? arena_push_array(arena, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    valid = valid && successor_offsets != 0 && successors != 0 && predecessor_offsets != 0 && predecessors != 0 && cursor != 0;
    if (valid)
    {
        memset(successor_offsets, 0, (blocks + 1) * sizeof(uint32_t));
        memset(predecessor_offsets, 0, (blocks + 1) * sizeof(uint32_t));
    }
    uint64_t previous = 0;
    for (uint64_t edge = 0; valid && edge < edges; edge += 1)
    {
        uint64_t source = 0;
        uint64_t target = 0;
        valid = dominators_export_number(text, blocks - 1, ' ', &source) && dominators_export_number(text, blocks - 1, '\n', &target) && source >= previous;
        if (valid)
        {
            successors[edge] = (uint32_t)target;
            successor_offsets[source + 1] += 1;
            predecessor_offsets[target + 1] += 1;
            previous = source;
        }
    }
    valid = valid && dominators_export_literal(text, "end\n");
    if (valid)
    {
        for (uint64_t block = 0; block < blocks; block += 1)
        {
            successor_offsets[block + 1] += successor_offsets[block];
            predecessor_offsets[block + 1] += predecessor_offsets[block];
            cursor[block] = predecessor_offsets[block];
        }
        for (uint32_t source = 0; source < blocks; source += 1)
        {
            for (uint32_t edge = successor_offsets[source]; edge < successor_offsets[source + 1]; edge += 1)
            {
                predecessors[cursor[successors[edge]]] = source;
                cursor[successors[edge]] += 1;
            }
        }
        graph->form = (uint32_t)form;
        graph->cfg = (DominatorCfg){
            .block_count = (uint32_t)blocks,
            .edge_count = (uint32_t)edges,
            .entry = (uint32_t)entry,
            .successor_offsets = successor_offsets,
            .successors = successors,
            .predecessor_offsets = predecessor_offsets,
            .predecessors = predecessors,
        };
    }
    if (cursor != 0)
    {
        arena_pop_to(arena, cursor_start);
    }
    return valid;
}

// Reads the whole export in `bytes` into `arena`. Fails, leaving the arena and the result as they were, unless the
// text is a complete export as the header comment describes.
static bool dominators_export_read(Arena* arena, const uint8_t* bytes, uint64_t length, DominatorExport* result)
{
    const uint64_t saved = arena->length;
    // Records start lines with "cfg "; no name, edge or completion line can, so this count sizes the record array.
    uint64_t records = 0;
    for (uint64_t i = 0; i + 4 <= length; i += 1)
    {
        records += (i == 0 || bytes[i - 1] == '\n') && memcmp(bytes + i, "cfg ", 4) == 0;
    }
    DominatorExportText text = {
        .bytes = bytes,
        .length = length,
    };
    DominatorExport export = { 0 };
    bool valid = records <= UINT32_MAX && dominators_export_literal(&text, BUSTER_DOMINATOR_EXPORT_HEADER);
    export.graphs = valid ? arena_push_array(arena, records, sizeof(DominatorExportGraph), _Alignof(DominatorExportGraph)) : 0;
    valid = valid && export.graphs != 0;
    while (valid && export.count < records)
    {
        valid = dominators_export_graph(&text, arena, &export.graphs[export.count]);
        if (valid)
        {
            export.blocks += export.graphs[export.count].cfg.block_count;
            export.edges += export.graphs[export.count].cfg.edge_count;
            export.count += 1;
        }
    }
    uint64_t forms = 0;
    valid = valid && dominators_export_literal(&text, "complete ") && dominators_export_number(&text, UINT64_MAX, ' ', &export.files);
    valid = valid && dominators_export_number(&text, UINT64_MAX, ' ', &export.functions) && dominators_export_number(&text, UINT64_MAX, '\n', &forms);
    valid = valid && forms == records && text.position == length;
    if (valid)
    {
        *result = export;
    }
    else
    {
        arena_pop_to(arena, saved);
    }
    return valid;
}
