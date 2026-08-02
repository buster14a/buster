#pragma once

#include <string.h>

#include <buster/lib/rendering.h>
#include <buster/lib/string.h>
#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/file.h>
#include <buster/lib/font_provider.h>
#include <buster/lib/window.h>
#include <buster/lib/shaders/rect_shared.h>
#include <buster/lib/shaders/paths.h>

#ifndef BUSTER_USE_SLANG_SHADERS
#define BUSTER_USE_SLANG_SHADERS 0
#endif

#if defined(__APPLE__)
#include <buster/lib/apple_runtime.h>
#endif

#if BUSTER_USE_METAL && BUSTER_USE_SLANG_SHADERS
#include <buster/lib/shaders/metal.h>
#endif

#if BUSTER_USE_D3D12 && BUSTER_USE_SLANG_SHADERS
#include <buster/lib/shaders/d3d12.h>
#endif

typedef struct RectVertex RectVertex;

typedef enum BusterPipeline
{
    BUSTER_PIPELINE_RECT,
    BUSTER_PIPELINE_COUNT,
} BusterPipeline;

#define BUSTER_GPU_VALIDATION_ENABLED (!BUSTER_OPTIMIZE || BUSTER_SANITIZE)
