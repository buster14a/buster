// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Optional C value metadata beside debug.c's five core sections. A sixth section holds DWARF 5 location lists.
// References and definition addresses point into .debug_info, sorted and disjoint within each column; the caller
// owns all bytes through object/executable emission. No padding is assumed. Type references are CU-relative ref4
// values and never appear here. This header is shared by the two native writers, not an ELF input parser.
#ifndef BUSTER_NATIVE_DEBUG_H
#define BUSTER_NATIVE_DEBUG_H

#define BUSTER_NATIVE_DEBUG_CORE_SECTIONS 5
#define BUSTER_NATIVE_DEBUG_SECTIONS 6
#define BUSTER_NATIVE_DEBUG_LOCLISTS 5

typedef struct NativeDebugField NativeDebugField;
struct NativeDebugField
{
    uint32_t offset;
    uint32_t section;
};

typedef struct NativeDebugAddress NativeDebugAddress;
struct NativeDebugAddress
{
    uint32_t offset;
    uint32_t symbol;
    uint64_t addend;
};

typedef struct NativeDebugExtra NativeDebugExtra;
struct NativeDebugExtra
{
    const NativeDebugField* fields;
    const NativeDebugAddress* addresses;
    uint32_t field_count;
    uint32_t address_count;
};

static bool native_debug_extra_valid(const String8* sections, const NativeDebugExtra* extra)
{
    const uint64_t size = sections[0].length;
    bool valid = (extra->field_count == 0 || extra->fields != NULL) && (extra->address_count == 0 || extra->addresses != NULL) &&
                 ((extra->field_count == 0 && extra->address_count == 0) || sections[0].pointer != NULL);
    uint64_t previous = 12;
    for (uint32_t f = 0; valid && f < extra->field_count; f += 1)
    {
        const NativeDebugField field = extra->fields[f];
        valid = field.offset >= previous && (uint64_t)field.offset + 4 <= size && field.section >= 4 && field.section < BUSTER_NATIVE_DEBUG_SECTIONS;
        if (valid)
        {
            uint32_t value = 0;
            memcpy(&value, sections[0].pointer + field.offset, 4);
            valid = sections[field.section].pointer != NULL && value >= 12 && value < sections[field.section].length;
        }
        previous = (uint64_t)field.offset + 4;
    }
    previous = 12;
    uint32_t field = 0;
    for (uint32_t a = 0; valid && a < extra->address_count; a += 1)
    {
        const NativeDebugAddress address = extra->addresses[a];
        valid = address.offset >= previous && (uint64_t)address.offset + 8 <= size && address.symbol != 0;
        while (valid && field < extra->field_count && (uint64_t)extra->fields[field].offset + 4 <= address.offset)
        {
            field += 1;
        }
        valid = valid && (field == extra->field_count || extra->fields[field].offset >= (uint64_t)address.offset + 8);
        previous = (uint64_t)address.offset + 8;
    }
    // The optional location section consists of complete DWARF 5 contributions, without an offset table.
    const String8 locations = sections[BUSTER_NATIVE_DEBUG_LOCLISTS];
    uint64_t at = 0;
    valid = valid && (locations.length == 0 || locations.pointer != NULL);
    while (valid && at < locations.length)
    {
        valid = locations.length - at >= 12;
        if (valid)
        {
            uint32_t length = 0;
            uint32_t entries = 0;
            uint16_t version = 0;
            memcpy(&length, locations.pointer + at, 4);
            memcpy(&version, locations.pointer + at + 4, 2);
            memcpy(&entries, locations.pointer + at + 8, 4);
            valid = length >= 8 && length < 0xfffffff0u && length <= locations.length - at - 4 && version == 5 &&
                    locations.pointer[at + 6] == 8 && locations.pointer[at + 7] == 0 && entries == 0;
            at += (uint64_t)length + 4;
        }
    }
    // Extra patches belong to child DIEs, not the core producer's CU header, strings or four section fields.
    // Walk the sorted columns together across contributions and reject fields which straddle a unit boundary.
    at = 0;
    field = 0;
    uint32_t address = 0;
    while (valid && (field < extra->field_count || address < extra->address_count))
    {
        uint32_t length = 0;
        valid = at <= size && size - at >= 13;
        if (valid)
        {
            memcpy(&length, sections[0].pointer + at, 4);
            valid = length >= 9 && length < 0xfffffff0u && length <= size - at - 4 && sections[0].pointer[at + 12] == 1;
        }
        const uint64_t end = at + 4 + length;
        uint64_t children = at + 13;
        for (uint32_t string = 0; valid && string < 3; string += 1)
        {
            const char* nul = children < end ? memchr(sections[0].pointer + children, 0, end - children) : NULL;
            valid = nul != NULL;
            children = valid ? (uint64_t)(nul - sections[0].pointer) + 1 : children;
        }
        children += 14; // language, statement list, address base and CU ranges
        valid = valid && children < end;
        while (valid && field < extra->field_count && extra->fields[field].offset < end)
        {
            const uint64_t offset = extra->fields[field++].offset;
            valid = offset >= children && offset + 4 <= end;
        }
        while (valid && address < extra->address_count && extra->addresses[address].offset < end)
        {
            const uint64_t offset = extra->addresses[address++].offset;
            valid = offset >= children && offset + 8 <= end;
        }
        at = end;
    }
    return valid;
}

#endif
