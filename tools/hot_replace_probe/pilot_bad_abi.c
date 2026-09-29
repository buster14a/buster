#include "pilot_contract.h"

unsigned long long const pilot_abi = PILOT_ABI_STATELESS + 99;

long long pilot(long long value)
{
    return value + 3;
}
