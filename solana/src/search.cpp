#include "solvan/core.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#endif
namespace solvan {
namespace {
std::string now() {
    auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream s;
    s << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return s.str();
}
std::vector<uint32_t> cpu_batch(const Prepared &prepared, uint64_t first, uint32_t n,
                                unsigned threads) {
    if (!threads)
        threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    threads = std::min(threads, n);
    std::vector<std::vector<uint32_t>> local(threads);
    std::vector<std::thread> workers;
    std::exception_ptr error;
    std::mutex error_mutex;
    auto task = [&](unsigned t) {
        try {
            for (uint32_t i = t; i < n; i += threads) {
                auto seed = derive_seed(prepared, first + i);
                auto pub = public_key(seed);
                wipe(seed.data(), 32);
                if (matches(prepared, pub))
                    local[t].push_back(i);
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(error_mutex);
            error = std::current_exception();
        }
    };
    for (unsigned t = 1; t < threads; t++)
        workers.emplace_back(task, t);
    task(0);
    for (auto &worker : workers)
        worker.join();
    if (error)
        std::rethrow_exception(error);
    std::vector<uint32_t> result;
    for (auto &v : local)
        result.insert(result.end(), v.begin(), v.end());
    return result;
}
void public_index(const std::filesystem::path &dir, const std::vector<Hit> &hits) {
    auto file = dir / "results.csv", temp = dir / "results.csv.tmp";
    std::ofstream f(temp, std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("Cannot write public result index");
    f << "Address,KeypairFile,TimeUTC\n";
    for (auto &h : hits)
        f << h.address << ",keys/" << h.file << ',' << h.time << '\n';
    f.close();
    if (!f)
        throw std::runtime_error("Cannot flush public result index");
#ifdef _WIN32
    // Keep public CSV replacement platform-correct without including Windows macros in this file.
    std::error_code ec;
    std::filesystem::remove(file, ec);
#endif
    std::filesystem::rename(temp, file);
}
} // namespace
Progress Search::progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}
State Search::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}
void Search::run(State state, const std::string &directory) {
    validate(state.config);
    const auto dir = std::filesystem::u8path(directory), keys = dir / "keys";
    bool newdir = std::filesystem::create_directories(dir),
         newkeys = std::filesystem::create_directories(keys);
#ifndef _WIN32
    if (newdir)
        chmod(dir.c_str(), 0700);
    if (newkeys)
        chmod(keys.c_str(), 0700);
#else
    (void)newdir;
    (void)newkeys;
#endif
    auto checkpoint = (dir / "search.state").u8string();
    auto started = std::chrono::steady_clock::now(), rate_start = started, saved = started;
    double prior = state.seconds, rate = 0;
    uint64_t rate_tested = state.tested;
    auto prepared = prepare(state.master, state.config.prefix);
    struct Wiper {
        Prepared &p;
        ~Wiper() {
            wipe(&p, sizeof(p));
        }
    } wiper{prepared};
    Device device = gpu_device();
    std::unique_ptr<Gpu> gpu;
    if (state.config.gpu && device.available) {
        try {
            gpu = std::make_unique<Gpu>();
        } catch (const std::exception &e) {
            device.name = std::string("CPU fallback: ") + e.what();
        }
    }
    auto probability = prefix_probability(state.config.prefix);
    auto publish = [&](const std::string &status) {
        auto tick = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(tick - rate_start).count();
        state.seconds = prior + std::chrono::duration<double>(tick - started).count();
        if (dt >= 0.25 || (rate == 0 && dt > 0)) {
            rate = (state.tested - rate_tested) / dt;
            rate_start = tick;
            rate_tested = state.tested;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = state;
        progress_.status = status;
        progress_.backend = gpu ? "CUDA" : "CPU";
        progress_.gpu = device.name;
        progress_.tested = state.tested;
        progress_.hits = state.hits.size();
        progress_.seconds = state.seconds;
        progress_.rate = rate;
        progress_.mean_eta = rate > 0 ? double(1 / (probability * rate)) : -1;
    };
    try {
        save_state(checkpoint, state);
        publish("Searching");
        while (!stop_ && state.hits.size() < state.config.limit) {
            uint32_t n = gpu ? state.config.batch : std::min(1024u, state.config.batch);
            if (state.next > UINT64_MAX - n)
                throw std::runtime_error("Search counter exhausted; start a new search");
            auto indices = gpu ? gpu->run(prepared, state.next, n)
                               : cpu_batch(prepared, state.next, n, state.config.threads);
            std::sort(indices.begin(), indices.end());
            if (std::adjacent_find(indices.begin(), indices.end()) != indices.end())
                throw std::runtime_error("Backend returned duplicate indices");
            uint32_t consumed = n;
            for (auto index : indices) {
                if (index >= n)
                    throw std::runtime_error("Backend index outside batch");
                auto seed = derive_seed(prepared, state.next + index),
                     independent = independent_seed(state.master, state.next + index);
                auto pub = public_key(seed);
                auto addr = address(pub);
                if (seed != independent || independent_public(seed) != pub ||
                    !matches(prepared, pub) || addr.rfind(state.config.prefix, 0) != 0) {
                    wipe(seed.data(), 32);
                    wipe(independent.data(), 32);
                    throw std::runtime_error("Independent CPU validator rejected a result");
                }
                std::string file = addr + ".json";
                save_keypair((keys / file).u8string(), seed, pub);
                wipe(seed.data(), 32);
                wipe(independent.data(), 32);
                if (std::none_of(state.hits.begin(), state.hits.end(),
                                 [&](const Hit &h) { return h.address == addr; }))
                    state.hits.push_back({addr, file, now()});
                if (state.hits.size() >= state.config.limit) {
                    consumed = index + 1;
                    break;
                }
            }
            state.next += consumed;
            state.tested += n;
            publish("Searching");
            auto tick = std::chrono::steady_clock::now();
            if (!indices.empty() || tick - saved > std::chrono::seconds(2)) {
                save_state(checkpoint, state);
                public_index(dir, state.hits);
                saved = tick;
            }
        }
        publish(stop_ ? "Stopped" : "Result limit reached");
        save_state(checkpoint, state);
        public_index(dir, state.hits);
    } catch (const std::exception &e) {
        publish(std::string("Error: ") + e.what());
        throw;
    }
    wipe(state.master.data(), 32);
}
} // namespace solvan
