#include "conversion.hpp"
#include "common.hpp"
#include "corrective_ir.hpp"
#include "clo_refiner.hpp"

#include <algorithm>
#include <cwctype>

#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ntc {
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
constexpr int kExitOk = 0;
constexpr int kExitUsage = 2;
constexpr int kExitRuntimeMissing = 10;
constexpr int kExitDllLoad = 11;
constexpr int kExitExportMissing = 12;
constexpr int kExitStageFailure = 20;
constexpr int kExitWorkerLaunch = 21;
constexpr int kExitWorkerTimeout = 22;
constexpr int kExitConversionTimeout = 30;
constexpr int kExitConversionBadSize = 32;
constexpr int kExitCopyFailure = 40;
constexpr int kExitMappingFailure = 41;
constexpr int kExitSehBase = 100;
constexpr std::uint8_t kBufferSentinel = 0xCC;
constexpr std::uint64_t kNamConvertArg6 = 0;
constexpr int kTimeoutSeconds = 180;

struct WorkerOptions {
    fs::path dll;
    fs::path inputWav;
    fs::path outputWav;
    fs::path inputNam;
    fs::path outputClo;
    std::wstring mappingName;
    fs::path diagnosticLog;
    fs::path crashDump;
    int timeoutSeconds = kTimeoutSeconds;
};

struct SharedBuffer {
    HANDLE mapping = nullptr;
    std::uint8_t* view = nullptr;
    std::wstring name;
    ~SharedBuffer() {
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
    }
    SharedBuffer() = default;
    SharedBuffer(const SharedBuffer&) = delete;
    SharedBuffer& operator=(const SharedBuffer&) = delete;
};

void report(const StatusCallback& cb, const wchar_t* text) {
    if (cb) cb(text);
}

std::string diagnosticTimestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::ostringstream os;
    os << std::setfill('0')
       << std::setw(4) << st.wYear << '-'
       << std::setw(2) << st.wMonth << '-'
       << std::setw(2) << st.wDay << ' '
       << std::setw(2) << st.wHour << ':'
       << std::setw(2) << st.wMinute << ':'
       << std::setw(2) << st.wSecond << '.'
       << std::setw(3) << st.wMilliseconds;
    return os.str();
}

void appendDiagnostic(const fs::path& path, const std::string& text) noexcept {
    if (path.empty()) return;
    try {
        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (!out) return;
        out << '[' << diagnosticTimestamp() << "][pid=" << GetCurrentProcessId() << "] " << text << "\r\n";
        out.flush();
    } catch (...) {}
}


fs::path gCrashDiagnosticLog;
fs::path gCrashDumpPath;
PVOID gVectoredHandler = nullptr;

std::string exceptionModuleDiagnostic(void* address) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!address || VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi)) return "module=<unknown>";
    const auto module = static_cast<HMODULE>(mbi.AllocationBase);
    wchar_t path[MAX_PATH * 4]{};
    const DWORD n = GetModuleFileNameW(module, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    std::ostringstream os;
    os << "module=" << (n ? pathToUtf8(fs::path(path)) : std::string("<unknown>"))
       << " | moduleBase=0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(module);
    return os.str();
}

bool writeCrashDump(EXCEPTION_POINTERS* ep) noexcept {
    if (gCrashDumpPath.empty()) return false;
    HANDLE file = CreateFileW(gCrashDumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        appendDiagnostic(gCrashDiagnosticLog, "CRASH: CreateFileW(dump) failed: " + win32ErrorMessage(GetLastError()));
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal |
        MiniDumpWithDataSegs |
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules |
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpScanMemory);
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                      ep ? &mei : nullptr, nullptr, nullptr);
    const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        appendDiagnostic(gCrashDiagnosticLog, "CRASH: MiniDumpWriteDump failed: " + win32ErrorMessage(err));
        return false;
    }
    appendDiagnostic(gCrashDiagnosticLog, "CRASH: minidump written: " + pathToUtf8(gCrashDumpPath));
    return true;
}

LONG WINAPI workerVectoredExceptionHandler(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const auto code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    std::ostringstream os;
    os << "CRASH VEH: code=" << hex32(code)
       << " | thread=" << GetCurrentThreadId()
       << " | address=0x" << std::hex << std::uppercase
       << reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress)
       << " | " << exceptionModuleDiagnostic(ep->ExceptionRecord->ExceptionAddress);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        const auto op = ep->ExceptionRecord->ExceptionInformation[0];
        const auto target = ep->ExceptionRecord->ExceptionInformation[1];
        os << " | operation=" << (op == 0 ? "read" : (op == 1 ? "write" : (op == 8 ? "execute" : "unknown")))
           << " | target=0x" << std::hex << std::uppercase << target;
    }
    appendDiagnostic(gCrashDiagnosticLog, os.str());
    writeCrashDump(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI workerUnhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    if (ep && ep->ExceptionRecord) {
        appendDiagnostic(gCrashDiagnosticLog, "CRASH UEF: unhandled exception code=" + hex32(ep->ExceptionRecord->ExceptionCode));
    } else {
        appendDiagnostic(gCrashDiagnosticLog, "CRASH UEF: unhandled exception (no exception record)");
    }
    writeCrashDump(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

void installWorkerCrashDiagnostics(const WorkerOptions& options) {
    gCrashDiagnosticLog = options.diagnosticLog;
    gCrashDumpPath = options.crashDump;
    SetUnhandledExceptionFilter(workerUnhandledExceptionFilter);
    gVectoredHandler = AddVectoredExceptionHandler(1, workerVectoredExceptionHandler);
    appendDiagnostic(options.diagnosticLog, "WORKER [0]: crash diagnostics installed; dump=" + pathToUtf8(options.crashDump));

    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
#pragma warning(push)
#pragma warning(disable:4996)
    GetVersionExW(&vi);
#pragma warning(pop)
    std::ostringstream os;
    os << "WORKER ENV: Windows=" << vi.dwMajorVersion << '.' << vi.dwMinorVersion << " build=" << vi.dwBuildNumber
       << " | arch=" << si.wProcessorArchitecture << " | CPUs=" << si.dwNumberOfProcessors
       << " | pageSize=" << si.dwPageSize;
    appendDiagnostic(options.diagnosticLog, os.str());
    appendDiagnostic(options.diagnosticLog, std::string("WORKER ENV: PF_XMMI64_INSTRUCTIONS_AVAILABLE=") +
        (IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) ? "yes" : "no"));
}



std::string envValueUtf8(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (!needed) return "<unset>";
    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (!written) return "<error>";
    if (!value.empty() && value.back() == L'\0') value.pop_back();
    return pathToUtf8(fs::path(value));
}

std::string currentDirectoryUtf8() {
    const DWORD needed = GetCurrentDirectoryW(0, nullptr);
    if (!needed) return "<error>";
    std::wstring value(needed, L'\0');
    const DWORD written = GetCurrentDirectoryW(needed, value.data());
    if (!written) return "<error>";
    value.resize(written);
    return pathToUtf8(fs::path(value));
}

std::string tokenDiagnostic() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return "token=<OpenProcessToken failed: " + win32ErrorMessage(GetLastError()) + ">";

    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    const bool haveElevation = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes) != FALSE;

    TOKEN_ELEVATION_TYPE elevationType = TokenElevationTypeDefault;
    const bool haveType = GetTokenInformation(token, TokenElevationType, &elevationType, sizeof(elevationType), &bytes) != FALSE;

    DWORD integrityRid = 0;
    DWORD needed = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &needed);
    if (needed) {
        std::vector<std::uint8_t> buffer(needed);
        if (GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), needed, &needed)) {
            const auto* til = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
            if (til && til->Label.Sid) {
                const DWORD count = *GetSidSubAuthorityCount(til->Label.Sid);
                if (count) integrityRid = *GetSidSubAuthority(til->Label.Sid, count - 1);
            }
        }
    }

    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminSid = nullptr;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &adminSid)) {
        CheckTokenMembership(nullptr, adminSid, &isAdmin);
        FreeSid(adminSid);
    }
    CloseHandle(token);

    const char* type = "unknown";
    if (haveType) {
        type = elevationType == TokenElevationTypeDefault ? "default" :
               elevationType == TokenElevationTypeFull ? "full" :
               elevationType == TokenElevationTypeLimited ? "limited" : "unknown";
    }
    std::ostringstream os;
    os << "elevated=" << (haveElevation && elevation.TokenIsElevated ? "yes" : "no")
       << " | elevationType=" << type
       << " | adminGroup=" << (isAdmin ? "yes" : "no")
       << " | integrityRID=0x" << std::hex << std::uppercase << integrityRid;
    return os.str();
}

