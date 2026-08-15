#include "conversion.hpp"
#include "common.hpp"
#include "resource.h"

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
constexpr int IDC_TAIL_MODE = 110;
constexpr int IDC_RECORDED_PATH = 111;
constexpr int IDC_BROWSE_RECORDED = 112;
constexpr int IDC_VERSION = 113;
constexpr int IDC_SUBTITLE = 114;
constexpr int IDC_INFO = 115;
constexpr int IDC_CUSTOM_STIMULUS_PATH = 116;
constexpr int IDC_BROWSE_CUSTOM_STIMULUS = 117;
constexpr int IDC_APPLY_CORRECTIVE_IR = 118;
constexpr int IDC_CORRECTIVE_IR_PATH = 119;
constexpr int IDC_BROWSE_CORRECTIVE_IR = 120;
constexpr int IDC_REFINE_CLO = 121;

constexpr COLORREF kColorWindow = RGB(246, 248, 252);
constexpr COLORREF kColorCard = RGB(255, 255, 255);
constexpr COLORREF kColorBorder = RGB(220, 226, 235);
constexpr COLORREF kColorAccent = RGB(46, 115, 233);
constexpr COLORREF kColorAccentDark = RGB(33, 95, 204);
constexpr COLORREF kColorText = RGB(26, 31, 41);
constexpr COLORREF kColorSubtleText = RGB(88, 97, 112);
constexpr COLORREF kColorFooter = RGB(239, 243, 249);
constexpr COLORREF kColorInfo = RGB(244, 248, 255);
constexpr COLORREF kColorStatusOk = RGB(73, 193, 89);
constexpr COLORREF kColorDisabled = RGB(203, 210, 220);

enum class InputMode { None, SingleNam, Folder };

struct UiMetrics {
    RECT header{};
    RECT sectionInput{};
    RECT sectionOutput{};
    RECT sectionStimulus{};
    RECT sectionTail{};
    RECT sectionRecorded{};
    RECT sectionCorrective{};
    RECT sectionRefine{};
    RECT buttonArea{};
    RECT footer{};
    RECT infoBox{};
};

HWND gInputEdit = nullptr;
HWND gOutEdit = nullptr;
HWND gLoadFileButton = nullptr;
HWND gLoadFolderButton = nullptr;
HWND gBrowseButton = nullptr;
HWND gConvertButton = nullptr;
HWND gOpenButton = nullptr;
HWND gStatus = nullptr;
HWND gStimulusCombo = nullptr;
HWND gCustomStimulusEdit = nullptr;
HWND gBrowseCustomStimulusButton = nullptr;
HWND gTailCombo = nullptr;
HWND gRecordedEdit = nullptr;
HWND gBrowseRecordedButton = nullptr;
HWND gCorrectiveCheck = nullptr;
HWND gCorrectiveEdit = nullptr;
HWND gBrowseCorrectiveButton = nullptr;
HWND gRefineCheck = nullptr;
HWND gVersion = nullptr;
HWND gInfo = nullptr;
HWND gSubtitle = nullptr;
HFONT gFont = nullptr;
HFONT gTitleFont = nullptr;
HFONT gSubtitleFont = nullptr;
HFONT gSectionFont = nullptr;
HBRUSH gWindowBrush = nullptr;
HBRUSH gCardBrush = nullptr;
HBRUSH gFooterBrush = nullptr;
HBRUSH gInfoBrush = nullptr;
HBRUSH gStatusBrush = nullptr;
HBITMAP gLogoBitmap = nullptr;
HBITMAP gSectionIcons[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
UiMetrics gUi{};
bool gBusy = false;
InputMode gInputMode = InputMode::None;

std::wstring getText(HWND h) {
    const int len = GetWindowTextLengthW(h);
    std::wstring s(static_cast<std::size_t>(len) + 1, L'\0');
    if (len) GetWindowTextW(h, s.data(), len + 1);
    s.resize(static_cast<std::size_t>(len));
    return s;
}

void setText(HWND h, const std::wstring& s) {
    SetWindowTextW(h, s.c_str());
    if (h == gStatus || h == gVersion) {
        InvalidateRect(h, nullptr, TRUE);
        UpdateWindow(h);
    }
}

HMENU controlId(int id) { return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)); }

void safeDeleteObject(HGDIOBJ obj) {
    if (obj) DeleteObject(obj);
}

void createResources() {
    gFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gTitleFont = CreateFontW(-40, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gSubtitleFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gSectionFont = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    gWindowBrush = CreateSolidBrush(kColorWindow);
    gCardBrush = CreateSolidBrush(kColorCard);
    gFooterBrush = CreateSolidBrush(kColorFooter);
    gInfoBrush = CreateSolidBrush(kColorInfo);
    gStatusBrush = CreateSolidBrush(kColorStatusOk);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    gLogoBitmap = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_LOGO));
    gSectionIcons[0] = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_ICON_INPUT));
    gSectionIcons[1] = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_ICON_OUTPUT));
    gSectionIcons[2] = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_ICON_STIMULUS));
    gSectionIcons[3] = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_ICON_REAMP));
    gSectionIcons[4] = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_ICON_RECORDED));
}

