    MACHINE_X64_TLS_DARWIN,  // descriptor call; address returned in RAX
    MACHINE_A64_TLS_WINDOWS, // def fixed X9; clobber X10; payload = call-target index
    MACHINE_A64_TLS_DARWIN,  // descriptor call; address returned in X0
    // Empty inline assembly with only a memory clobber is a scheduling
    // barrier, but contributes no target bytes.
    MACHINE_X64_COMPILER_BARRIER,
    MACHINE_A64_COMPILER_BARRIER,
    // Predicate-bank forms preserve the legacy replay identities above.
    MACHINE_X64_KMOV_FROM_GENERAL, // def mask, use general; payload = bit width
    MACHINE_X64_KMOV_TO_GENERAL,   // def general, use mask; payload = bit width
