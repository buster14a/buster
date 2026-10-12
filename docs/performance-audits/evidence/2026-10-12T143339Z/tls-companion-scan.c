// 9700X direct workload for #1915: scan TLS companion membership.
//
// Replays the relocation table of a real Clang 18 -O0 -fPIC object (input.data,
// written by tls-companion-extract.py beside this file) through the
// two x86-64 ELF link phases that ask whether a __tls_get_addr call is the
// second half of a TLSGD/TLSLD pair: import classification
// (link_elf_symbol_only_relaxed_tls_get_addr) and relocation application.
// TLS_METHOD 0 is the baseline full-table scan (link.c at 6082834); 1 is the
// sparse exact (section, offset) index merged by #2247 (22b1a04), including its
// buster_hash_64 open addressing. Only membership is replayed: parsing,
// merging, layout, patching and publication are outside this workload.
//
// The program exits 1 unless every helper reference is classified as a relaxed
// companion. Its printed digest of the relaxed rows must equal the other
// method's digest for the same input; the report compares them.
#define TLS_METHOD 0

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Row Row;
struct Row
{
    uint64_t offset;
    uint32_t section;
    uint32_t symbol;
    uint32_t kind;
    uint32_t zero;
};

enum
{
    KIND_PC32 = 1,
    KIND_PLT32 = 2,
    KIND_GOT = 3,
    KIND_TLSGD = 4,
    KIND_TLSLD = 5,
    // x86_64_metadata.h: GD helper 12 - address 4; LD helper 8 / indirect 9 - address 3.
    DELTA_GENERAL = 8,
    DELTA_LOCAL = 5,
    DELTA_LOCAL_INDIRECT = 6,
    SITE_GENERAL = 1,
    SITE_LOCAL = 2,
};

typedef struct Site Site;
struct Site
{
    uint64_t offset;
    uint32_t section;
    uint8_t kinds;
    uint8_t reserved[3];
};

typedef struct Table Table;
struct Table
{
    Row* rows;
    uint32_t count;
    uint32_t helper;
    Site* sites;
    uint64_t capacity;
};

#if TLS_METHOD
static uint64_t rotl64(uint64_t value, unsigned bits)
{
    return (value << bits) | (value >> (64 - bits));
}

static uint64_t hash_round(uint64_t acc, uint64_t input)
{
    acc += input * 14029467366897019727ULL;
    acc = rotl64(acc, 31);
    acc *= 11400714785074694791ULL;
    return acc;
}

// buster_hash_64 (hash.c, USE_XXHASH 0) specialized to its 16-byte key path.
static uint64_t hash_key(uint32_t section, uint64_t offset)
{
    uint64_t key[2] = {section, offset};
    uint64_t hash = 2870177450012600261ULL + sizeof(key);
    for (unsigned i = 0; i < 2; i += 1)
    {
        hash ^= hash_round(0, key[i]);
        hash = rotl64(hash, 27) * 11400714785074694791ULL + 9650029242287828579ULL;
    }
    hash ^= hash >> 33;
    hash *= 14029467366897019727ULL;
    hash ^= hash >> 29;
    hash *= 1609587929392839161ULL;
    hash ^= hash >> 32;
    return hash ? hash : 1;
}

static Site* site_find(Table* table, uint32_t section, uint64_t offset, int insert)
{
    Site* result = 0;
    if (table->capacity)
    {
        uint64_t mask = table->capacity - 1;
        uint64_t bucket = hash_key(section, offset) & mask;
        int done = 0;
        for (uint64_t probe = 0; !done && probe < table->capacity; probe += 1)
        {
            Site* site = table->sites + bucket;
            if (!site->kinds)
            {
                result = insert ? site : 0;
                done = 1;
            }
            else if (site->section == section && site->offset == offset)
            {
                result = site;
                done = 1;
            }
            bucket = (bucket + 1) & mask;
        }
    }
    return result;
}

