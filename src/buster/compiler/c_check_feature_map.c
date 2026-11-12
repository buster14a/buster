// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The order of a function's feature map, as clang 23's checkTargetFeatures iterates it (#2093): an llvm::StringMap<bool>,
// whose iteration order is its buckets'. The map is X86TargetInfo::initFeatureMap's for a CPU and a list of "+x" and
// "-x" features, and its keys' buckets follow from the order initFeatureMap first writes them in:
//
// 1. sse2, with setFeatureEnabled: the key, then each feature it implies, transitively, in FeatureInfos' index order
//    (llvm::X86::updateImpliedFeatures); turning one off writes, after it, each feature that implies it, transitively;
// 2. the CPU's features, in index order (getFeaturesForCPU, 64bit left out), each with setFeatureEnabled;
// 3. the list, in order: `+apxf` and `-apxf` as their eight features', `+general-regs-only` as -x87, -mmx and -sse;
// 4. popcnt and crc32 with sse4.2, mmx with sse and xsave with avx, where the map has the first true and the list
//    doesn't turn the second off.
//
// A StringMap hashes a key with xxh3_64bits, truncated to 32 bits (StringMapImpl::hash, xxh3_short.c), starts with 16
// buckets at its first insertion, puts a key in the first free bucket from its hash's low bits on (linear probing),
// and doubles when an insertion leaves more than three quarters of its buckets full, reinserting the old buckets in
// order (RehashTable). Nothing here erases a key, so there are no tombstones.
//
// The features and CPUs are c_check_feature_map.inc's, generated from LLVM's X86TargetParser (THIRD_PARTY_NOTICES.md).
// This is cold, scalar code: it runs only to name the feature of a target-feature error.

#include "buster/compiler/xxh3_short.c"

#define BUSTER_CHECK_FEATURE_MAP_BUCKETS 256u
#define BUSTER_CHECK_FEATURE_MAP_EMPTY 0xffu

typedef struct CheckFeatureMapFeature CheckFeatureMapFeature;
struct CheckFeatureMapFeature
{
    const char* name;
    uint64_t implies[2];
};

typedef struct CheckFeatureMapCPU CheckFeatureMapCPU;
struct CheckFeatureMapCPU
{
    const char* name;
    uint64_t features[2];
};

#define BUSTER_CHECK_FEATURE_MAP_FEATURE(name, word0, word1) { name, { word0, word1 } },
#define BUSTER_CHECK_FEATURE_MAP_CPU(name, word0, word1)
static const CheckFeatureMapFeature check_feature_map_features[] = {
#include "c_check_feature_map.inc"
};
#undef BUSTER_CHECK_FEATURE_MAP_CPU
#undef BUSTER_CHECK_FEATURE_MAP_FEATURE
#define BUSTER_CHECK_FEATURE_MAP_FEATURE(name, word0, word1)
#define BUSTER_CHECK_FEATURE_MAP_CPU(name, word0, word1) { name, { word0, word1 } },
static const CheckFeatureMapCPU check_feature_map_cpus[] = {
#include "c_check_feature_map.inc"
};
#undef BUSTER_CHECK_FEATURE_MAP_CPU
#undef BUSTER_CHECK_FEATURE_MAP_FEATURE

#define BUSTER_CHECK_FEATURE_MAP_COUNT ((uint32_t)BUSTER_ARRAY_LENGTH(check_feature_map_features))
_Static_assert(BUSTER_ARRAY_LENGTH(check_feature_map_features) <= 128, "a feature set is two 64-bit words");

// A feature map: each bucket's key, a feature's index or BUSTER_CHECK_FEATURE_MAP_EMPTY, its hash and its value.
typedef struct CheckFeatureMap CheckFeatureMap;
struct CheckFeatureMap
{
    uint8_t keys[BUSTER_CHECK_FEATURE_MAP_BUCKETS];
    uint32_t hashes[BUSTER_CHECK_FEATURE_MAP_BUCKETS];
    bool values[BUSTER_CHECK_FEATURE_MAP_BUCKETS];
    uint32_t buckets;
    uint32_t items;
};

// A feature's index by its name, or -1.
static int32_t check_feature_map_index(const char* name, uint32_t length)
{
    int32_t result = -1;
    for (uint32_t f = 0; f < BUSTER_CHECK_FEATURE_MAP_COUNT && result < 0; f += 1)
    {
        const char* entry = check_feature_map_features[f].name;
        result = length != 0 && strlen(entry) == length && memcmp(entry, name, length) == 0 ? (int32_t)f : -1;
    }
    return result;
}

