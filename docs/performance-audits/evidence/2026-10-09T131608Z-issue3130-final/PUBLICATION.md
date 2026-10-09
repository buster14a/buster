# Evidence publication and reconstruction

The immutable core archive is published as two consecutive byte parts. They reconstruct the exact 14,093,600-byte archive frozen at 2026-10-09T13:16:08Z; no archived member or closed manifest was changed. Its SHA-256 is `4e4059a9f1d92eb5f1d30c42ef56938c70ea0138c916221d72413cdfb4fb884e` and Git blob identity is `f98db8a138007355aa39bf20281a76162a3809ca`.

From this directory:

```sh
sha256sum -c PUBLICATION-SHA256SUMS
cat 3130-evidence-core.tar.xz.part01 3130-evidence-core.tar.xz.part02 > 3130-evidence-core.tar.xz
sha256sum -c SHA256SUMS
xz -t 3130-evidence-core.tar.xz
```

The original core README and SHA256SUMS refer to the reconstructed whole archive and retain their frozen bytes. `MANIFEST-core.json` binds all 106 regular archived files. The third V1 and actual V2 original ZIP each appear exactly once in that reconstructed archive. Canonical self-host validation completed after the core freeze; the separate validation appendix retains the complete command and its failed sparse-fixture setup attempt without duplicating either ZIP. Final hosted CI and protected integration are recorded on PR#3167 after publication.
