# #3130 core evidence freeze

Frozen at 2026-10-09T13:16:08Z. The adjacent `3130-evidence-core.tar.xz` contains all 106 regular files listed in `MANIFEST-core.json`: the 104 staged evidence files plus the stage’s provisional manifest and checksum sidecar. The two original receipt ZIPs are included exactly once each; the external manifest records every archive member’s uncompressed size and SHA-256.

The V2 receipt passes strict identity, inventory, arm, result, shard, aggregate, phase, and sampler validation. Its report has no direct speed ratio. The separately derived net disposition is NoGo: median analysis wall is +7.861666%, analyzer CPU +5.969640%, and total experimental phase wall +10.409317% for the candidate. This is specific to that V2 measurement.

The historical third-campaign V1 receipt is invalid under its fixed profile, despite successful observed runs. It records 184 selected rows and 137 candidate executions plus 47 aliases; the fixed V1 profile requires 182 rows and 135 executions plus 47 aliases. All observed row results pass, but strict validation suppresses per-TU medians and ratios, and the publisher result is “Not benchmarked.”

Current source checks included in this core: exact canonical commit `42fc9eb51891da028883058d7618a42abe540d9e` / tree `4310f86cf35b41acdca079f52ad8133505fd4800` source-size passes against base `dfedd934e50a5465975c28a2870b0d6382632a60` (production +4,992 bytes; build +0; baseline `66c8a99c005e083853fe0e9f97531bef80280cab`). The analyzer native self-test passes 93 checks, 36 expected rejections, and 0 failures on local commit `e32c9c40cb7e0bf1ce061183724946d1434bd1c9`, whose tree is byte-identical to the canonical tree; it is not represented as a literal run at commit `42fc9eb`.

The canonical-source self-host check and final hosted CI were still pending at freeze time. Only the earlier self-host setup attempt is included; it exited 6 before a compiler/build result. This core therefore does not claim those gates passed. The current-source follow-up will be preserved as a separate small validation appendix, without duplicating either receipt ZIP.

The earlier local Clang exit 135 was caused by a truncated local LLVM shared library; fresh package-matched Clang startup succeeded. Package/binary hashes are recorded, but package and tool binaries are not included. No new physical performance run was started for this recovery.
