-- SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-- Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
-- #146: the SQL engine's main features, run by each sqlite3 shell test-sqlite builds; every build's output must be
-- the clang-built shell's, byte for byte. Deterministic: no random(), no 'now', no paths.
.headers on
.mode list
.nullvalue NULL
CREATE TABLE dept(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, budget REAL);
CREATE TABLE emp(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT COLLATE NOCASE, dept INTEGER REFERENCES dept(id), salary INTEGER, hired TEXT, boss INTEGER);
INSERT INTO dept VALUES(1,'eng',1.5e6),(2,'ops',250000.25),(3,'sales',NULL),(4,'empty',0);
INSERT INTO emp(name,dept,salary,hired,boss) VALUES('Ada',1,180000,'2019-03-04',NULL),('bob',1,120000,'2020-11-30',1),
  ('Cy',2,95000,'2018-01-15',1),('dee',2,97000,'2021-07-01',3),('Eve',3,70000,'2022-02-28',1),('fay',3,NULL,'2023-12-31',5),
  ('Gus',1,150000,'2017-05-05',1),('hal',NULL,40000,'2024-02-29',NULL);
SELECT d.name, count(e.id) AS n, sum(e.salary), avg(e.salary), min(e.salary), max(e.salary), total(e.salary)
  FROM dept d LEFT JOIN emp e ON e.dept=d.id GROUP BY d.id ORDER BY d.id;
