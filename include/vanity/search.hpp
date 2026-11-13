#pragma once
#include "types.hpp"
#include <atomic>
#include <functional>
#include <mutex>
namespace vanity {
struct Hit {
    std::string wif, key, time;
    bool compressed = false;
};
struct Progress {
    uint64_t tested = 0, job_index = 0, offset = 0, jobs = 0, results = 0;
    double seconds = 0, rate = 0, eta = -1;
    std::string pattern, backend, status = "Idle", gpu;
    char filler = 'K';
    unsigned suffix = 0;
    int utilization = -1;
};
struct Checkpoint {
    Config config;
    uint64_t job = 0, offset = 0, tested = 0;
    double seconds = 0;
    std::vector<Hit> hits;
};
void save_state(const std::string &, const Checkpoint &);
Checkpoint load_state(const std::string &);
void export_hits(const std::string &, const std::vector<Hit> &);
struct Device {
    bool available = false;
    std::string name = "No CUDA device (CPU fallback)", error;
};
Device gpu_device();
int gpu_utilization(); // -1 when NVML telemetry is unavailable.
class GpuBatch {
    void *impl_ = nullptr;

  public:
    GpuBatch();
    ~GpuBatch();
    GpuBatch(const GpuBatch &) = delete;
    GpuBatch &operator=(const GpuBatch &) = delete;
    std::vector<uint32_t> run(const Job &, const Key &, uint32_t);
};
class Search {
    mutable std::mutex mutex_;
    Progress progress_;
    Checkpoint state_;
    std::atomic<bool> stop_{false};

  public:
    void stop() {
        stop_ = true;
    }
    Progress progress() const;
    Checkpoint checkpoint() const;
    void run(Checkpoint state, const std::string &state_path, const std::string &output_base);
};
} // namespace vanity
