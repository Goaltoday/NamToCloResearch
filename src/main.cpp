#include "common.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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
constexpr int kExitSehBase = 100;

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
};

struct WorkerOptions {
    fs::path dll;
    fs::path inputWav;
    fs::path outputWav;
    fs::path inputNam;
    fs::path outputClo;
    int timeoutSeconds = 180;
    bool verbose = false;
};

void printBanner() {
    std::wcout << L"NamToCloResearch " << ntc::kVersion << L" - experimental Ampero NAM->CLO probe\n";
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
  --ampero-dir <dir>     Root directory of an Ampero II installation/package.
  --dll <path>           Explicit path to HTUSBTools.dll.
  --stimulus <path>      Explicit path to nam_input_wav.wav.
  --timeout <seconds>    Conversion timeout. Default: 180.
  --keep-temp            Preserve the staged temporary files.
  --verbose              Print additional research diagnostics.

RUNTIME DISCOVERY
  Resolution order is:
    1. --dll / --stimulus
    2. --ampero-dir
    3. AMPERO_II_DIR environment variable
    4. runtime/ next to NamToClo.exe
    5. runtime/ in the current working directory

IMPORTANT
  This is a research probe. The five-argument ABI and parameter meaning used by
  this build are strongly supported by static analysis but still require dynamic
  validation. The proprietary Hotone DLL and stimulus WAV are NOT distributed
  by this repository.
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

    const std::wstring dllPathW = paths.dll.wstring();
    HMODULE module = LoadLibraryExW(dllPathW.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
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

    bool missing = false;
    for (const char* name : exports) {
        const FARPROC proc = GetProcAddress(module, name);
        std::cout << "  " << name << ": " << (proc ? "present" : "MISSING");
        if (verbose && proc) {
            std::cout << " @ 0x" << std::hex << std::uppercase
                      << reinterpret_cast<std::uintptr_t>(proc) << std::dec;
        }
        std::cout << "\n";
        if (!proc && std::string(name) == "namConvertClo") {
            missing = true;
        }
    }

    FreeLibrary(module);
    return missing ? kExitExportMissing : kExitOk;
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

    if (!ntc::copyFileCreatingParents(runtime.stimulus, worker.inputWav, error)) {
        return false;
    }
    if (!ntc::copyFileCreatingParents(options.inputNam, worker.inputNam, error)) {
        return false;
    }
    return true;
}

std::wstring makeWorkerCommandLine(const WorkerOptions& worker) {
    const fs::path exe = ntc::executablePath();
    std::vector<std::wstring> args = {
        exe.wstring(),
        L"--worker",
        L"--dll", worker.dll.wstring(),
        L"--input-wav", worker.inputWav.wstring(),
        L"--output-wav", worker.outputWav.wstring(),
        L"--nam", worker.inputNam.wstring(),
        L"--clo", worker.outputClo.wstring(),
        L"--timeout", std::to_wstring(worker.timeoutSeconds),
    };
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

int launchWorker(const WorkerOptions& worker) {
    std::wstring command = makeWorkerCommandLine(worker);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (worker.verbose) {
        std::cout << "Worker command: " << ntc::toUtf8(command) << "\n";
    }

    const BOOL ok = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!ok) {
        const DWORD err = GetLastError();
        std::cerr << "ERROR: CreateProcessW failed: " << ntc::win32ErrorMessage(err)
                  << " (" << ntc::hex32(err) << ")\n";
        return kExitWorkerLaunch;
    }

    const DWORD waitMs = static_cast<DWORD>((worker.timeoutSeconds + 15) * 1000);
    const DWORD wait = WaitForSingleObject(pi.hProcess, waitMs);
    if (wait == WAIT_TIMEOUT) {
        std::cerr << "ERROR: Worker exceeded the parent timeout and will be terminated.\n";
        TerminateProcess(pi.hProcess, static_cast<UINT>(kExitWorkerTimeout));
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return kExitWorkerTimeout;
    }

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode)) {
        exitCode = static_cast<DWORD>(kExitWorkerLaunch);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode >= 0xC0000000u) {
        std::cerr << "ERROR: Worker terminated with Windows exception " << ntc::hex32(exitCode)
                  << ". This is useful evidence that the current ABI hypothesis needs adjustment.\n";
        return static_cast<int>(exitCode & 0x7FFFFFFFu);
    }
    return static_cast<int>(exitCode);
}

using NamConvertCloFn = std::uint32_t(__cdecl*)(void*, const char*, const char*, const char*, const char*);

