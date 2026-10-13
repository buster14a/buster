#pragma once

#include <stddef.h>
#include <buster/lib/base.h>
#include <buster/lib/arena.h>

#define DISK_BUILDER_SECTOR_SIZE 512
#define DISK_BUILDER_IMAGE_SIZE BUSTER_MB(64)

typedef struct MBRPartitionRecord MBRPartitionRecord;
// Byte fields preserve the MBR wire layout without host bitfield or packing
// rules.
struct MBRPartitionRecord
{
    u8 boot_indicator;
    u8 starting_chs[3];
    u8 os_type;
    u8 ending_chs[3];
    u8 starting_lba[4];
    u8 size_in_lba[4];
};
BUSTER_CT_CHECK(sizeof(MBRPartitionRecord) == 16);

typedef struct ProtectiveMBR ProtectiveMBR;
struct ProtectiveMBR
{
    u8 boot_code[440];
    u8 unique_mbr_disk_signature[4];
    u8 unknown[2];
    MBRPartitionRecord partition_records[4];
    u8 signature[2];
};

BUSTER_CT_CHECK(offsetof(ProtectiveMBR, partition_records) == 446);
BUSTER_CT_CHECK(offsetof(ProtectiveMBR, signature) == 510);
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

BUSTER_F_DECL u32 disk_builder_mbr_read_u24_le(u8 const bytes[3]);
BUSTER_F_DECL u32 disk_builder_mbr_read_u32_le(u8 const bytes[4]);
BUSTER_F_DECL void disk_builder_mbr_write_u24_le(u8 bytes[3], u32 value);
BUSTER_F_DECL void disk_builder_mbr_write_u32_le(u8 bytes[4], u32 value);
BUSTER_F_DECL u32 disk_builder_protective_mbr_size_in_lba(u64 disk_size);
BUSTER_F_DECL VerificationError verify_disk(u8* disk, u64 disk_size);

typedef enum DiskBuilderReferenceStatus
{
    DISK_BUILDER_REFERENCE_OK,
    DISK_BUILDER_REFERENCE_INVALID_ARGUMENT,
    DISK_BUILDER_REFERENCE_OPEN_FAILED,
    DISK_BUILDER_REFERENCE_STATS_FAILED,
    DISK_BUILDER_REFERENCE_NOT_REGULAR,
    DISK_BUILDER_REFERENCE_SIZE_MISMATCH,
    DISK_BUILDER_REFERENCE_ALLOCATION_FAILED,
    DISK_BUILDER_REFERENCE_READ_FAILED,
    DISK_BUILDER_REFERENCE_SIZE_CHANGED,
    DISK_BUILDER_REFERENCE_CLOSE_FAILED,
} DiskBuilderReferenceStatus;

typedef struct DiskBuilderReferenceResult DiskBuilderReferenceResult;
struct DiskBuilderReferenceResult
{
    ByteSlice bytes;
    DiskBuilderReferenceStatus status;
    u64 observed_size;
    u64 transferred;
    u32 error;
    u32 cleanup_error;
};

BUSTER_F_DECL DiskBuilderReferenceResult disk_builder_reference_read(Arena* arena, String8 path);
BUSTER_F_DECL bool disk_builder_reference_is_valid(DiskBuilderReferenceResult reference);
BUSTER_F_DECL bool disk_builder_reference_compare_and_publish(DiskBuilderReferenceResult reference, ByteSlice image,
                                                               String8 output_path, bool* match_out);
