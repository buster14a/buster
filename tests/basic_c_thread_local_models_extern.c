// The other half of tests/basic_c_thread_local_models.c: the definitions it
// only declares.  They are here so that file's references are undefined in
// their own translation unit, which is what makes them initial-exec in a
// plain build -- a definition in the same module would be local-exec instead.
__thread int initial_exec_value = 11;
__thread long long initial_exec_wide = 12;

int thread_local_models_add(int left, int right)
{
    return left + right;
}