std::uint64_t fnv1aFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    std::uint64_t hash = 14695981039346656037ull;
    char buf[64 * 1024];
    while (in) {
        in.read(buf, sizeof(buf));
        const auto n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            hash ^= static_cast<std::uint8_t>(buf[i]);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

std::string hex64(std::uint64_t v) {
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << v;
    return os.str();
}

std::string probeWritableDirectory(const fs::path& dir) {
    std::error_code ec;
    const bool exists = fs::exists(dir, ec) && !ec;
    if (!exists) return pathToUtf8(dir) + " | exists=no";
    const fs::path probe = dir / (L"NamToClo_write_probe_" + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return pathToUtf8(dir) + " | writable=no | error=" + win32ErrorMessage(GetLastError());
    const char byte = 'x'; DWORD written = 0;
    const BOOL ok = WriteFile(h, &byte, 1, &written, nullptr);
    const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(h);
    return pathToUtf8(dir) + " | writable=" + (ok && written == 1 ? std::string("yes") : std::string("no")) +
           (ok ? std::string() : " | error=" + win32ErrorMessage(err));
}

std::string wavHeaderDiagnostic(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "WAV parse: could not open";
    auto readU16 = [&](std::uint16_t& v) { char b[2]; if (!in.read(b,2)) return false; v = (std::uint8_t)b[0] | ((std::uint16_t)(std::uint8_t)b[1] << 8); return true; };
    auto readU32 = [&](std::uint32_t& v) { char b[4]; if (!in.read(b,4)) return false; v = (std::uint8_t)b[0] | ((std::uint32_t)(std::uint8_t)b[1] << 8) | ((std::uint32_t)(std::uint8_t)b[2] << 16) | ((std::uint32_t)(std::uint8_t)b[3] << 24); return true; };
    char riff[4]{}, wave[4]{}; std::uint32_t riffSize=0;
    if (!in.read(riff,4) || !readU32(riffSize) || !in.read(wave,4)) return "WAV parse: truncated RIFF header";
    bool gotFmt=false, gotData=false; std::uint16_t format=0, channels=0, bits=0, blockAlign=0; std::uint32_t rate=0, byteRate=0, dataBytes=0;
    while (in && !(gotFmt && gotData)) {
        char id[4]{}; std::uint32_t size=0;
        if (!in.read(id,4) || !readU32(size)) break;
        const auto payload = in.tellg();
        if (std::memcmp(id,"fmt ",4)==0 && size >= 16) {
            gotFmt = readU16(format) && readU16(channels) && readU32(rate) && readU32(byteRate) && readU16(blockAlign) && readU16(bits);
        } else if (std::memcmp(id,"data",4)==0) { gotData=true; dataBytes=size; }
        in.clear(); in.seekg(payload + static_cast<std::streamoff>(size + (size & 1u)));
    }
    std::ostringstream os;
    os << "WAV parse: RIFF=" << std::string(riff,4) << " WAVE=" << std::string(wave,4)
       << " riffSize=" << riffSize << " fmt=" << (gotFmt?"yes":"no") << " format=" << format
       << " channels=" << channels << " sampleRate=" << rate << " byteRate=" << byteRate
       << " blockAlign=" << blockAlign << " bits=" << bits << " data=" << (gotData?"yes":"no")
       << " dataBytes=" << dataBytes;
    return os.str();
}

void appendEnvironmentAndPermissionDiagnostics(const fs::path& log, const WorkerOptions& options) {
    appendDiagnostic(log, "WORKER ENV: token " + tokenDiagnostic());
    appendDiagnostic(log, "WORKER ENV: CWD=" + currentDirectoryUtf8());
    appendDiagnostic(log, "WORKER ENV: TEMP=" + envValueUtf8(L"TEMP"));
    appendDiagnostic(log, "WORKER ENV: TMP=" + envValueUtf8(L"TMP"));
    appendDiagnostic(log, "WORKER ENV: LOCALAPPDATA=" + envValueUtf8(L"LOCALAPPDATA"));
    appendDiagnostic(log, "WORKER ENV: APPDATA=" + envValueUtf8(L"APPDATA"));
    appendDiagnostic(log, "WORKER ENV: PROGRAMDATA=" + envValueUtf8(L"PROGRAMDATA"));
    appendDiagnostic(log, "WORKER ENV: USERPROFILE=" + envValueUtf8(L"USERPROFILE"));
    std::vector<fs::path> dirs;
    wchar_t cwdBuf[32768]{}; const DWORD cwdN=GetCurrentDirectoryW(32768,cwdBuf); if (cwdN && cwdN < 32768) dirs.emplace_back(cwdBuf);
    dirs.push_back(options.dll.parent_path());
    dirs.push_back(options.inputWav.parent_path());
    dirs.push_back(executablePath().parent_path());
    for (const wchar_t* var : {L"TEMP",L"TMP",L"LOCALAPPDATA",L"APPDATA",L"PROGRAMDATA",L"USERPROFILE"}) {
        const DWORD n=GetEnvironmentVariableW(var,nullptr,0); if (n) { std::wstring v(n,L'\0'); if (GetEnvironmentVariableW(var,v.data(),n)) { if (!v.empty()&&v.back()==L'\0') v.pop_back(); dirs.emplace_back(v); } }
    }
    std::vector<std::wstring> seen;
    for (const auto& d : dirs) {
        const auto key=d.wstring(); if (d.empty() || std::find(seen.begin(),seen.end(),key)!=seen.end()) continue; seen.push_back(key);
        appendDiagnostic(log, "WORKER PROBE: " + probeWritableDirectory(d));
    }
    appendDiagnostic(log, "WORKER HASH: DLL fnv1a64=" + hex64(fnv1aFile(options.dll)));
    appendDiagnostic(log, "WORKER HASH: input WAV fnv1a64=" + hex64(fnv1aFile(options.inputWav)));
    appendDiagnostic(log, "WORKER HASH: input NAM fnv1a64=" + hex64(fnv1aFile(options.inputNam)));
    appendDiagnostic(log, wavHeaderDiagnostic(options.inputWav));
}

std::string fileDiagnostic(const fs::path& p) {
    std::error_code ec;
    const bool exists = fs::exists(p, ec) && !ec;
    const auto size = exists ? fs::file_size(p, ec) : 0;
    std::ostringstream os;
    os << pathToUtf8(p) << " | exists=" << (exists ? "yes" : "no");
    if (exists && !ec) os << " | bytes=" << size;
    return os.str();
}

fs::path findFirstExisting(const std::vector<fs::path>& candidates) {
    std::error_code ec;
    for (const auto& p : candidates) {
        if (!p.empty() && fs::exists(p, ec) && !ec) return p;
        ec.clear();
    }
    return {};
}

fs::path makeWorkDirectory() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const DWORD pid = GetCurrentProcessId();
    fs::path base = fs::temp_directory_path() / L"NamToClo";
    std::error_code ec;
    fs::create_directories(base, ec);
    fs::path work = base / (L"job-" + std::to_wstring(pid) + L"-" + std::to_wstring(now));
    fs::create_directories(work, ec);
    return ec ? fs::path{} : work;
}

std::wstring makeMappingName() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return L"Local\\NamToClo-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(now);
}

