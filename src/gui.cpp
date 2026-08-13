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

namespace fs = std::filesystem;

namespace {
constexpr wchar_t kClassName[] = L"NamToCloMainWindow";
constexpr UINT WM_APP_STATUS = WM_APP + 1;
constexpr UINT WM_APP_DONE_SINGLE = WM_APP + 2;
constexpr UINT WM_APP_DONE_BATCH = WM_APP + 3;
constexpr int IDC_INPUT_PATH = 101;
constexpr int IDC_LOAD_FILE = 102;
constexpr int IDC_LOAD_FOLDER = 103;
constexpr int IDC_OUTPUT_PATH = 104;
constexpr int IDC_BROWSE_OUTPUT = 105;
constexpr int IDC_CONVERT = 106;
constexpr int IDC_OPEN_OUTPUT = 107;
constexpr int IDC_STATUS = 108;
constexpr int IDC_STIMULUS_MODE = 109;

enum class InputMode { None, SingleNam, Folder };

HWND gInputEdit = nullptr;
HWND gOutEdit = nullptr;
HWND gLoadFileButton = nullptr;
HWND gLoadFolderButton = nullptr;
HWND gBrowseButton = nullptr;
HWND gConvertButton = nullptr;
HWND gOpenButton = nullptr;
HWND gStatus = nullptr;
HWND gStimulusCombo = nullptr;
HFONT gFont = nullptr;
bool gBusy = false;
InputMode gInputMode = InputMode::None;

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
    EnableWindow(gLoadFileButton, enable);
    EnableWindow(gLoadFolderButton, enable);
    EnableWindow(gBrowseButton, enable);
    EnableWindow(gConvertButton, enable);
    EnableWindow(gOpenButton, enable);
    EnableWindow(gStimulusCombo, enable);
}

bool isNamFile(const fs::path& p) {
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext == L".nam";
}

void setSingleNam(const fs::path& p) {
    if (p.empty()) return;
    if (!isNamFile(p)) {
        MessageBoxW(nullptr, L"Please select a .nam file.", L"NAM to CLO", MB_ICONWARNING | MB_OK);
        return;
    }
    gInputMode = InputMode::SingleNam;
    setText(gInputEdit, p.wstring());
    if (getText(gOutEdit).empty()) setText(gOutEdit, p.parent_path().wstring());
    setText(gStatus, L"Single-file mode. Ready to convert.");
}

void setNamFolder(const fs::path& p) {
    if (p.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(p, ec) || ec) {
        MessageBoxW(nullptr, L"Please select a valid folder.", L"NAM to CLO", MB_ICONWARNING | MB_OK);
        return;
    }

    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(p, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && !ec && isNamFile(entry.path())) ++count;
        ec.clear();
    }
    if (count == 0) {
        MessageBoxW(nullptr, L"The selected folder contains no .nam files.", L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        return;
    }

    gInputMode = InputMode::Folder;
    setText(gInputEdit, p.wstring());
    if (getText(gOutEdit).empty()) setText(gOutEdit, p.wstring());
    setText(gStatus, L"Batch mode: " + std::to_wstring(count) + L" NAM file(s) found.");
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
    if (GetOpenFileNameW(&ofn)) setSingleNam(fs::path(file));
}

int CALLBACK browseCallback(HWND hwnd, UINT msg, LPARAM, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data) SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

bool chooseFolder(HWND owner, const wchar_t* title, const std::wstring& current, fs::path& selected) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = browseCallback;
    bi.lParam = reinterpret_cast<LPARAM>(current.c_str());
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH]{};
    const bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    if (ok) selected = fs::path(path);
    CoTaskMemFree(pidl);
    return ok;
}

void chooseNamFolder(HWND owner) {
    fs::path selected;
    const std::wstring current = getText(gInputEdit);
    if (chooseFolder(owner, L"Select folder containing NAM files", current, selected)) setNamFolder(selected);
}

void chooseOutput(HWND owner) {
    fs::path selected;
    const std::wstring current = getText(gOutEdit);
    if (chooseFolder(owner, L"Select output folder", current, selected)) setText(gOutEdit, selected.wstring());
}


ntc::StimulusMode selectedStimulusMode() {
    const LRESULT index = SendMessageW(gStimulusCombo, CB_GETCURSEL, 0, 0);
    switch (index) {
    case 1: return ntc::StimulusMode::Clean;
    case 2: return ntc::StimulusMode::Dist;
    default: return ntc::StimulusMode::Legacy;
    }
}

void postStatus(HWND hwnd, const std::wstring& s) {
    auto* copy = new std::wstring(s);
    PostMessageW(hwnd, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(copy));
}

