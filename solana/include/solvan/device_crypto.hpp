#pragma once
#include "solvan/device.hpp"
#include <cstddef>
namespace solvan_device {
#include "fe.c"
#include "ge.c"
#include "sha512.c"

__device__ void seed_at(const solvan::Prepared &p, uint64_t index, uint8_t *seed) {
    uint8_t message[23], digest[64];
    const char domain[] = "SolanaVanity-v1";
    for (unsigned i = 0; i < 15; i++)
        message[i] = domain[i];
    for (unsigned i = 0; i < 8; i++)
        message[15 + i] = uint8_t(index >> (8 * i));
    sha512_context ctx{};
    ctx.length = 1024;
    for (unsigned i = 0; i < 8; i++)
        ctx.state[i] = p.inner[i];
    sha512_update(&ctx, message, 23);
    sha512_final(&ctx, digest);
    ctx = {};
    ctx.length = 1024;
    for (unsigned i = 0; i < 8; i++)
        ctx.state[i] = p.outer[i];
    sha512_update(&ctx, digest, 64);
    sha512_final(&ctx, digest);
    for (unsigned i = 0; i < 32; i++)
        seed[i] = digest[i];
}
__device__ void pub_at(const solvan::Prepared &p, uint64_t index, uint8_t *pub) {
    uint8_t seed[32], expanded[64];
    seed_at(p, index, seed);
    sha512(seed, 32, expanded);
    expanded[0] &= 248;
    expanded[31] &= 63;
    expanded[31] |= 64;
    ge_p3 point;
    ge_scalarmult_base(&point, expanded);
    ge_p3_tobytes(pub, &point);
}

} // namespace solvan_device
