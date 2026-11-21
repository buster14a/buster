// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

long g;
int ga[4];
enum E { Z, O };

int narrow(void) { if ((char)(long)&g) return 1; }
int word(void) { if ((int)(long)&g) return 1; }
int array(void) { if ((unsigned short)(long)ga) return 1; }
int real(void) { if ((double)(long)&g) return 1; }
int either(int x) { if ((char)(long)&g || x) return 1; }
int neg(void) { if (!(char)(long)&g) return 1; }
int nested(int x) { if (!((long)(char)(long)&g && x)) return 1; }
int loop(void) { while ((enum E)(long)&g) return 1; }
int unknown(int x) { if ((char)x) return 1; }
int address(void) { if ((long)&g) return 1; }
int boolean(void) { if ((_Bool)&g) return 1; }
int pointer(void) { if ((void*)(long)&g) return 1; }
int wider(void) { if ((__int128)(long)&g) return 1; }
int integer(void) { if ((char)1) return 1; }
int floating(void) { if (!(int)0.5) return 1; }
int left(int x) { if ((char)(1 || x)) return 1; }
int right(int x) { if ((double)(x || 1)) return 1; }
int inverted(int x) { if ((long)!(x && 0)) return 1; }
int twice(int x) { if ((long)(char)(x || 1)) return 1; }
int arm(void) { return (char)(long)&g ? 1 : 1 / 0; }
