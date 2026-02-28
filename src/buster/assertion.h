#pragma once
#include <buster/base.h>
#include <buster/string8.h>

#ifdef NDEBUG
#define BUSTER_CHECK(ok) ({ if (BUSTER_UNLIKELY(!(ok))) BUSTER_UNREACHABLE(); })
#else
#define BUSTER_CHECK(ok) ({ if (BUSTER_UNLIKELY(!(ok))) buster_failed_assertion(__LINE__, S8(__FUNCTION__), S8(__FILE__)); })
#endif

[[noreturn]] [[gnu::cold]] BUSTER_DECL void buster_failed_assertion(u32 line, String8 function_name, String8 file_path);
