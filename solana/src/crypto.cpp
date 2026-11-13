#include "solvan/core.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
extern "C" {
#include "ed25519.h"
#include "sha512.h"
#include "monocypher.h"
}
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif
namespace solvan {
void wipe(void *p, size_t n) {
    crypto_wipe(p, n);
}
std::string hex(const Bytes &b) {
    std::string s;
    for (auto x : b) {
        s += "0123456789abcdef"[x >> 4];
        s += "0123456789abcdef"[x & 15];
    }
    return s;
}
Bytes from_hex(const std::string &s) {
    if (s.size() != 64)
        throw std::invalid_argument("Expected 32 bytes of HEX");
    Bytes b{};
    for (size_t i = 0; i < 64; i++) {
        char c = s[i];
        int v = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                       : -1;
        if (v < 0)
            throw std::invalid_argument("Invalid HEX");
        b[i / 2] |= uint8_t(v << (i % 2 ? 0 : 4));
    }
    return b;
}
Bytes random_master() {
    Bytes b{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, b.data(), ULONG(b.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Windows secure random generator failed");
#else
    size_t done = 0;
    while (done < b.size()) {
        auto n = getrandom(b.data() + done, b.size() - done, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            throw std::runtime_error("OS secure random generator failed");
        done += size_t(n);
    }
#endif
    return b;
}
std::string address(const Bytes &bytes) {
    auto b = bytes;
    size_t zeros = 0;
    while (zeros < 32 && b[zeros] == 0)
        zeros++;
    size_t start = zeros;
    std::string s;
    while (start < 32) {
        unsigned carry = 0;
        for (size_t i = start; i < 32; i++) {
            unsigned v = carry * 256 + b[i];
            b[i] = uint8_t(v / 58);
            carry = v % 58;
        }
        s += alphabet[carry];
        while (start < 32 && b[start] == 0)
            start++;
    }
    s.append(zeros, '1');
    std::reverse(s.begin(), s.end());
    return s;
}
Bytes public_key(const Bytes &seed) {
    Bytes pub{};
    unsigned char expanded[64];
    ed25519_create_keypair(pub.data(), expanded, seed.data());
    wipe(expanded, sizeof(expanded));
    return pub;
}
static void hmac_message(uint64_t index, uint8_t *msg) {
    const char domain[] = "SolanaVanity-v1";
    std::copy_n(reinterpret_cast<const uint8_t *>(domain), 15, msg);
    for (unsigned i = 0; i < 8; i++)
        msg[15 + i] = uint8_t(index >> (8 * i));
}
Prepared prepare(const Bytes &master, const std::string &prefix) {
    Prepared out{};
    auto ranges = prefix_intervals(prefix);
    if (ranges.size() > 4)
        throw std::runtime_error("Too many numeric prefix intervals");
    out.range_count = uint32_t(ranges.size());
    std::copy(ranges.begin(), ranges.end(), out.ranges);
    uint8_t pad[128];
    for (unsigned i = 0; i < 128; i++)
        pad[i] = (i < 32 ? master[i] : 0) ^ 0x36;
    sha512_context ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, pad, 128);
    std::copy_n(ctx.state, 8, out.inner);
    for (unsigned i = 0; i < 128; i++)
        pad[i] ^= 0x36 ^ 0x5c;
    sha512_init(&ctx);
    sha512_update(&ctx, pad, 128);
    std::copy_n(ctx.state, 8, out.outer);
    wipe(pad, sizeof(pad));
    wipe(&ctx, sizeof(ctx));
    return out;
}
Bytes derive_seed(const Prepared &p, uint64_t index) {
    uint8_t msg[23], digest[64];
    hmac_message(index, msg);
    sha512_context ctx{};
    ctx.length = 1024;
    std::copy_n(p.inner, 8, ctx.state);
    sha512_update(&ctx, msg, sizeof(msg));
    sha512_final(&ctx, digest);
    ctx = {};
    ctx.length = 1024;
    std::copy_n(p.outer, 8, ctx.state);
    sha512_update(&ctx, digest, 64);
    sha512_final(&ctx, digest);
    Bytes seed{};
    std::copy_n(digest, 32, seed.begin());
    wipe(digest, sizeof(digest));
    wipe(&ctx, sizeof(ctx));
    return seed;
}
} // namespace solvan
