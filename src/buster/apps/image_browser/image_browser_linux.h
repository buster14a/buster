#pragma once

// Linux image-browser filesystem and persistent-loader ownership boundary.
// catalog_open/destroy own one bounded, immutable flat path snapshot.
// worker_start creates one OS thread. submit borrows a catalog path until take
// or stop_join; catalog storage must outlive the successful join.
// set_generation supersedes reads; one synchronous bounded decode can finish.
// take moves the sole result. stop_join returns join success separately from
// result presence and retains every resource if the wait fails.
// Only the worker owns encoded/output/scratch arenas until result publication.

#include <buster/apps/image_browser/image_browser_state.h>
#include <buster/lib/os.h>
#include <pthread.h>

#define IMAGE_BROWSER_MAX_FILES 4096u
#define IMAGE_BROWSER_MAX_SCAN_RECORDS 32768u
#define IMAGE_BROWSER_MAX_PATH_BYTES 4095u
#define IMAGE_BROWSER_MAX_CATALOG_BYTES BUSTER_MB(1)
#define IMAGE_BROWSER_READ_CHUNK BUSTER_KB(64)

typedef struct ImageBrowserCatalog ImageBrowserCatalog;
struct ImageBrowserCatalog
{
    Arena* arena;
    SliceString8 paths;
    u64 initial_index;
};

typedef struct ImageBrowserWorker ImageBrowserWorker;
struct ImageBrowserWorker
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    OsThreadHandle* thread;
    ImageBrowserRequest request;
    String8 path;
    ImageBrowserResult result;
    u64 generation;
    bool initialized;
    bool stopping;
    bool queued;
    bool active;
    bool has_result;
};

BUSTER_F_DECL bool image_browser_catalog_open(String8 input, ImageBrowserCatalog* catalog, s32* error);
BUSTER_F_DECL void image_browser_catalog_destroy(ImageBrowserCatalog* catalog);
BUSTER_F_DECL bool image_browser_worker_start(ImageBrowserWorker* worker, s32* error);
BUSTER_F_DECL void image_browser_worker_set_generation(ImageBrowserWorker* worker, u64 generation);
BUSTER_F_DECL bool image_browser_worker_submit(ImageBrowserWorker* worker, ImageBrowserRequest request, String8 path);
BUSTER_F_DECL bool image_browser_worker_take(ImageBrowserWorker* worker, ImageBrowserResult* result);
// Failed join leaves worker, queued/active storage and result owned for retry.
BUSTER_F_DECL bool image_browser_worker_stop_join(ImageBrowserWorker* worker, ImageBrowserResult* result, bool* has_result);