bool createSharedBuffer(SharedBuffer& out, std::string& error) {
    out.name = makeMappingName();
    out.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     static_cast<DWORD>(kExpectedCloSize), out.name.c_str());
    if (!out.mapping) {
        error = "CreateFileMappingW failed: " + win32ErrorMessage(GetLastError());
        return false;
    }
    out.view = static_cast<std::uint8_t*>(MapViewOfFile(out.mapping, FILE_MAP_ALL_ACCESS, 0, 0, kExpectedCloSize));
    if (!out.view) {
        error = "MapViewOfFile failed: " + win32ErrorMessage(GetLastError());
        return false;
    }
    std::memset(out.view, kBufferSentinel, static_cast<std::size_t>(kExpectedCloSize));
    return true;
}

bool startsWithVtsi(const std::uint8_t* data) {
    return data && data[0] == 'V' && data[1] == 'T' && data[2] == 'S' && data[3] == 'I';
}

std::size_t changedByteCount(const std::uint8_t* data) {
    std::size_t changed = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(kExpectedCloSize); ++i) {
        if (data[i] != kBufferSentinel) ++changed;
    }
    return changed;
}

fs::path uniquePath(const fs::path& desired) {
    std::error_code ec;
    if (!fs::exists(desired, ec)) return desired;
    const auto parent = desired.parent_path();
    const auto stem = desired.stem().wstring();
    const auto ext = desired.extension().wstring();
    for (int i = 1; i < 10000; ++i) {
        fs::path p = parent / (stem + L" (" + std::to_wstring(i) + L")" + ext);
        ec.clear();
        if (!fs::exists(p, ec)) return p;
    }
    return desired;
}

std::wstring makeWorkerCommandLine(const WorkerOptions& w) {
    const std::vector<std::wstring> args = {
        executablePath().wstring(), L"--worker",
        L"--dll", w.dll.wstring(),
        L"--input-wav", w.inputWav.wstring(),
        L"--output-wav", w.outputWav.wstring(),
        L"--nam", w.inputNam.wstring(),
        L"--clo", w.outputClo.wstring(),
        L"--mapping", w.mappingName,
        L"--diag", w.diagnosticLog.wstring(),
        L"--dump", w.crashDump.wstring(),
        L"--timeout", std::to_wstring(w.timeoutSeconds)
    };
    std::wstring cmd;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) cmd.push_back(L' ');
        cmd += quoteWindowsArg(args[i]);
    }
    return cmd;
}

