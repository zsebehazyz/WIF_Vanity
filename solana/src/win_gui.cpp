#include "solvan/core.hpp"
#include "solvan/ui_ids.hpp"
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
using namespace solvan;
using namespace solvan::ui;
namespace {
HWND window = nullptr, list = nullptr;
HFONT font = nullptr, title_font = nullptr;
std::thread worker;
std::unique_ptr<Search> search;
std::atomic<bool> running{false};
std::string directory, statefile, error;
std::mutex error_mutex;
std::vector<Hit> shown;
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::string text(int id) {
    auto h = GetDlgItem(window, id);
    int n = GetWindowTextLengthW(h);
    std::wstring out(n + 1, 0);
    GetWindowTextW(h, out.data(), n + 1);
    out.resize(n);
    return utf8(out);
}
void set(int id, const std::string &s) {
    SetWindowTextW(GetDlgItem(window, id), wide(s).c_str());
}
unsigned number(int id) {
    auto s = text(id);
    size_t p = 0;
    auto v = std::stoul(s, &p);
    if (p != s.size() || v > 1000000)
        throw std::invalid_argument("Invalid numeric field");
    return unsigned(v);
}
bool checked(int id) {
    return SendDlgItemMessageW(window, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
HWND control(const wchar_t *cls, const wchar_t *label, int id, int x, int y, int w, int h,
             DWORD style = 0) {
    auto handle =
        CreateWindowExW(std::wstring(cls) == L"EDIT" ? WS_EX_CLIENTEDGE : 0, cls, label,
                        WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                        reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return handle;
}
void field(const wchar_t *label, const wchar_t *value, int id, int x, int y, int width) {
    control(L"STATIC", label, 0, x, y, width, 22);
    control(L"EDIT", value, id, x, y + 25, width, 29, ES_AUTOHSCROLL | WS_TABSTOP);
    SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, id == PREFIX ? 12 : 6, 0);
}
void buttons() {
    for (int id : {PREFIX, LIMIT, THREADS, BATCH, GPU, START, RESUME, CLEAR})
        EnableWindow(GetDlgItem(window, id), !running);
    EnableWindow(GetDlgItem(window, STOP), running);
}
void display(const std::vector<Hit> &hits) {
    shown = hits;
    ListView_SetItemCountEx(list, int(shown.size()), 0);
    InvalidateRect(list, nullptr, TRUE);
}
void apply(const Config &c) {
    set(PREFIX, c.prefix);
    set(LIMIT, std::to_string(c.limit));
    set(THREADS, std::to_string(c.threads));
    set(BATCH, std::to_string(c.batch));
    SendDlgItemMessageW(window, GPU, BM_SETCHECK, c.gpu ? BST_CHECKED : BST_UNCHECKED, 0);
}
Config config() {
    Config c;
    c.prefix = text(PREFIX);
    c.limit = number(LIMIT);
    c.threads = number(THREADS);
    c.batch = number(BATCH);
    c.gpu = checked(GPU);
    validate(c);
    return c;
}
void refresh() {
    if (!search)
        return;
    auto p = search->progress();
    std::ostringstream s;
    s << p.status << "  |  " << p.backend << "  |  " << p.hits
      << " talalat\r\nJeloltek: " << p.tested << "  |  Sebesseg: " << std::fixed
      << std::setprecision(1) << p.rate << " kulcs/s  |  Eltelt: " << p.seconds
      << " s\r\nAtlagos talalati ido (becsles, nem visszaszamlalas): ";
    if (p.mean_eta < 0)
        s << "n/a";
    else if (p.mean_eta > 1e7)
        s << std::scientific << p.mean_eta << " s";
    else
        s << p.mean_eta << " s";
    s << "\r\n" << p.gpu;
    set(STATS, s.str());
    if (p.hits != shown.size()) {
        auto state = search->snapshot();
        display(state.hits);
        wipe(state.master.data(), 32);
    }
    if (!running && worker.joinable()) {
        worker.join();
        buttons();
        std::string e;
        {
            std::lock_guard<std::mutex> lock(error_mutex);
            e = error;
            error.clear();
        }
        if (!e.empty())
            MessageBoxW(window, wide(e).c_str(), L"Keresesi hiba", MB_ICONERROR);
    }
}
void begin(bool resume) {
    if (running)
        return;
    if (worker.joinable())
        worker.join();
    State state;
    if (resume) {
        auto requested = number(LIMIT);
        state = load_state(statefile);
        state.config.limit = requested > state.hits.size()
                                 ? requested
                                 : std::min<uint64_t>(10000, state.hits.size() + 1);
        apply(state.config);
    } else {
        auto c = config();
        if (std::filesystem::exists(std::filesystem::u8path(statefile)) &&
            MessageBoxW(window,
                        L"Uj kereses indul uj veletlen mesterkulccsal. Az elozo folytatasi "
                        L"allapotot lecsereli; a mar mentett kulcsfajlok megmaradnak. Folytatod?",
                        L"Uj kereses", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        state = new_state(c);
    }
    display(state.hits);
    search = std::make_unique<Search>();
    running = true;
    buttons();
    worker = std::thread([state]() mutable {
        try {
            search->run(state, directory);
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lock(error_mutex);
            error = e.what();
        }
        wipe(state.master.data(), 32);
        running = false;
    });
    wipe(state.master.data(), 32);
}
void copy_address() {
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (row < 0 || size_t(row) >= shown.size())
        return;
    auto s = wide(shown[row].address);
    if (!OpenClipboard(window))
        throw std::runtime_error("Clipboard unavailable");
    auto mem = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (!mem) {
        CloseClipboard();
        throw std::bad_alloc();
    }
    auto p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        CloseClipboard();
        throw std::runtime_error("Clipboard memory unavailable");
    }
    memcpy(p, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(mem);
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem))
        GlobalFree(mem);
    CloseClipboard();
}
void create() {
    font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                       L"Segoe UI");
    title_font = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    auto title = control(L"STATIC", L"SolanaVanity  /  CUDA Prefix Search", 0, 24, 20, 960, 40);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
    control(L"STATIC",
            L"Ed25519  •  Kis-/nagybetuerzekeny Base58 prefix  •  Offline kulcsgeneralas", 0, 24,
            70, 960, 25);
    control(L"STATIC",
            L"A keys/*.json fajlok privat kulcsot tartalmaznak. Tartsd oket titokban, es keszits "
            L"biztonsagi masolatot.",
            0, 24, 104, 960, 25);
    control(L"STATIC", L"GPU felderitese...", DEVICE, 24, 137, 960, 25);
    field(L"Keresett prefix", L"Ab", PREFIX, 24, 185, 400);
    field(L"Talalati korlat", L"1", LIMIT, 450, 185, 150);
    field(L"CPU szalak (0=auto)", L"0", THREADS, 624, 185, 170);
    field(L"GPU batch meret", L"4096", BATCH, 818, 185, 170);
    control(L"BUTTON", L"CUDA GPU hasznalata, ha elerheto (egyebkent CPU fallback)", GPU, 24, 260,
            850, 27, BS_AUTOCHECKBOX | WS_TABSTOP);
    struct Button {
        const wchar_t *label;
        int id, width;
    };
    int x = 24;
    for (auto b : {Button{L"START", START, 120}, Button{L"STOP", STOP, 120},
                   Button{L"FOLYTATAS", RESUME, 150}, Button{L"Cim masolasa", COPY, 150},
                   Button{L"Kulcsfajlok", OPENFOLDER, 150}, Button{L"Lista torlese", CLEAR, 150}}) {
        control(L"BUTTON", b.label, b.id, x, 311, b.width, 36, BS_PUSHBUTTON | WS_TABSTOP);
        x += b.width + 12;
    }
    control(L"STATIC", L"Keszen all. Tiltott Base58 karakterek: 0, O, I, l, szokoz.", STATS, 24,
            374, 964, 112);
    list = control(WC_LISTVIEWW, L"", LIST, 24, 500, 964, 230,
                   LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                LVS_EX_GRIDLINES);
    const wchar_t *names[] = {L"Solana cim", L"Ido (UTC)", L"Kulcsfajl (keys/)"};
    int widths[] = {410, 190, 425};
    for (int i = 0; i < 3; i++) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t *>(names[i]);
        col.cx = widths[i];
        ListView_InsertColumn(list, i, &col);
    }
    apply(Config{});
    buttons();
    SetTimer(window, 1, 250, nullptr);
    worker = std::thread([] {
        auto d = gpu_device();
        auto s = new std::wstring(wide(d.name));
        if (!PostMessageW(window, WM_APP + 1, 0, reinterpret_cast<LPARAM>(s)))
            delete s;
    });
    if (std::filesystem::exists(std::filesystem::u8path(statefile))) {
        try {
            auto s = load_state(statefile);
            apply(s.config);
            display(s.hits);
            wipe(s.master.data(), 32);
            set(STATS, "Mentett kereses talalhato. A FOLYTATAS ugyanazzal a mesterkulccsal, a "
                       "kovetkezo jelolttol indul.");
        } catch (const std::exception &e) {
            set(STATS, std::string("A mentett allapot nem olvashato: ") + e.what());
        }
    }
}
LRESULT CALLBACK proc(HWND h, UINT message, WPARAM w, LPARAM l) {
    try {
        switch (message) {
        case WM_CREATE:
            window = h;
            create();
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(w), RGB(255, 255, 255));
            SetTextColor(reinterpret_cast<HDC>(w), RGB(25, 37, 55));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        case WM_APP + 1: {
            std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring *>(l));
            SetWindowTextW(GetDlgItem(h, DEVICE), s->c_str());
            return 0;
        }
        case WM_NOTIFY: {
            auto header = reinterpret_cast<NMHDR *>(l);
            if (header->hwndFrom == list && header->code == LVN_GETDISPINFOW) {
                auto info = reinterpret_cast<NMLVDISPINFOW *>(l);
                int row = info->item.iItem;
                if ((info->item.mask & LVIF_TEXT) && row >= 0 && size_t(row) < shown.size()) {
                    auto &hit = shown[size_t(row)];
                    auto value = wide(info->item.iSubItem == 0   ? hit.address
                                      : info->item.iSubItem == 1 ? hit.time
                                                                 : hit.file);
                    if (info->item.pszText && info->item.cchTextMax > 0)
                        lstrcpynW(info->item.pszText, value.c_str(), info->item.cchTextMax);
                }
                return 0;
            }
            break;
        }
        case WM_TIMER:
            refresh();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(w)) {
            case START:
                begin(false);
                break;
            case STOP:
                if (search)
                    search->stop();
                break;
            case RESUME:
                begin(true);
                break;
            case COPY:
                copy_address();
                break;
            case OPENFOLDER:
                ShellExecuteW(h, L"open", wide(directory).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case CLEAR:
                if (!running &&
                    MessageBoxW(h,
                                L"Toroljem a talalati listat a folytatasi allapotbol? A mar "
                                L"mentett JSON kulcsfajlok MEGMARADNAK.",
                                L"Lista torlese", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (worker.joinable())
                        worker.join();
                    if (std::filesystem::exists(std::filesystem::u8path(statefile))) {
                        auto s = load_state(statefile);
                        s.hits.clear();
                        save_state(statefile, s);
                        wipe(s.master.data(), 32);
                    }
                    search.reset();
                    display({});
                    set(STATS, "Lista torolve. A kulcsfajlok es a keresesi pozicio megmaradtak.");
                }
                break;
            }
            return 0;
        case WM_SIZE:
            if (list) {
                RECT r;
                GetClientRect(h, &r);
                MoveWindow(list, 24, 500, std::max(100L, r.right - 48),
                           std::max(80L, r.bottom - 524), TRUE);
            }
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {1040, 680};
            return 0;
        case WM_CLOSE:
            if (search)
                search->stop();
            if (worker.joinable())
                worker.join();
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            KillTimer(h, 1);
            DeleteObject(font);
            DeleteObject(title_font);
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception &e) {
        MessageBoxW(h, wide(e.what()).c_str(), L"SolanaVanity", MB_ICONERROR);
    }
    return DefWindowProcW(h, message, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        SetProcessDPIAware();
        PWSTR local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)))
            throw std::runtime_error("LocalAppData unavailable");
        directory = utf8(local) + "/SolanaVanity";
        CoTaskMemFree(local);
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
            throw std::runtime_error("Cannot read command line");
        if (argc == 3 && std::wstring(argv[1]) == L"--data-dir")
            directory = utf8(argv[2]);
        else if (argc != 1) {
            LocalFree(argv);
            throw std::invalid_argument("Usage: SolanaVanity.exe [--data-dir PATH]");
        }
        LocalFree(argv);
        std::filesystem::create_directories(std::filesystem::u8path(directory));
        statefile = directory + "/search.state";
        auto lock =
            CreateFileW(wide(directory + "/session.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE)
            throw std::runtime_error(
                "Output directory is locked by another instance or is not writable");
        struct Lock {
            HANDLE h;
            ~Lock() {
                CloseHandle(h);
            }
        } file_lock{lock};
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&init);
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.hInstance = instance;
        cls.lpfnWndProc = proc;
        cls.lpszClassName = L"SolanaVanity.Main";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&cls);
        auto h = CreateWindowExW(0, cls.lpszClassName, L"SolanaVanity — CUDA Prefix Generator",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1050, 820,
                                 nullptr, nullptr, instance, nullptr);
        if (!h)
            throw std::runtime_error("Cannot create window");
        ShowWindow(h, show);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(h, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        return int(msg.wParam);
    } catch (const std::exception &e) {
        MessageBoxA(nullptr, e.what(), "SolanaVanity", MB_ICONERROR);
        return 1;
    }
}
