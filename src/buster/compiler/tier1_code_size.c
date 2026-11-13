// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1116: deterministic per-source ELF function-size accounting, shared by the c-testsuite and program measurements.
// Every Tier 1 body explicitly named by --tier1-function-sizes contributes its ELF st_size to tier1_bytes; only
// exact-name Clang counterparts contribute to clang_bytes. A missing counterpart contributes no reference bytes,
// so it makes the corpus bound stricter; its Tier 1 bytes also remain visible in unmatched_bytes. Helpers, aliases
// and padding remain in other_bytes, and fallback bodies have separate counts. Text bytes are executable section
// sizes; function sizes exclude inter-function padding.
// This is opt-in measurement tooling, outside the compile path, with no separate vector implementation.

#include <inttypes.h>

typedef struct Tier1CodeSize Tier1CodeSize;
struct Tier1CodeSize
{
    uint64_t units;
    uint64_t compiled;
    uint64_t fallbacks;
    uint64_t matched;
    uint64_t tier1_bytes;
    uint64_t clang_bytes;
    uint64_t fallback_bytes;
    uint64_t unmatched;
    uint64_t unmatched_bytes;
    uint64_t other_bytes;
    uint64_t text_bytes;
    uint64_t clang_text_bytes;
    // Sticky failure for aggregate overflow: add() cannot turn a failed sum into a passing small number.
    bool invalid;
};

typedef struct Tier1SizeSymbol Tier1SizeSymbol;
struct Tier1SizeSymbol
{
    String8 name;
    uint64_t size;
    uint64_t value;
    AtomId atom;
    bool executable;
};

typedef struct Tier1SizeObject Tier1SizeObject;
struct Tier1SizeObject
{
    Tier1SizeSymbol* symbols;
    uint32_t count;
    uint64_t text_bytes;
};

typedef struct Tier1SizeBody Tier1SizeBody;
struct Tier1SizeBody
{
    String8 name;
    uint64_t size;
    uint64_t value;
    AtomId atom;
    bool tier1;
};

static bool tier1_code_size_sum(uint64_t* total, uint64_t value)
{
    const bool valid = value <= UINT64_MAX - *total;
    if (valid)
    {
        *total += value;
    }
    return valid;
}

static bool tier1_code_size_executable(uint8_t class)
{
    return class == BUSTER_LINK_CLASS_TEXT || class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI;
}

static int tier1_code_size_name_order(String8 a, String8 b)
{
    const uint64_t common = a.length < b.length ? a.length : b.length;
    const int order = common != 0 ? memcmp(a.pointer, b.pointer, common) : 0;
    return order != 0 ? order : (a.length > b.length) - (a.length < b.length);
}

static int tier1_code_size_symbol_order(const void* a, const void* b)
{
    const Tier1SizeSymbol* x = a;
    const Tier1SizeSymbol* y = b;
    return tier1_code_size_name_order(x->name, y->name);
}

static int tier1_code_size_body_order(const void* a, const void* b)
{
    const Tier1SizeBody* x = a;
    const Tier1SizeBody* y = b;
    return tier1_code_size_name_order(x->name, y->name);
}

static int tier1_code_size_range_order(const void* a, const void* b)
{
    const Tier1SizeBody* x = a;
    const Tier1SizeBody* y = b;
    const int atom = (x->atom > y->atom) - (x->atom < y->atom);
    return atom != 0 ? atom : (x->value > y->value) - (x->value < y->value);
}