// A processor's features by its name, or none.
static bool check_feature_map_cpu(const char* name, uint32_t length, uint64_t features[2])
{
    bool found = false;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(check_feature_map_cpus) && !found; c += 1)
    {
        const char* entry = check_feature_map_cpus[c].name;
        found = strlen(entry) == length && memcmp(entry, name, length) == 0;
        features[0] = found ? check_feature_map_cpus[c].features[0] : features[0];
        features[1] = found ? check_feature_map_cpus[c].features[1] : features[1];
    }
    return found;
}

// The bucket of feature `key` in `map`, from its hash on: its own, or the first free one.
static uint32_t check_feature_map_bucket(const CheckFeatureMap* map, uint32_t key, uint32_t hash)
{
    uint32_t bucket = hash & (map->buckets - 1);
    while (map->keys[bucket] != BUSTER_CHECK_FEATURE_MAP_EMPTY && map->keys[bucket] != key)
    {
        bucket = (bucket + 1) & (map->buckets - 1);
    }
    return bucket;
}

// Map[key] = value: the key inserted where it is new, and the table doubled where that fills it past three quarters.
static void check_feature_map_put(CheckFeatureMap* map, uint32_t key, bool value)
{
    const char* name = check_feature_map_features[key].name;
    const uint32_t hash = (uint32_t)xxh3_short((const uint8_t*)name, (uint32_t)strlen(name));
    const uint32_t bucket = check_feature_map_bucket(map, key, hash);
    if (map->keys[bucket] == BUSTER_CHECK_FEATURE_MAP_EMPTY)
    {
        map->keys[bucket] = (uint8_t)key;
        map->hashes[bucket] = hash;
        map->items += 1;
    }
    map->values[bucket] = value;
    if (map->items * 4 > map->buckets * 3 && map->buckets * 2 <= BUSTER_CHECK_FEATURE_MAP_BUCKETS)
    {
        CheckFeatureMap old = *map;
        map->buckets *= 2;
        memset(map->keys, BUSTER_CHECK_FEATURE_MAP_EMPTY, sizeof(map->keys));
        for (uint32_t i = 0; i < old.buckets; i += 1)
        {
            if (old.keys[i] != BUSTER_CHECK_FEATURE_MAP_EMPTY)
            {
                const uint32_t moved = check_feature_map_bucket(map, old.keys[i], old.hashes[i]);
                map->keys[moved] = old.keys[i];
                map->hashes[moved] = old.hashes[i];
                map->values[moved] = old.values[i];
            }
        }
    }
}

// Map[key]: whether it holds the key, and its value in *value.
static bool check_feature_map_get(const CheckFeatureMap* map, uint32_t key, bool* value)
{
    const char* name = check_feature_map_features[key].name;
    const uint32_t bucket = check_feature_map_bucket(map, key, (uint32_t)xxh3_short((const uint8_t*)name, (uint32_t)strlen(name)));
    const bool found = map->keys[bucket] == key;
    *value = found && map->values[bucket];
    return found;
}

// setFeatureEnabled: the key, then what turning it on turns on (its implications' closure) or what turning it off turns
// off (the closure of the features that imply it, itself among them), in index order, features without names left out.
static void check_feature_map_enable(CheckFeatureMap* map, uint32_t key, bool enabled)
{
    check_feature_map_put(map, key, enabled);
    uint64_t bits[2] = { 0, 0 };
    if (enabled)
    {
        bits[0] = check_feature_map_features[key].implies[0];
        bits[1] = check_feature_map_features[key].implies[1];
    }
    else
    {
        bits[key / 64] |= 1ull << (key % 64);
    }
    bool changed = bits[0] != 0 || bits[1] != 0;
    while (changed)
    {
        const uint64_t before[2] = { bits[0], bits[1] };
        for (uint32_t f = 0; f < BUSTER_CHECK_FEATURE_MAP_COUNT; f += 1)
        {
            const uint64_t* implies = check_feature_map_features[f].implies;
            const bool set = ((bits[f / 64] >> (f % 64)) & 1) != 0;
            if (enabled && set)
            {
                bits[0] |= implies[0];
                bits[1] |= implies[1];
            }
            else if (!enabled && ((implies[0] & bits[0]) != 0 || (implies[1] & bits[1]) != 0))
            {
                bits[f / 64] |= 1ull << (f % 64);
            }
        }
        changed = bits[0] != before[0] || bits[1] != before[1];
    }
    for (uint32_t f = 0; f < BUSTER_CHECK_FEATURE_MAP_COUNT; f += 1)
    {
        if (((bits[f / 64] >> (f % 64)) & 1) != 0 && check_feature_map_features[f].name[0] != 0)
        {
            check_feature_map_put(map, f, enabled);
        }
    }
}

// An item of initFeatureMap's list: a feature's index and whether it is "+", as (index << 1) | on.
static bool check_feature_map_listed_off(const uint16_t* list, uint32_t count, const char* name)
{
    const int32_t index = check_feature_map_index(name, (uint32_t)strlen(name));
    bool found = false;
    for (uint32_t i = 0; i < count && index >= 0; i += 1)
    {
        found |= list[i] == (uint16_t)((uint32_t)index << 1);
    }
    return found;
}

