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
HWND gTailCombo = nullptr;
HWND gRecordedEdit = nullptr;
HWND gBrowseRecordedButton = nullptr;
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

void setText(HWND h, const std::wstring& s) { SetWindowTextW(h, s.c_str()); }

void safeDeleteObject(HGDIOBJ obj) {
    if (obj) DeleteObject(obj);
}

void createResources() {
    gFont = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gTitleFont = CreateFontW(-54, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gSubtitleFont = CreateFontW(-22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gSectionFont = CreateFontW(-21, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    gWindowBrush = CreateSolidBrush(kColorWindow);
    gCardBrush = CreateSolidBrush(kColorCard);
    gFooterBrush = CreateSolidBrush(kColorFooter);
    gInfoBrush = CreateSolidBrush(kColorInfo);
    gStatusBrush = CreateSolidBrush(kColorStatusOk);
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
    if (!enable) {
        EnableWindow(gRecordedEdit, FALSE);
        EnableWindow(gBrowseRecordedButton, FALSE);
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
    default: return ntc::StimulusMode::Legacy;
    }
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

ntc::TailMode selectedTailMode() {
    return SendMessageW(gTailCombo, CB_GETCURSEL, 0, 0) == 1
        ? ntc::TailMode::RecordedAudio
        : ntc::TailMode::PresetAudio;
}

void updateTailControls() {
    if (gBusy) return;
    const ntc::StimulusMode mode = selectedStimulusMode();
    const bool soundCloneMode = mode != ntc::StimulusMode::Legacy;
    EnableWindow(gTailCombo, soundCloneMode ? TRUE : FALSE);
    const bool recorded = soundCloneMode && selectedTailMode() == ntc::TailMode::RecordedAudio;
    EnableWindow(gRecordedEdit, recorded ? TRUE : FALSE);
    EnableWindow(gBrowseRecordedButton, recorded ? TRUE : FALSE);
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
    stimulus.tailMode = selectedTailMode();
    stimulus.recordedAudio = fs::path(getText(gRecordedEdit));
    if (stimulus.mode != ntc::StimulusMode::Legacy
        && stimulus.tailMode == ntc::TailMode::RecordedAudio
        && stimulus.recordedAudio.empty()) {
        MessageBoxW(hwnd, L"Select a Recorded Audio WAV file.", L"NAM to CLO", MB_ICONINFORMATION | MB_OK);
        return;
    }

    enableControls(false);
    if (gInputMode == InputMode::SingleNam) {
        setText(gStatus, L"Starting conversion...");
        std::thread([hwnd, input, out, stimulus] {
            auto result = std::make_unique<ntc::ConversionResult>(
                ntc::convertNamToBoth(input, out, stimulus, [hwnd](const std::wstring& s) { postStatus(hwnd, s); }));
            PostMessageW(hwnd, WM_APP_DONE_SINGLE, 0, reinterpret_cast<LPARAM>(result.release()));
        }).detach();
    } else {
        setText(gStatus, L"Starting batch conversion...");
        std::thread([hwnd, input, out, stimulus] {
            auto result = std::make_unique<ntc::BatchConversionResult>(
                ntc::convertNamFolder(input, out, stimulus, [hwnd](const std::wstring& s) { postStatus(hwnd, s); }));
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
    const int margin = 36;
    const int gap = 14;
    const int sectionH1 = 122;
    const int sectionH2 = 106;
    const int sectionH3 = 108;
    const int sectionH4 = 108;
    const int sectionH5 = 206;
    const int buttonH = 56;
    const int footerH = 60;

    gUi.header = RECT{ margin, 18, clientW - margin, 154 };

    int y = 166;
    gUi.sectionInput = RECT{ margin, y, clientW - margin, y + sectionH1 }; y += sectionH1 + gap;
    gUi.sectionOutput = RECT{ margin, y, clientW - margin, y + sectionH2 }; y += sectionH2 + gap;
    gUi.sectionStimulus = RECT{ margin, y, clientW - margin, y + sectionH3 }; y += sectionH3 + gap;
    gUi.sectionTail = RECT{ margin, y, clientW - margin, y + sectionH4 }; y += sectionH4 + gap;
    gUi.sectionRecorded = RECT{ margin, y, clientW - margin, y + sectionH5 }; y += sectionH5 + gap;
    gUi.buttonArea = RECT{ margin, y, clientW - margin, y + buttonH };
    gUi.footer = RECT{ 0, clientH - footerH, clientW, clientH };
    gUi.infoBox = RECT{ gUi.sectionRecorded.left + 148, gUi.sectionRecorded.top + 118,
                        gUi.sectionRecorded.right - 22, gUi.sectionRecorded.top + 188 };
}

void layoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    computeLayout(rc.right - rc.left, rc.bottom - rc.top);

    const int contentX = gUi.sectionInput.left + 154;
    const int wideButtonW = 150;
    const int mediumButtonW = 132;
    const int sectionRightInset = 20;

    HWND title = GetDlgItem(hwnd, 1001);
    if (title) moveCtrl(title, 170, 44, 460, 64);
    if (gSubtitle) moveCtrl(gSubtitle, 172, 108, rc.right - 220, 28);

    // Input section
    moveCtrl(GetDlgItem(hwnd, 1002), contentX, gUi.sectionInput.top + 18, 280, 28);
    const int inputEditW = (gUi.sectionInput.right - sectionRightInset) - (contentX + 652);
    moveCtrl(gInputEdit, contentX, gUi.sectionInput.top + 56, inputEditW, 36);
    moveCtrl(gLoadFileButton, gUi.sectionInput.right - sectionRightInset - 316, gUi.sectionInput.top + 54, 148, 40);
    moveCtrl(gLoadFolderButton, gUi.sectionInput.right - sectionRightInset - 156, gUi.sectionInput.top + 54, 148, 40);

    // Output section
    moveCtrl(GetDlgItem(hwnd, 1003), contentX, gUi.sectionOutput.top + 16, 200, 28);
    const int outputEditW = (gUi.sectionOutput.right - sectionRightInset) - (contentX + 184);
    moveCtrl(gOutEdit, contentX, gUi.sectionOutput.top + 54, outputEditW, 36);
    moveCtrl(gBrowseButton, gUi.sectionOutput.right - sectionRightInset - mediumButtonW, gUi.sectionOutput.top + 52, mediumButtonW, 40);

    // Stimulus section
    moveCtrl(GetDlgItem(hwnd, 1004), contentX, gUi.sectionStimulus.top + 16, 220, 28);
    moveCtrl(gStimulusCombo, contentX, gUi.sectionStimulus.top + 52, 640, 250);

    // Tail section
    moveCtrl(GetDlgItem(hwnd, 1005), contentX, gUi.sectionTail.top + 16, 240, 28);
    moveCtrl(gTailCombo, contentX, gUi.sectionTail.top + 52, 640, 250);

    // Recorded section
    moveCtrl(GetDlgItem(hwnd, 1006), contentX, gUi.sectionRecorded.top + 16, 560, 28);
    const int recordedEditW = (gUi.sectionRecorded.right - sectionRightInset) - (contentX + 184);
    moveCtrl(gRecordedEdit, contentX, gUi.sectionRecorded.top + 54, recordedEditW, 36);
    moveCtrl(gBrowseRecordedButton, gUi.sectionRecorded.right - sectionRightInset - 150, gUi.sectionRecorded.top + 52, 150, 40);
    moveCtrl(gInfo, gUi.infoBox.left + 48, gUi.infoBox.top + 13, (gUi.infoBox.right - gUi.infoBox.left) - 58, 42);

    moveCtrl(gConvertButton, gUi.buttonArea.left, gUi.buttonArea.top, 286, 56);
    moveCtrl(gOpenButton, gUi.buttonArea.left + 308, gUi.buttonArea.top, 284, 56);

    moveCtrl(gStatus, 72, gUi.footer.top + 16, rc.right - 320, 28);
    moveCtrl(gVersion, rc.right - 180, gUi.footer.top + 16, 140, 28);
}

void createSectionLabel(HWND hwnd, int id, const wchar_t* text) {
    HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                           0, 0, 100, 24, hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
    applyFont(h, gSectionFont);
}

void createUi(HWND hwnd) {
    createResources();

    HWND title = CreateWindowW(L"STATIC", L"NAM to CLO", WS_CHILD | WS_VISIBLE,
                               0, 0, 100, 30, hwnd, reinterpret_cast<HMENU>(1001), nullptr, nullptr);
    applyFont(title, gTitleFont);

    gSubtitle = CreateWindowW(L"STATIC", L"Convert one NAM or batch-convert every NAM in a selected folder.",
                              WS_CHILD | WS_VISIBLE, 0, 0, 100, 20, hwnd,
                              reinterpret_cast<HMENU>(IDC_SUBTITLE), nullptr, nullptr);
    applyFont(gSubtitle, gSubtitleFont);

    createSectionLabel(hwnd, 1002, L"Input NAM or folder");
    createSectionLabel(hwnd, 1003, L"Output folder");
    createSectionLabel(hwnd, 1004, L"Stimulus profile");
    createSectionLabel(hwnd, 1005, L"Tail / Reamp source");
    createSectionLabel(hwnd, 1006, L"Recorded WAV (adapted automatically to 20.000 s)");

    gInputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                 0, 0, 100, 30, hwnd, reinterpret_cast<HMENU>(IDC_INPUT_PATH), nullptr, nullptr);
    applyFont(gInputEdit);
    gLoadFileButton = CreateWindowW(L"BUTTON", L"Load NAM...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    0, 0, 110, 34, hwnd, reinterpret_cast<HMENU>(IDC_LOAD_FILE), nullptr, nullptr);
    gLoadFolderButton = CreateWindowW(L"BUTTON", L"Load Folder...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                      0, 0, 120, 34, hwnd, reinterpret_cast<HMENU>(IDC_LOAD_FOLDER), nullptr, nullptr);

    gOutEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                               0, 0, 100, 30, hwnd, reinterpret_cast<HMENU>(IDC_OUTPUT_PATH), nullptr, nullptr);
    applyFont(gOutEdit);
    gBrowseButton = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                  0, 0, 120, 34, hwnd, reinterpret_cast<HMENU>(IDC_BROWSE_OUTPUT), nullptr, nullptr);

    gStimulusCombo = CreateWindowW(L"COMBOBOX", L"",
                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                   0, 0, 100, 120, hwnd, reinterpret_cast<HMENU>(IDC_STIMULUS_MODE), nullptr, nullptr);
    applyFont(gStimulusCombo);
    for (const auto mode : { ntc::StimulusMode::Legacy, ntc::StimulusMode::Clean, ntc::StimulusMode::Dist }) {
        SendMessageW(gStimulusCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(ntc::stimulusModeDisplayName(mode)));
    }
    SendMessageW(gStimulusCombo, CB_SETCURSEL, 0, 0);

    gTailCombo = CreateWindowW(L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                               0, 0, 100, 120, hwnd, reinterpret_cast<HMENU>(IDC_TAIL_MODE), nullptr, nullptr);
    applyFont(gTailCombo);
    for (const auto mode : { ntc::TailMode::PresetAudio, ntc::TailMode::RecordedAudio }) {
        SendMessageW(gTailCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(ntc::tailModeDisplayName(mode)));
    }
    SendMessageW(gTailCombo, CB_SETCURSEL, 0, 0);

    gRecordedEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                    0, 0, 100, 30, hwnd, reinterpret_cast<HMENU>(IDC_RECORDED_PATH), nullptr, nullptr);
    applyFont(gRecordedEdit);
    gBrowseRecordedButton = CreateWindowW(L"BUTTON", L"Browse WAV...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          0, 0, 120, 34, hwnd, reinterpret_cast<HMENU>(IDC_BROWSE_RECORDED), nullptr, nullptr);

    gInfo = CreateWindowW(L"STATIC",
                          L"Recorded Audio is converted automatically to mono PCM16 44.1 kHz and to exactly 20 s (trim/pad);\r\n"
                          L"its level is not normalized.",
                          WS_CHILD | WS_VISIBLE,
                          0, 0, 100, 40, hwnd, reinterpret_cast<HMENU>(IDC_INFO), nullptr, nullptr);
    applyFont(gInfo);

    gConvertButton = CreateWindowW(L"BUTTON", L"Convert", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   0, 0, 150, 42, hwnd, reinterpret_cast<HMENU>(IDC_CONVERT), nullptr, nullptr);
    applyFont(gConvertButton);
    gOpenButton = CreateWindowW(L"BUTTON", L"Open output folder", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                0, 0, 180, 42, hwnd, reinterpret_cast<HMENU>(IDC_OPEN_OUTPUT), nullptr, nullptr);
    applyFont(gOpenButton);

    gStatus = CreateWindowW(L"STATIC", L"Checking runtime...", WS_CHILD | WS_VISIBLE,
                            0, 0, 100, 22, hwnd, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);
    applyFont(gStatus);

    gVersion = CreateWindowW(L"STATIC", L"Version 1.5.0", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                             0, 0, 110, 22, hwnd, reinterpret_cast<HMENU>(IDC_VERSION), nullptr, nullptr);
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

void drawIconBadge(HDC hdc, const RECT& rc) {
    drawRoundedRect(hdc, rc, kColorAccent, kColorAccentDark, 22);
    HPEN pen = CreatePen(PS_SOLID, 6, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;
    const int heights[] = { 18, 34, 56, 78, 56, 34, 18 };
    const int xOffsets[] = { -30, -20, -10, 0, 10, 20, 30 };
    for (int i = 0; i < 7; ++i) {
        MoveToEx(hdc, cx + xOffsets[i], cy - heights[i] / 2, nullptr);
        LineTo(hdc, cx + xOffsets[i], cy + heights[i] / 2);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void drawSectionIcon(HDC hdc, const RECT& rc, int kind) {
    drawRoundedRect(hdc, rc, RGB(244, 248, 255), RGB(205, 220, 245), 16);
    HPEN pen = CreatePen(PS_SOLID, 3, kColorAccent);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;
    switch (kind) {
    case 0: // file
        Rectangle(hdc, cx - 14, cy - 18, cx + 14, cy + 18);
        MoveToEx(hdc, cx - 8, cy - 8, nullptr); LineTo(hdc, cx + 8, cy - 8);
        MoveToEx(hdc, cx - 8, cy, nullptr); LineTo(hdc, cx + 8, cy);
        MoveToEx(hdc, cx - 8, cy + 8, nullptr); LineTo(hdc, cx + 4, cy + 8);
        break;
    case 1: // folder
        MoveToEx(hdc, cx - 16, cy - 10, nullptr);
        LineTo(hdc, cx - 3, cy - 10);
        LineTo(hdc, cx + 2, cy - 16);
        LineTo(hdc, cx + 16, cy - 16);
        LineTo(hdc, cx + 16, cy + 14);
        LineTo(hdc, cx - 16, cy + 14);
        LineTo(hdc, cx - 16, cy - 10);
        break;
    case 2: // waveform
        MoveToEx(hdc, cx - 18, cy, nullptr);
        LineTo(hdc, cx - 10, cy);
        LineTo(hdc, cx - 6, cy - 12);
        LineTo(hdc, cx, cy + 14);
        LineTo(hdc, cx + 5, cy - 16);
        LineTo(hdc, cx + 10, cy + 2);
        LineTo(hdc, cx + 18, cy + 2);
        break;
    case 3: // reamp arrows
        Arc(hdc, cx - 18, cy - 18, cx + 18, cy + 18, cx + 12, cy - 8, cx + 18, cy + 6);
        Arc(hdc, cx - 18, cy - 18, cx + 18, cy + 18, cx - 12, cy + 8, cx - 18, cy - 6);
        MoveToEx(hdc, cx + 16, cy + 4, nullptr); LineTo(hdc, cx + 22, cy + 10); LineTo(hdc, cx + 13, cy + 12);
        MoveToEx(hdc, cx - 16, cy - 4, nullptr); LineTo(hdc, cx - 22, cy - 10); LineTo(hdc, cx - 13, cy - 12);
        break;
    case 4: // note
        MoveToEx(hdc, cx + 8, cy - 14, nullptr);
        LineTo(hdc, cx + 8, cy + 10);
        LineTo(hdc, cx - 8, cy + 6);
        Ellipse(hdc, cx - 20, cy + 2, cx - 4, cy + 18);
        Ellipse(hdc, cx + 0, cy - 2, cx + 16, cy + 14);
        break;
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void drawSectionCard(HDC hdc, const RECT& rc, int iconKind) {
    drawRoundedRect(hdc, rc, kColorCard, kColorBorder, 18);
    RECT iconRect{ rc.left + 22, rc.top + 20, rc.left + 88, rc.top + 86 };
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

    RECT heroIcon{ 58, 44, 142, 128 };
    drawIconBadge(hdc, heroIcon);

    drawSectionCard(hdc, gUi.sectionInput, 0);
    drawSectionCard(hdc, gUi.sectionOutput, 1);
    drawSectionCard(hdc, gUi.sectionStimulus, 2);
    drawSectionCard(hdc, gUi.sectionTail, 3);
    drawSectionCard(hdc, gUi.sectionRecorded, 4);
    drawInfoBox(hdc);
    fillRect(hdc, gUi.footer, kColorFooter);

    RECT statusDot{ 28, gUi.footer.top + 14, 52, gUi.footer.top + 38 };
    HBRUSH dotBrush = CreateSolidBrush(kColorStatusOk);
    HGDIOBJ oldBrush = SelectObject(hdc, dotBrush);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(HOLLOW_PEN));
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
            { rc.left + 44, rc.top + 18 },
            { rc.left + 44, rc.bottom - 18 },
            { rc.left + 66, (rc.top + rc.bottom) / 2 }
        };
        HBRUSH triBrush = CreateSolidBrush(text);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, triBrush);
        HGDIOBJ oldPen = SelectObject(dis->hDC, GetStockObject(NULL_PEN));
        Polygon(dis->hDC, pts, 3);
        SelectObject(dis->hDC, oldPen);
        SelectObject(dis->hDC, oldBrush);
        DeleteObject(triBrush);
        rc.left += 78;
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
        if (ctrl == gSubtitle || ctrl == gStatus || ctrl == gVersion || ctrl == gInfo || ctrl == GetDlgItem(hwnd, 1001)
            || ctrl == GetDlgItem(hwnd, 1002) || ctrl == GetDlgItem(hwnd, 1003) || ctrl == GetDlgItem(hwnd, 1004)
            || ctrl == GetDlgItem(hwnd, 1005) || ctrl == GetDlgItem(hwnd, 1006)) {
            if (ctrl == gSubtitle || ctrl == gInfo || ctrl == gStatus || ctrl == gVersion) SetTextColor(hdc, kColorSubtleText);
            else SetTextColor(hdc, kColorText);
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
        updateTailControls();
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

    HWND hwnd = CreateWindowExW(0, kClassName, L"NAM to CLO 1.5",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1230, 980,
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