void startConversion(HWND hwnd) {
    if (gBusy) return;
    const fs::path input = getText(gInputEdit);
    const fs::path out = getText(gOutEdit);
    if (input.empty() || gInputMode == InputMode::None) {
        MessageBoxW(hwnd, L"Select a NAM file or a folder containing NAM files first.", L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (out.empty()) {
        MessageBoxW(hwnd, L"Select an output folder.", L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        return;
    }

    const ntc::StimulusMode stimulusMode = selectedStimulusMode();

    enableControls(false);
    if (gInputMode == InputMode::SingleNam) {
        setText(gStatus, L"Starting conversion...");
        std::thread([hwnd, input, out, stimulusMode] {
            auto result = std::make_unique<ntc::ConversionResult>(
                ntc::convertNamToBoth(input, out, stimulusMode, [hwnd](const std::wstring& s) { postStatus(hwnd, s); }));
            PostMessageW(hwnd, WM_APP_DONE_SINGLE, 0, reinterpret_cast<LPARAM>(result.release()));
        }).detach();
    } else {
        setText(gStatus, L"Starting batch conversion...");
        std::thread([hwnd, input, out, stimulusMode] {
            auto result = std::make_unique<ntc::BatchConversionResult>(
                ntc::convertNamFolder(input, out, stimulusMode, [hwnd](const std::wstring& s) { postStatus(hwnd, s); }));
            PostMessageW(hwnd, WM_APP_DONE_BATCH, 0, reinterpret_cast<LPARAM>(result.release()));
        }).detach();
    }
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

    HWND sub = CreateWindowW(L"STATIC", L"Convert one NAM or batch-convert every NAM in a selected folder.",
                             WS_CHILD | WS_VISIBLE, 30, 60, 680, 24, hwnd, nullptr, nullptr, nullptr);
    applyFont(sub);

    HWND l1 = CreateWindowW(L"STATIC", L"Input NAM or folder", WS_CHILD | WS_VISIBLE, 30, 106, 190, 24, hwnd, nullptr, nullptr, nullptr);
    applyFont(l1);
    gInputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                 30, 132, 440, 32, hwnd, reinterpret_cast<HMENU>(IDC_INPUT_PATH), nullptr, nullptr);
    applyFont(gInputEdit);
    gLoadFileButton = CreateWindowW(L"BUTTON", L"Load NAM...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    482, 131, 105, 34, hwnd, reinterpret_cast<HMENU>(IDC_LOAD_FILE), nullptr, nullptr);
    applyFont(gLoadFileButton);
    gLoadFolderButton = CreateWindowW(L"BUTTON", L"Load Folder...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      597, 131, 120, 34, hwnd, reinterpret_cast<HMENU>(IDC_LOAD_FOLDER), nullptr, nullptr);
    applyFont(gLoadFolderButton);

    HWND l2 = CreateWindowW(L"STATIC", L"Output folder", WS_CHILD | WS_VISIBLE, 30, 190, 150, 24, hwnd, nullptr, nullptr, nullptr);
    applyFont(l2);
    gOutEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                               30, 216, 540, 32, hwnd, reinterpret_cast<HMENU>(IDC_OUTPUT_PATH), nullptr, nullptr);
    applyFont(gOutEdit);
    gBrowseButton = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  582, 215, 135, 34, hwnd, reinterpret_cast<HMENU>(IDC_BROWSE_OUTPUT), nullptr, nullptr);
    applyFont(gBrowseButton);

    HWND stimulusLabel = CreateWindowW(L"STATIC", L"Stimulus profile", WS_CHILD | WS_VISIBLE,
                                       30, 272, 180, 24, hwnd, nullptr, nullptr, nullptr);
    applyFont(stimulusLabel);
    gStimulusCombo = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        30, 298, 440, 180, hwnd, reinterpret_cast<HMENU>(IDC_STIMULUS_MODE), nullptr, nullptr);
    applyFont(gStimulusCombo);
    for (const auto mode : { ntc::StimulusMode::Legacy, ntc::StimulusMode::Clean,
                             ntc::StimulusMode::Dist }) {
        SendMessageW(gStimulusCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(ntc::stimulusModeDisplayName(mode)));
    }
    SendMessageW(gStimulusCombo, CB_SETCURSEL, 0, 0);

    HWND info = CreateWindowW(L"STATIC",
        L"Original preserves the validated v1.1 stimulus byte-for-byte. Clean/Dist reproduce the Sound Clone stimulus structure.\r\n"
        L"For each NAM: <name>_Ampero_2048.clo + <name>_GP200_1024.clo",
        WS_CHILD | WS_VISIBLE, 30, 345, 690, 55, hwnd, nullptr, nullptr, nullptr);
    applyFont(info);

    gConvertButton = CreateWindowW(L"BUTTON", L"Convert", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                   30, 418, 150, 42, hwnd, reinterpret_cast<HMENU>(IDC_CONVERT), nullptr, nullptr);
    applyFont(gConvertButton);
    gOpenButton = CreateWindowW(L"BUTTON", L"Open output folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                194, 418, 180, 42, hwnd, reinterpret_cast<HMENU>(IDC_OPEN_OUTPUT), nullptr, nullptr);
    applyFont(gOpenButton);

    gStatus = CreateWindowW(L"STATIC", L"Checking runtime...", WS_CHILD | WS_VISIBLE,
                            30, 488, 690, 42, hwnd, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);
    applyFont(gStatus);

    HWND ver = CreateWindowW(L"STATIC", L"Version 1.3.0", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                             500, 548, 217, 22, hwnd, nullptr, nullptr, nullptr);
    applyFont(ver);
    DragAcceptFiles(hwnd, TRUE);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        createUi(hwnd);
        std::string error;
        const auto rt = ntc::resolveDefaultRuntime();
        if (ntc::validateRuntime(rt, error)) setText(gStatus, L"Runtime ready. Load a NAM, load a folder, or drag one onto this window.");
        else setText(gStatus, L"Runtime missing: " + ntc::fromUtf8(error));
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_LOAD_FILE: chooseNam(hwnd); return 0;
        case IDC_LOAD_FOLDER: chooseNamFolder(hwnd); return 0;
        case IDC_BROWSE_OUTPUT: chooseOutput(hwnd); return 0;
        case IDC_CONVERT: startConversion(hwnd); return 0;
        case IDC_OPEN_OUTPUT: openOutputFolder(hwnd); return 0;
        default: break;
        }
        break;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[32768]{};
        if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path)))) {
            fs::path p(path);
            std::error_code ec;
            if (fs::is_directory(p, ec) && !ec) setNamFolder(p);
            else setSingleNam(p);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_APP_STATUS: {
        std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lParam));
        if (s) setText(gStatus, *s);
        return 0;
    }
    case WM_APP_DONE_SINGLE: {
        std::unique_ptr<ntc::ConversionResult> r(reinterpret_cast<ntc::ConversionResult*>(lParam));
        enableControls(true);
        if (r && r->ok) {
            std::wstring msg = L"Conversion complete.\r\n\r\nAmpero 2048:\r\n" + r->ampero2048.wstring()
                             + L"\r\n\r\nGP-200 1024:\r\n" + r->gp2001024.wstring();
            setText(gStatus, L"Done. Two CLO files were generated successfully.");
            MessageBoxW(hwnd, msg.c_str(), L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        } else {
            const std::wstring err = r ? ntc::fromUtf8(r->error) : L"Unknown conversion error.";
            setText(gStatus, L"Conversion failed.");
            MessageBoxW(hwnd, err.c_str(), L"Conversion failed", MB_ICONERROR | MB_OK);
        }
        return 0;
    }
    case WM_APP_DONE_BATCH: {
        std::unique_ptr<ntc::BatchConversionResult> r(reinterpret_cast<ntc::BatchConversionResult*>(lParam));
        enableControls(true);
        if (!r || r->total == 0) {
            setText(gStatus, L"Batch conversion did not find any NAM files.");
            MessageBoxW(hwnd, L"No .nam files were found in the selected folder.", L"Batch conversion", MB_ICONINFORMATION | MB_OK);
            return 0;
        }

        std::wstring msg = L"Batch conversion complete.\r\n\r\nProcessed: " + std::to_wstring(r->total)
                         + L"\r\nSucceeded: " + std::to_wstring(r->succeeded)
                         + L"\r\nFailed: " + std::to_wstring(r->failed);
        if (r->failed > 0) {
            msg += L"\r\n\r\nFailed files:";
            for (const auto& item : r->items) {
                if (!item.ok) {
                    msg += L"\r\n- " + item.inputNam.filename().wstring();
                    if (!item.error.empty()) msg += L": " + ntc::fromUtf8(item.error);
                }
            }
        }

        setText(gStatus, L"Batch done: " + std::to_wstring(r->succeeded) + L" succeeded, " + std::to_wstring(r->failed) + L" failed.");
        MessageBoxW(hwnd, msg.c_str(), L"NAM to CLO - Batch", (r->failed == 0 ? MB_ICONINFORMATION : MB_ICONWARNING) | MB_OK);
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

    HWND hwnd = CreateWindowExW(0, kClassName, L"NAM to CLO 1.2 Experimental",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 765, 625,
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
