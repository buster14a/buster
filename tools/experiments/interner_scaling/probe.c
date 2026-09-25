/* Isolated production-helper experiment. No timing and no compiler acceptance. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef char char8;
typedef struct { char8 *pointer; u64 length; } String8;
typedef struct CSymbolSlot CSymbolSlot;
typedef struct { void *blocks[128]; unsigned count; u64 bytes; } Arena;
typedef struct {
    Arena *arena;
    CSymbolSlot *slots;
    String8 *names;
    u32 count, slot_capacity, name_capacity;
} CSymbolTable; /* Only the production helper's consumed fields. */
typedef struct {
    u64 query_slots, middle_calls, middle_words;
    u64 rehash_scan, rehash_slots, requery_slots, middle_hash_bytes;
} Counters;
static Counters probe_counts;
#define BUSTER_C_INTERNAL static
#define BUSTER_C_SHARED static
#define BUSTER_CHECK(x) assert(x)
#define BUSTER_VALIDATE(x) assert(x)
#ifndef PROBE_ALTERNATIVE
#define PROBE_ALTERNATIVE 0
#endif
#ifndef PROBE_FORCE_COLLISIONS
#define PROBE_FORCE_COLLISIONS 0
#endif

static void require(bool ok, char const *message)
{
    if (!ok) { fprintf(stderr, "probe: %s\n", message); exit(1); }
}

static void *allocate(Arena *arena, size_t size)
{
    require(arena->count < 128 && size <= UINT64_C(67108864) - arena->bytes, "64 MiB arena budget");
    void *result = malloc(size);
    require(result != NULL, "allocation failure");
    arena->blocks[arena->count++] = result;
    arena->bytes += size;
    return result;
}
#define arena_allocate(arena, type, count) ((type *)allocate((arena), sizeof(type) * (size_t)(count)))
#include "kernel.inc"

/* Prototype: retain short-key hash; mix the omitted bytes for long names.
   All slots, exact equality, IDs, insertion order and growth remain unchanged.
   This is not a worst-case guarantee for an arbitrary deterministic hash. */
static u32 probe_hash(CSymbolKey key, String8 name)
{
    u32 hash = c_symbol_slot_hash(key, name.length);
    if (PROBE_ALTERNATIVE && name.length > 16)
    {
        u64 mixed = UINT64_C(14695981039346656037) ^ hash;
        for (u64 i = 8; i < name.length - 8; i += 1)
        {
            mixed ^= (u8)name.pointer[i];
            mixed *= UINT64_C(1099511628211);
            probe_counts.middle_hash_bytes += 1;
        }
        mixed ^= mixed >> 32;
        mixed *= UINT64_C(0xD6E8FEB86659FD93);
        hash = (u32)(mixed >> 32);
    }
    if (PROBE_FORCE_COLLISIONS) { hash = 0; }
    return hash;
}

static CSymbolTable make_table(Arena *arena)
{
    CSymbolTable table = {.arena = arena, .slot_capacity = 1u << 14, .name_capacity = 1u << 12};
    table.names = arena_allocate(arena, String8, table.name_capacity);
    table.slots = arena_allocate(arena, CSymbolSlot, table.slot_capacity);
    memset(table.slots, 0, sizeof(*table.slots) * table.slot_capacity);
    return table;
}

static void release(Arena *arena)
{
    for (unsigned i = 0; i < arena->count; i += 1) { free(arena->blocks[i]); }
    memset(arena, 0, sizeof(*arena));
}

