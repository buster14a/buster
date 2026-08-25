typedef struct cJSON
{
    int type;
} cJSON;

#define NULL ((void *)0)
#define cJSON_Invalid (0)
#define cJSON_False (1 << 0)
#define cJSON_True (1 << 1)

/* Keep the upstream cJSON_SetBoolValue shape: its true arm assigns a member
   and the whole conditional is immediately compared by the caller. */
#define cJSON_SetBoolValue(object, boolValue) ( \
    (object != NULL && ((object)->type & (cJSON_False | cJSON_True))) ? \
    (object)->type = ((object)->type & (~(cJSON_False | cJSON_True))) | ((boolValue) ? cJSON_True : cJSON_False) : \
    cJSON_Invalid \
)

int main(void)
{
    cJSON *refobj = 0;
    if ((cJSON_SetBoolValue(refobj, 1) == cJSON_Invalid))
    {
    }
    else
    {
        return 1;
    }

    cJSON object = {0};
    object.type = cJSON_False;
    cJSON *bobj = &object;
    if ((cJSON_SetBoolValue(bobj, 1) == cJSON_True))
    {
    }
    else
    {
        return 2;
    }
    return bobj->type == cJSON_True ? 0 : 3;
}
