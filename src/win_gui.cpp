#include "vanity/search.hpp"
#include "vanity/ui_ids.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <commctrl.h>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <stdexcept>
#include <thread>
using namespace vanity;
namespace {
using namespace vanity::ui;
HWND window = nullptr, list = nullptr;
HFONT font = nullptr, titlefont = nullptr;
std::thread worker;
std::unique_ptr<Search> search;
std::atomic<bool> running{false};
std::string folder, statefile, output;
std::vector<Hit> shown;
std::string failure;
std::mutex failure_mutex;
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
    HWND h = GetDlgItem(window, id);
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return utf8(s);
}
void set(int id, const std::string &s) {
    SetWindowTextW(GetDlgItem(window, id), wide(s).c_str());
}
unsigned number(int id) {
    auto s = text(id);
    size_t p = 0;
    auto n = std::stoul(s, &p);
    if (p != s.size() || n > 100000)
        throw std::invalid_argument("Invalid numeric field");
    return unsigned(n);
}
bool checked(int id) {
    return SendDlgItemMessageW(window, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
void check(int id, bool b) {
    SendDlgItemMessageW(window, id, BM_SETCHECK, b ? BST_CHECKED : BST_UNCHECKED, 0);
}
HWND control(const wchar_t *cls, const wchar_t *title, int id, int x, int y, int w, int h,
             DWORD style = 0) {
    auto hnd =
        CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, title,
                        WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                        reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hnd;
}
void label(const wchar_t *s, int x, int y, int w) {
    control(L"STATIC", s, 0, x, y, w, 22);
}
void field(const wchar_t *name, const wchar_t *value, int id, int x, int y, int w) {
    label(name, x, y, w);
    control(L"EDIT", value, id, x, y + 24, w, 27, WS_TABSTOP | ES_AUTOHSCROLL);
    SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, 128, 0);
}
void buttons() {
    for (int id : {PHRASE, FILLERS, MINF, MAXF, MINS, MAXS, COMPRESSED, BEFORE, AFTER, POSITIONS,
                   GPU, LIMIT, START, RESUME, CLEAR})
        EnableWindow(GetDlgItem(window, id), !running);
    EnableWindow(GetDlgItem(window, STOP), running);
}
void apply(const Config &c) {
    set(PHRASE, c.phrase);
    set(FILLERS, c.fillers);
    set(BEFORE, c.before);
    set(AFTER, c.after);
    set(MINF, std::to_string(c.min_fill));
    set(MAXF, std::to_string(c.max_fill));
    set(MINS, std::to_string(c.min_suffix));
    set(MAXS, std::to_string(c.max_suffix));
    set(LIMIT, std::to_string(c.max_results));
    check(COMPRESSED, c.compressed);
    check(POSITIONS, c.all_positions);
    check(GPU, c.use_gpu);
}
Config config() {
    Config c;
    c.phrase = text(PHRASE);
    c.fillers = text(FILLERS);
    c.before = text(BEFORE);
    c.after = text(AFTER);
    c.min_fill = number(MINF);
    c.max_fill = number(MAXF);
    c.min_suffix = number(MINS);
    c.max_suffix = number(MAXS);
    c.max_results = number(LIMIT);
    c.compressed = checked(COMPRESSED);
    c.all_positions = checked(POSITIONS);
    c.use_gpu = checked(GPU);
    validate_config(c);
    return c;
}
void display(const std::vector<Hit> &hits) {
    // A virtual list requests only visible rows, keeping large result sets
    // from blocking STOP and window painting with thousands of insert messages.
    shown = hits;
    ListView_SetItemCountEx(list, int(shown.size()), 0);
    InvalidateRect(list, nullptr, TRUE);
}
void refresh() {
    if (!search)
        return;
    auto p = search->progress();
    std::ostringstream s;
    s << p.status << "  |  " << p.backend << "  |  " << p.results << " talalat\r\n"
      << "Vizsgalt jeloltek: " << p.tested << "  |  Sebesseg: " << std::fixed
      << std::setprecision(1) << p.rate << " jelolt/s  |  Ido: " << p.seconds
      << " s  |  ETA (teljes terv): ";
    if (p.eta < 0)
        s << "n/a";
    else
        s << p.eta << " s";
    s << "\r\nMintak: " << p.job_index << " / " << p.jobs << "  |  Kitolto: " << p.filler
      << "  |  Vegzodes: " << p.suffix << "  |  GPU kihasznaltsag: "
      << (p.utilization < 0 ? "n/a" : std::to_string(p.utilization) + "%") << "\r\n"
      << p.pattern;
    set(STATS, s.str());
    if (p.results != shown.size())
        display(search->checkpoint().hits);
    if (!running && worker.joinable()) {
        worker.join();
        buttons();
        display(search->checkpoint().hits);
        std::string e;
        {
            std::lock_guard<std::mutex> l(failure_mutex);
            e = failure;
            failure.clear();
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
    Checkpoint cp;
    if (resume) {
        cp = load_state(statefile);
        apply(cp.config);
        if (cp.hits.size() >= cp.config.max_results) {
            cp.config.max_results = std::min<uint64_t>(100000, cp.config.max_results + 100);
            set(LIMIT, std::to_string(cp.config.max_results));
        }
    } else {
        cp.config = config();
        if (std::filesystem::exists(std::filesystem::u8path(statefile)) &&
            MessageBoxW(window,
                        L"Az uj kereses lecsereli a mentett allapotot es az exportokat. Folytatod?",
                        L"Uj kereses", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        save_state(statefile, cp);
        export_hits(output, cp.hits);
    }
    display(cp.hits);
    search = std::make_unique<Search>();
    running = true;
    buttons();
    worker = std::thread([cp] {
        try {
            search->run(cp, statefile, output);
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> l(failure_mutex);
            failure = e.what();
        }
        running = false;
    });
}
void copy(bool key) {
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (row < 0 || size_t(row) >= shown.size())
        return;
    auto value = wide(key ? shown[row].key : shown[row].wif);
    if (!OpenClipboard(window))
        throw std::runtime_error("Cannot open clipboard");
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    if (!mem) {
        CloseClipboard();
        throw std::bad_alloc();
    }
    void *p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        CloseClipboard();
        throw std::runtime_error("Cannot lock clipboard memory");
    }
    memcpy(p, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
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
    titlefont = CreateFontW(-27, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
    auto title = control(L"STATIC", L"Vanity WIF  /  CUDA Search", 0, 24, 18, 900, 38);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titlefont), TRUE);
    control(
        L"STATIC",
        L"Kutatasi eszkoz: a kotott mintaju privat kulcsok kiszamithatok. Ne tarolj rajtuk penzt!",
        0, 24, 60, 1100, 24);
    control(L"STATIC", L"GPU felderitese...", DEVICE, 24, 90, 1100, 24);
    field(L"Keresett Base58 kifejezes", L"MysteryBansheePromotion", PHRASE, 24, 130, 470);
    field(L"Kitolto karakterek (prioritasi sorrend)", L"KLJMHGSA", FILLERS, 516, 130, 610);
    field(L"Min. kitoltes", L"0", MINF, 24, 200, 130);
    field(L"Max. kitoltes", L"40", MAXF, 174, 200, 130);
    field(L"Min. vegzodes", L"4", MINS, 324, 200, 130);
    field(L"Max. vegzodes (4..12)", L"6", MAXS, 474, 200, 170);
    field(L"Talalati korlat", L"100", LIMIT, 664, 200, 150);
    control(L"BUTTON", L"Compressed WIF", COMPRESSED, 840, 225, 280, 27,
            BS_AUTOCHECKBOX | WS_TABSTOP);
    field(L"Kifejezes eletti elvalaszto", L"1", BEFORE, 24, 270, 270);
    field(L"Kifejezes utani elvalaszto", L"1", AFTER, 314, 270, 270);
    control(L"BUTTON", L"Minden megvalosithato pozicio", POSITIONS, 614, 274, 440, 25,
            BS_AUTOCHECKBOX | WS_TABSTOP);
    control(L"BUTTON", L"CUDA GPU (ha elerheto)", GPU, 614, 303, 440, 25,
            BS_AUTOCHECKBOX | WS_TABSTOP);
    int x = 24;
    struct Button {
        const wchar_t *name;
        int id, w;
    };
    for (auto b : {Button{L"START", START, 100}, Button{L"STOP", STOP, 100},
                   Button{L"FOLYTATAS", RESUME, 130}, Button{L"Talalatok torlese", CLEAR, 160},
                   Button{L"WIF masolasa", COPY, 150}, Button{L"HEX masolasa", COPYHEX, 150},
                   Button{L"Mentesek", OPENFOLDER, 130}}) {
        control(L"BUTTON", b.name, b.id, x, 352, b.w, 34, WS_TABSTOP | BS_PUSHBUTTON);
        x += b.w + 12;
    }
    control(L"STATIC",
            L"Keszen all. Eloszor a legtobb ismetlodo karaktert tartalmazo mintak kovetkeznek.",
            STATS, 24, 410, 1102, 105);
    list = control(WC_LISTVIEWW, L"", LIST, 24, 530, 1102, 230,
                   LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                LVS_EX_GRIDLINES);
    const wchar_t *names[] = {L"WIF", L"HEX privat kulcs", L"Tipus", L"Ido (UTC)"};
    int widths[] = {445, 495, 110, 185};
    for (int i = 0; i < 4; i++) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t *>(names[i]);
        col.cx = widths[i];
        ListView_InsertColumn(list, i, &col);
    }
    apply(Config{});
    buttons();
    SetTimer(window, 1, 250, nullptr);
    // Probe CUDA off the GUI thread; a CUDA driver can take time to initialize.
    worker = std::thread([] {
        auto d = gpu_device();
        auto s = new std::wstring(wide(d.name));
        PostMessageW(window, WM_APP + 1, 0, reinterpret_cast<LPARAM>(s));
    });
    if (std::filesystem::exists(std::filesystem::u8path(statefile))) {
        try {
            auto saved = load_state(statefile);
            apply(saved.config);
            display(saved.hits);
            set(STATS, "Mentett kereses talalhato. A FOLYTATAS a kovetkezo jelolttol indul.");
        } catch (const std::exception &e) {
            set(STATS, std::string("Hibas mentett allapot: ") + e.what());
        }
    }
}
LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    try {
        switch (msg) {
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(w), RGB(255, 255, 255));
            SetTextColor(reinterpret_cast<HDC>(w), RGB(28, 38, 52));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        case WM_CREATE:
            window = h;
            create();
            return 0;
        case WM_APP + 1: {
            std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring *>(l));
            SetWindowTextW(GetDlgItem(h, DEVICE), s->c_str());
            return 0;
        }
        case WM_NOTIFY: {
            auto header = reinterpret_cast<NMHDR *>(l);
            if (header->hwndFrom == list && header->code == LVN_GETDISPINFOW) {
                auto info = reinterpret_cast<NMLVDISPINFOW *>(l);
                auto row = info->item.iItem;
                if ((info->item.mask & LVIF_TEXT) && row >= 0 && size_t(row) < shown.size()) {
                    const auto &hit = shown[size_t(row)];
                    std::wstring value;
                    switch (info->item.iSubItem) {
                    case 0:
                        value = wide(hit.wif);
                        break;
                    case 1:
                        value = wide(hit.key);
                        break;
                    case 2:
                        value = hit.compressed ? L"compressed" : L"uncompressed";
                        break;
                    case 3:
                        value = wide(hit.time);
                        break;
                    }
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
            case RESUME:
                begin(true);
                break;
            case STOP:
                if (search)
                    search->stop();
                break;
            case COPY:
                copy(false);
                break;
            case COPYHEX:
                copy(true);
                break;
            case OPENFOLDER:
                ShellExecuteW(h, L"open", wide(folder).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case CLEAR:
                if (!running &&
                    MessageBoxW(h,
                                L"Toroljem a talalatokat a mentett allapotbol es az exportokbol?",
                                L"Torles", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (worker.joinable())
                        worker.join();
                    auto cp = std::filesystem::exists(std::filesystem::u8path(statefile))
                                  ? load_state(statefile)
                                  : Checkpoint{};
                    cp.hits.clear();
                    save_state(statefile, cp);
                    export_hits(output, cp.hits);
                    search.reset();
                    ListView_SetItemCountEx(list, 0, 0);
                    shown.clear();
                    set(STATS, "Talalatok torolve; a keresesi pozicio megmaradt.");
                }
                break;
            }
            return 0;
        case WM_SIZE:
            if (list) {
                RECT r;
                GetClientRect(h, &r);
                MoveWindow(list, 24, 530, std::max(200L, r.right - 48),
                           std::max(80L, r.bottom - 554), TRUE);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto p = reinterpret_cast<MINMAXINFO *>(l);
            p->ptMinTrackSize = {1170, 720};
            return 0;
        }
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
            DeleteObject(titlefont);
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception &e) {
        MessageBoxW(h, wide(e.what()).c_str(), L"VanityWIF", MB_ICONERROR);
    }
    return DefWindowProcW(h, msg, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        SetProcessDPIAware();
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path)))
            throw std::runtime_error("LocalAppData unavailable");
        folder = utf8(path) + "/VanityWIF";
        CoTaskMemFree(path);
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
            throw std::runtime_error("Cannot read command line");
        if (argc == 3 && std::wstring(argv[1]) == L"--data-dir")
            folder = utf8(argv[2]);
        else if (argc != 1) {
            LocalFree(argv);
            throw std::invalid_argument("Usage: VanityWIF.exe [--data-dir PATH]");
        }
        LocalFree(argv);
        std::filesystem::create_directories(std::filesystem::u8path(folder));
        HANDLE lock =
            CreateFileW(wide(folder + "/session.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE)
            throw std::runtime_error(
                "Another instance uses this data directory, or it is not writable");
        struct Lock {
            HANDLE h;
            ~Lock() {
                CloseHandle(h);
            }
        } file_lock{lock};
        statefile = folder + "/search.state";
        output = folder + "/results";
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&init);
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.hInstance = instance;
        cls.lpfnWndProc = proc;
        cls.lpszClassName = L"VanityWIF.Main";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&cls);
        auto h = CreateWindowExW(0, cls.lpszClassName, L"VanityWIF — Bitcoin WIF Pattern Search",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 850,
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
        MessageBoxA(nullptr, e.what(), "VanityWIF", MB_ICONERROR);
        return 1;
    }
}