// The importer checks ELF bounds, symbol ranges and the section table before any size is consumed here. Preserve
// non-executable functions in the name index so a required one fails explicitly instead of appearing unmatched.
static bool tier1_code_size_object(Tier1SizeObject* result, String8 bytes, Arena* arena)
{
    *result = (Tier1SizeObject){ 0 };
    LinkCapacity capacity = { 0 };
    Link link = { 0 };
    ImportObject object = { 0 };
    ImportResult imported = import_object_capacity(bytes, &capacity);
    bool valid = imported.error == IMPORT_ERROR_NONE && link_initialize(&link, arena, arena, capacity);
    if (valid)
    {
        imported = import_object(&link, bytes, 0, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &object);
        valid = imported.error == IMPORT_ERROR_NONE;
    }
    result->symbols = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, Tier1SizeSymbol, link.symbols.count) : NULL;
    valid = valid && result->symbols != NULL;
    for (AtomId a = 1; valid && a < link.atoms.count; a += 1)
    {
        if (tier1_code_size_executable(link.atoms.class[a]))
        {
            valid = tier1_code_size_sum(&result->text_bytes, link.atoms.size[a]);
        }
    }
    for (SymbolId s = 1; valid && s < link.symbols.count; s += 1)
    {
        const AtomId atom = link.symbols.atom[s];
        const NameId name = link.symbols.name[s];
        if (atom != 0 && name != 0 && (link.symbols.flags[s] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION)
        {
            result->symbols[result->count++] = (Tier1SizeSymbol){
                .name = { .pointer = (char*)link.names.bytes[name], .length = link.names.length[name] },
                .size = link.symbols.size[s],
                .value = link.symbols.value[s],
                .atom = atom,
                .executable = tier1_code_size_executable(link.atoms.class[atom]),
            };
        }
    }
    if (valid && result->count > 1)
    {
        qsort(result->symbols, result->count, sizeof(Tier1SizeSymbol), tier1_code_size_symbol_order);
        for (uint32_t s = 1; valid && s < result->count; s += 1)
        {
            valid = tier1_code_size_name_order(result->symbols[s - 1].name, result->symbols[s].name) != 0;
        }
    }
    return valid;
}

static bool tier1_code_size_take(String8* text, const char* prefix)
{
    const uint64_t length = strlen(prefix);
    const bool found = text->length >= length && memcmp(text->pointer, prefix, length) == 0;
    if (found)
    {
        text->pointer += length;
        text->length -= length;
    }
    return found;
}

static bool tier1_code_size_decimal(String8 text, uint64_t* result)
{
    uint64_t value = 0;
    bool valid = text.length != 0;
    for (uint64_t i = 0; valid && i < text.length; i += 1)
    {
        const uint8_t digit = (uint8_t)text.pointer[i] - '0';
        valid = digit <= 9 && value <= (UINT64_MAX - digit) / 10;
        if (valid)
        {
            value = value * 10 + digit;
        }
    }
    *result = valid ? value : 0;
    return valid;
}

static uint32_t tier1_code_size_hex(uint8_t c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : UINT32_MAX;
}

static bool tier1_code_size_body(Tier1SizeBody* body, String8 text, Arena* arena)
{
    *body = (Tier1SizeBody){ 0 };
    const bool tier1 = tier1_code_size_take(&text, "tier1-function-size\ttier=tier1\tbytes=");
    bool valid = tier1 || tier1_code_size_take(&text, "tier1-function-size\ttier=tier0\tbytes=");
    const char* end = valid ? memchr(text.pointer, '\t', text.length) : NULL;
    valid = valid && end != NULL && tier1_code_size_decimal((String8){ .pointer = text.pointer, .length = (uint64_t)(end - text.pointer) }, &body->size);
    if (valid)
    {
        text.length -= (uint64_t)(end - text.pointer);
        text.pointer = (char*)end;
        valid = tier1_code_size_take(&text, "\tname=") && text.length != 0 && text.length % 2 == 0 && text.length / 2 <= UINT32_MAX;
    }
    body->name.length = valid ? text.length / 2 : 0;
    body->name.pointer = valid ? arena_push(arena, body->name.length, 1) : NULL;
    valid = valid && body->name.pointer != NULL;
    for (uint64_t i = 0; valid && i < body->name.length; i += 1)
    {
        const uint32_t high = tier1_code_size_hex((uint8_t)text.pointer[i * 2]);
        const uint32_t low = tier1_code_size_hex((uint8_t)text.pointer[i * 2 + 1]);
        valid = high < 16 && low < 16 && (high != 0 || low != 0);
        if (valid)
        {
            body->name.pointer[i] = (char)(high * 16 + low);
        }
    }
    body->tier1 = tier1;
    return valid;
}

