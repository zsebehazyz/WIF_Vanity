#include "solvan/core.hpp"
#include <stdexcept>
namespace solvan {
Device gpu_device() {
    return {false, "CPU-only build", "CUDA not compiled"};
}
Gpu::Gpu() {
    throw std::runtime_error("CUDA not compiled");
}
Gpu::~Gpu() = default;
std::vector<uint32_t> Gpu::run(const Prepared &, uint64_t, uint32_t) {
    throw std::runtime_error("CUDA not compiled");
}
std::vector<Bytes> Gpu::derive_for_test(const Prepared &, uint64_t, uint32_t) {
    throw std::runtime_error("CUDA not compiled");
}
} // namespace solvan
