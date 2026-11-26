// Replacement hints: a corrected identifier and a pointer's member access (#2376).
int counter;
int misspelled(void) { return countr; }
struct Record { int field; };
int pointer_member(struct Record *p) { return p.field; }