int launchWorker(const WorkerOptions& worker, SharedBuffer& shared, bool& capturedValid, std::string& error) {
    capturedValid = false;
    appendDiagnostic(worker.diagnosticLog, "PARENT: launchWorker begin");
    appendDiagnostic(worker.diagnosticLog, "PARENT: dll: " + fileDiagnostic(worker.dll));
    appendDiagnostic(worker.diagnosticLog, "PARENT: input WAV: " + fileDiagnostic(worker.inputWav));
    appendDiagnostic(worker.diagnosticLog, "PARENT: input NAM: " + fileDiagnostic(worker.inputNam));
    appendDiagnostic(worker.diagnosticLog, "PARENT: output WAV: " + pathToUtf8(worker.outputWav));
    appendDiagnostic(worker.diagnosticLog, "PARENT: output CLO: " + pathToUtf8(worker.outputClo));
    appendDiagnostic(worker.diagnosticLog, "PARENT: crash dump: " + pathToUtf8(worker.crashDump));
    std::wstring command = makeWorkerCommandLine(worker);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_NO_WINDOW;
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        appendDiagnostic(worker.diagnosticLog, "PARENT: CreateProcessW FAILED: " + win32ErrorMessage(GetLastError()));
        error = "Could not start conversion worker: " + win32ErrorMessage(GetLastError());
        return kExitWorkerLaunch;
    }

    appendDiagnostic(worker.diagnosticLog, "PARENT: worker process created pid=" + std::to_string(pi.dwProcessId));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(worker.timeoutSeconds + 15);
    while (std::chrono::steady_clock::now() < deadline) {
        const DWORD wait = WaitForSingleObject(pi.hProcess, 100);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
    }

    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    if (exitCode == STILL_ACTIVE) {
        TerminateProcess(pi.hProcess, kExitWorkerTimeout);
        WaitForSingleObject(pi.hProcess, 2000);
        exitCode = kExitWorkerTimeout;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    appendDiagnostic(worker.diagnosticLog, "PARENT: worker exit code=" + hex32(exitCode));
    appendDiagnostic(worker.diagnosticLog, "PARENT: crash dump after worker: " + fileDiagnostic(worker.crashDump));
    const std::size_t changed = changedByteCount(shared.view);
    capturedValid = startsWithVtsi(shared.view) && changed > 0;
    appendDiagnostic(worker.diagnosticLog, "PARENT: shared buffer changed bytes=" + std::to_string(changed) + ", VTSI=" + (startsWithVtsi(shared.view) ? std::string("yes") : std::string("no")));
    if (capturedValid) {
        if (!writeFileBytes(worker.outputClo, shared.view, static_cast<std::size_t>(kExpectedCloSize), error)) {
            capturedValid = false;
        }
    }

    if (capturedValid) return kExitOk;
    if (exitCode >= 0xC0000000u) {
        error = "HTUSBTools worker terminated with Windows exception " + hex32(exitCode) + ". Diagnostic log: " + pathToUtf8(worker.diagnosticLog);
        return static_cast<int>(exitCode & 0x7FFFFFFFu);
    }
    if (error.empty()) error = "Conversion worker failed (exit code " + std::to_string(exitCode) + "). Diagnostic log: " + pathToUtf8(worker.diagnosticLog);
    return static_cast<int>(exitCode);
}

using NamConvertCloDataFn = std::uint32_t(__cdecl*)(void*, const char*, const char*, const char*, void*, std::uint64_t);

DWORD invokeDataWithSeh(NamConvertCloDataFn fn, const char* inputWav, const char* outputWav,
                        const char* inputNam, void* outputBuffer, std::uint32_t* apiReturn) noexcept {
#if defined(_MSC_VER)
    __try {
        *apiReturn = fn(nullptr, inputWav, outputWav, inputNam, outputBuffer, kNamConvertArg6);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
#else
    *apiReturn = fn(nullptr, inputWav, outputWav, inputNam, outputBuffer, kNamConvertArg6);
    return 0;
#endif
}

int observeWorkerOutput(const WorkerOptions& options, std::uint8_t* mappedData) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.timeoutSeconds);
    std::size_t lastChanged = 0;
    int stablePolls = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const std::size_t changed = changedByteCount(mappedData);
        if (changed > 0) {
            if (changed == lastChanged) ++stablePolls;
            else { lastChanged = changed; stablePolls = 0; }
            if (startsWithVtsi(mappedData) && stablePolls >= 10) return kExitOk;
        }
        std::this_thread::sleep_for(100ms);
    }
    return kExitConversionTimeout;
}

std::optional<int> positiveInt(const std::wstring& s) {
    try {
        std::size_t n = 0;
        const int v = std::stoi(s, &n, 10);
        if (n != s.size() || v <= 0) return std::nullopt;
        return v;
    } catch (...) { return std::nullopt; }
}

bool parseWorker(int argc, wchar_t** argv, WorkerOptions& w) {
    auto has = [argc](int i) { return i + 1 < argc; };
    for (int i = 2; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--dll" && has(i)) w.dll = argv[++i];
        else if (a == L"--input-wav" && has(i)) w.inputWav = argv[++i];
        else if (a == L"--output-wav" && has(i)) w.outputWav = argv[++i];
        else if (a == L"--nam" && has(i)) w.inputNam = argv[++i];
        else if (a == L"--clo" && has(i)) w.outputClo = argv[++i];
        else if (a == L"--mapping" && has(i)) w.mappingName = argv[++i];
        else if (a == L"--diag" && has(i)) w.diagnosticLog = argv[++i];
        else if (a == L"--dump" && has(i)) w.crashDump = argv[++i];
        else if (a == L"--timeout" && has(i)) {
            const auto v = positiveInt(argv[++i]); if (!v) return false; w.timeoutSeconds = *v;
        } else return false;
    }
    return !w.dll.empty() && !w.inputWav.empty() && !w.outputWav.empty() && !w.inputNam.empty()
        && !w.outputClo.empty() && !w.mappingName.empty();
}

bool isA2SlimmableNam(const fs::path& path) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!readFileBytes(path, bytes, error)) return false;
    static constexpr char kMarker[] = "\"architecture\"";
    static constexpr char kSlim[] = "SlimmableContainer";
    const auto a = std::search(bytes.begin(), bytes.end(),
                               reinterpret_cast<const std::uint8_t*>(kMarker),
                               reinterpret_cast<const std::uint8_t*>(kMarker) + sizeof(kMarker) - 1);
    if (a == bytes.end()) return false;
    const auto b = std::search(a, bytes.end(),
                               reinterpret_cast<const std::uint8_t*>(kSlim),
                               reinterpret_cast<const std::uint8_t*>(kSlim) + sizeof(kSlim) - 1);
    return b != bytes.end();
}

// Return the end position (one past '}') of a JSON object beginning at openPos.
// This tiny scanner is intentional: an A2 SlimmableContainer already contains each
// submodel as a complete NAM JSON object, so we can copy the Full model verbatim
// without introducing a JSON dependency or reserialising floating-point weights.
std::optional<std::size_t> jsonObjectEnd(const std::string& text, std::size_t openPos) {
    if (openPos >= text.size() || text[openPos] != '{') return std::nullopt;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = openPos; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}') {
            if (--depth == 0) return i + 1;
            if (depth < 0) return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<double> parseJsonNumberAfter(const std::string& text, std::size_t keyPos) {
    const auto colon = text.find(':', keyPos);
    if (colon == std::string::npos) return std::nullopt;
    const char* begin = text.c_str() + colon + 1;
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    if (end == begin) return std::nullopt;
    return v;
}

