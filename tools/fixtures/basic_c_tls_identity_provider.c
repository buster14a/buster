// Provide an exported TLS alias so the object readers and writers have to
// keep TLS identity attached to both a storage symbol and an alias symbol.
__thread int tls_identity_storage = 37;
extern __thread int tls_identity_value __attribute__((alias("tls_identity_storage")));
