#include "solvan/device.hpp"
#include "solvan/device_crypto.hpp"
namespace {
__device__ bool prefix_match(const solvan::Prepared &p, const uint8_t *pub) {
    for (unsigned r = 0; r < p.range_count; r++) {
        int low = 0, high = 0;
#pragma unroll
        for (unsigned i = 0; i < 32; i++) {
            if (!low && pub[i] != p.ranges[r].low[i])
                low = pub[i] > p.ranges[r].low[i] ? 1 : -1;
            if (!high && pub[i] != p.ranges[r].high[i])
                high = pub[i] > p.ranges[r].high[i] ? 1 : -1;
        }
        if (low >= 0 && high <= 0)
            return true;
    }
    return false;
}
} // namespace
extern "C" __global__ void scan_solana(solvan::KernelParams params, uint32_t *indices,
                                       uint32_t *found) {
    unsigned index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= params.count)
        return;
    uint8_t pub[32];
    solvan_device::pub_at(params.prepared, params.first + index, pub);
    bool hit = prefix_match(params.prepared, pub);
    unsigned active = __activemask(), mask = __ballot_sync(active, hit);
    if (mask) {
        unsigned lane = threadIdx.x & 31, base = 0;
        int leader = __ffs(mask) - 1;
        if (lane == unsigned(leader))
            base = atomicAdd(found, unsigned(__popc(mask)));
        base = __shfl_sync(active, base, leader);
        if (hit)
            indices[base + __popc(mask & ((1u << lane) - 1))] = index;
    }
}
extern "C" __global__ void derive_solana(solvan::KernelParams params, uint8_t *output) {
    unsigned index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < params.count)
        solvan_device::pub_at(params.prepared, params.first + index, output + index * 32);
}
