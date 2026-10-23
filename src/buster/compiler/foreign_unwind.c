// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Prune an object's .eh_frame before canonical symbol resolution loses COMDAT ownership. Include after foreign.c,
// import.c and executable.c. A unique PC32 relocation at FDE+8 identifies its original target section. The caller's
// original-section -> kept-atom map decides which FDEs survive; only CIEs used by those FDEs survive. Other fields,
// including personalities and LSDAs, follow their owning record, never decide its liveness. Input bytes, symbols,
// relocations and section maps remain unchanged. Output bytes, relocations and interval maps belong to the arena.
//
// The supported records are the writer's DWARF32 zR/zPLR family, with PC-relative sdata4 FDE addresses. Personalities
// and LSDAs support fixed absolute 4/8-byte pointers or PC-relative 4-byte pointers, optionally indirect. Unsupported
// encodings, ambiguous ownership, overlapping fields and malformed records fail before publishing output. Variable
// record parsing is shared; bounded relocation classification and output transposition have exact/vector twins.
// Failure restores the arena length and leaves the result untouched. All argument pointers are required; nonempty
// slices require storage, and output/input/arena storage must not overlap. ForeignUnwindInput is already one section's
// combined RELA table, whose symbol table and original section map must correspond to that same object.
//
// foreign_unwind_offset maps a retained byte or the original section endpoint; UINT64_MAX marks discarded records.
// The adapter must separately translate references TO .eh_frame, including section-symbol-plus-addend expressions,
// and symbol extents spanning removed records. An STT_SECTION symbol at zero names the compact section, even when
// its first original record was removed. No terminator is synthesized; an all-discarded section stays empty.

typedef enum ForeignUnwindError
{
    FOREIGN_UNWIND_ERROR_NONE,
    FOREIGN_UNWIND_ERROR_INPUT,
    FOREIGN_UNWIND_ERROR_MEMORY,
    FOREIGN_UNWIND_ERROR_MALFORMED,
    FOREIGN_UNWIND_ERROR_UNSUPPORTED,
} ForeignUnwindError;

typedef enum ForeignUnwindKind
{
    FOREIGN_UNWIND_CIE,
    FOREIGN_UNWIND_FDE,
    FOREIGN_UNWIND_TERMINATOR,
} ForeignUnwindKind;

typedef struct ForeignUnwindInput ForeignUnwindInput;
struct ForeignUnwindInput
{
    String8 bytes;
    const Elf64_Rela* relocations;
    uint32_t relocation_count;
    const ImportSymbols* symbols;
    const AtomId* sections;
    uint32_t section_count;
};

typedef struct ForeignUnwindRecord ForeignUnwindRecord;
struct ForeignUnwindRecord
{
    uint64_t old_offset;
    uint64_t new_offset;
    uint64_t length;
    uint64_t cie_offset;
    uint32_t kind;
    uint32_t section;
    AtomId atom;
};

typedef struct ForeignUnwind ForeignUnwind;
struct ForeignUnwind
{
    String8 bytes;
    Elf64_Rela* relocations;
    uint32_t relocation_count;
    ForeignUnwindRecord* records;
    uint32_t record_count;
    uint64_t old_length;
    uint32_t fde_count;
};

typedef struct ForeignUnwindResult ForeignUnwindResult;
struct ForeignUnwindResult
{
    ForeignUnwindError error;
    uint64_t offset;
    // Original relocation index plus one, or zero for a record-level failure.
    uint32_t relocation;
};

typedef struct ForeignUnwindState ForeignUnwindState;
struct ForeignUnwindState
{
    uint64_t auxiliary;
    uint64_t instructions;
    uint32_t auxiliary_width;
    uint32_t cie;
    uint32_t pc;
    uint8_t lsda;
    uint8_t auxiliary_encoding;
    bool kept;
    bool auxiliary_relocated;
};

static ForeignUnwindResult foreign_unwind_fail(ForeignUnwindError error, uint64_t offset, uint32_t relocation)
{
    return (ForeignUnwindResult){
        .error = error,
        .offset = offset,
        .relocation = relocation,
    };
}

