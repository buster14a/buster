-- SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-- Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
-- #146: on-disk databases, run in an empty directory: the B-tree under many inserts and deletes, journal and WAL
-- modes, VACUUM, ATTACH, backup and restore, compared with the clang-built shell's output.
.open --new main.db
PRAGMA page_size=1024;
PRAGMA journal_mode=DELETE;
CREATE TABLE big(k INTEGER PRIMARY KEY, h INTEGER, v TEXT, b BLOB);
CREATE INDEX big_h ON big(h);
CREATE INDEX big_v ON big(v, h DESC);
WITH RECURSIVE c(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM c WHERE i<20000)
  INSERT INTO big SELECT (i*2654435761) % 1000003, i, printf('%08x-%s', (i*40503) % 65536, substr('abcdefghijklmnopqrstuvwxyz', 1 + i % 26)),
    CASE WHEN i % 97 = 0 THEN zeroblob(3000 + i % 500) ELSE NULL END FROM c;
SELECT count(*), sum(k), sum(h), max(length(v)), sum(length(b)) FROM big;
PRAGMA integrity_check;
DELETE FROM big WHERE h % 3 = 0;
UPDATE big SET v = v || v WHERE h % 5 = 0;
SELECT count(*), sum(k), total(length(v)) FROM big;
SELECT k, h, v FROM big WHERE v LIKE '0000%' ORDER BY v, h LIMIT 5;
SELECT h FROM big WHERE h BETWEEN 1000 AND 1100 ORDER BY h DESC LIMIT 3 OFFSET 2;
PRAGMA integrity_check;
PRAGMA page_count;
VACUUM;
PRAGMA page_count;
PRAGMA freelist_count;
.sha3sum
PRAGMA journal_mode=WAL;
BEGIN;
INSERT INTO big(h, v) SELECT h + 100000, upper(v) FROM big WHERE h % 7 = 1;
COMMIT;
PRAGMA wal_checkpoint(TRUNCATE);
SELECT count(*), max(h) FROM big;
PRAGMA journal_mode=DELETE;
ATTACH 'other.db' AS other;
CREATE TABLE other.copy AS SELECT k, h FROM big WHERE h < 500;
SELECT count(*), sum(h) FROM other.copy;
SELECT count(*) FROM big JOIN other.copy USING(k);
DETACH other;
.backup backup.db
DELETE FROM big;
SELECT count(*) FROM big;
.restore backup.db
SELECT count(*), sum(k) FROM big;
.dbinfo
.selftest --init
.selftest
PRAGMA integrity_check;
