struct Late;
int late(struct Late* p) { _Static_assert(!__atomic_always_lock_free(8, p), "incomplete here"); return 0; }
struct Late { long x; };
int early(struct Late* p) { _Static_assert(__atomic_always_lock_free(8, p), "complete here"); return 0; }
int function(void (*p)(void))
{
    _Static_assert(__atomic_always_lock_free(4, p) && __atomic_is_lock_free(4, p), "function alignment");
    _Static_assert(!__atomic_always_lock_free(8, p), "past it");
    return 0;
}
