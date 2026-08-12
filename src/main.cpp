#include "common.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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
constexpr int kExitConversionNoClo = 31;
constexpr int kExitConversionBadSize = 32;
constexpr int kExitCopyFailure = 40;
constexpr int kExitMappingFailure = 41;
constexpr int kExitSehBase = 100;
constexpr std::uint8_t kBufferSentinel = 0xCC;

enum class ConvertMode {
    Data,
    File,
};

enum class RuntimeKind {
    Auto,
    Ampero,
    Sonicake,
};

struct RuntimePaths {
    fs::path dll;
    fs::path stimulus;
    RuntimeKind kind = RuntimeKind::Auto;
};

struct ConvertOptions {
    fs::path inputNam;
    fs::path outputClo;
    fs::path amperoDir;
    fs::path sonicakeDir;
    fs::path dll;
    fs::path stimulus;
    RuntimeKind runtimeKind = RuntimeKind::Auto;
    int timeoutSeconds = 180;
    bool keepTemp = false;
    bool verbose = false;
    ConvertMode mode = ConvertMode::Data;
    std::uint64_t arg6 = 0;
    bool gp200Size = false;
    bool gp200Rate = false;
    bool gp200Compact = false;
};

struct WorkerOptions {
    fs::path dll;
    fs::path inputWav;
    fs::path outputWav;
    fs::path inputNam;
    fs::path outputClo;
    std::wstring mappingName;
    int timeoutSeconds = 180;
    bool verbose = false;
    ConvertMode mode = ConvertMode::Data;
    std::uint64_t arg6 = 0;
    bool gp200Size = false;
    bool gp200Rate = false;
    RuntimeKind runtimeKind = RuntimeKind::Auto;
};

struct SharedBuffer {
    HANDLE mapping = nullptr;
    std::uint8_t* view = nullptr;
    std::wstring name;

    ~SharedBuffer() {
        if (view) {
            UnmapViewOfFile(view);
        }
        if (mapping) {
            CloseHandle(mapping);
        }
    }

    SharedBuffer() = default;
    SharedBuffer(const SharedBuffer&) = delete;
    SharedBuffer& operator=(const SharedBuffer&) = delete;
};

void printBanner() {
    std::wcout << L"NamToCloResearch " << ntc::kVersion << L" - experimental Ampero/Sonicake NAM->CLO probe\n";
}

const char* modeName(ConvertMode mode) {
    return mode == ConvertMode::Data ? "data" : "file";
}

const char* runtimeKindName(RuntimeKind kind) {
    switch (kind) {
        case RuntimeKind::Ampero: return "ampero";
        case RuntimeKind::Sonicake: return "sonicake";
        default: return "auto";
    }
}

std::optional<RuntimeKind> parseRuntimeKind(const std::wstring& text) {
    if (text == L"auto") return RuntimeKind::Auto;
    if (text == L"ampero") return RuntimeKind::Ampero;
    if (text == L"sonicake") return RuntimeKind::Sonicake;
    return std::nullopt;
}

void printHelp() {
    printBanner();
    std::cout << R"HELP(

USAGE
  NamToClo.exe input.nam output.clo [options]
  NamToClo.exe --check-runtime [options]
  NamToClo.exe --inspect file.clo
  NamToClo.exe --compare-gp200 candidate.clo reference.clo
  NamToClo.exe --clo-rate input.clo 44100|48000|96000 output.clo [runtime options]
  NamToClo.exe --clo-rate-matrix input.clo output-dir [runtime options]
  NamToClo.exe --cross-runtime input.nam [--output-dir dir] [--reference file.clo] [--verbose]
  NamToClo.exe --help
  NamToClo.exe --version

OPTIONS
  --mode data|file       Conversion API to probe. Default: data.
                         data = namConvertCloData + shared 0x2288-byte buffer.
                         file = legacy namConvertClo path used by v0.1.
  --provider <kind>      auto|ampero|sonicake. Default: auto.
  --ampero-dir <dir>     Root directory of an Ampero II installation/package.
  --sonicake-dir <dir>   Root directory of a Sonicake Manager installation/package.
  --dll <path>           Explicit path to HTUSBTools.dll or 5868USB.dll.
  --stimulus <path>      Explicit path to nam_input_wav.wav.
  --timeout <seconds>    Conversion timeout. Default: 180.
  --arg6 <value>         Experimental 6th argument for namConvertCloData.
                         Accepts decimal or 0x-prefixed hexadecimal. Default: 0.
  --gp200-size           Patch only the DSP model length: 2048.0f -> 1024.0f.
  --gp200-rate           Patch only the CLO-stage sample-rate load: 44100.0f -> 48000.0f.
  --gp200-combined       Apply both GP-200 experimental patches.
  --gp200-probe          Legacy alias for --gp200-size (v0.4 behaviour).
  --gp200-compact        Keep normal 2048-coefficient DSP, serialize GP-200 compact shape.
  --keep-temp            Preserve staged files and captured-buffer.bin.
  --verbose              Print additional research diagnostics.

RUNTIME DISCOVERY
  Resolution order is:
    1. --dll / --stimulus
    2. --ampero-dir
    3. AMPERO_II_DIR environment variable
    4. runtime/ampero or runtime/sonicake next to NamToClo.exe
    5. legacy runtime/ next to NamToClo.exe

IMPORTANT
  v0.7.2 can run the same namConvertCloData probe against Hotone HTUSBTools.dll or
  Sonicake 5868USB.dll. The proprietary DLLs/WAVs are not distributed. The new
  --clo-rate commands call Sonicake's exported cloConvertSampleRate(VTSI*, double)
  in an isolated worker and preserve the original input file.
)HELP";
}

bool hasValue(int i, int argc) {
    return i + 1 < argc;
}

