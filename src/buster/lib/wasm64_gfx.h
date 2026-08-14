#pragma once

// Experimental, versioned host boundary for Wasm64 graphical programs.  It
// is deliberately a small Buster-owned abstraction rather than a copy of
// wasi-gfx resource layouts.  A host may implement it directly or bridge it
// to pinned wasi-gfx:surface/frame-buffer packages.
#if defined(__wasm64__)
#define BUSTER_WASM64_IMPORT(link_name) __asm__(link_name)
#else
#define BUSTER_WASM64_IMPORT(link_name)
#endif

// Keep this target-library header freestanding: including the compiler's host
// base header would pull in libc headers that a wasm64-unknown-freestanding
// sysroot deliberately does not provide.
typedef unsigned char BusterWasm64U8;
typedef unsigned int BusterWasm64U32;
typedef int BusterWasm64S32;
typedef unsigned long long BusterWasm64U64;
typedef BusterWasm64U32 BusterWasm64Bool;
typedef BusterWasm64U64 BusterWasm64Window;

typedef enum BusterWasm64WindowEventKind
{
    BUSTER_WASM64_WINDOW_EVENT_NONE,
    BUSTER_WASM64_WINDOW_EVENT_CLOSE,
    BUSTER_WASM64_WINDOW_EVENT_RESIZE,
    BUSTER_WASM64_WINDOW_EVENT_POINTER,
    BUSTER_WASM64_WINDOW_EVENT_KEY,
    BUSTER_WASM64_WINDOW_EVENT_COUNT,
} BusterWasm64WindowEventKind;

typedef struct BusterWasm64WindowEvent BusterWasm64WindowEvent;
struct BusterWasm64WindowEvent
{
    BusterWasm64U64 timestamp_ns;
    BusterWasm64S32 x;
    BusterWasm64S32 y;
    BusterWasm64U32 width;
    BusterWasm64U32 height;
    BusterWasm64U32 code;
    BusterWasm64WindowEventKind kind;
};

typedef struct BusterWasm64FrameBuffer BusterWasm64FrameBuffer;
struct BusterWasm64FrameBuffer
{
    BusterWasm64U8* pixels;
    BusterWasm64U64 byte_length;
    BusterWasm64U32 width;
    BusterWasm64U32 height;
    BusterWasm64U32 stride;
    BusterWasm64U32 format;
};

BusterWasm64Window buster_wasm64_window_create(BusterWasm64U32 width, BusterWasm64U32 height, BusterWasm64U32 flags)
    BUSTER_WASM64_IMPORT("buster:gfx/window@0.1.0#create");
void buster_wasm64_window_destroy(BusterWasm64Window window)
    BUSTER_WASM64_IMPORT("buster:gfx/window@0.1.0#destroy");
BusterWasm64Bool buster_wasm64_window_poll(BusterWasm64Window window, BusterWasm64WindowEvent* event)
    BUSTER_WASM64_IMPORT("buster:gfx/window@0.1.0#poll");
BusterWasm64Bool buster_wasm64_frame_buffer_acquire(BusterWasm64Window window, BusterWasm64FrameBuffer* frame)
    BUSTER_WASM64_IMPORT("buster:gfx/frame-buffer@0.1.0#acquire");
void buster_wasm64_frame_buffer_present(BusterWasm64Window window)
    BUSTER_WASM64_IMPORT("buster:gfx/frame-buffer@0.1.0#present");

#undef BUSTER_WASM64_IMPORT
