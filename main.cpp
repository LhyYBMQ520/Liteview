#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <wrl.h>
#include <algorithm>
#include <string>
#include <vector>
#include "WebView2.h"
#include "resource.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kClass[] = L"Liteview.Window";
constexpr int kBack = 101, kForward = 102, kReload = 103, kHome = 104;
constexpr int kAddress = 105, kGo = 106, kFocusAddress = 107, kStop = 108;
constexpr int kTabs = 109, kNewTab = 110, kCloseTab = 111, kNextTab = 112, kPreviousTab = 113;
constexpr int kSelectTab = 120;
constexpr int kMinimize = 130, kMaximize = 131, kExit = 132;
constexpr int kMenu = 140, kNewWindow = 141, kZoomOut = 142, kZoomReset = 143;
constexpr int kZoomIn = 144, kFullscreen = 145, kHistory = 146, kExitAll = 147, kMenuTab = 148;
constexpr wchar_t kMenuClass[] = L"Liteview.Menu";
constexpr UINT kDispatchKey = WM_APP + 1;
constexpr UINT kCloseTabById = WM_APP + 2;
constexpr COLORREF kSurface = RGB(246, 247, 249), kInk = RGB(40, 48, 59);
struct Browser;
std::vector<Browser*> browsers;
Browser* CreateBrowserWindow(int show, Browser* source = nullptr);
LRESULT CALLBACK MenuProc(HWND, UINT, WPARAM, LPARAM);
// Local content keeps startup instant and makes no network request.
constexpr wchar_t kHomePage[] = LR"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Liteview</title><style>
*{box-sizing:border-box}body{margin:0;background:#fafbfc;color:#26313e;font:15px/1.6 system-ui,'Segoe UI',sans-serif;min-height:100vh;display:grid;place-items:center}
main{width:min(440px,85vw);padding:40px 0}.mark{display:block;width:64px;height:52px;margin-bottom:24px}
h1{font-size:36px;letter-spacing:-1.4px;font-weight:600;margin:0 0 8px}p{color:#637080;margin:0 0 28px}hr{border:0;border-top:1px solid #e6e9ed;margin:28px 0}.row{display:flex;justify-content:space-between;margin:12px 0;color:#637080;font-size:13px}kbd{font:12px system-ui;border:1px solid #dce1e7;border-radius:5px;background:white;padding:2px 7px;color:#44505f}footer{color:#8b94a0;font-size:12px}
</style><main>)HTML"
#include "assets/home-icon.inc"
LR"HTML(<h1>Liteview</h1><p>从地址栏开始。输入网址，或搜索你想知道的事。</p><div class="row"><span>输入网址或搜索</span><span><kbd>Ctrl</kbd> + <kbd>L</kbd></span></div><div class="row"><span>后退 / 前进</span><span><kbd>Alt</kbd> + <kbd>←</kbd> / <kbd>→</kbd></span></div><div class="row"><span>刷新页面</span><kbd>F5</kbd></div><hr><footer>轻量化绿色版浏览器 · 数据保存在程序旁的 Data 文件夹</footer></main></html>)HTML";

struct Tab {
    int id{};
    bool loading = false, homePending = false, historyPage = false;
    std::wstring title = L"新标签页", addressText, historyKey;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    ComPtr<ICoreWebView2NewWindowRequestedEventArgs> popup;
    ComPtr<ICoreWebView2Deferral> deferral;
};

struct Browser {
    HWND window{}, address{}, back{}, forward{}, reload{}, home{}, go{}, tooltip{}, tabStrip{}, newTab{};
    HWND minimize{}, maximize{}, exit{}, hotButton{};
    HWND menuButton{}, menuPopup{}, zoomLabel{};
    bool fullscreen = false;
    DWORD restoreStyle{};
    WINDOWPLACEMENT restorePlacement{sizeof(WINDOWPLACEMENT)};
    int hotTabId = 0;
    bool hotTabClose = false;
    HFONT font{};
    HBRUSH surface{};
    UINT dpi = 96;
    RECT addressFrame{};
    bool loading = false, ready = false, closing = false;
    int activeId = 0, nextId = 0;
    std::vector<Tab> tabs;
    std::wstring dataPath;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;

    void ShowMenu();
    void CloseMenu();
    void RefreshZoom();
    void ChangeZoom(int direction);
    void ToggleFullscreen();
    void RecordVisit(int id);
    std::wstring HistoryPage(const std::wstring& key = L"") const;

    int Px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }

    void UpdateTabHover(POINT point) {
        TCHITTESTINFO hit{}; hit.pt = point;
        const int index = TabCtrl_HitTest(tabStrip, &hit);
        int id = 0; bool close = false;
        if (index >= 0 && index < static_cast<int>(tabs.size())) {
            RECT rect{}; TabCtrl_GetItemRect(tabStrip, index, &rect);
            id = tabs[index].id; close = point.x >= rect.right - Px(24);
        }
        if (id != hotTabId || close != hotTabClose) {
            hotTabId = id; hotTabClose = close;
            InvalidateRect(tabStrip, nullptr, FALSE);
        }
    }

    int ResizeBorder(int metric) const {
        return GetSystemMetricsForDpi(metric, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    }

    void ExtendFrame() const {
        const MARGINS margins{0, 0, 1, 0};
        DwmExtendFrameIntoClientArea(window, &margins);
    }

    int CaptionButtonsLeft() const {
        RECT client{}; GetClientRect(window, &client);
        return client.right - Px(138);
    }

    LRESULT HitTest(POINT screen) const {
        if (fullscreen) return HTCLIENT;
        RECT outer{}; GetWindowRect(window, &outer);
        if (!IsZoomed(window)) {
            const int x = ResizeBorder(SM_CXSIZEFRAME), y = ResizeBorder(SM_CYSIZEFRAME);
            const bool left = screen.x < outer.left + x, right = screen.x >= outer.right - x;
            const bool top = screen.y < outer.top + y, bottom = screen.y >= outer.bottom - y;
            if (top) return left ? HTTOPLEFT : (right ? HTTOPRIGHT : HTTOP);
            if (bottom) return left ? HTBOTTOMLEFT : (right ? HTBOTTOMRIGHT : HTBOTTOM);
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
        }
        POINT client = screen; ScreenToClient(window, &client);
        if (client.y < Px(36)) {
            RECT rect{};
            for (HWND control : {tabStrip, newTab, minimize, maximize, exit}) {
                GetWindowRect(control, &rect);
                if (PtInRect(&rect, screen)) return HTCLIENT;
            }
            return client.x < CaptionButtonsLeft() ? HTCAPTION : HTCLIENT;
        }
        return HTCLIENT;
    }

    void Error(const wchar_t* text, HRESULT hr) const {
        wchar_t detail[640]{};
        swprintf_s(detail, L"%s\n\n错误代码：0x%08lX", text, static_cast<unsigned long>(hr));
        MessageBoxW(window, detail, L"Liteview", MB_OK | MB_ICONERROR);
    }

    void FocusAddress() const {
        SetFocus(address);
        SendMessageW(address, EM_SETSEL, 0, -1);
    }

    void UpdateHistory() const {
        BOOL canBack = FALSE, canForward = FALSE;
        if (web) { web->get_CanGoBack(&canBack); web->get_CanGoForward(&canForward); }
        EnableWindow(back, canBack);
        EnableWindow(forward, canForward);
    }

    Tab* FindTab(int id) {
        if (!closing) for (auto& tab : tabs) if (tab.id == id) return &tab;
        return nullptr;
    }

    void SaveAddress() {
        if (auto* tab = FindTab(activeId)) {
            const int size = GetWindowTextLengthW(address);
            tab->addressText.resize(static_cast<size_t>(size) + 1);
            GetWindowTextW(address, tab->addressText.data(), size + 1);
            tab->addressText.resize(size);
        }
    }

    void UpdateSource(int id) {
        auto* tab = FindTab(id);
        if (!tab || !tab->web) return;
        LPWSTR source = nullptr;
        if (SUCCEEDED(tab->web->get_Source(&source)) && source) {
            tab->addressText = wcscmp(source, L"about:blank") == 0 ? L"" : source;
            if (tab->historyPage && tab->addressText.starts_with(L"data:text/html;charset=utf-8;base64,")) tab->addressText.clear();
            if (id == activeId) SetWindowTextW(address, tab->addressText.c_str());
        }
        CoTaskMemFree(source);
    }

    void UpdateTitle(int id) {
        auto* tab = FindTab(id);
        if (!tab || !tab->web) return;
        LPWSTR title = nullptr;
        if (SUCCEEDED(tab->web->get_DocumentTitle(&title))) {
            tab->title = title && *title && wcscmp(title, L"Liteview") != 0 ? title : L"新标签页";
            const int index = static_cast<int>(tab - tabs.data());
            TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = tab->title.data();
            TabCtrl_SetItem(tabStrip, index, &item);
        }
        CoTaskMemFree(title);
        if (id == activeId) SetWindowTextW(window,
            tab->title == L"新标签页" ? L"Liteview" : (tab->title + L" — Liteview").c_str());
    }

    void ActivateTab(int index, bool focus = true) {
        if (index < 0 || index >= static_cast<int>(tabs.size())) return;
        SaveAddress();
        activeId = tabs[index].id;
        controller = tabs[index].controller; web = tabs[index].web;
        loading = tabs[index].loading; ready = web != nullptr;
        TabCtrl_SetCurSel(tabStrip, index);
        for (auto& tab : tabs) if (tab.controller)
            tab.controller->put_IsVisible(tab.id == activeId && !IsIconic(window));
        EnableWindow(address, ready); EnableWindow(go, ready);
        EnableWindow(home, ready); EnableWindow(reload, ready);
        SetWindowTextW(address, tabs[index].addressText.c_str());
        if (!ready) SetWindowTextW(window, L"新标签页 — Liteview");
        UpdateHistory(); UpdateTitle(activeId); Layout();
        RefreshZoom();
        InvalidateRect(tabStrip, nullptr, FALSE); InvalidateRect(reload, nullptr, FALSE);
        if (focus) {
            if (controller) controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            else SetFocus(window);
        }
    }

    void CycleTab(int direction) {
        if (tabs.empty()) return;
        const int index = TabCtrl_GetCurSel(tabStrip), count = static_cast<int>(tabs.size());
        ActivateTab((index + direction + count) % count);
    }

    static void FinishPopup(Tab& tab, ICoreWebView2* target = nullptr) {
        auto popup = std::move(tab.popup);
        auto deferral = std::move(tab.deferral);
        if (popup && target) popup->put_NewWindow(target);
        if (deferral) deferral->Complete();
    }

    void CloseTab(int id) {
        auto* tab = FindTab(id);
        if (!tab) return;
        if (tabs.size() == 1) { PostMessageW(window, WM_CLOSE, 0, 0); return; }
        const int index = static_cast<int>(tab - tabs.data());
        const bool active = id == activeId;
        if (active) { web.Reset(); controller.Reset(); }
        FinishPopup(*tab);
        if (tab->controller) tab->controller->Close();
        tabs.erase(tabs.begin() + index);
        TabCtrl_DeleteItem(tabStrip, index);
        if (active) ActivateTab(std::min(index, static_cast<int>(tabs.size()) - 1));
        else { Layout(); TabCtrl_SetCurSel(tabStrip, IndexOf(activeId)); }
    }

    int IndexOf(int id) const {
        for (size_t i = 0; i < tabs.size(); ++i) if (tabs[i].id == id) return static_cast<int>(i);
        return -1;
    }

    void Shutdown() {
        CloseMenu();
        closing = true; web.Reset(); controller.Reset();
        for (auto& tab : tabs) {
            FinishPopup(tab);
            if (tab.controller) tab.controller->Close();
        }
        tabs.clear(); environment.Reset();
    }

    void Layout() {
        RECT client{};
        GetClientRect(window, &client);
        if (fullscreen) {
            if (controller) controller->put_Bounds(client);
            return;
        }
        const int pad = Px(12), gap = Px(6), size = Px(34), top = Px(47);
        const HWND captionButtons[] = {minimize, maximize, exit};
        int captionX = CaptionButtonsLeft();
        for (HWND button : captionButtons) {
            MoveWindow(button, captionX, 0, Px(46), Px(36), TRUE); captionX += Px(46);
        }
        // Leave room for the adjacent +, a drag region, and the native caption buttons.
        const int available = std::max(Px(110), CaptionButtonsLeft() - Px(90));
        const int count = std::max(1, static_cast<int>(tabs.size()));
        const int width = std::clamp((available - Px(8)) / count, Px(110), Px(210));
        const int stripWidth = std::min(available, count * width + Px(8));
        MoveWindow(tabStrip, Px(8), Px(3), stripWidth, Px(34), TRUE);
        TabCtrl_SetItemSize(tabStrip, width, Px(29));
        MoveWindow(newTab, Px(8) + stripWidth + Px(4), Px(3), Px(30), Px(30), TRUE);
        InvalidateRect(newTab, nullptr, FALSE);
        POINT cursor{}; GetCursorPos(&cursor);
        if (WindowFromPoint(cursor) == tabStrip) { ScreenToClient(tabStrip, &cursor); UpdateTabHover(cursor); }
        else UpdateTabHover({-1, -1});
        const HWND buttons[] = {back, forward, reload, home};
        int x = pad;
        for (HWND button : buttons) { MoveWindow(button, x, top, size, size, TRUE); x += size + gap; }
        addressFrame = {x + Px(4), top, client.right - pad - size - gap, top + size};
        MoveWindow(address, addressFrame.left + Px(11), top + Px(7),
            std::max<LONG>(1, addressFrame.right - addressFrame.left - Px(48)), Px(22), TRUE);
        MoveWindow(go, addressFrame.right - Px(31), top + Px(3), Px(28), Px(28), TRUE);
        MoveWindow(menuButton, client.right - pad - size, top, size, size, TRUE);
        if (controller) {
            RECT bounds{0, Px(93), client.right, std::max<LONG>(Px(93), client.bottom)};
            controller->put_Bounds(bounds);
        }
        InvalidateRect(window, nullptr, FALSE);
    }

    void SetFont() {
        HFONT old = font;
        font = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SendMessageW(address, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(tabStrip, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (old) DeleteObject(old);
    }

    void Home() {
        if (web) {
            auto* tab = FindTab(activeId);
            tab->historyPage = false;
            tab->homePending = true;
            const HRESULT hr = web->NavigateToString(kHomePage);
            if (FAILED(hr)) { tab->homePending = false; Error(L"无法打开起始页。", hr); }
        }
    }

    static std::wstring SearchUrl(const std::wstring& text) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
        std::wstring result = L"https://www.bing.com/search?q=";
        constexpr wchar_t hex[] = L"0123456789ABCDEF";
        for (const unsigned char c : utf8) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += c;
            else { result += L'%'; result += hex[c >> 4]; result += hex[c & 15]; }
        }
        return result;
    }

    static std::wstring ResolveAddress(std::wstring text) {
        const size_t start = text.find_first_not_of(L" \t\r\n");
        if (start == std::wstring::npos) return {};
        text = text.substr(start, text.find_last_not_of(L" \t\r\n") - start + 1);
        if (text.size() > 1 && text.front() == L'"' && text.back() == L'"') text = text.substr(1, text.size() - 2);
        if (_wcsnicmp(text.c_str(), L"http://", 7) == 0 || _wcsnicmp(text.c_str(), L"https://", 8) == 0 ||
            _wcsnicmp(text.c_str(), L"file://", 7) == 0 || _wcsnicmp(text.c_str(), L"about:", 6) == 0) return text;
        if ((text.size() > 2 && text[1] == L':' && (text[2] == L'\\' || text[2] == L'/')) || text.starts_with(L"\\\\")) {
            wchar_t fileUrl[32768]{};
            DWORD count = ARRAYSIZE(fileUrl);
            if (SUCCEEDED(UrlCreateFromPathW(text.c_str(), fileUrl, &count, 0))) return fileUrl;
        }
        const std::wstring host = text.substr(0, text.find_first_of(L"/?#"));
        const bool local = _wcsicmp(host.c_str(), L"localhost") == 0 || _wcsnicmp(host.c_str(), L"localhost:", 10) == 0 ||
            host.starts_with(L"127.") || host.starts_with(L"[::1]");
        if (text.find_first_of(L" \t\r\n") == std::wstring::npos &&
            (local || host.find(L'.') != std::wstring::npos || host.starts_with(L"["))) {
            return (local ? L"http://" : L"https://") + text;
        }
        return SearchUrl(text);
    }

    void Go() {
        if (!web) return;
        const int length = GetWindowTextLengthW(address);
        std::wstring input(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(address, input.data(), length + 1);
        input.resize(length);
        const auto url = ResolveAddress(input);
        if (url.empty()) { Home(); return; }
        const HRESULT hr = web->Navigate(url.c_str());
        if (FAILED(hr)) { Error(L"无法打开这个地址，请检查网址。", hr); return; }
        controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }

    void Command(int id) {
        if (id == kMenu) { ShowMenu(); return; }
        if (id == kMenuTab) { AddTab(); return; }
        if (id == kNewWindow) { CreateBrowserWindow(SW_SHOWNORMAL, this); return; }
        if (id == kZoomOut || id == kZoomIn || id == kZoomReset) {
            ChangeZoom(id == kZoomReset ? 0 : (id == kZoomOut ? -1 : 1)); return;
        }
        if (id == kFullscreen) { ToggleFullscreen(); return; }
        if (id == kHistory) { AddTab(nullptr, true); return; }
        if (id == kExitAll) {
            if (browsers.empty()) PostMessageW(window, WM_CLOSE, 0, 0);
            else for (auto* browser : browsers) if (!browser->closing) PostMessageW(browser->window, WM_CLOSE, 0, 0);
            return;
        }
        if (id == kMinimize) { ShowWindow(window, SW_MINIMIZE); return; }
        if (id == kMaximize) { ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE); return; }
        if (id == kExit) { SendMessageW(window, WM_CLOSE, 0, 0); return; }
        if (id == kFocusAddress) { FocusAddress(); return; }
        if (id == kNewTab) { AddTab(); return; }
        if (id == kCloseTab) { CloseTab(activeId); return; }
        if (id == kNextTab || id == kPreviousTab) { CycleTab(id == kNextTab ? 1 : -1); return; }
        if (id >= kSelectTab && id < kSelectTab + 9) {
            ActivateTab(id == kSelectTab + 8 ? static_cast<int>(tabs.size()) - 1 : id - kSelectTab); return;
        }
        if (!web) return;
        BOOL allowed = FALSE;
        switch (id) {
        case kBack: web->get_CanGoBack(&allowed); if (allowed) web->GoBack(); break;
        case kForward: web->get_CanGoForward(&allowed); if (allowed) web->GoForward(); break;
        case kReload: if (loading) web->Stop(); else web->Reload(); break;
        case kStop: if (fullscreen) ToggleFullscreen(); else web->Stop(); break;
        case kHome: Home(); break;
        case kGo: Go(); break;
        }
    }

    static int Shortcut(UINT key) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (ctrl && key == 'N') return kNewWindow;
        if (ctrl && key == 'H') return kHistory;
        if (key == VK_F11) return kFullscreen;
        if (ctrl && (key == VK_OEM_PLUS || key == VK_ADD)) return kZoomIn;
        if (ctrl && (key == VK_OEM_MINUS || key == VK_SUBTRACT)) return kZoomOut;
        if (ctrl && (key == '0' || key == VK_NUMPAD0)) return kZoomReset;
        if ((alt && key == 'F') || key == VK_F10) return kMenu;
        if (ctrl && key == 'T') return kNewTab;
        if (ctrl && (key == 'W' || key == VK_F4)) return kCloseTab;
        if (ctrl && key == VK_TAB) return (GetKeyState(VK_SHIFT) & 0x8000) ? kPreviousTab : kNextTab;
        if (ctrl && key >= '1' && key <= '9') return kSelectTab + static_cast<int>(key - '1');
        if (ctrl && (key == 'L' || key == 'K')) return kFocusAddress;
        if (alt && key == VK_LEFT) return kBack;
        if (alt && key == VK_RIGHT) return kForward;
        if (alt && key == VK_HOME) return kHome;
        if (key == VK_F5 || (ctrl && key == 'R')) return kReload;
        if (key == VK_ESCAPE) return kStop;
        return 0;
    }

    bool PrepareData() {
        wchar_t path[32768]{};
        const DWORD size = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        if (!size || size >= ARRAYSIZE(path)) return false;
        dataPath.assign(path, size);
        dataPath.resize(dataPath.find_last_of(L"\\/"));
        dataPath += L"\\Data";
        if (!CreateDirectoryW(dataPath.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        const auto probe = dataPath + L"\\.write-test-" + std::to_wstring(GetCurrentProcessId());
        HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        CloseHandle(file);
        return true;
    }

    void BindEvents(int id) {
        auto* tab = FindTab(id);
        if (!tab) return;
        auto view = tab->web;
        auto control = tab->controller;
        EventRegistrationToken token{};
        view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this, id](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) -> HRESULT {
                if (auto* t = FindTab(id)) {
                    t->loading = true;
                    if (id == activeId) { loading = true; InvalidateRect(reload, nullptr, TRUE); }
                }
                return S_OK;
            }).Get(), &token);
        view->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, id](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                auto* t = FindTab(id);
                if (!t) return S_OK;
                t->loading = false;
                UpdateSource(id); UpdateTitle(id);
                BOOL success = FALSE; args->get_IsSuccess(&success);
                if (success) RecordVisit(id);
                if (id == activeId) {
                    loading = false; UpdateHistory(); InvalidateRect(reload, nullptr, TRUE);
                    if (t->homePending) FocusAddress();
                }
                t->homePending = false;
                return S_OK;
            }).Get(), &token);
        view->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
            [this, id](ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*) -> HRESULT { UpdateSource(id); return S_OK; }).Get(), &token);
        view->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this, id](ICoreWebView2*, IUnknown*) -> HRESULT { if (id == activeId) UpdateHistory(); return S_OK; }).Get(), &token);
        view->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this, id](ICoreWebView2*, IUnknown*) -> HRESULT { UpdateTitle(id); return S_OK; }).Get(), &token);
        view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE);
                if (!closing) AddTab(args);
                return S_OK;
            }).Get(), &token);
        view->add_WindowCloseRequested(Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
            [this, id](ICoreWebView2*, IUnknown*) -> HRESULT {
                if (!closing) PostMessageW(window, kCloseTabById, id, 0);
                return S_OK;
            }).Get(), &token);
        view->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this, id](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                auto* tab = FindTab(id);
                if (!tab || !tab->historyPage) return S_OK;
                // A per-document nonce authenticates the internal page, including
                // WebView2's data-URL restoration after Back navigation.
                LPWSTR message = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                    const std::wstring text = message;
                    if (!tab->historyKey.empty() && text.starts_with(tab->historyKey)) {
                        const auto url = text.substr(tab->historyKey.size());
                        if (url.starts_with(L"https://") || url.starts_with(L"http://") || url.starts_with(L"file://")) tab->web->Navigate(url.c_str());
                    }
                }
                CoTaskMemFree(message);
                return S_OK;
            }).Get(), &token);
        control->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [this, id](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                if (id != activeId || closing) return S_OK;
                COREWEBVIEW2_KEY_EVENT_KIND kind{};
                UINT key{};
                args->get_KeyEventKind(&kind); args->get_VirtualKey(&key);
                if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                    const int command = Shortcut(key);
                    if (command) {
                        args->put_Handled(TRUE);
                        // Avoid synchronous controller/focus operations in the key callback.
                        PostMessageW(window, command == kCloseTab ? kCloseTabById : kDispatchKey,
                            command == kCloseTab ? id : command, 0);
                    }
                }
                return S_OK;
            }).Get(), &token);
        control->add_MoveFocusRequested(Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
            [this, id](ICoreWebView2Controller*, ICoreWebView2MoveFocusRequestedEventArgs* args) -> HRESULT {
                if (id != activeId || closing) return S_OK;
                COREWEBVIEW2_MOVE_FOCUS_REASON reason{};
                args->get_Reason(&reason);
                SetFocus(reason == COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS ? go :
                    (IsWindowEnabled(back) ? back : reload));
                args->put_Handled(TRUE);
                return S_OK;
            }).Get(), &token);
        control->add_ZoomFactorChanged(Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
            [this, id](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                if (!closing && id == activeId) RefreshZoom(); return S_OK;
            }).Get(), &token);
    }

    void AddTab(ICoreWebView2NewWindowRequestedEventArgs* popup = nullptr, bool history = false) {
        if (!environment || closing) return;
        Tab tab;
        tab.id = ++nextId;
        tab.historyPage = history;
        if (history) {
            GUID guid{}; wchar_t key[40]{};
            if (FAILED(CoCreateGuid(&guid))) return;
            StringFromGUID2(guid, key, ARRAYSIZE(key)); tab.historyKey = key;
        }
        if (popup) {
            tab.popup = popup;
            const HRESULT hr = popup->GetDeferral(&tab.deferral);
            if (FAILED(hr)) { Error(L"无法创建网页弹出标签。", hr); return; }
        }
        const int id = tab.id;
        tabs.push_back(std::move(tab));
        TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(L"新标签页");
        TabCtrl_InsertItem(tabStrip, static_cast<int>(tabs.size()) - 1, &item);
        ActivateTab(static_cast<int>(tabs.size()) - 1, false);
        SetFocus(window);
        const HRESULT hr = environment->CreateCoreWebView2Controller(window,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this, id](HRESULT result, ICoreWebView2Controller* created) -> HRESULT {
                    auto* t = FindTab(id);
                    if (!t) { if (created) created->Close(); return S_OK; }
                    if (SUCCEEDED(result) && created) {
                        t->controller = created;
                        result = created->get_CoreWebView2(&t->web);
                    } else if (SUCCEEDED(result)) result = E_FAIL;
                    if (FAILED(result)) {
                        FinishPopup(*t);
                        PostMessageW(window, kCloseTabById, id, 0);
                        Error(L"无法创建标签页。", result);
                        return S_OK;
                    }
                    BindEvents(id);
                    RECT client{}; GetClientRect(window, &client);
                    RECT bounds{0, fullscreen ? 0 : Px(93), client.right, std::max<LONG>(fullscreen ? 0 : Px(93), client.bottom)};
                    t->controller->put_Bounds(bounds);
                    t->controller->put_IsVisible(id == activeId && !IsIconic(window));
                    if (id == activeId) ActivateTab(IndexOf(id), false);
                    // Returning the actual WebView preserves window.opener and script popups.
                    if (t->popup) FinishPopup(*t, t->web.Get());
                    else if (t->historyPage) t->web->NavigateToString(HistoryPage(t->historyKey).c_str());
                    else {
                        t->homePending = true;
                        t->web->NavigateToString(kHomePage);
                    }
                    return S_OK;
                }).Get());
        if (FAILED(hr)) {
            if (auto* t = FindTab(id)) FinishPopup(*t);
            PostMessageW(window, kCloseTabById, id, 0);
            Error(L"无法创建标签页。", hr);
        }
    }

    void StartWebView() {
        if (!PrepareData()) {
            MessageBoxW(window, L"无法在程序旁创建或写入 Data 文件夹。\n请把 Liteview 放到有写入权限的普通文件夹，再重新打开。", L"Liteview", MB_OK | MB_ICONERROR);
            return;
        }
        const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataPath.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    if (closing) return S_OK;
                    if (FAILED(result) || !env) { Error(L"无法启动 WebView2。请确认已安装 Microsoft Edge WebView2 Runtime。", FAILED(result) ? result : E_FAIL); return S_OK; }
                    environment = env;
                    EnableWindow(newTab, TRUE);
                    AddTab();
                    return S_OK;
                }).Get());
        if (FAILED(hr)) Error(L"无法加载 WebView2 Runtime。请确认已安装该运行时。", hr);
    }
};