std::optional<int> parsePositiveInt(const std::wstring& text) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used, 10);
        if (used != text.size() || value <= 0) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint64_t> parseUint64(const std::wstring& text) {
    try {
        std::size_t used = 0;
        const auto value = std::stoull(text, &used, 0);
        if (used != text.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<ConvertMode> parseMode(const std::wstring& text) {
    if (text == L"data") {
        return ConvertMode::Data;
    }
    if (text == L"file") {
        return ConvertMode::File;
    }
    return std::nullopt;
}

fs::path findFirstExisting(const std::vector<fs::path>& candidates) {
    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && fs::exists(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }
    return {};
}

RuntimePaths discoverFromRoot(const fs::path& root, RuntimeKind preferred = RuntimeKind::Auto) {
    RuntimePaths paths;
    if (root.empty()) return paths;

    auto findAmperoDll = [&]() {
        return findFirstExisting({
            root / L"assets" / L"HTUSBTools.dll",
            root / L"data" / L"flutter_assets" / L"assets" / L"HTUSBTools.dll",
            root / L"HTUSBTools.dll",
        });
    };
    auto findSonicakeDll = [&]() {
        return findFirstExisting({
            root / L"assets" / L"5868USB.dll",
            root / L"data" / L"flutter_assets" / L"assets" / L"5868USB.dll",
            root / L"5868USB.dll",
        });
    };

    if (preferred == RuntimeKind::Sonicake) {
        paths.dll = findSonicakeDll();
        if (!paths.dll.empty()) paths.kind = RuntimeKind::Sonicake;
    } else if (preferred == RuntimeKind::Ampero) {
        paths.dll = findAmperoDll();
        if (!paths.dll.empty()) paths.kind = RuntimeKind::Ampero;
    } else {
        paths.dll = findAmperoDll();
        if (!paths.dll.empty()) {
            paths.kind = RuntimeKind::Ampero;
        } else {
            paths.dll = findSonicakeDll();
            if (!paths.dll.empty()) paths.kind = RuntimeKind::Sonicake;
        }
    }

    paths.stimulus = findFirstExisting({
        root / L"data" / L"flutter_assets" / L"assets" / L"wavs" / L"nam_input_wav.wav",
        root / L"assets" / L"wavs" / L"nam_input_wav.wav",
        root / L"wavs" / L"nam_input_wav.wav",
        root / L"nam_input_wav.wav",
    });
    return paths;
}

RuntimeKind detectRuntimeKindFromDll(const fs::path& dll) {
    std::wstring name = dll.filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), ::towlower);
    if (name == L"htusbtools.dll") return RuntimeKind::Ampero;
    if (name == L"5868usb.dll") return RuntimeKind::Sonicake;
    return RuntimeKind::Auto;
}

RuntimePaths resolveRuntime(const ConvertOptions& options) {
    RuntimePaths result{options.dll, options.stimulus, options.runtimeKind};
    if (!result.dll.empty() && result.kind == RuntimeKind::Auto) {
        result.kind = detectRuntimeKindFromDll(result.dll);
    }

    auto normalize = [](fs::path& path) {
        if (path.empty()) return;
        std::error_code ec;
        const fs::path absolute = fs::absolute(path, ec);
        if (!ec) path = absolute;
    };

    auto fillMissing = [&result](const RuntimePaths& candidate) {
        if (result.dll.empty() && !candidate.dll.empty()) {
            result.dll = candidate.dll;
            if (result.kind == RuntimeKind::Auto) result.kind = candidate.kind;
        }
        if (result.stimulus.empty()) result.stimulus = candidate.stimulus;
    };

    if (options.runtimeKind != RuntimeKind::Sonicake && !options.amperoDir.empty())
        fillMissing(discoverFromRoot(options.amperoDir, RuntimeKind::Ampero));
    if (options.runtimeKind != RuntimeKind::Ampero && !options.sonicakeDir.empty())
        fillMissing(discoverFromRoot(options.sonicakeDir, RuntimeKind::Sonicake));

    if (result.dll.empty() || result.stimulus.empty()) {
        wchar_t buffer[32768]{};
        constexpr DWORD bufferCount = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
        if (options.runtimeKind != RuntimeKind::Sonicake) {
            const DWORD len = GetEnvironmentVariableW(L"AMPERO_II_DIR", buffer, bufferCount);
            if (len > 0 && len < bufferCount) fillMissing(discoverFromRoot(fs::path(buffer), RuntimeKind::Ampero));
        }
        if (options.runtimeKind != RuntimeKind::Ampero && (result.dll.empty() || result.stimulus.empty())) {
            const DWORD len = GetEnvironmentVariableW(L"SONICAKE_MANAGER_DIR", buffer, bufferCount);
            if (len > 0 && len < bufferCount) fillMissing(discoverFromRoot(fs::path(buffer), RuntimeKind::Sonicake));
        }
    }

    if (result.dll.empty() || result.stimulus.empty()) {
        const fs::path exe = ntc::executablePath();
        if (!exe.empty()) {
            if (options.runtimeKind == RuntimeKind::Sonicake)
                fillMissing(discoverFromRoot(exe.parent_path() / L"runtime" / L"sonicake", RuntimeKind::Sonicake));
            else if (options.runtimeKind == RuntimeKind::Ampero)
                fillMissing(discoverFromRoot(exe.parent_path() / L"runtime" / L"ampero", RuntimeKind::Ampero));
            else {
                fillMissing(discoverFromRoot(exe.parent_path() / L"runtime" / L"ampero", RuntimeKind::Ampero));
                if (result.dll.empty() || result.stimulus.empty())
                    fillMissing(discoverFromRoot(exe.parent_path() / L"runtime" / L"sonicake", RuntimeKind::Sonicake));
                if (result.dll.empty() || result.stimulus.empty())
                    fillMissing(discoverFromRoot(exe.parent_path() / L"runtime", RuntimeKind::Auto));
            }
        }
    }

    normalize(result.dll);
    normalize(result.stimulus);
    if (result.kind == RuntimeKind::Auto && !result.dll.empty()) result.kind = detectRuntimeKindFromDll(result.dll);
    return result;
}

bool validateRuntimeFiles(const RuntimePaths& paths) {
    std::error_code ec;
    if (paths.dll.empty() || !fs::exists(paths.dll, ec) || ec) {
        std::cerr << "ERROR: runtime DLL was not found. Use --ampero-dir, --sonicake-dir or --dll.\n";
        return false;
    }
    ec.clear();
    if (paths.stimulus.empty() || !fs::exists(paths.stimulus, ec) || ec) {
        std::cerr << "ERROR: nam_input_wav.wav was not found. Use --ampero-dir or --stimulus.\n";
        return false;
    }
    return true;
}

int checkRuntime(const RuntimePaths& paths, bool verbose) {
    if (!validateRuntimeFiles(paths)) {
        return kExitRuntimeMissing;
    }

    std::cout << "Provider: " << runtimeKindName(paths.kind) << "\n";
    std::cout << "DLL:      " << ntc::pathToUtf8(paths.dll) << "\n";
    std::cout << "Stimulus: " << ntc::pathToUtf8(paths.stimulus) << "\n";

    std::error_code ec;
    const auto dllSize = fs::file_size(paths.dll, ec);
    if (!ec) {
        std::cout << "DLL size: " << dllSize << " bytes\n";
    }
    ec.clear();
    const auto wavSize = fs::file_size(paths.stimulus, ec);
    if (!ec) {
        std::cout << "WAV size: " << wavSize << " bytes\n";
    }

    HMODULE module = LoadLibraryExW(paths.dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        const DWORD err = GetLastError();
        std::cerr << "ERROR: LoadLibraryExW failed: " << ntc::win32ErrorMessage(err)
                  << " (" << ntc::hex32(err) << ")\n";
        return kExitDllLoad;
    }

    const char* exports[] = {
        "namConvertClo",
        "namConvertCloData",
        "cloConvertSampleRate",
        "getNormalWav",
        "initWithWaveDicPath",
        "initWithWaveDicPathJson",
        "InitDartApiDL",
    };

    bool missingRequired = false;
    for (const char* name : exports) {
        const FARPROC proc = GetProcAddress(module, name);
        std::cout << "  " << name << ": " << (proc ? "present" : "MISSING");
        if (verbose && proc) {
            std::cout << " @ 0x" << std::hex << std::uppercase
                      << reinterpret_cast<std::uintptr_t>(proc) << std::dec;
        }
        std::cout << "\n";
        if (!proc && (std::string(name) == "namConvertClo" || std::string(name) == "namConvertCloData")) {
            missingRequired = true;
        }
    }

    FreeLibrary(module);
    return missingRequired ? kExitExportMissing : kExitOk;
}

fs::path makeWorkDirectory() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const DWORD pid = GetCurrentProcessId();
    fs::path base = fs::temp_directory_path() / L"NamToCloResearch";
    std::error_code ec;
    fs::create_directories(base, ec);
    fs::path work = base / (L"job-" + std::to_wstring(pid) + L"-" + std::to_wstring(now));
    fs::create_directories(work, ec);
    if (ec) {
        return {};
    }
    return work;
}