// Require the existing summary's relevant fields exactly once, on this line alone. No substring scan may pick up
// a field on another diagnostic's line, and a missing report never means zero compiled functions.
static bool tier1_code_size_summary(String8 line, uint64_t* functions, uint64_t* compiled, uint64_t* fallbacks)
{
    static const char* const names[] = { "functions=", "requested=", "compiled=", "fallbacks=" };
    uint64_t values[4] = { 0 };
    uint32_t seen = 0;
    bool valid = tier1_code_size_take(&line, "tier1\t");
    while (valid && line.length != 0)
    {
        const char* end = memchr(line.pointer, '\t', line.length);
        const uint64_t length = end != NULL ? (uint64_t)(end - line.pointer) : line.length;
        for (uint32_t f = 0; valid && f < 4; f += 1)
        {
            String8 field = { .pointer = line.pointer, .length = length };
            if (tier1_code_size_take(&field, names[f]))
            {
                valid = (seen & (1u << f)) == 0 && tier1_code_size_decimal(field, &values[f]);
                seen |= 1u << f;
            }
        }
        const uint64_t consumed = length + (end != NULL);
        line.pointer += consumed;
        line.length -= consumed;
    }
    *functions = values[0];
    *compiled = values[2];
    *fallbacks = values[3];
    return valid && seen == 15 && values[0] == values[1];
}

// One -c command's document and final summary. Multifile compiler invocations may print several documents, but
// this reader deliberately requires one source/object pair so static names from different units never collide.
static bool tier1_code_size_manifest(Tier1SizeBody** bodies, uint32_t* count, String8 text, Arena* arena)
{
    *bodies = NULL;
    *count = 0;
    uint64_t declared = 0;
    uint64_t compiled = 0;
    uint64_t fallbacks = 0;
    uint64_t functions_reported = 0;
    uint64_t compiled_reported = 0;
    uint64_t fallbacks_reported = 0;
    bool opened = false;
    bool closed = false;
    bool summarized = false;
    bool valid = text.pointer != NULL;
    const uint64_t maximum = text.length / 40;
    while (valid && text.length != 0)
    {
        const char* end = memchr(text.pointer, '\n', text.length);
        const uint64_t length = end != NULL ? (uint64_t)(end - text.pointer) : text.length;
        String8 line = { .pointer = text.pointer, .length = length };
        String8 value = line;
        if (tier1_code_size_take(&value, "tier1-function-sizes\tversion=1\tfunctions="))
        {
            valid = !opened && !summarized && end != NULL && tier1_code_size_decimal(value, &declared) && declared <= maximum && declared <= UINT32_MAX;
            *bodies = valid && declared != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, Tier1SizeBody, declared) : NULL;
            valid = valid && (declared == 0 || *bodies != NULL);
            opened = true;
        }
        else if (tier1_code_size_take(&value, "tier1-function-sizes-end\tfunctions="))
        {
            uint64_t finished = 0;
            valid = opened && !closed && end != NULL && tier1_code_size_decimal(value, &finished) && finished == declared && *count == declared;
            closed = true;
        }
        else if (tier1_code_size_take(&value, "tier1-function-size\t"))
        {
            valid = opened && !closed && end != NULL && *count < declared && tier1_code_size_body(*bodies + *count, line, arena);
            if (valid)
            {
                compiled += (*bodies)[*count].tier1;
                fallbacks += !(*bodies)[*count].tier1;
                *count += 1;
            }
        }
        else if (tier1_code_size_take(&value, "tier1\t"))
        {
            valid = closed && !summarized && end != NULL && tier1_code_size_summary(line, &functions_reported, &compiled_reported, &fallbacks_reported);
            summarized = true;
        }
        else
        {
            // Outside the document ordinary compiler diagnostics are harmless. Unknown versions and malformed
            // records use the reserved prefix, and must never be silently ignored as diagnostics.
            value = line;
            valid = !(opened && !closed) && !tier1_code_size_take(&value, "tier1-function-size");
        }
        const uint64_t consumed = length + (end != NULL);
        text.pointer += consumed;
        text.length -= consumed;
    }
    valid = valid && opened && closed && summarized && functions_reported == declared && compiled_reported == compiled && fallbacks_reported == fallbacks;
    if (valid && *count > 1)
    {
        qsort(*bodies, *count, sizeof(Tier1SizeBody), tier1_code_size_body_order);
        for (uint32_t b = 1; valid && b < *count; b += 1)
        {
            valid = tier1_code_size_name_order((*bodies)[b - 1].name, (*bodies)[b].name) != 0;
        }
    }
    return valid;
}

