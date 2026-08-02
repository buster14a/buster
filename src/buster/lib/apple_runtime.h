#pragma once

#include <buster/lib/base.h>

#if defined(__APPLE__)
#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>

typedef unsigned long BusterNSUInteger;
typedef signed long BusterNSInteger;
typedef double BusterCGFloat;

typedef struct BusterCGPoint BusterCGPoint;
struct BusterCGPoint
{
    BusterCGFloat x;
    BusterCGFloat y;
};

typedef struct BusterCGSize BusterCGSize;
struct BusterCGSize
{
    BusterCGFloat width;
    BusterCGFloat height;
};

typedef struct BusterCGRect BusterCGRect;
struct BusterCGRect
{
    BusterCGPoint origin;
    BusterCGSize size;
};
#endif