bool stageInputFiles(const RuntimePaths& runtime,
                     const ConvertOptions& options,
                     const fs::path& work,
                     WorkerOptions& worker,
                     std::string& error) {
    worker.dll = runtime.dll;
    worker.inputWav = work / L"nam_input_wav.wav";
    worker.outputWav = work / L"outputFile.wav";
    worker.inputNam = work / L"input.nam";
    worker.outputClo = work / L"output.clo";
    worker.timeoutSeconds = options.timeoutSeconds;
    worker.verbose = options.verbose;
    worker.mode = options.mode;
    worker.arg6 = options.arg6;
    worker.gp200Size = options.gp200Size;
    worker.gp200Rate = options.gp200Rate;
    worker.runtimeKind = runtime.kind;

    if (!ntc::copyFileCreatingParents(runtime.stimulus, worker.inputWav, error)) {
        return false;
    }
    if (!ntc::copyFileCreatingParents(options.inputNam, worker.inputNam, error)) {
        return false;
    }
    return true;
}

std::wstring makeMappingName() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return L"Local\\NamToCloResearch-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(now);
}

bool createSharedBuffer(SharedBuffer& out, std::string& error) {
    out.name = makeMappingName();
    out.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     static_cast<DWORD>(ntc::kExpectedCloSize), out.name.c_str());
    if (!out.mapping) {
        const DWORD code = GetLastError();
        error = "CreateFileMappingW failed: " + ntc::win32ErrorMessage(code) + " (" + ntc::hex32(code) + ")";
        return false;
    }
    out.view = static_cast<std::uint8_t*>(MapViewOfFile(out.mapping, FILE_MAP_ALL_ACCESS, 0, 0, ntc::kExpectedCloSize));
    if (!out.view) {
        const DWORD code = GetLastError();
        error = "MapViewOfFile failed: " + ntc::win32ErrorMessage(code) + " (" + ntc::hex32(code) + ")";
        return false;
    }
    std::memset(out.view, kBufferSentinel, static_cast<std::size_t>(ntc::kExpectedCloSize));
    return true;
}

std::wstring makeWorkerCommandLine(const WorkerOptions& worker) {
    const fs::path exe = ntc::executablePath();
    std::vector<std::wstring> args = {
        exe.wstring(),
        L"--worker",
        L"--mode", worker.mode == ConvertMode::Data ? L"data" : L"file",
        L"--dll", worker.dll.wstring(),
        L"--provider", ntc::fromUtf8(runtimeKindName(worker.runtimeKind)),
        L"--input-wav", worker.inputWav.wstring(),
        L"--output-wav", worker.outputWav.wstring(),
        L"--nam", worker.inputNam.wstring(),
        L"--clo", worker.outputClo.wstring(),
        L"--timeout", std::to_wstring(worker.timeoutSeconds),
    };
    if (worker.mode == ConvertMode::Data) {
        args.push_back(L"--mapping");
        args.push_back(worker.mappingName);
        args.push_back(L"--arg6");
        args.push_back(std::to_wstring(worker.arg6));
        if (worker.gp200Size) args.push_back(L"--gp200-size");
        if (worker.gp200Rate) args.push_back(L"--gp200-rate");
    }
    if (worker.verbose) {
        args.push_back(L"--verbose");
    }

    std::wstring command;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            command.push_back(L' ');
        }
        command += ntc::quoteWindowsArg(args[i]);
    }
    return command;
}

std::size_t changedByteCount(const std::uint8_t* data) {
    std::size_t changed = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(ntc::kExpectedCloSize); ++i) {
        if (data[i] != kBufferSentinel) {
            ++changed;
        }
    }
    return changed;
}

bool startsWithVtsi(const std::uint8_t* data) {
    return data[0] == 'V' && data[1] == 'T' && data[2] == 'S' && data[3] == 'I';
}

bool writeRawBuffer(const fs::path& path, const std::uint8_t* data, std::size_t size, std::string& error) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "Cannot create directory for captured buffer: " + ec.message();
            return false;
        }
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "Cannot create '" + ntc::pathToUtf8(path) + "'.";
        return false;
    }
    stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!stream) {
        error = "Failed while writing '" + ntc::pathToUtf8(path) + "'.";
        return false;
    }
    return true;
}

int launchWorker(const WorkerOptions& worker, SharedBuffer* shared, const fs::path& work, bool& capturedValid) {
    capturedValid = false;
    std::wstring command = makeWorkerCommandLine(worker);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (worker.verbose) {
        std::cout << "Worker command: " << ntc::toUtf8(command) << "\n";
    }

    const BOOL ok = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0,
                                   nullptr, nullptr, &si, &pi);
    if (!ok) {
        const DWORD err = GetLastError();
        std::cerr << "ERROR: CreateProcessW failed: " << ntc::win32ErrorMessage(err)
                  << " (" << ntc::hex32(err) << ")\n";
        return kExitWorkerLaunch;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(worker.timeoutSeconds + 15);
    std::size_t lastChanged = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const DWORD wait = WaitForSingleObject(pi.hProcess, 100);

        if (shared && shared->view) {
            const std::size_t changed = changedByteCount(shared->view);
            if (changed != lastChanged) {
                if (worker.verbose || changed > 0) {
                    std::cout << "[parent] shared CLO buffer changed: " << changed
                              << "/" << ntc::kExpectedCloSize << " bytes differ from 0xCC";
                    if (startsWithVtsi(shared->view)) {
                        std::cout << " [VTSI detected]";
                    }
                    std::cout << "\n";
                }
                lastChanged = changed;
            }
        }

        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait == WAIT_FAILED) {
            break;
        }
    }

    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    if (exitCode == STILL_ACTIVE) {
        std::cerr << "ERROR: Worker exceeded the parent timeout and will be terminated.\n";
        TerminateProcess(pi.hProcess, static_cast<UINT>(kExitWorkerTimeout));
        WaitForSingleObject(pi.hProcess, 2000);
        exitCode = kExitWorkerTimeout;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (shared && shared->view) {
        const std::size_t changed = changedByteCount(shared->view);
        const fs::path captured = work / L"captured-buffer.bin";
        std::string error;
        if (!writeRawBuffer(captured, shared->view, static_cast<std::size_t>(ntc::kExpectedCloSize), error)) {
            std::cerr << "Warning: " << error << "\n";
        } else {
            std::cout << "Captured shared buffer: " << ntc::pathToUtf8(captured) << "\n";
            std::cout << "Captured changed bytes: " << changed << "/" << ntc::kExpectedCloSize << "\n";
            std::cout << "Captured prefix: ";
            for (std::size_t i = 0; i < 16; ++i) {
                const unsigned v = shared->view[i];
                static const char* hex = "0123456789ABCDEF";
                std::cout << hex[(v >> 4) & 0xF] << hex[v & 0xF] << (i == 15 ? '\n' : ' ');
            }
        }

        if (startsWithVtsi(shared->view) && changed > 0) {
            capturedValid = true;
            if (!writeRawBuffer(worker.outputClo, shared->view, static_cast<std::size_t>(ntc::kExpectedCloSize), error)) {
                std::cerr << "Warning: could not materialize staged CLO: " << error << "\n";
                capturedValid = false;
            } else {
                std::cout << "[parent] VTSI buffer materialized as: " << ntc::pathToUtf8(worker.outputClo) << "\n";
            }
        }
    }

    if (exitCode >= 0xC0000000u) {
        std::cerr << "ERROR: Worker terminated with Windows exception " << ntc::hex32(exitCode)
                  << ". Shared-buffer evidence was preserved before returning.\n";
        if (capturedValid) {
            std::cout << "[parent] A VTSI-shaped 0x2288-byte buffer survived the worker crash.\n";
            return kExitOk;
        }
        return static_cast<int>(exitCode & 0x7FFFFFFFu);
    }

    if (capturedValid) {
        return kExitOk;
    }
    return static_cast<int>(exitCode);
}

