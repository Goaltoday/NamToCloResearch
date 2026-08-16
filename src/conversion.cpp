#include "conversion.hpp"
#include "common.hpp"
#include "corrective_ir.hpp"
#include "clo_refiner.hpp"

#include <algorithm>
#include <cwctype>

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
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
constexpr double kA2FullSlimFactor = 1.0;

struct WorkerOptions {
    fs::path dll;
    fs::path inputWav;
    fs::path outputWav;
    fs::path inputNam;
    fs::path outputClo;
    std::wstring mappingName;
    int timeoutSeconds = kTimeoutSeconds;
    bool forceA2Full = true;
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
        L"--timeout", std::to_wstring(w.timeoutSeconds)
    };
    std::vector<std::wstring> finalArgs = args;
    if (w.forceA2Full) finalArgs.push_back(L"--a2-full");
    std::wstring cmd;
    for (std::size_t i = 0; i < finalArgs.size(); ++i) {
        if (i) cmd.push_back(L' ');
        cmd += quoteWindowsArg(finalArgs[i]);
    }
    return cmd;
}

int launchWorker(const WorkerOptions& worker, SharedBuffer& shared, bool& capturedValid, std::string& error) {
    capturedValid = false;
    std::wstring command = makeWorkerCommandLine(worker);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_NO_WINDOW;
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        error = "Could not start conversion worker: " + win32ErrorMessage(GetLastError());
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
        TerminateProcess(pi.hProcess, kExitWorkerTimeout);
        WaitForSingleObject(pi.hProcess, 2000);
        exitCode = kExitWorkerTimeout;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    const std::size_t changed = changedByteCount(shared.view);
    capturedValid = startsWithVtsi(shared.view) && changed > 0;
    if (capturedValid) {
        if (!writeFileBytes(worker.outputClo, shared.view, static_cast<std::size_t>(kExpectedCloSize), error)) {
            capturedValid = false;
        }
    }

    if (capturedValid) return kExitOk;
    if (exitCode >= 0xC0000000u) {
        error = "HTUSBTools worker terminated with Windows exception " + hex32(exitCode);
        return static_cast<int>(exitCode & 0x7FFFFFFFu);
    }
    if (error.empty()) error = "Conversion worker failed (exit code " + std::to_string(exitCode) + ").";
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
        else if (a == L"--timeout" && has(i)) {
            const auto v = positiveInt(argv[++i]); if (!v) return false; w.timeoutSeconds = *v;
        } else if (a == L"--a2-full") w.forceA2Full = true;
        else return false;
    }
    return !w.dll.empty() && !w.inputWav.empty() && !w.outputWav.empty() && !w.inputNam.empty()
        && !w.outputClo.empty() && !w.mappingName.empty();
}

bool isA2SlimmableNam(const fs::path& path) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!readFileBytes(path, bytes, error)) return false;
    static constexpr char kMarker[] = "SlimmableContainer";
    return std::search(bytes.begin(), bytes.end(),
                       reinterpret_cast<const std::uint8_t*>(kMarker),
                       reinterpret_cast<const std::uint8_t*>(kMarker) + sizeof(kMarker) - 1) != bytes.end();
}

using ConvertNamToNambWithSlimFn = const char*(__cdecl*)(const char*, double);