SELECT e.name, d.name FROM emp e RIGHT JOIN dept d ON e.dept=d.id ORDER BY d.id, e.id;
SELECT e.name, d.name FROM emp e FULL JOIN dept d ON e.dept=d.id ORDER BY coalesce(e.id,99), d.id;
SELECT name FROM emp WHERE name='ADA' OR name LIKE 'B%' OR name GLOB 'C*' ORDER BY name;
SELECT dept, group_concat(name, '|' ORDER BY name DESC) FROM emp GROUP BY dept HAVING count(*)>1 ORDER BY dept;
SELECT name, salary, rank() OVER w, dense_rank() OVER w, row_number() OVER w, percent_rank() OVER w, cume_dist() OVER w,
  ntile(3) OVER w, lag(name) OVER w, lead(salary,1,-1) OVER w, first_value(name) OVER w, last_value(name) OVER w,
  sum(salary) OVER (ORDER BY id ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING),
  avg(salary) OVER (PARTITION BY dept ORDER BY hired RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
  FROM emp WINDOW w AS (ORDER BY salary DESC NULLS LAST) ORDER BY id;
WITH RECURSIVE chain(id, name, depth, path) AS (
  SELECT id, name, 0, name FROM emp WHERE boss IS NULL
  UNION ALL SELECT e.id, e.name, c.depth+1, c.path || '/' || e.name FROM emp e JOIN chain c ON e.boss=c.id)
  SELECT * FROM chain ORDER BY path;
WITH RECURSIVE fib(n, a, b) AS (VALUES(1, 0, 1) UNION ALL SELECT n+1, b, a+b FROM fib WHERE n<92)
  SELECT n, a FROM fib WHERE n % 13 = 0 OR n > 89;
WITH RECURSIVE
  xaxis(x) AS (VALUES(-2.0) UNION ALL SELECT x+0.05 FROM xaxis WHERE x<1.2),
  yaxis(y) AS (VALUES(-1.0) UNION ALL SELECT y+0.1 FROM yaxis WHERE y<1.0),
  m(iter, cx, cy, x, y) AS (
    SELECT 0, x, y, 0.0, 0.0 FROM xaxis, yaxis
    UNION ALL
    SELECT iter+1, cx, cy, x*x-y*y + cx, 2.0*x*y + cy FROM m WHERE (x*x + y*y) < 4.0 AND iter<28),
  m2(iter, cx, cy) AS (SELECT max(iter), cx, cy FROM m GROUP BY cx, cy),
  a(t) AS (SELECT group_concat( substr(' .+*#', 1+min(iter/7,4), 1), '') FROM m2 GROUP BY cy)
SELECT group_concat(rtrim(t),x'0a') FROM a;
-- Upserts, RETURNING, triggers, views and constraints.
CREATE TABLE audit(t TEXT);
CREATE TRIGGER emp_raise AFTER UPDATE OF salary ON emp WHEN new.salary > old.salary
  BEGIN INSERT INTO audit VALUES(printf('%s: %d -> %d', new.name, old.salary, new.salary)); END;
CREATE VIEW payroll AS SELECT d.name AS dept, sum(e.salary) AS cost FROM emp e JOIN dept d ON d.id=e.dept GROUP BY d.name;
UPDATE emp SET salary = salary * 11 / 10 WHERE dept=2 RETURNING name, salary;
INSERT INTO dept VALUES(2,'ops',1.0) ON CONFLICT(id) DO UPDATE SET budget = budget + excluded.budget RETURNING *;
INSERT OR IGNORE INTO dept VALUES(5,'eng',1);
INSERT OR REPLACE INTO dept VALUES(6,'legal',12.5);
SELECT * FROM audit ORDER BY t;
SELECT * FROM payroll ORDER BY cost DESC;
CREATE TABLE s(a INTEGER NOT NULL, b TEXT CHECK (length(b) < 5), c ANY, PRIMARY KEY(a)) STRICT;
INSERT INTO s VALUES(1,'ab',1.5),(2,'cd',x'00ff'),(3,'ef','text');
INSERT INTO s VALUES('x','gh',0);
INSERT INTO s VALUES(4,'toolong',0);
SELECT a, b, typeof(c), quote(c) FROM s;
CREATE TABLE g(a INTEGER, b INTEGER, sumab INTEGER GENERATED ALWAYS AS (a+b) STORED, prod GENERATED ALWAYS AS (a*b) VIRTUAL);
INSERT INTO g(a,b) VALUES(3,4),(-7,6),(1<<40,1<<20);
SELECT * FROM g;
CREATE TABLE kv(k TEXT PRIMARY KEY, v) WITHOUT ROWID;
INSERT INTO kv VALUES('b',2),('a',1),('c',3),('A',0);
SELECT * FROM kv ORDER BY k COLLATE NOCASE, k;
CREATE INDEX emp_hi ON emp(salary) WHERE salary > 100000;
CREATE INDEX emp_lower ON emp(lower(name), dept DESC);
EXPLAIN QUERY PLAN SELECT name FROM emp WHERE salary > 150000;
SELECT name FROM emp WHERE lower(name) BETWEEN 'b' AND 'e' ORDER BY lower(name);
-- Transactions and savepoints.
BEGIN;
SAVEPOINT one;
DELETE FROM emp WHERE dept=1;
SAVEPOINT two;
DELETE FROM emp;
ROLLBACK TO two;
SELECT count(*) FROM emp;
ROLLBACK TO one;
RELEASE one;
COMMIT;
SELECT count(*) FROM emp;
PRAGMA foreign_keys=ON;
DELETE FROM dept WHERE id=1;
PRAGMA foreign_key_check;
-- Scalar functions on every type.
SELECT typeof(1), typeof(1.0), typeof('1'), typeof(x'01'), typeof(NULL), 9223372036854775807 + 1, -9223372036854775808 - 1,
  9223372036854775807 * 2, 7 / 2, 7 / 2.0, 7 % -3, -7 % 3, 1 << 63, (1 << 63) >> 63, ~5, 5 & 3 | 8, abs(-9223372036854775807);
SELECT CAST('12abc' AS INTEGER), CAST('1e3' AS REAL), CAST(12.9 AS INTEGER), CAST(-12.9 AS INTEGER), CAST(1e30 AS INTEGER),
  CAST(x'414243' AS TEXT), CAST('3.0' AS NUMERIC), CAST('0x1F' AS INTEGER), 0x7fffffffffffffff, 1e308*10, -1e308*10;
SELECT 0.1 + 0.2, 1.0/3, 2.0/3, 100.0, 1e15, 1e16, 1.5e-7, 123456789012345678.0, 4.9e-324, 1.7976931348623157e308,
  round(2.5), round(-2.5), round(1.23456789, 4), round(1e20, 2), 3.14159265358979323846;
SELECT printf('%d|%5d|%-5d|%05d|%x|%X|%o|%c|%e|%.3f|%g|%10.4g|%s|%.2s|%q|%Q|%w|%%|%,d|%z', 42, 42, 42, 42, 255, 255, 8, 65,
  12345.678, 3.14159, 0.0001234, 123456789.0, 'str', 'str', 'it''s', NULL, 'a"b', 1234567, 'zz');
SELECT format('%!.20g %#x %+d % d %lld', 0.1, 255, 5, 5, 9223372036854775807);
SELECT length('héllo'), length(x'00ff00'), octet_length('héllo'), upper('héllo'), lower('ÀBC'), substr('héllo', 2, 3),
  substr('abcdef', -3), substring('abcdef', 2), instr('banana', 'nan'), replace('banana', 'an', 'AN'), trim('  x  '),
  ltrim('xxyxz', 'x'), rtrim('abccc', 'c'), hex('é'), unhex('C3A9'), unicode('€'), char(72, 105, 8364), quote(x'abcd'),
  zeroblob(3), length(zeroblob(1000)), concat('a', NULL, 2), concat_ws('-', 'a', NULL, 'b');
SELECT coalesce(NULL, NULL, 3), ifnull(NULL, 'd'), nullif(1, 1), nullif(1, 2), iif(1 > 2, 'y', 'n'), min(3, 1, 2),
  max('a', 'b'), likelihood(1, 0.5), unlikely(1), sign(-3.5), 'abc' < 'abd', 'a' = 'A' COLLATE NOCASE,
  'x  ' = 'x' COLLATE RTRIM, CASE 3 WHEN 1 THEN 'one' WHEN 3 THEN 'three' ELSE 'many' END;
SELECT sqrt(2), pow(2, 0.5), exp(1), ln(10), log10(1000), log2(1024), log(2, 8), sin(1), cos(1), tan(1), atan2(1, 2),
  asin(0.5), acosh(2), floor(-2.5), ceil(-2.5), trunc(-2.5), mod(7.5, 2), pi(), degrees(pi()), radians(180);
SELECT date('2024-02-29', '+1 year'), datetime('2000-01-01 12:00:00', '-1 day', 'start of month', '+90 minutes'),
  julianday('2024-01-01'), strftime('%Y-%m-%d %H:%M:%S %j %w %W %s', '2021-12-31 23:59:59.999'), unixepoch('1970-01-02'),
  time('12:34:56.789', '+1 second'), date(2460000.5), datetime(1700000000, 'unixepoch'), timediff('2025-03-01', '2024-01-31'),
  strftime('%f', '2001-02-03 04:05:06.789'), date('2023-01-31', '+1 month'), date('2023-01-31', '+1 month', 'floor');
-- JSON.
SELECT json('{"a":[1,2.5,"x",null,true]}'), json_array(1, 'a', NULL, 2.0), json_object('k', 1, 'l', json_array()),
  json_extract('{"a":{"b":[10,20]}}', '$.a.b[1]'), '{"a":{"b":[10,20]}}' -> '$.a', '{"a":{"b":[10,20]}}' ->> '$.a.b[#-1]',
  json_insert('[1,2]', '$[#]', 3), json_replace('{"a":1}', '$.a', 'x'), json_set('{}', '$.p.q', 1), json_remove('[0,1,2]', '$[1]'),
  json_patch('{"a":1,"b":2}', '{"b":null,"c":3}'), json_type('[1]', '$[0]'), json_valid('{x}'), json_quote('a"b'),
  json_array_length('[1,[2,3]]'), json_pretty('{"a":[1,{"b":2}]}'), json(jsonb('{"z":1e400,"y":-0.0}'));
SELECT key, value, type, atom, fullkey, path FROM json_tree('{"a":[1,{"b":null}],"c":"d"}');
SELECT json_group_array(name ORDER BY id), json_group_object(name, salary) FROM emp WHERE dept=2;
-- Aggregates from the bundled extensions and the core.
SELECT median(salary), percentile(salary, 25), percentile_cont(salary, 0.9), percentile_disc(salary, 0.9),
  string_agg(name, ',' ORDER BY salary), count(DISTINCT dept) FROM emp;
SELECT value FROM generate_series(1, 30, 7);
SELECT decimal_add('0.1', '0.2'), decimal_mul('12345678901234567890', '98765432109876543210'), decimal('1e-20'),
  decimal_sum(value) FROM generate_series(1, 100);
SELECT ieee754(2.5), ieee754(5, -1), ieee754_mantissa(0.1), ieee754_exponent(0.1), hex(ieee754_to_blob(1.0));
SELECT hex(sha3('abc')), hex(sha3('abc', 384)), hex(sha3_query('SELECT 1, ''a'', x''00'''));
SELECT base64(x'000102fffe'), hex(base64('AAEC//4=')), base85(x'0001020304'), 'x' REGEXP '^[a-z]$', 'caaab' REGEXP 'a+b$';
SELECT 'b' < 'B10' COLLATE uint, 'a2' < 'a10' COLLATE uint;
