#pragma once
#include <buster/base.h>
#include <buster/string8.h>
[[noreturn]] [[gnu::cold]] BUSTER_DECL void buster_failed_assertion(u32 line, String8 function_name, String8 file_path);