using NamConvertCloFn = std::uint32_t(__cdecl*)(void*, const char*, const char*, const char*, const char*);
using NamConvertCloDataFn = std::uint32_t(__cdecl*)(void*, const char*, const char*, const char*, void*, std::uint64_t);

DWORD invokeFileWithSeh(NamConvertCloFn fn,
                        const char* inputWav,
                        const char* outputWav,
                        const char* inputNam,
                        const char* outputClo,
                        std::uint32_t* apiReturn) noexcept {
#if defined(_MSC_VER)
    __try {
        *apiReturn = fn(nullptr, inputWav, outputWav, inputNam, outputClo);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
#else
    *apiReturn = fn(nullptr, inputWav, outputWav, inputNam, outputClo);
    return 0;
#endif
}

DWORD invokeDataWithSeh(NamConvertCloDataFn fn,
                        const char* inputWav,
                        const char* outputWav,
                        const char* inputNam,
                        void* outputBuffer,
                        std::uint64_t arg6,
                        std::uint32_t* apiReturn) noexcept {
#if defined(_MSC_VER)
    __try {
        *apiReturn = fn(nullptr, inputWav, outputWav, inputNam, outputBuffer, arg6);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
#else
    *apiReturn = fn(nullptr, inputWav, outputWav, inputNam, outputBuffer, arg6);
    return 0;
#endif
}

int observeWorkerOutput(const WorkerOptions& options, std::uint8_t* mappedData) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.timeoutSeconds);
    bool sawOutputWav = false;
    std::size_t lastChanged = 0;
    int stablePolls = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code ec;
        if (!sawOutputWav && fs::exists(options.outputWav, ec) && !ec) {
            sawOutputWav = true;
            std::cout << "[worker] observed output WAV: " << ntc::pathToUtf8(options.outputWav) << "\n";
        }

        if (options.mode == ConvertMode::File) {
            ec.clear();
            if (fs::exists(options.outputClo, ec) && !ec) {
                const auto size = fs::file_size(options.outputClo, ec);
                if (!ec && size == ntc::kExpectedCloSize) {
                    const ntc::CloInfo info = ntc::inspectClo(options.outputClo);
                    ntc::printCloInfo(options.outputClo, info);
                    return kExitOk;
                }
            }
        } else if (mappedData) {
            const std::size_t changed = changedByteCount(mappedData);
            if (changed > 0) {
                if (changed == lastChanged) {
                    ++stablePolls;
                } else {
                    lastChanged = changed;
                    stablePolls = 0;
                    if (options.verbose) {
                        std::cout << "[worker] mapped buffer changed: " << changed << " bytes";
                        if (startsWithVtsi(mappedData)) {
                            std::cout << " [VTSI]";
                        }
                        std::cout << "\n";
                    }
                }
                if (startsWithVtsi(mappedData) && stablePolls >= 10) {
                    std::cout << "[worker] shared buffer is VTSI-shaped and stable.\n";
                    return kExitOk;
                }
            }
        }
        std::this_thread::sleep_for(100ms);
    }

    std::cerr << "[worker] ERROR conversion observation timed out.\n";
    return kExitConversionTimeout;
}


bool patchBytes(HMODULE module, std::uintptr_t rva,
                const std::uint8_t* expected, const std::uint8_t* replacement,
                std::size_t size, const char* label, bool verbose) {
    auto* target = reinterpret_cast<std::uint8_t*>(module) + rva;
    if (std::memcmp(target, expected, size) != 0) {
        std::cerr << "[worker] ERROR " << label << " patch validation failed at RVA 0x"
                  << std::hex << std::uppercase << rva << std::dec
                  << ". DLL version/layout does not match the analyzed build.\n";
        return false;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        std::cerr << "[worker] ERROR VirtualProtect for " << label << " patch failed.\n";
        return false;
    }
    std::memcpy(target, replacement, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    DWORD ignored = 0;
    VirtualProtect(target, size, oldProtect, &ignored);
    if (verbose) {
        std::cout << "[worker] " << label << " patch applied at runtime DLL+0x"
                  << std::hex << std::uppercase << rva << std::dec << "\n";
    }
    return true;
}

bool patchGp200ModelLength(HMODULE module, bool verbose) {
    constexpr std::uintptr_t kModelLengthFloatRva = 0x2DEAF0;
    const float expectedFloat = 2048.0f;
    const float replacementFloat = 1024.0f;
    std::uint8_t expected[sizeof(float)]{};
    std::uint8_t replacement[sizeof(float)]{};
    std::memcpy(expected, &expectedFloat, sizeof(float));
    std::memcpy(replacement, &replacementFloat, sizeof(float));
    if (!patchBytes(module, kModelLengthFloatRva, expected, replacement, sizeof(float),
                    "GP200 size 2048.0f -> 1024.0f", verbose)) {
        return false;
    }
    if (verbose) {
        std::cout << "[worker] GP200 size detail: model-count source constant 2048.0f -> 1024.0f\n";
    }
    return true;
}

bool patchGp200CloRate(HMODULE module, bool verbose) {
    // In the VTSI-building function, RVA 0xA001D loads 44100.0f from RVA 0x2DEB20.
    // Retarget only this RIP-relative MOVSS to the adjacent 48000.0f at RVA 0x2DEB24.
    // This avoids altering unrelated 44.1 kHz comparisons/resampling code elsewhere.
    constexpr std::uintptr_t kRateLoadInstructionRva = 0xA001D;
    constexpr std::uint8_t expected[] = {0xF3,0x0F,0x10,0x35,0xFB,0xEA,0x23,0x00};
    constexpr std::uint8_t replacement[] = {0xF3,0x0F,0x10,0x35,0xFF,0xEA,0x23,0x00};
    if (!patchBytes(module, kRateLoadInstructionRva, expected, replacement, sizeof(expected),
                    "GP200 CLO-rate 44100.0f -> 48000.0f", verbose)) {
        return false;
    }
    if (verbose) {
        std::cout << "[worker] GP200 rate detail: VTSI-stage MOVSS retargeted RVA 0x2DEB20 -> 0x2DEB24\n";
    }
    return true;
}

int runWorker(const WorkerOptions& options) {
    std::cout << "[worker] loading: " << ntc::pathToUtf8(options.dll) << "\n";
    HMODULE module = LoadLibraryExW(options.dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        const DWORD err = GetLastError();
        std::cerr << "[worker] ERROR LoadLibraryExW: " << ntc::win32ErrorMessage(err)
                  << " (" << ntc::hex32(err) << ")\n";
        return kExitDllLoad;
    }

    if ((options.gp200Size || options.gp200Rate) && options.runtimeKind == RuntimeKind::Sonicake) {
        std::cerr << "[worker] ERROR GP-200 binary patches are specific to the analyzed HTUSBTools.dll build and are disabled for Sonicake.\n";
        return kExitStageFailure;
    }

    if (options.gp200Size && !patchGp200ModelLength(module, options.verbose)) {
        return kExitStageFailure;
    }
    if (options.gp200Rate && !patchGp200CloRate(module, options.verbose)) {
        return kExitStageFailure;
    }

    const std::string inputWav = ntc::pathToUtf8(options.inputWav);
    const std::string outputWav = ntc::pathToUtf8(options.outputWav);
    const std::string inputNam = ntc::pathToUtf8(options.inputNam);
    const std::string outputClo = ntc::pathToUtf8(options.outputClo);

    HANDLE mapping = nullptr;
    std::uint8_t* mappedData = nullptr;
    if (options.mode == ConvertMode::Data) {
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, options.mappingName.c_str());
        if (!mapping) {
            const DWORD err = GetLastError();
            std::cerr << "[worker] ERROR OpenFileMappingW: " << ntc::win32ErrorMessage(err)
                      << " (" << ntc::hex32(err) << ")\n";
            return kExitMappingFailure;
        }
        mappedData = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, ntc::kExpectedCloSize));
        if (!mappedData) {
            const DWORD err = GetLastError();
            std::cerr << "[worker] ERROR MapViewOfFile: " << ntc::win32ErrorMessage(err)
                      << " (" << ntc::hex32(err) << ")\n";
            CloseHandle(mapping);
            return kExitMappingFailure;
        }
    }

    std::uint32_t apiReturn = 0;
    DWORD exceptionCode = 0;

    if (options.mode == ConvertMode::Data) {
        auto fn = reinterpret_cast<NamConvertCloDataFn>(GetProcAddress(module, "namConvertCloData"));
        if (!fn) {
            std::cerr << "[worker] ERROR export namConvertCloData not found.\n";
            return kExitExportMissing;
        }

        if (options.verbose) {
            std::cout << "[worker] ABI hypothesis for namConvertCloData:\n"
                      << "  arg1 context      = nullptr\n"
                      << "  arg2 inputWav     = " << inputWav << "\n"
                      << "  arg3 outputWav    = " << outputWav << "\n"
                      << "  arg4 inputNam     = " << inputNam << "\n"
                      << "  arg5 outputBuffer = shared mapping @ 0x" << std::hex << std::uppercase
                      << reinterpret_cast<std::uintptr_t>(mappedData) << std::dec << "\n"
                      << "  arg6 unknown      = " << options.arg6 << " (0x" << std::hex << std::uppercase
                      << options.arg6 << std::dec << ") experimental\n"
                      << "  gp200Size         = " << (options.gp200Size ? "ON (2048->1024 model-length patch)" : "off") << "\n"
                      << "  gp200Rate         = " << (options.gp200Rate ? "ON (CLO-stage 44100->48000 patch)" : "off") << "\n";
        }

        exceptionCode = invokeDataWithSeh(fn, inputWav.c_str(), outputWav.c_str(), inputNam.c_str(),
                                          mappedData, options.arg6, &apiReturn);
    } else {
        auto fn = reinterpret_cast<NamConvertCloFn>(GetProcAddress(module, "namConvertClo"));
        if (!fn) {
            std::cerr << "[worker] ERROR export namConvertClo not found.\n";
            return kExitExportMissing;
        }

        if (options.verbose) {
            std::cout << "[worker] ABI hypothesis for namConvertClo (legacy v0.1 path):\n"
                      << "  arg1 context   = nullptr\n"
                      << "  arg2 inputWav  = " << inputWav << "\n"
                      << "  arg3 outputWav = " << outputWav << "\n"
                      << "  arg4 inputNam  = " << inputNam << "\n"
                      << "  arg5 outputClo = " << outputClo << "\n";
        }
        exceptionCode = invokeFileWithSeh(fn, inputWav.c_str(), outputWav.c_str(), inputNam.c_str(),
                                          outputClo.c_str(), &apiReturn);
    }

    if (exceptionCode != 0) {
        std::cerr << "[worker] ERROR API raised SEH exception " << ntc::hex32(exceptionCode) << "\n";
        return kExitSehBase;
    }

    std::cout << "[worker] " << (options.mode == ConvertMode::Data ? "namConvertCloData" : "namConvertClo")
              << " returned " << apiReturn << " (0x" << std::hex << std::uppercase << apiReturn << std::dec << ")";
    if (apiReturn == ntc::kExpectedApiReturn) {
        std::cout << " [matches 0x2288 static-analysis result]";
    }
    std::cout << "\n";

    const int observed = observeWorkerOutput(options, mappedData);
    // Do not FreeLibrary: the conversion work belongs to DLL-created threads and the
    // worker is intentionally disposable. Process exit performs final cleanup.
    return observed;
}

