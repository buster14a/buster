#pragma once
#include <buster/base.h>
#include <buster/os.h>
BUSTER_DECL TimeDataType timestamp_take();
BUSTER_DECL u64 timestamp_ns_between(TimeDataType start, TimeDataType end);
