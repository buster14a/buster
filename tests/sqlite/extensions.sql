-- SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-- Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
-- #146: the virtual tables the shell's build enables (FTS4, R-Tree, dbstat, sqlite_dbpage, bytecode and
-- sqlite_stmt), compared with the clang-built shell's output.
.headers on
.mode list
CREATE VIRTUAL TABLE docs USING fts4(title, body, tokenize=porter);
INSERT INTO docs VALUES('SQLite','SQLite is a small, fast, self-contained, high-reliability SQL database engine.'),
  ('Compilers','A compiler translates source code into machine code; compilers check types and optimize.'),
  ('Databases','Database engines store, index and query data; an index speeds up queries.'),
  ('Testing','Testing finds bugs: databases and compilers both need running tests.');
SELECT title FROM docs WHERE docs MATCH 'database' ORDER BY title;
SELECT title, snippet(docs, '[', ']', '...', -1, 4) FROM docs WHERE docs MATCH 'compil*' ORDER BY title;
SELECT title, offsets(docs) FROM docs WHERE docs MATCH 'index OR test' ORDER BY title;
SELECT title, hex(matchinfo(docs)) FROM docs WHERE docs MATCH 'engine NEAR/3 database' ORDER BY title;
SELECT title FROM docs WHERE docs MATCH 'title:testing' UNION ALL SELECT title FROM docs WHERE docs MATCH '"machine code"';
INSERT INTO docs(docs) VALUES('optimize');
SELECT count(*) FROM docs WHERE docs MATCH 'data*';
CREATE VIRTUAL TABLE box USING rtree(id, x0, x1, y0, y1);
WITH RECURSIVE c(i) AS (VALUES(0) UNION ALL SELECT i+1 FROM c WHERE i<399)
  INSERT INTO box SELECT i, (i*37)%100, (i*37)%100 + i%7 + 0.5, (i*61)%100, (i*61)%100 + i%5 + 0.25 FROM c;
SELECT count(*), sum(id) FROM box WHERE x0 >= 10 AND x1 <= 40 AND y0 >= 20 AND y1 <= 70;
SELECT id FROM box WHERE x1 >= 50 AND x0 <= 50 AND y1 >= 50 AND y0 <= 50 ORDER BY id;
SELECT rtreecheck('box');
DELETE FROM box WHERE id % 3 = 0;
SELECT count(*), rtreecheck('box') FROM box;
SELECT name, path, pageno, pagetype, ncell, payload, unused, mx_payload FROM dbstat WHERE name LIKE 'docs%' ORDER BY name, path;
SELECT pgno, length(data) FROM sqlite_dbpage WHERE pgno <= 3;
SELECT count(*) > 10 FROM bytecode('SELECT title FROM docs WHERE rowid > 1');
SELECT opcode, p1, p2, p3, p5 FROM bytecode('SELECT 1 + 2') ORDER BY addr;
SELECT sql FROM sqlite_stmt WHERE sql LIKE 'SELECT sql FROM sqlite_stmt%';
PRAGMA integrity_check;