int convert(const ConvertOptions& options) {
    std::error_code ec;
    if (!fs::exists(options.inputNam, ec) || ec) {
        std::cerr << "ERROR: input NAM does not exist: " << ntc::pathToUtf8(options.inputNam) << "\n";
        return kExitUsage;
    }

    if (options.gp200Compact && (options.gp200Size || options.gp200Rate)) {
        std::cerr << "ERROR: --gp200-compact intentionally uses the normal Ampero DSP and cannot be combined with --gp200-size/--gp200-rate.\n";
        return kExitUsage;
    }

    const RuntimePaths runtime = resolveRuntime(options);
    if (!validateRuntimeFiles(runtime)) {
        return kExitRuntimeMissing;
    }

    std::cout << "Mode:             " << modeName(options.mode) << "\n";
    std::cout << "Provider:         " << runtimeKindName(runtime.kind) << "\n";
    std::cout << "Runtime DLL:      " << ntc::pathToUtf8(runtime.dll) << "\n";
    std::cout << "Runtime stimulus: " << ntc::pathToUtf8(runtime.stimulus) << "\n";
    if (options.mode == ConvertMode::Data) {
        std::cout << "arg6:             " << options.arg6 << " (0x" << std::hex << std::uppercase << options.arg6 << std::dec << ")\n";
    }
    if (options.gp200Compact) {
        std::cout << "GP-200 compact:   ON (normal DSP, compact serializer)\n";
    }

    const fs::path work = makeWorkDirectory();
    if (work.empty()) {
        std::cerr << "ERROR: could not create temporary work directory.\n";
        return kExitStageFailure;
    }
    std::cout << "Work directory:   " << ntc::pathToUtf8(work) << "\n";

    WorkerOptions worker;
    std::string error;
    if (!stageInputFiles(runtime, options, work, worker, error)) {
        std::cerr << "ERROR: staging failed: " << error << "\n";
        return kExitStageFailure;
    }

    SharedBuffer shared;
    SharedBuffer* sharedPtr = nullptr;
    if (options.mode == ConvertMode::Data) {
        if (!createSharedBuffer(shared, error)) {
            std::cerr << "ERROR: " << error << "\n";
            return kExitMappingFailure;
        }
        worker.mappingName = shared.name;
        sharedPtr = &shared;
        if (options.verbose) {
            std::cout << "Shared mapping:   " << ntc::toUtf8(shared.name) << "\n";
        }
    }

    bool capturedValid = false;
    const int workerExit = launchWorker(worker, sharedPtr, work, capturedValid);
    if (workerExit != kExitOk) {
        std::cerr << "Conversion worker failed with exit code " << workerExit << ".\n";
        std::cerr << "Research files kept at: " << ntc::pathToUtf8(work) << "\n";
        return workerExit;
    }

    const ntc::CloInfo stagedInfo = ntc::inspectClo(worker.outputClo);
    ntc::printCloInfo(worker.outputClo, stagedInfo);
    if (!stagedInfo.exists || stagedInfo.size != ntc::kExpectedCloSize) {
        std::cerr << "ERROR: staged CLO validation failed.\n";
        std::cerr << "Research files kept at: " << ntc::pathToUtf8(work) << "\n";
        return kExitConversionBadSize;
    }

    if (options.gp200Compact) {
        std::cout << "Applying GP-200 compact serialization (no DSP patch):\n"
                  << "  declared-size 0x2288 -> 0x1288\n"
                  << "  payload-size  0x2200 -> 0x1200\n"
                  << "  model-field   0x0800 -> 0x0400\n"
                  << "  keep block A (128 floats) + first 1024 floats of block B\n"
                  << "  zero trailing 0x1000 bytes and recalculate CRC16\n";
        if (!ntc::makeGp200CompactClo(worker.outputClo, options.outputClo, error)) {
            std::cerr << "ERROR: compact serialization failed: " << error << "\n";
            std::cerr << "Research files kept at: " << ntc::pathToUtf8(work) << "\n";
            return kExitCopyFailure;
        }
    } else if (!ntc::copyFileCreatingParents(worker.outputClo, options.outputClo, error)) {
        std::cerr << "ERROR: " << error << "\n";
        std::cerr << "Research files kept at: " << ntc::pathToUtf8(work) << "\n";
        return kExitCopyFailure;
    }

    std::cout << "\nSUCCESS: experimental CLO created.\n";
    ntc::printCloInfo(options.outputClo, ntc::inspectClo(options.outputClo));

    if (options.keepTemp) {
        std::cout << "Temporary files preserved at: " << ntc::pathToUtf8(work) << "\n";
    } else {
        fs::remove_all(work, ec);
        if (ec && options.verbose) {
            std::cerr << "Warning: could not fully remove temporary directory: " << ec.message() << "\n";
        }
    }
    return kExitOk;
}

