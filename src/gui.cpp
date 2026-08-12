#include "conversion.hpp"
#include "common.hpp"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <cwctype>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr wchar_t kClassName[] = L"NamToCloMainWindow";
constexpr UINT WM_APP_STATUS = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;
constexpr int IDC_NAM_PATH = 101;
constexpr int IDC_LOAD = 102;
constexpr int IDC_OUTPUT_PATH = 103;
constexpr int IDC_BROWSE_OUTPUT = 104;
constexpr int IDC_CONVERT = 105;
constexpr int IDC_OPEN_OUTPUT = 106;
constexpr int IDC_STATUS = 107;

HWND gNamEdit = nullptr;
HWND gOutEdit = nullptr;
HWND gLoadButton = nullptr;
HWND gBrowseButton = nullptr;
HWND gConvertButton = nullptr;
HWND gOpenButton = nullptr;
HWND gStatus = nullptr;
HFONT gFont = nullptr;
bool gBusy = false;

std::wstring getText(HWND h) {
    const int len = GetWindowTextLengthW(h);
    std::wstring s(static_cast<std::size_t>(len) + 1, L'\0');
    if (len) GetWindowTextW(h, s.data(), len + 1);
    s.resize(static_cast<std::size_t>(len));
    return s;
}

void setText(HWND h, const std::wstring& s) { SetWindowTextW(h, s.c_str()); }

void enableControls(bool enable) {
    gBusy = !enable;
    EnableWindow(gLoadButton, enable);
    EnableWindow(gBrowseButton, enable);
    EnableWindow(gConvertButton, enable);
    EnableWindow(gOpenButton, enable);
}

void setSelectedNam(const fs::path& p) {
    if (p.empty()) return;
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    if (ext != L".nam") {
        MessageBoxW(nullptr, L"Please select a .nam file.", L"NAM to CLO", MB_ICONWARNING | MB_OK);
        return;
    }
    setText(gNamEdit, p.wstring());
    if (getText(gOutEdit).empty()) setText(gOutEdit, p.parent_path().wstring());
    setText(gStatus, L"Ready to convert.");
}

void chooseNam(HWND owner) {
    wchar_t file[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Neural Amp Model (*.nam)\0*.nam\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"nam";
    if (GetOpenFileNameW(&ofn)) setSelectedNam(fs::path(file));
}

int CALLBACK browseCallback(HWND hwnd, UINT msg, LPARAM, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data) SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

void chooseOutput(HWND owner) {
    std::wstring current = getText(gOutEdit);
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select output folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = browseCallback;
    bi.lParam = reinterpret_cast<LPARAM>(current.c_str());
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(pidl, path)) setText(gOutEdit, path);
    CoTaskMemFree(pidl);
}

void postStatus(HWND hwnd, const std::wstring& s) {
    auto* copy = new std::wstring(s);
    PostMessageW(hwnd, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(copy));
}

