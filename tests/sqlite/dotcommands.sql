-- SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-- Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
-- #146: the shell's output modes and dot-commands, compared with the clang-built shell's output.
CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, score REAL, data BLOB, note);
INSERT INTO t VALUES(1,'alpha',1.5,x'00ff','a "quoted", value'),(2,'béta',-0.25,NULL,'multi
line'),(3,'gamma',1e100,x'414243',NULL),(4,'',0,zeroblob(2),12);
CREATE INDEX t_name ON t(name);
CREATE VIEW v AS SELECT name, score FROM t WHERE score > 0;
.headers on
.nullvalue <null>
.mode list
SELECT * FROM t;
.mode csv
SELECT * FROM t;
.mode tabs
SELECT * FROM t;
.mode line
SELECT * FROM t WHERE id < 3;
.mode column
SELECT * FROM t;
.mode box
SELECT * FROM t;
.mode table
SELECT * FROM t;
.mode markdown
SELECT * FROM t;
.mode json
SELECT * FROM t;
.mode html
SELECT * FROM t;
.mode insert t2
SELECT * FROM t;
.mode quote
SELECT * FROM t;
.mode ascii
SELECT * FROM t;
.mode list
.separator " ; " "\r\n"
SELECT * FROM t;
.separator |
.headers off
.print -- schema and listing
.schema
.tables
.indexes
.fullschema --indent
.print -- dump
.dump
.print -- parameters
.parameter init
.parameter set @x 41
.parameter set :y 'text'
SELECT @x + 1, :y;
.parameter list
.parameter clear
.print -- changes, echo and eqp
.changes on
UPDATE t SET score = score * 2 WHERE id < 3;
.changes off
.echo on
SELECT count(*) FROM t;
.echo off
.eqp on
SELECT name FROM t WHERE name > 'b';
.eqp off
.print -- import
CREATE TABLE imp(a, b, c);
.import -csv <<END imp
1,"two, three",4.5
"x""y",,z
END
SELECT a, b, c, typeof(a), typeof(c) FROM imp;
.print -- sha3sum
.sha3sum
.sha3sum --schema
.sha3sum -sha3-224 t
.show
