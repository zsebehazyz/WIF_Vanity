// End-to-end test of the actual Windows executable, using ordinary Win32 UI messages.
#include "solvan/core.hpp"
#include "solvan/ui_ids.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <functional>
#include <set>
#include <stdexcept>
using namespace solvan;
using namespace solvan::ui;
static void require(bool b, const char *why) {
    if (!b)
        throw std::runtime_error(why);
}
static void wait_for(const std::function<bool()> &condition, const char *why) {
    auto end = GetTickCount64() + 20000;
    while (GetTickCount64() < end) {
        if (condition())
            return;
        Sleep(20);
    }
    throw std::runtime_error(why);
}
struct App {
    PROCESS_INFORMATION p{};
    HWND h = nullptr;
    App(const std::filesystem::path &exe, const std::filesystem::path &data) {
        std::wstring cmd = L"\"" + exe.wstring() + L"\" --data-dir \"" + data.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        require(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                               &startup, &p),
                "Launch GUI");
        wait_for(
            [&] {
                EnumWindows(
                    [](HWND w, LPARAM param) -> BOOL {
                        auto self = reinterpret_cast<App *>(param);
                        DWORD pid = 0;
                        GetWindowThreadProcessId(w, &pid);
                        wchar_t cls[80] = {};
                        GetClassNameW(w, cls, 80);
                        if (pid == self->p.dwProcessId &&
                            std::wstring(cls) == L"SolanaVanity.Main") {
                            self->h = w;
                            return FALSE;
                        }
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(this));
                return h != nullptr && IsWindowVisible(h) && GetDlgItem(h, START) != nullptr;
            },
            "GUI window did not appear");
    }
    void set(int id, const wchar_t *s) {
        SendMessageW(GetDlgItem(h, id), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(s));
    }
    void click(int id) {
        SendMessageW(h, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                     reinterpret_cast<LPARAM>(GetDlgItem(h, id)));
    }
    void check(int id, bool value) {
        SendMessageW(GetDlgItem(h, id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    void idle() {
        wait_for([&] { return IsWindowEnabled(GetDlgItem(h, START)); },
                 "GUI did not return to idle");
    }
    void close() {
        if (h && IsWindow(h)) {
            PostMessageW(h, WM_CLOSE, 0, 0);
            require(WaitForSingleObject(p.hProcess, 20000) == WAIT_OBJECT_0,
                    "GUI did not close cleanly");
            DWORD code = 1;
            GetExitCodeProcess(p.hProcess, &code);
            require(code == 0, "GUI exit status");
            h = nullptr;
        }
    }
    ~App() {
        if (h && IsWindow(h)) {
            PostMessageW(h, WM_CLOSE, 0, 0);
            WaitForSingleObject(p.hProcess, 20000);
        }
        if (p.hThread)
            CloseHandle(p.hThread);
        if (p.hProcess)
            CloseHandle(p.hProcess);
    }
};
int main() {
    try {
        wchar_t module[32768];
        GetModuleFileNameW(nullptr, module, 32768);
        auto exe = std::filesystem::path(module).parent_path() / "SolanaVanity.exe";
        auto data = std::filesystem::temp_directory_path() /
                    ("solana-gui-smoke-" + std::to_string(GetCurrentProcessId()) + "-" +
                     std::to_string(GetTickCount64()));
        std::filesystem::create_directories(data);
        auto statefile = (data / "search.state").u8string();
        {
            App app(exe, data);
            app.set(PREFIX, L"1");
            app.set(LIMIT, L"1");
            app.set(THREADS, L"2");
            app.set(BATCH, L"256");
            app.check(GPU, false);
            app.click(START);
            app.idle();
            auto s = load_state(statefile);
            require(s.hits.size() == 1, "START should find one Solana key");
            for (const auto &h : s.hits)
                require(address(read_keypair((data / "keys" / h.file).u8string())) == h.address,
                        "GUI keypair validation");
            app.set(LIMIT, L"2");
            app.click(RESUME);
            app.idle();
            s = load_state(statefile);
            require(s.hits.size() == 2, "RESUME should retain keys and reach two results");
            app.close();
        }
        {
            App app(exe, data);
            app.click(RESUME);
            app.idle();
            auto s = load_state(statefile);
            require(s.hits.size() == 3, "Close/reopen and DPAPI resume");
            std::set<std::string> unique;
            for (auto &h : s.hits) {
                require(h.address.rfind("1", 0) == 0, "Exact prefix in GUI result");
                require(address(read_keypair((data / "keys" / h.file).u8string())) == h.address,
                        "Persisted seed matches public key");
                unique.insert(h.address);
            }
            require(unique.size() == 3, "GUI resume must not repeat results");
            app.close();
        }
        {
            auto stopdir = data / "stop";
            App app(exe, stopdir);
            app.set(PREFIX, L"111111111111");
            app.set(LIMIT, L"10");
            app.set(THREADS, L"2");
            app.set(BATCH, L"256");
            app.check(GPU, false);
            app.click(START);
            wait_for(
                [&] {
                    wchar_t status[2048] = {};
                    GetWindowTextW(GetDlgItem(app.h, STATS), status, 2048);
                    std::wstring s = status;
                    return s.find(L"Searching") != std::wstring::npos &&
                           s.find(L"Jeloltek: 0 ") == std::wstring::npos;
                },
                "No real GUI search batch");
            app.click(STOP);
            app.idle();
            auto s = load_state((stopdir / "search.state").u8string());
            require(s.tested > 0 && s.next > 0, "STOP persisted real work");
            require(s.hits.empty(), "Hard-prefix run has no spurious results");
            app.close();
        }
        std::filesystem::remove_all(data);
        std::cout << "PASS GUI: start, prefix correctness, Solana JSON, DPAPI state, resume, "
                     "close/reopen, STOP checkpoint\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL GUI: " << e.what() << '\n';
        return 1;
    }
}
