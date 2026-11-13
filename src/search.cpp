#include "vanity/search.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
namespace vanity {
static void base58_text(const std::string &s) {
    for (char c : s)
        if (!c || !std::strchr(alphabet, c))
            throw std::invalid_argument(
                "Text must use Base58: 0, O, I, l and spaces are forbidden");
}
void validate_config(const Config &c) {
    if (c.phrase.empty() || c.phrase.size() > 46)
        throw std::invalid_argument("Phrase length must be 1..46");
    base58_text(c.phrase);
    base58_text(c.fillers);
    base58_text(c.before);
    base58_text(c.after);
    if (c.fillers.empty() || c.fillers.size() > 58)
        throw std::invalid_argument("Use 1..58 filler characters");
    if (c.min_fill > c.max_fill || c.max_fill > 50)
        throw std::invalid_argument("Filler limits must satisfy 0 <= min <= max <= 50");
    if (c.min_suffix < 4 || c.max_suffix > 12 || c.min_suffix > c.max_suffix)
        throw std::invalid_argument("Suffix limits must satisfy 4 <= min <= max <= 12");
    if (c.max_results < 1 || c.max_results > 100000)
        throw std::invalid_argument("Result limit must be 1..100000");
}
static bool make_job(Job &j) {
    j.low = decode_number(j.prefix + std::string(j.suffix, '1'));
    j.high = decode_number(j.prefix + std::string(j.suffix, 'z'));
    unsigned n = j.compressed ? 38 : 37, start = 40 - n;
    Key lo{};
    lo[31] = 1;
    Key hi = key_from_hex("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364140");
    Big minimum{}, maximum{};
    minimum[start] = maximum[start] = 0x80;
    std::copy(lo.begin(), lo.end(), minimum.begin() + start + 1);
    std::copy(hi.begin(), hi.end(), maximum.begin() + start + 1);
    if (j.compressed)
        minimum[35] = maximum[35] = 1;
    std::fill(maximum.end() - 4, maximum.end(), 255);
    j.low = std::max(j.low, minimum);
    j.high = std::min(j.high, maximum);
    if (j.low > j.high)
        return false;
    std::copy_n(j.low.begin() + start + 1, 32, j.first.begin());
    std::copy_n(j.high.begin() + start + 1, 32, j.last.begin());
    // Projection intentionally includes both boundary keys; hash and exact interval
    // comparison remove checksum/marker boundary mismatches without losing a key.
    Key diff{};
    int borrow = 0;
    for (int i = 31; i >= 0; i--) {
        int d = int(j.last[i]) - j.first[i] - borrow;
        borrow = d < 0;
        diff[i] = uint8_t(d);
    }
    if (std::any_of(diff.begin(), diff.begin() + 24, [](uint8_t b) { return b != 0; }))
        throw std::overflow_error("Interval exceeds 64-bit work counter");
    uint64_t d = 0;
    for (unsigned i = 24; i < 32; i++)
        d = (d << 8) | diff[i];
    if (d == UINT64_MAX)
        throw std::overflow_error("Interval too large");
    j.count = d + 1;
    return true;
}
std::vector<Job> plan(const Config &c) {
    validate_config(c);
    std::vector<Job> out;
    std::set<std::string> seen;
    const std::string motif = c.before + c.phrase + c.after;
    const auto heads = c.compressed ? std::vector<std::string>{"K", "L"}
                                    : std::vector<std::string>{"5H", "5J", "5K"};
    for (unsigned suffix = c.min_suffix; suffix <= c.max_suffix; ++suffix) {
        const size_t fixed = (c.compressed ? 52 : 51) - suffix;
        if (motif.size() > fixed)
            continue;
        for (char filler : c.fillers)
            for (const auto &head : heads) {
                size_t first = c.all_positions ? 0 : head.size();
                size_t last = c.all_positions ? fixed - motif.size() : head.size();
                for (size_t pos = first; pos <= last; ++pos) {
                    if (pos + motif.size() > fixed)
                        continue;
                    size_t overlap =
                        pos < head.size() ? std::min(head.size() - pos, motif.size()) : 0;
                    const size_t fill = fixed - head.size() - (motif.size() - overlap);
                    if (fill < c.min_fill || fill > c.max_fill)
                        continue;
                    Job j;
                    j.prefix = head + std::string(fixed - head.size(), filler);
                    bool compatible = true;
                    for (size_t i = 0; i < overlap; ++i)
                        if (j.prefix[pos + i] != motif[i])
                            compatible = false;
                    if (!compatible)
                        continue;
                    j.prefix.replace(pos, motif.size(), motif);
                    j.compressed = c.compressed;
                    j.filler = filler;
                    j.suffix = suffix;
                    j.fill = unsigned(fill);
                    if (seen.insert(j.prefix + ":" + std::to_string(suffix)).second && make_job(j))
                        out.push_back(std::move(j));
                }
            }
    }
    std::stable_sort(out.begin(), out.end(), [](const Job &a, const Job &b) {
        if (a.fill != b.fill)
            return a.fill > b.fill;
        return a.suffix < b.suffix;
    });
    return out;
}
Progress Search::progress() const {
    std::lock_guard<std::mutex> l(mutex_);
    return progress_;
}
Checkpoint Search::checkpoint() const {
    std::lock_guard<std::mutex> l(mutex_);
    return state_;
}
static std::string now() {
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
void Search::run(Checkpoint state, const std::string &path, const std::string &output) {
    auto started = std::chrono::steady_clock::now();
    double prior = state.seconds;
    {
        std::lock_guard<std::mutex> l(mutex_);
        state_ = state;
        progress_ = {};
        progress_.status = "Planning";
    }
    try {
        auto jobs = plan(state.config);
        if (jobs.empty())
            throw std::invalid_argument("No feasible WIF patterns for these settings");
        if (state.job > jobs.size() ||
            (state.job < jobs.size() && state.offset > jobs[state.job].count))
            throw std::runtime_error("Checkpoint position outside search plan");
        Device device = gpu_device();
        std::unique_ptr<GpuBatch> gpu;
        if (state.config.use_gpu && device.available)
            gpu = std::make_unique<GpuBatch>();
        std::unordered_set<std::string> known;
        for (const auto &h : state.hits)
            known.insert(h.wif);
        auto rate_start = started;
        uint64_t rate_tested = state.tested;
        double rate = 0;
        long double remaining = 0;
        for (size_t i = state.job; i < jobs.size(); ++i)
            remaining += jobs[i].count;
        remaining -= state.offset;
        auto publish = [&](const std::string &status) {
            state.seconds =
                prior +
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::lock_guard<std::mutex> l(mutex_);
            state_ = state;
            progress_.tested = state.tested;
            progress_.results = state.hits.size();
            progress_.job_index = state.job;
            progress_.offset = state.offset;
            progress_.jobs = jobs.size();
            progress_.seconds = state.seconds;
            progress_.status = status;
            progress_.gpu = device.name;
            progress_.backend = gpu ? "CUDA" : "CPU";
            auto tick = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(tick - rate_start).count();
            if (dt >= 0.25 || (rate == 0 && dt > 0)) {
                progress_.utilization = gpu ? gpu_utilization() : -1;
                rate = (state.tested - rate_tested) / dt;
                rate_tested = state.tested;
                rate_start = tick;
            }
            progress_.rate = rate;
            progress_.eta = progress_.rate > 0 ? double(remaining / progress_.rate) : -1;
            if (state.job < jobs.size()) {
                auto &j = jobs[state.job];
                progress_.pattern = j.prefix + std::string(j.suffix, '?');
                progress_.filler = j.filler;
                progress_.suffix = j.suffix;
            }
        };
        auto saved = std::chrono::steady_clock::now();
        for (; state.job < jobs.size() && !stop_ && state.hits.size() < state.config.max_results;) {
            const auto &j = jobs[state.job];
            uint32_t count =
                uint32_t(std::min<uint64_t>(gpu ? 65536 : 4096, j.count - state.offset));
            if (!count) {
                state.job++;
                state.offset = 0;
                continue;
            }
            Key first = offset_key(j.first, state.offset);
            std::vector<uint32_t> matches;
            if (gpu)
                matches = gpu->run(j, first, count);
            else
                for (uint32_t i = 0; i < count; i++)
                    if (candidate(j, offset_key(first, i)))
                        matches.push_back(i);
            std::sort(matches.begin(), matches.end());
            bool hit_limit = false;
            uint32_t consumed = count;
            for (auto i : matches) {
                Key k = offset_key(first, i);
                std::string wif = encode_wif(k, j.compressed);
                auto checked = verify_wif(wif);
                if (!checked.valid || checked.key != k || checked.compressed != j.compressed ||
                    wif.compare(0, j.prefix.size(), j.prefix) != 0)
                    throw std::runtime_error("Independent verifier rejected a backend result");
                if (known.insert(wif).second)
                    state.hits.push_back({wif, hex(k), now(), j.compressed});
                if (state.hits.size() >= state.config.max_results) {
                    consumed = i + 1;
                    hit_limit = true;
                    break;
                }
            }
            state.offset += consumed;
            remaining -= consumed;
            state.tested += count; // All count candidates really executed, even if result limit
                                   // stops consumption.
            if (state.offset == j.count) {
                state.job++;
                state.offset = 0;
            }
            publish("Searching");
            auto current = std::chrono::steady_clock::now();
            if (hit_limit || current - saved > std::chrono::seconds(2)) {
                save_state(path, state);
                export_hits(output, state.hits);
                saved = current;
            }
            if (hit_limit)
                break;
        }
        publish(stop_                                           ? "Stopped"
                : state.hits.size() >= state.config.max_results ? "Result limit reached"
                                                                : "Complete");
        save_state(path, state);
        export_hits(output, state.hits);
    } catch (const std::exception &e) {
        {
            std::lock_guard<std::mutex> l(mutex_);
            state_ = state;
            progress_.status = std::string("Error: ") + e.what();
        }
        throw;
    }
}
} // namespace vanity
