#include "common.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstring>
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
constexpr int kExitConversionBadSize = 32;
constexpr int kExitCopyFailure = 40;
constexpr int kExitMappingFailure = 41;
constexpr int kExitSehBase = 100;
constexpr std::uint8_t kBufferSentinel = 0xCC;
constexpr std::uint64_t kNamConvertArg6 = 0; // confirmed sweep: 0..3 did not alter VTSI structure/content path

struct RuntimePaths {
    fs::path dll;
    fs::path stimulus;
};

struct ConvertOptions {
    fs::path inputNam;
    fs::path outputClo;
    fs::path amperoDir;
    fs::path dll;
    fs::path stimulus;
    int timeoutSeconds = 180;
    bool keepTemp = false;
    bool verbose = false;
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
    bool preserveCapture = false;
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

void printBanner() {
    std::wcout << L"NamToCloResearch " << ntc::kVersion
               << L" - Ampero NAM->VTSI research tool\n";
}

void printHelp() {
    printBanner();
    std::cout << R"HELP(

USAGE
  NamToClo.exe input.nam output.clo [options]
  NamToClo.exe --check-runtime [options]
  NamToClo.exe --inspect file.clo
  NamToClo.exe --compare-gp200 candidate.clo reference.clo
  NamToClo.exe --help
  NamToClo.exe --version

CONVERSION OPTIONS
  --gp200-compact        After the normal Ampero 2048-coefficient conversion,
                         serialize the GP-200 compact 1024-coefficient shape.
  --timeout <seconds>    Conversion timeout. Default: 180.
  --keep-temp            Keep staged input/output WAV and captured-buffer.bin.
  --verbose              Print worker and ABI diagnostics.

RUNTIME OPTIONS
  --ampero-dir <dir>     Root directory of an Ampero II package/installation.
  --dll <path>           Explicit HTUSBTools.dll path.
  --stimulus <path>      Explicit nam_input_wav.wav path.

DEFAULT RUNTIME LAYOUT
  NamToClo.exe
  runtime\ampero\HTUSBTools.dll
  runtime\ampero\nam_input_wav.wav

NOTES
  - Only namConvertCloData is used. The legacy namConvertClo file API was removed.
  - The sixth native argument is fixed to 0; the old arg6 sweep was removed.
  - Old 2048->1024 DSP and 44.1->48 kHz binary patch probes were removed because
    they do not reproduce the GP-200 algorithm.
  - Sonicake and cloConvertSampleRate probes were removed after confirming that
    Sonicake and Ampero generate byte-identical VTSI for the tested NAMs and that
    sample-rate conversion does not explain the Valeton coefficient difference.
  - Proprietary Hotone files are not distributed by this repository.
)HELP";
}

bool hasValue(int i, int argc) { return i + 1 < argc; }

std::optional<int> parsePositiveInt(const std::wstring& text) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used, 10);
        if (used != text.size() || value <= 0) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

fs::path findFirstExisting(const std::vector<fs::path>& candidates) {
    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && fs::exists(candidate, ec) && !ec) return candidate;
        ec.clear();
    }
    return {};
}

RuntimePaths discoverFromRoot(const fs::path& root) {
    RuntimePaths paths;
    if (root.empty()) return paths;
    paths.dll = findFirstExisting({
        root / L"assets" / L"HTUSBTools.dll",
        root / L"data" / L"flutter_assets" / L"assets" / L"HTUSBTools.dll",
        root / L"HTUSBTools.dll",
    });
    paths.stimulus = findFirstExisting({
        root / L"data" / L"flutter_assets" / L"assets" / L"wavs" / L"nam_input_wav.wav",
        root / L"assets" / L"wavs" / L"nam_input_wav.wav",
        root / L"wavs" / L"nam_input_wav.wav",
        root / L"nam_input_wav.wav",
    });
    return paths;
}

RuntimePaths resolveRuntime(const ConvertOptions& options) {
    RuntimePaths result{options.dll, options.stimulus};

    auto fillMissing = [&result](const RuntimePaths& candidate) {
        if (result.dll.empty()) result.dll = candidate.dll;
        if (result.stimulus.empty()) result.stimulus = candidate.stimulus;
    };
    auto normalize = [](fs::path& path) {
        if (path.empty()) return;
        std::error_code ec;
        auto absolute = fs::absolute(path, ec);
        if (!ec) path = absolute;
    };

    if (!options.amperoDir.empty()) fillMissing(discoverFromRoot(options.amperoDir));

    if (result.dll.empty() || result.stimulus.empty()) {
        wchar_t buffer[32768]{};
        constexpr DWORD count = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
        const DWORD len = GetEnvironmentVariableW(L"AMPERO_II_DIR", buffer, count);
        if (len > 0 && len < count) fillMissing(discoverFromRoot(fs::path(buffer)));
    }

    if (result.dll.empty() || result.stimulus.empty()) {
        const fs::path exe = ntc::executablePath();
        if (!exe.empty()) {
            fillMissing(discoverFromRoot(exe.parent_path() / L"runtime" / L"ampero"));
            if (result.dll.empty() || result.stimulus.empty()) {
                fillMissing(discoverFromRoot(exe.parent_path() / L"runtime"));
            }
        }
    }

    normalize(result.dll);
    normalize(result.stimulus);
    return result;
}