struct A2SubmodelRecord {
    double maxValue = 0.0;
    std::string modelJson;
};

bool parseA2Submodels(const fs::path& inputNam, std::vector<A2SubmodelRecord>& records, std::string& error) {
    records.clear();
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(inputNam, bytes, error)) return false;
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    const std::string submodelsKey = "\"submodels\"";
    const auto key = text.find(submodelsKey);
    if (key == std::string::npos) { error = "A2 SlimmableContainer has no submodels array."; return false; }
    const auto arrayOpen = text.find('[', key + submodelsKey.size());
    if (arrayOpen == std::string::npos) { error = "A2 SlimmableContainer submodels array is malformed."; return false; }

    std::size_t pos = arrayOpen + 1;
    while (pos < text.size()) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
        if (pos >= text.size() || text[pos] == ']') break;
        if (text[pos] == ',') { ++pos; continue; }
        if (text[pos] != '{') { error = "A2 SlimmableContainer submodel entry is malformed."; return false; }
        const auto entryEndOpt = jsonObjectEnd(text, pos);
        if (!entryEndOpt) { error = "A2 SlimmableContainer submodel object is malformed."; return false; }
        const std::size_t entryEnd = *entryEndOpt;
        const std::string maxKey = "\"max_value\"";
        const auto maxPos = text.find(maxKey, pos);
        const std::string modelKey = "\"model\"";
        const auto modelPos = text.find(modelKey, pos);
        if (maxPos < entryEnd && modelPos < entryEnd) {
            const auto maxValue = parseJsonNumberAfter(text, maxPos);
            const auto modelColon = text.find(':', modelPos + modelKey.size());
            const auto modelOpen = modelColon == std::string::npos ? std::string::npos : text.find('{', modelColon + 1);
            if (maxValue && modelOpen < entryEnd) {
                const auto modelEnd = jsonObjectEnd(text, modelOpen);
                if (modelEnd && *modelEnd <= entryEnd)
                    records.push_back({*maxValue, text.substr(modelOpen, *modelEnd - modelOpen)});
            }
        }
        pos = entryEnd;
    }
    if (records.empty()) { error = "Could not extract any A2 submodels from SlimmableContainer."; return false; }
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.maxValue < b.maxValue; });
    return true;
}

bool writeTextFile(const fs::path& path, const std::string& text, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "Could not create temporary A2 Full model: " + pathToUtf8(path); return false; }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out.good()) { error = "Could not write temporary A2 Full model: " + pathToUtf8(path); return false; }
    return true;
}

bool prepareA2FullModel(const fs::path& inputNam, const fs::path& workDir,
                        fs::path& modelPath, std::string& error) {
    modelPath = inputNam;
    if (!isA2SlimmableNam(inputNam)) return true;

    std::vector<A2SubmodelRecord> records;
    if (!parseA2Submodels(inputNam, records, error)) return false;

    // Verified with Modelo4 A2: the highest-max_value embedded WaveNet is the
    // Full renderer (8 channels, max_value 1.0); the lower entry is Lite.
    const auto& full = records.back();
    const fs::path fullNam = workDir / L"a2_full_model.nam";
    if (!writeTextFile(fullNam, full.modelJson, error)) return false;
    modelPath = fullNam;
    return true;
}

int runWorker(const WorkerOptions& options) {
    installWorkerCrashDiagnostics(options);
    appendDiagnostic(options.diagnosticLog, "WORKER [1]: started");
    appendEnvironmentAndPermissionDiagnostics(options.diagnosticLog, options);
    appendDiagnostic(options.diagnosticLog, "WORKER [2]: DLL before LoadLibraryExW: " + fileDiagnostic(options.dll));
    appendDiagnostic(options.diagnosticLog, "WORKER: input WAV: " + fileDiagnostic(options.inputWav));
    appendDiagnostic(options.diagnosticLog, "WORKER: input NAM: " + fileDiagnostic(options.inputNam));
    appendDiagnostic(options.diagnosticLog, "WORKER: output WAV: " + pathToUtf8(options.outputWav));
    appendDiagnostic(options.diagnosticLog, "WORKER: output CLO: " + pathToUtf8(options.outputClo));

    HMODULE module = LoadLibraryExW(options.dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        appendDiagnostic(options.diagnosticLog, "WORKER [2]: LoadLibraryExW FAILED: " + win32ErrorMessage(GetLastError()));
        return kExitDllLoad;
    }
    appendDiagnostic(options.diagnosticLog, "WORKER [3]: HTUSBTools.dll loaded successfully");

    auto fn = reinterpret_cast<NamConvertCloDataFn>(GetProcAddress(module, "namConvertCloData"));
    if (!fn) {
        appendDiagnostic(options.diagnosticLog, "WORKER [4]: GetProcAddress(namConvertCloData) FAILED");
        FreeLibrary(module);
        return kExitExportMissing;
    }
    appendDiagnostic(options.diagnosticLog, "WORKER [4]: namConvertCloData resolved");

    appendDiagnostic(options.diagnosticLog, "WORKER [5]: opening shared mapping");
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, options.mappingName.c_str());
    if (!mapping) {
        appendDiagnostic(options.diagnosticLog, "WORKER [5]: OpenFileMappingW FAILED: " + win32ErrorMessage(GetLastError()));
        FreeLibrary(module);
        return kExitMappingFailure;
    }
    auto* mapped = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, kExpectedCloSize));
    if (!mapped) {
        appendDiagnostic(options.diagnosticLog, "WORKER [5]: MapViewOfFile FAILED: " + win32ErrorMessage(GetLastError()));
        CloseHandle(mapping);
        FreeLibrary(module);
        return kExitMappingFailure;
    }
    appendDiagnostic(options.diagnosticLog, "WORKER [5]: shared mapping ready");

    fs::path modelPath = options.inputNam;
    std::string modelError;
    const bool a2 = isA2SlimmableNam(options.inputNam);
    appendDiagnostic(options.diagnosticLog, std::string("WORKER [6]: A2 SlimmableContainer=") + (a2 ? "yes" : "no"));
    appendDiagnostic(options.diagnosticLog, "WORKER [7]: preparing model passed to HTUSBTools");
    if (!prepareA2FullModel(options.inputNam, options.outputClo.parent_path(), modelPath, modelError)) {
        appendDiagnostic(options.diagnosticLog, "WORKER [7]: prepareA2FullModel FAILED: " + modelError);
        UnmapViewOfFile(mapped);
        CloseHandle(mapping);
        FreeLibrary(module);
        return kExitStageFailure;
    }
    appendDiagnostic(options.diagnosticLog, "WORKER [7]: model ready: " + fileDiagnostic(modelPath));

    const std::string inWav = pathToUtf8(options.inputWav);
    const std::string outWav = pathToUtf8(options.outputWav);
    const std::string nam = pathToUtf8(modelPath);
    std::uint32_t apiReturn = 0;
    appendDiagnostic(options.diagnosticLog, "WORKER [8]: ENTER namConvertCloData");
    const DWORD exceptionCode = invokeDataWithSeh(fn, inWav.c_str(), outWav.c_str(), nam.c_str(), mapped, &apiReturn);
    appendDiagnostic(options.diagnosticLog, "WORKER [9]: RETURN namConvertCloData exception=" + hex32(exceptionCode) + ", apiReturn=" + std::to_string(apiReturn));

    int result = kExitOk;
    if (exceptionCode != 0) {
        appendDiagnostic(options.diagnosticLog, "WORKER [9]: SEH exception caught inside namConvertCloData: " + hex32(exceptionCode));
        result = kExitSehBase;
    }
    else if (apiReturn != kExpectedApiReturn) {
        appendDiagnostic(options.diagnosticLog, "WORKER [9]: unexpected API return");
        result = kExitConversionBadSize;
    }
    else {
        appendDiagnostic(options.diagnosticLog, "WORKER [10]: observing CLO output buffer");
        result = observeWorkerOutput(options, mapped);
        appendDiagnostic(options.diagnosticLog, "WORKER [10]: observe result=" + std::to_string(result));
    }

    appendDiagnostic(options.diagnosticLog, "WORKER [11]: output WAV after call: " + fileDiagnostic(options.outputWav));
    appendDiagnostic(options.diagnosticLog, "WORKER [12]: unmapping shared buffer");
    UnmapViewOfFile(mapped);
    CloseHandle(mapping);
    appendDiagnostic(options.diagnosticLog, "WORKER [13]: FreeLibrary begin");
    FreeLibrary(module);
    appendDiagnostic(options.diagnosticLog, "WORKER [14]: clean exit result=" + std::to_string(result));
    return result;
}

