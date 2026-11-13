#include "solvan/core.hpp"
#include "monocypher-ed25519.h"
#include <algorithm>
namespace solvan {
Bytes independent_public(const Bytes &seed) {
    Bytes pub{}, copy = seed;
    uint8_t secret[64];
    crypto_ed25519_key_pair(secret, pub.data(), copy.data());
    wipe(secret, sizeof(secret));
    wipe(copy.data(), copy.size());
    return pub;
}
Bytes independent_seed(const Bytes &master, uint64_t index) {
    uint8_t message[23] = {};
    const char domain[] = "SolanaVanity-v1";
    std::copy_n(reinterpret_cast<const uint8_t *>(domain), 15, message);
    for (unsigned i = 0; i < 8; i++)
        message[15 + i] = uint8_t(index >> (8 * i));
    uint8_t hmac[64];
    crypto_sha512_hmac(hmac, master.data(), 32, message, sizeof(message));
    Bytes b{};
    std::copy_n(hmac, 32, b.begin());
    wipe(hmac, sizeof(hmac));
    return b;
}
} // namespace solvan
