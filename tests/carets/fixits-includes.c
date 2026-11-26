// Header replacements cover both delimiters and preserve written macro arguments.
#include <fixits-includes-local.h>
#include " fixits-includes-local.h"
#include "fixits-includes-local.h "
#include "fixits-includes-local.h."
#include "...fixits-includes-local.h..."
#include < tests/carets/fixits-includes-local.h >

#define HEADER_ARGUMENT(name) name
#define HEADER_FORWARD(name) HEADER_ARGUMENT(name)
#define HEADER_ANGLE <fixits-includes-local.h>
#define HEADER_QUOTED " fixits-includes-local.h "
#include HEADER_ARGUMENT(<fixits-includes-local.h>)
#include HEADER_FORWARD("fixits-includes-local.h.")
#include HEADER_ARGUMENT(" fixits-includes-local.h ")
#include HEADER_ANGLE
#include HEADER_QUOTED
#include "fixits-includes-local.h" trailing
#include HEADER_ARGUMENT("fixits-includes-local.h") trailing
#define HEADER_EXTRA "fixits-includes-local.h" trailing
#include HEADER_EXTRA

int include_value = CARET_INCLUDE_VALUE;
#warning after corrected includes
#include HEADER_ARGUMENT("missing-fixits-includes.h")
