#include "solvan/core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace solvan {
namespace {
using Big = std::array<uint8_t, 40>;
Big number(const std::string &s) {
    Big b{};
    for (char c : s) {
        const char *p = std::strchr(alphabet, c);
        if (!p || !c)
            throw std::invalid_argument(
                "Invalid Base58 prefix: 0, O, I, l and spaces are forbidden");
        unsigned carry = unsigned(p - alphabet);
        for (int i = 39; i >= 0; i--) {
            carry += unsigned(b[i]) * 58;
            b[i] = uint8_t(carry);
            carry >>= 8;
        }
        if (carry)
            throw std::overflow_error("Prefix number overflow");
    }
    return b;
}
} // namespace
std::vector<Interval> prefix_intervals(const std::string &prefix) {
    if (prefix.empty() || prefix.size() > 12)
        throw std::invalid_argument("Prefix length must be 1..12");
    number(prefix);
    size_t zeros = 0;
    while (zeros < prefix.size() && prefix[zeros] == '1')
        zeros++;
    std::vector<Interval> ranges;
    if (zeros == prefix.size()) {
        Interval r{};
        std::fill(r.high + zeros, r.high + 32, 255);
        ranges.push_back(r);
        return ranges;
    }
    const std::string tail = prefix.substr(zeros);
    Big minimum{}, maximum{};
    minimum[8 + zeros] = 1;
    std::fill(maximum.begin() + 8 + zeros, maximum.end(), 255);
    for (size_t digits = tail.size(); digits <= 44 - zeros; digits++) {
        auto low = number(tail + std::string(digits - tail.size(), '1'));
        auto high = number(tail + std::string(digits - tail.size(), 'z'));
        low = std::max(low, minimum);
        high = std::min(high, maximum);
        if (low <= high) {
            Interval r{};
            std::copy_n(low.begin() + 8, 32, r.low);
            std::copy_n(high.begin() + 8, 32, r.high);
            ranges.push_back(r);
        }
    }
    if (ranges.empty())
        throw std::invalid_argument("Prefix has no possible 32-byte public key range");
    return ranges;
}
bool matches(const Prepared &p, const Bytes &pub) {
    for (unsigned j = 0; j < p.range_count; j++) {
        const auto &r = p.ranges[j];
        if (std::memcmp(pub.data(), r.low, 32) >= 0 && std::memcmp(pub.data(), r.high, 32) <= 0)
            return true;
    }
    return false;
}
long double prefix_probability(const std::string &prefix) {
    long double count = 0;
    for (auto &r : prefix_intervals(prefix)) {
        // Subtract as integers before conversion, preserving tiny ranges near 2^256.
        uint8_t diff[32];
        int borrow = 0;
        for (int i = 31; i >= 0; i--) {
            int t = int(r.high[i]) - r.low[i] - borrow;
            borrow = t < 0;
            diff[i] = uint8_t(t);
        }
        long double width = 0;
        for (auto b : diff)
            width = width * 256 + b;
        count += width + 1;
    }
    return std::ldexp(count, -256);
}
void validate(const Config &c) {
    prefix_intervals(c.prefix);
    if (c.threads > 32)
        throw std::invalid_argument("CPU threads must be 0 (auto)..32");
    if (c.batch < 256 || c.batch > 65536 || c.batch % 128)
        throw std::invalid_argument("Batch must be 256..65536, divisible by 128");
    if (c.limit < 1 || c.limit > 10000)
        throw std::invalid_argument("Result limit must be 1..10000");
}
State new_state(const Config &c) {
    validate(c);
    State s;
    s.config = c;
    s.master = random_master();
    return s;
}
} // namespace solvan
