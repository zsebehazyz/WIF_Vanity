#include "vanity/search.hpp"
#include <stdexcept>
namespace vanity {
Device gpu_device() {
    return {false, "CPU-only build (CUDA not compiled)", ""};
}
int gpu_utilization() {
    return -1;
}
GpuBatch::GpuBatch() {
    throw std::runtime_error("CUDA not compiled");
}
GpuBatch::~GpuBatch() = default;
std::vector<uint32_t> GpuBatch::run(const Job &, const Key &, uint32_t) {
    throw std::runtime_error("CUDA not compiled");
}
} // namespace vanity
