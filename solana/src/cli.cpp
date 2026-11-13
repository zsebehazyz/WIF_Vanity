#include "solvan/core.hpp"
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace solvan;
static volatile std::sig_atomic_t interrupted = 0;
static void on_signal(int) {
    interrupted = 1;
}
int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            std::cout << "solana-cli device | verify KEYPAIR.json | search PREFIX [--cpu] "
                         "[--threads N] [--batch N] [--limit N] [--out DIRECTORY] | resume "
                         "DIRECTORY [--limit N] | benchmark\n";
            return 0;
        }
        std::string op = argv[1];
        if (op == "device") {
            std::cout << gpu_device().name << '\n';
            return 0;
        }
        if (op == "encode-public") {
            if (argc != 3)
                throw std::invalid_argument("Expected 32 public bytes in HEX");
            std::cout << address(from_hex(argv[2])) << '\n';
            return 0;
        }
        if (op == "verify") {
            if (argc != 3)
                throw std::invalid_argument("Expected keypair JSON file");
            std::cout << address(read_keypair(argv[2])) << '\n';
            return 0;
        }
        if (op == "benchmark") {
            Bytes master{};
            for (unsigned i = 0; i < 32; i++)
                master[i] = uint8_t(i);
            auto p = prepare(master, "A");
            constexpr unsigned n = 2048;
            // Warm up, then alternate order to avoid favoring the second, warm-cache run.
            unsigned warmup = 0;
            for (unsigned i = 0; i < 256; i++)
                warmup += matches(p, public_key(derive_seed(p, i)));
            constexpr unsigned rounds = 7;
            unsigned hits = 0;
            double fast = 0, baseline = 0;
            auto start = std::chrono::steady_clock::now();
            for (unsigned round = 0; round < rounds; round++) {
                auto measure = [&](bool numeric) {
                    auto began = std::chrono::steady_clock::now();
                    unsigned found = 0;
                    for (unsigned i = 0; i < n; i++) {
                        auto pub = public_key(derive_seed(p, i));
                        found += numeric ? matches(p, pub) : address(pub).rfind("A", 0) == 0;
                    }
                    double elapsed =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - began)
                            .count();
                    if (numeric) {
                        fast += elapsed;
                        hits = found;
                    } else
                        baseline += elapsed;
                    return found;
                };
                unsigned a, b;
                if (round % 2) {
                    a = measure(false);
                    b = measure(true);
                } else {
                    a = measure(true);
                    b = measure(false);
                }
                if (a != b)
                    throw std::runtime_error("Benchmark matcher parity failed");
            }
            std::cout << "CPU single-thread candidates_per_round=" << n << " rounds=" << rounds
                      << " matches_per_round=" << hits << " interval_seconds_total=" << fast
                      << " interval_keys/s=" << (rounds * n) / fast
                      << " base58_seconds_total=" << baseline
                      << " base58_keys/s=" << (rounds * n) / baseline
                      << " warmup_matches=" << warmup << '\n';
            auto device = gpu_device();
            if (device.available) {
                Gpu gpu;
                gpu.run(p, 0, n); // Warm up module and kernel.
                start = std::chrono::steady_clock::now();
                auto found = gpu.run(p, 0, n);
                double seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                if (found.size() != hits)
                    throw std::runtime_error("GPU benchmark parity failed");
                std::cout << "CUDA keys/s=" << n / seconds << " matches=" << found.size() << '\n';
            } else
                std::cout << "GPU benchmark SKIPPED: " << device.name << '\n';
            wipe(&p, sizeof(p));
            return 0;
        }
        State state;
        std::string out = "solana-output";
        int first = 3;
        if (op == "resume") {
            if (argc < 3)
                throw std::invalid_argument("Expected output directory");
            out = argv[2];
            state = load_state((std::filesystem::u8path(out) / "search.state").u8string());
        } else if (op == "search") {
            if (argc < 3)
                throw std::invalid_argument("Expected Base58 prefix");
            state.config.prefix = argv[2];
        } else
            throw std::invalid_argument("Unknown command");
        for (int i = first; i < argc; i++) {
            std::string a = argv[i];
            auto value = [&]() {
                if (++i >= argc)
                    throw std::invalid_argument("Missing option value");
                return std::string(argv[i]);
            };
            if (a == "--cpu")
                state.config.gpu = false;
            else if (a == "--limit")
                state.config.limit = std::stoull(value());
            else if (a == "--threads")
                state.config.threads = unsigned(std::stoul(value()));
            else if (a == "--batch")
                state.config.batch = unsigned(std::stoul(value()));
            else if (a == "--out" && op == "search")
                out = value();
            else
                throw std::invalid_argument("Unknown option");
        }
        validate(state.config);
        if (op == "search") {
            if (std::filesystem::exists(std::filesystem::u8path(out) / "search.state"))
                throw std::runtime_error(
                    "Directory has a checkpoint; use resume or another --out directory");
            state = new_state(state.config);
        }
        Search search;
        std::atomic<bool> done{false};
        std::signal(SIGINT, on_signal);
        std::thread watcher([&] {
            while (!done) {
                if (interrupted)
                    search.stop();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        try {
            search.run(state, out);
        } catch (...) {
            done = true;
            watcher.join();
            throw;
        }
        done = true;
        watcher.join();
        wipe(state.master.data(), 32);
        auto progress = search.progress();
        std::cout << progress.status << " backend=" << progress.backend
                  << " tested=" << progress.tested << " hits=" << progress.hits << '\n';
        for (auto &hit : search.snapshot().hits)
            std::cout << hit.address << "  "
                      << (std::filesystem::u8path(out) / "keys" / hit.file).u8string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
