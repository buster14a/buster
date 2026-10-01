# Delimiter producer-fact experiment

Predeclared on [#2183](https://github.com/buster14a/buster/issues/2183) before implementation.
Inspection anchor: `c5a073139691e9111986c4fca6f6b03d2a6dfcf1`, tree `68adbe94cf6bd2c00671c4e413552d54791fd1af`.
Codex root is the only writer of `codex/2183-publish-delimiter-pairs`; scouts were read-only.

Baseline fixed-point reproduction runs on GitHub-hosted Ubuntu before production
changes. The proposed slice publishes both directions of each already matched
delimiter pair in the existing immutable token index, replacing three inverse
map reconstructions. Forward non-opener reads must remain UINT32_MAX; backward
reads require start <= opener < closer. Malformed delimiter policy, diagnostics,
arena ownership and canonical IR validation boundaries must remain unchanged.

Falsification: independent pair oracle on every token, scalar/shape parity,
malformed and clipped ranges, dirty arenas, 64/4096 boundaries, affected consumer
regressions, frozen-source object and diagnostic equality, mode and fixed-point
checks. Diagnostic counts must show zero inverse-map allocation/clearing/scans
and one added four-byte publication per matched pair. Reject on correctness,
code quality or resource regression; publish neutral and negative evidence.

No current end-to-end cost fraction or speedup is claimed. The 2x hypothesis is
unproven; this slice is unlikely to reach it. Hosted results are diagnostics.
Qualified-host performance acceptance remains pending; no laptop or benchpress
execution is authorized by this experiment. Buster's first-party license remains
unspecified per the anchored LICENSES/README.md; no external implementation
is imported.