bool valid2048(const fs::path& p) {
    const CloInfo i = inspectClo(p);
    return i.exists && i.size == kExpectedCloSize && i.magic == "VTSI"
        && i.declaredSize == 0x2288 && i.payloadSize == 0x2200 && i.modelField == 0x800;
}

bool valid1024(const fs::path& p) {
    const CloInfo i = inspectClo(p);
    return i.exists && i.size == kExpectedCloSize && i.magic == "VTSI"
        && i.declaredSize == 0x1288 && i.payloadSize == 0x1200 && i.modelField == 0x400;
}

} // namespace

RuntimePaths resolveDefaultRuntime() {
    RuntimePaths r;
    const fs::path exe = executablePath();
    if (exe.empty()) return r;
    const fs::path root = exe.parent_path() / L"runtime" / L"ampero";
    // Keep expected paths even when optional Sound Clone resources are absent.
    // This lets mode-specific validation return precise error messages.
    r.dll = root / L"HTUSBTools.dll";
    r.legacyStimulus = root / L"nam_input_wav.wav";
    r.cleanStimulus = root / L"inputSignalCleanSW.wav";
    r.distStimulus = root / L"inputSignalDistSW.wav";
    r.presetAudio = root / L"PresetAudio.wav";
    return r;
}

bool validateRuntime(const RuntimePaths& r, std::string& error) {
    std::error_code ec;
    if (r.dll.empty() || !fs::exists(r.dll, ec) || ec) {
        error = "Missing runtime\\ampero\\HTUSBTools.dll";
        return false;
    }
    ec.clear();
    if (r.legacyStimulus.empty() || !fs::exists(r.legacyStimulus, ec) || ec) {
        error = "Missing runtime\\ampero\\nam_input_wav.wav";
        return false;
    }
    return true;
}

