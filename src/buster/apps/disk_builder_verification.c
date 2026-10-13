#include <buster/apps/disk_builder_verification.h>
#include <string.h>

// The MBR length counts sectors after LBA 0. Keep the builder's u32
// saturation rule in one place; the separate CHS end field remains 0xffffff.
u32 disk_builder_protective_mbr_size_in_lba(u64 disk_size)
{
    u64 sector_count = disk_size / DISK_BUILDER_SECTOR_SIZE;
    u64 size_in_lba = sector_count ? sector_count - 1 : 0;
    u32 result = size_in_lba > UINT32_MAX ? UINT32_MAX : (u32)size_in_lba;
    return result;
}

// The three records after the protective entry must be entirely zero; the scan
// stops at the first non-zero one because that is already the whole answer.
BUSTER_GLOBAL_LOCAL bool verify_disk_reserved_partitions_zeroed(ProtectiveMBR const* mbr)
{
    MBRPartitionRecord zero_record;
    memset(&zero_record, 0, sizeof(zero_record));

    bool result = true;
    for (int i = 1; i < 4 && result; i += 1)
    {
        result = memcmp(&mbr->partition_records[i], &zero_record, sizeof(zero_record)) == 0;
    }

    return result;
}

// Reports the first thing wrong with the image. The checks are ordered so that
// each one only runs once its predecessors have vouched for the bytes it reads:
// the sector-size check guards the MBR cast, and the MBR checks guard the
// protective partition record.
VerificationError verify_disk(u8* disk, u64 disk_size)
{
    VerificationError result = VERIFICATION_ERROR_DISK_SUCCESS;
    if (disk_size == 0)
    {
        result = VERIFICATION_ERROR_DISK_EMPTY;
    }
    else if (disk_size % DISK_BUILDER_SECTOR_SIZE != 0)
    {
        result = VERIFICATION_ERROR_DISK_NOT_SECTOR_SIZED;
    }
    else
    {
        ProtectiveMBR* mbr = (ProtectiveMBR*)disk;
        MBRPartitionRecord* gpt_partition_record = &mbr->partition_records[0];

        if (mbr->unique_mbr_disk_signature != 0)
        {
            result = VERIFICATION_ERROR_MBR_DISK_SIGNATURE_NOT_ZERO;
        }
        else if (mbr->unknown != 0)
        {
            result = VERIFICATION_ERROR_UNKNOWN_NOT_ZERO;
        }
        else if (!verify_disk_reserved_partitions_zeroed(mbr))
        {
            result = VERIFICATION_ERROR_MBR_ZERO_PARTITIONS_NOT_ZEROED;
        }
        else if (mbr->signature[0] != 0x55 || mbr->signature[1] != 0xaa)
        {
            result = VERIFICATION_ERROR_MBR_BAD_SIGNATURE;
        }
        else if (gpt_partition_record->boot_indicator != 0)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_BOOT_INDICATOR_NOT_ZERO;
        }
        else if (gpt_partition_record->starting_chs != 0x200)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_STARTING_CHS_NOT_512;
        }
        else if (gpt_partition_record->os_type != 0xee)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_OS_TYPE_NOT_PROTECTIVE;
        }
        else if (gpt_partition_record->ending_chs != 0xffffff)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_ENDING_CHS;
        }
        else if (gpt_partition_record->starting_lba != 1)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_STARTING_LBA;
        }
        else if (gpt_partition_record->size_in_lba != disk_builder_protective_mbr_size_in_lba(disk_size))
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA;
        }
    }

    return result;
}