std::string Utf8(const std::wstring& value) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring Wide(const std::string& value) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::wstring Html(const std::wstring& value) {
    std::wstring result;
    for (wchar_t c : value) {
        switch (c) {
        case L'&': result += L"&amp;"; break;
        case L'<': result += L"&lt;"; break;
        case L'>': result += L"&gt;"; break;
        case L'"': result += L"&quot;"; break;
        case L'\'': result += L"&#39;"; break;
        default: result += c;
        }
    }
    return result;
}

void Browser::RecordVisit(int id) {
    auto* tab = FindTab(id);
    if (!tab || dataPath.empty()) return;
    const auto& url = tab->addressText;
    if (!url.starts_with(L"https://") && !url.starts_with(L"http://") && !url.starts_with(L"file://")) return;
    std::wstring title = tab->title.substr(0, 512), cleanUrl = url.substr(0, 8192);
    for (auto* text : {&title, &cleanUrl}) for (auto& c : *text) if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t date[32]{};
    swprintf_s(date, L"%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const auto line = Utf8(std::wstring(date) + L"\t" + title + L"\t" + cleanUrl + L"\n");
    HANDLE file = CreateFileW((dataPath + L"\\history.tsv").c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(file, nullptr), count{};
    if (size != INVALID_FILE_SIZE && size > 1024 * 1024) {
        // Bound portable history storage, retaining complete recent records.
        constexpr DWORD keep = 512 * 1024;
        SetFilePointer(file, size - keep, nullptr, FILE_BEGIN);
        std::string recent(keep, '\0');
        if (ReadFile(file, recent.data(), keep, &count, nullptr)) {
            recent.resize(count);
            const auto first = recent.find('\n');
            if (first != std::string::npos) recent.erase(0, first + 1);
            SetFilePointer(file, 0, nullptr, FILE_BEGIN);
            WriteFile(file, recent.data(), static_cast<DWORD>(recent.size()), &count, nullptr);
            SetEndOfFile(file);
        }
    }
    SetFilePointer(file, 0, nullptr, FILE_END);
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &count, nullptr);
    CloseHandle(file);
}

std::wstring Browser::HistoryPage(const std::wstring& key) const {
    std::wstring page = LR"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>历史记录</title><style>
body{font:14px/1.6 system-ui,'Segoe UI',sans-serif;margin:36px auto;padding:0 24px;max-width:900px;color:#28313c;background:#fafbfc}h1{font-size:26px}p,time,small{color:#697586}input{box-sizing:border-box;width:100%;padding:10px 12px;border:1px solid #d8dee6;border-radius:6px;background:white;font:inherit;margin:8px 0 24px}article{border-bottom:1px solid #e4e8ed;padding:12px 0}a{color:#245dc4;text-decoration:none;overflow-wrap:anywhere}a:hover{text-decoration:underline}small{display:block;overflow-wrap:anywhere}time{font-size:12px;float:right;margin-left:16px}</style><h1>历史记录</h1><p>最近访问的页面 · 数据保存在程序旁的 Data 文件夹</p><input aria-label="搜索历史记录" placeholder="搜索标题或网址" oninput="for(const row of document.querySelectorAll('article'))row.hidden=!row.textContent.toLocaleLowerCase().includes(this.value.toLocaleLowerCase())">)HTML";
    HANDLE file = CreateFileW((dataPath + L"\\history.tsv").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::vector<std::wstring> rows;
    if (file != INVALID_HANDLE_VALUE) {
        const DWORD size = std::min(GetFileSize(file, nullptr), static_cast<DWORD>(1100 * 1024));
        std::string content(size, '\0'); DWORD count{};
        if (ReadFile(file, content.data(), size, &count, nullptr)) {
            content.resize(count);
            size_t start = 0;
            while (start < content.size()) {
                const auto end = content.find('\n', start);
                if (end == std::string::npos) break;
                auto record = Wide(content.substr(start, end - start));
                const auto first = record.find(L'\t'), second = first == std::wstring::npos ? first : record.find(L'\t', first + 1);
                if (second != std::wstring::npos) {
                    const auto url = record.substr(second + 1);
                    if (url.starts_with(L"https://") || url.starts_with(L"http://") || url.starts_with(L"file://"))
                        rows.push_back(L"<article><time>" + Html(record.substr(0, first)) + L"</time><a href=\"" + Html(url) + L"\">" +
                            Html(record.substr(first + 1, second - first - 1)) + L"</a><small>" + Html(url) + L"</small></article>");
                }
                start = end + 1;
            }
        }
        CloseHandle(file);
    }
    if (rows.empty()) page += L"<p>暂无浏览历史记录。</p>";
    else for (size_t i = rows.size(), count = 0; i && count < 500; ++count) {
        const auto& row = rows[--i];
        if (page.size() + row.size() > 500000) break;
        page += row;
    }
    return page + L"<script>document.addEventListener('click',event=>{const link=event.target.closest('a');if(link){event.preventDefault();window.chrome.webview.postMessage('" +
        key + L"'+link.href)}})</script></html>";
}

void Browser::RefreshZoom() {
    if (!menuPopup) return;
    double factor = 1;
    if (controller) controller->get_ZoomFactor(&factor);
    wchar_t text[16]{}; swprintf_s(text, L"%d%%", static_cast<int>(factor * 100 + .5));
    SetWindowTextW(zoomLabel, text);
    for (int id : {kZoomOut, kZoomReset, kZoomIn}) EnableWindow(GetDlgItem(menuPopup, id), controller != nullptr);
    InvalidateRect(zoomLabel, nullptr, FALSE);
}

void Browser::ChangeZoom(int direction) {
    if (!controller) return;
    double current = 1, target = 1;
    controller->get_ZoomFactor(&current);
    constexpr double levels[] = {.25, .5, .67, .75, .8, .9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4, 5};
    if (direction > 0) {
        target = levels[ARRAYSIZE(levels) - 1];
        for (double level : levels) if (level > current + .001) { target = level; break; }
    } else if (direction < 0) {
        target = levels[0];
        for (double level : levels) if (level < current - .001) target = level;
    }
    controller->put_ZoomFactor(target); RefreshZoom();
}

void Browser::ToggleFullscreen() {
    CloseMenu();
    fullscreen = !fullscreen;
    if (fullscreen) {
        restoreStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
        GetWindowPlacement(window, &restorePlacement);
    }
    for (HWND child : {back, forward, reload, home, address, go, menuButton, tabStrip, newTab, minimize, maximize, exit})
        ShowWindow(child, fullscreen ? SW_HIDE : SW_SHOWNA);
    if (fullscreen) {
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(window, GWL_STYLE, restoreStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(window, GWL_STYLE, restoreStyle);
        SetWindowPlacement(window, &restorePlacement);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    Layout();
    if (controller) controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void Browser::CloseMenu() {
    if (!menuPopup) return;
    HWND popup = menuPopup;
    menuPopup = nullptr; zoomLabel = nullptr;
    DestroyWindow(popup);
}

void Browser::ShowMenu() {
    if (menuPopup) { CloseMenu(); return; }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.lpfnWndProc = MenuProc; cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.lpszClassName = kMenuClass;
    RegisterClassW(&cls);
    RECT anchor{}; GetWindowRect(menuButton, &anchor);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    const int width = Px(306), height = Px(240);
    const int left = std::clamp<LONG>(anchor.right - width, monitor.rcWork.left, monitor.rcWork.right - width);
    const int top = std::max<LONG>(monitor.rcWork.top, std::min<LONG>(anchor.bottom + Px(5), monitor.rcWork.bottom - height));
    menuPopup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kMenuClass, L"Liteview 菜单",
        WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, left, top, width, height, window, nullptr, instance, this);
    if (menuPopup) {
        RefreshZoom(); ShowWindow(menuPopup, SW_SHOWNORMAL); SetFocus(GetDlgItem(menuPopup, kMenuTab));
    }
}

LRESULT CALLBACK AddressProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR context) {
    auto* browser = reinterpret_cast<Browser*>(context);
    if (message == WM_GETDLGCODE) return DefSubclassProc(window, message, wp, lp) | DLGC_WANTALLKEYS;
    if (message == WM_KEYDOWN && wp == VK_RETURN) { browser->Go(); return 0; }
    if (message == WM_KEYDOWN && wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        SendMessageW(window, EM_SETSEL, 0, -1); return 0;
    }
    if (message == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, AddressProc, 1);
    return DefSubclassProc(window, message, wp, lp);
}

void DrawTab(const Browser& browser, const DRAWITEMSTRUCT& item);

LRESULT CALLBACK TabProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR context) {
    auto* browser = reinterpret_cast<Browser*>(context);
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        // Keep the native tab layout, hit testing and accessibility, but paint the
        // entire strip ourselves so no themed bevel or open-bottom frame remains.
        PAINTSTRUCT paint{};
        HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wp);
        RECT client{}; GetClientRect(window, &client);
        FillRect(dc, &client, browser->surface);
        for (size_t i = 0; i < browser->tabs.size(); ++i) {
            DRAWITEMSTRUCT item{}; item.CtlType = ODT_TAB; item.CtlID = kTabs;
            item.itemID = static_cast<UINT>(i); item.hwndItem = window; item.hDC = dc;
            TabCtrl_GetItemRect(window, static_cast<int>(i), &item.rcItem);
            DrawTab(*browser, item);
        }
        if (message == WM_PAINT) EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_MOUSEMOVE) {
        browser->UpdateTabHover({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
    }
    if (message == WM_MOUSELEAVE) browser->UpdateTabHover({-1, -1});
    if (message == WM_LBUTTONDOWN || message == WM_MBUTTONUP) {
        TCHITTESTINFO hit{}; hit.pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int index = TabCtrl_HitTest(window, &hit);
        if (index >= 0 && index < static_cast<int>(browser->tabs.size())) {
            RECT rect{}; TabCtrl_GetItemRect(window, index, &rect);
            if (message == WM_MBUTTONUP || hit.pt.x >= rect.right - browser->Px(24)) {
                PostMessageW(browser->window, kCloseTabById, browser->tabs[index].id, 0);
                return 0;
            }
        }
    }
    if (message == WM_KEYDOWN && (wp == VK_LEFT || wp == VK_RIGHT)) {
        browser->CycleTab(wp == VK_LEFT ? -1 : 1); return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, TabProc, 1);
    return DefSubclassProc(window, message, wp, lp);
}

LRESULT CALLBACK ButtonProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR context) {
    auto* browser = reinterpret_cast<Browser*>(context);
    if (message == WM_MOUSEMOVE && IsWindowEnabled(window) && browser->hotButton != window) {
        if (browser->hotButton) InvalidateRect(browser->hotButton, nullptr, FALSE);
        browser->hotButton = window; InvalidateRect(window, nullptr, FALSE);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
    }
    if ((message == WM_MOUSELEAVE || (message == WM_ENABLE && !wp) || message == WM_NCDESTROY) && browser->hotButton == window) {
        browser->hotButton = nullptr; InvalidateRect(window, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ButtonProc, 1);
    return DefSubclassProc(window, message, wp, lp);
}

void DrawTab(const Browser& browser, const DRAWITEMSTRUCT& item) {
    if (item.itemID >= browser.tabs.size()) return;
    // Tab item rectangles can extend beyond the visible strip during insertion/scrolling.
    // WM_DRAWITEM does not automatically clip a tab's supplied DC to its control.
    const int saved = SaveDC(item.hDC);
    if (!saved) return;
    RECT viewport{}; GetClientRect(browser.tabStrip, &viewport);
    IntersectClipRect(item.hDC, viewport.left, viewport.top, viewport.right, viewport.bottom);
    IntersectClipRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
    const auto& tab = browser.tabs[item.itemID];
    RECT rect = item.rcItem;
    const bool hot = tab.id == browser.hotTabId;
    const COLORREF background = tab.id == browser.activeId ? RGB(220, 224, 230) : (hot ? RGB(234, 237, 242) : kSurface);
    HBRUSH fill = CreateSolidBrush(background);
    FillRect(item.hDC, &rect, fill); DeleteObject(fill);
    SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, kInk);
    const HGDIOBJ oldFont = SelectObject(item.hDC, browser.font);
    RECT text = rect; text.left += browser.Px(10); text.right -= browser.Px(28);
    DrawTextW(item.hDC, tab.title.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    const int x = rect.right - browser.Px(13), y = (rect.top + rect.bottom) / 2, d = browser.Px(3);
    if (hot && browser.hotTabClose) {
        fill = CreateSolidBrush(RGB(214, 222, 232));
        HGDIOBJ oldBrush = SelectObject(item.hDC, fill), previousPen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
        const int radius = browser.Px(10);
        RoundRect(item.hDC, x - radius, y - radius, x + radius, y + radius, browser.Px(6), browser.Px(6));
        SelectObject(item.hDC, previousPen); SelectObject(item.hDC, oldBrush); DeleteObject(fill);
    }
    HPEN pen = CreatePen(PS_SOLID, std::max(1, browser.Px(1)), RGB(100, 110, 122));
    HGDIOBJ oldPen = SelectObject(item.hDC, pen);
    MoveToEx(item.hDC, x - d, y - d, nullptr); LineTo(item.hDC, x + d + 1, y + d + 1);
    MoveToEx(item.hDC, x + d, y - d, nullptr); LineTo(item.hDC, x - d - 1, y + d + 1);
    SelectObject(item.hDC, oldPen); SelectObject(item.hDC, oldFont); DeleteObject(pen);
    RestoreDC(item.hDC, saved);
}

void DrawButton(const Browser& browser, const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    RECT rect = item.rcItem;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool caption = item.CtlID >= kMinimize && item.CtlID <= kExit;
    const bool popup = browser.menuPopup && GetParent(item.hwndItem) == browser.menuPopup;
    const bool hot = !disabled && browser.hotButton == item.hwndItem;
    const bool pressed = !disabled && (item.itemState & ODS_SELECTED);
    const bool red = item.CtlID == kExit && (hot || pressed);
    const COLORREF highlight = pressed ? RGB(209, 218, 229) : RGB(227, 233, 241);
    if (caption && (hot || pressed)) {
        HBRUSH fill = CreateSolidBrush(red ? (pressed ? RGB(190, 28, 40) : RGB(218, 40, 52)) : highlight);
        FillRect(dc, &rect, fill); DeleteObject(fill);
    } else FillRect(dc, &rect, popup || item.CtlID == kGo ? static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)) : browser.surface);
    if (!caption && (hot || pressed)) {
        HBRUSH fill = CreateSolidBrush(highlight);
        HGDIOBJ oldBrush = SelectObject(dc, fill), oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1, browser.Px(8), browser.Px(8));
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(fill);
    }
    if (popup) {
        wchar_t label[96]{}; GetWindowTextW(item.hwndItem, label, ARRAYSIZE(label));
        HGDIOBJ previous = SelectObject(dc, browser.font);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, disabled ? RGB(155, 164, 177) : kInk);
        RECT text = rect; InflateRect(&text, -browser.Px(12), 0);
        wchar_t* shortcut = wcschr(label, L'\t');
        if (shortcut) {
            *shortcut++ = 0;
            DrawTextW(dc, shortcut, -1, &text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        const UINT align = item.CtlID >= kZoomOut && item.CtlID <= kFullscreen ? DT_CENTER : DT_LEFT;
        DrawTextW(dc, label, -1, &text, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            InflateRect(&rect, -browser.Px(4), -browser.Px(4)); DrawFocusRect(dc, &rect);
        }
        SelectObject(dc, previous); return;
    }
    HPEN pen = CreatePen(PS_SOLID, std::max(1, browser.Px(caption ? 1 : 2)),
        disabled ? RGB(183, 191, 201) : (red ? RGB(255, 255, 255) : kInk));
    HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int cx = (rect.left + rect.right) / 2, cy = (rect.top + rect.bottom) / 2, d = browser.Px(6);
    auto line = [&](int x1, int y1, int x2, int y2) { MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2); };
    switch (item.CtlID) {
    case kMenu:
        for (int offset : {-5, 0, 5}) {
            const int x = cx + browser.Px(offset), r = std::max(1, browser.Px(1));
            HBRUSH dot = CreateSolidBrush(kInk); HGDIOBJ previous = SelectObject(dc, dot);
            Ellipse(dc, x - r, cy - r, x + r + 1, cy + r + 1);
            SelectObject(dc, previous); DeleteObject(dot);
        }
        break;
    case kMinimize: line(cx - d, cy + 1, cx + d, cy + 1); break;
    case kMaximize:
        if (IsZoomed(browser.window)) {
            Rectangle(dc, cx - d + 2, cy - d, cx + d, cy + d - 2);
            HBRUSH fill = CreateSolidBrush(hot || pressed ? highlight : kSurface);
            HGDIOBJ previous = SelectObject(dc, fill);
            Rectangle(dc, cx - d, cy - d + 2, cx + d - 2, cy + d);
            SelectObject(dc, previous); DeleteObject(fill);
        } else Rectangle(dc, cx - d + 1, cy - d + 1, cx + d, cy + d);
        break;
    case kExit:
        line(cx - d + 1, cy - d + 1, cx + d, cy + d);
        line(cx + d - 1, cy - d + 1, cx - d, cy + d); break;
    case kNewTab:
        line(cx - d, cy, cx + d, cy); line(cx, cy - d, cx, cy + d); break;
    case kBack: case kForward: case kGo: {
        const int direction = item.CtlID == kBack ? -1 : 1;
        line(cx - d, cy, cx + d, cy);
        line(cx + direction * d, cy, cx, cy - d);
        line(cx + direction * d, cy, cx, cy + d);
        break;
    }
    case kReload:
        if (browser.loading) { line(cx - d, cy - d, cx + d, cy + d); line(cx + d, cy - d, cx - d, cy + d); }
        else {
            Arc(dc, cx - d, cy - d, cx + d + 1, cy + d + 1, cx + d, cy - d, cx + d, cy);
            line(cx + d, cy - d - 1, cx + d, cy - 1);
            line(cx + d, cy - 1, cx + 1, cy - 1);
        }
        break;
    case kHome:
        line(cx - d - 1, cy, cx, cy - d);
        line(cx, cy - d, cx + d + 1, cy);
        line(cx - d + 1, cy, cx - d + 1, cy + d);
        line(cx - d + 1, cy + d, cx + d - 1, cy + d);
        line(cx + d - 1, cy + d, cx + d - 1, cy);
        break;
    }
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        InflateRect(&rect, -browser.Px(4), -browser.Px(4)); DrawFocusRect(dc, &rect);
    }
}

