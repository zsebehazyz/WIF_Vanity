#include "vanity/types.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/sha.h>
#endif
namespace vanity {
static std::array<uint8_t, 32> independent_hash(const uint8_t *p, size_t n) {
    std::array<uint8_t, 32> out{};
#ifdef _WIN32
    // Windows CNG, independent of the CPU/CUDA search SHA implementation.
    struct Provider {
        BCRYPT_ALG_HANDLE handle = nullptr;
        Provider() {
            if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
                throw std::runtime_error("Cannot open Windows SHA256 provider");
        }
        ~Provider() {
            BCryptCloseAlgorithmProvider(handle, 0);
        }
    };
    static Provider provider;
    if (BCryptHash(provider.handle, nullptr, 0, const_cast<PUCHAR>(p), ULONG(n), out.data(), 32) <
        0)
        throw std::runtime_error("Windows CNG SHA256 failed");
#else
    if (!SHA256(p, n, out.data()))
        throw std::runtime_error("OpenSSL SHA256 failed");
#endif
    return out;
}
Validation verify_wif(const std::string &s) {
    Validation r;
    auto fail = [&](const char *why) {
        r.error = why;
        return r;
    };
    if (s.size() != 51 && s.size() != 52)
        return fail("WIF must contain 51 or 52 characters");
    // Independent little-endian base conversion, not decode_number().
    std::vector<uint8_t> bytes(1, 0);
    for (char c : s) {
        const char *q = std::strchr(alphabet, c);
        if (!q || !c)
            return fail("Invalid Base58 character");
        unsigned carry = unsigned(q - alphabet);
        for (auto &x : bytes) {
            carry += 58u * x;
            x = uint8_t(carry);
            carry >>= 8;
        }
        while (carry) {
            bytes.push_back(uint8_t(carry));
            carry >>= 8;
        }
    }
    for (char c : s) {
        if (c != '1')
            break;
        bytes.push_back(0);
    }
    std::reverse(bytes.begin(), bytes.end());
    if (bytes.size() != 37 && bytes.size() != 38)
        return fail("Invalid decoded WIF length");
    if (bytes[0] != 0x80)
        return fail("Not a Bitcoin mainnet private key");
    r.compressed = bytes.size() == 38;
    if (r.compressed && bytes[33] != 1)
        return fail("Invalid compression marker");
    std::copy_n(bytes.begin() + 1, 32, r.key.begin());
    // Separate scalar bounds, do not trust valid_key() from the search backend.
    const uint8_t order[32] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                               0xff, 0xff, 0xff, 0xff, 0xfe, 0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48,
                               0xa0, 0x3b, 0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x41};
    if (std::all_of(r.key.begin(), r.key.end(), [](uint8_t x) { return x == 0; }) ||
        !std::lexicographical_compare(r.key.begin(), r.key.end(), order, order + 32))
        return fail("Private key outside secp256k1 range");
    auto h = independent_hash(bytes.data(), bytes.size() - 4);
    h = independent_hash(h.data(), h.size());
    if (!std::equal(h.begin(), h.begin() + 4, bytes.end() - 4))
        return fail("Checksum mismatch");
    r.valid = true;
    return r;
}
} // namespace vanity