static u64 digest_step(u64 hash, u64 value)
{
    hash ^= value;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static void make_name(char *name, u32 id, unsigned length, bool control)
{
    require(length >= 24 && length <= 256 && id < 10000000, "name bounds");
    memset(name, 'm', length);
    memcpy(name, "project_", 8);
    memcpy(name + length - 8, "_handler", 8);
    char unique[9];
    require(snprintf(unique, sizeof(unique), "p%07" PRIu32, id) == 8, "name formatting");
    memcpy(name + (control ? 0 : length - 16), unique, 8);
    name[length] = 0;
}

static void row(char const *kind, char const *input, CSymbolTable const *table,
                u64 queries, u64 input_bytes, u64 spelling_bytes, u64 insert_slots,
                u64 hit_slots, u64 digest)
{
    printf("%s,%s,%" PRIu32 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%" PRIu64 ",%" PRIu32 ",%" PRIu32 ",%" PRIu64 ",%016" PRIx64 "\n",
           kind, input, table->count, queries, input_bytes, spelling_bytes,
           insert_slots, hit_slots, probe_counts.query_slots, probe_counts.rehash_scan,
           probe_counts.rehash_slots, probe_counts.requery_slots, probe_counts.middle_calls,
           probe_counts.middle_words, probe_counts.middle_hash_bytes, table->slot_capacity,
           table->name_capacity, table->arena->bytes, digest);
}

static void synthetic(u32 n, u32 references, unsigned length, bool control, bool permuted)
{
    require(n <= 8193 && references <= 16384, "work budget");
    Arena arena = {0};
    CSymbolTable table = make_table(&arena);
    probe_counts = (Counters){0};
    char *names = malloc((size_t)n * (length + 1));
    u32 *ids = calloc(n, sizeof(*ids));
    u32 *order = malloc((size_t)n * sizeof(*order));
    require(names != NULL && ids != NULL && order != NULL, "fixture allocation");
    for (u32 i = 0; i < n; i += 1) { make_name(names + (size_t)i * (length + 1), i, length, control); order[i] = i; }
    u32 seed = UINT32_C(0xB057E2);
    if (permuted)
    {
        for (u32 i = n; i > 1; i -= 1)
        {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            u32 j = seed % i, old = order[i - 1]; order[i - 1] = order[j]; order[j] = old;
        }
    }
    u64 digest = UINT64_C(14695981039346656037);
    for (u32 i = 0; i < n; i += 1)
    {
        u32 index = order[i];
        String8 name = {names + (size_t)index * (length + 1), length};
        u32 id = c_symbol_intern(&table, name);
        require(id == i + 1 && table.names[id].pointer == name.pointer, "insertion ID/ownership mismatch");
        ids[index] = id; digest = digest_step(digest, id);
    }
    u64 insert_slots = probe_counts.query_slots;
    for (u32 i = 0; i < references; i += 1)
    {
        u32 index = i % n;
        char copy[257]; /* Equal spelling, different address; preserve first storage. */
        memcpy(copy, names + (size_t)index * (length + 1), length + 1);
        u32 id = c_symbol_intern(&table, (String8){copy, length});
        require(id == ids[index] && table.names[id].pointer == names + (size_t)index * (length + 1), "hit ID/first-spelling mismatch");
        digest = digest_step(digest, id);
    }
    u64 hit_slots = probe_counts.query_slots - insert_slots;
    if (!control && !permuted && !PROBE_ALTERNATIVE && !PROBE_FORCE_COLLISIONS && n <= 8192)
    {
        require(insert_slots == (u64)n * (n + 1) / 2, "incumbent insertion model mismatch");
        u64 full = references / n, tail = references % n;
        require(hit_slots == full * n * (n + 1) / 2 + tail * (tail + 1) / 2, "incumbent reference model mismatch");
    }
    if (PROBE_ALTERNATIVE && !PROBE_FORCE_COLLISIONS)
    {
        require(probe_counts.query_slots <= 8 * ((u64)n + references), "candidate non-timing complexity regression");
        require(probe_counts.rehash_slots + probe_counts.requery_slots <= 8 * (u64)n, "candidate growth regression");
    }
    char label[96];
    snprintf(label, sizeof(label), "n%u-r%u-l%u-%s", n, references, length, permuted ? "perm" : "ordered");
    row(control ? "control" : "middle", label, &table, (u64)n + references, 0,
        ((u64)n + references) * length, insert_slots, hit_slots, digest);
    free(order); free(ids); free(names); release(&arena);
}

/* Bounded ASCII lexical census, deliberately not a preprocessed TU census.
   Comments and string/character literals are skipped; directives and inactive
   branches remain. Every source file is a separate table, not merged TUs. */
static bool word_start(unsigned char c)
{
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool word_next(unsigned char c)
{
    return word_start(c) || (c >= '0' && c <= '9');
}
static void census(char const *path)
{
    FILE *file = fopen(path, "rb"); require(file != NULL, "census open");
    require(fseek(file, 0, SEEK_END) == 0, "census seek");
    long size = ftell(file); require(size >= 0 && size <= 16 * 1024 * 1024, "census size");
    require(fseek(file, 0, SEEK_SET) == 0, "census rewind");
    char *text = malloc((size_t)size + 1); require(text != NULL, "census memory");
    require(fread(text, 1, (size_t)size, file) == (size_t)size, "census read");
    require(fclose(file) == 0, "census close"); text[size] = 0;
    Arena arena = {0}; CSymbolTable table = make_table(&arena); probe_counts = (Counters){0};
    u64 queries = 0, spelling_bytes = 0, insert_slots = 0, hit_slots = 0;
    u64 digest = UINT64_C(14695981039346656037);
    size_t i = 0, limit = (size_t)size;
    while (i < limit)
    {
        if (text[i] == '/' && i + 1 < limit && text[i + 1] == '/')
        {
            i += 2; while (i < limit && text[i] != '\n') { i += 1; }
        }
        else if (text[i] == '/' && i + 1 < limit && text[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < limit && !(text[i] == '*' && text[i + 1] == '/')) { i += 1; }
            i = i + 1 < limit ? i + 2 : limit;
        }
        else if (text[i] == '"' || text[i] == '\'')
        {
            char quote = text[i++];
            while (i < limit && text[i] != quote)
            {
                i += text[i] == '\\' && i + 1 < limit ? 2 : 1;
            }
            if (i < limit) { i += 1; }
        }
        else if (word_start((unsigned char)text[i]))
        {
            size_t start = i++; while (i < limit && word_next((unsigned char)text[i])) { i += 1; }
            String8 name = {text + start, i - start};
            u64 before = probe_counts.query_slots; u32 count = table.count;
            u32 id = c_symbol_intern(&table, name);
            if (count == table.count) { hit_slots += probe_counts.query_slots - before; }
            else { insert_slots += probe_counts.query_slots - before; }
            queries += 1; spelling_bytes += name.length; digest = digest_step(digest, id);
        }
        else { i += 1; }
    }
    row("lexical", path, &table, queries, (u64)size, spelling_bytes, insert_slots, hit_slots, digest);
    /* A separate exact-projection census; not charged to helper counters. */
    typedef struct { CSymbolKey key; u64 length; u32 count, first; } Group;
    u32 capacity = 1;
    while (capacity < 2 * table.count + 1) { capacity *= 2; }
    Group *groups = calloc(capacity, sizeof(*groups)); require(groups != NULL, "group allocation");
    u32 best = 0;
    for (u32 id = 1; id <= table.count; id += 1)
    {
        String8 name = table.names[id];
        if (name.length > 16)
        {
            CSymbolKey key = c_symbol_key(name);
            u32 slot = c_symbol_slot_hash(key, name.length) & (capacity - 1);
            while (groups[slot].count && !(groups[slot].key.low == key.low &&
                   groups[slot].key.high == key.high && groups[slot].length == name.length))
            { slot = (slot + 1) & (capacity - 1); }
            if (!groups[slot].count) { groups[slot].key = key; groups[slot].length = name.length; groups[slot].first = id; }
            groups[slot].count += 1;
            if (groups[slot].count > groups[best].count) { best = slot; }
        }
    }
    u64 pairs = 0;
    for (u32 slot = 0; slot < capacity; slot += 1)
    { pairs += (u64)groups[slot].count * (groups[slot].count ? groups[slot].count - 1 : 0) / 2; }
    if (groups[best].count)
    {
        fprintf(stderr, "PROJECTION_GROUP file=%s max_unique=%u length=%" PRIu64 " mandatory_pair_checks=%" PRIu64 " examples=", path, groups[best].count, groups[best].length, pairs);
        unsigned shown = 0;
        for (u32 id = 1; id <= table.count && shown < 5; id += 1)
        {
            String8 name = table.names[id]; CSymbolKey key = c_symbol_key(name);
            if (name.length == groups[best].length && key.low == groups[best].key.low && key.high == groups[best].key.high)
            { fprintf(stderr, "%s%.*s", shown ? "|" : "", (int)name.length, name.pointer); shown += 1; }
        }
        fputc('\n', stderr);
    }
    free(groups);
    release(&arena); free(text);
}

static void generate(char const *path, u32 n, u32 references, bool control)
{
    require(n > 0 && n <= 8193 && references <= 16384, "generator bounds");
    FILE *file = fopen(path, "wb"); require(file != NULL, "generator open");
    char name[257];
    for (u32 i = 0; i < n; i += 1)
    {
        make_name(name, i, 24, control); fprintf(file, "extern unsigned %s;\n", name);
    }
    fputs("unsigned probe(void) { unsigned sum = 0;\n", file);
    for (u32 i = 0; i < references; i += 1)
    {
        make_name(name, i % n, 24, control); fprintf(file, "sum += %s;\n", name);
    }
    fputs("return sum; }\n", file); require(!ferror(file), "generator write"); require(fclose(file) == 0, "generator close");
}

static void boundaries(void)
{
    Arena arena = {0}; CSymbolTable table = make_table(&arena); probe_counts = (Counters){0};
    unsigned const lengths[] = {1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 18, 23, 24, 31, 32, 33, 63, 64, 65, 255};
    char *owned[40]; unsigned used = 0;
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i += 1)
    {
        unsigned length = lengths[i];
        char *first = malloc(length), *copy = malloc(length);
        require(first != NULL && copy != NULL, "boundary allocation"); owned[used++] = first; owned[used++] = copy;
        memset(first, 'x', length); memcpy(copy, first, length);
        u32 id = c_symbol_intern(&table, (String8){first, length});
        require(c_symbol_intern(&table, (String8){copy, length}) == id, "boundary equality");
        require(table.names[id].pointer == first, "boundary ownership");
        copy[length / 2] = 'y';
        require(c_symbol_intern(&table, (String8){copy, length}) != id, "boundary distinction");
    }
    release(&arena); for (unsigned i = 0; i < used; i += 1) { free(owned[i]); }
    fprintf(stderr, "BOUNDARIES_PASS cases=%u alternative=%d forced_collisions=%d\n", used / 2, PROBE_ALTERNATIVE, PROBE_FORCE_COLLISIONS);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "generate") == 0)
    {
        require(argc == 6, "generate PATH N REFERENCES CONTROL");
        generate(argv[2], (u32)strtoul(argv[3], NULL, 10), (u32)strtoul(argv[4], NULL, 10), strcmp(argv[5], "1") == 0);
    }
    else
    {
        puts("kind,input,unique,queries,input_bytes,spelling_bytes,insert_slots,hit_slots,query_slots,rehash_scan,rehash_slots,requery_slots,middle_calls,middle_words,middle_hash_bytes,slot_capacity,name_capacity,retained_arena_bytes,id_digest");
        boundaries();
        if (argc > 1)
        {
            for (int i = 1; i < argc; i += 1) { census(argv[i]); }
        }
        else
        {
            u32 const sizes[] = {64, 128, 256, 512, 1024, 2048, 4096};
            for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i += 1)
            {
                u32 n = sizes[i];
                synthetic(n, n, 24, false, false); synthetic(n, n, 24, true, false);
            }
            u32 const ns[] = {64, 256, 1024}, rs[] = {1024, 4096, 16384};
            for (unsigned i = 0; i < 3; i += 1) { for (unsigned j = 0; j < 3; j += 1) { synthetic(ns[i], rs[j], 24, false, false); } }
            synthetic(8192, 0, 24, false, false); synthetic(8193, 0, 24, false, false);
            synthetic(1024, 4096, 24, false, true);
            synthetic(1024, 1024, 32, false, false); synthetic(1024, 1024, 64, false, false);
        }
    }
    return 0;
}
