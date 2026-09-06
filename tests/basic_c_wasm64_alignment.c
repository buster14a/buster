// Over-alignment belongs to object layout, not the Wasm memory opcode.
typedef signed char WasmAligned_i8 __attribute__((aligned(16)));
WasmAligned_i8 wasm_aligned_i8 = -7;
signed char read_i8(void) { return wasm_aligned_i8; }
void write_i8(signed char value) { wasm_aligned_i8 = value; }

typedef short WasmAligned_i16 __attribute__((aligned(32)));
WasmAligned_i16 wasm_aligned_i16 = -1234;
short read_i16(void) { return wasm_aligned_i16; }
void write_i16(short value) { wasm_aligned_i16 = value; }

typedef unsigned int WasmAligned_i32 __attribute__((aligned(16)));
WasmAligned_i32 wasm_aligned_i32 = 11;
unsigned int read_i32(void) { return wasm_aligned_i32; }
void write_i32(unsigned int value) { wasm_aligned_i32 = value; }

typedef unsigned long WasmAligned_i64 __attribute__((aligned(64)));
WasmAligned_i64 wasm_aligned_i64 = 19;
unsigned long read_i64(void) { return wasm_aligned_i64; }
void write_i64(unsigned long value) { wasm_aligned_i64 = value; }

typedef float WasmAligned_f32 __attribute__((aligned(32)));
WasmAligned_f32 wasm_aligned_f32 = 1.5f;
float read_f32(void) { return wasm_aligned_f32; }
void write_f32(float value) { wasm_aligned_f32 = value; }

typedef double WasmAligned_f64 __attribute__((aligned(64)));
WasmAligned_f64 wasm_aligned_f64 = 2.5;
double read_f64(void) { return wasm_aligned_f64; }
void write_f64(double value) { wasm_aligned_f64 = value; }

typedef void* WasmAligned_pointer __attribute__((aligned(16)));
WasmAligned_pointer wasm_aligned_pointer = 0;
void* read_pointer(void) { return wasm_aligned_pointer; }
void write_pointer(void* value) { wasm_aligned_pointer = value; }