bool parseWorkerOptions(int argc, wchar_t** argv, WorkerOptions& out) {
    for (int i = 2; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--mode" && hasValue(i, argc)) {
            const auto mode = parseMode(argv[++i]);
            if (!mode) return false;
            out.mode = *mode;
        } else if (arg == L"--dll" && hasValue(i, argc)) {
            out.dll = argv[++i];
        } else if (arg == L"--provider" && hasValue(i, argc)) {
            const auto kind = parseRuntimeKind(argv[++i]);
            if (!kind) return false;
            out.runtimeKind = *kind;
        } else if (arg == L"--input-wav" && hasValue(i, argc)) {
            out.inputWav = argv[++i];
        } else if (arg == L"--output-wav" && hasValue(i, argc)) {
            out.outputWav = argv[++i];
        } else if (arg == L"--nam" && hasValue(i, argc)) {
            out.inputNam = argv[++i];
        } else if (arg == L"--clo" && hasValue(i, argc)) {
            out.outputClo = argv[++i];
        } else if (arg == L"--mapping" && hasValue(i, argc)) {
            out.mappingName = argv[++i];
        } else if (arg == L"--timeout" && hasValue(i, argc)) {
            const auto parsed = parsePositiveInt(argv[++i]);
            if (!parsed) return false;
            out.timeoutSeconds = *parsed;
        } else if (arg == L"--arg6" && hasValue(i, argc)) {
            const auto parsed = parseUint64(argv[++i]);
            if (!parsed) return false;
            out.arg6 = *parsed;
        } else if (arg == L"--gp200-size" || arg == L"--gp200-probe") {
            out.gp200Size = true;
        } else if (arg == L"--gp200-rate") {
            out.gp200Rate = true;
        } else if (arg == L"--gp200-combined") {
            out.gp200Size = true;
            out.gp200Rate = true;
        } else if (arg == L"--verbose") {
            out.verbose = true;
        } else {
            return false;
        }
    }

    const bool common = !out.dll.empty() && !out.inputWav.empty() && !out.outputWav.empty() && !out.inputNam.empty();
    if (!common) return false;
    return out.mode == ConvertMode::Data ? !out.mappingName.empty() : !out.outputClo.empty();
}

bool parseCommonRuntimeOption(const std::wstring& arg, int& i, int argc, wchar_t** argv, ConvertOptions& out) {
    if (arg == L"--mode" && hasValue(i, argc)) {
        const auto mode = parseMode(argv[++i]);
        if (!mode) return false;
        out.mode = *mode;
        return true;
    }
    if (arg == L"--provider" && hasValue(i, argc)) {
        const auto kind = parseRuntimeKind(argv[++i]);
        if (!kind) return false;
        out.runtimeKind = *kind;
        return true;
    }
    if (arg == L"--ampero-dir" && hasValue(i, argc)) {
        out.amperoDir = argv[++i];
        return true;
    }
    if (arg == L"--sonicake-dir" && hasValue(i, argc)) {
        out.sonicakeDir = argv[++i];
        return true;
    }
    if (arg == L"--dll" && hasValue(i, argc)) {
        out.dll = argv[++i];
        return true;
    }
    if (arg == L"--stimulus" && hasValue(i, argc)) {
        out.stimulus = argv[++i];
        return true;
    }
    if (arg == L"--timeout" && hasValue(i, argc)) {
        const auto parsed = parsePositiveInt(argv[++i]);
        if (!parsed) return false;
        out.timeoutSeconds = *parsed;
        return true;
    }
    if (arg == L"--arg6" && hasValue(i, argc)) {
        const auto parsed = parseUint64(argv[++i]);
        if (!parsed) return false;
        out.arg6 = *parsed;
        return true;
    }
    if (arg == L"--gp200-size" || arg == L"--gp200-probe") {
        out.gp200Size = true;
        return true;
    }
    if (arg == L"--gp200-rate") {
        out.gp200Rate = true;
        return true;
    }
    if (arg == L"--gp200-combined") {
        out.gp200Size = true;
        out.gp200Rate = true;
        return true;
    }
    if (arg == L"--gp200-compact") {
        out.gp200Compact = true;
        return true;
    }
    if (arg == L"--keep-temp") {
        out.keepTemp = true;
        return true;
    }
    if (arg == L"--verbose") {
        out.verbose = true;
        return true;
    }
    return false;
}


using CloConvertSampleRateFn = void*(__cdecl*)(void*, double);

DWORD invokeCloRateWithSeh(CloConvertSampleRateFn fn,
                           void* inputBuffer,
                           double rate,
                           void** returnedBuffer) noexcept {
#if defined(_MSC_VER)
    __try {
        *returnedBuffer = fn(inputBuffer, rate);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
#else
    *returnedBuffer = fn(inputBuffer, rate);
    return 0;
#endif
}

bool isSupportedRate(double rate) {
    return rate == 44100.0 || rate == 48000.0 || rate == 96000.0;
}

std::optional<double> parseSampleRate(const std::wstring& text) {
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size() || !isSupportedRate(value)) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

int runCloRateWorker(const fs::path& dll, const fs::path& input, double rate,
                     const fs::path& output, bool verbose) {
    std::vector<std::uint8_t> data;
    std::string error;
    if (!ntc::readFileBytes(input, data, error)) {
        std::cerr << "[rate-worker] ERROR: " << error << "\n";
        return kExitUsage;
    }
    if (data.size() != ntc::kExpectedCloSize || std::memcmp(data.data(), "VTSI", 4) != 0) {
        std::cerr << "[rate-worker] ERROR input must be a 0x2288-byte VTSI file.\n";
        return kExitConversionBadSize;
    }

    HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        const DWORD err = GetLastError();
        std::cerr << "[rate-worker] ERROR LoadLibraryExW: " << ntc::win32ErrorMessage(err)
                  << " (" << ntc::hex32(err) << ")\n";
        return kExitDllLoad;
    }
    auto fn = reinterpret_cast<CloConvertSampleRateFn>(GetProcAddress(module, "cloConvertSampleRate"));
    if (!fn) {
        std::cerr << "[rate-worker] ERROR export cloConvertSampleRate not found. "
                     "The analyzed Ampero HTUSBTools.dll does not export it; use Sonicake 5868USB.dll.\n";
        return kExitExportMissing;
    }

    void* returned = nullptr;
    const DWORD exceptionCode = invokeCloRateWithSeh(fn, data.data(), rate, &returned);
    if (exceptionCode != 0) {
        std::cerr << "[rate-worker] ERROR API raised SEH exception " << ntc::hex32(exceptionCode) << "\n";
        return kExitSehBase;
    }
    if (!returned) {
        std::cerr << "[rate-worker] ERROR cloConvertSampleRate returned null.\n";
        return kExitStageFailure;
    }

    const auto* outData = static_cast<const std::uint8_t*>(returned);
    if (std::memcmp(outData, "VTSI", 4) != 0) {
        std::cerr << "[rate-worker] ERROR returned buffer is not VTSI.\n";
        return kExitStageFailure;
    }
    if (!ntc::writeFileBytes(output, outData, static_cast<std::size_t>(ntc::kExpectedCloSize), error)) {
        std::cerr << "[rate-worker] ERROR: " << error << "\n";
        return kExitCopyFailure;
    }
    if (verbose) {
        std::cout << "[rate-worker] input=" << ntc::pathToUtf8(input)
                  << " rate=" << static_cast<int>(rate)
                  << " returned=0x" << std::hex << std::uppercase
                  << reinterpret_cast<std::uintptr_t>(returned) << std::dec << "\n";
    }
    ntc::printCloInfo(output, ntc::inspectClo(output, 32));
    return kExitOk;
}