// The soft rule: map[then] = true where map[when] is true and the list doesn't turn `then` off.
static void check_feature_map_soft(CheckFeatureMap* map, const uint16_t* list, uint32_t count, const char* when, const char* then)
{
    const int32_t first = check_feature_map_index(when, (uint32_t)strlen(when));
    const int32_t second = check_feature_map_index(then, (uint32_t)strlen(then));
    bool value = false;
    if (first >= 0 && second >= 0 && check_feature_map_get(map, (uint32_t)first, &value) && value && !check_feature_map_listed_off(list, count, then))
    {
        check_feature_map_put(map, (uint32_t)second, true);
    }
}

// initFeatureMap's map of processor `cpu` (getFeaturesForCPU's: none where LLVM has no such processor) and the list's
// items, each (index << 1) | on, after the APX and general-regs-only expansions.
static void check_feature_map_initialize(CheckFeatureMap* map, const char* cpu, uint32_t cpu_length, const uint16_t* list, uint32_t count)
{
    memset(map->keys, BUSTER_CHECK_FEATURE_MAP_EMPTY, sizeof(map->keys));
    map->buckets = 16;
    map->items = 0;
    const int32_t sse2 = check_feature_map_index("sse2", 4);
    if (sse2 >= 0)
    {
        check_feature_map_enable(map, (uint32_t)sse2, true);
    }
    uint64_t features[2] = { 0, 0 };
    check_feature_map_cpu(cpu, cpu_length, features);
    const int32_t bit64 = check_feature_map_index("64bit", 5);
    if (bit64 >= 0)
    {
        features[bit64 / 64] &= ~(1ull << (bit64 % 64));
    }
    for (uint32_t f = 0; f < BUSTER_CHECK_FEATURE_MAP_COUNT; f += 1)
    {
        if (((features[f / 64] >> (f % 64)) & 1) != 0 && check_feature_map_features[f].name[0] != 0)
        {
            check_feature_map_enable(map, f, true);
        }
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_feature_map_enable(map, list[i] >> 1, (list[i] & 1) != 0);
    }
    check_feature_map_soft(map, list, count, "sse4.2", "popcnt");
    check_feature_map_soft(map, list, count, "sse", "mmx");
    check_feature_map_soft(map, list, count, "avx", "xsave");
    check_feature_map_soft(map, list, count, "sse4.2", "crc32");
}

// Appends a list item for feature `name` (setFeatureEnabled's sse4 is sse4.2 on and sse4.1 off; apxf its eight
// features; general-regs-only, on, -x87, -mmx and -sse), where clang knows it, to `list` of `capacity`.
static void check_feature_map_item(uint16_t* list, uint32_t* count, uint32_t capacity, const char* name, uint32_t length, bool on)
{
    static const char* const apx[] = { "egpr", "push2pop2", "ppx", "ndd", "ccmp", "nf", "zu", "jmpabs" };
    static const char* const general[] = { "x87", "mmx", "sse" };
    const bool is_apx = length == 4 && memcmp(name, "apxf", 4) == 0;
    const bool is_general = on && length == 17 && memcmp(name, "general-regs-only", 17) == 0;
    const char* const* names = is_apx ? apx : general;
    const uint32_t expanded = is_apx ? BUSTER_ARRAY_LENGTH(apx) : is_general ? BUSTER_ARRAY_LENGTH(general) : 0;
    for (uint32_t i = 0; i < expanded && *count < capacity; i += 1)
    {
        const int32_t index = check_feature_map_index(names[i], (uint32_t)strlen(names[i]));
        if (index >= 0)
        {
            list[(*count)++] = (uint16_t)(((uint32_t)index << 1) | (is_apx && on ? 1u : 0u));
        }
    }
    const bool sse4 = length == 4 && memcmp(name, "sse4", 4) == 0;
    const int32_t index = expanded != 0 ? -1 : sse4 ? check_feature_map_index(on ? "sse4.2" : "sse4.1", 6) : check_feature_map_index(name, length);
    if (index >= 0 && *count < capacity)
    {
        list[(*count)++] = (uint16_t)(((uint32_t)index << 1) | (on ? 1u : 0u));
    }
}

// The map's true keys in its iteration order, as feature indices into `order`; their count.
static uint32_t check_feature_map_order(const CheckFeatureMap* map, uint8_t* order)
{
    uint32_t count = 0;
    for (uint32_t b = 0; b < map->buckets; b += 1)
    {
        if (map->keys[b] != BUSTER_CHECK_FEATURE_MAP_EMPTY && map->values[b])
        {
            order[count++] = map->keys[b];
        }
    }
    return count;
}
