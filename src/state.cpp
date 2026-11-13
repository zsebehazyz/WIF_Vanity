#include "vanity/search.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace vanity {
static void replace(const std::string &from, const std::string &to) {
#ifdef _WIN32
    auto f = std::filesystem::u8path(from).wstring(), t = std::filesystem::u8path(to).wstring();
    if (!MoveFileExW(f.c_str(), t.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot atomically replace output file");
#else
    std::filesystem::rename(from, to);
#endif
}
static std::ofstream stream(const std::string &path) {
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("Cannot write " + path);
    return f;
}
static void finish(std::ofstream &f, const std::string &tmp, const std::string &path) {
    f.flush();
    if (!f)
        throw std::runtime_error("Write failed: " + tmp);
    f.close();
    if (!f)
        throw std::runtime_error("Close failed: " + tmp);
    replace(tmp, path);
}
void save_state(const std::string &path, const Checkpoint &s) {
    auto f = stream(path + ".tmp");
    auto &c = s.config;
    f << "VanityWIF-state-1\n"
      << std::quoted(c.phrase) << ' ' << std::quoted(c.fillers) << ' ' << std::quoted(c.before)
      << ' ' << std::quoted(c.after) << '\n'
      << c.min_fill << ' ' << c.max_fill << ' ' << c.min_suffix << ' ' << c.max_suffix << ' '
      << c.compressed << ' ' << c.all_positions << ' ' << c.use_gpu << ' ' << c.max_results << '\n'
      << s.job << ' ' << s.offset << ' ' << s.tested << ' ' << std::setprecision(17) << s.seconds
      << ' ' << s.hits.size() << '\n';
    for (auto &h : s.hits)
        f << std::quoted(h.wif) << ' ' << std::quoted(h.key) << ' ' << std::quoted(h.time) << ' '
          << h.compressed << '\n';
    finish(f, path + ".tmp", path);
}
Checkpoint load_state(const std::string &path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    Checkpoint s;
    std::string magic;
    std::getline(f, magic);
    if (magic != "VanityWIF-state-1")
        throw std::runtime_error("Invalid checkpoint format");
    auto &c = s.config;
    size_t count = 0;
    f >> std::quoted(c.phrase) >> std::quoted(c.fillers) >> std::quoted(c.before) >>
        std::quoted(c.after) >> c.min_fill >> c.max_fill >> c.min_suffix >> c.max_suffix >>
        c.compressed >> c.all_positions >> c.use_gpu >> c.max_results >> s.job >> s.offset >>
        s.tested >> s.seconds >> count;
    if (!f || count > 100000 || s.seconds < 0 || s.seconds > 1e12)
        throw std::runtime_error("Corrupt checkpoint");
    validate_config(c);
    for (size_t i = 0; i < count; i++) {
        Hit h;
        f >> std::quoted(h.wif) >> std::quoted(h.key) >> std::quoted(h.time) >> h.compressed;
        if (!f)
            throw std::runtime_error("Truncated checkpoint");
        auto v = verify_wif(h.wif);
        if (!v.valid || hex(v.key) != h.key || v.compressed != h.compressed)
            throw std::runtime_error("Checkpoint contains invalid WIF");
        s.hits.push_back(h);
    }
    f >> std::ws;
    if (!f.eof())
        throw std::runtime_error("Unexpected checkpoint data");
    return s;
}
void export_hits(const std::string &base, const std::vector<Hit> &hits) {
    auto csv = stream(base + ".csv.tmp"), txt = stream(base + ".txt.tmp");
    csv << "WIF,HEX,Type,TimeUTC\r\n";
    for (auto &h : hits) {
        const char *type = h.compressed ? "compressed" : "uncompressed";
        csv << h.wif << ',' << h.key << ',' << type << ',' << h.time << "\r\n";
        txt << h.wif << '\t' << h.key << '\t' << type << '\t' << h.time << "\r\n";
    }
    finish(csv, base + ".csv.tmp", base + ".csv");
    finish(txt, base + ".txt.tmp", base + ".txt");
}
} // namespace vanity
