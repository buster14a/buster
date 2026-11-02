// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The output options shared by the C driver and link-foreign. Input ordering and library state belong to each
// caller. Arguments are borrowed NUL-terminated OS strings; retained paths are bounded slices, joined once after
// parsing. No option is accepted speculatively: false means the caller must handle it or diagnose it.
#ifndef BUSTER_LINKER_OPTIONS_INCLUDED
#define BUSTER_LINKER_OPTIONS_INCLUDED

typedef struct LinkerOptions LinkerOptions;
struct LinkerOptions
{
    String8 entry;
    String8 runpath;
    String8* paths;
    uint32_t path_count;
    uint32_t path_capacity;
    bool old_dtags;
    const char* error;
    const char* error_value;
    bool missing;
    bool resource;
};

static bool linker_options_initialize(LinkerOptions* options, uint32_t capacity, Arena* arena)
{
    *options = (LinkerOptions){
        .paths = arena_push_array(arena, capacity, sizeof(String8), _Alignof(String8)),
        .path_capacity = capacity,
    };
    options->resource = options->paths == NULL;
    return !options->resource;
}

static bool linker_options_parse(LinkerOptions* options, int count, char** arguments, int* index)
{
    const char* argument = arguments[*index];
    const bool path = strcmp(argument, "-rpath") == 0 || strcmp(argument, "--rpath") == 0 ||
                      strncmp(argument, "-rpath=", 7) == 0 || strncmp(argument, "--rpath=", 8) == 0 ||
                      (argument[0] == '-' && argument[1] == 'R');
    const bool entry = strcmp(argument, "-e") == 0 || strcmp(argument, "--entry") == 0 || strncmp(argument, "--entry=", 8) == 0;
    const bool keyword = strncmp(argument, "-z", 2) == 0;
    bool handled = true;
    if (path || entry || keyword)
    {
        // -R/path and -znow are the joined short forms. An equals sign inside their value is literal.
        const bool joined = (argument[1] == 'R' || keyword) && argument[2] != 0;
        const char* equals = !joined ? strchr(argument, '=') : NULL;
        const char* value = joined ? argument + 2 : equals != NULL ? equals + 1 : NULL;
        if (value == NULL && *index + 1 < count)
        {
            *index += 1;
            value = arguments[*index];
        }
        if (value == NULL || (!path && value[0] == 0))
        {
            options->error = argument;
            options->missing = true;
        }
        else if (path)
        {
            if (options->path_count < options->path_capacity)
            {
                options->paths[options->path_count++] = (String8){
                    .pointer = (char*)value,
                    .length = strlen(value),
                };
            }
            else
            {
                options->resource = true;
            }
        }
        else if (entry)
        {
            options->entry = (String8){
                .pointer = (char*)value,
                .length = strlen(value),
            };
        }
        else if (strcmp(value, "now") != 0 && strcmp(value, "relro") != 0 && strcmp(value, "noexecstack") != 0 && strcmp(value, "text") != 0)
        {
            options->error = argument;
            options->error_value = joined ? NULL : value;
        }
    }
    else if (strcmp(argument, "--enable-new-dtags") == 0 || strcmp(argument, "--disable-new-dtags") == 0)
    {
        options->old_dtags = strcmp(argument, "--disable-new-dtags") == 0;
    }
    else if (strcmp(argument, "--build-id") == 0 || strcmp(argument, "--eh-frame-hdr") == 0 || strcmp(argument, "--hash-style=gnu") == 0)
    {
        // The writer always emits these. Its imports are eagerly resolved through the GOT (no lazy PLT), its GOT is
        // RELRO, its stack non-executable, and relocations into read-only memory are refused: the -z keywords above.
    }
    else
    {
        handled = false;
    }
    return handled;
}

static bool linker_options_finish(LinkerOptions* options, Arena* arena)
{
    const uint32_t count = options->path_count;
    const String8* const paths = options->paths;
    uint64_t length = count != 0 ? count - 1 : 0;
    bool valid = options->error == NULL && !options->resource;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        valid = paths[i].length <= UINT32_MAX && length < UINT32_MAX - paths[i].length;
        length += valid ? paths[i].length : 0;
    }
    if (valid && count != 0)
    {
        char* joined = arena_push(arena, length + 1, 1);
        valid = joined != NULL;
        uint64_t offset = 0;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            if (i != 0)
            {
                joined[offset++] = ':';
            }
            memcpy(joined + offset, paths[i].pointer, paths[i].length);
            offset += paths[i].length;
        }
        if (valid)
        {
            joined[length] = 0;
            options->runpath = (String8){
                .pointer = joined,
                .length = length,
            };
        }
    }
    options->resource |= !valid && options->error == NULL;
    return valid;
}

static void linker_options_diagnose(const LinkerOptions* options)
{
    if (options->resource)
    {
        fprintf(stderr, "error: linker arguments exceed available memory or ELF string limits\n");
    }
    else if (options->missing)
    {
        fprintf(stderr, "error: missing argument to linker option '%s'\n", options->error);
    }
    else
    {
        fprintf(stderr, "error: unsupported linker option '%s%s%s'\n", options->error,
                options->error_value != NULL ? " " : "", options->error_value != NULL ? options->error_value : "");
    }
}

#endif
