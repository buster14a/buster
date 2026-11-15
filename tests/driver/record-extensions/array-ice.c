int overflow_zero[(2147483647 + 1) + (-2147483647 - 1)];
int literal_cast[(int)0.0];
int short_circuit[0 && (1 / 0)];
int conditional[1 ? 0 : (1 / 0)];