LRESULT CALLBACK MenuProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* browser = reinterpret_cast<Browser*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        browser = static_cast<Browser*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(browser));
        browser->menuPopup = window;
    }
    if (!browser) return DefWindowProcW(window, message, wp, lp);
    switch (message) {
    case WM_CREATE: {
        auto button = [&](int id, const wchar_t* label, int x, int y, int width, int height = 36) {
            HWND child = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                browser->Px(x), browser->Px(y), browser->Px(width), browser->Px(height), window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SetWindowSubclass(child, ButtonProc, 1, reinterpret_cast<DWORD_PTR>(browser));
            return child;
        };
        button(kMenuTab, L"新建标签页\tCtrl+T", 8, 8, 288);
        button(kNewWindow, L"新建窗口\tCtrl+N", 8, 44, 288);
        HWND label = CreateWindowExW(0, L"STATIC", L"缩放", WS_CHILD | WS_VISIBLE,
            browser->Px(20), browser->Px(99), browser->Px(65), browser->Px(24), window, nullptr, nullptr, nullptr);
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(browser->font), FALSE);
        button(kZoomOut, L"−", 94, 90, 36);
        browser->zoomLabel = button(kZoomReset, L"100%", 132, 90, 62);
        button(kZoomIn, L"+", 196, 90, 36);
        button(kFullscreen, L"全屏", 238, 90, 58);
        button(kHistory, L"历史记录\tCtrl+H", 8, 140, 288);
        button(kExitAll, L"关闭 Liteview", 8, 194, 288);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) browser->CloseMenu();
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == kZoomOut || id == kZoomReset || id == kZoomIn) browser->Command(id);
        else {
            browser->CloseMenu();
            PostMessageW(browser->window, kDispatchKey, id, 0);
        }
        return 0;
    }
    case WM_DRAWITEM: DrawButton(*browser, *reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wp), kInk); SetBkColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT client{}; GetClientRect(window, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(228, 232, 238)); HGDIOBJ previous = SelectObject(dc, pen);
        for (int y : {84, 132, 184}) {
            MoveToEx(dc, browser->Px(10), browser->Px(y), nullptr); LineTo(dc, client.right - browser->Px(10), browser->Px(y));
        }
        SelectObject(dc, previous); DeleteObject(pen); EndPaint(window, &paint); return 0;
    }
    case WM_CLOSE: browser->CloseMenu(); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* browser = reinterpret_cast<Browser*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        browser = static_cast<Browser*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        browser->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(browser));
    }
    if (!browser) return DefWindowProcW(window, message, wp, lp);
    switch (message) {
    case WM_NCCALCSIZE:
        if (wp) {
            if (browser->fullscreen) return 0;
            auto* size = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            // Retain native side/bottom resize borders outside the WebView child window.
            const int x = browser->ResizeBorder(SM_CXSIZEFRAME), y = browser->ResizeBorder(SM_CYSIZEFRAME);
            size->rgrc[0].left += x; size->rgrc[0].right -= x; size->rgrc[0].bottom -= y;
            if (IsZoomed(window)) size->rgrc[0].top += y;
            return 0;
        }
        break;
    case WM_NCHITTEST: return browser->HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
    case WM_ACTIVATE:
    case WM_DWMCOMPOSITIONCHANGED: browser->ExtendFrame(); break;
    case WM_CREATE: {
        browser->dpi = GetDpiForWindow(window);
        browser->surface = CreateSolidBrush(kSurface);
        browser->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        auto button = [&](int id, const wchar_t* label) {
            HWND child = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_OWNERDRAW | WS_DISABLED,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SetWindowSubclass(child, ButtonProc, 1, reinterpret_cast<DWORD_PTR>(browser));
            TOOLINFOW tool{sizeof(tool)};
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.hwnd = window; tool.uId = reinterpret_cast<UINT_PTR>(child); tool.lpszText = const_cast<wchar_t*>(label);
            SendMessageW(browser->tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            return child;
        };
        browser->back = button(kBack, L"后退 (Alt+←)"); browser->forward = button(kForward, L"前进 (Alt+→)");
        browser->reload = button(kReload, L"刷新 / 停止 (F5 / Esc)"); browser->home = button(kHome, L"起始页 (Alt+Home)");
        browser->address = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_DISABLED,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddress)), nullptr, nullptr);
        SendMessageW(browser->address, EM_SETLIMITTEXT, 32760, 0);
        SendMessageW(browser->address, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"输入网址或搜索 (Ctrl+L)"));
        SetWindowSubclass(browser->address, AddressProc, 1, reinterpret_cast<DWORD_PTR>(browser));
        browser->go = button(kGo, L"转到 (Enter)");
        browser->menuButton = button(kMenu, L"菜单 (Alt+F)"); EnableWindow(browser->menuButton, TRUE);
        browser->tabStrip = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH | TCS_SINGLELINE | TCS_FOCUSNEVER,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabs)), nullptr, nullptr);
        SetWindowSubclass(browser->tabStrip, TabProc, 1, reinterpret_cast<DWORD_PTR>(browser));
        browser->newTab = button(kNewTab, L"新建标签页 (Ctrl+T)");
        browser->minimize = button(kMinimize, L"最小化");
        browser->maximize = button(kMaximize, L"最大化 / 还原");
        browser->exit = button(kExit, L"关闭窗口");
        for (HWND caption : {browser->minimize, browser->maximize, browser->exit}) {
            EnableWindow(caption, TRUE);
        }
        browser->SetFont(); browser->Layout();
        browser->ExtendFrame();
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        return 0;
    }
    case WM_SIZE:
        if (browser->maximize) InvalidateRect(browser->maximize, nullptr, FALSE);
        if (browser->controller) browser->controller->put_IsVisible(wp != SIZE_MINIMIZED);
        if (wp != SIZE_MINIMIZED) browser->Layout();
        return 0;
    case WM_MOVE:
        browser->CloseMenu();
        for (auto& tab : browser->tabs) if (tab.controller) tab.controller->NotifyParentWindowPositionChanged();
        return 0;
    case WM_DPICHANGED: {
        browser->CloseMenu();
        browser->dpi = HIWORD(wp); browser->SetFont();
        browser->ExtendFrame();
        const auto* rect = reinterpret_cast<RECT*>(lp);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        browser->Layout(); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* bounds = reinterpret_cast<MINMAXINFO*>(lp);
        bounds->ptMinTrackSize = {browser->Px(480), browser->Px(320)}; return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) browser->Command(LOWORD(wp));
        return 0;
    case kDispatchKey: browser->Command(static_cast<int>(wp)); return 0;
    case kCloseTabById: browser->CloseTab(static_cast<int>(wp)); return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lp)->hwndFrom == browser->tabStrip &&
            reinterpret_cast<NMHDR*>(lp)->code == TCN_SELCHANGE) {
            browser->ActivateTab(TabCtrl_GetCurSel(browser->tabStrip)); return 0;
        }
        break;
    case WM_APPCOMMAND:
        if (GET_APPCOMMAND_LPARAM(lp) == APPCOMMAND_BROWSER_BACKWARD) { browser->Command(kBack); return TRUE; }
        if (GET_APPCOMMAND_LPARAM(lp) == APPCOMMAND_BROWSER_FORWARD) { browser->Command(kForward); return TRUE; }
        break;
    case WM_DRAWITEM:
        if (wp == kTabs) DrawTab(*browser, *reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        else DrawButton(*browser, *reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wp), kInk); SetBkColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client); FillRect(dc, &client, browser->surface);
        if (browser->fullscreen) { EndPaint(window, &paint); return 0; }
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(218, 224, 231));
        HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(WHITE_BRUSH));
        const RECT& rect = browser->addressFrame;
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, browser->Px(10), browser->Px(10));
        MoveToEx(dc, 0, browser->Px(92), nullptr); LineTo(dc, client.right, browser->Px(92));
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen);
        EndPaint(window, &paint); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SETFOCUS:
        if (browser->ready) browser->FocusAddress();
        return 0;
    case WM_CLOSE:
        browser->Shutdown();
        DestroyWindow(window); return 0;
    case WM_DESTROY:
        if (browser->font) DeleteObject(browser->font);
        if (browser->surface) DeleteObject(browser->surface);
        if (browsers.empty() || std::none_of(browsers.begin(), browsers.end(), [](const Browser* item) { return !item->closing; }))
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

