#include <stdio.h>
#include <stdlib.h>
typedef struct Frame { long *stack; long sp; long acc; } Frame;
static long eval(const unsigned char *code, long *consts, long depth);
static long eval(const unsigned char *code, long *consts, long depth) {
  static void *targets[121] = {&&TARGET_0, &&TARGET_1, &&TARGET_2, &&TARGET_3, &&TARGET_4, &&TARGET_5, &&TARGET_6, &&TARGET_7, &&TARGET_8, &&TARGET_9, &&TARGET_10, &&TARGET_11, &&TARGET_12, &&TARGET_13, &&TARGET_14, &&TARGET_15, &&TARGET_16, &&TARGET_17, &&TARGET_18, &&TARGET_19, &&TARGET_20, &&TARGET_21, &&TARGET_22, &&TARGET_23, &&TARGET_24, &&TARGET_25, &&TARGET_26, &&TARGET_27, &&TARGET_28, &&TARGET_29, &&TARGET_30, &&TARGET_31, &&TARGET_32, &&TARGET_33, &&TARGET_34, &&TARGET_35, &&TARGET_36, &&TARGET_37, &&TARGET_38, &&TARGET_39, &&TARGET_40, &&TARGET_41, &&TARGET_42, &&TARGET_43, &&TARGET_44, &&TARGET_45, &&TARGET_46, &&TARGET_47, &&TARGET_48, &&TARGET_49, &&TARGET_50, &&TARGET_51, &&TARGET_52, &&TARGET_53, &&TARGET_54, &&TARGET_55, &&TARGET_56, &&TARGET_57, &&TARGET_58, &&TARGET_59, &&TARGET_60, &&TARGET_61, &&TARGET_62, &&TARGET_63, &&TARGET_64, &&TARGET_65, &&TARGET_66, &&TARGET_67, &&TARGET_68, &&TARGET_69, &&TARGET_70, &&TARGET_71, &&TARGET_72, &&TARGET_73, &&TARGET_74, &&TARGET_75, &&TARGET_76, &&TARGET_77, &&TARGET_78, &&TARGET_79, &&TARGET_80, &&TARGET_81, &&TARGET_82, &&TARGET_83, &&TARGET_84, &&TARGET_85, &&TARGET_86, &&TARGET_87, &&TARGET_88, &&TARGET_89, &&TARGET_90, &&TARGET_91, &&TARGET_92, &&TARGET_93, &&TARGET_94, &&TARGET_95, &&TARGET_96, &&TARGET_97, &&TARGET_98, &&TARGET_99, &&TARGET_100, &&TARGET_101, &&TARGET_102, &&TARGET_103, &&TARGET_104, &&TARGET_105, &&TARGET_106, &&TARGET_107, &&TARGET_108, &&TARGET_109, &&TARGET_110, &&TARGET_111, &&TARGET_112, &&TARGET_113, &&TARGET_114, &&TARGET_115, &&TARGET_116, &&TARGET_117, &&TARGET_118, &&TARGET_119, &&TARGET_HALT};
  long stack[64]; long sp = 0, acc = depth, tmp = 1; const unsigned char *pc = code;
#define DISPATCH() goto *targets[*pc++]
  DISPATCH();
  TARGET_0: { stack[sp & 63] = consts[0 & 7] + acc; sp++; DISPATCH(); }
  TARGET_1: { if (sp) { sp--; acc += stack[sp & 63] * 2; } DISPATCH(); }
  TARGET_2: { long a = acc ^ 2, b = tmp + 2; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_3: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 3; DISPATCH(); }
  TARGET_4: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 4; DISPATCH(); }
  TARGET_5: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_6: { stack[sp & 63] = consts[6 & 7] + acc; sp++; DISPATCH(); }
  TARGET_7: { if (sp) { sp--; acc += stack[sp & 63] * 8; } DISPATCH(); }
  TARGET_8: { long a = acc ^ 8, b = tmp + 8; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_9: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 9; DISPATCH(); }
  TARGET_10: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 10; DISPATCH(); }
  TARGET_11: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_12: { stack[sp & 63] = consts[12 & 7] + acc; sp++; DISPATCH(); }
  TARGET_13: { if (sp) { sp--; acc += stack[sp & 63] * 14; } DISPATCH(); }
  TARGET_14: { long a = acc ^ 14, b = tmp + 14; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_15: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 15; DISPATCH(); }
  TARGET_16: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 16; DISPATCH(); }
  TARGET_17: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_18: { stack[sp & 63] = consts[18 & 7] + acc; sp++; DISPATCH(); }
  TARGET_19: { if (sp) { sp--; acc += stack[sp & 63] * 20; } DISPATCH(); }
  TARGET_20: { long a = acc ^ 20, b = tmp + 20; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_21: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 21; DISPATCH(); }
  TARGET_22: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 22; DISPATCH(); }
  TARGET_23: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_24: { stack[sp & 63] = consts[24 & 7] + acc; sp++; DISPATCH(); }
  TARGET_25: { if (sp) { sp--; acc += stack[sp & 63] * 26; } DISPATCH(); }
  TARGET_26: { long a = acc ^ 26, b = tmp + 26; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_27: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 27; DISPATCH(); }
  TARGET_28: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 28; DISPATCH(); }
  TARGET_29: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_30: { stack[sp & 63] = consts[30 & 7] + acc; sp++; DISPATCH(); }
  TARGET_31: { if (sp) { sp--; acc += stack[sp & 63] * 32; } DISPATCH(); }
  TARGET_32: { long a = acc ^ 32, b = tmp + 32; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_33: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 33; DISPATCH(); }
  TARGET_34: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 34; DISPATCH(); }
  TARGET_35: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_36: { stack[sp & 63] = consts[36 & 7] + acc; sp++; DISPATCH(); }
  TARGET_37: { if (sp) { sp--; acc += stack[sp & 63] * 38; } DISPATCH(); }
  TARGET_38: { long a = acc ^ 38, b = tmp + 38; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_39: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 39; DISPATCH(); }
  TARGET_40: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 40; DISPATCH(); }
  TARGET_41: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_42: { stack[sp & 63] = consts[42 & 7] + acc; sp++; DISPATCH(); }
  TARGET_43: { if (sp) { sp--; acc += stack[sp & 63] * 44; } DISPATCH(); }
  TARGET_44: { long a = acc ^ 44, b = tmp + 44; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_45: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 45; DISPATCH(); }
  TARGET_46: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 46; DISPATCH(); }
  TARGET_47: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_48: { stack[sp & 63] = consts[48 & 7] + acc; sp++; DISPATCH(); }
  TARGET_49: { if (sp) { sp--; acc += stack[sp & 63] * 50; } DISPATCH(); }
  TARGET_50: { long a = acc ^ 50, b = tmp + 50; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_51: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 51; DISPATCH(); }
  TARGET_52: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 52; DISPATCH(); }
  TARGET_53: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_54: { stack[sp & 63] = consts[54 & 7] + acc; sp++; DISPATCH(); }
  TARGET_55: { if (sp) { sp--; acc += stack[sp & 63] * 56; } DISPATCH(); }
  TARGET_56: { long a = acc ^ 56, b = tmp + 56; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_57: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 57; DISPATCH(); }
  TARGET_58: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 58; DISPATCH(); }
  TARGET_59: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_60: { stack[sp & 63] = consts[60 & 7] + acc; sp++; DISPATCH(); }
  TARGET_61: { if (sp) { sp--; acc += stack[sp & 63] * 62; } DISPATCH(); }
  TARGET_62: { long a = acc ^ 62, b = tmp + 62; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_63: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 63; DISPATCH(); }
  TARGET_64: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 64; DISPATCH(); }
  TARGET_65: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_66: { stack[sp & 63] = consts[66 & 7] + acc; sp++; DISPATCH(); }
  TARGET_67: { if (sp) { sp--; acc += stack[sp & 63] * 68; } DISPATCH(); }
  TARGET_68: { long a = acc ^ 68, b = tmp + 68; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_69: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 69; DISPATCH(); }
  TARGET_70: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 70; DISPATCH(); }
  TARGET_71: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_72: { stack[sp & 63] = consts[72 & 7] + acc; sp++; DISPATCH(); }
  TARGET_73: { if (sp) { sp--; acc += stack[sp & 63] * 74; } DISPATCH(); }
  TARGET_74: { long a = acc ^ 74, b = tmp + 74; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_75: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 75; DISPATCH(); }
  TARGET_76: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 76; DISPATCH(); }
  TARGET_77: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_78: { stack[sp & 63] = consts[78 & 7] + acc; sp++; DISPATCH(); }
  TARGET_79: { if (sp) { sp--; acc += stack[sp & 63] * 80; } DISPATCH(); }
  TARGET_80: { long a = acc ^ 80, b = tmp + 80; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_81: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 81; DISPATCH(); }
  TARGET_82: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 82; DISPATCH(); }
  TARGET_83: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_84: { stack[sp & 63] = consts[84 & 7] + acc; sp++; DISPATCH(); }
  TARGET_85: { if (sp) { sp--; acc += stack[sp & 63] * 86; } DISPATCH(); }
  TARGET_86: { long a = acc ^ 86, b = tmp + 86; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_87: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 87; DISPATCH(); }
  TARGET_88: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 88; DISPATCH(); }
  TARGET_89: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_90: { stack[sp & 63] = consts[90 & 7] + acc; sp++; DISPATCH(); }
  TARGET_91: { if (sp) { sp--; acc += stack[sp & 63] * 92; } DISPATCH(); }
  TARGET_92: { long a = acc ^ 92, b = tmp + 92; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_93: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 93; DISPATCH(); }
  TARGET_94: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 94; DISPATCH(); }
  TARGET_95: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_96: { stack[sp & 63] = consts[96 & 7] + acc; sp++; DISPATCH(); }
  TARGET_97: { if (sp) { sp--; acc += stack[sp & 63] * 98; } DISPATCH(); }
  TARGET_98: { long a = acc ^ 98, b = tmp + 98; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_99: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 99; DISPATCH(); }
  TARGET_100: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 100; DISPATCH(); }
  TARGET_101: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_102: { stack[sp & 63] = consts[102 & 7] + acc; sp++; DISPATCH(); }
  TARGET_103: { if (sp) { sp--; acc += stack[sp & 63] * 104; } DISPATCH(); }
  TARGET_104: { long a = acc ^ 104, b = tmp + 104; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_105: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 105; DISPATCH(); }
  TARGET_106: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 106; DISPATCH(); }
  TARGET_107: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_108: { stack[sp & 63] = consts[108 & 7] + acc; sp++; DISPATCH(); }
  TARGET_109: { if (sp) { sp--; acc += stack[sp & 63] * 110; } DISPATCH(); }
  TARGET_110: { long a = acc ^ 110, b = tmp + 110; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_111: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 111; DISPATCH(); }
  TARGET_112: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 112; DISPATCH(); }
  TARGET_113: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_114: { stack[sp & 63] = consts[114 & 7] + acc; sp++; DISPATCH(); }
  TARGET_115: { if (sp) { sp--; acc += stack[sp & 63] * 116; } DISPATCH(); }
  TARGET_116: { long a = acc ^ 116, b = tmp + 116; tmp = a * b; acc += tmp & 255; DISPATCH(); }
  TARGET_117: { if (depth < consts[0] && pc == code + 1) acc += eval(code, consts, depth + 1) + 117; DISPATCH(); }
  TARGET_118: { for (int j = 0; j < (int)(acc & 3); j++) tmp += j + 118; DISPATCH(); }
  TARGET_119: { acc = (acc << 1) ^ tmp; if (acc > 100000007) acc %= 100000007; DISPATCH(); }
  TARGET_HALT: return acc + tmp + sp;
}
int main(int argc, char **argv) {
  long depth = argc > 1 ? atol(argv[1]) : 100; long consts[8] = {depth, 3, 5, 7, 11, 13, 17, 19};
  unsigned char code[121]; for (int i = 0; i < 120; i++) code[i] = (unsigned char)((i * 7) % 120); code[120] = 120;
  code[0] = 3; code[1] = 9; code[2] = 0; code[3] = 1; code[4] = 2;
  printf("ceval depth=%ld result=%ld\n", depth, eval(code, consts, 0));
  return 0;
}