bool validateRuntime(const RuntimePaths& runtime) {
    std::error_code ec;
    if (runtime.dll.empty() || !fs::exists(runtime.dll, ec) || ec) {
        std::cerr << "ERROR: HTUSBTools.dll not found. Expected runtime\\ampero\\HTUSBTools.dll or use --ampero-dir/--dll.\n";
        return false;
    }
    ec.clear();
    if (runtime.stimulus.empty() || !fs::exists(runtime.stimulus, ec) || ec) {
        std::cerr << "ERROR: nam_input_wav.wav not found. Expected runtime\\ampero\\nam_input_wav.wav or use --stimulus.\n";
        return false;
    }
    return true;
}

int checkRuntime(const RuntimePaths& runtime, bool verbose) {
    if (!validateRuntime(runtime)) return kExitRuntimeMissing;

    std::cout << "DLL:      " << ntc::pathToUtf8(runtime.dll) << "\n";
    std::cout << "Stimulus: " << ntc::pathToUtf8(runtime.stimulus) << "\n";

    HMODULE module = LoadLibraryExW(runtime.dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        const DWORD err = GetLastError();
        std::cerr << "ERROR: LoadLibraryExW failed: " << ntc::win32ErrorMessage(err)
                  << " (" << ntc::hex32(err) << ")\n";
        return kExitDllLoad;
    }

    const FARPROC convert = GetProcAddress(module, "namConvertCloData");
    std::cout << "namConvertCloData: " << (convert ? "present" : "MISSING");
    if (verbose && convert) {
        std::cout << " @ 0x" << std::hex << std::uppercase
                  << reinterpret_cast<std::uintptr_t>(convert) << std::dec;
    }
    std::cout << "\n";

    if (verbose) {
        for (const char* name : {"getNormalWav", "initWithWaveDicPath", "initWithWaveDicPathJson"}) {
            std::cout << name << ": " << (GetProcAddress(module, name) ? "present" : "missing") << "\n";
        }
    }

    FreeLibrary(module);
    return convert ? kExitOk : kExitExportMissing;
}

fs::path makeWorkDirectory() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const DWORD pid = GetCurrentProcessId();
    fs::path base = fs::temp_directory_path() / L"NamToCloResearch";
    std::error_code ec;
    fs::create_directories(base, ec);
    fs::path work = base / (L"job-" + std::to_wstring(pid) + L"-" + std::to_wstring(now));
    fs::create_directories(work, ec);
    return ec ? fs::path{} : work;
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
        error = "CreateFileMappingW failed: " + ntc::win32ErrorMessage(code);
        return false;
    }
    out.view = static_cast<std::uint8_t*>(MapViewOfFile(out.mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                                                        ntc::kExpectedCloSize));
    if (!out.view) {
        const DWORD code = GetLastError();
        error = "MapViewOfFile failed: " + ntc::win32ErrorMessage(code);
        return false;
    }
    std::memset(out.view, kBufferSentinel, static_cast<std::size_t>(ntc::kExpectedCloSize));
    return true;
}

bool startsWithVtsi(const std::uint8_t* data) {
    return data && data[0] == 'V' && data[1] == 'T' && data[2] == 'S' && data[3] == 'I';
}

std::size_t changedByteCount(const std::uint8_t* data) {
    std::size_t changed = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(ntc::kExpectedCloSize); ++i) {
        if (data[i] != kBufferSentinel) ++changed;
    }
    return changed;
}

bool stageFiles(const RuntimePaths& runtime, const ConvertOptions& options, const fs::path& work,
                WorkerOptions& worker, std::string& error) {
    worker.dll = runtime.dll;
    worker.inputWav = work / L"nam_input_wav.wav";
    worker.outputWav = work / L"outputFile.wav";
    worker.inputNam = work / L"input.nam";
    worker.outputClo = work / L"output.clo";
    worker.timeoutSeconds = options.timeoutSeconds;
    worker.verbose = options.verbose;
    worker.preserveCapture = options.keepTemp;
    if (!ntc::copyFileCreatingParents(runtime.stimulus, worker.inputWav, error)) return false;
    if (!ntc::copyFileCreatingParents(options.inputNam, worker.inputNam, error)) return false;
    return true;
}

