#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace vanity {
inline constexpr char alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
using Key = std::array<uint8_t, 32>;
using Big = std::array<uint8_t, 40>;
struct Validation {
    bool valid = false, compressed = false;
    Key key{};
    std::string error;
};
std::string hex(const Key &);
Key key_from_hex(const std::string &);
bool valid_key(const Key &);
std::string encode_wif(const Key &, bool);
Validation verify_wif(const std::string &); // Independent decode and SHA implementation.
Big decode_number(const std::string &);
std::string encode_number(const uint8_t *, size_t);
struct Job {
    std::string prefix;
    char filler = 'K';
    unsigned suffix = 4, fill = 0;
    bool compressed = false;
    Big low{}, high{};
    Key first{}, last{};
    uint64_t count = 0;
};
struct Config {
    std::string phrase = "MysteryBansheePromotion",
                fillers = "KLJMHGSA123456789BCDEFNPQRTUVWXYZabcdefghijkmnopqrstuvwxyz",
                before = "1", after = "1";
    unsigned min_fill = 0, max_fill = 40, min_suffix = 4, max_suffix = 6;
    bool compressed = false, all_positions = true, use_gpu = true;
    uint64_t max_results = 100;
};
void validate_config(const Config &);
std::vector<Job> plan(const Config &);
Key offset_key(Key, uint64_t);
bool candidate(const Job &, const Key &);
} // namespace vanity
