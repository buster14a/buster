// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

const char *concatenated = "ok";
const char *concatenated = "\x100" "\uD800";

const char *grouped = "ok";
const char *grouped = ("\x100");

const char *direct __attribute__((unused)) ("ok");
const char *direct __attribute__((unused)) ("ok", "\x100");

int later = 0;
int later = 1;
