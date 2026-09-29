#include "pilot_contract.h"

unsigned long long const pilot_abi = PILOT_ABI_STATELESS;
_Thread_local int pilot_tls_value = 1;

int pilot(int value)
{
    return value + pilot_tls_value;
}
