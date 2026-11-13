// Execute the CUDA-adapted arithmetic on the host to test the port without a GPU.
// This does not replace a hardware parity test or validate device execution.
#include "solvan/core.hpp"
#define __device__
#define __constant__
#include "solvan/device_crypto.hpp"
namespace solvan {
Bytes ported_public(const Prepared &p, uint64_t index) {
    Bytes out{};
    solvan_device::pub_at(p, index, out.data());
    return out;
}
Bytes ported_seed(const Prepared &p, uint64_t index) {
    Bytes out{};
    solvan_device::seed_at(p, index, out.data());
    return out;
}
} // namespace solvan
