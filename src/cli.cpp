#include "vanity/search.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace vanity;
static volatile std::sig_atomic_t interrupted = 0;
static void signal_handler(int) {
    interrupted = 1;
}
int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            std::cout << "vanity-cli verify WIF | search PHRASE [--compressed] [--suffix N] "
                         "[--fillers KL...] [--before TEXT] [--after TEXT] [--limit N] [--cpu] "
                         "[--fixed-position] | resume | device | benchmark\n";
            return 0;
        }
        std::string op = argv[1];
        if (op == "device") {
            auto d = gpu_device();
            std::cout << d.name << '\n';
            return 0;
        }
        if (op == "verify") {
            if (argc != 3)
                throw std::invalid_argument("Expected a WIF");
            auto v = verify_wif(argv[2]);
            if (!v.valid) {
                std::cerr << v.error << '\n';
                return 1;
            }
            std::cout << hex(v.key) << ' ' << (v.compressed ? "compressed" : "uncompressed")
                      << '\n';
            return 0;
        }
        if (op == "benchmark") {
            Config c;
            c.phrase = "Mystery";
            c.min_suffix = c.max_suffix = 9;
            c.all_positions = false;
            auto jobs = plan(c);
            if (jobs.empty())
                throw std::runtime_error("Empty benchmark plan");
            auto j = jobs.front();
            uint32_t n = uint32_t(std::min<uint64_t>(65536, j.count));
            auto begin = std::chrono::steady_clock::now();
            uint32_t found = 0;
            for (uint32_t i = 0; i < n; i++)
                found += candidate(j, offset_key(j.first, i));
            double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            std::cout << "CPU interval candidates=" << n << " matches=" << found
                      << " seconds=" << seconds << " candidates/s=" << n / seconds << '\n';
            auto d = gpu_device();
            if (d.available) {
                GpuBatch gpu;
                begin = std::chrono::steady_clock::now();
                auto hits = gpu.run(j, j.first, n);
                seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
                if (hits.size() != found)
                    throw std::runtime_error("GPU/CPU parity failed");
                std::cout << "CUDA candidates=" << n << " matches=" << hits.size()
                          << " seconds=" << seconds << " candidates/s=" << n / seconds << '\n';
            } else
                std::cout << d.name << "; GPU benchmark not run\n";
            return 0;
        }
        Checkpoint cp;
        if (op == "resume")
            cp = load_state("search.state");
        else if (op == "search") {
            if (argc < 3)
                throw std::invalid_argument("Expected phrase");
            cp.config.phrase = argv[2];
            for (int i = 3; i < argc; i++) {
                std::string a = argv[i];
                auto value = [&]() {
                    if (++i >= argc)
                        throw std::invalid_argument("Missing option value");
                    return std::string(argv[i]);
                };
                if (a == "--compressed")
                    cp.config.compressed = true;
                else if (a == "--cpu")
                    cp.config.use_gpu = false;
                else if (a == "--fixed-position")
                    cp.config.all_positions = false;
                else if (a == "--suffix")
                    cp.config.max_suffix = unsigned(std::stoul(value()));
                else if (a == "--limit")
                    cp.config.max_results = std::stoull(value());
                else if (a == "--fillers")
                    cp.config.fillers = value();
                else if (a == "--before")
                    cp.config.before = value();
                else if (a == "--after")
                    cp.config.after = value();
                else
                    throw std::invalid_argument("Unknown option: " + a);
            }
        } else
            throw std::invalid_argument("Unknown command");
        Search search;
        std::atomic<bool> done{false};
        std::signal(SIGINT, signal_handler);
        std::thread watcher([&]() {
            while (!done) {
                if (interrupted)
                    search.stop();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        try {
            search.run(cp, "search.state", "results");
        } catch (...) {
            done = true;
            watcher.join();
            throw;
        }
        done = true;
        watcher.join();
        auto p = search.progress();
        std::cout << p.status << "; tested=" << p.tested << "; hits=" << p.results
                  << "; backend=" << p.backend << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
