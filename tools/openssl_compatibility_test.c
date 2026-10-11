// Independent compiler-routing and optional-input controls for #3184.
// Included after the intake helpers by openssl_compatibility.c.
BUSTER_GLOBAL_LOCAL bool openssl_self_test(Arena* arena)
{
    String8 c[] = {S8("-c"), S8("x.c"), S8("-o"), S8("result.S")};
    String8 assembly[] = {S8("-c"), S8("x.S"), S8("-o"), S8("result.c")};
    String8 link[] = {S8("x.o"), S8("libcrypto.a"), S8("-o"), S8("result.c")};
    String8 mixed[] = {S8("x.c"), S8("x.S")};
    String8 probe[] = {S8("--version")};
    bool routes = openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(c)) == OPENSSL_ROUTE_C &&
                  openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(assembly)) == OPENSSL_ROUTE_ASSEMBLY &&
                  openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(link)) == OPENSSL_ROUTE_LINK &&
                  openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(mixed)) == OPENSSL_ROUTE_INVALID &&
                  openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(probe)) == OPENSSL_ROUTE_PROBE;
    make_directory_recursive(arena, S8("build"));
    String8 root = string_format_z(arena, S8("build/openssl-input-controls-{u64}-{u64}"), os_get_current_process_id(), os_now_microseconds());
    bool claimed = os_make_directory_exclusive(root).created;
    bool inputs = false;
    if (claimed)
    {
        root = os_path_absolute(arena, root, true);
        String8 entry = S8("H 160000 7e33b0e7739d633c77b8c478620167f693ed13f4 0\tpyca-cryptography");
        String8 wrong = S8("H 160000 0000000000000000000000000000000000000000 0\tpyca-cryptography");
        String8 unknown = S8("H 160000 7e33b0e7739d633c77b8c478620167f693ed13f4 0\tunknown-input");
        String8 nested = path_join(arena, root, S8("pyca-cryptography"));
        String8 payload = path_join(arena, nested, S8("unexpected.h"));
        bool absent = openssl_gitlink_admitted(arena, root, entry);
        bool wrong_rejected = !openssl_gitlink_admitted(arena, root, wrong);
        bool unknown_rejected = !openssl_gitlink_admitted(arena, root, unknown);
        bool empty = os_make_directory_exclusive(nested).created && openssl_gitlink_admitted(arena, root, entry);
        bool populated_rejected = file_write(payload, BUSTER_SLICE_TO_BYTE_SLICE(S8("unqualified input\n"))) &&
                                  !openssl_gitlink_admitted(arena, root, entry);
        inputs = absent && wrong_rejected && unknown_rejected && empty && populated_rejected;
        string_print(S8("OPENSSL_INPUT_CONTROLS absent={u32} empty={u32} wrong_pin={u32} unknown_path={u32} populated={u32}\n"),
                     (u32)absent, (u32)empty, (u32)wrong_rejected, (u32)unknown_rejected, (u32)populated_rejected);
    }
    bool removed = !claimed || os_directory_delete(root);
    bool pristine = raddebugger_self_test(arena) == PROCESS_RESULT_SUCCESS;
    bool result = routes && inputs && removed && pristine;
    string_print(S8("OPENSSL_SELF_TEST status={S8}\n"), result ? S8("pass") : S8("fail"));
    return result;
}