int launchCloRateWorker(const fs::path& dll, const fs::path& input, double rate,
                        const fs::path& output, bool verbose) {
    const fs::path exe = ntc::executablePath();
    std::vector<std::wstring> args = {
        exe.wstring(), L"--rate-worker",
        L"--dll", dll.wstring(),
        L"--input", input.wstring(),
        L"--rate", std::to_wstring(static_cast<int>(rate)),
        L"--output", output.wstring(),
    };
    if (verbose) args.push_back(L"--verbose");
    std::wstring command;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) command.push_back(L' ');
        command += ntc::quoteWindowsArg(args[i]);
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
        const DWORD err = GetLastError();
        std::cerr << "ERROR launching rate worker: " << ntc::win32ErrorMessage(err) << "\n";
        return kExitWorkerLaunch;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exitCode >= 0xC0000000u) {
        std::cerr << "ERROR rate worker terminated with Windows exception " << ntc::hex32(exitCode) << "\n";
        return static_cast<int>(exitCode & 0x7FFFFFFFu);
    }
    return static_cast<int>(exitCode);
}

bool parseRateRuntimeOptions(int start, int argc, wchar_t** argv, ConvertOptions& options) {
    for (int i = start; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--provider" && hasValue(i, argc)) {
            const auto k = parseRuntimeKind(argv[++i]);
            if (!k) return false;
            options.runtimeKind = *k;
        } else if (arg == L"--sonicake-dir" && hasValue(i, argc)) {
            options.sonicakeDir = argv[++i];
        } else if (arg == L"--ampero-dir" && hasValue(i, argc)) {
            options.amperoDir = argv[++i];
        } else if (arg == L"--dll" && hasValue(i, argc)) {
            options.dll = argv[++i];
        } else if (arg == L"--stimulus" && hasValue(i, argc)) {
            options.stimulus = argv[++i];
        } else if (arg == L"--verbose") {
            options.verbose = true;
        } else {
            return false;
        }
    }
    return true;
}

int commandCloRate(int argc, wchar_t** argv) {
    if (argc < 5) {
        std::cerr << "Usage: NamToClo.exe --clo-rate input.clo 44100|48000|96000 output.clo [--sonicake-dir dir|--dll 5868USB.dll]\n";
        return kExitUsage;
    }
    const fs::path input = argv[2];
    const auto rate = parseSampleRate(argv[3]);
    const fs::path output = argv[4];
    if (!rate) {
        std::cerr << "ERROR supported rates are 44100, 48000 and 96000.\n";
        return kExitUsage;
    }
    ConvertOptions options;
    options.runtimeKind = RuntimeKind::Sonicake;
    if (!parseRateRuntimeOptions(5, argc, argv, options)) {
        std::cerr << "ERROR invalid runtime option for --clo-rate.\n";
        return kExitUsage;
    }
    RuntimePaths runtime = resolveRuntime(options);
    if (runtime.dll.empty()) {
        std::cerr << "ERROR Sonicake 5868USB.dll not found. Use --sonicake-dir or --dll.\n";
        return kExitRuntimeMissing;
    }
    std::cout << "CLO sample-rate conversion via: " << ntc::pathToUtf8(runtime.dll) << "\n";
    return launchCloRateWorker(runtime.dll, input, *rate, output, options.verbose);
}

int commandCloRateMatrix(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::cerr << "Usage: NamToClo.exe --clo-rate-matrix input.clo output-dir [--sonicake-dir dir|--dll 5868USB.dll]\n";
        return kExitUsage;
    }
    const fs::path input = argv[2];
    const fs::path outDir = argv[3];
    ConvertOptions options;
    options.runtimeKind = RuntimeKind::Sonicake;
    if (!parseRateRuntimeOptions(4, argc, argv, options)) {
        std::cerr << "ERROR invalid runtime option for --clo-rate-matrix.\n";
        return kExitUsage;
    }
    RuntimePaths runtime = resolveRuntime(options);
    if (runtime.dll.empty()) {
        std::cerr << "ERROR Sonicake 5868USB.dll not found. Use --sonicake-dir or --dll.\n";
        return kExitRuntimeMissing;
    }
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::cerr << "ERROR cannot create output directory: " << ec.message() << "\n";
        return kExitCopyFailure;
    }
    const std::wstring stem = input.stem().wstring();
    for (int rate : {44100, 48000, 96000}) {
        const fs::path output = outDir / (stem + L"_sr" + std::to_wstring(rate) + L".clo");
        std::cout << "\n=== target " << rate << " Hz ===\n";
        const int rc = launchCloRateWorker(runtime.dll, input, static_cast<double>(rate), output, options.verbose);
        if (rc != kExitOk) return rc;
    }
    std::cout << "\nSUCCESS rate matrix written to: " << ntc::pathToUtf8(outDir) << "\n";
    return kExitOk;
}


