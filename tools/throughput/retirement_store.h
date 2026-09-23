/* Linux service-owned durable result publication for #1023.
 * The bench service owns the directory descriptor, workspace and authority
 * handoff. begin/publish create and seal one file without replacement;
 * validate rereads every sealed inode. receipt_authority returns a reference
 * for the private service channel, never an authority embedded in the bundle.
 * Definitions live in tools/bench_service/retirement_result.c.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_STORE_H
#define BUSTER_THROUGHPUT_RETIREMENT_STORE_H
#ifdef __linux__
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#define TP_RETIREMENT_STORE_FILES 4096u
#define TP_RETIREMENT_STORE_PATH_BYTES 192u
#define TP_RETIREMENT_STORE_FILE_BYTES UINT64_C(67108864)
#define TP_RETIREMENT_STORE_TOTAL_BYTES (UINT64_C(128) * 1024 * 1024 * 1024)
#define TP_RETIREMENT_EXECUTION_RECEIPT_PATH "retirement-execution-receipt.json"

typedef struct TpRetirementStoredFile
{
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char sha256[65];
    uint64_t bytes;
    dev_t device;
    ino_t inode;
    dev_t parent_device;
    ino_t parent_inode;
    uid_t owner;
} TpRetirementStoredFile;

typedef struct TpRetirementStore
{
    int root;
    struct stat root_identity;
    TpRetirementStoredFile* files;
    unsigned count, capacity, planned_files, external_entries;
    uint64_t total, external_bytes;
    int active, failed, authority_issued, planned;
} TpRetirementStore;

typedef struct TpRetirementPending
{
    FILE* stream;
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char temporary[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    dev_t parent_device;
    ino_t parent_inode;
    uint64_t limit;
} TpRetirementPending;

typedef struct TpRetirementReceiptAuthority
{
    /* The service must persist and send this on its authenticated private
     * channel. Copying these fields into a bundle does not authenticate it. */
    char job[129], plan_sha256[65], context_sha256[65], receipt_sha256[65], authority_sha256[65];
    uint64_t attempt;
} TpRetirementReceiptAuthority;

int tp_retirement_store_open(TpRetirementStore* store, int root,
                             TpRetirementStoredFile* workspace, unsigned capacity);
/* Account for every service-owned payload and all directory/control entries
 * the bundle validator will inventory outside this store. The caller supplies
 * a conservative byte reservation for those external files. */
int tp_retirement_store_plan(TpRetirementStore* store, unsigned owned_files,
                             unsigned external_entries, uint64_t external_bytes);
int tp_retirement_store_begin(TpRetirementStore* store, char const* path,
                              uint64_t limit, TpRetirementPending* pending);
int tp_retirement_store_publish(TpRetirementStore* store, TpRetirementPending* pending,
                                uint64_t bytes, char const* sha256);
void tp_retirement_store_abort(TpRetirementStore* store, TpRetirementPending* pending);
int tp_retirement_store_validate(TpRetirementStore* store);
int tp_retirement_store_receipt_authority(TpRetirementStore* store, int authority_root, char const* path,
    char const* job, uint64_t attempt, char const* plan_sha256, char const* context_sha256,
    TpRetirementReceiptAuthority* authority);
int tp_retirement_store_authority_matches(TpRetirementStore* store, int authority_root, char const* path,
    char const* job, uint64_t attempt, char const* plan_sha256, char const* context_sha256,
    TpRetirementReceiptAuthority const* trusted);
void tp_retirement_store_close(TpRetirementStore* store);
#endif
#endif
