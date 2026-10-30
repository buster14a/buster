#if __has_include("/etc/passwd")
#error External files must be unavailable to the in-memory fuzzer.
#endif
#if __has_embed("/etc/passwd")
#error External resources must be unavailable to the in-memory fuzzer.
#endif
int value;
