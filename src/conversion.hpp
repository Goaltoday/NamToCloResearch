#pragma once

#include "stimulus.hpp"
#include "corrective_ir.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ntc {

namespace fs = std::filesystem;

struct RuntimePaths {
    fs::path dll;
    fs::path legacyStimulus;
    fs::path cleanStimulus;
    fs::path distStimulus;
    fs::path presetAudio;
};

struct ConversionResult {
    bool ok = false;
    int exitCode = 0;
    std::string error;
    fs::path inputNam;
    fs::path ampero2048;
    fs::path gp2001024;
};

struct BatchConversionResult {
    bool ok = false;
    std::size_t total = 0;
    std::size_t succeeded = 0;
    std::size_t failed = 0;
    std::vector<ConversionResult> items;
};

using StatusCallback = std::function<void(const std::wstring&)>;

RuntimePaths resolveDefaultRuntime();
// Base runtime validation deliberately checks only the files required by
// Legacy mode so v1.1 installations remain usable unchanged.
bool validateRuntime(const RuntimePaths& runtime, std::string& error);
ConversionResult convertNamToBoth(const fs::path& inputNam,
                                  const fs::path& outputDirectory,
                                  StimulusConfig stimulus = {},
                                  CorrectiveIrConfig correction = {},
                                  const StatusCallback& status = {});
BatchConversionResult convertNamFolder(const fs::path& inputDirectory,
                                       const fs::path& outputDirectory,
                                       StimulusConfig stimulus = {},
                                       CorrectiveIrConfig correction = {},
                                       const StatusCallback& status = {});

// Internal worker entry used by the same GUI executable in a hidden child process.
int runWorkerCommandLine(int argc, wchar_t** argv);

} // namespace ntc
