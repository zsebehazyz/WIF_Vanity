#include "solvan/core.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#endif
using namespace solvan;
#ifdef SOLVAN_PORT_HOST
namespace solvan {
Bytes ported_public(const Prepared &, uint64_t);
Bytes ported_seed(const Prepared &, uint64_t);
} // namespace solvan
#endif
static uint64_t assertions = 0;
static void require(bool v, const char *why) {
    assertions++;
    if (!v)
        throw std::runtime_error(why);
}
template <class F> static void throws(F f, const char *why) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, why);
}
static std::string read_file(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
int main(int argc, char **argv) {
    try {
        const char *seeds[] = {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
                               "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
                               "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7"};
        const char *pubs[] = {"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
                              "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
                              "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025"};
        for (unsigned i = 0; i < 3; i++) {
            auto seed = from_hex(seeds[i]);
            require(hex(public_key(seed)) == pubs[i], "RFC8032 reference public key");
            require(hex(independent_public(seed)) == pubs[i],
                    "RFC8032 independent Monocypher public key");
        }
        Bytes master{};
        for (unsigned i = 0; i < 32; i++)
            master[i] = uint8_t(i);
        auto prepared = prepare(master, "A");
        uint64_t indices[] = {0, 1, 2, 4294967303ULL, 18446744073709551614ULL};
        const char *expected[] = {
            "8c4c181bec241017278e84a39e32c2c23d870d0b4b46fd77947bdd5579d77ec1",
            "eaf8a2e89734cae7faaa478290f0aa0699ac00ca9bdbc8bdfd09ef5a4eef45c4",
            "a62b0627b66dc32b204fc1726f8870274312d3692798830f8a4aae13f5e71407",
            "d7addb9de3353a5276da503d83687bda9142c0d17d8fd85ffcff4d40c786a7e6",
            "36ec9662c22e27cb0b6738c9eb1fad258b86758232f86892e76e47cc09e85c10"};
        for (unsigned i = 0; i < 5; i++) {
            require(hex(derive_seed(prepared, indices[i])) == expected[i],
                    "Python hashlib HMAC-SHA512 fixture");
            require(independent_seed(master, indices[i]) == derive_seed(prepared, indices[i]),
                    "Independent HMAC with 64-bit counter");
        }
        for (unsigned i = 0; i < 128; i++) {
            auto seed = derive_seed(prepared, i);
            require(independent_seed(master, i) == seed, "HMAC implementation parity");
            require(public_key(seed) == independent_public(seed), "Ed25519 implementation parity");
#ifdef SOLVAN_PORT_HOST
            require(ported_seed(prepared, i) == seed, "CUDA-adapted rolling SHA512 host parity");
            require(ported_public(prepared, i) == public_key(seed),
                    "CUDA-adapted Ed25519 host parity");
#endif
        }
        Bytes zeros{};
        require(address(zeros) == std::string(32, '1'), "Leading zero Base58 encoding");
        auto one = zeros;
        one.back() = 1;
        require(address(one) == std::string(31, '1') + "2", "Base58 leading zero count");
        std::vector<std::string> prefixes = {
            "11",  "111",  "1112",         "111A", "Ab",      "ab",          "SoL",
            "soL", "pump", "111111111111", "zzzz", "1111112", "11111111111A"};
        for (char c : std::string(alphabet))
            prefixes.push_back(std::string(1, c));
        uint64_t rng = 0x871abd9f1234567ULL;
        std::vector<Bytes> samples;
        for (unsigned i = 0; i < 512; i++) {
            Bytes b{};
            for (auto &x : b) {
                rng ^= rng << 13;
                rng ^= rng >> 7;
                rng ^= rng << 17;
                x = uint8_t(rng);
            }
            samples.push_back(b);
        }
        samples.push_back(zeros);
        for (unsigned z = 0; z < 32; z++) {
            Bytes b{};
            b[z] = 1;
            for (unsigned i = z + 1; i < 32; i++)
                b[i] = uint8_t(i * 17 + 3);
            samples.push_back(b);
        }
        for (auto &prefix : prefixes) {
            auto p = prepare(master, prefix);
            require(p.range_count > 0 && p.range_count <= 4, "Prefix range bound");
            for (auto &b : samples)
                require(matches(p, b) == (address(b).rfind(prefix, 0) == 0),
                        "Numeric prefix equals actual Base58, including leading zeros");
            for (unsigned r = 0; r < p.range_count; r++)
                for (bool high : {false, true}) {
                    Bytes edge{};
                    std::copy_n(high ? p.ranges[r].high : p.ranges[r].low, 32, edge.begin());
                    require(matches(p, edge) == (address(edge).rfind(prefix, 0) == 0),
                            "Inclusive prefix interval boundary");
                }
        }
        for (auto prefix : {"", "0", "O", "I", "l", "a b", "aaaaaaaaaaaaa"})
            throws([&] { prepare(master, prefix); }, "Invalid prefix rejected");
        Config bad;
        bad.batch = 123;
        throws([&] { validate(bad); }, "Invalid CUDA batch rejected");
        bad.batch = 4096;
        bad.threads = 33;
        throws([&] { validate(bad); }, "Invalid CPU thread count");
        auto random1 = random_master(), random2 = random_master();
        require(random1 != random2, "OS CSPRNG provides fresh masters");
        auto temp = std::filesystem::temp_directory_path() /
                    ("solvan-tests-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp);
        auto keyfile = temp / "test.json";
        auto seed = from_hex(seeds[0]), pub = from_hex(pubs[0]);
        save_keypair(keyfile.u8string(), seed, pub);
        require(read_keypair(keyfile.u8string()) == pub, "Solana 64-byte JSON roundtrip");
        save_keypair(keyfile.u8string(), seed, pub);
        throws([&] { save_keypair(keyfile.u8string(), from_hex(seeds[1]), from_hex(pubs[1])); },
               "Existing key file cannot be overwritten");
#ifndef _WIN32
        struct stat st{};
        require(stat(keyfile.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600,
                "Private key permissions are 0600");
#endif
        {
            std::ofstream f(keyfile);
            f << "[0,1]";
        }
        throws([&] { read_keypair(keyfile.u8string()); }, "Malformed keypair rejected");
        auto dir = temp / "search";
        State state;
        state.master = master;
        state.config.gpu = false;
        state.config.threads = 2;
        state.config.batch = 256;
        state.config.limit = 1;
        state.config.prefix = address(public_key(derive_seed(prepared, 0))).substr(0, 1);
        Search first;
        first.run(state, dir.u8string());
        auto saved = load_state((dir / "search.state").u8string());
        require(saved.hits.size() == 1 && saved.next > 0, "Real prefix search and checkpoint");
        require(saved.master == master, "Master checkpoint roundtrip");
#ifdef _WIN32
        require(read_file(dir / "search.state").find(hex(master)) == std::string::npos,
                "Windows DPAPI hides plaintext master");
#endif
        saved.config.limit = 3;
        Search resumed;
        resumed.run(saved, dir.u8string());
        auto complete = load_state((dir / "search.state").u8string());
        require(complete.hits.size() == 3, "Resume produces additional keys");
        state.config.limit = 3;
        Search fresh;
        fresh.run(state, (temp / "fresh").u8string());
        auto reference = fresh.snapshot();
        for (unsigned i = 0; i < 3; i++)
            require(complete.hits[i].address == reference.hits[i].address,
                    "Resume has no gaps or duplicates");
        for (auto &h : complete.hits) {
            require(h.address.rfind(state.config.prefix, 0) == 0,
                    "Found address has exact case-sensitive prefix");
            require(address(read_keypair((dir / "keys" / h.file).u8string())) == h.address,
                    "Every persisted key independently verified");
        }
        auto hard = state;
        hard.config.prefix = "111111111111";
        hard.config.limit = 10000;
        hard.hits.clear();
        Search stoppable;
        std::exception_ptr worker_error;
        std::thread worker([&] {
            try {
                stoppable.run(hard, (temp / "stop").u8string());
            } catch (...) {
                worker_error = std::current_exception();
            }
        });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (stoppable.progress().tested == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        stoppable.stop();
        worker.join();
        if (worker_error)
            std::rethrow_exception(worker_error);
        auto paused = load_state((temp / "stop" / "search.state").u8string());
        require(paused.tested > 0 && stoppable.progress().status == "Stopped",
                "STOP saves a completed real batch");
        {
            std::ofstream f(dir / "search.state");
            f << "corrupt";
        }
        throws([&] { load_state((dir / "search.state").u8string()); }, "Corrupt state rejected");
        auto device = gpu_device();
        bool require_gpu = argc == 2 && std::string(argv[1]) == "--require-gpu";
        if (require_gpu)
            require(device.available, "Real compatible NVIDIA GPU required");
        if (device.available) {
            Gpu gpu;
            for (uint64_t start : {0ULL, 4294967290ULL}) {
                auto generated = gpu.derive_for_test(prepared, start, 128);
                for (unsigned i = 0; i < 128; i++)
                    require(generated[i] == independent_public(independent_seed(master, start + i)),
                            "Actual GPU public keys equal independent CPU");
            }
            for (auto prefix : {"A", "z", "1", "11", "Ab"}) {
                auto p = prepare(master, prefix);
                auto found = gpu.run(p, 0, 4096);
                std::set<uint32_t> set(found.begin(), found.end());
                require(set.size() == found.size(), "GPU indices are unique");
                for (auto i : found)
                    require(i < 4096, "GPU index bound");
                for (unsigned i = 0; i < 4096; i++)
                    require(bool(set.count(i)) ==
                                matches(p, public_key(independent_seed(master, i))),
                            "GPU numeric matcher parity");
            }
        } else
            std::cout << "GPU hardware tests SKIPPED: " << device.name << '\n';
        if (argc == 3 && std::string(argv[1]) == "--export-fixtures") {
            std::filesystem::create_directories(std::filesystem::u8path(argv[2]));
            for (unsigned i = 0; i < 3; i++)
                save_keypair((std::filesystem::u8path(argv[2]) /
                              ("rfc8032-" + std::to_string(i + 1) + ".json"))
                                 .u8string(),
                             from_hex(seeds[i]), from_hex(pubs[i]));
        }
        std::filesystem::remove_all(temp);
        std::cout << "PASS " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << assertions << ": " << e.what() << '\n';
        return 1;
    }
}