fs::path findGeneratedNamb(const fs::path& inputNam) {
    std::error_code ec;
    fs::path expected = inputNam;
    expected.replace_extension(L".namb");
    if (fs::exists(expected, ec) && !ec) return expected;
    ec.clear();
    const fs::path appended = fs::path(inputNam.wstring() + L".namb");
    if (fs::exists(appended, ec) && !ec) return appended;
    ec.clear();
    for (const auto& entry : fs::directory_iterator(inputNam.parent_path(), ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
        std::wstring ext = entry.path().extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
        if (ext == L".namb") return entry.path();
    }
    return {};
}

bool prepareA2FullModel(HMODULE module, const fs::path& inputNam, fs::path& modelPath, std::string& error) {
    modelPath = inputNam;
    if (!isA2SlimmableNam(inputNam)) return true;

    auto convertFull = reinterpret_cast<ConvertNamToNambWithSlimFn>(
        GetProcAddress(module, "convertNamToNambWithSlim"));
    if (!convertFull) {
        error = "A2 Full requested, but HTUSBTools.dll has no convertNamToNambWithSlim export.";
        return false;
    }

    // Static analysis of this DLL shows SlimmableContainer thresholds are
    // ascending: factor 0.0 selects the first/smallest model, while 1.0 falls
    // through to the last/full model. Never fall back to the original A2 NAM,
    // because that would silently re-introduce the Lite/Slim=0 path.
    const std::string namUtf8 = pathToUtf8(inputNam);
    const char* result = convertFull(namUtf8.c_str(), kA2FullSlimFactor);
    (void)result;

    const fs::path namb = findGeneratedNamb(inputNam);
    if (namb.empty()) {
        error = "HTUSBTools.dll did not produce the Full A2 .namb model (slim factor 1.0).";
        return false;
    }
    modelPath = namb;
    return true;
}

int runWorker(const WorkerOptions& options) {
    HMODULE module = LoadLibraryExW(options.dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) return kExitDllLoad;
    auto fn = reinterpret_cast<NamConvertCloDataFn>(GetProcAddress(module, "namConvertCloData"));
    if (!fn) { FreeLibrary(module); return kExitExportMissing; }

    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, options.mappingName.c_str());
    if (!mapping) { FreeLibrary(module); return kExitMappingFailure; }
    auto* mapped = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, kExpectedCloSize));
    if (!mapped) { CloseHandle(mapping); FreeLibrary(module); return kExitMappingFailure; }

    fs::path modelPath = options.inputNam;
    std::string modelError;
    if (options.forceA2Full && !prepareA2FullModel(module, options.inputNam, modelPath, modelError)) {
        std::cerr << "[worker] ERROR: " << modelError << "\n";
        UnmapViewOfFile(mapped);
        CloseHandle(mapping);
        FreeLibrary(module);
        return kExitStageFailure;
    }

    const std::string inWav = pathToUtf8(options.inputWav);
    const std::string outWav = pathToUtf8(options.outputWav);
    const std::string nam = pathToUtf8(modelPath);
    std::uint32_t apiReturn = 0;
    const DWORD exceptionCode = invokeDataWithSeh(fn, inWav.c_str(), outWav.c_str(), nam.c_str(), mapped, &apiReturn);
    int result = kExitOk;
    if (exceptionCode != 0) result = kExitSehBase;
    else if (apiReturn != kExpectedApiReturn) result = kExitConversionBadSize;
    else result = observeWorkerOutput(options, mapped);

    UnmapViewOfFile(mapped);
    CloseHandle(mapping);
    FreeLibrary(module);
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

    SharedBuffer shared;
    if (!createSharedBuffer(shared, error)) {
        result.exitCode = kExitMappingFailure;
        result.error = error;
        fs::remove_all(work, ec);
        return result;
    }
    worker.mappingName = shared.name;

    if (isA2SlimmableNam(worker.inputNam))
        report(status, L"A2 SlimmableContainer detected: forcing FULL model (slim factor 1.0)...");
    report(status, L"Generating Ampero 2048 CLO...");
    bool capturedValid = false;
    const int workerExit = launchWorker(worker, shared, capturedValid, error);
    if (workerExit != kExitOk || !capturedValid || !valid2048(worker.outputClo)) {
        result.exitCode = workerExit != kExitOk ? workerExit : kExitConversionBadSize;
        result.error = error.empty() ? "The generated Ampero CLO failed validation." : error;
        fs::remove_all(work, ec);
        return result;
    }

    // Preserve the existing conversion path. Refinement is an optional,
    // additional research output generated from the raw official Ampero B2048
    // model and HTUSBTools' own rendered NAM target.
    fs::path refinedWorkClo;
    fs::path bestWorkClo;
    if (refine.enabled) {
        report(status, L"Exact VST CAB Tone Match replication on final 20 seconds (v2.6.0)...");
        refinedWorkClo = work / L"ampero_2048_VST_EXACT_REFINE.clo";
        bestWorkClo = work / L"ampero_2048_VST_EXACT_BEST.clo";
        if (!refineCloBOnly(worker.outputClo, worker.inputWav, worker.outputWav, refinedWorkClo, bestWorkClo,
                         refine, result.refineStats, error, status)
            || !valid2048(refinedWorkClo) || !valid2048(bestWorkClo)) {
            result.exitCode = kExitStageFailure;
            result.error = error.empty() ? "Experimental CLO refinement failed." : error;
            fs::remove_all(work, ec);
            return result;
        }
        const std::wstring refineStatus =
            L"Tone match complete: NMSE " + std::to_wstring(result.refineStats.originalNmse)
            + L" -> " + std::to_wstring(result.refineStats.refinedNmse)
            + L" (" + std::to_wstring(result.refineStats.improvementPercent) + L"% improvement; stimulus "
            + std::to_wstring(result.refineStats.stimulusImprovementPercent) + L"%; tail "
            + std::to_wstring(result.refineStats.tailImprovementPercent) + L"%; MR-STFT "
            + std::to_wstring(result.refineStats.spectralImprovementPercent) + L"%; direct output spectrum "
            + std::to_wstring(result.refineStats.responseSpectralImprovementPercent) + L"%; envelope "
            + std::to_wstring(result.refineStats.envelopeImprovementPercent) + L"%; searched low/mid/high "
            + std::to_wstring(result.refineStats.searchedLowLevelImprovementPercent) + L"/"
            + std::to_wstring(result.refineStats.searchedMidLevelImprovementPercent) + L"/"
            + std::to_wstring(result.refineStats.searchedHighLevelImprovementPercent) + L"%).";
        report(status, refineStatus.c_str());
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
        result.bestAmpero2048 = uniquePath(outDir / (base + L"_Ampero_2048_VST_EXACT_BEST.clo"));
        if (!copyFileCreatingParents(bestWorkClo, result.bestAmpero2048, error)) {
            result.exitCode = kExitCopyFailure; result.error = error; fs::remove_all(work, ec); return result;
        }
        report(status, L"Generating BEST GP-200 1024 compact CLO for audition...");
        result.bestGp2001024 = uniquePath(outDir / (base + L"_GP200_1024_VST_EXACT_BEST.clo"));
        if (!makeGp200CompactClo(bestWorkClo, result.bestGp2001024, error)
            || !valid1024(result.bestGp2001024)) {
            result.exitCode = kExitCopyFailure;
            result.error = error.empty() ? "The BEST GP-200 compact CLO failed validation." : error;
            fs::remove_all(work, ec); return result;
        }

        {
            const fs::path autoIrWork = work / L"auto_tonematch_ir.wav";
            if (fs::exists(autoIrWork)) {
                const fs::path autoIrOut = uniquePath(outDir / (base + L"_auto_tonematch_ir.wav"));
                if (!copyFileCreatingParents(autoIrWork, autoIrOut, error)) {
                    result.exitCode = kExitCopyFailure; result.error = error; fs::remove_all(work, ec); return result;
                }
            }
        }

        result.refinedAmpero2048 = uniquePath(outDir / (base + L"_Ampero_2048_VST_EXACT_REFINE.clo"));
        if (!copyFileCreatingParents(refinedWorkClo, result.refinedAmpero2048, error)) {
            result.exitCode = kExitCopyFailure; result.error = error; fs::remove_all(work, ec); return result;
        }
        report(status, L"Generating refined GP-200 1024 compact CLO...");
        result.refinedGp2001024 = uniquePath(outDir / (base + L"_GP200_1024_VST_EXACT_REFINE.clo"));
        if (!makeGp200CompactClo(refinedWorkClo, result.refinedGp2001024, error)
            || !valid1024(result.refinedGp2001024)) {
            result.exitCode = kExitCopyFailure;
            result.error = error.empty() ? "The refined GP-200 compact CLO failed validation." : error;
            fs::remove_all(work, ec); return result;
        }
    }

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
