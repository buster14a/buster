#pragma once
#include <buster/base.h>
#include <buster/os.h>

BUSTER_F_DECL TimeDataType timestamp_take(void);
BUSTER_F_DECL u64 timestamp_ns_between(TimeDataType start, TimeDataType end);
