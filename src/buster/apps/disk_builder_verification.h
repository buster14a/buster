#pragma once

#include <buster/lib/base.h>

#define DISK_BUILDER_SECTOR_SIZE 512

typedef struct MBRPartitionRecord MBRPartitionRecord;
struct MBRPartitionRecord
{
    u32 boot_indicator : 8;
    u32 starting_chs : 24;
    u32 os_type : 8;
    u32 ending_chs : 24;
    u32 starting_lba;
    u32 size_in_lba;
}
BUSTER_PACKED;
BUSTER_CT_CHECK(sizeof(MBRPartitionRecord) == 16);

typedef struct ProtectiveMBR ProtectiveMBR;
struct ProtectiveMBR
{
    u8 boot_code[440];
    u32 unique_mbr_disk_signature;
    u16 unknown;
    MBRPartitionRecord partition_records[4];
    u8 signature[2];
};

BUSTER_CT_CHECK(sizeof(ProtectiveMBR) == DISK_BUILDER_SECTOR_SIZE);

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

BUSTER_F_DECL u32 disk_builder_protective_mbr_size_in_lba(u64 disk_size);
BUSTER_F_DECL VerificationError verify_disk(u8* disk, u64 disk_size);
