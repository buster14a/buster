// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

const char *concatenated = "ok";
const char *concatenated = "\x100" "\uD800";

const char *grouped = "ok";
const char *grouped = ("\x100");

const char *direct __attribute__((unused)) ("ok");
const char *direct __attribute__((unused)) ("ok", "\x100");

const char *compound = "ok";
const char *compound = (char[]){ "\x100" };

const char *compound_grouped = "ok";
const char *compound_grouped = (const char[]){ ("\x100") };

struct Slots { const char *first; const char *rest[2]; };
const struct Slots *compound_with_sibling = 0;
const struct Slots *compound_with_sibling = &(struct Slots){ .first = "\x100", .rest[1] = "ok" };

int later = 0;
int later = 1;
