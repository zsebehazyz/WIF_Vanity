#include "vanity/sha256.hpp"
#include "vanity/types.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace vanity {
std::string hex(const Key &k) {
    std::string s;
    for (auto x : k) {
        s += "0123456789abcdef"[x >> 4];
        s += "0123456789abcdef"[x & 15];
    }
    return s;
}
Key key_from_hex(const std::string &s) {
    if (s.size() != 64)
        throw std::invalid_argument("HEX key must have 64 digits");
    Key k{};
    for (unsigned i = 0; i < 64; i++) {
        char c = s[i];
        int v = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                       : -1;
        if (v < 0)
            throw std::invalid_argument("Invalid HEX");
        k[i / 2] |= uint8_t(v << (i % 2 ? 0 : 4));
    }
    return k;
}
bool valid_key(const Key &k) {
    static const Key order =
        key_from_hex("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
    return std::any_of(k.begin(), k.end(), [](uint8_t b) { return b != 0; }) && k < order;
}
Big decode_number(const std::string &s) {
    Big b{};
    for (char c : s) {
        const char *p = std::strchr(alphabet, c);
        if (!p || !c)
            throw std::invalid_argument("Not a Base58 character");
        unsigned carry = unsigned(p - alphabet);
        for (int i = 39; i >= 0; i--) {
            carry += b[i] * 58u;
            b[i] = uint8_t(carry);
            carry >>= 8;
        }
        if (carry)
            throw std::invalid_argument("Base58 number too large");
    }
    return b;
}
std::string encode_number(const uint8_t *b, size_t n) {
    std::vector<uint8_t> x(b, b + n);
    size_t zeros = 0;
    while (zeros < n && !b[zeros])
        ++zeros;
    std::string s;
    size_t start = zeros;
    while (start < n) {
        unsigned rem = 0;
        for (size_t i = start; i < n; i++) {
            unsigned t = rem * 256 + x[i];
            x[i] = uint8_t(t / 58);
            rem = t % 58;
        }
        s += alphabet[rem];
        while (start < n && !x[start])
            ++start;
    }
    s.append(zeros, '1');
    std::reverse(s.begin(), s.end());
    return s;
}
std::string encode_wif(const Key &k, bool c) {
    if (!valid_key(k))
        throw std::invalid_argument("Private key outside secp256k1 range");
    uint8_t b[38];
    wif_bytes(k.data(), c, b);
    return encode_number(b, c ? 38 : 37);
}
Key offset_key(Key k, uint64_t offset) {
    for (int i = 31; i >= 0 && offset; i--) {
        uint64_t t = k[i] + (offset & 255);
        k[i] = uint8_t(t);
        offset = (offset >> 8) + (t >> 8);
    }
    if (offset)
        throw std::overflow_error("Key overflow");
    return k;
}
bool candidate(const Job &j, const Key &k) {
    if (!valid_key(k))
        return false;
    Big b{};
    unsigned n = j.compressed ? 38 : 37;
    wif_bytes(k.data(), j.compressed, b.data() + 40 - n);
    return b >= j.low && b <= j.high;
}
} // namespace vanity