ConversionResult convertNamToBoth(const fs::path& inputNam, const fs::path& outputDirectory,
                                  const StimulusConfig stimulus, const CorrectiveIrConfig correction,
                                  const CloRefineConfig refine, const StatusCallback& status) {
    ConversionResult result;
    result.inputNam = inputNam;
    std::error_code ec;
    std::wstring inputExt = inputNam.extension().wstring();
    for (auto& c : inputExt) c = static_cast<wchar_t>(towlower(c));
    if (!fs::exists(inputNam, ec) || ec || inputExt != L".nam") {
        result.exitCode = kExitUsage;
        result.error = "Select a valid .nam file.";
        return result;
    }

    RuntimePaths runtime = resolveDefaultRuntime();
    if (!validateRuntime(runtime, result.error)) {
        result.exitCode = kExitRuntimeMissing;
        return result;
    }

    fs::path outDir = outputDirectory.empty() ? inputNam.parent_path() : outputDirectory;
    fs::create_directories(outDir, ec);
    if (ec) {
        result.exitCode = kExitCopyFailure;
        result.error = "Could not create output folder: " + ec.message();
        return result;
    }

    const fs::path work = makeWorkDirectory();
    const fs::path diagnosticLog = uniquePath(outDir / (inputNam.stem().wstring() + L"_HTUSBTools_DIAGNOSTIC.log"));
    appendDiagnostic(diagnosticLog, "=== NamToClo v2.6.7 diagnostic conversion begin ===");
    appendDiagnostic(diagnosticLog, "Application: " + pathToUtf8(executablePath()));
    appendDiagnostic(diagnosticLog, "Work directory: " + pathToUtf8(work));
    if (work.empty()) {
        result.exitCode = kExitStageFailure;
        result.error = "Could not create temporary work directory.";
        return result;
    }

    report(status, L"Preparing conversion...");
    WorkerOptions worker;
    worker.dll = runtime.dll;
    worker.inputWav = work / L"nam_input_wav.wav";
    worker.outputWav = work / L"outputFile.wav";
    worker.inputNam = work / L"input.nam";
    worker.outputClo = work / L"ampero_2048.clo";
    worker.diagnosticLog = diagnosticLog;
    worker.crashDump = uniquePath(outDir / (inputNam.stem().wstring() + L"_HTUSBTools_CRASH.dmp"));

    std::string error;
    const std::wstring stimulusStatus = std::wstring(L"Preparing stimulus: ") + stimulusModeDisplayName(stimulus.mode);
    report(status, stimulusStatus.c_str());
    if (!buildStimulus(runtime, stimulus, worker.inputWav, error)
        || !copyFileCreatingParents(inputNam, worker.inputNam, error)) {
        result.exitCode = kExitStageFailure;
        result.error = "Could not stage conversion files: " + error;
        fs::remove_all(work, ec);
        return result;
    }

    // Diagnostic-only preserved copies. These make it possible to compare the exact staged
    // WAV/NAM between a failing non-elevated run and a successful elevated run.
    const fs::path diagInputWav = uniquePath(outDir / (inputNam.stem().wstring() + L"_DIAG_INPUT.wav"));
    const fs::path diagInputNam = uniquePath(outDir / (inputNam.stem().wstring() + L"_DIAG_INPUT.nam"));
    std::error_code diagEc;
    fs::copy_file(worker.inputWav, diagInputWav, fs::copy_options::overwrite_existing, diagEc);
    appendDiagnostic(diagnosticLog, std::string("PARENT DIAG COPY WAV: ") + (diagEc ? ("FAILED: " + diagEc.message()) : fileDiagnostic(diagInputWav)));
    diagEc.clear();
    fs::copy_file(worker.inputNam, diagInputNam, fs::copy_options::overwrite_existing, diagEc);
    appendDiagnostic(diagnosticLog, std::string("PARENT DIAG COPY NAM: ") + (diagEc ? ("FAILED: " + diagEc.message()) : fileDiagnostic(diagInputNam)));
    appendDiagnostic(diagnosticLog, "PARENT HASH: staged WAV fnv1a64=" + hex64(fnv1aFile(worker.inputWav)));
    appendDiagnostic(diagnosticLog, "PARENT HASH: staged NAM fnv1a64=" + hex64(fnv1aFile(worker.inputNam)));
    appendDiagnostic(diagnosticLog, wavHeaderDiagnostic(worker.inputWav));

    SharedBuffer shared;
    if (!createSharedBuffer(shared, error)) {
        result.exitCode = kExitMappingFailure;
        result.error = error;
        fs::remove_all(work, ec);
        return result;
    }
    worker.mappingName = shared.name;

    if (isA2SlimmableNam(worker.inputNam))
        report(status, L"A2 Full: using the verified highest-max_value embedded submodel.");
    appendDiagnostic(diagnosticLog, "=== FIRST HTUSBTools PASS: base conversion ===");
    report(status, L"Generating Ampero 2048 CLO...");
    bool capturedValid = false;
    const int workerExit = launchWorker(worker, shared, capturedValid, error);
    if (workerExit != kExitOk || !capturedValid || !valid2048(worker.outputClo)) {
        result.exitCode = workerExit != kExitOk ? workerExit : kExitConversionBadSize;
        result.error = error.empty() ? "The generated Ampero CLO failed validation. Diagnostic log: " + pathToUtf8(diagnosticLog) : error;
        fs::remove_all(work, ec);
        return result;
    }

    // Optional Block-B Tone Match refinement. The comparison must always use
    // the SAME input performance through the NAM and through the already-created
    // CLO. When a refinement test WAV is selected, its FIRST 20 seconds are
    // adapted by StimulusBuilder and inserted as the 20-second tail of a second
    // otherwise-identical 70-second stimulus. HTUSBTools renders that stimulus
    // through the verified NAM Full path; CloPlayer renders the exact same
    // stimulus through the original Ampero 2048 CLO. Tone Match then compares
    // the common final 20-second section.
    fs::path refinedWorkClo;
    fs::path bestWorkClo;
    CloRefineStats refineStats{};
    if (refine.enabled) {
        fs::path refineStimulus = worker.inputWav;
        fs::path refineTarget = worker.outputWav;

        if (!refine.referenceWav.empty()) {
            std::error_code referenceEc;
            if (!fs::exists(refine.referenceWav, referenceEc) || referenceEc) {
                result.exitCode = kExitStageFailure;
                result.error = "The selected refinement test WAV does not exist.";
                fs::remove_all(work, ec);
                return result;
            }

            report(status, L"Preparing matched refinement stimulus (first 20 s of selected WAV)...");
            StimulusConfig refineStimulusConfig = stimulus;
            refineStimulusConfig.tailMode = TailMode::RecordedAudio;
            refineStimulusConfig.recordedAudio = refine.referenceWav;
            refineStimulus = work / L"refine_input_wav.wav";
            if (!buildStimulus(runtime, refineStimulusConfig, refineStimulus, error)) {
                result.exitCode = kExitStageFailure;
                result.error = "Could not prepare refinement test WAV: " + error;
                fs::remove_all(work, ec);
                return result;
            }

            // A second HTUSBTools pass is used only to obtain the NAM render of
            // the matched refinement stimulus. The generated CLO is intentionally
            // ignored: refinement is applied to the ORIGINAL CLO from the first pass.
            WorkerOptions refineWorker;
            refineWorker.dll = runtime.dll;
            refineWorker.inputWav = refineStimulus;
            refineWorker.outputWav = work / L"refine_nam_output.wav";
            refineWorker.inputNam = worker.inputNam;
            refineWorker.outputClo = work / L"refine_unused_2048.clo";
            refineWorker.diagnosticLog = diagnosticLog;
            refineWorker.crashDump = worker.crashDump;
            appendDiagnostic(diagnosticLog, "=== SECOND HTUSBTools PASS: matched-input refinement render ===");

            SharedBuffer refineShared;
            if (!createSharedBuffer(refineShared, error)) {
                result.exitCode = kExitMappingFailure;
                result.error = error;
                fs::remove_all(work, ec);
                return result;
            }
            refineWorker.mappingName = refineShared.name;

            report(status, L"Rendering refinement stimulus through NAM Full...");
            bool refineCapturedValid = false;
            const int refineWorkerExit = launchWorker(refineWorker, refineShared, refineCapturedValid, error);
            if (refineWorkerExit != kExitOk || !refineCapturedValid || !valid2048(refineWorker.outputClo)) {
                result.exitCode = refineWorkerExit != kExitOk ? refineWorkerExit : kExitConversionBadSize;
                result.error = error.empty()
                    ? "Could not render the refinement stimulus through the NAM. Diagnostic log: " + pathToUtf8(diagnosticLog)
                    : error;
                fs::remove_all(work, ec);
                return result;
            }
            refineTarget = refineWorker.outputWav;
            report(status, L"Tone Match: same refinement stimulus through NAM Full vs original CLO.");
        } else {
            report(status, L"Tone Match: original conversion stimulus through NAM Full vs original CLO.");
        }

        refinedWorkClo = work / L"ampero_2048_REFINED.clo";
        bestWorkClo = work / L"ampero_2048_REFINED_CANDIDATE.clo";
        if (!refineCloBOnly(worker.outputClo, refineStimulus, refineTarget, refinedWorkClo, bestWorkClo,
                           refine, refineStats, error, status)
            || !valid2048(refinedWorkClo)) {
            result.exitCode = kExitStageFailure;
            result.error = error.empty() ? "CLO refinement failed." : error;
            fs::remove_all(work, ec);
            return result;
        }

        result.hasRefineStats = true;
        result.refineMetricImproved = refineStats.improved;
        result.refineOriginalResponseSpectralError = refineStats.originalResponseSpectralError;
        result.refineResponseSpectralError = refineStats.refinedResponseSpectralError;
        result.refineResponseSpectralImprovementPercent = refineStats.responseSpectralImprovementPercent;
        result.refineDecisionReason = refineStats.searchedDecisionReason;

        if (status) {
            std::wostringstream refineSummary;
            refineSummary << std::fixed << std::setprecision(3)
                          << L"Tone Match metric: before " << result.refineOriginalResponseSpectralError
                          << L" dB, after " << result.refineResponseSpectralError
                          << L" dB, change " << std::showpos << std::setprecision(2)
                          << result.refineResponseSpectralImprovementPercent << L"%" << std::noshowpos
                          << L". REFINED correction applied ("
                          << (result.refineMetricImproved ? L"metric improved" : L"metric did not improve")
                          << L").";
            status(refineSummary.str());
        }
    }

    fs::path sourceForOutput = worker.outputClo;
    if (correction.enabled) {
        if (correction.wav.empty()) {
            result.exitCode = kExitStageFailure;
            result.error = "Select a Corrective IR WAV file.";
            fs::remove_all(work, ec);
            return result;
        }

        report(status, L"Applying corrective IR...");
        const fs::path correctedClo = work / L"ampero_2048_corrected.clo";
        CorrectiveIrStats correctionStats;
        if (!applyCorrectiveIrToClo(worker.outputClo, correction.wav, correctedClo, correctionStats, error)
            || !valid2048(correctedClo)) {
            result.exitCode = kExitStageFailure;
            result.error = error.empty() ? "The corrected Ampero CLO failed validation." : error;
            fs::remove_all(work, ec);
            return result;
        }

        const std::wstring correctionStatus =
            L"Corrective IR applied: linear convolution, RMS match, -6 dB post gain. RMS gain "
            + std::to_wstring(correctionStats.rmsGainDb) + L" dB; total "
            + std::to_wstring(correctionStats.totalGainDb) + L" dB.";
        if (status) status(correctionStatus);
        sourceForOutput = correctedClo;
    }

    const std::wstring base = inputNam.stem().wstring();
    result.ampero2048 = uniquePath(outDir / (base + L"_Ampero_2048.clo"));
    if (!copyFileCreatingParents(sourceForOutput, result.ampero2048, error)) {
        result.exitCode = kExitCopyFailure;
        result.error = error;
        fs::remove_all(work, ec);
        return result;
    }

    report(status, L"Generating GP-200 1024 compact CLO...");
    result.gp2001024 = uniquePath(outDir / (base + L"_GP200_1024.clo"));
    if (!makeGp200CompactClo(sourceForOutput, result.gp2001024, error) || !valid1024(result.gp2001024)) {
        result.exitCode = kExitCopyFailure;
        result.error = error.empty() ? "The GP-200 compact CLO failed validation." : error;
        fs::remove_all(work, ec);
        return result;
    }

    if (refine.enabled) {
        report(status, L"Generating refined GP-200 1024 compact CLO...");
        result.refinedGp2001024 = uniquePath(outDir / (base + L"_GP200_1024_REFINED.clo"));
        if (!makeGp200CompactClo(refinedWorkClo, result.refinedGp2001024, error)
            || !valid1024(result.refinedGp2001024)) {
            result.exitCode = kExitCopyFailure;
            result.error = error.empty() ? "The refined GP-200 compact CLO failed validation." : error;
            fs::remove_all(work, ec);
            return result;
        }
    }

    appendDiagnostic(diagnosticLog, "=== Conversion completed successfully ===");
    fs::remove_all(work, ec);
    result.ok = true;
    result.exitCode = kExitOk;
    report(status, L"Done.");
    return result;
}


