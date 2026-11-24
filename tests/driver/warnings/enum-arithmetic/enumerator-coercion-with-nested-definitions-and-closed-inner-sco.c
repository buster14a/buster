enum Outer { OuterFirst = sizeof(enum Inner { InnerOnly }), OuterLast };
enum Peer { PeerFirst = sizeof(enum PeerInner { PeerInnerOnly }), PeerLast };
int f(enum Outer a, enum Peer b) { return (OuterLast == PeerLast) + (a == OuterLast) + (b == PeerLast); }
