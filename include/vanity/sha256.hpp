#pragma once
#include <cstdint>
#ifdef __CUDACC__
#define VW_HD __host__ __device__
#else
#define VW_HD
#endif
namespace vanity {
VW_HD inline uint32_t rr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}
// WIF payloads and their digests fit a single SHA256 block.
VW_HD inline void sha_short(const uint8_t *msg, unsigned len, uint8_t *out) {
    const uint32_t k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
                            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
                            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
                            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
                            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
                            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
                            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
                            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
                            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[16] = {};
    for (unsigned i = 0; i < len; i++)
        w[i / 4] |= uint32_t(msg[i]) << (24 - 8 * (i % 4));
    w[len / 4] |= 0x80u << (24 - 8 * (len % 4));
    w[15] = len * 8;
    uint32_t a = 0x6a09e667, b = 0xbb67ae85, c = 0x3c6ef372, d = 0xa54ff53a, e = 0x510e527f,
             f = 0x9b05688c, g = 0x1f83d9ab, h = 0x5be0cd19;
    for (unsigned i = 0; i < 64; i++) {
        if (i >= 16) {
            uint32_t x = w[(i + 1) & 15], y = w[(i + 14) & 15];
            w[i & 15] += (rr(x, 7) ^ rr(x, 18) ^ (x >> 3)) + w[(i + 9) & 15] +
                         (rr(y, 17) ^ rr(y, 19) ^ (y >> 10));
        }
        uint32_t t =
            h + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i & 15];
        h = g;
        g = f;
        f = e;
        e = d + t;
        d = c;
        c = b;
        b = a;
        a = t + (rr(b, 2) ^ rr(b, 13) ^ rr(b, 22)) + ((b & c) ^ (b & d) ^ (c & d));
    }
    uint32_t v[8] = {a + 0x6a09e667, b + 0xbb67ae85, c + 0x3c6ef372, d + 0xa54ff53a,
                     e + 0x510e527f, f + 0x9b05688c, g + 0x1f83d9ab, h + 0x5be0cd19};
    for (unsigned i = 0; i < 32; i++)
        out[i] = uint8_t(v[i / 4] >> (24 - 8 * (i % 4)));
}
VW_HD inline void wif_bytes(const uint8_t *key, bool compressed, uint8_t *out) {
    uint8_t digest[32], second[32];
    out[0] = 0x80;
    for (unsigned i = 0; i < 32; i++)
        out[i + 1] = key[i];
    unsigned n = compressed ? 34 : 33;
    if (compressed)
        out[33] = 1;
    sha_short(out, n, digest);
    sha_short(digest, 32, second);
    for (unsigned i = 0; i < 4; i++)
        out[n + i] = second[i];
}
} // namespace vanity