int commandCrossRuntime(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::cerr << "Usage: NamToClo.exe --cross-runtime input.nam [--output-dir dir] [--reference file.clo] [--verbose]\n";
        return kExitUsage;
    }

    const fs::path inputNam = argv[2];
    fs::path outputDir = L"cross-runtime-results";
    fs::path referenceClo;
    bool verbose = false;

    for (int i = 3; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--output-dir" && hasValue(i, argc)) {
            outputDir = argv[++i];
        } else if (arg == L"--reference" && hasValue(i, argc)) {
            referenceClo = argv[++i];
        } else if (arg == L"--verbose") {
            verbose = true;
        } else {
            std::cerr << "ERROR invalid option for --cross-runtime: " << ntc::toUtf8(arg) << "\n";
            return kExitUsage;
        }
    }

    std::error_code ec;
    fs::create_directories(outputDir, ec);
    if (ec) {
        std::cerr << "ERROR cannot create output directory: " << ec.message() << "\n";
        return kExitCopyFailure;
    }

    const fs::path ampero = outputDir / L"ampero_raw_2048.clo";
    const fs::path sonicake = outputDir / L"sonicake_raw.clo";

    ConvertOptions amperoOptions;
    amperoOptions.inputNam = inputNam;
    amperoOptions.outputClo = ampero;
    amperoOptions.runtimeKind = RuntimeKind::Ampero;
    amperoOptions.mode = ConvertMode::Data;
    amperoOptions.verbose = verbose;

    std::cout << "\n=== Ampero namConvertCloData ===\n";
    int rc = convert(amperoOptions);
    if (rc != kExitOk) {
        std::cerr << "ERROR Ampero conversion failed: " << rc << "\n";
        return rc;
    }

    ConvertOptions sonicakeOptions;
    sonicakeOptions.inputNam = inputNam;
    sonicakeOptions.outputClo = sonicake;
    sonicakeOptions.runtimeKind = RuntimeKind::Sonicake;
    sonicakeOptions.mode = ConvertMode::Data;
    sonicakeOptions.verbose = verbose;

    std::cout << "\n=== Sonicake namConvertCloData ===\n";
    rc = convert(sonicakeOptions);
    if (rc != kExitOk) {
        std::cerr << "ERROR Sonicake conversion failed: " << rc << "\n";
        return rc;
    }

    ConvertOptions rateOptions;
    rateOptions.runtimeKind = RuntimeKind::Sonicake;
    rateOptions.verbose = verbose;
    const RuntimePaths sonicakeRuntime = resolveRuntime(rateOptions);
    if (sonicakeRuntime.dll.empty()) {
        std::cerr << "ERROR Sonicake 5868USB.dll was not found in runtime/sonicake.\n";
        return kExitRuntimeMissing;
    }

    auto runMatrix = [&](const fs::path& source, const fs::path& dir) -> int {
        std::error_code matrixEc;
        fs::create_directories(dir, matrixEc);
        if (matrixEc) {
            std::cerr << "ERROR cannot create matrix directory: " << matrixEc.message() << "\n";
            return kExitCopyFailure;
        }
        const std::wstring stem = source.stem().wstring();
        for (int rate : {44100, 48000, 96000}) {
            const fs::path output = dir / (stem + L"_sr" + std::to_wstring(rate) + L".clo");
            const int matrixRc = launchCloRateWorker(sonicakeRuntime.dll, source,
                                                     static_cast<double>(rate), output, verbose);
            if (matrixRc != kExitOk) return matrixRc;
        }
        return kExitOk;
    };

    std::cout << "\n=== Sonicake cloConvertSampleRate matrix: Ampero raw ===\n";
    rc = runMatrix(ampero, outputDir / L"ampero_rate_matrix");
    if (rc != kExitOk) return rc;

    std::cout << "\n=== Sonicake cloConvertSampleRate matrix: Sonicake raw ===\n";
    rc = runMatrix(sonicake, outputDir / L"sonicake_rate_matrix");
    if (rc != kExitOk) return rc;

    if (!referenceClo.empty()) {
        if (!fs::exists(referenceClo, ec) || ec) {
            std::cerr << "ERROR reference CLO not found: " << ntc::pathToUtf8(referenceClo) << "\n";
            return kExitUsage;
        }
        std::cout << "\n=== Sonicake cloConvertSampleRate matrix: reference ===\n";
        rc = runMatrix(referenceClo, outputDir / L"reference_rate_matrix");
        if (rc != kExitOk) return rc;
    }

    std::ofstream note(outputDir / L"README_RESULTS.txt", std::ios::binary);
    if (note) {
        note << "NamToCloResearch cross-runtime results\r\n"
             << "Input NAM: " << ntc::pathToUtf8(inputNam) << "\r\n"
             << "Ampero: ampero_raw_2048.clo\r\n"
             << "Sonicake: sonicake_raw.clo\r\n"
             << "Rate matrices: 44100 / 48000 / 96000 via Sonicake cloConvertSampleRate\r\n";
        if (!referenceClo.empty()) note << "Reference: " << ntc::pathToUtf8(referenceClo) << "\r\n";
    }

    std::cout << "\n=== Raw comparison: Ampero vs Sonicake ===\n";
    ntc::printGp200Compare(ampero, sonicake, ntc::compareGp200Clo(ampero, sonicake));
    if (!referenceClo.empty()) {
        std::cout << "\n=== Raw comparison: Ampero vs reference ===\n";
        ntc::printGp200Compare(ampero, referenceClo, ntc::compareGp200Clo(ampero, referenceClo));
        std::cout << "\n=== Raw comparison: Sonicake vs reference ===\n";
        ntc::printGp200Compare(sonicake, referenceClo, ntc::compareGp200Clo(sonicake, referenceClo));
    }

    std::cout << "\nSUCCESS cross-runtime results written to: " << ntc::pathToUtf8(outputDir) << "\n";
    return kExitOk;
}

int commandCheckRuntime(int argc, wchar_t** argv) {
    ConvertOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (!parseCommonRuntimeOption(arg, i, argc, argv, options)) {
            std::cerr << "Unknown or invalid option for --check-runtime: " << ntc::toUtf8(arg) << "\n";
            return kExitUsage;
        }
    }
    return checkRuntime(resolveRuntime(options), options.verbose);
}

int commandConvert(int argc, wchar_t** argv) {
    if (argc < 3) {
        printHelp();
        return kExitUsage;
    }

    ConvertOptions options;
    options.inputNam = argv[1];
    options.outputClo = argv[2];

    for (int i = 3; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (!parseCommonRuntimeOption(arg, i, argc, argv, options)) {
            std::cerr << "Unknown or invalid option: " << ntc::toUtf8(arg) << "\n";
            return kExitUsage;
        }
    }
    return convert(options);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc <= 1) {
        printHelp();
        return kExitUsage;
    }

    const std::wstring first = argv[1];
    if (first == L"--help" || first == L"-h" || first == L"/?") {
        printHelp();
        return kExitOk;
    }
    if (first == L"--version") {
        printBanner();
        return kExitOk;
    }
    if (first == L"--compare-gp200") {
        if (argc != 4) {
            std::cerr << "Usage: NamToClo.exe --compare-gp200 candidate.clo reference.clo\n";
            return kExitUsage;
        }
        const fs::path a = argv[2];
        const fs::path b = argv[3];
        const ntc::Gp200CompareResult result = ntc::compareGp200Clo(a, b);
        ntc::printGp200Compare(a, b, result);
        return result.ok ? kExitOk : kExitUsage;
    }
    if (first == L"--inspect") {
        if (argc != 3) {
            std::cerr << "Usage: NamToClo.exe --inspect file.clo\n";
            return kExitUsage;
        }
        const fs::path path = argv[2];
        const ntc::CloInfo info = ntc::inspectClo(path, 32);
        ntc::printCloInfo(path, info);
        return info.exists ? kExitOk : kExitUsage;
    }
    if (first == L"--clo-rate") {
        return commandCloRate(argc, argv);
    }
    if (first == L"--clo-rate-matrix") {
        return commandCloRateMatrix(argc, argv);
    }
    if (first == L"--rate-worker") {
        fs::path dll, input, output;
        double rate = 0.0;
        bool verbose = false;
        for (int i = 2; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (arg == L"--dll" && hasValue(i, argc)) dll = argv[++i];
            else if (arg == L"--input" && hasValue(i, argc)) input = argv[++i];
            else if (arg == L"--output" && hasValue(i, argc)) output = argv[++i];
            else if (arg == L"--rate" && hasValue(i, argc)) {
                const auto parsed = parseSampleRate(argv[++i]);
                if (!parsed) return kExitUsage;
                rate = *parsed;
            } else if (arg == L"--verbose") verbose = true;
            else return kExitUsage;
        }
        if (dll.empty() || input.empty() || output.empty() || rate == 0.0) return kExitUsage;
        return runCloRateWorker(dll, input, rate, output, verbose);
    }
    if (first == L"--cross-runtime") {
        return commandCrossRuntime(argc, argv);
    }
    if (first == L"--check-runtime") {
        return commandCheckRuntime(argc, argv);
    }
    if (first == L"--worker") {
        WorkerOptions options;
        if (!parseWorkerOptions(argc, argv, options)) {
            std::cerr << "Invalid internal worker arguments.\n";
            return kExitUsage;
        }
        return runWorker(options);
    }

    return commandConvert(argc, argv);
}