static const Tier1SizeSymbol* tier1_code_size_find(const Tier1SizeObject* object, String8 name)
{
    const Tier1SizeSymbol key = { .name = name };
    return object->count != 0 ? bsearch(&key, object->symbols, object->count, sizeof(Tier1SizeSymbol), tier1_code_size_symbol_order) : NULL;
}

static bool tier1_code_size_ranges(Tier1SizeBody* bodies, uint32_t count)
{
    bool valid = true;
    if (count > 1)
    {
        qsort(bodies, count, sizeof(Tier1SizeBody), tier1_code_size_range_order);
        for (uint32_t b = 1; valid && b < count; b += 1)
        {
            const Tier1SizeBody* previous = bodies + b - 1;
            const Tier1SizeBody* current = bodies + b;
            valid = previous->atom != current->atom || previous->size <= current->value - previous->value;
        }
    }
    return valid;
}

static bool tier1_code_size_compare(Tier1CodeSize* result, String8 object, String8 oracle, String8 report, Arena* arena, char* detail, uint64_t capacity)
{
    *result = (Tier1CodeSize){ 0 };
    Tier1SizeObject ours = { 0 };
    Tier1SizeObject theirs = { 0 };
    Tier1SizeBody* bodies = NULL;
    uint32_t count = 0;
    bool valid = tier1_code_size_object(&ours, object, arena) && tier1_code_size_object(&theirs, oracle, arena);
    const char* reason = "code size: malformed object or ambiguous function symbol names";
    if (valid)
    {
        valid = tier1_code_size_manifest(&bodies, &count, report, arena);
        reason = "code size: missing, malformed or inconsistent Tier 1 function manifest";
    }
    Tier1SizeBody* references = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, Tier1SizeBody, count) : NULL;
    if (valid && count != 0 && references == NULL)
    {
        valid = false;
        reason = "code size: no memory for matched function ranges";
    }
    Tier1CodeSize unit = { .units = 1, .text_bytes = ours.text_bytes, .clang_text_bytes = theirs.text_bytes };
    uint64_t body_bytes = 0;
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        Tier1SizeBody* body = bodies + b;
        const Tier1SizeSymbol* own = tier1_code_size_find(&ours, body->name);
        const Tier1SizeSymbol* reference = tier1_code_size_find(&theirs, body->name);
        valid = own != NULL && own->executable && own->size != 0 && own->size == body->size;
        reason = "code size: a manifest body is absent, non-executable, empty or differs from its ELF size";
        if (valid)
        {
            body->atom = own->atom;
            body->value = own->value;
            unit.compiled += body->tier1;
            unit.fallbacks += !body->tier1;
            valid = tier1_code_size_sum(&body_bytes, own->size);
            reason = "code size: function-byte total overflow";
        }
        if (valid && body->tier1)
        {
            valid = tier1_code_size_sum(&unit.tier1_bytes, own->size);
        }
        if (valid && !body->tier1)
        {
            valid = tier1_code_size_sum(&unit.fallback_bytes, own->size);
        }
        else if (valid && reference == NULL)
        {
            unit.unmatched += 1;
            valid = tier1_code_size_sum(&unit.unmatched_bytes, own->size);
        }
        else if (valid)
        {
            valid = reference->executable && reference->size != 0;
            reason = "code size: a Clang counterpart is empty or not executable";
            if (valid)
            {
                references[unit.matched] = (Tier1SizeBody){ .atom = reference->atom, .value = reference->value, .size = reference->size };
                unit.matched += 1;
                valid = tier1_code_size_sum(&unit.clang_bytes, reference->size);
                reason = "code size: matched-function total overflow";
            }
        }
    }
    if (valid)
    {
        valid = tier1_code_size_ranges(bodies, count) && tier1_code_size_ranges(references, (uint32_t)unit.matched);
        reason = "code size: compared source bodies overlap or alias the same code range";
    }
    valid = valid && body_bytes <= ours.text_bytes;
    if (valid)
    {
        unit.other_bytes = ours.text_bytes - body_bytes;
        *result = unit;
    }
    else
    {
        result->invalid = true;
    }
    if (capacity != 0)
    {
        snprintf(detail, capacity, "%s", valid ? "" : reason);
    }
    return valid;
}

