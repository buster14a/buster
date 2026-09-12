#pragma once

typedef unsigned char FrameByte1 __attribute__((vector_size(1)));
typedef unsigned char FrameByte2 __attribute__((vector_size(2)));
typedef unsigned char FrameByte4 __attribute__((vector_size(4)));
typedef unsigned char FrameByte8 __attribute__((vector_size(8)));
typedef unsigned char FrameByte16 __attribute__((vector_size(16)));
typedef float FrameFloat1 __attribute__((vector_size(4)));
typedef float FrameFloat2 __attribute__((vector_size(8)));
typedef double FrameDouble1 __attribute__((vector_size(8)));

#define FRAME_VECTOR_TYPES(X) \
    X(byte1, FrameByte1) X(byte2, FrameByte2) X(byte4, FrameByte4) \
    X(byte8, FrameByte8) X(byte16, FrameByte16) X(float1, FrameFloat1) \
    X(float2, FrameFloat2) X(double1, FrameDouble1)
#define FRAME_VECTOR_DECLARE(name, type) \
    type frame_vector_##name(type left, type right, int choose); \
    type frame_vector_host_##name(type value); \
    type frame_vector_call_##name(type value);
FRAME_VECTOR_TYPES(FRAME_VECTOR_DECLARE)
#undef FRAME_VECTOR_DECLARE
