#ifndef BUSTER_NATIVE_RETIREMENT_DARWIN_AVAILABILITY_H
#define BUSTER_NATIVE_RETIREMENT_DARWIN_AVAILABILITY_H

#include_next <Availability.h>

// The pinned Darwin SDK only defines its legacy iPhone availability names
// when the frontend reports the availability attribute. The supported iOS
// census target has a minimum version of at least 2.0, so this one annotation
// carries no availability restriction for that target. Leave every later
// version name undefined, and do not change the frontend's attribute query.
#if defined(__BUSTER__)
#if defined(__has_builtin) && defined(__has_attribute)
#if __has_builtin(__is_target_os) && __is_target_os(ios) && !__has_attribute(availability)
#if defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && defined(__IPHONE_2_0)
#if __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_2_0
#ifndef __AVAILABILITY_INTERNAL__IPHONE_2_0
#define __AVAILABILITY_INTERNAL__IPHONE_2_0
#endif
#endif
#endif
#endif
#endif
#endif

#endif
