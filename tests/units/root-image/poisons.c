// Poisons a name the probe declares, a predefined macro's and one of its own.
#include "probe.h"
int poisons_before = sizeof(root_image_poisoned);
#pragma GCC poison root_image_poisoned __linux__ poisons_own
int poisons_after;
