// Reads the probe's headers again, by their guards and through a macro's name, and a header of its own twice.
#include "probe.h"
#define HEADERS_NAME "guarded.h"
#include HEADERS_NAME
#include "once.h"
#undef ROOT_IMAGE_GUARDED_H
#include "repeated.h"
#include "repeated.h"
#include "repeated.h"
int headers_count = REPEATED_COUNT;