static int index_build(Table* table)
{
    uint64_t count = 0;
    int valid = 1;
    for (uint32_t row = 0; row < table->count; row += 1)
    {
        count += table->rows[row].kind == KIND_TLSGD || table->rows[row].kind == KIND_TLSLD;
    }
    if (count)
    {
        uint64_t capacity = 1;
        while (capacity < count * 2) capacity *= 2;
        table->sites = calloc(capacity, sizeof(*table->sites));
        table->capacity = table->sites ? capacity : 0;
        valid = table->sites != 0;
        for (uint32_t row = 0; valid && row < table->count; row += 1)
        {
            Row* relocation = table->rows + row;
            uint8_t kinds = relocation->kind == KIND_TLSGD ? SITE_GENERAL : relocation->kind == KIND_TLSLD ? SITE_LOCAL : 0;
            if (kinds)
            {
                Site* site = site_find(table, relocation->section, relocation->offset, 1);
                valid = site != 0;
                if (valid)
                {
                    site->section = relocation->section;
                    site->offset = relocation->offset;
                    site->kinds |= kinds;
                }
            }
        }
    }
    return valid;
}

#endif

static int is_relaxed(Table* table, Row* relocation)
{
    int direct = relocation->kind == KIND_PC32 || relocation->kind == KIND_PLT32;
    int indirect = relocation->kind == KIND_GOT;
    int result = 0;
    if ((direct || indirect) && relocation->symbol == table->helper)
    {
#if TLS_METHOD
        uint64_t deltas[2] = {DELTA_GENERAL, direct ? DELTA_LOCAL : DELTA_LOCAL_INDIRECT};
        uint8_t kinds[2] = {SITE_GENERAL, SITE_LOCAL};
        for (unsigned kind = 0; !result && kind < 2; kind += 1)
        {
            if (relocation->offset >= deltas[kind])
            {
                Site* site = site_find(table, relocation->section, relocation->offset - deltas[kind], 0);
                result = site && (site->kinds & kinds[kind]);
            }
        }
#else
        for (uint32_t index = 0; !result && index < table->count; index += 1)
        {
            Row* candidate = table->rows + index;
            uint64_t delta = candidate->kind == KIND_TLSGD   ? DELTA_GENERAL
                             : candidate->kind == KIND_TLSLD ? (direct ? DELTA_LOCAL : DELTA_LOCAL_INDIRECT)
                                                             : 0;
            result = delta && candidate->section == relocation->section && candidate->offset <= UINT64_MAX - delta &&
                     candidate->offset + delta == relocation->offset;
        }
#endif
    }
    return result;
}

static int load(Table* table)
{
    int valid = 0;
    FILE* file = fopen("input.data", "rb");
    if (file)
    {
        char magic[8];
        uint32_t header[2];
        if (fread(magic, 1, 8, file) == 8 && memcmp(magic, "BTLSREL1", 8) == 0 && fread(header, 4, 2, file) == 2)
        {
            table->count = header[0];
            table->helper = header[1];
            table->rows = malloc((size_t)table->count * sizeof(Row) + 1);
            valid = table->rows && fread(table->rows, sizeof(Row), table->count, file) == table->count;
        }
        fclose(file);
    }
    return valid;
}

int main(void)
{
    Table table = {0};
    int status = 1;
    if (load(&table))
    {
        int built = 1;
#if TLS_METHOD
        built = index_build(&table);
#endif
        // Import classification: every helper reference must be a relaxed call.
        int only_relaxed = 1;
        for (uint32_t row = 0; built && only_relaxed && row < table.count; row += 1)
        {
            only_relaxed = table.rows[row].symbol != table.helper || is_relaxed(&table, table.rows + row);
        }
        // Relocation application: ask once per row, as the writer does.
        uint64_t references = 0;
        uint64_t relaxed = 0;
        uint64_t sites = 0;
        uint64_t digest = 14695981039346656037ULL;
        for (uint32_t row = 0; built && row < table.count; row += 1)
        {
            Row* relocation = table.rows + row;
            sites += relocation->kind == KIND_TLSGD || relocation->kind == KIND_TLSLD;
            references += relocation->symbol == table.helper;
            if (is_relaxed(&table, relocation))
            {
                relaxed += 1;
                digest = (digest ^ row) * 1099511628211ULL;
            }
        }
        int pass = built && only_relaxed && sites && relaxed == sites && relaxed == references;
        printf("TLS_COMPANION method=%s rows=%u sites=%llu helper_references=%llu relaxed=%llu only_relaxed=%d digest=%016llx self_check=%s\n",
               TLS_METHOD ? "index" : "scan", table.count, (unsigned long long)sites, (unsigned long long)references,
               (unsigned long long)relaxed, only_relaxed, (unsigned long long)digest, pass ? "pass" : "FAIL");
        status = pass ? 0 : 1;
    }
    else
    {
        printf("TLS_COMPANION self_check=FAIL cannot read input.data\n");
    }
    free(table.rows);
    free(table.sites);
    return status;
}
