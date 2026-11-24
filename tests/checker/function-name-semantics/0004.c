const char *before=__PRETTY_FUNCTION__; int f(void){return sizeof __PRETTY_FUNCTION__;} const char *after=__PRETTY_FUNCTION__; _Static_assert(sizeof __func__==1,"file");
