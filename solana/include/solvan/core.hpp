#pragma once
#include "device.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
namespace solvan {
inline constexpr char alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
using Bytes = std::array<uint8_t, 32>;
std::string hex(const Bytes &);
Bytes from_hex(const std::string &);
Bytes random_master();
std::string address(const Bytes &);
Bytes public_key(const Bytes &seed);
Bytes independent_public(const Bytes &seed);
Prepared prepare(const Bytes &master, const std::string &prefix);
Bytes derive_seed(const Prepared &, uint64_t index);
Bytes independent_seed(const Bytes &master, uint64_t index);
bool matches(const Prepared &, const Bytes &);
std::vector<Interval> prefix_intervals(const std::string &);
long double prefix_probability(const std::string &);
void wipe(void *, size_t);
struct Device {
    bool available = false;
    std::string name, error;
};
Device gpu_device();
class Gpu {
    void *impl_ = nullptr;

  public:
    Gpu();
    ~Gpu();
    Gpu(const Gpu &) = delete;
    Gpu &operator=(const Gpu &) = delete;
    std::vector<uint32_t> run(const Prepared &, uint64_t first, uint32_t count);
    std::vector<Bytes> derive_for_test(const Prepared &, uint64_t first, uint32_t count);
};
struct Config {
    std::string prefix = "Ab";
    bool gpu = true;
    unsigned threads = 0, batch = 4096;
    uint64_t limit = 1;
};
void validate(const Config &);
struct Hit {
    std::string address, file, time;
};
struct State {
    Config config;
    Bytes master{};
    uint64_t next = 0, tested = 0;
    double seconds = 0;
    std::vector<Hit> hits;
};
State new_state(const Config &);
void save_state(const std::string &file, const State &);
State load_state(const std::string &file);
Bytes read_keypair(const std::string &file); // Validates 64-byte seed||public JSON.
void save_keypair(const std::string &file, const Bytes &seed, const Bytes &pub);
struct Progress {
    std::string status = "Idle", backend, gpu;
    uint64_t tested = 0, hits = 0;
    double seconds = 0, rate = 0, mean_eta = -1;
};
class Search {
    mutable std::mutex mutex_;
    State state_;
    Progress progress_;
    std::atomic<bool> stop_{false};

  public:
    ~Search() {
        wipe(state_.master.data(), state_.master.size());
    }
    void stop() {
        stop_ = true;
    }
    Progress progress() const;
    State snapshot() const;
    void run(State state, const std::string &directory);
};
} // namespace solvan
