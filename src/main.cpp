#include "common.hpp"

#include <windows.h>

#include <algorithm>
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
    ConvertMode mode = ConvertMode::Data;
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
    std::wcout << L"NamToCloResearch " << ntc::kVersion << L" - experimental Ampero NAM->CLO probe\n";
}

const char* modeName(ConvertMode mode) {
    return mode == ConvertMode::Data ? "data" : "file";
}

void printHelp() {
    printBanner();
    std::cout << R"HELP(

USAGE
  NamToClo.exe input.nam output.clo [options]
  NamToClo.exe --check-runtime [options]
  NamToClo.exe --inspect file.clo
  NamToClo.exe --help
  NamToClo.exe --version

OPTIONS
  --mode data|file       Conversion API to probe. Default: data.
                         data = namConvertCloData + shared 0x2288-byte buffer.
                         file = legacy namConvertClo path used by v0.1.
  --ampero-dir <dir>     Root directory of an Ampero II installation/package.
  --dll <path>           Explicit path to HTUSBTools.dll.
  --stimulus <path>      Explicit path to nam_input_wav.wav.
  --timeout <seconds>    Conversion timeout. Default: 180.
  --keep-temp            Preserve staged files and captured-buffer.bin.
  --verbose              Print additional research diagnostics.

RUNTIME DISCOVERY
  Resolution order is:
    1. --dll / --stimulus
    2. --ampero-dir
    3. AMPERO_II_DIR environment variable
    4. runtime/ next to NamToClo.exe
    5. runtime/ in the current working directory

IMPORTANT
  v0.2 defaults to namConvertCloData. Static analysis strongly supports arg5 as
  a caller-owned 0x2288-byte destination buffer. arg6 is still experimental and
  is intentionally passed as zero. The worker remains isolated so a DLL fast-fail
  cannot terminate the parent process. The proprietary Hotone DLL and WAV are not
  distributed by this repository.
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

RuntimePaths discoverFromRoot(const fs::path& root) {
    RuntimePaths paths;
    if (root.empty()) {
        return paths;
    }

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

    auto normalize = [](fs::path& path) {
        if (path.empty()) {
            return;
        }
        std::error_code ec;
        const fs::path absolute = fs::absolute(path, ec);
        if (!ec) {
            path = absolute;
        }
    };

    auto fillMissing = [&result](const RuntimePaths& candidate) {
        if (result.dll.empty()) {
            result.dll = candidate.dll;
        }
        if (result.stimulus.empty()) {
            result.stimulus = candidate.stimulus;
        }
    };

    if (!options.amperoDir.empty()) {
        fillMissing(discoverFromRoot(options.amperoDir));
    }

    if (result.dll.empty() || result.stimulus.empty()) {
        wchar_t buffer[32768]{};
        constexpr DWORD bufferCount = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
        const DWORD len = GetEnvironmentVariableW(L"AMPERO_II_DIR", buffer, bufferCount);
        if (len > 0 && len < bufferCount) {
            fillMissing(discoverFromRoot(fs::path(buffer)));
        }
    }

    if (result.dll.empty() || result.stimulus.empty()) {
        const fs::path exe = ntc::executablePath();
        if (!exe.empty()) {
            fillMissing(discoverFromRoot(exe.parent_path() / L"runtime"));
        }
    }

    if (result.dll.empty() || result.stimulus.empty()) {
        fillMissing(discoverFromRoot(fs::current_path() / L"runtime"));
    }

    normalize(result.dll);
    normalize(result.stimulus);
    return result;
}

bool validateRuntimeFiles(const RuntimePaths& paths) {
    std::error_code ec;
    if (paths.dll.empty() || !fs::exists(paths.dll, ec) || ec) {
        std::cerr << "ERROR: HTUSBTools.dll was not found. Use --ampero-dir or --dll.\n";
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
        L"--input-wav", worker.inputWav.wstring(),
        L"--output-wav", worker.outputWav.wstring(),
        L"--nam", worker.inputNam.wstring(),
        L"--clo", worker.outputClo.wstring(),
        L"--timeout", std::to_wstring(worker.timeoutSeconds),
    };
    if (worker.mode == ConvertMode::Data) {
        args.push_back(L"--mapping");
        args.push_back(worker.mappingName);
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

int runWorker(const WorkerOptions& options) {
    std::cout << "[worker] loading: " << ntc::pathToUtf8(options.dll) << "\n";
    HMODULE module = LoadLibraryExW(options.dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        const DWORD err = GetLastError();
        std::cerr << "[worker] ERROR LoadLibraryExW: " << ntc::win32ErrorMessage(err)
                  << " (" << ntc::hex32(err) << ")\n";
        return kExitDllLoad;
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
                      << "  arg6 unknown      = 0 (experimental)\n";
        }

        exceptionCode = invokeDataWithSeh(fn, inputWav.c_str(), outputWav.c_str(), inputNam.c_str(),
                                          mappedData, 0, &apiReturn);
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

    const RuntimePaths runtime = resolveRuntime(options);
    if (!validateRuntimeFiles(runtime)) {
        return kExitRuntimeMissing;
    }

    std::cout << "Mode:             " << modeName(options.mode) << "\n";
    std::cout << "Runtime DLL:      " << ntc::pathToUtf8(runtime.dll) << "\n";
    std::cout << "Runtime stimulus: " << ntc::pathToUtf8(runtime.stimulus) << "\n";

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

    if (!ntc::copyFileCreatingParents(worker.outputClo, options.outputClo, error)) {
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
    if (arg == L"--ampero-dir" && hasValue(i, argc)) {
        out.amperoDir = argv[++i];
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
