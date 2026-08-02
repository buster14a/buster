#pragma once
#include <buster/lib/base.h>
#include <buster/lib/os.h>

BUSTER_F_DECL TimeDataType timestamp_take(void);
BUSTER_F_DECL u64 timestamp_ns_between(TimeDataType start, TimeDataType end);
