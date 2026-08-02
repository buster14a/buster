#pragma once

#include <buster/lib/apple_runtime.h>

#if defined(__x86_64__)
#define buster_msg_send_stret objc_msgSend_stret
#else
#define buster_msg_send_stret objc_msgSend
#endif

BUSTER_GLOBAL_LOCAL SEL buster_sel(const char* name)
{
    return sel_registerName(name);
}

BUSTER_GLOBAL_LOCAL id buster_msg_id(id receiver, const char* selector)
{
    return ((id (*)(id, SEL))objc_msgSend)(receiver, buster_sel(selector));
}

BUSTER_GLOBAL_LOCAL void buster_msg_void(id receiver, const char* selector)
{
    ((void (*)(id, SEL))objc_msgSend)(receiver, buster_sel(selector));
}

BUSTER_GLOBAL_LOCAL void buster_msg_void_id(id receiver, const char* selector, id argument)
{
    ((void (*)(id, SEL, id))objc_msgSend)(receiver, buster_sel(selector), argument);
}

#if BUSTER_MACOS
BUSTER_GLOBAL_LOCAL void buster_msg_void_bool(id receiver, const char* selector, bool argument)
{
    ((void (*)(id, SEL, bool))objc_msgSend)(receiver, buster_sel(selector), argument);
}

BUSTER_GLOBAL_LOCAL bool buster_msg_bool(id receiver, const char* selector)
{
    return ((bool (*)(id, SEL))objc_msgSend)(receiver, buster_sel(selector));
}
#endif

BUSTER_GLOBAL_LOCAL id buster_nsstring_from_cstring(const char* string)
{
    id ns_string_class = (id)objc_getClass("NSString");
    id allocated = buster_msg_id(ns_string_class, "alloc");
    return ((id (*)(id, SEL, const char*))objc_msgSend)(allocated, buster_sel("initWithUTF8String:"), string);
}

#if BUSTER_MACOS
BUSTER_GLOBAL_LOCAL void buster_release(id object)
{
    if (object)
    {
        buster_msg_void(object, "release");
    }
}
#endif
