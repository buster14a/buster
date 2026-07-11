#pragma once

#include <string.h>

#include <buster/rendering.h>
#include <buster/string.h>
#include <buster/os.h>
#include <buster/arena.h>
#include <buster/file.h>
#include <buster/font_provider.h>
#include <buster/window.h>
#include <buster/shaders/rect_shared.h>
#include <buster/shaders/paths.h>

#ifndef BUSTER_USE_SLANG_SHADERS
#define BUSTER_USE_SLANG_SHADERS 0
#endif

#if defined(__APPLE__)
#include <buster/apple_runtime.h>
#endif

#if BUSTER_USE_METAL && BUSTER_USE_SLANG_SHADERS
#include <buster/shaders/metal.h>
#endif

#if BUSTER_USE_D3D12 && BUSTER_USE_SLANG_SHADERS
#include <buster/shaders/d3d12.h>
#endif

typedef struct RectVertex RectVertex;

typedef enum BusterPipeline
{
    BUSTER_PIPELINE_RECT,
    BUSTER_PIPELINE_COUNT,
} BusterPipeline;

#define BUSTER_GPU_VALIDATION_ENABLED (!BUSTER_OPTIMIZE || BUSTER_SANITIZE)