void destroyResources() {
    safeDeleteObject(gFont);
    safeDeleteObject(gTitleFont);
    safeDeleteObject(gSubtitleFont);
    safeDeleteObject(gSectionFont);
    safeDeleteObject(gWindowBrush);
    safeDeleteObject(gCardBrush);
    safeDeleteObject(gFooterBrush);
    safeDeleteObject(gInfoBrush);
    safeDeleteObject(gStatusBrush);
    safeDeleteObject(gLogoBitmap);
    gLogoBitmap = nullptr;
    for (auto& icon : gSectionIcons) {
        safeDeleteObject(icon);
        icon = nullptr;
    }
    gFont = gTitleFont = gSubtitleFont = gSectionFont = nullptr;
    gWindowBrush = gCardBrush = gFooterBrush = gInfoBrush = gStatusBrush = nullptr;
}

void applyFont(HWND h, HFONT font) {
    if (h && font) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void applyFont(HWND h) { applyFont(h, gFont); }

void enableControls(bool enable) {
    gBusy = !enable;
    EnableWindow(gLoadFileButton, enable);
    EnableWindow(gLoadFolderButton, enable);
    EnableWindow(gBrowseButton, enable);
    EnableWindow(gConvertButton, enable);
    EnableWindow(gOpenButton, enable);
    EnableWindow(gStimulusCombo, enable);
    EnableWindow(gTailCombo, enable);
    EnableWindow(gCorrectiveCheck, enable);
    EnableWindow(gRefineCheck, enable);
    if (!enable) {
        EnableWindow(gCustomStimulusEdit, FALSE);
        EnableWindow(gBrowseCustomStimulusButton, FALSE);
    }
    if (!enable) {
        EnableWindow(gRecordedEdit, FALSE);
        EnableWindow(gBrowseRecordedButton, FALSE);
        EnableWindow(gCorrectiveEdit, FALSE);
        EnableWindow(gBrowseCorrectiveButton, FALSE);
    }
    InvalidateRect(GetParent(gConvertButton), nullptr, FALSE);
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
    case 3: return ntc::StimulusMode::Custom;
    default: return ntc::StimulusMode::Legacy;
    }
}