Browser* CreateBrowserWindow(int show, Browser* source) {
    auto* browser = new Browser;
    browsers.push_back(browser);
    const UINT dpi = source ? source->dpi : GetDpiForSystem();
    HWND window = CreateWindowExW(0, kClass, L"Liteview", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1120, dpi, 96), MulDiv(760, dpi, 96), nullptr, nullptr,
        GetModuleHandleW(nullptr), browser);
    if (!window) { browsers.pop_back(); delete browser; return nullptr; }
    ShowWindow(window, show); UpdateWindow(window);
    if (source && source->environment) {
        browser->dataPath = source->dataPath; browser->environment = source->environment;
        EnableWindow(browser->newTab, TRUE); browser->AddTab();
    } else browser->StartWebView();
    return browser;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{sizeof(cls)};
    cls.style = CS_HREDRAW | CS_VREDRAW; cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_LITEVIEW), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXICON, GetDpiForSystem()),
        GetSystemMetricsForDpi(SM_CYICON, GetDpiForSystem()), LR_SHARED));
    cls.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_LITEVIEW), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem()),
        GetSystemMetricsForDpi(SM_CYSMICON, GetDpiForSystem()), LR_SHARED));
    cls.lpszClassName = kClass;
    if (!RegisterClassExW(&cls)) { CoUninitialize(); return 1; }
    if (!CreateBrowserWindow(show)) { CoUninitialize(); return 1; }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        Browser* current = nullptr;
        const HWND root = GetAncestor(message.hwnd, GA_ROOTOWNER);
        for (auto* item : browsers) if (!item->closing && item->window == root) { current = item; break; }
        if (!current) { TranslateMessage(&message); DispatchMessageW(&message); continue; }
        Browser& browser = *current;
        if (browser.menuPopup && (message.hwnd == browser.menuPopup || IsChild(browser.menuPopup, message.hwnd))) {
            if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { browser.CloseMenu(); continue; }
            if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
                SendMessageW(browser.menuPopup, WM_COMMAND, GetDlgCtrlID(message.hwnd), 0); continue;
            }
            if (IsDialogMessageW(browser.menuPopup, &message)) continue;
        }
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
            const int command = Browser::Shortcut(static_cast<UINT>(message.wParam));
            if (command) { browser.Command(command); continue; }
            if (message.wParam == VK_RETURN && (message.hwnd == browser.back ||
                message.hwnd == browser.forward || message.hwnd == browser.reload ||
                message.hwnd == browser.home || message.hwnd == browser.go || message.hwnd == browser.newTab ||
                message.hwnd == browser.minimize || message.hwnd == browser.maximize || message.hwnd == browser.exit || message.hwnd == browser.menuButton)) {
                browser.Command(GetDlgCtrlID(message.hwnd)); continue;
            }
            if (message.wParam == VK_TAB && (message.hwnd == browser.newTab || message.hwnd == browser.tabStrip ||
                message.hwnd == browser.minimize || message.hwnd == browser.maximize || message.hwnd == browser.exit)) {
                browser.FocusAddress(); continue;
            }
            // Web content handles its own Tab traversal; the native toolbar uses dialog navigation.
            if (message.wParam == VK_TAB && (message.hwnd == browser.address ||
                message.hwnd == browser.back || message.hwnd == browser.forward ||
                message.hwnd == browser.reload || message.hwnd == browser.home || message.hwnd == browser.go || message.hwnd == browser.menuButton)) {
                const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                HWND next = GetNextDlgTabItem(browser.window, message.hwnd, backwards);
                const bool nextIsToolbar = next == browser.address || next == browser.back ||
                    next == browser.forward || next == browser.reload || next == browser.home || next == browser.go || next == browser.menuButton;
                const HWND first = IsWindowEnabled(browser.back) ? browser.back : browser.reload;
                if (browser.controller && (!nextIsToolbar || (!backwards && message.hwnd == browser.menuButton) ||
                    (backwards && message.hwnd == first))) {
                    browser.controller->MoveFocus(backwards ? COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS : COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT);
                } else SetFocus(next);
                continue;
            }
        }
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    for (auto* browser : browsers) { browser->Shutdown(); delete browser; }
    browsers.clear();
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
