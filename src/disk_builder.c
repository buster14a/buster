#include <lib.h>
#include <stdio.h>

#if BUSTER_UNITY_BUILD
#include <lib.c>
#endif

STRUCT(MBRPartitionRecord)
{
    u32 boot_indicator:8;
    u32 starting_chs:24;
    u32 os_type:8;
    u32 ending_chs:24;
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
    u8 signature[2];
} BUSTER_PACKED;

static_assert(sizeof(ProtectiveMBR) == 512);

STRUCT(GPTHeader)
{
    u8 signature[8];
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved0;
    u64 header_lba;
    u64 alternate_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8 disk_guid[16];
    u64 partition_entry_lba;
    u32 partition_entry_count;
    u32 partition_entry_size;
    u32 partition_entry_crc32;
    u32 reserved1;
};

static_assert(sizeof(GPTHeader) == 96);

typedef enum VerificationError
{
    VERIFICATION_ERROR_DISK_SUCCESS,
    VERIFICATION_ERROR_DISK_EMPTY,
    VERIFICATION_ERROR_DISK_NOT_SECTOR_SIZED,
    VERIFICATION_ERROR_MBR_DISK_SIGNATURE_NOT_ZERO,
    VERIFICATION_ERROR_UNKNOWN_NOT_ZERO,
    VERIFICATION_ERROR_MBR_ZERO_PARTITIONS_NOT_ZEROED,
    VERIFICATION_ERROR_MBR_BAD_SIGNATURE,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_BOOT_INDICATOR_NOT_ZERO,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_STARTING_CHS_NOT_512,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_OS_TYPE_NOT_PROTECTIVE,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_ENDING_CHS,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_STARTING_LBA,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA,
} VerificationError;

BUSTER_LOCAL VerificationError verify_disk(u8* disk, u64 disk_size)
{
    if (disk_size == 0)
    {
        return VERIFICATION_ERROR_DISK_EMPTY;
    }

    if (disk_size % 512 != 0)
    {
        return VERIFICATION_ERROR_DISK_NOT_SECTOR_SIZED;
    }

    let mbr = (ProtectiveMBR*)disk;
    u64 error_count = 0;

    if (mbr->unique_mbr_disk_signature != 0)
    {
        return VERIFICATION_ERROR_MBR_DISK_SIGNATURE_NOT_ZERO;
    }

    if (mbr->unknown != 0)
    {
        return VERIFICATION_ERROR_UNKNOWN_NOT_ZERO;
    }

    MBRPartitionRecord zero_record;
    memset(&zero_record, 0, sizeof(zero_record));

    for (int i = 1; i < 4; i += 1)
    {
        let record = &mbr->partition_records[i];
        bool is_equal = memcmp(record, &zero_record, sizeof(*record)) == 0;
        if (!is_equal)
        {
            return VERIFICATION_ERROR_MBR_ZERO_PARTITIONS_NOT_ZEROED;
        }
    }

    if (mbr->signature[0] != 0x55 || mbr->signature[1] != 0xaa)
    {
        return VERIFICATION_ERROR_MBR_BAD_SIGNATURE;
    }

    let gpt_partition_record = &mbr->partition_records[0];

    if (gpt_partition_record->boot_indicator != 0)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_BOOT_INDICATOR_NOT_ZERO;
    }

    if (gpt_partition_record->starting_chs != 0x200)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_STARTING_CHS_NOT_512;
    }

    if (gpt_partition_record->os_type != 0xee)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_OS_TYPE_NOT_PROTECTIVE;
    }

    if (gpt_partition_record->ending_chs != 0xffffff)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_ENDING_CHS;
    }

    if (gpt_partition_record->starting_lba != 1)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_STARTING_LBA;
    }

    if (gpt_partition_record->size_in_lba != 0xffffff)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA;
    }

    return VERIFICATION_ERROR_DISK_SUCCESS;
}

int main()
{
    return 0;
}
