#!/usr/bin/env python3
# Turn phase_census.py shares into ms, ns per preprocessed token, share of the
# CPU gap to TinyCC and share of the peak-RSS gap. Inputs are the measured
# medians and shares recorded in the audit.
def table(name, cpu, tcc_cpu, tokens, peak, tcc_peak, rows, first_rss):
    gap = cpu - tcc_cpu; mgap = peak - tcc_peak
    print(f"## {name}: Buster {cpu:.3f} s CPU vs TinyCC {tcc_cpu:.3f} s; gap {gap*1e3:.0f} ms; "
          f"TinyCC whole compile {tcc_cpu/tokens*1e9:.0f} ns/token; peak {peak:.1f} vs {tcc_peak:.1f} MB, gap {mgap:.1f} MB")
    print(f"{'phase':44s} {'cpu%':>6s} {'ms':>6s} {'ns/tok':>7s} {'x tcc':>6s} {'%gap':>6s} {'MB':>6s} {'%memgap':>7s}")
    for label, share, mb in rows:
        ms = cpu * share / 100 * 1e3
        ns = ms * 1e6 / tokens
        print(f"{label:44s} {share:6.1f} {ms:6.0f} {ns:7.0f} {ns/(tcc_cpu/tokens*1e9):6.2f} {100*ms/1e3/gap:6.1f} {mb:6.1f} {100*mb/mgap:7.1f}")
    print(f"{'resident at first sample (binary, libc)':44s} {'':6s} {'':6s} {'':7s} {'':6s} {'':6s} {first_rss:6.1f}")

sql_g0 = [("prewarm: x86 tables", 2.1, 18.1), ("preprocess", 11.5, 71.1), ("parse", 1.8, 1.1),
    ("sema: lowering constraints", 18.3, 8.8), ("sema: other", 7.7, 16.8), ("lower to IR", 34.5, 98.6),
    ("IR prepare (FAST, verify)", 5.4, 5.0), ("debug info", 0.1, 0.3), ("codegen: predicate placement", 6.1, 2.3),
    ("codegen: instruction selection", 6.8, 2.7), ("codegen: encoding", 1.6, 0.9), ("codegen: other", 1.4, 2.5),
    ("object writing", 0.2, 1.0), ("other (driver, startup, exit)", 2.3, 9.7)]
table("SQLite -c -g0, as shipped", 0.779, 0.165, 684891, 256.2, 18.3, sql_g0, 16.5)
sql_f = [("prewarm: x86 tables", 2.1, 18.1), ("preprocess", 10.5, 68.4), ("parse", 1.8, 1.2),
    ("sema: lowering constraints", 17.2, 8.6), ("sema: other", 7.4, 17.0), ("lower to IR", 32.0, 97.9),
    ("IR prepare (FAST, verify)", 12.5, 6.1), ("debug info", 0.1, 0.4), ("codegen: predicate placement", 5.1, 2.0),
    ("codegen: instruction selection", 5.5, 2.3), ("codegen: encoding", 1.6, 0.7), ("codegen: other", 1.4, 2.7),
    ("object writing", 0.1, 0.7), ("other (driver, startup, exit)", 2.6, 11.9)]
table("SQLite -c -g0, FAST applied", 0.840, 0.165, 684891, 256.2, 18.3, sql_f, 17.2)
sql_g = [("prewarm: x86 tables", 2.1, 18.1), ("preprocess", 10.4, 70.3), ("parse", 1.7, 1.2),
    ("sema: lowering constraints", 16.1, 8.9), ("sema: other", 6.8, 16.8), ("lower to IR", 31.1, 98.7),
    ("IR prepare (FAST, verify)", 4.9, 4.9), ("debug info", 8.6, 21.4), ("codegen: predicate placement", 5.6, 3.3),
    ("codegen: instruction selection", 6.5, 4.0), ("codegen: encoding", 1.5, 1.1), ("codegen: other", 1.5, 3.1),
    ("object writing", 0.3, 2.1), ("other (driver, startup, exit)", 2.9, 15.8)]
table("SQLite -c -g, as shipped", 0.907, 0.166, 684891, 286.1, 19.5, sql_g, 16.1)
lua = [("prewarm: x86 tables", 52.6, 18.5), ("preprocess", 11.1, 0.6), ("parse", 1.5, 0.2),
    ("sema: lowering constraints", 4.0, 0.5), ("sema: other", 6.2, 0.9), ("lower to IR", 9.6, 1.8),
    ("IR prepare (FAST, verify)", 3.1, 0.3), ("debug info", 2.5, 0.3), ("codegen: predicate placement", 1.5, 0.2),
    ("codegen: instruction selection", 2.0, 0.3), ("codegen: encoding", 0.7, 0.1), ("codegen: other", 0.4, 0.1),
    ("object writing", 0.1, 0.0), ("other (driver, startup, exit)", 4.7, 0.4)]
table("Lua 5.4.8, 33 files one process each, -g (memory: per-process peak)", 1.262, 0.293, 559177, 34.8, 7.5, lua, 10.6)
