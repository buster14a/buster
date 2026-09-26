#pragma once
#include <buster/lib/os.h>

typedef struct FileReadOptions FileReadOptions;
struct FileReadOptions
{
    u32 start_padding;
    u32 start_alignment;
    u32 end_padding;
    u32 end_alignment;
    u32 map_required; // file_map_read returns an empty result instead of falling back to file_read.
};

// Identity captured from the same open descriptor that supplied `bytes`.
// POSIX uses device/inode; Windows uses volume serial/file index. `valid` is
// false for non-filesystem namespaces such as Android APK assets.
typedef struct FileIdentity FileIdentity;
struct FileIdentity
{
    u64 device;
    u64 index;
    bool valid;
};

typedef struct FileMapRead FileMapRead;
struct FileMapRead
{
    ByteSlice bytes;
    FileIdentity identity;
    void* mapped_pointer;
    u64 mapped_size;
    void* mapped_handle;
};

typedef struct FileReadResult FileReadResult;
struct FileReadResult
{
    ByteSlice bytes;
    FileIdentity identity;
    OsFileReadStatus status;
    OsError error;
};

// Nonzero initial sizes use a bounded read-exact size snapshot: later appended bytes
// are excluded; premature EOF is failure. Zero-sized descriptors stream to EOF.
// Success (including an empty file) has a nonnull pointer. Failure exposes no
// prefix and restores the read allocation. Mappings require stable input files.
BUSTER_F_DECL FileReadResult file_read_checked(Arena* arena, String8 path, FileReadOptions options);
BUSTER_F_DECL ByteSlice file_read(Arena* arena, String8 path, FileReadOptions options);
BUSTER_F_DECL FileMapRead file_map_read(Arena* arena, String8 path, FileReadOptions options);
BUSTER_F_DECL void file_map_unmap(FileMapRead map);
// Completion includes close. On error a prefix may remain at the destination;
// atomic replacement and crash durability are separate contracts.
BUSTER_F_DECL OsFileTransferResult file_write_checked(String8 path, ByteSlice content, OpenPermissions permissions);
BUSTER_F_DECL bool file_write(String8 path, ByteSlice content);

// Atomic publication for complete in-memory artifacts. The destination is
// inspected without following its final link, then an exclusively-created file
// in the same directory is written, flushed, closed, and atomically renamed.
// Every result other than FILE_PUBLISH_PUBLISHED leaves an existing destination
// byte-identical and a missing destination absent. Handled failures remove the
// staging file; cleanup_error reports a failed close/delete without replacing
// the primary failure.
//
// A directory, link/reparse point, or special destination is refused. Windows
// also refuses the read-only attribute. A new file is created 0644 or 0755
// before umask. On POSIX replacement preserves the old 0777 bits except that
// all execute bits are set for an executable publication and cleared for an
// ordinary one. Ownership, ACLs, extended attributes, timestamps, Windows
// attributes/streams, and multi-file transactionality are not preserved.
// Flushing the staging file catches delayed write failures before publication;
// the containing directory is not flushed, so power-loss durability is not
// promised. Staging names are recognizable after an uncatchable process exit.
typedef enum FilePublishStatus
{
    FILE_PUBLISH_FAILED,
    FILE_PUBLISH_PUBLISHED,
    FILE_PUBLISH_UNSUPPORTED_DESTINATION,
    FILE_PUBLISH_INVALID_STAGING,
} FilePublishStatus;

typedef struct FilePublishResult FilePublishResult;
struct FilePublishResult
{
    FilePublishStatus status;
    OsError error;
    OsError cleanup_error;
};

BUSTER_F_DECL FilePublishResult file_publish_checked(String8 path, ByteSlice content, OpenPermissions permissions);
BUSTER_F_DECL FilePublishResult file_publish_slices_checked(String8 path, ByteSlice const* slices, u64 slice_count, OpenPermissions permissions);
BUSTER_F_DECL bool file_publish_slices(String8 path, ByteSlice const* slices, u64 slice_count);
BUSTER_F_DECL bool file_publish(String8 path, ByteSlice content);
BUSTER_F_DECL bool file_publish_executable(String8 path, ByteSlice content);

typedef struct CopyFileArguments CopyFileArguments;
struct CopyFileArguments
{
    String8 original_path;
    String8 new_path;
};

typedef enum FileCopyStatus
{
    // `error` holds the first failure.
    FILE_COPY_FAILED,
    FILE_COPY_PUBLISHED,
    // One spelling, or new_path names the file opened as the source.
    FILE_COPY_SAME_FILE,
    // new_path is a link, directory or special file; it is not followed.
    FILE_COPY_UNSUPPORTED_DESTINATION,
} FileCopyStatus;

typedef struct FileCopyResult FileCopyResult;
struct FileCopyResult
{
    FileCopyStatus status;
    OsError error;
    // First failure releasing the source or staging file after a failure or
    // refusal. It never replaces `error`; a failed staging deletion leaves that
    // file behind.
    OsError cleanup_error;
};

// Streams original_path into a staging file created exclusively in new_path's
// directory, then renames it over new_path. Nothing at new_path changes before
// that rename: every other result leaves an existing destination byte-identical
// and a missing one absent. The rename is the last fallible step, so
// FILE_COPY_PUBLISHED means exactly that new_path names the complete copy.
//
// Aliases are judged by identity (device/inode; Windows volume serial and file
// index) of the opened source and the inspected destination, so "./" and ".."
// spellings, hard links, a source symlink to the destination and case aliases
// on case-insensitive filesystems are FILE_COPY_SAME_FILE, with nothing
// modified. A source symlink is followed. A destination symbolic link (Windows:
// any reparse point), directory or special file is refused, neither followed
// nor replaced. An existing destination must still be writable: POSIX opens it
// write-only without truncation to check; Windows refuses the read-only attribute.
//
// Replacement publishes a new file: other hard links to the old destination
// keep the old bytes. POSIX permission bits (0777) of a replaced destination
// are kept, and a new destination is created 0644 before umask. Ownership,
// ACLs, extended attributes, timestamps and Windows attributes or streams are
// not preserved. The rename is namespace-atomic; nothing is flushed, so crash
// durability is not promised. The source is streamed to EOF in bounded chunks
// without a size snapshot; concurrent changes to either path are not isolated.
BUSTER_F_DECL FileCopyResult file_copy_checked(CopyFileArguments arguments);
// True only for FILE_COPY_PUBLISHED.
BUSTER_F_DECL bool file_copy(CopyFileArguments arguments);


#if BUSTER_ANDROID
struct AAssetManager;
extern struct AAssetManager* buster_android_asset_manager;
extern String8 buster_android_internal_data_path;
#endif