void startConversion(HWND hwnd) {
    if (gBusy) return;
    const fs::path nam = getText(gNamEdit);
    const fs::path out = getText(gOutEdit);
    if (nam.empty()) {
        MessageBoxW(hwnd, L"Select a NAM file first.", L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (out.empty()) {
        MessageBoxW(hwnd, L"Select an output folder.", L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        return;
    }
    enableControls(false);
    setText(gStatus, L"Starting conversion...");
    std::thread([hwnd, nam, out] {
        auto result = std::make_unique<ntc::ConversionResult>(
            ntc::convertNamToBoth(nam, out, [hwnd](const std::wstring& s) { postStatus(hwnd, s); }));
        PostMessageW(hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}

void openOutputFolder(HWND hwnd) {
    const std::wstring out = getText(gOutEdit);
    if (out.empty()) return;
    ShellExecuteW(hwnd, L"open", out.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void applyFont(HWND h) { if (h && gFont) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE); }

void createUi(HWND hwnd) {
    gFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HWND title = CreateWindowW(L"STATIC", L"NAM to CLO", WS_CHILD | WS_VISIBLE,
                               28, 22, 440, 34, hwnd, nullptr, nullptr, nullptr);
    HFONT titleFont = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
    SetPropW(hwnd, L"TitleFont", titleFont);

    HWND sub = CreateWindowW(L"STATIC", L"Generate Ampero 2048 and experimental GP-200 1024 CLO files from a NAM model.",
                             WS_CHILD | WS_VISIBLE, 30, 60, 660, 24, hwnd, nullptr, nullptr, nullptr);
    applyFont(sub);

    HWND l1 = CreateWindowW(L"STATIC", L"NAM file", WS_CHILD | WS_VISIBLE, 30, 108, 150, 24, hwnd, nullptr, nullptr, nullptr);
    applyFont(l1);
    gNamEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                               30, 134, 540, 32, hwnd, reinterpret_cast<HMENU>(IDC_NAM_PATH), nullptr, nullptr);
    applyFont(gNamEdit);
    gLoadButton = CreateWindowW(L"BUTTON", L"Load NAM...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                582, 133, 120, 34, hwnd, reinterpret_cast<HMENU>(IDC_LOAD), nullptr, nullptr);
    applyFont(gLoadButton);

    HWND l2 = CreateWindowW(L"STATIC", L"Output folder", WS_CHILD | WS_VISIBLE, 30, 190, 150, 24, hwnd, nullptr, nullptr, nullptr);
    applyFont(l2);
    gOutEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                               30, 216, 540, 32, hwnd, reinterpret_cast<HMENU>(IDC_OUTPUT_PATH), nullptr, nullptr);
    applyFont(gOutEdit);
    gBrowseButton = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  582, 215, 120, 34, hwnd, reinterpret_cast<HMENU>(IDC_BROWSE_OUTPUT), nullptr, nullptr);
    applyFont(gBrowseButton);

    HWND info = CreateWindowW(L"STATIC",
        L"Output: <name>_Ampero_2048.clo  +  <name>_GP200_1024.clo\r\n"
        L"The GP-200 1024 file follows the compact structure confirmed in the official editor, but hardware validation is still pending.",
        WS_CHILD | WS_VISIBLE, 30, 273, 672, 55, hwnd, nullptr, nullptr, nullptr);
    applyFont(info);

    gConvertButton = CreateWindowW(L"BUTTON", L"Convert", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                   30, 350, 150, 42, hwnd, reinterpret_cast<HMENU>(IDC_CONVERT), nullptr, nullptr);
    applyFont(gConvertButton);
    gOpenButton = CreateWindowW(L"BUTTON", L"Open output folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                194, 350, 180, 42, hwnd, reinterpret_cast<HMENU>(IDC_OPEN_OUTPUT), nullptr, nullptr);
    applyFont(gOpenButton);

    gStatus = CreateWindowW(L"STATIC", L"Checking runtime...", WS_CHILD | WS_VISIBLE,
                            30, 420, 672, 28, hwnd, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);
    applyFont(gStatus);

    HWND ver = CreateWindowW(L"STATIC", L"Version 1.0.0", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                             580, 462, 122, 22, hwnd, nullptr, nullptr, nullptr);
    applyFont(ver);
    DragAcceptFiles(hwnd, TRUE);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        createUi(hwnd);
        std::string error;
        const auto rt = ntc::resolveDefaultRuntime();
        if (ntc::validateRuntime(rt, error)) setText(gStatus, L"Runtime ready. Load a NAM file or drag it onto this window.");
        else setText(gStatus, L"Runtime missing: " + ntc::fromUtf8(error));
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_LOAD: chooseNam(hwnd); return 0;
        case IDC_BROWSE_OUTPUT: chooseOutput(hwnd); return 0;
        case IDC_CONVERT: startConversion(hwnd); return 0;
        case IDC_OPEN_OUTPUT: openOutputFolder(hwnd); return 0;
        default: break;
        }
        break;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[32768]{};
        if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path)))) setSelectedNam(fs::path(path));
        DragFinish(drop);
        return 0;
    }
    case WM_APP_STATUS: {
        std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lParam));
        if (s) setText(gStatus, *s);
        return 0;
    }
    case WM_APP_DONE: {
        std::unique_ptr<ntc::ConversionResult> r(reinterpret_cast<ntc::ConversionResult*>(lParam));
        enableControls(true);
        if (r && r->ok) {
            std::wstring msg = L"Conversion complete.\r\n\r\nAmpero 2048:\r\n" + r->ampero2048.wstring()
                             + L"\r\n\r\nGP-200 1024 experimental:\r\n" + r->gp2001024.wstring();
            setText(gStatus, L"Done. Two CLO files were generated successfully.");
            MessageBoxW(hwnd, msg.c_str(), L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        } else {
            const std::wstring err = r ? ntc::fromUtf8(r->error) : L"Unknown conversion error.";
            setText(gStatus, L"Conversion failed.");
            MessageBoxW(hwnd, err.c_str(), L"Conversion failed", MB_ICONERROR | MB_OK);
        }
        return 0;
    }
    case WM_CLOSE:
        if (gBusy) {
            if (MessageBoxW(hwnd, L"A conversion is running. Close anyway?", L"NAM to CLO", MB_ICONWARNING | MB_YESNO) != IDYES) return 0;
        }
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY: {
        if (HFONT f = reinterpret_cast<HFONT>(GetPropW(hwnd, L"TitleFont"))) DeleteObject(f);
        if (gFont) DeleteObject(gFont);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1 && std::wstring(argv[1]) == L"--worker") {
        const int code = ntc::runWorkerCommandLine(argc, argv);
        LocalFree(argv);
        return code;
    }
    if (argv) LocalFree(argv);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = wndProc;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kClassName, L"NAM to CLO 1.0",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 750, 540,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) { CoUninitialize(); return 1; }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
