#include <lib.h>
#include <stdio.h>

#if UNITY_BUILD
#include <lib.c>
#endif

STRUCT(MBRPartitionRecord)
{
    u8 boot_indicator;
    u8 starting_chs[3];
    u8 os_type;
    u8 ending_chs[3];
    u32 starting_lba;
    u32 size_in_lba;
} BUSTER_PACKED;
static_assert(sizeof(MBRPartitionRecord) == 16);

STRUCT(ProtectiveMBR)
{
    u8 boot_code[440];
    u32 unique_mbr_disk_signature;
    u16 unknown;
    MBRPartitionRecord partition_records[4];
    u16 signature;
} BUSTER_PACKED;

static_assert(sizeof(ProtectiveMBR) == 512);

LOCAL bool verify_disk(Arena* logger, u8* disk, u64 disk_size)
{
    let mbr = (ProtectiveMBR*)disk;
    u64 error_count = 0;

    if (mbr->unique_mbr_disk_signature != 0)
    {
        return false;
    }

    if (mbr->unknown != 0)
    {
        return false;
    }

    return true;
}

int main()
{
    return 0;
}