BatchConversionResult convertNamFolder(const fs::path& inputDirectory, const fs::path& outputDirectory,
                                       const StimulusConfig stimulus, const CorrectiveIrConfig correction,
                                       const CloRefineConfig refine, const StatusCallback& status) {
    BatchConversionResult batch;
    std::error_code ec;
    if (!fs::exists(inputDirectory, ec) || ec || !fs::is_directory(inputDirectory, ec)) {
        return batch;
    }

    std::vector<fs::path> namFiles;
    for (const auto& entry : fs::directory_iterator(inputDirectory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
        std::wstring ext = entry.path().extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
        if (ext == L".nam") namFiles.push_back(entry.path());
    }

    std::sort(namFiles.begin(), namFiles.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename().wstring() < b.filename().wstring();
    });

    batch.total = namFiles.size();
    if (namFiles.empty()) return batch;

    const fs::path outDir = outputDirectory.empty() ? inputDirectory : outputDirectory;
    batch.items.reserve(namFiles.size());

    for (std::size_t i = 0; i < namFiles.size(); ++i) {
        const auto& nam = namFiles[i];
        if (status) {
            status(L"[" + std::to_wstring(i + 1) + L"/" + std::to_wstring(namFiles.size()) +
                   L"] " + nam.filename().wstring());
        }
        auto item = convertNamToBoth(nam, outDir, stimulus, correction, refine, [&, i, nam](const std::wstring& text) {
            if (status) {
                status(L"[" + std::to_wstring(i + 1) + L"/" + std::to_wstring(namFiles.size()) +
                       L"] " + nam.filename().wstring() + L" - " + text);
            }
        });
        if (item.ok) ++batch.succeeded;
        else ++batch.failed;
        batch.items.push_back(std::move(item));
    }

    batch.ok = batch.failed == 0 && batch.succeeded == batch.total;
    return batch;
}

int runWorkerCommandLine(int argc, wchar_t** argv) {
    WorkerOptions w;
    if (!parseWorker(argc, argv, w)) return kExitUsage;
    return runWorker(w);
}

} // namespace ntc