static Elf64_Rela foreign_unwind_relocation(const ForeignUnwindInput* input, uint32_t index)
{
    Elf64_Rela result;
    memcpy(&result, (const char*)input->relocations + (uint64_t)index * sizeof(result), sizeof(result));
    return result;
}

// Strict LEB width, unlike the writer's skip-only helper: no truncation past 64 bits, including the tenth byte.
static bool foreign_unwind_leb(const uint8_t* bytes, uint64_t end, uint64_t* position, bool signed_value, uint64_t* value)
{
    uint64_t result = 0;
    bool complete = false;
    bool valid = true;
    for (uint32_t i = 0; valid && !complete && i < 10; i += 1)
    {
        valid = *position < end;
        if (valid)
        {
            const uint8_t byte = bytes[*position];
            *position += 1;
            const uint8_t part = byte & 0x7f;
            valid = i != 9 || (signed_value ? part == 0 || part == 0x7f : part <= 1);
            result |= (uint64_t)part << (i * 7);
            complete = (byte & 0x80) == 0;
        }
    }
    if (value != 0)
    {
        *value = result;
    }
    return valid && complete;
}

static uint32_t foreign_unwind_pointer_width(uint8_t encoding)
{
    const uint8_t format = encoding & 0x0f;
    const uint8_t application = encoding & 0x70;
    const uint32_t width = format == 0 || format == 4 || format == 12 ? 8 : format == 3 || format == 11 ? 4 : 0;
    return (application == 0 || (application == 0x10 && width == 4)) ? width : 0;
}

static ForeignUnwindError foreign_unwind_cie(const uint8_t* bytes, ForeignUnwindRecord* record, ForeignUnwindState* state)
{
    const uint64_t end = record->old_offset + record->length;
    uint64_t position = record->old_offset + 8;
    const uint8_t version = position < end ? bytes[position] : 0;
    position += 1;
    const uint64_t augmentation = position;
    while (position < end && bytes[position] != 0)
    {
        position += 1;
    }
    bool valid = (version == 1 || version == 3) && position < end && augmentation < end && bytes[augmentation] == 'z';
    position += 1;
    uint64_t alignment = 0;
    valid = valid && foreign_unwind_leb(bytes, end, &position, false, &alignment) && alignment != 0 && foreign_unwind_leb(bytes, end, &position, true, 0);
    valid = valid && (version == 3 ? foreign_unwind_leb(bytes, end, &position, false, 0) : position < end);
    position += version == 1 ? 1 : 0;
    uint64_t length = 0;
    valid = valid && foreign_unwind_leb(bytes, end, &position, false, &length) && length <= end - position;
    const uint64_t data_end = valid ? position + length : 0;
    uint32_t seen = 0;
    bool supported = true;
    state->lsda = 0xff;
    for (uint64_t a = augmentation + 1; valid && supported && bytes[a] != 0; a += 1)
    {
        const uint8_t letter = bytes[a];
        const uint32_t bit = letter == 'R' ? 1 : letter == 'P' ? 2 : letter == 'L' ? 4 : letter == 'S' ? 8 : 0;
        supported = bit != 0;
        valid = (seen & bit) == 0 && (letter == 'S' || position < data_end);
        seen |= bit;
        if (valid && supported && letter != 'S')
        {
            const uint8_t encoding = bytes[position];
            position += 1;
            if (letter == 'R')
            {
                supported = encoding == BUSTER_EXECUTABLE_EH_PE_PCREL_SDATA4;
            }
            else if (letter == 'L')
            {
                state->lsda = encoding;
                supported = encoding == 0xff || foreign_unwind_pointer_width(encoding) != 0;
            }
            else
            {
                state->auxiliary = position;
                state->auxiliary_encoding = encoding;
                state->auxiliary_width = foreign_unwind_pointer_width(encoding);
                supported = state->auxiliary_width != 0;
                valid = state->auxiliary_width <= data_end - position;
                position += valid ? state->auxiliary_width : 0;
            }
        }
    }
    state->instructions = data_end;
    valid = valid && position == data_end;
    supported = supported && (seen & 1) != 0;
    const bool indexable = valid && supported && executable_cie_indexable(bytes, record->old_offset, (uint32_t)record->length - 4);
    return !supported ? FOREIGN_UNWIND_ERROR_UNSUPPORTED : !valid ? FOREIGN_UNWIND_ERROR_MALFORMED : !indexable ? FOREIGN_UNWIND_ERROR_UNSUPPORTED : FOREIGN_UNWIND_ERROR_NONE;
}

