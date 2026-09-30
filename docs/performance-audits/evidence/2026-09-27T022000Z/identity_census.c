// DISPOSABLE identity census implementation; see identity_census.h.
#include <buster/lib/identity_census.h>

#if BUSTER_IDENTITY_CENSUS
#include <stdio.h>
#include <stdlib.h>

#define IDENTITY_CENSUS_SLOT_COUNT (1u << 18)

typedef struct IdentityCensusSlot IdentityCensusSlot;
struct IdentityCensusSlot
{
    void* site0;
    void* site1;
    u64 calls;
    u64 bytes;
    u64 aux;
    u32 phase;
    u32 kind;
    u32 used;
    u32 reserved;
};

BUSTER_GLOBAL_LOCAL IdentityCensusSlot identity_census_slots[IDENTITY_CENSUS_SLOT_COUNT];
BUSTER_GLOBAL_LOCAL u32 identity_census_current_phase;
BUSTER_GLOBAL_LOCAL u32 identity_census_registered;
BUSTER_GLOBAL_LOCAL u32 identity_census_lock;
BUSTER_GLOBAL_LOCAL u32 identity_census_overflowed;
extern char __executable_start;

BUSTER_GLOBAL_LOCAL void identity_census_dump(void)
{
    char const* path = getenv("BUSTER_IDENTITY_CENSUS_OUT");
    if (path && path[0])
    {
        FILE* file = fopen(path, "a");
        if (file)
        {
            fprintf(file, "# identity-census v1 overflowed=%u\n", identity_census_overflowed);
            for (u32 index = 0; index < IDENTITY_CENSUS_SLOT_COUNT; index += 1)
            {
                IdentityCensusSlot* slot = &identity_census_slots[index];
                if (slot->used)
                {
                    u64 offset0 = slot->site0 ? (u64)((char*)slot->site0 - &__executable_start) : 0;
                    u64 offset1 = slot->site0 ? (slot->site1 ? (u64)((char*)slot->site1 - &__executable_start) : 0) : (u64)slot->site1;
                    fprintf(file, "%u\t%u\t%llx\t%llx\t%llu\t%llu\t%llu\n", slot->phase, slot->kind, (unsigned long long)offset0,
                            (unsigned long long)offset1, (unsigned long long)slot->calls, (unsigned long long)slot->bytes,
                            (unsigned long long)slot->aux);
                }
            }
            fclose(file);
        }
    }
}

void identity_census_phase_set(IdentityCensusPhase phase)
{
    identity_census_current_phase = (u32)phase;
}

BUSTER_GLOBAL_LOCAL void identity_census_add(u32 kind, void* site0, void* site1, u64 bytes, u64 aux)
{
    while (__atomic_exchange_n(&identity_census_lock, 1, __ATOMIC_ACQUIRE))
    {
    }
    if (!identity_census_registered)
    {
        identity_census_registered = 1;
        atexit(identity_census_dump);
    }
    u32 phase = identity_census_current_phase;
    u64 key = (u64)(uintptr_t)site0 * UINT64_C(0x9E3779B97F4A7C15) ^ (u64)(uintptr_t)site1 * UINT64_C(0xC2B2AE3D27D4EB4F) ^
              ((u64)phase << 40) ^ ((u64)kind << 48);
    key ^= key >> 29;
    u32 slot_index = (u32)key & (IDENTITY_CENSUS_SLOT_COUNT - 1);
    u32 steps = 0;
    for (;;)
    {
        IdentityCensusSlot* slot = &identity_census_slots[slot_index];
        if (!slot->used)
        {
            *slot = (IdentityCensusSlot){.site0 = site0, .site1 = site1, .phase = phase, .kind = kind, .used = 1};
        }
        if (slot->site0 == site0 && slot->site1 == site1 && slot->phase == phase && slot->kind == kind)
        {
            slot->calls += 1;
            slot->bytes += bytes;
            slot->aux += aux;
            break;
        }
        slot_index = (slot_index + 1) & (IDENTITY_CENSUS_SLOT_COUNT - 1);
        steps += 1;
        if (steps == IDENTITY_CENSUS_SLOT_COUNT)
        {
            identity_census_overflowed = 1;
            break;
        }
    }
    __atomic_store_n(&identity_census_lock, 0, __ATOMIC_RELEASE);
}

void identity_census_record(IdentityCensusKind kind, void* site0, void* site1, u64 bytes, u64 aux)
{
    identity_census_add((u32)kind, site0, site1, bytes, aux);
}

void identity_census_record_tag(IdentityCensusKind kind, u32 tag, u64 bytes, u64 aux)
{
    identity_census_add((u32)kind, 0, (void*)(uintptr_t)tag, bytes, aux);
}
#endif
