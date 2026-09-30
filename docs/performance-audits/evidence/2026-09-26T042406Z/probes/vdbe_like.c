/* SQLite sqlite3VdbeExec style: case OP_X: { ... break; } */
#include <stdio.h>
#include <stdlib.h>
typedef struct Op { int opcode; long p1, p2, p3; } Op;
typedef struct Mem { long i; double r; int flags; } Mem;
static long run(Op *aOp, Mem *aMem, int nOp, long depth);
static long run(Op *aOp, Mem *aMem, int nOp, long depth) {
  long rc = 0; int pc = 0; long nVmStep = 0; Mem *pIn1 = 0, *pOut = 0;
  for (pc = 0; pc < nOp; pc++) {
    Op *pOp = &aOp[pc];
    nVmStep++;
    switch (pOp->opcode) {
    case 0: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 0; pOut->flags = 1; break; }
    case 1: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 1) { rc += pIn1->i; } else { rc -= 1; } break; }
    case 2: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 2; break; }
    case 3: { double r = aMem[pOp->p1 & 15].r + 3.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 4: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 4; break; }
    case 5: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 5 + j; break; }
    case 6: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 7: { rc = (rc << 1) ^ 7; if (rc > 1000000) rc %= 1000003; break; }
    case 8: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 8; pOut->flags = 1; break; }
    case 9: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 9) { rc += pIn1->i; } else { rc -= 9; } break; }
    case 10: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 10; break; }
    case 11: { double r = aMem[pOp->p1 & 15].r + 11.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 12: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 12; break; }
    case 13: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 13 + j; break; }
    case 14: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 15: { rc = (rc << 1) ^ 15; if (rc > 1000000) rc %= 1000003; break; }
    case 16: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 16; pOut->flags = 1; break; }
    case 17: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 17) { rc += pIn1->i; } else { rc -= 17; } break; }
    case 18: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 18; break; }
    case 19: { double r = aMem[pOp->p1 & 15].r + 19.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 20: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 20; break; }
    case 21: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 21 + j; break; }
    case 22: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 23: { rc = (rc << 1) ^ 23; if (rc > 1000000) rc %= 1000003; break; }
    case 24: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 24; pOut->flags = 1; break; }
    case 25: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 25) { rc += pIn1->i; } else { rc -= 25; } break; }
    case 26: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 26; break; }
    case 27: { double r = aMem[pOp->p1 & 15].r + 27.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 28: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 28; break; }
    case 29: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 29 + j; break; }
    case 30: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 31: { rc = (rc << 1) ^ 31; if (rc > 1000000) rc %= 1000003; break; }
    case 32: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 32; pOut->flags = 1; break; }
    case 33: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 33) { rc += pIn1->i; } else { rc -= 33; } break; }
    case 34: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 34; break; }
    case 35: { double r = aMem[pOp->p1 & 15].r + 35.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 36: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 36; break; }
    case 37: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 37 + j; break; }
    case 38: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 39: { rc = (rc << 1) ^ 39; if (rc > 1000000) rc %= 1000003; break; }
    case 40: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 40; pOut->flags = 1; break; }
    case 41: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 41) { rc += pIn1->i; } else { rc -= 41; } break; }
    case 42: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 42; break; }
    case 43: { double r = aMem[pOp->p1 & 15].r + 43.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 44: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 44; break; }
    case 45: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 45 + j; break; }
    case 46: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 47: { rc = (rc << 1) ^ 47; if (rc > 1000000) rc %= 1000003; break; }
    case 48: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 48; pOut->flags = 1; break; }
    case 49: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 49) { rc += pIn1->i; } else { rc -= 49; } break; }
    case 50: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 50; break; }
    case 51: { double r = aMem[pOp->p1 & 15].r + 51.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 52: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 52; break; }
    case 53: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 53 + j; break; }
    case 54: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 55: { rc = (rc << 1) ^ 55; if (rc > 1000000) rc %= 1000003; break; }
    case 56: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 56; pOut->flags = 1; break; }
    case 57: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 57) { rc += pIn1->i; } else { rc -= 57; } break; }
    case 58: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 58; break; }
    case 59: { double r = aMem[pOp->p1 & 15].r + 59.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 60: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 60; break; }
    case 61: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 61 + j; break; }
    case 62: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 63: { rc = (rc << 1) ^ 63; if (rc > 1000000) rc %= 1000003; break; }
    case 64: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 64; pOut->flags = 1; break; }
    case 65: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 65) { rc += pIn1->i; } else { rc -= 65; } break; }
    case 66: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 66; break; }
    case 67: { double r = aMem[pOp->p1 & 15].r + 67.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 68: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 68; break; }
    case 69: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 69 + j; break; }
    case 70: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 71: { rc = (rc << 1) ^ 71; if (rc > 1000000) rc %= 1000003; break; }
    case 72: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 72; pOut->flags = 1; break; }
    case 73: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 73) { rc += pIn1->i; } else { rc -= 73; } break; }
    case 74: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 74; break; }
    case 75: { double r = aMem[pOp->p1 & 15].r + 75.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 76: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 76; break; }
    case 77: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 77 + j; break; }
    case 78: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 79: { rc = (rc << 1) ^ 79; if (rc > 1000000) rc %= 1000003; break; }
    case 80: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 80; pOut->flags = 1; break; }
    case 81: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 81) { rc += pIn1->i; } else { rc -= 81; } break; }
    case 82: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 82; break; }
    case 83: { double r = aMem[pOp->p1 & 15].r + 83.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 84: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 84; break; }
    case 85: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 85 + j; break; }
    case 86: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 87: { rc = (rc << 1) ^ 87; if (rc > 1000000) rc %= 1000003; break; }
    case 88: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 88; pOut->flags = 1; break; }
    case 89: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 89) { rc += pIn1->i; } else { rc -= 89; } break; }
    case 90: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 90; break; }
    case 91: { double r = aMem[pOp->p1 & 15].r + 91.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 92: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 92; break; }
    case 93: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 93 + j; break; }
    case 94: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 95: { rc = (rc << 1) ^ 95; if (rc > 1000000) rc %= 1000003; break; }
    case 96: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 96; pOut->flags = 1; break; }
    case 97: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 97) { rc += pIn1->i; } else { rc -= 97; } break; }
    case 98: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 98; break; }
    case 99: { double r = aMem[pOp->p1 & 15].r + 99.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 100: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 100; break; }
    case 101: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 101 + j; break; }
    case 102: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 103: { rc = (rc << 1) ^ 103; if (rc > 1000000) rc %= 1000003; break; }
    case 104: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 104; pOut->flags = 1; break; }
    case 105: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 105) { rc += pIn1->i; } else { rc -= 105; } break; }
    case 106: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 106; break; }
    case 107: { double r = aMem[pOp->p1 & 15].r + 107.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 108: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 108; break; }
    case 109: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 109 + j; break; }
    case 110: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 111: { rc = (rc << 1) ^ 111; if (rc > 1000000) rc %= 1000003; break; }
    case 112: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 112; pOut->flags = 1; break; }
    case 113: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 113) { rc += pIn1->i; } else { rc -= 113; } break; }
    case 114: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 114; break; }
    case 115: { double r = aMem[pOp->p1 & 15].r + 115.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 116: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 116; break; }
    case 117: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 117 + j; break; }
    case 118: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 119: { rc = (rc << 1) ^ 119; if (rc > 1000000) rc %= 1000003; break; }
    case 120: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 120; pOut->flags = 1; break; }
    case 121: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 121) { rc += pIn1->i; } else { rc -= 121; } break; }
    case 122: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 122; break; }
    case 123: { double r = aMem[pOp->p1 & 15].r + 123.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 124: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 124; break; }
    case 125: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 125 + j; break; }
    case 126: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 127: { rc = (rc << 1) ^ 127; if (rc > 1000000) rc %= 1000003; break; }
    case 128: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 128; pOut->flags = 1; break; }
    case 129: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 129) { rc += pIn1->i; } else { rc -= 129; } break; }
    case 130: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 130; break; }
    case 131: { double r = aMem[pOp->p1 & 15].r + 131.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 132: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 132; break; }
    case 133: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 133 + j; break; }
    case 134: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 135: { rc = (rc << 1) ^ 135; if (rc > 1000000) rc %= 1000003; break; }
    case 136: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 136; pOut->flags = 1; break; }
    case 137: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 137) { rc += pIn1->i; } else { rc -= 137; } break; }
    case 138: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 138; break; }
    case 139: { double r = aMem[pOp->p1 & 15].r + 139.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 140: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 140; break; }
    case 141: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 141 + j; break; }
    case 142: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 143: { rc = (rc << 1) ^ 143; if (rc > 1000000) rc %= 1000003; break; }
    case 144: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 144; pOut->flags = 1; break; }
    case 145: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 145) { rc += pIn1->i; } else { rc -= 145; } break; }
    case 146: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 146; break; }
    case 147: { double r = aMem[pOp->p1 & 15].r + 147.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 148: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 148; break; }
    case 149: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 149 + j; break; }
    case 150: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 151: { rc = (rc << 1) ^ 151; if (rc > 1000000) rc %= 1000003; break; }
    case 152: { pIn1 = &aMem[pOp->p1 & 15]; pOut = &aMem[pOp->p2 & 15]; pOut->i = pIn1->i + 152; pOut->flags = 1; break; }
    case 153: { pIn1 = &aMem[pOp->p1 & 15]; if (pIn1->i > 153) { rc += pIn1->i; } else { rc -= 153; } break; }
    case 154: { long a = aMem[pOp->p1 & 15].i, b = aMem[pOp->p2 & 15].i; aMem[pOp->p3 & 15].i = a * b + 154; break; }
    case 155: { double r = aMem[pOp->p1 & 15].r + 155.5; aMem[pOp->p2 & 15].r = r * 0.5; rc += (long)r; break; }
    case 156: { if (depth < pOp->p1) rc += run(aOp + pc + 1, aMem, 1, depth + 1) + 156; break; }
    case 157: { for (int j = 0; j < (int)(pOp->p1 & 3); j++) aMem[j].i += 157 + j; break; }
    case 158: { pOut = &aMem[pOp->p3 & 15]; pOut->i ^= rc + nVmStep; if (pOut->i & 1) rc++; break; }
    case 159: { rc = (rc << 1) ^ 159; if (rc > 1000000) rc %= 1000003; break; }
    default: { return rc; }
    }
  }
  return rc + nVmStep;
}
int main(int argc, char **argv) {
  long depth = argc > 1 ? atol(argv[1]) : 100;
  Op ops[160]; Mem mem[16] = {0};
  for (int i = 0; i < 160; i++) { ops[i].opcode = i; ops[i].p1 = (i % 8 == 4) ? depth : i * 3; ops[i].p2 = i * 5 + 1; ops[i].p3 = i * 7 + 2; }
  for (int i = 0; i < 16; i++) { mem[i].i = i * 11; mem[i].r = i * 0.25; }
  long r = run(ops, mem, 160, 0);
  printf("vdbe depth=%ld result=%ld\n", depth, r);
  return 0;
}
