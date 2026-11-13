#include "vanity/search.hpp"
#include "vanity/sha256.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <chrono>
using namespace vanity;
static unsigned checks = 0;
static void require(bool ok, const char *why) {
    checks++;
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> static void throws(F f, const char *why) {
    bool yes = false;
    try {
        f();
    } catch (const std::exception &) {
        yes = true;
    }
    require(yes, why);
}
static std::string raw_wif(Key key, bool compressed, uint8_t version = 0x80, uint8_t marker = 1) {
    uint8_t b[38] = {};
    b[0] = version;
    std::copy(key.begin(), key.end(), b + 1);
    unsigned n = compressed ? 34 : 33;
    if (compressed)
        b[33] = marker;
    uint8_t h[32], d[32];
    sha_short(b, n, h);
    sha_short(h, 32, d);
    std::copy_n(d, 4, b + n);
    return encode_number(b, n + 4);
}
int main(int argc, char **argv) {
    try {
        const std::string sample = "5K1MysteryBansheePromotion1KKKKKKKKKKKKKKKKKKKuLH7t";
        const std::string expected =
            "9cd85e9576499f4cbd0e5012f3ce964dddc039e8a2b2c9c184119d83ff630e60";
        auto v = verify_wif(sample);
        require(v.valid && !v.compressed && hex(v.key) == expected, "User WIF vector");
        require(encode_wif(v.key, false) == sample, "Reencode user vector");
        Key one{};
        one[31] = 1;
        require(encode_wif(one, false) == "5HpHagT65TZzG1PH3CSu63k8DbpvD8s5ip4nEB3kEsreAnchuDf",
                "Key 1 uncompressed vector");
        require(encode_wif(one, true) == "KwDiBf89QgGbjEhKnhXJuH7LrciVrZi3qYjgd9M7rFU73sVHnoWn",
                "Key 1 compressed vector");
        require(verify_wif(encode_wif(one, true)).valid, "Compressed decode");
        std::string bad = sample;
        bad.back() = '1';
        require(!verify_wif(bad).valid, "Invalid checksum");
        require(!verify_wif(sample.substr(1)).valid, "Short WIF");
        require(!verify_wif(sample + "1").valid, "Wrong decoded length");
        bad = sample;
        bad[10] = '0';
        require(!verify_wif(bad).valid, "Invalid Base58");
        require(!verify_wif(raw_wif(Key{}, false)).valid, "Zero private key, valid checksum");
        auto order =
            key_from_hex("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
        require(!verify_wif(raw_wif(order, false)).valid, "Curve order, valid checksum");
        Key max{};
        max.fill(255);
        require(!verify_wif(raw_wif(max, true)).valid, "Key over curve order");
        require(!verify_wif(raw_wif(one, true, 0x80, 2)).valid, "Compression marker");
        require(!verify_wif(raw_wif(one, false, 0xef)).valid, "Testnet rejected");
        throws([&] { encode_wif(Key{}, false); }, "Encoder rejects zero");
        require(offset_key(key_from_hex(
                               "000000000000000000000000000000000000000000000000000000000000ffff"),
                           1)[29] == 1,
                "Multi-byte carry");
        for (unsigned i = 1; i <= 256; i++)
            for (bool compressed : {false, true}) {
                Key k = offset_key(v.key, i * 7919u);
                auto w = encode_wif(k, compressed);
                auto r = verify_wif(w);
                require(r.valid && r.key == k && r.compressed == compressed,
                        "Independent SHA roundtrip");
            }
        Config c;
        c.fillers = "K";
        c.all_positions = false;
        auto jobs = plan(c);
        bool covered = false;
        for (auto &j : jobs) {
            if (sample.compare(0, j.prefix.size(), j.prefix) == 0) {
                require(j.first <= v.key && v.key <= j.last, "Sample key interval containment");
                require(candidate(j, v.key), "Sample interval exact match");
                covered = true;
            }
        }
        require(covered, "Plan includes requested sample");
        c.phrase = "HelloWorldThisIsAttila";
        throws([&] { plan(c); }, "Invalid phrase rejected");
        c.phrase = "Mystery";
        c.max_suffix = 13;
        throws([&] { plan(c); }, "Suffix bound");
        c.max_suffix = 6;
        c.compressed = true;
        c.all_positions = true;
        jobs = plan(c);
        require(!jobs.empty(), "Compressed plan");
        for (auto &j : jobs) {
            uint64_t count = std::min<uint64_t>(j.count, 10);
            for (uint64_t i = 0; i < count; i++) {
                auto k = offset_key(j.first, i);
                auto w = encode_wif(k, true);
                require(candidate(j, k) == (w.compare(0, j.prefix.size(), j.prefix) == 0),
                        "Interval equals actual Base58 prefix");
            }
        }
        auto temp = std::filesystem::temp_directory_path() / "vanity-wif-tests";
        std::filesystem::create_directories(temp);
        auto state = (temp / "resume.state").string(), out = (temp / "results").string();
        Checkpoint cp;
        cp.config.fillers = "K";
        cp.config.all_positions = false;
        cp.config.max_results = 1;
        cp.config.use_gpu = false;
        Search first;
        first.run(cp, state, out);
        auto saved = load_state(state);
        require(saved.hits.size() == 1, "Search finds real WIF");
        require(verify_wif(saved.hits[0].wif).valid, "Saved independent verification");
        saved.config.max_results = 5;
        Search resumed;
        resumed.run(saved, state, out);
        auto final = load_state(state);
        require(final.hits.size() == 5, "Resume increases results");
        cp.config.max_results = 5;
        Search fresh;
        fresh.run(cp, (temp / "fresh.state").string(), (temp / "fresh").string());
        auto reference = fresh.checkpoint();
        for (size_t i = 0; i < final.hits.size(); i++)
            require(final.hits[i].wif == reference.hits[i].wif,
                    "Resume neither skips nor duplicates");
        require(std::filesystem::file_size(out + ".csv") > 0 &&
                    std::filesystem::file_size(out + ".txt") > 0,
                "CSV and TXT export");
        {
            std::ofstream f(state);
            f << "broken";
        }
        throws([&] { load_state(state); }, "Corrupt state rejected");
        Config overlap;
        overlap.phrase = "5K";
        overlap.before = "";
        overlap.after = "";
        overlap.fillers = "K";
        overlap.max_fill = 50;
        auto overlaps = plan(overlap);
        require(std::any_of(overlaps.begin(), overlaps.end(),
                            [](const Job &j) {
                                return j.prefix == "5K" + std::string(j.prefix.size() - 2, 'K');
                            }),
                "Phrase may overlap mandatory WIF header");
        Checkpoint long_run;
        long_run.config.phrase = "Mystery";
        long_run.config.fillers = "K";
        long_run.config.min_suffix = long_run.config.max_suffix = 9;
        long_run.config.all_positions = false;
        long_run.config.max_results = 100000;
        long_run.config.use_gpu = false;
        Search stoppable;
        std::exception_ptr worker_error;
        std::thread worker([&] {
            try {
                stoppable.run(long_run, state, out);
            } catch (...) {
                worker_error = std::current_exception();
            }
        });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (stoppable.progress().tested == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        stoppable.stop();
        worker.join();
        if (worker_error)
            std::rethrow_exception(worker_error);
        auto paused = load_state(state);
        require(stoppable.progress().status == "Stopped", "STOP exits and persists state");
        require(paused.tested > 0 && !paused.hits.empty(), "STOP follows real processed batch");
        auto previous = paused.hits.size();
        paused.config.max_results = previous + 1;
        Search continued;
        continued.run(paused, state, out);
        auto next = load_state(state);
        require(next.hits.size() == previous + 1, "Continue after STOP");
        std::set<std::string> unique;
        for (const auto &h : next.hits)
            unique.insert(h.wif);
        require(unique.size() == next.hits.size(), "STOP continuation has no duplicate results");
        auto device = gpu_device();
        if (argc == 2 && std::string(argv[1]) == "--require-gpu")
            require(device.available,
                    "A real compatible NVIDIA GPU is required for this acceptance run");
        if (device.available) {
            GpuBatch gpu;
            Config gc;
            gc.phrase = "Mystery";
            gc.min_suffix = gc.max_suffix = 9;
            gc.all_positions = false;
            for (bool compressed : {false, true}) {
                gc.compressed = compressed;
                auto js = plan(gc);
                for (size_t q = 0; q < std::min<size_t>(js.size(), 3); q++) {
                    auto &j = js[q];
                    uint32_t n = uint32_t(std::min<uint64_t>(65536, j.count));
                    auto indices = gpu.run(j, j.first, n);
                    std::set<uint32_t> actual(indices.begin(), indices.end());
                    for (uint32_t i = 0; i < n; i++)
                        require(bool(actual.count(i)) == candidate(j, offset_key(j.first, i)),
                                "GPU CPU parity");
                }
            }
        } else
            std::cout << "CUDA tests SKIPPED: " << device.name << '\n';
        std::filesystem::remove_all(temp);
        std::cout << "PASS " << checks << " assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