// First record whose end is after offset; records cover the original section without gaps.
static uint32_t foreign_unwind_record_at(const ForeignUnwind* output, uint64_t offset)
{
    uint32_t low = 0;
    uint32_t high = output->record_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const ForeignUnwindRecord* record = &output->records[middle];
        if (record->old_offset + record->length <= offset)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

static bool foreign_unwind_offset(const ForeignUnwind* output, uint64_t old_offset, uint64_t* new_offset)
{
    const uint32_t index = foreign_unwind_record_at(output, old_offset);
    const bool endpoint = old_offset == output->old_length;
    const bool retained = index < output->record_count && output->records[index].new_offset != UINT64_MAX;
    if (endpoint || retained)
    {
        *new_offset = endpoint ? output->bytes.length : output->records[index].new_offset + old_offset - output->records[index].old_offset;
    }
    return endpoint || retained;
}

// Bounded columns -> interleaved ELF records. The source vectors have already been compressed to count lanes.
static void foreign_unwind_store(Elf64_Rela* output, __m512i offsets, __m512i infos, __m512i addends, uint32_t count)
{
    static const uint64_t ab[3][8] = {
        { 0, 8, 0, 1, 9, 0, 2, 10 },
        { 0, 3, 11, 0, 4, 12, 0, 5 },
        { 13, 0, 6, 14, 0, 7, 15, 0 },
    };
    static const uint64_t c[3][8] = {
        { 0, 0, 0, 0, 0, 1, 0, 0 },
        { 2, 0, 0, 3, 0, 0, 4, 0 },
        { 0, 5, 0, 0, 6, 0, 0, 7 },
    };
    static const __mmask8 masks[3] = { 0x24, 0x49, 0x92 };
    for (uint32_t row = 0; row < 3 && row * 8 < count * 3; row += 1)
    {
        const uint32_t n = count * 3 - row * 8 < 8 ? count * 3 - row * 8 : 8;
        const __m512i pair = _mm512_permutex2var_epi64(offsets, _mm512_loadu_si512(ab[row]), infos);
        const __m512i third = _mm512_permutexvar_epi64(_mm512_loadu_si512(c[row]), addends);
        import_store64((char*)output + row * 64, _mm512_mask_mov_epi64(pair, masks[row], third), n);
    }
}

static ForeignUnwindResult foreign_unwind_prune(const ForeignUnwindInput* input, ForeignPath path, Arena* arena, ForeignUnwind* output)
{
    ForeignUnwindResult result = { 0 };
    const bool arguments = input != 0 && arena != 0 && output != 0 && input->symbols != 0 && input->sections != 0 &&
                           (path == FOREIGN_PATH_EXACT || path == FOREIGN_PATH_VECTOR);
    const uint64_t saved = arena != 0 ? arena->length : 0;
    bool valid = arguments;
    if (valid)
    {
        const ImportSymbols* symbols = input->symbols;
        valid = (input->bytes.length == 0 || input->bytes.pointer != 0) && (input->relocation_count == 0 || input->relocations != 0) &&
                symbols->count <= UINT32_MAX && symbols->entries.length >= (uint64_t)symbols->count * sizeof(Elf64_Sym) && (symbols->count == 0 || symbols->entries.pointer != 0) &&
                (symbols->extended.length == 0 || (symbols->extended.pointer != 0 && symbols->extended.length >= (uint64_t)symbols->count * sizeof(uint32_t)));
    }
    result.error = valid ? FOREIGN_UNWIND_ERROR_NONE : FOREIGN_UNWIND_ERROR_INPUT;
    if (valid && input->bytes.length > BUSTER_EXECUTABLE_MAX_ATOM_BYTES)
    {
        result.error = FOREIGN_UNWIND_ERROR_UNSUPPORTED;
    }
    ForeignUnwind built = { 0 };
    const uint8_t* bytes = valid ? (const uint8_t*)input->bytes.pointer : 0;
    uint64_t offset = 0;
    while (result.error == FOREIGN_UNWIND_ERROR_NONE && offset < input->bytes.length)
    {
        const uint64_t left = input->bytes.length - offset;
        const uint32_t length = left >= 4 ? executable_load32(bytes + offset) : 0;
        if (left >= 4 && length == UINT32_MAX)
        {
            result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_UNSUPPORTED, offset, 0);
        }
        else if (left < 4 || (length != 0 && (length < 4 || length > left - 4)))
        {
            result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_MALFORMED, offset, 0);
        }
        else
        {
            built.record_count += 1;
            if (length == 0)
            {
                for (uint64_t at = offset + 4; at < input->bytes.length; at += 1)
                {
                    if (bytes[at] != 0)
                    {
                        result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_MALFORMED, at, 0);
                    }
                }
                offset = input->bytes.length;
            }
            else
            {
                offset += (uint64_t)length + 4;
            }
        }
    }
    if (result.error == FOREIGN_UNWIND_ERROR_NONE)
    {
        built.old_length = input->bytes.length;
        built.records = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindRecord, built.record_count);
        result.error = built.records == 0 ? FOREIGN_UNWIND_ERROR_MEMORY : FOREIGN_UNWIND_ERROR_NONE;
    }
    Scratch scratch = { 0 };
    ForeignUnwindState* states = 0;
    uint32_t* owners = 0;
    uint32_t* order = 0;
    uint32_t* spare = 0;
    uint64_t* widths = 0;
    if (result.error == FOREIGN_UNWIND_ERROR_NONE)
    {
        scratch = scratch_begin(arena);
        states = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ForeignUnwindState, built.record_count);
        owners = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, input->relocation_count);
        order = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, input->relocation_count);
        spare = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, input->relocation_count);
        widths = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, input->relocation_count);
        result.error = states == 0 || owners == 0 || order == 0 || spare == 0 || widths == 0 ? FOREIGN_UNWIND_ERROR_MEMORY : FOREIGN_UNWIND_ERROR_NONE;
    }
    offset = 0;
    for (uint32_t r = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && r < built.record_count; r += 1)
    {
        const uint32_t length = executable_load32(bytes + offset);
        const uint32_t id = length != 0 ? executable_load32(bytes + offset + 4) : 0;
        ForeignUnwindRecord* record = &built.records[r];
        *record = (ForeignUnwindRecord){
            .old_offset = offset,
            .new_offset = UINT64_MAX,
            .length = length != 0 ? (uint64_t)length + 4 : input->bytes.length - offset,
            .cie_offset = UINT64_MAX,
            .kind = length == 0 ? FOREIGN_UNWIND_TERMINATOR : id == 0 ? FOREIGN_UNWIND_CIE : FOREIGN_UNWIND_FDE,
        };
        states[r] = (ForeignUnwindState){
            .auxiliary = UINT64_MAX,
            .lsda = 0xff,
        };
        if (record->kind == FOREIGN_UNWIND_CIE)
        {
            result.error = foreign_unwind_cie(bytes, record, &states[r]);
        }
        else if (record->kind == FOREIGN_UNWIND_FDE)
        {
            const uint64_t cie = offset + 4 >= id ? offset + 4 - id : UINT64_MAX;
            ForeignUnwind prefix = built;
            prefix.record_count = r;
            const uint32_t parent = foreign_unwind_record_at(&prefix, cie);
            valid = parent < r && built.records[parent].old_offset == cie && built.records[parent].kind == FOREIGN_UNWIND_CIE && length >= 13;
            if (valid)
            {
                states[r].cie = parent;
                record->cie_offset = cie;
                uint64_t position = offset + 16;
                uint64_t augmentation = 0;
                const uint64_t end = offset + record->length;
                valid = foreign_unwind_leb(bytes, end, &position, false, &augmentation) && augmentation <= end - position;
                states[r].instructions = valid ? position + augmentation : 0;
                const uint8_t encoding = states[parent].lsda;
                states[r].auxiliary_encoding = encoding;
                states[r].auxiliary_width = encoding == 0xff ? 0 : foreign_unwind_pointer_width(encoding);
                states[r].auxiliary = states[r].auxiliary_width != 0 ? position : UINT64_MAX;
                valid = valid && augmentation == states[r].auxiliary_width;
            }
            result.error = valid ? FOREIGN_UNWIND_ERROR_NONE : FOREIGN_UNWIND_ERROR_MALFORMED;
        }
        if (result.error != FOREIGN_UNWIND_ERROR_NONE)
        {
            result.offset = offset;
        }
        offset += record->length;
    }
    // Classify the relocation fields in bounded batches. Record lookup and original ownership remain irregular.
    for (uint64_t first = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && first < input->relocation_count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = input->relocation_count - first < BUSTER_IMPORT_BATCH ? (uint32_t)(input->relocation_count - first) : BUSTER_IMPORT_BATCH;
        if (path == FOREIGN_PATH_VECTOR)
        {
            __m512i words[3];
            import_load_records3((const char*)input->relocations + first * sizeof(Elf64_Rela), n, words);
            const __m512i types = _mm512_and_si512(words[1], _mm512_set1_epi64(UINT32_MAX));
            const __mmask8 four = _mm512_cmpeq_epi64_mask(types, _mm512_set1_epi64(R_X86_64_PC32)) |
                                 _mm512_cmpeq_epi64_mask(types, _mm512_set1_epi64(R_X86_64_32)) |
                                 _mm512_cmpeq_epi64_mask(types, _mm512_set1_epi64(R_X86_64_32S));
            __m512i sizes = _mm512_mask_mov_epi64(_mm512_set1_epi64(-1), four, _mm512_set1_epi64(4));
            sizes = _mm512_mask_mov_epi64(sizes, _mm512_cmpeq_epi64_mask(types, _mm512_set1_epi64(R_X86_64_64)), _mm512_set1_epi64(8));
            sizes = _mm512_mask_mov_epi64(sizes, _mm512_cmpeq_epi64_mask(types, _mm512_setzero_si512()), _mm512_setzero_si512());
            import_store64(widths + first, sizes, n);
        }
        for (uint32_t lane = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && lane < n; lane += 1)
        {
            const uint32_t i = (uint32_t)first + lane;
            const Elf64_Rela relocation = foreign_unwind_relocation(input, i);
            const uint32_t type = ELF64_R_TYPE(relocation.r_info);
            if (path == FOREIGN_PATH_EXACT)
            {
                widths[i] = type == R_X86_64_NONE ? 0 : type == R_X86_64_64 ? 8 : type == R_X86_64_PC32 || type == R_X86_64_32 || type == R_X86_64_32S ? 4 : UINT64_MAX;
            }
            order[i] = i;
            owners[i] = UINT32_MAX;
            if (widths[i] != 0)
            {
                const uint32_t r = foreign_unwind_record_at(&built, relocation.r_offset);
                const uint64_t symbol = ELF64_R_SYM(relocation.r_info);
                if (widths[i] == UINT64_MAX)
                {
                    result.error = FOREIGN_UNWIND_ERROR_UNSUPPORTED;
                }
                else if (r == built.record_count || symbol == 0 || symbol >= input->symbols->count)
                {
                    result.error = FOREIGN_UNWIND_ERROR_MALFORMED;
                }
                else
                {
                    ForeignUnwindRecord* record = &built.records[r];
                    ForeignUnwindState* state = &states[r];
                    const bool pc = record->kind == FOREIGN_UNWIND_FDE && relocation.r_offset == record->old_offset + 8;
                    const bool auxiliary = relocation.r_offset == state->auxiliary && widths[i] == state->auxiliary_width;
                    const bool instruction = relocation.r_offset >= state->instructions && record->kind != FOREIGN_UNWIND_TERMINATOR;
                    valid = widths[i] <= record->old_offset + record->length - relocation.r_offset && (pc || auxiliary || instruction);
                    if (valid && auxiliary)
                    {
                        const uint32_t expected = (state->auxiliary_encoding & 0x70) == 0x10 ? R_X86_64_PC32 : state->auxiliary_width == 8 ? R_X86_64_64 :
                                                  (state->auxiliary_encoding & 0x0f) == 11 ? R_X86_64_32S : R_X86_64_32;
                        valid = type == expected;
                    }
                    if (valid && pc)
                    {
                        const Elf64_Sym target = import_symbol(input->symbols, (uint32_t)symbol);
                        uint32_t section = 0;
                        valid = type == R_X86_64_PC32 && state->pc == 0 && import_symbol_section(input->symbols, (uint32_t)symbol, &target, &section) &&
                                section != SHN_UNDEF && section < input->section_count && (section < SHN_LORESERVE || target.st_shndx == SHN_XINDEX);
                        if (valid)
                        {
                            state->pc = i + 1;
                            record->section = section;
                            record->atom = input->sections[section];
                            state->kept = record->atom != 0;
                        }
                    }
                    state->auxiliary_relocated = state->auxiliary_relocated || auxiliary;
                    owners[i] = r;
                    result.error = valid ? FOREIGN_UNWIND_ERROR_NONE : FOREIGN_UNWIND_ERROR_MALFORMED;
                }
                if (result.error != FOREIGN_UNWIND_ERROR_NONE)
                {
                    result.offset = relocation.r_offset;
                    result.relocation = i + 1;
                }
            }
        }
    }
    if (result.error == FOREIGN_UNWIND_ERROR_NONE)
    {
        const String8 relocation_bytes = {
            .pointer = (char*)input->relocations,
            .length = (uint64_t)input->relocation_count * sizeof(Elf64_Rela),
        };
        const uint32_t* sorted = import_sort_relocations(relocation_bytes, order, spare, input->relocation_count);
        uint64_t previous_end = 0;
        for (uint32_t at = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && at < input->relocation_count; at += 1)
        {
            const uint32_t i = sorted[at];
            if (widths[i] != 0)
            {
                const uint64_t start = foreign_unwind_relocation(input, i).r_offset;
                if (start < previous_end)
                {
                    result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_MALFORMED, start, i + 1);
                }
                previous_end = start + widths[i];
            }
        }
    }
    for (uint32_t r = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && r < built.record_count; r += 1)
    {
        if (built.records[r].kind == FOREIGN_UNWIND_FDE)
        {
            if (states[r].pc == 0)
            {
                result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_MALFORMED, built.records[r].old_offset + 8, 0);
            }
            else if (states[r].kept)
            {
                states[states[r].cie].kept = true;
                built.fde_count += 1;
            }
        }
        if (states[r].auxiliary_width != 0 && !states[r].auxiliary_relocated)
        {
            uint64_t pointer = 0;
            memcpy(&pointer, bytes + states[r].auxiliary, states[r].auxiliary_width);
            if (pointer != 0)
            {
                result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_UNSUPPORTED, states[r].auxiliary, 0);
            }
        }
    }
    for (uint32_t r = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && r < built.record_count; r += 1)
    {
        ForeignUnwindRecord* record = &built.records[r];
        states[r].kept = states[r].kept || (record->kind == FOREIGN_UNWIND_TERMINATOR && (built.fde_count != 0 || built.record_count == 1));
        if (states[r].kept)
        {
            record->new_offset = built.bytes.length;
            built.bytes.length += record->length;
        }
    }
    for (uint32_t i = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && i < input->relocation_count; i += 1)
    {
        if (owners[i] != UINT32_MAX && states[owners[i]].kept)
        {
            const Elf64_Rela relocation = foreign_unwind_relocation(input, i);
            const Elf64_Sym symbol = import_symbol(input->symbols, (uint32_t)ELF64_R_SYM(relocation.r_info));
            uint32_t section = 0;
            const bool indexed = import_symbol_section(input->symbols, (uint32_t)ELF64_R_SYM(relocation.r_info), &symbol, &section);
            const bool ordinary = section != SHN_UNDEF && section < input->section_count && (section < SHN_LORESERVE || symbol.st_shndx == SHN_XINDEX);
            if (!indexed || (ELF64_ST_BIND(symbol.st_info) == STB_LOCAL && ordinary && input->sections[section] == 0))
            {
                result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_UNSUPPORTED, relocation.r_offset, i + 1);
            }
            built.relocation_count += 1;
        }
    }
    if (result.error == FOREIGN_UNWIND_ERROR_NONE)
    {
        built.bytes.pointer = BUSTER_ARENA_PUSH_ARRAY(arena, char, built.bytes.length);
        built.relocations = BUSTER_ARENA_PUSH_ARRAY(arena, Elf64_Rela, built.relocation_count);
        result.error = built.bytes.pointer == 0 || built.relocations == 0 ? FOREIGN_UNWIND_ERROR_MEMORY : FOREIGN_UNWIND_ERROR_NONE;
    }
    for (uint32_t r = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && r < built.record_count; r += 1)
    {
        const ForeignUnwindRecord* record = &built.records[r];
        if (states[r].kept)
        {
            memcpy(built.bytes.pointer + record->new_offset, bytes + record->old_offset, record->length);
            if (record->kind == FOREIGN_UNWIND_FDE)
            {
                const uint32_t distance = (uint32_t)(record->new_offset + 4 - built.records[states[r].cie].new_offset);
                memcpy(built.bytes.pointer + record->new_offset + 4, &distance, sizeof(distance));
            }
        }
    }
    uint32_t copied = 0;
    for (uint64_t first = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && first < input->relocation_count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = input->relocation_count - first < BUSTER_IMPORT_BATCH ? (uint32_t)(input->relocation_count - first) : BUSTER_IMPORT_BATCH;
        uint64_t offsets[BUSTER_IMPORT_BATCH] = { 0 };
        __mmask8 kept = 0;
        for (uint32_t lane = 0; lane < n; lane += 1)
        {
            const uint32_t i = (uint32_t)first + lane;
            if (owners[i] != UINT32_MAX && states[owners[i]].kept)
            {
                const ForeignUnwindRecord* record = &built.records[owners[i]];
                const Elf64_Rela relocation = foreign_unwind_relocation(input, i);
                offsets[lane] = record->new_offset + relocation.r_offset - record->old_offset;
                kept |= (__mmask8)(1u << lane);
                if (path == FOREIGN_PATH_EXACT)
                {
                    built.relocations[copied] = relocation;
                    built.relocations[copied].r_offset = offsets[lane];
                    copied += 1;
                }
            }
        }
        if (path == FOREIGN_PATH_VECTOR)
        {
            __m512i words[3];
            import_load_records3((const char*)input->relocations + first * sizeof(Elf64_Rela), n, words);
            const uint32_t count = (uint32_t)__builtin_popcount(kept);
            foreign_unwind_store(built.relocations + copied, _mm512_maskz_compress_epi64(kept, _mm512_loadu_si512(offsets)),
                                 _mm512_maskz_compress_epi64(kept, words[1]), _mm512_maskz_compress_epi64(kept, words[2]), count);
            copied += count;
        }
    }
    if (scratch.arena != 0)
    {
        scratch_end(scratch);
    }
    if (result.error == FOREIGN_UNWIND_ERROR_NONE)
    {
        *output = built;
    }
    else if (arena != 0)
    {
        arena_pop_to(arena, saved);
    }
    return result;
}