// Keep SEH in a tiny function with no C++ objects that require stack unwinding.
DWORD invokeWithSeh(NamConvertCloFn fn,
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

int runWorker(const WorkerOptions& options) {
    std::cout << "[worker] loading: " << ntc::pathToUtf8(options.dll) << "\n";

    const std::wstring dllPathW = options.dll.wstring();
    HMODULE module = LoadLibraryExW(dllPathW.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        const DWORD err = GetLastError();
        std::cerr << "[worker] ERROR LoadLibraryExW: " << ntc::win32ErrorMessage(err)
                  << " (" << ntc::hex32(err) << ")\n";
        return kExitDllLoad;
    }

    auto fn = reinterpret_cast<NamConvertCloFn>(GetProcAddress(module, "namConvertClo"));
    if (!fn) {
        std::cerr << "[worker] ERROR export namConvertClo not found.\n";
        FreeLibrary(module);
        return kExitExportMissing;
    }

    const std::string inputWav = ntc::pathToUtf8(options.inputWav);
    const std::string outputWav = ntc::pathToUtf8(options.outputWav);
    const std::string inputNam = ntc::pathToUtf8(options.inputNam);
    const std::string outputClo = ntc::pathToUtf8(options.outputClo);

    if (options.verbose) {
        std::cout << "[worker] ABI hypothesis:\n"
                  << "  arg1 context   = nullptr\n"
                  << "  arg2 inputWav  = " << inputWav << "\n"
                  << "  arg3 outputWav = " << outputWav << "\n"
                  << "  arg4 inputNam  = " << inputNam << "\n"
                  << "  arg5 outputClo = " << outputClo << "\n";
    }

    std::uint32_t apiReturn = 0;
    const DWORD exceptionCode = invokeWithSeh(fn,
                                               inputWav.c_str(),
                                               outputWav.c_str(),
                                               inputNam.c_str(),
                                               outputClo.c_str(),
                                               &apiReturn);
    if (exceptionCode != 0) {
        std::cerr << "[worker] ERROR namConvertClo raised SEH exception " << ntc::hex32(exceptionCode) << "\n";
        FreeLibrary(module);
        return kExitSehBase;
    }

    std::cout << "[worker] namConvertClo returned " << apiReturn
              << " (0x" << std::hex << std::uppercase << apiReturn << std::dec << ")";
    if (apiReturn == ntc::kExpectedApiReturn) {
        std::cout << " [matches 0x2288 static-analysis result]";
    } else {
        std::cout << " [unexpected; continuing observation]";
    }
    std::cout << "\n";

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.timeoutSeconds);
    std::uint64_t lastSize = UINT64_MAX;
    int stableCount = 0;
    bool sawOutputWav = false;

    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code ec;
        if (!sawOutputWav && fs::exists(options.outputWav, ec) && !ec) {
            sawOutputWav = true;
            std::cout << "[worker] observed output WAV: " << ntc::pathToUtf8(options.outputWav) << "\n";
        }
        ec.clear();

        if (fs::exists(options.outputClo, ec) && !ec) {
            const auto size = fs::file_size(options.outputClo, ec);
            if (!ec) {
                if (size == lastSize) {
                    ++stableCount;
                } else {
                    if (options.verbose) {
                        std::cout << "[worker] CLO size now " << size << " bytes\n";
                    }
                    lastSize = size;
                    stableCount = 0;
                }

                if (size == ntc::kExpectedCloSize && stableCount >= 5) {
                    const ntc::CloInfo info = ntc::inspectClo(options.outputClo);
                    ntc::printCloInfo(options.outputClo, info);
                    // The conversion thread belongs to this DLL. We intentionally keep the DLL
                    // loaded until the worker process exits instead of calling FreeLibrary here.
                    return kExitOk;
                }
            }
        }
        std::this_thread::sleep_for(100ms);
    }

    const ntc::CloInfo finalInfo = ntc::inspectClo(options.outputClo);
    if (!finalInfo.exists) {
        std::cerr << "[worker] ERROR timeout: no CLO file appeared.\n";
        return kExitConversionNoClo;
    }
    ntc::printCloInfo(options.outputClo, finalInfo);
    if (finalInfo.size != ntc::kExpectedCloSize) {
        std::cerr << "[worker] ERROR CLO appeared but size is not 0x2288.\n";
        return kExitConversionBadSize;
    }
    return kExitConversionTimeout;
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
        if (!options.keepTemp) {
            fs::remove_all(work, ec);
        }
        return kExitStageFailure;
    }

    const int workerExit = launchWorker(worker);
    if (workerExit != kExitOk) {
        std::cerr << "Conversion worker failed with exit code " << workerExit << ".\n";
        std::cerr << "Research files kept at: " << ntc::pathToUtf8(work) << "\n";
        return workerExit;
    }

    const ntc::CloInfo stagedInfo = ntc::inspectClo(worker.outputClo);
    ntc::printCloInfo(worker.outputClo, stagedInfo);
    if (!stagedInfo.exists || stagedInfo.size != ntc::kExpectedCloSize) {
        std::cerr << "ERROR: worker returned success but staged CLO validation failed.\n";
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
        if (arg == L"--dll" && hasValue(i, argc)) {
            out.dll = argv[++i];
        } else if (arg == L"--input-wav" && hasValue(i, argc)) {
            out.inputWav = argv[++i];
        } else if (arg == L"--output-wav" && hasValue(i, argc)) {
            out.outputWav = argv[++i];
        } else if (arg == L"--nam" && hasValue(i, argc)) {
            out.inputNam = argv[++i];
        } else if (arg == L"--clo" && hasValue(i, argc)) {
            out.outputClo = argv[++i];
        } else if (arg == L"--timeout" && hasValue(i, argc)) {
            const auto parsed = parsePositiveInt(argv[++i]);
            if (!parsed) {
                return false;
            }
            out.timeoutSeconds = *parsed;
        } else if (arg == L"--verbose") {
            out.verbose = true;
        } else {
            return false;
        }
    }
    return !out.dll.empty() && !out.inputWav.empty() && !out.outputWav.empty() &&
           !out.inputNam.empty() && !out.outputClo.empty();
}

bool parseCommonRuntimeOption(const std::wstring& arg, int& i, int argc, wchar_t** argv, ConvertOptions& out) {
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
        if (!parsed) {
            return false;
        }
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