static bool tier1_code_size_read(Tier1CodeSize* result, const char* object, const char* oracle, const char* report, Arena* arena, char* detail, uint64_t capacity)
{
    const String8 ours = object_test_read_file(arena, object);
    const String8 theirs = object_test_read_file(arena, oracle);
    const String8 manifest = object_test_read_file(arena, report);
    return tier1_code_size_compare(result, ours, theirs, manifest, arena, detail, capacity);
}

static void tier1_code_size_add(Tier1CodeSize* total, const Tier1CodeSize* unit)
{
    bool valid = !total->invalid && !unit->invalid;
#define BUSTER_TIER1_CODE_SIZE_ADD(field) valid = tier1_code_size_sum(&total->field, unit->field) && valid
    BUSTER_TIER1_CODE_SIZE_ADD(units);
    BUSTER_TIER1_CODE_SIZE_ADD(compiled);
    BUSTER_TIER1_CODE_SIZE_ADD(fallbacks);
    BUSTER_TIER1_CODE_SIZE_ADD(matched);
    BUSTER_TIER1_CODE_SIZE_ADD(tier1_bytes);
    BUSTER_TIER1_CODE_SIZE_ADD(clang_bytes);
    BUSTER_TIER1_CODE_SIZE_ADD(fallback_bytes);
    BUSTER_TIER1_CODE_SIZE_ADD(unmatched);
    BUSTER_TIER1_CODE_SIZE_ADD(unmatched_bytes);
    BUSTER_TIER1_CODE_SIZE_ADD(other_bytes);
    BUSTER_TIER1_CODE_SIZE_ADD(text_bytes);
    BUSTER_TIER1_CODE_SIZE_ADD(clang_text_bytes);
#undef BUSTER_TIER1_CODE_SIZE_ADD
    total->invalid = !valid;
}

// The corpus charges every Tier 1 byte against only the exact-name reference bytes it has. Missing functions may
// fit in that bound, but an empty reference cannot qualify. Subtraction keeps the classification check overflow-safe.
static bool tier1_code_size_evidence(const Tier1CodeSize* total)
{
    return !total->invalid && total->units != 0 && total->matched != 0 && total->clang_bytes != 0 && total->matched <= total->compiled &&
           total->unmatched == total->compiled - total->matched;
}

static bool tier1_code_size_qualified(const Tier1CodeSize* total)
{
    return tier1_code_size_evidence(total) && total->tier1_bytes <= total->clang_bytes;
}

// Report the same byte bound in optional and enforcing modes, including every unmatched body's cost and count.
static bool tier1_code_size_report(const Tier1CodeSize* total, const char* label, bool enforce)
{
    const bool qualified = tier1_code_size_qualified(total);
    printf("tier1-code-size\tcorpus=%s\tunits=%" PRIu64 "\tcompiled=%" PRIu64 "\tfallbacks=%" PRIu64 "\tmatched=%" PRIu64
           "\ttier1_bytes=%" PRIu64 "\tclang_bytes=%" PRIu64 "\tfallback_bytes=%" PRIu64 "\tunmatched=%" PRIu64
           "\tunmatched_bytes=%" PRIu64 "\tother_bytes=%" PRIu64 "\ttext_bytes=%" PRIu64 "\tclang_text_bytes=%" PRIu64 "\tgate=%s\tenforced=%u\n",
           label, total->units, total->compiled, total->fallbacks, total->matched, total->tier1_bytes, total->clang_bytes, total->fallback_bytes,
           total->unmatched, total->unmatched_bytes, total->other_bytes, total->text_bytes, total->clang_text_bytes, qualified ? "pass" : "fail", (unsigned)enforce);
    return tier1_code_size_evidence(total) && (!enforce || qualified);
}
