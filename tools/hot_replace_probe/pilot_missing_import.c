#include "pilot_contract.h"

unsigned long long const pilot_abi = PILOT_ABI_STATELESS;
extern int unavailable_host_function(int value);

int pilot(int value)
{
    return unavailable_host_function(value);
}
