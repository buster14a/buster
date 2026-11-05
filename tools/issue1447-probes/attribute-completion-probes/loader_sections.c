int a __attribute__((loader_uninitialized, section(".loader")));
int b __attribute__((loader_uninitialized, section(".data.loader")));
int c __attribute__((loader_uninitialized, section(".bss.loader")));
_Thread_local int d __attribute__((loader_uninitialized, section(".loader_tls")));
