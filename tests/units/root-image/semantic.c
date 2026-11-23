// An error the checker gives: the unit is read again from its start where it skipped an earlier unit's declarations
// (#2531), so its preprocessor starts twice.
#include "probe.h"
int semantic_value = semantic_undeclared + __COUNTER__;
