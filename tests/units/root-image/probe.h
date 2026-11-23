// What a unit's preprocessor starts with (#3070's root image): every unit here includes this first, then leaves the
// state another way. Compiled as the units of one command, on one lane, each unit but the first starts from the lane's
// image, after units that changed predefined macros, defined their own, poisoned a name, pushed and popped macros,
// read headers once, moved lines and left conditionals and invocations open. What a unit declares here must be what
// it declares alone: anything an earlier unit left changes its object or its diagnostics.
int probe_first_counter = __COUNTER__;
int probe_include_level = __INCLUDE_LEVEL__;
int probe_hosted = __STDC_HOSTED__;
int probe_destructive = __GCC_DESTRUCTIVE_SIZE;
int probe_constructive = __GCC_CONSTRUCTIVE_SIZE;
#ifdef __linux__
int probe_linux = __linux__;
#endif
#ifdef __x86_64__
int probe_x86_64 = __x86_64__;
#endif
#ifdef __STDC_VERSION__
long probe_version = __STDC_VERSION__;
#endif
#ifdef ROOT_IMAGE_LEFT_OBJECT
int probe_left_object = ROOT_IMAGE_LEFT_OBJECT;
#endif
#ifdef ROOT_IMAGE_LEFT_FUNCTION
int probe_left_function = ROOT_IMAGE_LEFT_FUNCTION(1);
#endif
#if defined(ROOT_IMAGE_LEFT_0001) || defined(ROOT_IMAGE_LEFT_0400) || defined(ROOT_IMAGE_LEFT_0799)
int probe_left_many;
#endif
// A name another unit poisons after its own use of it.
int root_image_poisoned;
// Headers read once and by their guards: a unit that kept another's record of either would declare nothing here.
#include "once.h"
#include "guarded.h"
#include "once.h"
#include "guarded.h"
// Pasted and stringized spellings, and a function-like macro's compiled body.
#define PROBE_PASTE(a, b) a##b
#define PROBE_STRING(x) #x
#define PROBE_EXPANDED(x) PROBE_STRING(x)
enum
{
    probe_constant = 9,
};
#define PROBE_SUM(a, b, ...) ((a) + (b) __VA_OPT__(+ PROBE_PASTE(probe_, constant)) __VA_ARGS__)
const char* probe_string = PROBE_EXPANDED(PROBE_PASTE(root_, image) __LINE__ __STDC_HOSTED__);
int PROBE_PASTE(probe_, pasted) = PROBE_SUM(1, 2, );
int probe_sum = PROBE_SUM(__COUNTER__, PROBE_SUM(3, 4), +5);
#if __has_include("once.h") && !__has_include("absent.h")
int probe_has_include;
#endif
// A warning that is on by default, in a header: every unit prints it with its include line, whatever came before.
int probe_shift = 1 << 40;
#undef PROBE_SUM
