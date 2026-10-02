// Buster-authored fixture for the spirv-vulkan1.2-compute direct backend.
// Set 0, binding 0 is a tightly packed unsigned 32-bit storage buffer.
// index receives GlobalInvocationId.x; the backend guards its buffer length.
// Dispatch only in X, with local size 1, and synchronize externally.
void kernel(unsigned* buffer, unsigned index)
{
    buffer[index] = (buffer[index] * 3u + 17u) ^ 0xa5a5a5a5u;
}
