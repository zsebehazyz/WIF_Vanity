#include "solvan/core.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif
namespace solvan {
namespace {
std::string read(const std::string &path, size_t max) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open saved file");
    std::string s;
    char c;
    while (f.get(c)) {
        if (s.size() >= max)
            throw std::runtime_error("Saved file exceeds size limit");
        s += c;
    }
    if (!f.eof())
        throw std::runtime_error("Cannot read saved file");
    return s;
}
void exclusive(const std::string &path, const std::string &data) {
#ifdef _WIN32
    auto p = std::filesystem::u8path(path).wstring();
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create private output file");
    DWORD n = 0;
    BOOL ok = WriteFile(h, data.data(), DWORD(data.size()), &n, nullptr);
    BOOL flushed = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !flushed || n != data.size())
        throw std::runtime_error("Private output write failed");
#else
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0)
        throw std::runtime_error("Cannot create private output file");
    size_t done = 0;
    while (done < data.size()) {
        auto n = write(fd, data.data() + done, data.size() - done);
        if (n <= 0) {
            close(fd);
            throw std::runtime_error("Private output write failed");
        }
        done += size_t(n);
    }
    int ok = fsync(fd);
    close(fd);
    if (ok)
        throw std::runtime_error("Private output flush failed");
#endif
}
std::string seal(const Bytes &master) {
#ifdef _WIN32
    DATA_BLOB in{32, const_cast<BYTE *>(master.data())}, out{};
    if (!CryptProtectData(&in, L"SolanaVanity state", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        throw std::runtime_error("Windows DPAPI encryption failed (error " + std::to_string(GetLastError()) + ")");
    std::string s = "dpapi:";
    for (DWORD i = 0; i < out.cbData; i++) {
        s += "0123456789abcdef"[out.pbData[i] >> 4];
        s += "0123456789abcdef"[out.pbData[i] & 15];
    }
    wipe(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return s;
#else
    return "linux-private-file:" + hex(master);
#endif
}
Bytes unseal(const std::string &s) {
#ifdef _WIN32
    if (s.rfind("dpapi:", 0) != 0 || s.size() % 2 || s.size() > 65536)
        throw std::runtime_error("Invalid DPAPI checkpoint");
    std::vector<uint8_t> encrypted;
    for (size_t i = 6; i < s.size(); i += 2) {
        unsigned n = 0;
        std::istringstream f(s.substr(i, 2));
        f >> std::hex >> n;
        if (!f || n > 255)
            throw std::runtime_error("Invalid DPAPI encoding");
        encrypted.push_back(uint8_t(n));
    }
    DATA_BLOB in{DWORD(encrypted.size()), encrypted.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &out))
        throw std::runtime_error("Cannot decrypt state on this Windows account");
    if (out.cbData != 32) {
        LocalFree(out.pbData);
        throw std::runtime_error("Invalid decrypted state length");
    }
    Bytes b{};
    std::copy_n(out.pbData, 32, b.begin());
    wipe(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return b;
#else
    if (s.rfind("linux-private-file:", 0) != 0)
        throw std::runtime_error("Incompatible encrypted checkpoint");
    return from_hex(s.substr(19));
#endif
}
void atomic(const std::string &path, const std::string &data) {
    auto temp = path + ".tmp";
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(temp), ec);
    exclusive(temp, data);
#ifdef _WIN32
    if (!MoveFileExW(std::filesystem::u8path(temp).c_str(), std::filesystem::u8path(path).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace checkpoint");
#else
    std::filesystem::rename(temp, path);
#endif
}
} // namespace
void save_keypair(const std::string &file, const Bytes &seed, const Bytes &pub) {
    if (independent_public(seed) != pub)
        throw std::runtime_error("Independent Ed25519 verification failed");
    if (std::filesystem::exists(std::filesystem::u8path(file))) {
        if (read_keypair(file) != pub)
            throw std::runtime_error("Existing key file mismatch");
        return;
    }
    std::ostringstream s;
    s << '[';
    for (unsigned i = 0; i < 64; i++) {
        if (i)
            s << ',';
        s << unsigned(i < 32 ? seed[i] : pub[i - 32]);
    }
    s << "]\n";
    auto data = s.str();
    exclusive(file, data);
    wipe(data.data(), data.size());
}
Bytes read_keypair(const std::string &file) {
    auto raw = read(file, 4096);
    std::istringstream f(raw);
    uint8_t b[64];
    char c = 0;
    f >> c;
    if (c != '[')
        throw std::runtime_error("Keypair must be a JSON array");
    for (unsigned i = 0; i < 64; i++) {
        int n = -1;
        f >> n;
        if (!f || n < 0 || n > 255)
            throw std::runtime_error("Keypair must contain exactly 64 byte values");
        b[i] = uint8_t(n);
        f >> c;
        if (c != (i == 63 ? ']' : ','))
            throw std::runtime_error("Keypair must contain exactly 64 bytes");
    }
    f >> std::ws;
    if (!f.eof())
        throw std::runtime_error("Trailing keypair data");
    Bytes seed{}, pub{};
    std::copy_n(b, 32, seed.begin());
    std::copy_n(b + 32, 32, pub.begin());
    bool ok = independent_public(seed) == pub;
    wipe(seed.data(), 32);
    wipe(b, sizeof(b));
    wipe(raw.data(), raw.size());
    if (!ok)
        throw std::runtime_error("Keypair seed does not match public key");
    return pub;
}
void save_state(const std::string &file, const State &s) {
    std::ostringstream f;
    f << "SolanaVanity-state-1\n"
      << std::quoted(s.config.prefix) << ' ' << s.config.gpu << ' ' << s.config.threads << ' '
      << s.config.batch << ' ' << s.config.limit << '\n'
      << seal(s.master) << '\n'
      << s.next << ' ' << s.tested << ' ' << std::setprecision(17) << s.seconds << ' '
      << s.hits.size() << '\n';
    for (auto &h : s.hits)
        f << std::quoted(h.address) << ' ' << std::quoted(h.file) << ' ' << std::quoted(h.time)
          << '\n';
    atomic(file, f.str());
}
State load_state(const std::string &file) {
    auto raw = read(file, 2 * 1024 * 1024);
    std::istringstream f(raw);
    std::string magic, sealed;
    std::getline(f, magic);
    if (magic != "SolanaVanity-state-1")
        throw std::runtime_error("Unsupported checkpoint format");
    State s;
    size_t count = 0;
    f >> std::quoted(s.config.prefix) >> s.config.gpu >> s.config.threads >> s.config.batch >>
        s.config.limit >> sealed >> s.next >> s.tested >> s.seconds >> count;
    if (!f || count > 10000 || !std::isfinite(s.seconds) || s.seconds < 0)
        throw std::runtime_error("Corrupt checkpoint");
    validate(s.config);
    s.master = unseal(sealed);
    for (size_t i = 0; i < count; i++) {
        Hit h;
        f >> std::quoted(h.address) >> std::quoted(h.file) >> std::quoted(h.time);
        if (!f || h.address.size() < 32 || h.address.size() > 44 || h.file != h.address + ".json" ||
            h.address.find_first_not_of(alphabet) != std::string::npos ||
            h.address.rfind(s.config.prefix, 0) != 0)
            throw std::runtime_error("Corrupt checkpoint result");
        auto key = read_keypair(
            (std::filesystem::u8path(file).parent_path() / "keys" / h.file).u8string());
        if (address(key) != h.address)
            throw std::runtime_error("Saved address does not match keypair");
        s.hits.push_back(h);
    }
    f >> std::ws;
    if (!f.eof())
        throw std::runtime_error("Unexpected checkpoint data");
    wipe(raw.data(), raw.size());
    return s;
}
} // namespace solvan