std::wstring makeWorkerCommandLine(const WorkerOptions& worker) {
    std::vector<std::wstring> args = {
        ntc::executablePath().wstring(), L"--worker",
        L"--dll", worker.dll.wstring(),
        L"--input-wav", worker.inputWav.wstring(),
        L"--output-wav", worker.outputWav.wstring(),
        L"--nam", worker.inputNam.wstring(),
        L"--clo", worker.outputClo.wstring(),
        L"--mapping", worker.mappingName,
        L"--timeout", std::to_wstring(worker.timeoutSeconds),
    };
    if (worker.verbose) args.push_back(L"--verbose");

    std::wstring command;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) command.push_back(L' ');
        command += ntc::quoteWindowsArg(args[i]);
    }
    return command;
}

int launchWorker(const WorkerOptions& worker, SharedBuffer& shared, const fs::path& work,
                 bool& capturedValid) {
    capturedValid = false;
    std::wstring command = makeWorkerCommandLine(worker);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    if (worker.verbose) std::cout << "Worker command: " << ntc::toUtf8(command) << "\n";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        const DWORD err = GetLastError();
        std::cerr << "ERROR: CreateProcessW failed: " << ntc::win32ErrorMessage(err) << "\n";
        return kExitWorkerLaunch;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(worker.timeoutSeconds + 15);
    while (std::chrono::steady_clock::now() < deadline) {
        const DWORD wait = WaitForSingleObject(pi.hProcess, 100);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
    }

    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    if (exitCode == STILL_ACTIVE) {
        std::cerr << "ERROR: worker timeout; terminating worker.\n";
        TerminateProcess(pi.hProcess, kExitWorkerTimeout);
        WaitForSingleObject(pi.hProcess, 2000);
        exitCode = kExitWorkerTimeout;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    const std::size_t changed = changedByteCount(shared.view);
    capturedValid = startsWithVtsi(shared.view) && changed > 0;

    std::string error;
    if (capturedValid) {
        if (!ntc::writeFileBytes(worker.outputClo, shared.view,
                                 static_cast<std::size_t>(ntc::kExpectedCloSize), error)) {
            std::cerr << "ERROR: could not materialize captured VTSI: " << error << "\n";
            capturedValid = false;
        }
    }
    if (worker.verbose || worker.preserveCapture || exitCode != kExitOk) {
        const fs::path captured = work / L"captured-buffer.bin";
        if (!ntc::writeFileBytes(captured, shared.view,
                                 static_cast<std::size_t>(ntc::kExpectedCloSize), error)) {
            std::cerr << "Warning: could not save captured-buffer.bin: " << error << "\n";
        }
        std::cout << "Captured changed bytes: " << changed << "/" << ntc::kExpectedCloSize
                  << (capturedValid ? " [VTSI]\n" : "\n");
    }

    if (capturedValid) return kExitOk;
    if (exitCode >= 0xC0000000u) {
        std::cerr << "ERROR: worker terminated with Windows exception " << ntc::hex32(exitCode) << "\n";
        return static_cast<int>(exitCode & 0x7FFFFFFFu);
    }
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
            else {
                lastChanged = changed;
                stablePolls = 0;
            }
            if (startsWithVtsi(mappedData) && stablePolls >= 10) return kExitOk;
        }
        std::this_thread::sleep_for(100ms);
    }
    std::cerr << "[worker] ERROR: conversion observation timed out.\n";
    return kExitConversionTimeout;
}

