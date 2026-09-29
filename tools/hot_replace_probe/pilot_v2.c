#include "pilot_contract.h"

unsigned long long const pilot_abi = PILOT_ABI_STATELESS;

int pilot(int value)
{
    return value + 2;
}