void chooseCustomStimulus(HWND owner) {
    wchar_t file[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"WAV audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"wav";
    if (GetOpenFileNameW(&ofn)) setText(gCustomStimulusEdit, fs::path(file).wstring());
}

void chooseRecordedAudio(HWND owner) {
    wchar_t file[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"WAV audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"wav";
    if (GetOpenFileNameW(&ofn)) setText(gRecordedEdit, fs::path(file).wstring());
}

void chooseCorrectiveIr(HWND owner) {
    wchar_t file[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"WAV impulse response (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"wav";
    if (GetOpenFileNameW(&ofn)) setText(gCorrectiveEdit, fs::path(file).wstring());
}

ntc::TailMode selectedTailMode() {
    return SendMessageW(gTailCombo, CB_GETCURSEL, 0, 0) == 1
        ? ntc::TailMode::RecordedAudio
        : ntc::TailMode::PresetAudio;
}

void updateTailControls() {
    if (gBusy) return;
    const ntc::StimulusMode mode = selectedStimulusMode();
    const bool customStimulus = mode == ntc::StimulusMode::Custom;
    EnableWindow(gCustomStimulusEdit, customStimulus ? TRUE : FALSE);
    EnableWindow(gBrowseCustomStimulusButton, customStimulus ? TRUE : FALSE);

    // v1.8: Tail selection applies to every stimulus profile, including Legacy.
    EnableWindow(gTailCombo, TRUE);
    const bool recorded = selectedTailMode() == ntc::TailMode::RecordedAudio;
    EnableWindow(gRecordedEdit, recorded ? TRUE : FALSE);
    EnableWindow(gBrowseRecordedButton, recorded ? TRUE : FALSE);

    const bool correctiveEnabled = SendMessageW(gCorrectiveCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(gCorrectiveEdit, correctiveEnabled ? TRUE : FALSE);
    EnableWindow(gBrowseCorrectiveButton, correctiveEnabled ? TRUE : FALSE);
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

    ntc::StimulusConfig stimulus;
    stimulus.mode = selectedStimulusMode();
    stimulus.customStimulus = fs::path(getText(gCustomStimulusEdit));
    stimulus.tailMode = selectedTailMode();
    stimulus.recordedAudio = fs::path(getText(gRecordedEdit));
    if (stimulus.mode == ntc::StimulusMode::Custom && stimulus.customStimulus.empty()) {
        MessageBoxW(hwnd, L"Select a Custom Stimulus WAV file.", L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (stimulus.tailMode == ntc::TailMode::RecordedAudio
        && stimulus.recordedAudio.empty()) {
        MessageBoxW(hwnd, L"Select a Recorded Audio WAV file.", L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        return;
    }

    ntc::CorrectiveIrConfig correction;
    correction.enabled = SendMessageW(gCorrectiveCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    correction.wav = fs::path(getText(gCorrectiveEdit));
    if (correction.enabled && correction.wav.empty()) {
        MessageBoxW(hwnd, L"Select a Corrective IR WAV file.", L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        return;
    }

    ntc::CloRefineConfig refine;
    refine.enabled = SendMessageW(gRefineCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    refine.passes = 4;

    enableControls(false);
    if (gInputMode == InputMode::SingleNam) {
        setText(gStatus, L"Starting conversion...");
        std::thread([hwnd, input, out, stimulus, correction, refine] {
            auto result = std::make_unique<ntc::ConversionResult>(
                ntc::convertNamToBoth(input, out, stimulus, correction, refine, [hwnd](const std::wstring& s) { postStatus(hwnd, s); }));
            PostMessageW(hwnd, WM_APP_DONE_SINGLE, 0, reinterpret_cast<LPARAM>(result.release()));
        }).detach();
    } else {
        setText(gStatus, L"Starting batch conversion...");
        std::thread([hwnd, input, out, stimulus, correction, refine] {
            auto result = std::make_unique<ntc::BatchConversionResult>(
                ntc::convertNamFolder(input, out, stimulus, correction, refine, [hwnd](const std::wstring& s) { postStatus(hwnd, s); }));
            PostMessageW(hwnd, WM_APP_DONE_BATCH, 0, reinterpret_cast<LPARAM>(result.release()));
        }).detach();
    }
}

void openOutputFolder(HWND hwnd) {
    const std::wstring out = getText(gOutEdit);
    if (out.empty()) return;
    ShellExecuteW(hwnd, L"open", out.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void moveCtrl(HWND h, int x, int y, int w, int hgt) {
    if (h) MoveWindow(h, x, y, w, hgt, TRUE);
}

void computeLayout(int clientW, int clientH) {
    const int margin = 24;
    const int gap = 7;
    const int footerH = 38;

    gUi.header = RECT{ margin, 12, clientW - margin, 98 };

    int y = 100;
    gUi.sectionInput = RECT{ margin, y, clientW - margin, y + 76 }; y += 76 + gap;
    gUi.sectionOutput = RECT{ margin, y, clientW - margin, y + 70 }; y += 70 + gap;
    gUi.sectionStimulus = RECT{ margin, y, clientW - margin, y + 105 }; y += 105 + gap;
    gUi.sectionTail = RECT{ margin, y, clientW - margin, y + 66 }; y += 66 + gap;
    gUi.sectionRecorded = RECT{ margin, y, clientW - margin, y + 105 }; y += 105 + gap;
    gUi.sectionCorrective = RECT{ margin, y, clientW - margin, y + 86 }; y += 86 + gap;
    gUi.sectionRefine = RECT{ margin, y, clientW - margin, y + 68 }; y += 68 + gap;
    gUi.buttonArea = RECT{ margin, y, clientW - margin, y + 38 };
    gUi.footer = RECT{ 0, clientH - footerH, clientW, clientH };
    gUi.infoBox = RECT{ gUi.sectionRecorded.left + 108, gUi.sectionRecorded.top + 62,
                        gUi.sectionRecorded.right - 16, gUi.sectionRecorded.top + 96 };
}

void layoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    computeLayout(rc.right - rc.left, rc.bottom - rc.top);

    const int contentX = gUi.sectionInput.left + 108;
    const int sectionRightInset = 16;

    HWND title = GetDlgItem(hwnd, 1001);
    if (title) moveCtrl(title, 118, 18, 340, 42);
    if (gSubtitle) moveCtrl(gSubtitle, 120, 62, rc.right - 150, 22);

    // Keep a visible gap between each label and its field/control.
    moveCtrl(GetDlgItem(hwnd, 1002), contentX, gUi.sectionInput.top + 8, 230, 20);
    const int inputButtonX = gUi.sectionInput.right - sectionRightInset - 244;
    const int inputEditW = inputButtonX - 12 - contentX;
    moveCtrl(gInputEdit, contentX, gUi.sectionInput.top + 35, inputEditW, 28);
    moveCtrl(gLoadFileButton, inputButtonX, gUi.sectionInput.top + 31, 116, 32);
    moveCtrl(gLoadFolderButton, gUi.sectionInput.right - sectionRightInset - 120, gUi.sectionInput.top + 31, 116, 32);

    moveCtrl(GetDlgItem(hwnd, 1003), contentX, gUi.sectionOutput.top + 7, 170, 20);
    const int outputEditW = (gUi.sectionOutput.right - sectionRightInset) - (contentX + 142);
    moveCtrl(gOutEdit, contentX, gUi.sectionOutput.top + 33, outputEditW, 28);
    moveCtrl(gBrowseButton, gUi.sectionOutput.right - sectionRightInset - 112, gUi.sectionOutput.top + 29, 112, 32);

    moveCtrl(GetDlgItem(hwnd, 1004), contentX, gUi.sectionStimulus.top + 6, 190, 20);
    moveCtrl(gStimulusCombo, contentX, gUi.sectionStimulus.top + 32, 490, 180);
    moveCtrl(GetDlgItem(hwnd, 1007), contentX, gUi.sectionStimulus.top + 60, 360, 20);
    const int customEditW = (gUi.sectionStimulus.right - sectionRightInset) - (contentX + 150);
    moveCtrl(gCustomStimulusEdit, contentX, gUi.sectionStimulus.top + 80, customEditW, 28);
    moveCtrl(gBrowseCustomStimulusButton, gUi.sectionStimulus.right - sectionRightInset - 124,
             gUi.sectionStimulus.top + 76, 124, 32);

    moveCtrl(GetDlgItem(hwnd, 1005), contentX, gUi.sectionTail.top + 6, 210, 20);
    moveCtrl(gTailCombo, contentX, gUi.sectionTail.top + 32, 490, 180);

    moveCtrl(GetDlgItem(hwnd, 1006), contentX, gUi.sectionRecorded.top + 6, 430, 20);
    const int recordedEditW = (gUi.sectionRecorded.right - sectionRightInset) - (contentX + 150);
    moveCtrl(gRecordedEdit, contentX, gUi.sectionRecorded.top + 32, recordedEditW, 28);
    moveCtrl(gBrowseRecordedButton, gUi.sectionRecorded.right - sectionRightInset - 124, gUi.sectionRecorded.top + 28, 124, 32);
    moveCtrl(gInfo, gUi.infoBox.left + 36, gUi.infoBox.top + 5,
             (gUi.infoBox.right - gUi.infoBox.left) - 44, 24);

    moveCtrl(GetDlgItem(hwnd, 1008), contentX, gUi.sectionCorrective.top + 8, 180, 22);
    moveCtrl(gCorrectiveCheck, contentX, gUi.sectionCorrective.top + 31, 170, 24);
    const int correctiveEditX = contentX + 180;
    const int correctiveEditW = (gUi.sectionCorrective.right - sectionRightInset - 124 - 8) - correctiveEditX;
    moveCtrl(gCorrectiveEdit, correctiveEditX, gUi.sectionCorrective.top + 29, correctiveEditW, 28);
    moveCtrl(gBrowseCorrectiveButton, gUi.sectionCorrective.right - sectionRightInset - 124, gUi.sectionCorrective.top + 27, 124, 32);

    moveCtrl(GetDlgItem(hwnd, 1009), contentX, gUi.sectionRefine.top + 7, 280, 22);
    moveCtrl(gRefineCheck, contentX, gUi.sectionRefine.top + 34, 560, 24);

    const int center = rc.right / 2;
    moveCtrl(gConvertButton, center - 222, gUi.buttonArea.top, 200, 36);
    moveCtrl(gOpenButton, center - 10, gUi.buttonArea.top, 200, 36);

    moveCtrl(gStatus, 44, gUi.footer.top + 8, rc.right - 220, 22);
    moveCtrl(gVersion, rc.right - 140, gUi.footer.top + 8, 110, 22);
}

void createSectionLabel(HWND hwnd, int id, const wchar_t* text) {
    HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                           0, 0, 100, 24, hwnd, controlId(id), nullptr, nullptr);
    applyFont(h, gSectionFont);
}

void createUi(HWND hwnd) {
    createResources();

    const std::wstring appHeader = std::wstring(L"NAM to CLO ") + ntc::kVersion;
    HWND title = CreateWindowW(L"STATIC", appHeader.c_str(), WS_CHILD | WS_VISIBLE,
                               0, 0, 100, 30, hwnd, controlId(1001), nullptr, nullptr);
    applyFont(title, gTitleFont);

    gSubtitle = CreateWindowW(L"STATIC", L"Convert one NAM or batch-convert every NAM in a selected folder.",
                              WS_CHILD | WS_VISIBLE, 0, 0, 100, 20, hwnd,
                              controlId(IDC_SUBTITLE), nullptr, nullptr);
    applyFont(gSubtitle, gSubtitleFont);

    createSectionLabel(hwnd, 1002, L"Input NAM or folder");
    createSectionLabel(hwnd, 1003, L"Output folder");
    createSectionLabel(hwnd, 1004, L"Stimulus profile");
    createSectionLabel(hwnd, 1007, L"Custom stimulus WAV (adapted automatically to 50.000 s)");
    createSectionLabel(hwnd, 1005, L"Tail / Reamp source");
    createSectionLabel(hwnd, 1006, L"Recorded WAV (adapted automatically to 20.000 s)");
    createSectionLabel(hwnd, 1008, L"Corrective IR");
    createSectionLabel(hwnd, 1009, L"CLO refinement v2.1 (A + P/K + spectral guard)");

    gInputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                 0, 0, 100, 30, hwnd, controlId(IDC_INPUT_PATH), nullptr, nullptr);
    applyFont(gInputEdit);
    gLoadFileButton = CreateWindowW(L"BUTTON", L"Load NAM...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    0, 0, 110, 34, hwnd, controlId(IDC_LOAD_FILE), nullptr, nullptr);
    gLoadFolderButton = CreateWindowW(L"BUTTON", L"Load Folder...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                      0, 0, 120, 34, hwnd, controlId(IDC_LOAD_FOLDER), nullptr, nullptr);

    gOutEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                               0, 0, 100, 30, hwnd, controlId(IDC_OUTPUT_PATH), nullptr, nullptr);
    applyFont(gOutEdit);
    gBrowseButton = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                  0, 0, 120, 34, hwnd, controlId(IDC_BROWSE_OUTPUT), nullptr, nullptr);

    gStimulusCombo = CreateWindowW(L"COMBOBOX", L"",
                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                   0, 0, 100, 120, hwnd, controlId(IDC_STIMULUS_MODE), nullptr, nullptr);
    applyFont(gStimulusCombo);
    for (const auto mode : { ntc::StimulusMode::Legacy, ntc::StimulusMode::Clean, ntc::StimulusMode::Dist, ntc::StimulusMode::Custom }) {
        SendMessageW(gStimulusCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(ntc::stimulusModeDisplayName(mode)));
    }
    SendMessageW(gStimulusCombo, CB_SETCURSEL, 0, 0);

    gCustomStimulusEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
        0, 0, 100, 30, hwnd, controlId(IDC_CUSTOM_STIMULUS_PATH), nullptr, nullptr);
    applyFont(gCustomStimulusEdit);
    gBrowseCustomStimulusButton = CreateWindowW(L"BUTTON", L"Browse WAV...",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 120, 34, hwnd, controlId(IDC_BROWSE_CUSTOM_STIMULUS), nullptr, nullptr);

    gTailCombo = CreateWindowW(L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                               0, 0, 100, 120, hwnd, controlId(IDC_TAIL_MODE), nullptr, nullptr);
    applyFont(gTailCombo);
    for (const auto mode : { ntc::TailMode::PresetAudio, ntc::TailMode::RecordedAudio }) {
        SendMessageW(gTailCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(ntc::tailModeDisplayName(mode)));
    }
    SendMessageW(gTailCombo, CB_SETCURSEL, 0, 0);

    gRecordedEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                    0, 0, 100, 30, hwnd, controlId(IDC_RECORDED_PATH), nullptr, nullptr);
    applyFont(gRecordedEdit);
    gBrowseRecordedButton = CreateWindowW(L"BUTTON", L"Browse WAV...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          0, 0, 120, 34, hwnd, controlId(IDC_BROWSE_RECORDED), nullptr, nullptr);

    gCorrectiveCheck = CreateWindowW(L"BUTTON", L"Apply corrective IR",
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     0, 0, 170, 24, hwnd, controlId(IDC_APPLY_CORRECTIVE_IR), nullptr, nullptr);
    applyFont(gCorrectiveCheck);
    gCorrectiveEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                      0, 0, 100, 30, hwnd, controlId(IDC_CORRECTIVE_IR_PATH), nullptr, nullptr);
    applyFont(gCorrectiveEdit);
    gBrowseCorrectiveButton = CreateWindowW(L"BUTTON", L"Browse WAV...",
                                             WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                             0, 0, 120, 34, hwnd, controlId(IDC_BROWSE_CORRECTIVE_IR), nullptr, nullptr);

    gRefineCheck = CreateWindowW(L"BUTTON", L"Refine Block A + P/K against NAM render (slow, experimental)",
                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                 0, 0, 520, 24, hwnd, controlId(IDC_REFINE_CLO), nullptr, nullptr);
    applyFont(gRefineCheck);

    gInfo = CreateWindowW(L"STATIC",
                          L"CLO files will be created as Mono, PCM16, 44.1 kHz.\r\n"
                          L"Audio will be trimmed or padded to exactly 20.000 seconds.",
                          WS_CHILD | WS_VISIBLE,
                          0, 0, 100, 40, hwnd, controlId(IDC_INFO), nullptr, nullptr);
    applyFont(gInfo);

    gConvertButton = CreateWindowW(L"BUTTON", L"Convert", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   0, 0, 150, 42, hwnd, controlId(IDC_CONVERT), nullptr, nullptr);
    applyFont(gConvertButton);
    gOpenButton = CreateWindowW(L"BUTTON", L"Open output folder", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                0, 0, 180, 42, hwnd, controlId(IDC_OPEN_OUTPUT), nullptr, nullptr);
    applyFont(gOpenButton);

    gStatus = CreateWindowW(L"STATIC", L"Checking runtime...", WS_CHILD | WS_VISIBLE,
                            0, 0, 100, 22, hwnd, controlId(IDC_STATUS), nullptr, nullptr);
    applyFont(gStatus);

    const std::wstring versionLabel = std::wstring(L"Version ") + ntc::kVersion;
    gVersion = CreateWindowW(L"STATIC", versionLabel.c_str(), WS_CHILD | WS_VISIBLE | SS_RIGHT,
                             0, 0, 110, 22, hwnd, controlId(IDC_VERSION), nullptr, nullptr);
    applyFont(gVersion);

    layoutControls(hwnd);
    updateTailControls();
    DragAcceptFiles(hwnd, TRUE);
}

void drawRoundedRect(HDC hdc, const RECT& rc, COLORREF fill, COLORREF border, int radius = 18) {
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void fillRect(HDC hdc, const RECT& rc, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
}

void drawBitmap(HDC hdc, HBITMAP bitmap, int x, int y) {
    if (!bitmap) return;
    BITMAP bm{};
    GetObjectW(bitmap, sizeof(bm), &bm);
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, bitmap);
    BitBlt(hdc, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

void drawSectionIcon(HDC hdc, const RECT& rc, int kind) {
    if (kind < 0 || kind >= 5 || !gSectionIcons[kind]) return;
    BITMAP bm{};
    GetObjectW(gSectionIcons[kind], sizeof(bm), &bm);
    const int x = rc.left + ((rc.right - rc.left) - bm.bmWidth) / 2;
    const int y = rc.top + ((rc.bottom - rc.top) - bm.bmHeight) / 2;
    drawBitmap(hdc, gSectionIcons[kind], x, y);
}

void drawSectionCard(HDC hdc, const RECT& rc, int iconKind) {
    drawRoundedRect(hdc, rc, kColorCard, kColorBorder, 18);
    RECT iconRect{ rc.left + 14, rc.top + 9, rc.left + 66, rc.top + 61 };
    drawSectionIcon(hdc, iconRect, iconKind);
}

void drawInfoBox(HDC hdc) {
    drawRoundedRect(hdc, gUi.infoBox, kColorInfo, RGB(210, 223, 247), 12);
    RECT iconRc{ gUi.infoBox.left + 14, gUi.infoBox.top + 12, gUi.infoBox.left + 34, gUi.infoBox.top + 32 };
    HPEN pen = CreatePen(PS_SOLID, 2, kColorAccent);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Ellipse(hdc, iconRc.left, iconRc.top, iconRc.right, iconRc.bottom);
    MoveToEx(hdc, (iconRc.left + iconRc.right) / 2, iconRc.top + 8, nullptr);
    LineTo(hdc, (iconRc.left + iconRc.right) / 2, iconRc.bottom - 7);
    MoveToEx(hdc, (iconRc.left + iconRc.right) / 2, iconRc.top + 4, nullptr);
    LineTo(hdc, (iconRc.left + iconRc.right) / 2 + 1, iconRc.top + 4);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void paintBackground(HWND hwnd, HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    fillRect(hdc, rc, kColorWindow);

    drawBitmap(hdc, gLogoBitmap, 28, 18);

    drawSectionCard(hdc, gUi.sectionInput, 0);
    drawSectionCard(hdc, gUi.sectionOutput, 1);
    drawSectionCard(hdc, gUi.sectionStimulus, 2);
    drawSectionCard(hdc, gUi.sectionTail, 3);
    drawSectionCard(hdc, gUi.sectionRecorded, 4);
    drawSectionCard(hdc, gUi.sectionCorrective, 4);
    drawSectionCard(hdc, gUi.sectionRefine, 2);
    drawInfoBox(hdc);
    fillRect(hdc, gUi.footer, kColorFooter);

    RECT statusDot{ 18, gUi.footer.top + 9, 32, gUi.footer.top + 23 };
    HBRUSH dotBrush = CreateSolidBrush(kColorStatusOk);
    HGDIOBJ oldBrush = SelectObject(hdc, dotBrush);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, statusDot.left, statusDot.top, statusDot.right, statusDot.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(dotBrush);
}

void drawButton(DRAWITEMSTRUCT* dis) {
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const int id = static_cast<int>(dis->CtlID);
    const bool primary = id == IDC_CONVERT;

    COLORREF fill = primary ? (selected ? kColorAccentDark : kColorAccent) : kColorCard;
    COLORREF border = primary ? (selected ? kColorAccentDark : kColorAccentDark) : kColorAccent;
    COLORREF text = primary ? RGB(255, 255, 255) : kColorAccentDark;
    if (disabled) {
        fill = primary ? kColorDisabled : RGB(247, 248, 250);
        border = RGB(208, 214, 224);
        text = RGB(145, 152, 164);
    }

    RECT rc = dis->rcItem;
    drawRoundedRect(dis->hDC, rc, fill, border, 16);

    std::wstring label = getText(dis->hwndItem);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text);
    SelectObject(dis->hDC, gSectionFont ? gSectionFont : gFont);

    if (primary) {
        POINT pts[3] = {
            { rc.left + 34, rc.top + 14 },
            { rc.left + 34, rc.bottom - 14 },
            { rc.left + 50, (rc.top + rc.bottom) / 2 }
        };
        HBRUSH triBrush = CreateSolidBrush(text);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, triBrush);
        HGDIOBJ oldPen = SelectObject(dis->hDC, GetStockObject(NULL_PEN));
        Polygon(dis->hDC, pts, 3);
        SelectObject(dis->hDC, oldPen);
        SelectObject(dis->hDC, oldBrush);
        DeleteObject(triBrush);
        rc.left += 60;
    }

    DrawTextW(dis->hDC, label.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if ((dis->itemState & ODS_FOCUS) != 0) {
        RECT focus = dis->rcItem;
        InflateRect(&focus, -5, -5);
        DrawFocusRect(dis->hDC, &focus);
    }
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
    case WM_SIZE:
        layoutControls(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        HWND ctrl = reinterpret_cast<HWND>(lParam);
        if (ctrl == gStatus || ctrl == gVersion) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, kColorFooter);
            SetTextColor(hdc, kColorSubtleText);
            return reinterpret_cast<LRESULT>(gFooterBrush);
        }
        if (ctrl == gInfo) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, kColorInfo);
            SetTextColor(hdc, kColorSubtleText);
            return reinterpret_cast<LRESULT>(gInfoBrush);
        }
        if (ctrl == gSubtitle || ctrl == GetDlgItem(hwnd, 1001)
            || ctrl == GetDlgItem(hwnd, 1002) || ctrl == GetDlgItem(hwnd, 1003) || ctrl == GetDlgItem(hwnd, 1004)
            || ctrl == GetDlgItem(hwnd, 1005) || ctrl == GetDlgItem(hwnd, 1006) || ctrl == GetDlgItem(hwnd, 1007)
            || ctrl == GetDlgItem(hwnd, 1008) || ctrl == GetDlgItem(hwnd, 1009)) {
            SetTextColor(hdc, ctrl == gSubtitle ? kColorSubtleText : kColorText);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        break;
    }
    case WM_DRAWITEM:
        drawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        paintBackground(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_LOAD_FILE: chooseNam(hwnd); return 0;
        case IDC_LOAD_FOLDER: chooseNamFolder(hwnd); return 0;
        case IDC_BROWSE_OUTPUT: chooseOutput(hwnd); return 0;
        case IDC_BROWSE_RECORDED: chooseRecordedAudio(hwnd); return 0;
        case IDC_BROWSE_CUSTOM_STIMULUS: chooseCustomStimulus(hwnd); return 0;
        case IDC_BROWSE_CORRECTIVE_IR: chooseCorrectiveIr(hwnd); return 0;
        case IDC_APPLY_CORRECTIVE_IR:
            if (HIWORD(wParam) == BN_CLICKED) updateTailControls();
            return 0;
        case IDC_STIMULUS_MODE:
            if (HIWORD(wParam) == CBN_SELCHANGE) updateTailControls();
            return 0;
        case IDC_TAIL_MODE:
            if (HIWORD(wParam) == CBN_SELCHANGE) updateTailControls();
            return 0;
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
        updateTailControls();
        if (r && r->ok) {
            std::wstring resultMessage = L"Conversion complete.\r\n\r\nAmpero 2048:\r\n" + r->ampero2048.wstring()
                                      + L"\r\n\r\nGP-200 1024:\r\n" + r->gp2001024.wstring();
            if (!r->refinedAmpero2048.empty()) {
                resultMessage += L"\r\n\r\nBEST A+P/K Ampero 2048 (audition candidate):\r\n" + r->bestAmpero2048.wstring()
                              + L"\r\n\r\nBEST A+P/K GP-200 1024 (audition candidate):\r\n" + r->bestGp2001024.wstring()
                              + L"\r\n\r\nRefined Ampero 2048:\r\n" + r->refinedAmpero2048.wstring()
                              + L"\r\n\r\nRefined GP-200 1024:\r\n" + r->refinedGp2001024.wstring()
                              + L"\r\n\r\nA+P/K full-render NMSE improvement: "
                              + std::to_wstring(r->refineStats.improvementPercent) + L"%"
                              + L"\r\nStimulus (first 50 s): "
                              + std::to_wstring(r->refineStats.stimulusImprovementPercent) + L"%"
                              + L"\r\nTail (remaining audio): "
                              + std::to_wstring(r->refineStats.tailImprovementPercent) + L"%"
                              + L"\r\nMR-STFT (512/2048/8192): "
                              + std::to_wstring(r->refineStats.spectralImprovementPercent) + L"%"
                              + L"\r\nEnvelope RMS (256/2048/8192): "
                              + std::to_wstring(r->refineStats.envelopeImprovementPercent) + L"%"
                              + L"\r\n\r\n--- v2.1 A + P/K + spectral-profile diagnostics ---"
                              + L"\r\nBest searched candidate: "
                              + std::wstring(r->refineStats.searchedCandidateAccepted ? L"ACCEPTED" : L"REJECTED")
                              + L"\r\nCombined A+P/K loss improvement: "
                              + std::to_wstring(r->refineStats.searchedCompositeImprovementPercent) + L"%"
                              + L"\r\nBest searched NMSE improvement: "
                              + std::to_wstring(r->refineStats.searchedNmseImprovementPercent) + L"%"
                              + L"\r\nBest searched stimulus improvement: "
                              + std::to_wstring(r->refineStats.searchedStimulusImprovementPercent) + L"%"
                              + L"\r\nBest searched tail improvement: "
                              + std::to_wstring(r->refineStats.searchedTailImprovementPercent) + L"%"
                              + L"\r\nBest searched MR-STFT improvement: "
                              + std::to_wstring(r->refineStats.searchedSpectralImprovementPercent) + L"%"
                              + L"\r\nBest searched transfer-profile improvement: "
                              + std::to_wstring(r->refineStats.searchedResponseSpectralImprovementPercent) + L"%"
                              + L"\r\nBest searched envelope improvement: "
                              + std::to_wstring(r->refineStats.searchedEnvelopeImprovementPercent) + L"%"
                              + L"\r\nLevel-balanced temporal improvement: "
                              + std::to_wstring(r->refineStats.searchedLevelBalancedImprovementPercent) + L"%"
                              + L"\r\n  Low excitation NMSE improvement: "
                              + std::to_wstring(r->refineStats.searchedLowLevelImprovementPercent) + L"%"
                              + L"\r\n  Mid excitation NMSE improvement: "
                              + std::to_wstring(r->refineStats.searchedMidLevelImprovementPercent) + L"%"
                              + L"\r\n  High excitation NMSE improvement: "
                              + std::to_wstring(r->refineStats.searchedHighLevelImprovementPercent) + L"%"
                              + L"\r\nBest searched P/K (A also optimized): "
                              + std::to_wstring(r->refineStats.searchedPPos) + L" / "
                              + std::to_wstring(r->refineStats.searchedPNeg) + L" / "
                              + std::to_wstring(r->refineStats.searchedKPos) + L" / "
                              + std::to_wstring(r->refineStats.searchedKNeg)
                              + L"\r\n\r\nAbsolute metrics (original -> BEST):"
                              + L"\r\nNMSE: " + std::to_wstring(r->refineStats.originalNmse) + L" -> " + std::to_wstring(r->refineStats.searchedNmse)
                              + L"\r\nMR-STFT: " + std::to_wstring(r->refineStats.originalSpectralError) + L" -> " + std::to_wstring(r->refineStats.searchedSpectralError)
                              + L"\r\nTransfer-profile dB-shape MAE: " + std::to_wstring(r->refineStats.originalResponseSpectralError) + L" -> " + std::to_wstring(r->refineStats.searchedResponseSpectralError)
                              + L"\r\nEnvelope RMS dB error: " + std::to_wstring(r->refineStats.originalEnvelopeError) + L" -> " + std::to_wstring(r->refineStats.searchedEnvelopeError)
                              + L"\r\nLow-level NMSE: " + std::to_wstring(r->refineStats.originalLowLevelNmse) + L" -> " + std::to_wstring(r->refineStats.searchedLowLevelNmse)
                              + L"\r\nMid-level NMSE: " + std::to_wstring(r->refineStats.originalMidLevelNmse) + L" -> " + std::to_wstring(r->refineStats.searchedMidLevelNmse)
                              + L"\r\nHigh-level NMSE: " + std::to_wstring(r->refineStats.originalHighLevelNmse) + L" -> " + std::to_wstring(r->refineStats.searchedHighLevelNmse)
                              + L"\r\nDecision: " + ntc::fromUtf8(r->refineStats.searchedDecisionReason)
                              + L"\r\n\r\nNote: v2.1 jointly optimizes Block A and P/K, but _BEST/_REFINE are taken only from candidates that preserve the input-referenced 30 Hz-20 kHz transfer-magnitude contour (96 log-frequency bands, max +0.10% profile-error regression). PRE, POST and B remain fixed.";
            }
            setText(gStatus, L"Done. Two CLO files were generated successfully.");
            const std::wstring doneTitle = std::wstring(L"NAM to CLO ") + ntc::kVersion;
            MessageBoxW(hwnd, resultMessage.c_str(), doneTitle.c_str(), MB_ICONINFORMATION | MB_OK);
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
        updateTailControls();
        if (!r || r->total == 0) {
            setText(gStatus, L"Batch conversion did not find any NAM files.");
            MessageBoxW(hwnd, L"No .nam files were found in the selected folder.", L"Batch conversion", MB_ICONINFORMATION | MB_OK);
            return 0;
        }

        std::wstring resultMessage = L"Batch conversion complete.\r\n\r\nProcessed: " + std::to_wstring(r->total)
                                   + L"\r\nSucceeded: " + std::to_wstring(r->succeeded)
                                   + L"\r\nFailed: " + std::to_wstring(r->failed);
        if (r->failed > 0) {
            resultMessage += L"\r\n\r\nFailed files:";
            for (const auto& item : r->items) {
                if (!item.ok) {
                    resultMessage += L"\r\n- " + item.inputNam.filename().wstring();
                    if (!item.error.empty()) resultMessage += L": " + ntc::fromUtf8(item.error);
                }
            }
        }

        setText(gStatus, L"Batch done: " + std::to_wstring(r->succeeded) + L" succeeded, " + std::to_wstring(r->failed) + L" failed.");
        MessageBoxW(hwnd, resultMessage.c_str(), L"NAM to CLO - Batch", (r->failed == 0 ? MB_ICONINFORMATION : MB_ICONWARNING) | MB_OK);
        return 0;
    }
    case WM_CLOSE:
        if (gBusy) {
            if (MessageBoxW(hwnd, L"A conversion is running. Close anyway?", L"NAM to CLO", MB_ICONWARNING | MB_YESNO) != IDYES) return 0;
        }
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        destroyResources();
        PostQuitMessage(0); return 0;
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

    // Prevent Windows DPI virtualization from inflating the whole window on 125%/150% displays.
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = wndProc;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    const std::wstring windowTitle = std::wstring(L"NAM to CLO ") + ntc::kVersion;
    HWND hwnd = CreateWindowExW(0, kClassName, windowTitle.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1040, 870,
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