int runWorker(const WorkerOptions& options) {
    HMODULE module = LoadLibraryExW(options.dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        const DWORD err = GetLastError();
        std::cerr << "[worker] ERROR LoadLibraryExW: " << ntc::win32ErrorMessage(err)
                  << " (" << ntc::hex32(err) << ")\n";
        return kExitDllLoad;
    }

    auto fn = reinterpret_cast<NamConvertCloDataFn>(GetProcAddress(module, "namConvertCloData"));
    if (!fn) {
        std::cerr << "[worker] ERROR: namConvertCloData export not found.\n";
        return kExitExportMissing;
    }

    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, options.mappingName.c_str());
    if (!mapping) {
        std::cerr << "[worker] ERROR: OpenFileMappingW failed.\n";
        return kExitMappingFailure;
    }
    auto* mappedData = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                                                                ntc::kExpectedCloSize));
    if (!mappedData) {
        CloseHandle(mapping);
        std::cerr << "[worker] ERROR: MapViewOfFile failed.\n";
        return kExitMappingFailure;
    }

    const std::string inputWav = ntc::pathToUtf8(options.inputWav);
    const std::string outputWav = ntc::pathToUtf8(options.outputWav);
    const std::string inputNam = ntc::pathToUtf8(options.inputNam);

    if (options.verbose) {
        std::cout << "[worker] namConvertCloData ABI used:\n"
                  << "  arg1 context      = nullptr\n"
                  << "  arg2 inputWav     = " << inputWav << "\n"
                  << "  arg3 outputWav    = " << outputWav << "\n"
                  << "  arg4 inputNam     = " << inputNam << "\n"
                  << "  arg5 outputBuffer = shared 0x2288-byte mapping\n"
                  << "  arg6              = 0 (fixed)\n";
    }

    std::uint32_t apiReturn = 0;
    const DWORD exceptionCode = invokeDataWithSeh(fn, inputWav.c_str(), outputWav.c_str(),
                                                   inputNam.c_str(), mappedData, &apiReturn);
    if (exceptionCode != 0) {
        std::cerr << "[worker] ERROR: API raised SEH exception " << ntc::hex32(exceptionCode) << "\n";
        return kExitSehBase;
    }

    std::cout << "[worker] namConvertCloData returned " << apiReturn;
    if (apiReturn == ntc::kExpectedApiReturn) std::cout << " (0x2288)";
    std::cout << "\n";

    return observeWorkerOutput(options, mappedData);
}

bool parseRuntimeOption(const std::wstring& arg, int& i, int argc, wchar_t** argv,
                        ConvertOptions& options) {
    if (arg == L"--ampero-dir" && hasValue(i, argc)) {
        options.amperoDir = argv[++i]; return true;
    }
    if (arg == L"--dll" && hasValue(i, argc)) {
        options.dll = argv[++i]; return true;
    }
    if (arg == L"--stimulus" && hasValue(i, argc)) {
        options.stimulus = argv[++i]; return true;
    }
    if (arg == L"--timeout" && hasValue(i, argc)) {
        const auto value = parsePositiveInt(argv[++i]);
        if (!value) return false;
        options.timeoutSeconds = *value; return true;
    }
    if (arg == L"--keep-temp") { options.keepTemp = true; return true; }
    if (arg == L"--verbose") { options.verbose = true; return true; }
    if (arg == L"--gp200-compact") { options.gp200Compact = true; return true; }
    return false;
}

bool parseWorkerOptions(int argc, wchar_t** argv, WorkerOptions& options) {
    for (int i = 2; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--dll" && hasValue(i, argc)) options.dll = argv[++i];
        else if (arg == L"--input-wav" && hasValue(i, argc)) options.inputWav = argv[++i];
        else if (arg == L"--output-wav" && hasValue(i, argc)) options.outputWav = argv[++i];
        else if (arg == L"--nam" && hasValue(i, argc)) options.inputNam = argv[++i];
        else if (arg == L"--clo" && hasValue(i, argc)) options.outputClo = argv[++i];
        else if (arg == L"--mapping" && hasValue(i, argc)) options.mappingName = argv[++i];
        else if (arg == L"--timeout" && hasValue(i, argc)) {
            const auto value = parsePositiveInt(argv[++i]);
            if (!value) return false;
            options.timeoutSeconds = *value;
        } else if (arg == L"--verbose") options.verbose = true;
        else return false;
    }
    return !options.dll.empty() && !options.inputWav.empty() && !options.outputWav.empty()
        && !options.inputNam.empty() && !options.outputClo.empty() && !options.mappingName.empty();
}

