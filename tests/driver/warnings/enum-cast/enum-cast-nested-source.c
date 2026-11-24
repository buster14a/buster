enum O { OA = sizeof(enum I { IX, IY }), OB, OC = IY };
enum P { PA };
void f(enum P p) { p = OB; p = IX; p = OA; p = IY; p = OC; (void)p; }
enum Q { QA = sizeof(enum R { RX }) + sizeof(enum S { SX }), QB };
void g(enum P p) { p = QB; p = RX; p = SX; p = QA; (void)p; }
