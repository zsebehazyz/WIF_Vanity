// End-to-end test of the actual Windows executable, using ordinary Win32 UI messages.
#include "vanity/search.hpp"
#include "vanity/ui_ids.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <functional>
#include <set>
#include <stdexcept>
using namespace vanity;
using namespace vanity::ui;
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
                        if (pid == self->p.dwProcessId && std::wstring(cls) == L"VanityWIF.Main") {
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
        auto exe = std::filesystem::path(module).parent_path() / "VanityWIF.exe";
        auto data = std::filesystem::temp_directory_path() /
                    ("vanity-gui-smoke-" + std::to_string(GetCurrentProcessId()) + "-" +
                     std::to_string(GetTickCount64()));
        std::filesystem::create_directories(data);
        auto file = (data / "search.state").u8string();
        {
            App app(exe, data);
            app.set(FILLERS, L"K");
            app.set(LIMIT, L"5");
            app.set(MAXS, L"8");
            app.check(POSITIONS, false);
            app.click(START);
            app.idle();
            auto cp = load_state(file);
            require(cp.hits.size() == 5, "START must find five valid WIFs");
            app.click(RESUME);
            app.idle();
            cp = load_state(file);
            require(cp.hits.size() == 105, "RESUME should retain prior results and add 100");
            std::set<std::string> unique;
            for (const auto &h : cp.hits) {
                require(verify_wif(h.wif).valid, "GUI persisted invalid WIF");
                unique.insert(h.wif);
            }
            require(unique.size() == 105, "GUI resume duplicated results");
            app.close();
        }
        // Continue from the state after closing and reopening the actual application.
        {
            App app(exe, data);
            app.click(RESUME);
            app.idle();
            auto cp = load_state(file);
            require(cp.hits.size() == 205, "Restart and resume");
            app.close();
        }
        auto longdata = data / "stop";
        std::filesystem::create_directories(longdata);
        {
            App app(exe, longdata);
            app.set(PHRASE, L"Mystery");
            app.set(FILLERS, L"K");
            app.set(MINS, L"9");
            app.set(MAXS, L"9");
            app.set(LIMIT, L"100000");
            app.check(POSITIONS, false);
            app.check(GPU, false);
            app.click(START);
            // Wait until a real batch has reached the UI; then STOP must save that progress.
            wait_for(
                [&] {
                    wchar_t status[2048] = {};
                    GetWindowTextW(GetDlgItem(app.h, STATS), status, 2048);
                    return std::wstring(status).find(L"Searching") != std::wstring::npos;
                },
                "No real search progress");
            app.click(STOP);
            app.idle();
            auto cp = load_state((longdata / "search.state").u8string());
            require(cp.tested > 0 && !cp.hits.empty(), "STOP must persist processed work");
            app.close();
        }
        std::filesystem::remove_all(data);
        std::cout << "PASS GUI: start, resume, close/reopen, independent WIF verification, STOP "
                     "checkpoint\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL GUI: " << e.what() << '\n';
        return 1;
    }
}