int convert(const ConvertOptions& options) {
    std::error_code ec;
    if (!fs::exists(options.inputNam, ec) || ec) {
        std::cerr << "ERROR: input NAM does not exist: " << ntc::pathToUtf8(options.inputNam) << "\n";
        return kExitUsage;
    }

    const RuntimePaths runtime = resolveRuntime(options);
    if (!validateRuntime(runtime)) return kExitRuntimeMissing;

    const fs::path work = makeWorkDirectory();
    if (work.empty()) {
        std::cerr << "ERROR: could not create temporary work directory.\n";
        return kExitStageFailure;
    }

    WorkerOptions worker;
    std::string error;
    if (!stageFiles(runtime, options, work, worker, error)) {
        std::cerr << "ERROR: staging failed: " << error << "\n";
        return kExitStageFailure;
    }

    SharedBuffer shared;
    if (!createSharedBuffer(shared, error)) {
        std::cerr << "ERROR: " << error << "\n";
        return kExitMappingFailure;
    }
    worker.mappingName = shared.name;

    if (options.verbose) {
        std::cout << "Runtime DLL:      " << ntc::pathToUtf8(runtime.dll) << "\n"
                  << "Runtime stimulus: " << ntc::pathToUtf8(runtime.stimulus) << "\n"
                  << "Work directory:   " << ntc::pathToUtf8(work) << "\n";
    }

    bool capturedValid = false;
    const int workerExit = launchWorker(worker, shared, work, capturedValid);
    if (workerExit != kExitOk || !capturedValid) {
        std::cerr << "ERROR: conversion worker failed with exit code " << workerExit << ".\n"
                  << "Research files kept at: " << ntc::pathToUtf8(work) << "\n";
        return workerExit != kExitOk ? workerExit : kExitConversionBadSize;
    }

    const ntc::CloInfo stagedInfo = ntc::inspectClo(worker.outputClo);
    if (!stagedInfo.exists || stagedInfo.size != ntc::kExpectedCloSize || stagedInfo.magic != "VTSI") {
        std::cerr << "ERROR: captured CLO is not a valid 0x2288-byte VTSI.\n";
        return kExitConversionBadSize;
    }

    if (options.gp200Compact) {
        if (!ntc::makeGp200CompactClo(worker.outputClo, options.outputClo, error)) {
            std::cerr << "ERROR: GP-200 compact serialization failed: " << error << "\n";
            return kExitCopyFailure;
        }
    } else if (!ntc::copyFileCreatingParents(worker.outputClo, options.outputClo, error)) {
        std::cerr << "ERROR: " << error << "\n";
        return kExitCopyFailure;
    }

    std::cout << "SUCCESS: " << ntc::pathToUtf8(options.outputClo) << "\n";
    ntc::printCloInfo(options.outputClo, ntc::inspectClo(options.outputClo));

    if (options.keepTemp) {
        std::cout << "Temporary research files kept at: " << ntc::pathToUtf8(work) << "\n";
    } else {
        fs::remove_all(work, ec);
        if (ec && options.verbose) std::cerr << "Warning: could not remove temp directory: " << ec.message() << "\n";
    }
    return kExitOk;
}

int commandCheckRuntime(int argc, wchar_t** argv) {
    ConvertOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (!parseRuntimeOption(arg, i, argc, argv, options) || arg == L"--gp200-compact" || arg == L"--keep-temp") {
            std::cerr << "Invalid option for --check-runtime: " << ntc::toUtf8(arg) << "\n";
            return kExitUsage;
        }
    }
    return checkRuntime(resolveRuntime(options), options.verbose);
}

int commandConvert(int argc, wchar_t** argv) {
    if (argc < 3) { printHelp(); return kExitUsage; }
    ConvertOptions options;
    options.inputNam = argv[1];
    options.outputClo = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (!parseRuntimeOption(arg, i, argc, argv, options)) {
            std::cerr << "Unknown or invalid option: " << ntc::toUtf8(arg) << "\n";
            return kExitUsage;
        }
    }
    return convert(options);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc <= 1) { printHelp(); return kExitUsage; }

    const std::wstring first = argv[1];
    if (first == L"--help" || first == L"-h" || first == L"/?") { printHelp(); return kExitOk; }
    if (first == L"--version") { printBanner(); return kExitOk; }
    if (first == L"--inspect") {
        if (argc != 3) {
            std::cerr << "Usage: NamToClo.exe --inspect file.clo\n";
            return kExitUsage;
        }
        const auto info = ntc::inspectClo(argv[2], 32);
        ntc::printCloInfo(argv[2], info);
        return info.exists ? kExitOk : kExitUsage;
    }
    if (first == L"--compare-gp200") {
        if (argc != 4) {
            std::cerr << "Usage: NamToClo.exe --compare-gp200 candidate.clo reference.clo\n";
            return kExitUsage;
        }
        const auto result = ntc::compareGp200Clo(argv[2], argv[3]);
        ntc::printGp200Compare(argv[2], argv[3], result);
        return result.ok ? kExitOk : kExitUsage;
    }
    if (first == L"--check-runtime") return commandCheckRuntime(argc, argv);
    if (first == L"--worker") {
        WorkerOptions options;
        if (!parseWorkerOptions(argc, argv, options)) return kExitUsage;
        return runWorker(options);
    }
    return commandConvert(argc, argv);
}
