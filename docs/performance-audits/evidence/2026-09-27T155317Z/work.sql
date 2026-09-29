PRAGMA journal_mode=OFF;
PRAGMA synchronous=OFF;
CREATE TABLE t(a INTEGER PRIMARY KEY, b INTEGER, c TEXT);
WITH RECURSIVE s(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM s WHERE x<400000)
INSERT INTO t SELECT x, (x*7919)%100003, printf('row-%08d-%s', x, hex(x*x)) FROM s;
CREATE INDEX tb ON t(b);
SELECT count(*), sum(b), max(length(c)) FROM t;
SELECT b % 97 AS k, count(*), avg(a) FROM t GROUP BY k ORDER BY k LIMIT 5;
SELECT count(*) FROM t AS x JOIN t AS y ON x.b = y.a WHERE y.b < 5000;
SELECT substr(group_concat(c, ','), 1, 40) FROM (SELECT c FROM t WHERE b BETWEEN 100 AND 400 ORDER BY c);
UPDATE t SET c = upper(c) WHERE a % 3 = 0;
SELECT count(*) FROM t WHERE c LIKE 'ROW-%';
